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

// sudo busybox devmem 0x50010198 32 0x1
// sudo busybox devmem 0x30010308 32 0x1

irqreturn_t top_intc_test_irq_handler1(int irq, void *data)
{
	pr_info("%s hwirq %ld, irq %d\n", __func__, irq_get_irq_data(irq)->hwirq, irq);
	return IRQ_HANDLED;
}

irqreturn_t top_intc_test_irq_handler2(int irq, void *data)
{
	pr_info("%s hwirq %ld, irq %d\n", __func__, irq_get_irq_data(irq)->hwirq, irq);
	return IRQ_HANDLED;
}

static int top_intc_test_probe(struct platform_device *pdev)
{
	int top_intc_irq1;
	struct irq_desc *desc1;
	int top_intc_irq2;
	struct irq_desc *desc2;
	int ret;

	top_intc_irq1 = platform_get_irq_byname(pdev, "msi0_test");
	if (top_intc_irq1 >= 0) {
		desc1 = irq_to_desc(top_intc_irq1);

		ret = request_threaded_irq(top_intc_irq1, top_intc_test_irq_handler1,
					NULL, IRQF_SHARED | IRQF_TRIGGER_HIGH, "top-intc-test1", pdev);
		if (ret < 0)
			dev_err(&pdev->dev, "register irq1 failed %d\n", ret);

		dev_info(&pdev->dev, "registered irq hwirq %ld, irq %d\n", desc1->irq_data.hwirq, top_intc_irq1);
	} else {
		dev_err(&pdev->dev, "test irq 1 not found\n");
	}

	top_intc_irq2 = platform_get_irq_byname(pdev, "msi7_test");
	if (top_intc_irq2 >= 0) {
		desc2 = irq_to_desc(top_intc_irq2);

		ret = request_threaded_irq(top_intc_irq2, top_intc_test_irq_handler2,
					NULL, IRQF_SHARED | IRQF_TRIGGER_HIGH, "top-intc-test2", pdev);
		if (ret < 0)
			dev_err(&pdev->dev, "register irq2 failed %d\n", ret);

		dev_info(&pdev->dev, "registered irq hwirq %ld, irq %d\n", desc2->irq_data.hwirq, top_intc_irq2);
	} else {
		dev_err(&pdev->dev, "test irq 2 not found\n");
	}

	return 0;
}

static int top_intc_test_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id top_intc_test_of_match[] = {
	{
		.compatible = "bitmain,top-intc-test",
	},
	{}
};

static struct platform_driver top_intc_test_driver = {
	.probe = top_intc_test_probe,
	.remove =  top_intc_test_remove,
	.driver = {
		.name = "bitmain,top-intc-test",
		.of_match_table = top_intc_test_of_match,
	},
};

static int __init top_intc_test_init(void)
{
	return platform_driver_register(&top_intc_test_driver);
}

static void __exit top_intc_test_exit(void)
{
}

module_init(top_intc_test_init);
module_exit(top_intc_test_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("xiao.wang@bitmain.com");
MODULE_DESCRIPTION("TOP interrupt test");
MODULE_VERSION("ALPHA");

