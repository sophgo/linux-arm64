#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>

#define RTC_CCVR	0x0
#define RTC_CMR		0x4
#define RTC_CLR		0x8
#define RTC_CCR		0xC
#define RTC_STAT	0x10
#define RTC_RSTAT	0x14
#define RTC_EOI		0x18
#define RTC_VERSION	0x1C
#define RTC_CPSR	0x20
#define RTC_CPCVR	0x24

struct sg_rtc_data {
	struct platform_device *pdev;
	struct rtc_device *rdev;
	void __iomem *reg_base;
	struct clk *rtc_clk;
};

static void disable_rtc(struct sg_rtc_data *data)
{
	writel(readl(data->reg_base + RTC_CCR) & ~0x5, data->reg_base + RTC_CCR);
}

static void enable_rtc(struct sg_rtc_data *data)
{
	/*
	 * we warnt to enable but mask interrupt, then use the mask bit to control IRQ.
	 * so that we can check whether there was a pending interrupt by reading raw
	 * interrupt status register. but we can't enable inerrupt here, it should be
	 * done after the first time we set CMR to a non-zero value. otherwise we will
	 * got a pending interrupt immediately, as now CCVR == CMR == 0.
	 */
	writel(readl(data->reg_base + RTC_CCR) | 0x6, data->reg_base + RTC_CCR);
}

static int sg_rtc_get_time(struct device *dev, struct rtc_time *t)
{
	struct sg_rtc_data *data = dev_get_drvdata(dev);
	time64_t seconds;

	seconds = readl(data->reg_base + RTC_CCVR);
	rtc_time64_to_tm(seconds, t);

	dev_info(dev, "%s secs=%d, mins=%d, hours=%d, mday=%d, mon=%d, year=%d, wday=%d\n",
		"read", t->tm_sec, t->tm_min,
		t->tm_hour, t->tm_mday,
		t->tm_mon, t->tm_year, t->tm_wday);

	return 0;
}

static int sg_rtc_set_time(struct device *dev, struct rtc_time *t)
{
	struct sg_rtc_data *data = dev_get_drvdata(dev);
	time64_t seconds = rtc_tm_to_time64(t);

	writel(seconds, data->reg_base + RTC_CLR);
	// when rtc_counter++ for the next time, it will be updated to CLR+1

	dev_info(dev, "%s secs=%d, mins=%d, hours=%d, mday=%d, mon=%d, year=%d, wday=%d\n",
		"write", t->tm_sec, t->tm_min,
		t->tm_hour, t->tm_mday,
		t->tm_mon, t->tm_year, t->tm_wday);

	return 0;
}

static int sg_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct sg_rtc_data *data = dev_get_drvdata(dev);
	time64_t seconds;

	seconds = readl(data->reg_base + RTC_CMR);
	rtc_time64_to_tm(seconds, &t->time);
	t->enabled = readl(data->reg_base + RTC_CCR) & 0x2;
	t->pending = readl(data->reg_base + RTC_RSTAT) & 0x1;

	dev_info(dev, "%s secs=%d, mins=%d, hours=%d, mday=%d, enabled=%d, pending=%d\n",
		"alarm read", t->time.tm_sec, t->time.tm_min,
		t->time.tm_hour, t->time.tm_mday,
		t->enabled, t->pending);

	return 0;
}

static int sg_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct sg_rtc_data *data = dev_get_drvdata(dev);
	time64_t seconds = rtc_tm_to_time64(&t->time);

	writel(seconds, data->reg_base + RTC_CMR);
	if (t->enabled) {
		writel(readl(data->reg_base + RTC_CCR) & ~0x2, data->reg_base + RTC_CCR);
		if (!(readl(data->reg_base + RTC_CCR) & 0x1))
			writel(readl(data->reg_base + RTC_CCR) | 0x1, data->reg_base + RTC_CCR);
	}

	dev_info(dev, "%s secs=%d, mins=%d, hours=%d, mday=%d, enabled=%d, pending=%d\n",
		"alarm set", t->time.tm_sec, t->time.tm_min,
		t->time.tm_hour, t->time.tm_mday,
		t->enabled, t->pending);

	return 0;
}

static int sg_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct sg_rtc_data *data = dev_get_drvdata(dev);

	dev_info(dev, "alarm irq %s\n", enabled ? "enabled" : "disabled");

	if (enabled)
		writel(readl(data->reg_base + RTC_CCR) & ~0x2, data->reg_base + RTC_CCR);
	else
		writel(readl(data->reg_base + RTC_CCR) | 0x2, data->reg_base + RTC_CCR);
	return 0;
}


static const struct rtc_class_ops sg_rtc_ops = {
	.read_time	= sg_rtc_get_time,
	.set_time	= sg_rtc_set_time,
	.read_alarm	= sg_rtc_read_alarm,
	.set_alarm	= sg_rtc_set_alarm,
	.alarm_irq_enable = sg_rtc_alarm_irq_enable,
};

irqreturn_t sg_rtc_irq_handler(int irq, void *d)
{
	struct sg_rtc_data *data;

	pr_info("%s hwirq %ld, irq %d\n", __func__, irq_get_irq_data(irq)->hwirq, irq);

	data = platform_get_drvdata(d);
	readl(data->reg_base + RTC_EOI);
	rtc_update_irq(data->rdev, 1, RTC_AF | RTC_IRQF);
	return IRQ_HANDLED;
}

static int sg_rtc_probe(struct platform_device *pdev)
{
	struct sg_rtc_data *data;
	struct resource *res;
	int rtc_irq;
	int ret;

	data = kzalloc(sizeof(struct sg_rtc_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->pdev = pdev;
	platform_set_drvdata(pdev, data);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "reg");
	data->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->reg_base)) {
		dev_err(&pdev->dev, "failed map registers\n");
		ret = PTR_ERR(data->reg_base);
		goto out;
	}

	data->rtc_clk = devm_clk_get(&pdev->dev, "rtc_clk");
	if (!data->rtc_clk) {
		dev_err(&pdev->dev, "failed to get clock\n");
		goto out;
	} else {
		dev_info(&pdev->dev, "clock freq: %ld\n", clk_get_rate(data->rtc_clk));
	}

	rtc_irq = platform_get_irq(pdev, 0);
	if (rtc_irq >= 0) {
		ret = request_threaded_irq(rtc_irq, sg_rtc_irq_handler,
					NULL, IRQF_SHARED | IRQF_TRIGGER_HIGH, "sg-rtc", pdev);
		if (ret < 0)
			dev_err(&pdev->dev, "register irq failed %d\n", ret);

	} else {
		dev_err(&pdev->dev, "irq not found\n");
	}

	if (of_property_read_bool(pdev->dev.of_node, "wakeup-source"))
		device_set_wakeup_capable(&pdev->dev, true);

	data->rdev = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(data->rdev)) {
		dev_err(&pdev->dev, "failed to alloc rtc device\n");
		ret = PTR_ERR(data->rdev);
		goto out;
	}
	data->rdev->ops = &sg_rtc_ops;

	ret = rtc_register_device(data->rdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register rtc\n");
		goto out;
	}

	disable_rtc(data);
	writel(0, data->reg_base + RTC_CMR);
	writel(clk_get_rate(data->rtc_clk), data->reg_base + RTC_CPSR);
	writel(0, data->reg_base + RTC_CLR);
	enable_rtc(data);
	dev_info(&pdev->dev, "rtc version: 0x%x\n", readl(data->reg_base + RTC_VERSION));

	return 0;

out:
	if (data->reg_base)
		iounmap(data->reg_base);
	kfree(data);
	return ret;
}

static int sg_rtc_remove(struct platform_device *pdev)
{
	struct sg_rtc_data *data;

	data = platform_get_drvdata(pdev);
	iounmap(data->reg_base);
	kfree(data);
	return 0;
}

static const struct of_device_id sg_rtc_of_match[] = {
	{
		.compatible = "sophgo,rtc",
	},
	{}
};

static struct platform_driver sg_rtc_driver = {
	.probe = sg_rtc_probe,
	.remove =  sg_rtc_remove,
	.driver = {
		.name = "sophgo,rtc",
		.of_match_table = sg_rtc_of_match,
	},
};

static int __init sg_rtc_init(void)
{
	return platform_driver_register(&sg_rtc_driver);
}

static void __exit sg_rtc_exit(void)
{
}

module_init(sg_rtc_init);
module_exit(sg_rtc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("xiao.wang@sophgo.com");
MODULE_DESCRIPTION("Sophgo RTC driver");
MODULE_VERSION("ALPHA");

