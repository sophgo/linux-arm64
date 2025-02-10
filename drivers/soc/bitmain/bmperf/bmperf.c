#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/mfd/core.h>
#include <linux/mod_devicetable.h>
#include <linux/sched/clock.h>
#include "bmperf.h"

static unsigned long axi_base_address = 0x50008000;
static unsigned int axi_port;
static unsigned long axi_period = 100000;

// #define BMPERF_DEBUG
#define SLEEP_MS           (1)

// module params
module_param(axi_base_address, ulong, 0400);
MODULE_PARM_DESC(axi_base_address, "AXI bus memory mmaped register base address.");

module_param(axi_port, uint, 0400);
MODULE_PARM_DESC(axi_port, "AXI bus port to be listened.");

module_param(axi_period, ulong, 0400);
MODULE_PARM_DESC(axi_period, "AXI period");

// kthreads
static struct task_struct *tsk_axi_bus;

static struct platform_device *tsk_pdev;

static int kthread_axi(void *data)
{
	struct axi_result a_result = {0};
	unsigned int *base = ioremap(axi_base_address, sizeof(struct axi_register));
	unsigned int count = 0;

	// configure port
	base[offsetof(struct axi_register, port) / 4] = (axi_port & 0xff) | 0x100;

	// configure period to enable PM
	base[offsetof(struct axi_register, period) / 4] = axi_period;

	// pooling to get the PM result
	while (!kthread_should_stop()) {
		// routine sampling mode
		a_result.routine_aw_sampling += base[offsetof(struct axi_register, routine_aw_sampling) / 4];
		a_result.routine_wr_sampling += base[offsetof(struct axi_register, routine_wr_sampling) / 4];
		a_result.routine_ar_sampling += base[offsetof(struct axi_register, routine_ar_sampling) / 4];

		// routine trace mode
		count = base[offsetof(struct axi_register, routine_aw_available) / 4] & 0xff;
		while (count--)
			a_result.routine_aw_trace += base[offsetof(struct axi_register, routine_aw_trace) / 4];

		count = base[offsetof(struct axi_register, routine_wr_available) / 4] & 0xff;
		while (count--)
			a_result.routine_wr_trace += base[offsetof(struct axi_register, routine_wr_trace) / 4];

		count = base[offsetof(struct axi_register, routine_ar_available) / 4] & 0xff;
		while (count--)
			a_result.routine_ar_trace += base[offsetof(struct axi_register, routine_ar_trace) / 4];

		msleep(SLEEP_MS);
	}

	// peak mode
	a_result.peak_aw = base[offsetof(struct axi_register, peak_aw) / 4];
	a_result.peak_wr = base[offsetof(struct axi_register, peak_wr) / 4];
	a_result.peak_ar = base[offsetof(struct axi_register, peak_ar) / 4];

	// disable PM
	base[offsetof(struct axi_register, period) / 4] = 0;
	iounmap(base);

	// AXI bus PM result
	pr_info("Routine mode(Sampling): aw = %-20lu    wr = %-20lu    ar = %-20lu\n",
		a_result.routine_aw_sampling, a_result.routine_wr_sampling, a_result.routine_ar_sampling);
	pr_info("Routine mode(Trace):    aw = %-20lu    wr = %-20lu    ar = %-20lu\n",
		a_result.routine_aw_trace, a_result.routine_wr_trace, a_result.routine_ar_trace);
	pr_info("Peak mode:              aw = %-20lu    wr = %-20lu    ar = %-20lu\n",
		a_result.peak_aw, a_result.peak_wr, a_result.peak_ar);

	return 0;
}

static int bmperf_probe(struct platform_device *pdev)
{
	pr_info("%s: axi_base_address = 0x%lx, axi_port = %u, axi_period = %lu\n",
		__func__, axi_base_address, axi_port, axi_period);

	tsk_pdev = pdev;

	// AXI bus kthread
	tsk_axi_bus = kthread_run(kthread_axi, (void *)0, "AXI bus kthread");

	return 0;
}

static int bmperf_remove(struct platform_device *pdev)
{
	pr_info("%s is called.\n", __func__);

	if (tsk_axi_bus != NULL) {
		kthread_stop(tsk_axi_bus);
		tsk_axi_bus = NULL;
	}

	return 0;
}

static const struct of_device_id bmperf_of_match[] = {
	{
		.compatible = "bitmain,bmperf",
	},
	{}
};

static struct platform_driver bmperf_driver = {
	.probe = bmperf_probe,
	.remove =  bmperf_remove,
	.driver = {
		.name = "bitmain,bmperf",
		.of_match_table = bmperf_of_match,
	},
};

static int __init bmperf_init(void)
{
	return platform_driver_register(&bmperf_driver);
}

static void __exit bmperf_exit(void)
{
	platform_driver_unregister(&bmperf_driver);
}

module_init(bmperf_init);
module_exit(bmperf_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("tim.zhang@bitmain.com");
MODULE_DESCRIPTION("performance monitor for AXI/TPU/GDMA");
MODULE_SUPPORTED_DEVICE("bm1684");
