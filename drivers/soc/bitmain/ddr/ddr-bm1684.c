#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/interrupt.h>
#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#define ECC_CFG 0x74
#define ECC_STAT 0x78
#define ECC_CTL 0x7C
#define DDR_SWCTL 0x320

struct ecc_reg_stat {
	/* ddr ecc information*/
	int ecc_err_count;
	int ecc_stat;
};

struct ddr_cfg {
	int ecc_enable;
	int ddr_count;
};

struct ddr_dev {
	int id;
	int irq_num;
	char name[16];

	spinlock_t lock;
	void __iomem *regs;
	struct ddr_cfg *data;
	struct ecc_reg_stat *ecc_stat;
};

static struct ddr_cfg *cfg;

static const struct of_device_id ddr_of_id_table[] = {
	{ .compatible = "bitmain,ddr" },
	{ .compatible = "ddrc" },
	{}
};
MODULE_DEVICE_TABLE(of, ddr_of_id_table);

static ssize_t store_ecc_status(struct kobject *kobj,
					 struct kobj_attribute *attr,
					 const char *buf, size_t count)
{
	struct device *dev = kobj_to_dev(kobj->parent);
	struct ddr_dev *d = dev_get_drvdata(dev);
	unsigned int value;
	int ecc_swctl_old, ret;

	if (!d->data->ecc_enable)
		return count;

	/*only for debug write*/
	return 0;

	ret = kstrtouint(buf, 0, &value);
	if (ret)
		return ret;

	/* enable ecc reg access */
	ecc_swctl_old = readl(d->regs + DDR_SWCTL);
	writel(0, d->regs + DDR_SWCTL);

	/* read stat and clear intterupt */
	writel(value, d->regs + 0x7c);

	/* restore ecc */
	writel(ecc_swctl_old, d->regs + DDR_SWCTL);

	return count;
}

static ssize_t show_ecc_status(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct device *dev = kobj_to_dev(kobj->parent);
	struct ecc_reg_stat *ecc_stat;
	struct ddr_dev *d = dev_get_drvdata(dev);

	if (!d->data->ecc_enable)
		return 0;

	ecc_stat = d->ecc_stat;

	pr_info("ddr controller: %s\n", d->name);
	pr_info("1 bit err: %ld\n",
			FIELD_GET(GENMASK(8, 8), ecc_stat->ecc_stat));
	pr_info("2 bit err: %ld\n",
			FIELD_GET(GENMASK(16, 16), ecc_stat->ecc_stat));
	pr_info("1 bit ecc correct bit number: %ld\n",
			FIELD_GET(GENMASK(16, 0), ecc_stat->ecc_stat));
	pr_info("total 1/2 bit ecc errs: %d\n", ecc_stat->ecc_err_count);

	return 0;
}

static struct kobj_attribute ddr_attr_ecc =
		__ATTR(ecc, 0644, show_ecc_status, store_ecc_status);

static const struct attribute *dbg_attrs[] = {
	&ddr_attr_ecc.attr,
	NULL
};

static irqreturn_t ddr_irq_handler(int irq, void *devid)
{
	int ecc_swctl_old, ecc_ctl;
	unsigned long flags;
	struct ddr_dev *dev = (struct ddr_dev *)devid;

	spin_lock_irqsave(&dev->lock, flags);

	if (dev->data->ecc_enable) {
		dev->ecc_stat->ecc_err_count++;

		/* enable ecc reg access */
		ecc_swctl_old = readl(dev->regs + DDR_SWCTL);
		writel(0, dev->regs + DDR_SWCTL);

		/* read stat and clear intterupt */
		dev->ecc_stat->ecc_stat = readl(dev->regs + ECC_STAT);
		ecc_ctl = readl(dev->regs + ECC_CTL);
		writel(ecc_ctl | 0xf, dev->regs + ECC_CTL);

		/* restore ecc */
		writel(ecc_swctl_old, dev->regs + DDR_SWCTL);
	}

	spin_unlock_irqrestore(&dev->lock, flags);

	return IRQ_HANDLED;
}

static struct kobject *bmddr_global_kobject;
static int bm1684_ddr_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *np = pdev->dev.of_node;

	if (of_device_is_compatible(np, "bitmain,ddr")) {
		const char *ecc_status;
		int reg_val;
		struct regmap *regmap;
		struct device_node *syscon;

		cfg = devm_kzalloc(&pdev->dev, sizeof(*cfg), GFP_KERNEL);
		if (!of_property_read_string(np, "ecc_status", &ecc_status) &&
		    !strcmp(ecc_status, "enable"))
			cfg->ecc_enable = 1;

		syscon = of_parse_phandle(pdev->dev.of_node, "subctrl-syscon", 0);
		if (!syscon) {
			dev_err(&pdev->dev, "no subctrl-syscon property\n");
			return -ENODEV;
		}
		regmap = syscon_node_to_regmap(syscon);
		of_node_put(syscon);
		if (IS_ERR(regmap))
			return PTR_ERR(regmap);

		regmap_read(regmap, 0x08, &reg_val);
		regmap_write(regmap, 0x08, reg_val | (0xf << 5));

		of_property_read_u32(np, "ddr_count", &cfg->ddr_count);

		bmddr_global_kobject = kobject_create_and_add("attr", &pdev->dev.kobj);
		if (!bmddr_global_kobject)
			return -ENOMEM;

		of_platform_populate(pdev->dev.of_node, ddr_of_id_table,
					 NULL, &pdev->dev);

		return 0;
	}

	if (of_device_is_compatible(np, "ddrc")) {
		struct ddr_dev *ddev;
		struct resource mem;
		struct kobject *ddr_kobj;

		ddev = devm_kzalloc(&pdev->dev, sizeof(*ddev), GFP_KERNEL);
		if (!ddev)
			return -ENOMEM;

		if (cfg->ecc_enable) {
			ddev->ecc_stat = devm_kzalloc(&pdev->dev,
					sizeof(struct ecc_reg_stat), GFP_KERNEL);
			if (!ddev->ecc_stat)
				return -ENOMEM;
		}

		ret = of_address_to_resource(np, 0, &mem);
		if (ret) {
			dev_warn(&pdev->dev, "invalid address\n");
			return -EINVAL;
		}
		ddev->regs = devm_ioremap_resource(&pdev->dev, &mem);
		if (IS_ERR(ddev->regs)) {
			ret = PTR_ERR(ddev->regs);
			return ret;
		}

		of_property_read_u32(np, "id", &ddev->id);
		sprintf(ddev->name, "ddr-%s",
				ddev->id == 0 ? "0a"
				: ddev->id == 1 ? "0b"
				: ddev->id == 2 ? "1"
				: "2");

		spin_lock_init(&ddev->lock);
		ddev->irq_num = platform_get_irq(pdev, 0);
		ret = devm_request_threaded_irq(&pdev->dev, ddev->irq_num, NULL,
						ddr_irq_handler,
						IRQF_ONESHOT, ddev->name,
						ddev);
		if (ret) {
			dev_err(&pdev->dev, "can't request ddr: %s irq(%d)\n",
					ddev->name, ddev->irq_num);
			return ret;
		}

		ddev->data = cfg;
		dev_set_drvdata(&pdev->dev, ddev);

		ddr_kobj = kobject_create_and_add("status", &pdev->dev.kobj);
		if (ddr_kobj) {
			ret = sysfs_create_files(ddr_kobj, dbg_attrs);
			if (!ret) {
				ret = sysfs_create_link(bmddr_global_kobject,
						ddr_kobj, ddev->name);
				if (ret)
					dev_warn(&pdev->dev, "failed to creat link files: %d\n", ret);
			}
		}

		return 0;
	}

	return 0;
}

static int bm1684_ddr_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver ddr_driver = {
	.probe = bm1684_ddr_probe,
	.remove = bm1684_ddr_remove,
	.driver = {
		.name = "bm1684_ddr",
		.of_match_table = of_match_ptr(ddr_of_id_table),
	},
};

static int __init bm1684_ddr_init_module(void)
{
	return platform_driver_register(&ddr_driver);

}

static void __exit bm1684_ddr_exit_module(void)
{
	platform_driver_unregister(&ddr_driver);
}

module_init(bm1684_ddr_init_module);
module_exit(bm1684_ddr_exit_module);

MODULE_DESCRIPTION("BM1684 ddr module");
MODULE_LICENSE("GPL");
