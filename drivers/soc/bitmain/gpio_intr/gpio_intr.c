#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>

irqreturn_t gpio_intr_test_irq_handler(int irq, void *data)
{
	pr_info("%s hwirq %ld, irq %d\n", __func__, irq_get_irq_data(irq)->hwirq, irq);
	return IRQ_HANDLED;
}

static int gpio_intr_probe(struct platform_device *pdev)
{
	int top_intc_irq;
	struct irq_desc *desc;
	int ret;

	top_intc_irq = platform_get_irq_byname(pdev, "test");
	if (top_intc_irq >= 0) {
		desc = irq_to_desc(top_intc_irq);

		ret = request_threaded_irq(top_intc_irq, gpio_intr_test_irq_handler,
					NULL, IRQF_SHARED | IRQF_TRIGGER_HIGH, "gpio_intr_test", pdev);
		if (ret < 0)
			dev_err(&pdev->dev, "register irq failed %d\n", ret);

		dev_info(&pdev->dev, "registered irq hwirq %ld, irq %d\n", desc->irq_data.hwirq, top_intc_irq);
	} else {
		dev_err(&pdev->dev, "test irq not found\n");
	}

	return 0;
}

static int gpio_intr_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id gpio_intr_of_match[] = {
	{
		.compatible = "sophgo,gpio-intr",
	},
	{}
};

static struct platform_driver gpio_intr_driver = {
	.probe = gpio_intr_probe,
	.remove =  gpio_intr_remove,
	.driver = {
		.name = "sophgo,gpio-intr",
		.of_match_table = gpio_intr_of_match,
	},
};

static int __init gpio_intr_init(void)
{
	return platform_driver_register(&gpio_intr_driver);
}

static void __exit gpio_intr_exit(void)
{
}

module_init(gpio_intr_init);
module_exit(gpio_intr_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("xiao.wang@sophgo.com");
MODULE_DESCRIPTION("GPIO interrupt test");
MODULE_VERSION("ALPHA");

