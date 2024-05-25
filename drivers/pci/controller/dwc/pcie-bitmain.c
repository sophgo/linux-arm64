/*
 * PCIe host controller driver for Bitmain bm168x_pcie SoCs
 *
 * bm168x_pcie PCIe RC Source Code
 *
 * Copyright (C) 2019-2025 Bitmain
 * Jibin Xu <jibin.xu@bitmain.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <soc/bitmain/bm1684_top.h>
#include "pcie-designware.h"

#define dw_pcie_readl_rc(pci, reg) dw_pcie_read_dbi((pci), (reg), 4)
#define dw_pcie_writel_rc(pci, reg, val) dw_pcie_write_dbi((pci), (reg), 4, (val))

#define PCIE_MSI_CTRL_REG     0x010
#define PCIE_LTSSM_REG        0x258
#define PCIE_LINKUP_REG       0x2b4
#define PCIE_INT_STATUS_REG   0x1fc
#define PCIE_RC_CFG_WRITE_REG 0x8bc
#define PCIE_IS_LINKUP        0x0c0
#define PCIE_REST_REG         0x048
#define PCIE_PRST_REG         0x04C
#define PCIE_MSI_EN           (1 << 24)
#define PCIE_MSI_INT          (1 << 8)
#define PCIE_EP_ONLY          (0x1 << 24)
#define DBI_RESET_BIT         2
#define CORE_RESET_BIT        7
#define RC_PRST_X_IN_BIT      23
#define EP_PRST_X_IN_BIT      22
#define to_bm168x_pcie(x)     dev_get_drvdata((x)->dev)
// #define NETWORK_TOP_IRQ

struct bm168x_pcie {
	struct dw_pcie *pci; /* DT dbi is pci.dbi_base */
	void __iomem *apb_base; /* snps apb base */
	void __iomem *top_apb_base; /* bm168x top apb base */
	int reset_gpio;
};

static int bm168x_pcie_reset(struct bm168x_pcie *bm168x_pcie, unsigned int ms)
{
	gpio_set_value(bm168x_pcie->reset_gpio, 0);
	msleep(ms);
	gpio_set_value(bm168x_pcie->reset_gpio, 1);
	return 0;
}

static int bm168x_pcie_poll_reset(struct bm168x_pcie *bm168x_pcie, int bit, int timeout_ms)
{
	unsigned int val;
	int ms = 0;

	do {
		val = readl(bm168x_pcie->top_apb_base + PCIE_REST_REG);
		val &= (1 << bit);
		if (val)
			return 0;
		ms++;
		mdelay(1);
	} while (!val && ms < timeout_ms);

	return -1;
}

static int bm168x_pcie_poll_prst_x_in(struct bm168x_pcie *bm168x_pcie, int bit, int timeout_ms)
{
	unsigned int val;
	int ms = 0;

	do {
		val = readl(bm168x_pcie->top_apb_base + PCIE_PRST_REG);
		val &= (1 << bit);
		if (val)
			return 0;
		ms++;
		mdelay(1);
	} while (!val && ms < timeout_ms);

	return -1;
}


static int bm168x_pcie_establish_link(struct bm168x_pcie *bm168x_pcie)
{
	unsigned int val;
	struct dw_pcie *pci = bm168x_pcie->pci;
	struct pcie_port *pp = &pci->pp;

	/* enable rc configuration write */
	val = dw_pcie_readl_rc(pci, PCIE_RC_CFG_WRITE_REG);
	val |= 0x1;
	dw_pcie_writel_rc(pci, PCIE_RC_CFG_WRITE_REG, val);

	dw_pcie_setup_rc(pp);

	/* disable RC dl scale feature */
	val = dw_pcie_readl_rc(pci, 0x2d4);
	val &= ~(1 << 31);
	dw_pcie_writel_rc(pci, 0x2d4, val);

	/* disable rc configuration write */
	val = dw_pcie_readl_rc(pci, PCIE_RC_CFG_WRITE_REG);
	val &= ~0x1;
	dw_pcie_writel_rc(pci, PCIE_RC_CFG_WRITE_REG, val);

	/* enable ltssm */
	val = readl(bm168x_pcie->apb_base + PCIE_LTSSM_REG);
	val |= 0x1;
	writel(val, bm168x_pcie->apb_base + PCIE_LTSSM_REG);

	return dw_pcie_wait_for_link(pci);
}

#if 1
static irqreturn_t bm168x_pcie_irq_handler(int irq, void *arg)
{
	struct bm168x_pcie *bm168x_pcie = arg;
	struct dw_pcie *pci = bm168x_pcie->pci;
	struct pcie_port *pp = &pci->pp;
	unsigned int status;

	status = readl(bm168x_pcie->top_apb_base + PCIE_INT_STATUS_REG);
	if (status & PCIE_MSI_INT) {
		WARN_ON(!IS_ENABLED(CONFIG_PCI_MSI));
		dw_handle_msi_irq(pp);
	}

	return IRQ_HANDLED;
}
#endif

static int dw_pcie_wr_own_conf(struct pcie_port *pp, int where, int size,
			       u32 val)
{
	struct dw_pcie *pci;

	if (pp->ops->wr_own_conf)
		return pp->ops->wr_own_conf(pp, where, size, val);

	pci = to_dw_pcie_from_pp(pp);
	return dw_pcie_write(pci->dbi_base + where, size, val);
}

static void bm168x_pcie_msi_init(struct pcie_port *pp)
{
	u64 msi_target;

	/*
	 * for compatibility with devices which do not support
	 * 64bit MSI address. the original dw_pcie_msi_init
	 * function would alloc a page and use its address as MSI
	 * address. on BM168x, this will always produce an address
	 * above 32bit, as DDRs are all there.
	 */
	pp->msi_data = 0x10000000;
	msi_target = (u64)pp->msi_data;

	/* Program the msi_data */
	dw_pcie_wr_own_conf(pp, PCIE_MSI_ADDR_LO, 4,
			    lower_32_bits(msi_target));
	dw_pcie_wr_own_conf(pp, PCIE_MSI_ADDR_HI, 4,
			    upper_32_bits(msi_target));
}

static void bm168x_pcie_enable_interrupts(struct bm168x_pcie *bm168x_pcie)
{
	struct dw_pcie *pci = bm168x_pcie->pci;
	struct pcie_port *pp = &pci->pp;
	unsigned int val;

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		bm168x_pcie_msi_init(pp);
		val = readl(bm168x_pcie->top_apb_base + PCIE_MSI_CTRL_REG);
		val |= PCIE_MSI_EN;
		writel(val, bm168x_pcie->top_apb_base + PCIE_MSI_CTRL_REG);
	}
}

static int bm168x_pcie_link_up(struct dw_pcie *pci)
{
	unsigned int status;
	struct bm168x_pcie *bm168x_pcie = to_bm168x_pcie(pci);

	status = readl(bm168x_pcie->apb_base + PCIE_LINKUP_REG);
	status &= PCIE_IS_LINKUP;
	if (status == PCIE_IS_LINKUP)
		return 1;
	else
		return 0;
}

static int bm168x_pcie_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct bm168x_pcie *bm168x_pcie = to_bm168x_pcie(pci);

	bm168x_pcie_establish_link(bm168x_pcie);
	bm168x_pcie_enable_interrupts(bm168x_pcie);

	return 0;
}

#ifdef NETWORK_TOP_IRQ

static void cdns_pcie_msi_ack_irq(struct irq_data *d)
{
	irq_chip_ack_parent(d);
}

static void cdns_pcie_msi_mask_irq(struct irq_data *d)
{
	pci_msi_mask_irq(d);
	irq_chip_mask_parent(d);
}

static void cdns_pcie_msi_unmask_irq(struct irq_data *d)
{
	pci_msi_unmask_irq(d);
	irq_chip_unmask_parent(d);
}

static struct irq_chip cdns_pcie_msi_irq_chip = {
	.name = "cdns-msi",
	.irq_ack = cdns_pcie_msi_ack_irq,
	.irq_mask = cdns_pcie_msi_mask_irq,
	.irq_unmask = cdns_pcie_msi_unmask_irq,
};

static struct msi_domain_info cdns_pcie_msi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		   MSI_FLAG_PCI_MSIX | MSI_FLAG_MULTI_PCI_MSI),
	.chip	= &cdns_pcie_msi_irq_chip,
};

extern struct irq_domain *cdns_pcie_get_parent_irq_domain(void);

int bm168x_msi_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct fwnode_handle *fwnode = of_node_to_fwnode(pci->dev->of_node);
	struct irq_domain *irq_parent = cdns_pcie_get_parent_irq_domain();

	pp->msi_domain = pci_msi_create_irq_domain(fwnode,
						   &cdns_pcie_msi_domain_info,
						   irq_parent);
	if (!pp->msi_domain) {
		dev_err(pci->dev, "create msi irq domain failed\n");
		return -ENODEV;
	}

	return 0;
}

#endif

static struct dw_pcie_host_ops bm168x_pcie_host_ops = {
	.host_init = bm168x_pcie_host_init,
};

static int bm168x_pcie_add_pcie_port(struct bm168x_pcie *bm168x_pcie,
		struct platform_device *pdev)
{
	struct dw_pcie *pci = bm168x_pcie->pci;
	struct pcie_port *pp = &pci->pp;
	struct device *dev = pci->dev;
	int ret;

	// if (device_property_read_bool(&pdev->dev, "pcie_irq_enable")) {
		if (IS_ENABLED(CONFIG_PCI_MSI)) {
			pp->msi_irq = platform_get_irq_byname(pdev, "msi");
			if (pp->msi_irq <= 0) {
				dev_err(dev, "failed to get MSI irq\n");
				return -ENODEV;
			}
			/*
			 * now we have irq_set_chained_handler_and_data in dw_pcie_host_init,
			 * no need to request irq here.
			 */
			#if 1
			ret = devm_request_irq(dev, pp->msi_irq,
					bm168x_pcie_irq_handler,
					IRQF_SHARED | IRQF_NO_THREAD,
					"bm168x-pcie-irq", bm168x_pcie);
			if (ret) {
				dev_err(dev, "failed to request MSI irq\n");
				return ret;
			}
			#endif
		}
	// } else {
	// 	pr_info("%s %d: should not run in\n", __func__, __LINE__);
	// 	// bm168x_pcie_host_ops.msi_host_init = bm168x_msi_host_init;
	// }

	pp->root_bus_nr = -1;
	pp->ops = &bm168x_pcie_host_ops;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.link_up = bm168x_pcie_link_up,
};

static u64 pci_dma_mask = DMA_BIT_MASK(35);

static int bm168x_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bm168x_pcie *bm168x_pcie;
	struct dw_pcie *pci;
	struct clk *clk;
	struct resource *dbi_base;
	struct resource *apb_base;
	struct resource *top_apb_base;
	unsigned int val, reset_timeout = 10;
	int ret, bin_type = 0;

	efuse_ft_get_bin_type(&bin_type);
	if (bin_type == 3) {
		dev_err(dev, "Normal bin without PCIe\n");
		return -1;
	}

	bm168x_pcie = devm_kzalloc(dev, sizeof(*bm168x_pcie), GFP_KERNEL);
	if (!bm168x_pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	bm168x_pcie->pci = pci;
	dma_set_mask(dev, pci_dma_mask);
	pci->dev = dev;
	pci->ops = &dw_pcie_ops;

	clk = devm_clk_get(&pdev->dev, "pcie_clk");
	if (!clk) {
		dev_err(dev, "get pcie clk failed\n");
		return -1;
	}

	clk_prepare_enable(clk);

	dbi_base = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi");
	pci->dbi_base = devm_ioremap_resource(dev, dbi_base);
	if (IS_ERR(pci->dbi_base)) {
		dev_err(dev, "couldn't remap dbi base %p\n", dbi_base);
		ret = PTR_ERR(pci->dbi_base);
		return ret;
	}

	apb_base = platform_get_resource_byname(pdev, IORESOURCE_MEM, "apb");
	bm168x_pcie->apb_base = devm_ioremap_resource(dev, apb_base);
	if (IS_ERR(bm168x_pcie->apb_base)) {
		dev_err(dev, "couldn't remap apb base %p\n", apb_base);
		ret = PTR_ERR(bm168x_pcie->apb_base);
		return ret;
	}

	top_apb_base = platform_get_resource_byname(pdev,
			IORESOURCE_MEM, "top_apb");
	bm168x_pcie->top_apb_base = devm_ioremap_resource(dev, top_apb_base);
	if (IS_ERR(bm168x_pcie->top_apb_base)) {
		dev_err(dev, "couldn't remap apb base %p\n", top_apb_base);
		ret = PTR_ERR(bm168x_pcie->top_apb_base);
		return ret;
	}

	bm168x_pcie->reset_gpio = of_get_named_gpio(dev->of_node, "reset-gpio", 0);
	if (bm168x_pcie->reset_gpio < 0) {
		dev_err(dev, "could not get pcie reset gpio\n");
		return -1;
	}

	ret = gpio_request_one(bm168x_pcie->reset_gpio, GPIOF_OUT_INIT_LOW, "pcie-reset");
	if (ret) {
		dev_err(dev, "could not request gpio %d failed!\n", bm168x_pcie->reset_gpio);
		return ret;
	}

	if (bm168x_pcie_reset(bm168x_pcie, 300)) {
		dev_err(dev, "unable to reset gpio\n");
		return -1;
	}

	/* set ss_mode to EP+RC */
	writel(readl(bm168x_pcie->top_apb_base) | 0x1, bm168x_pcie->top_apb_base);

	ret = bm168x_pcie_poll_reset(bm168x_pcie, CORE_RESET_BIT, reset_timeout);
	if (ret) {
		dev_err(dev, "polling core reset failed\n");
		return ret;
	}

	ret = bm168x_pcie_poll_reset(bm168x_pcie, DBI_RESET_BIT, reset_timeout);
	if (ret) {
		dev_err(dev, "polling dbi reset failed\n");
		return ret;
	}

	ret = bm168x_pcie_poll_prst_x_in(bm168x_pcie, RC_PRST_X_IN_BIT, reset_timeout);
	if (ret) {
		dev_err(dev, "polling rc prst_x_in failed\n");
		return ret;
	}

	/* set to EP only */
	val = readl(bm168x_pcie->top_apb_base);
	val &= ~(3 << 24);
	val |= PCIE_EP_ONLY;
	writel(val, bm168x_pcie->top_apb_base);

	platform_set_drvdata(pdev, bm168x_pcie);

	ret = bm168x_pcie_add_pcie_port(bm168x_pcie, pdev);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct of_device_id bm168x_pcie_of_match[] = {
	{ .compatible = "bitmain,bm168x-pcie", },
	{},
};

static struct platform_driver bm168x_pcie_driver = {
	.probe = bm168x_pcie_probe,
	.driver = {
		.name = "bm168x_pcie",
		.of_match_table = of_match_ptr(bm168x_pcie_of_match),
	},
};

static int __init bm168x_pcie_init(void)
{
	return platform_driver_register(&bm168x_pcie_driver);
}

device_initcall(bm168x_pcie_init);
