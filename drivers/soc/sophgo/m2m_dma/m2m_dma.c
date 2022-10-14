#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>

static struct class *m2m_dma_class;
static int m2m_dma_count;

struct m2m_dev_data {
	struct platform_device *pdev;
	struct device *cdev;
	dev_t cdevno;
	struct class *cdev_class;

	struct dma_chan *dma_ch;
	dma_addr_t addr_src;
	dma_addr_t addr_dst;
	char *vaddr_src;
	char *vaddr_dst;
	size_t length;
};

static ssize_t addr_src_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct m2m_dev_data *data;

	data = dev_get_drvdata(dev);
	return sprintf(buf, "0x%llx\n", data->addr_src);
}

static ssize_t addr_src_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t size)
{
	struct m2m_dev_data *data;
	unsigned long long val;
	int ret = 0;

	data = dev_get_drvdata(dev);

	ret = kstrtoull(buf, 0, &val);
	if (ret)
		return ret;

	data->addr_src = val;
	return ret ? : size;
}

static DEVICE_ATTR_RW(addr_src);

static ssize_t addr_dst_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct m2m_dev_data *data;

	data = dev_get_drvdata(dev);
	return sprintf(buf, "0x%llx\n", data->addr_dst);
}

static ssize_t addr_dst_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t size)
{
	struct m2m_dev_data *data;
	unsigned long long val;
	int ret = 0;

	data = dev_get_drvdata(dev);

	ret = kstrtoull(buf, 0, &val);
	if (ret)
		return ret;

	data->addr_dst = val;
	return ret ? : size;
}

static DEVICE_ATTR_RW(addr_dst);

static ssize_t length_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct m2m_dev_data *data;

	data = dev_get_drvdata(dev);
	return sprintf(buf, "0x%lx\n", data->length);
}

static ssize_t length_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t size)
{
	struct m2m_dev_data *data;
	struct dma_device *dmadev;
	unsigned long long val;
	int ret = 0;

	data = dev_get_drvdata(dev);
	dmadev = data->dma_ch->device;

	ret = kstrtoull(buf, 0, &val);
	if (ret)
		return ret;

	data->length = val;

	if (data->length != 0) {
		struct page *pg;
		unsigned long pg_offset;
		struct dma_async_tx_descriptor *tx = NULL;
		dma_cookie_t cookie;
		enum dma_ctrl_flags flags = DMA_CTRL_ACK;
		enum dma_status status;

		data->vaddr_src = kmalloc(data->length, GFP_KERNEL);
		data->vaddr_dst = kmalloc(data->length, GFP_KERNEL);
		if (!data->vaddr_src || !data->vaddr_dst) {
			dev_err(dev, "alloc buffer failed\n");
			ret = -ENOMEM;
			goto out;
		}

		pg = virt_to_page(data->vaddr_src);
		pg_offset = offset_in_page(data->vaddr_src);
		data->addr_src = dma_map_page(dmadev->dev, pg, pg_offset,
						data->length, DMA_TO_DEVICE);
		ret = dma_mapping_error(dmadev->dev, data->addr_src);
		if (ret) {
			dev_err(dev, "dma map source address failed\n");
			data->addr_src = 0;
			goto out;
		}

		pg = virt_to_page(data->vaddr_dst);
		pg_offset = offset_in_page(data->vaddr_dst);
		data->addr_dst = dma_map_page(dmadev->dev, pg, pg_offset,
						data->length, DMA_FROM_DEVICE);
		ret = dma_mapping_error(dmadev->dev, data->addr_dst);
		if (ret) {
			dev_err(dev, "dma map destination address failed\n");
			data->addr_dst = 0;
			goto out;
		}

		dev_info(dev, "start m2m dma, from 0x%llx/%p to 0x%llx/%p, 0x%lx bytes\n",
			data->addr_src, data->vaddr_src,
			data->addr_dst, data->vaddr_dst, data->length);

		tx = dmadev->device_prep_dma_memcpy(data->dma_ch,
							data->addr_dst,
							data->addr_src,
							data->length, flags);
		if (!tx) {
			dev_err(dev, "prepare dma memcpy failed\n");
			ret = -EFAULT;
			goto out;
		}

		cookie = tx->tx_submit(tx);
		if (dma_submit_error(cookie)) {
			dev_err(dev, "submit dma tx failed\n");
			ret = -EFAULT;
			goto out;
		}

		status = dma_sync_wait(data->dma_ch, cookie);
		dmaengine_terminate_sync(data->dma_ch);

		dev_info(dev, "dma done: %d\n", status);

		dma_unmap_page(dmadev->dev, data->addr_src, data->length, DMA_TO_DEVICE);
		data->addr_src = 0;
		dma_unmap_page(dmadev->dev, data->addr_dst, data->length, DMA_FROM_DEVICE);
		data->addr_dst = 0;

	}
out:
	if (data->addr_src)
		dma_unmap_page(dmadev->dev, data->addr_src, data->length, DMA_TO_DEVICE);
	if (data->addr_dst)
		dma_unmap_page(dmadev->dev, data->addr_dst, data->length, DMA_FROM_DEVICE);
	kfree(data->vaddr_src);
	kfree(data->vaddr_dst);
	return ret ? : size;
}

static DEVICE_ATTR_RW(length);

static struct attribute *m2m_dma_attrs[] = {
	&dev_attr_addr_src.attr,
	&dev_attr_addr_dst.attr,
	&dev_attr_length.attr,
	NULL,
};

ATTRIBUTE_GROUPS(m2m_dma);

static int m2m_dma_probe(struct platform_device *pdev)
{
	struct m2m_dev_data *data;
	int ret = 0;
	char dev_name[32];
	dma_cap_mask_t mask;

	dev_info(&pdev->dev, "probe");
	data = devm_kzalloc(&pdev->dev, sizeof(struct m2m_dev_data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;
	platform_set_drvdata(pdev, data);
	data->pdev = pdev;
	data->cdev_class = m2m_dma_class;

	ret = snprintf(dev_name, ARRAY_SIZE(dev_name) - 1, "%s-%d", "m2m-dma", m2m_dma_count++);
	if (ret <= 0) {
		dev_err(&pdev->dev, "%s snprintf failed\n", __func__);
		ret = -EINVAL;
		goto fail3;
	}
	ret = alloc_chrdev_region(&data->cdevno, 0, 1, dev_name);
	if (ret < 0) {
		dev_err(&pdev->dev, "register chrdev error\n");
		goto fail3;
	}
	data->cdev = device_create(data->cdev_class, NULL, data->cdevno, data, dev_name);
	if (IS_ERR(data->cdev)) {
		dev_err(&pdev->dev, "create device failed\n");
		ret = PTR_ERR(data->cdev);
		goto fail2;
	}

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);
	data->dma_ch = dma_request_channel(mask, NULL, data);
	if (IS_ERR(data->dma_ch)) {
		dev_err(&pdev->dev, "request m2m dma channel failed\n");
		data->dma_ch = NULL;
		ret = -ENODEV;
		goto fail1;
	}
	dev_info(&pdev->dev, "got dma channel %s\n", dma_chan_name(data->dma_ch));

	return ret;

fail1:
	device_destroy(data->cdev_class, data->cdevno);
fail2:
	unregister_chrdev_region(data->cdevno, 1);
fail3:
	return ret;
}

static int m2m_dma_remove(struct platform_device *pdev)
{
	struct m2m_dev_data *data = platform_get_drvdata(pdev);

	if (data->dma_ch)
		dma_release_channel(data->dma_ch);
	device_destroy(data->cdev_class, data->cdevno);
	unregister_chrdev_region(data->cdevno, 1);
	kfree(data);
	data = NULL;
	return 0;
}

static const struct of_device_id m2m_dma_of_match[] = {
	{
		.compatible = "sophgo,m2m-dma",
	},
	{}
};

static struct platform_driver m2m_dma_driver = {
	.probe = m2m_dma_probe,
	.remove =  m2m_dma_remove,
	.driver = {
		.name = "sophgo,m2m-dma",
		.of_match_table = m2m_dma_of_match,
	},
};

static int __init m2m_dma_init(void)
{
	m2m_dma_class = class_create(THIS_MODULE, "m2m-dma");
	if (IS_ERR(m2m_dma_class)) {
		pr_err("m2m dma class create failed\n");
		return PTR_ERR(m2m_dma_class);
	}
	m2m_dma_class->dev_groups = m2m_dma_groups;

	return platform_driver_register(&m2m_dma_driver);
}

static void __exit m2m_dma_exit(void)
{
	class_destroy(m2m_dma_class);
}

module_init(m2m_dma_init);
module_exit(m2m_dma_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("xiao.wang@sophgo.com");
MODULE_DESCRIPTION("memory to memory DMA test");
MODULE_VERSION("ALPHA");

