#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/irqreturn.h>
#include <linux/mod_devicetable.h>
#include <linux/spinlock.h>
#include <linux/of.h>

#include "sophgo_common.h"
#include "sophgo_veth.h"

static int set_ready_flag(struct veth_dev *vdev);

static inline void intr_clear(struct veth_dev *vdev)
{
	u32 value;

	value = (1 << 15);
	sg_write32(vdev->top_misc_reg, TOP_MISC_GP_REG15_CLR_OFFSET, value);
}

static irqreturn_t veth_irq(int irq, void *id)
{
	struct veth_dev *vdev = id;

	if (atomic_read(&vdev->link)) {
		if (pt_load_rx(vdev->pt)) {
			disable_irq_nosync(irq);
			napi_schedule_irqoff(&vdev->napi);
		}
	}
	intr_clear(vdev);
	return IRQ_HANDLED;
}

static void sg_enable_eth_irq(struct veth_dev *vdev)
{
	u32 intc_enable;
	u32 intc_mask;

	intc_enable = sg_read32(vdev->intc_cfg_reg, 4);
	intc_enable |= (1 << 18);
	sg_write32(vdev->intc_cfg_reg, 0x4, intc_enable);
}

static int notify_host(struct veth_dev *vdev)
{
	int counter;

	if (atomic_read(&vdev->link)) {
		u32 data = sg_read32(vdev->top_misc_reg, TOP_MISC_GP_REG14_STS_OFFSET);

		counter = 0;
		while ((0x4 & data) != 0) {
			data = sg_read32(vdev->top_misc_reg, TOP_MISC_GP_REG14_STS_OFFSET);
			counter++;
			if (counter > 100000) {
				pr_info("notify host timeout!\n");
				return NETDEV_TX_BUSY;
			}
		}
		data = sg_read32(vdev->top_misc_reg, TOP_MISC_GP_REG14_STS_OFFSET);
		data |= 0x4;
		sg_write32(vdev->top_misc_reg, TOP_MISC_GP_REG14_SET_OFFSET, data);
	}
	sg_enable_eth_irq(vdev);
	return NETDEV_TX_OK;
}

static int veth_open(struct net_device *ndev)
{
	struct veth_dev *vdev = netdev_priv(ndev);
	int err;

	intr_clear(vdev);
	if (devm_request_irq(&vdev->pdev->dev, vdev->rx_irq, veth_irq, 0, "veth", vdev)) {
		pr_err("request rx irq failed!\n");
		return 1;
	}
	sg_enable_eth_irq(vdev);
	disable_irq(vdev->rx_irq);
	atomic_set(&vdev->link, false);
	err = set_ready_flag(vdev);
	if (err) {
		pr_err("set ready falg failed!\n");
		return 1;
	}
	netdev_reset_queue(ndev);
	netif_start_queue(ndev);
	napi_enable(&vdev->napi);

	return 0;
}

static int veth_close(struct net_device *ndev)
{
	struct veth_dev *vdev = netdev_priv(ndev);

	napi_disable(&vdev->napi);
	netif_stop_queue(ndev);
	return 0;
}

static netdev_tx_t veth_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct veth_dev *vdev = netdev_priv(ndev);
	int err;
	int ret;

	if (!atomic_read(&vdev->link)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	pt_load_tx(vdev->pt);
	err = pt_send(vdev->pt, skb->data, skb->len);
	if (err < 0)
		return NETDEV_TX_BUSY;

	ret = pt_store_tx(vdev->pt);
	if (ret != NETDEV_TX_OK) {
		ndev->stats.tx_errors += err;
		return NETDEV_TX_BUSY;
	}

	ret = notify_host(vdev);
	if (ret == NETDEV_TX_BUSY)
		return ret;
	++ndev->stats.tx_packets;
	ndev->stats.tx_bytes += err;
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops veth_ops = {
	.ndo_open = veth_open,
	.ndo_stop = veth_close,
	.ndo_start_xmit = veth_xmit,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_change_mtu = eth_change_mtu,
};

static int veth_rx(struct veth_dev *vdev, int limit)
{
	int err;
	struct net_device *ndev;
	struct sk_buff *skb;
	int pkg_len;
	int pkg_cnt;
	struct pt *pt;

	ndev = vdev->ndev;
	pt = vdev->pt;
	pkg_cnt = 0;
	if (!atomic_read(&vdev->link)) {
		pr_info("veth rx not link!\n");
		return pkg_cnt;
	}

	while (pkg_cnt < limit) {
		pkg_len = pt_pkg_len(pt);
		if (pkg_len < 0) {
			napi_complete_done(&vdev->napi, pkg_cnt);
			break;
		}

		skb = netdev_alloc_skb(ndev, pkg_len + NET_IP_ALIGN);
		if (!skb) {
			// pr_err("allocate skb failed with package length %u\n", pkg_len);
			pt_info(pt);
			++ndev->stats.rx_dropped;
			continue;
		}

		skb_reserve(skb, NET_IP_ALIGN);
		err = pt_recv(pt, skb_put(skb, pkg_len), pkg_len);
		/* free space as soon as possible */
		pt_store_rx(pt);
		skb->protocol = eth_type_trans(skb, ndev);
		napi_gro_receive(&vdev->napi, skb);
		++ndev->stats.rx_packets;
		ndev->stats.rx_bytes += err;
	}
	return pkg_cnt;
}

static int veth_napi_poll_rx(struct napi_struct *napi, int budget)
{
	struct veth_dev *vdev;
	int err;

	vdev = container_of(napi, struct veth_dev, napi);
	err = veth_rx(vdev, budget);
	enable_irq(vdev->rx_irq);

	return err;
}

static void net_state_work(struct work_struct *worker)
{
	struct veth_dev *vdev;
	struct net_device *ndev;
	struct device *dev;

	vdev = container_of(worker, struct veth_dev, net_state_worker);
	ndev = vdev->ndev;
	dev = &vdev->pdev->dev;

	while (sg_read32(vdev->shm_cfg_reg, SHM_HANDSHAKE_OFFSET))
		msleep(20);

	sg_write32(vdev->shm_cfg_reg, SHM_HANDSHAKE_OFFSET, DEVICE_READY_FLAG);

	while (sg_read32(vdev->shm_cfg_reg, SHM_HANDSHAKE_OFFSET) != HOST_READY_FLAG)
		msleep(20);
	/* i am ready again */
	sg_write32(vdev->shm_cfg_reg, SHM_HANDSHAKE_OFFSET, DEVICE_READY_FLAG);
	vdev->pt = pt_init(dev, vdev->shm_cfg_reg, vdev);
	if (!vdev->pt) {
		pr_err("package transmiter init failed, try again\n");
		schedule_work(worker);
		return;
	}
	spin_lock_init(&vdev->lock_cdma);
	atomic_set(&vdev->link, true);
	netif_carrier_on(ndev);
	enable_irq(vdev->rx_irq);
	pr_info("connect success!\n");
}

static int set_ready_flag(struct veth_dev *vdev)
{
	INIT_WORK(&vdev->net_state_worker, net_state_work);
	schedule_work(&vdev->net_state_worker);

	return 0;
}

static void unset_ready_flag(struct veth_dev *vdev)
{
	sg_write32(vdev->shm_cfg_reg, SHM_HANDSHAKE_OFFSET, 0);
}

static int sg_veth_get_resource(struct platform_device *pdev, struct veth_dev *vdev)
{
	int err;

	vdev->shm_cfg_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "shm_reg");
	if (!vdev->shm_cfg_res) {
		pr_err("cannot get resource - shm reg base!\n");
		err = -ENODEV;
		return err;
	}
	vdev->shm_cfg_reg = ioremap(vdev->shm_cfg_res->start, vdev->shm_cfg_res->end - vdev->shm_cfg_res->start);
	if (!vdev->shm_cfg_reg) {
		pr_err("map shm cfg reg failed!\n");
		err = -ENOMEM;
		return err;
	}

	vdev->top_misc_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "top_misc");
	if (!vdev->top_misc_res) {
		pr_err("cannot get resource - top misc base!\n");
		err = -ENODEV;
		return err;
	}
	vdev->top_misc_reg = ioremap(vdev->top_misc_res->start, vdev->top_misc_res->end - vdev->top_misc_res->start);
	if (!vdev->top_misc_reg) {
		pr_err("map top misc reg failed!\n");
		err = -ENOMEM;
		return err;
	}

	vdev->cdma_cfg_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cdma_cfg");
	if (!vdev->cdma_cfg_res) {
		pr_err("cannot get resource - cdma reg base!\n");
		err = -ENODEV;
		return err;
	}
	vdev->cdma_cfg_reg = ioremap(vdev->cdma_cfg_res->start, vdev->cdma_cfg_res->end - vdev->cdma_cfg_res->start);
	if (!vdev->cdma_cfg_reg) {
		pr_err("map cdma cfg reg failed!\n");
		err = -ENOMEM;
		return err;
	}
	vdev->intc_cfg_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "intc_cfg");
	if (!vdev->intc_cfg_res) {
		pr_err("cannot get resource - intc reg base!\n");
		err = -ENODEV;
		return err;
	}
	vdev->intc_cfg_reg = ioremap(vdev->intc_cfg_res->start, vdev->intc_cfg_res->end - vdev->intc_cfg_res->start);
	if (!vdev->intc_cfg_reg) {
		pr_err("map intc cfg reg failed!\n");
		err = -ENOMEM;
		return err;
	}

	vdev->rx_irq = platform_get_irq_byname(pdev, "rx");
	if (vdev->rx_irq < 0) {
		pr_err("no rx interrupt resource!\n");
		err = -ENOMEM;
		return err;
	}

	return 0;
}

static int sg_veth_probe(struct platform_device *pdev)
{
	struct device *dev;
	struct veth_dev *vdev;
	struct net_device *ndev;
	int err;
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "sophgon, veth");
	if (!of_device_is_available(node)) {
		pr_err("veth driver node is disable!\n");
		err = -ENODEV;
		return err;
	}

	dev = &pdev->dev;
	dma_set_mask(dev, DMA_BIT_MASK(64));
	ndev = alloc_etherdev(sizeof(struct veth_dev));
	if (!ndev) {
		pr_err("cannot allocate device instance !\n");
		return -ENOMEM;
	}

	ether_setup(ndev);
	strncpy(ndev->name, "veth%d", IFNAMSIZ);
	eth_hw_addr_random(ndev);

	vdev = netdev_priv(ndev);
	vdev->ndev = ndev;

	err = sg_veth_get_resource(pdev, vdev);
	if (err != 0)
		goto err_free_netdev;

	vdev->pdev = pdev;
	platform_set_drvdata(pdev, ndev);

	ndev->netdev_ops = &veth_ops;
	ndev->irq = vdev->rx_irq;
	ndev->mtu = VETH_DEFAULT_MTU;

	netif_napi_add(ndev, &vdev->napi, veth_napi_poll_rx, NAPI_POLL_WEIGHT);
	err = register_netdev(ndev);
	if (err) {
		pr_err("register net device failed!\n");
		goto err_free_netdev;
	}
	return 0;

err_free_netdev:
	free_netdev(ndev);
	return err;
}

static int sg_veth_remove(struct platform_device *pdev)
{
	struct veth_dev *vdev;
	struct net_device *ndev;

	ndev = platform_get_drvdata(pdev);

	netif_carrier_off(ndev);
	if (!ndev)
		return -ENODEV;

	vdev = netdev_priv(ndev);
	disable_irq(vdev->rx_irq);

	unset_ready_flag(vdev);
	return 0;
}

static const struct of_device_id sg_veth_match[] = {
	{.compatible = "sophgon, veth"},
	{},
};

static struct platform_driver sg_veth_driver = {
	.probe = sg_veth_probe,
	.remove = sg_veth_remove,
	.driver = {
		.name = "veth",
		.of_match_table = sg_veth_match,
	},
};

module_platform_driver(sg_veth_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dong Yang <dong.yang@sophgon.com>");
MODULE_DESCRIPTION("Sophgon Virtual Ethernet Driver over PCIe");
