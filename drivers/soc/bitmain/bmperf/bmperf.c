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

static unsigned long tpu_control_register = 0x58001100;
static unsigned long gdma_base_address = 0x5800006C;

// #define BMPERF_DEBUG
#define SLEEP_MS           (1)
// do not exceed 4K bytes in total for this number of data
#define TPU_DATA_NUM       (16)
#define GDMA_DATA_NUM      (16)

// module params
module_param(axi_base_address, ulong, 0400);
MODULE_PARM_DESC(axi_base_address, "AXI bus memory mmaped register base address.");

module_param(axi_port, uint, 0400);
MODULE_PARM_DESC(axi_port, "AXI bus port to be listened.");

module_param(axi_period, ulong, 0400);
MODULE_PARM_DESC(axi_period, "AXI period");

// kthreads
static struct task_struct *tsk_axi_bus;
static struct task_struct *tsk_tpu;
static struct task_struct *tsk_gdma;
static struct platform_device *tsk_pdev;

static int kthread_axi(void *data)
{
	struct axi_result a_result = {0};
	unsigned int *base = ioremap(axi_base_address, sizeof(struct axi_register));
	unsigned int count = 0;

	// configure port
	base[offsetof(struct axi_register, port) / 4] = axi_port & 0xff;

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

static int kthread_tpu(void *data)
{
	char *buffer = NULL;
	struct tpu_data_pattern *v_start = NULL, *v_end = NULL, *v_cur = NULL;
	unsigned int *base = NULL;
	unsigned long p_start = 0, p_end = 0, p_buffer = 0;

	buffer = dma_alloc_coherent(&tsk_pdev->dev, 4096 * 2, (dma_addr_t *)&p_buffer, GFP_KERNEL);
	if (!buffer) {
		// pr_err("kmalloc_array %lu bytes failed\n", TPU_DATA_NUM * sizeof(struct tpu_data_pattern));
		return -ENOMEM;
	}
	v_start = (struct tpu_data_pattern *)((unsigned long)(buffer + 4096) & ~(4096));
	memset((void *)v_start, 0xff, TPU_DATA_NUM * sizeof(struct tpu_data_pattern));
	v_end = v_start + (TPU_DATA_NUM - 1);

	p_start = p_buffer + ((unsigned long)v_start - (unsigned long)buffer);
	p_end = p_buffer + ((unsigned long)v_end - (unsigned long)buffer);

	pr_info("TPU data buffer: virt = 0x%lx, phys = 0x%lx\n", (unsigned long)buffer, p_buffer);
	pr_info("TPU data start: virt = 0x%lx, phys = 0x%lx\n", (unsigned long)v_start, p_start);
	pr_info("TPU data end  : virt = 0x%lx, phys = 0x%lx\n", (unsigned long)v_end, p_end);

	// tpu monitor enable/result start address/result end address are started with 28th word.
	base = (unsigned int *)ioremap(tpu_control_register + 28 * sizeof(unsigned int), 4 * sizeof(unsigned int));
#ifdef BMPERF_DEBUG
	pr_info("tpu base init: base[0] = 0x%x, base[1] = 0x%x, base[2] = 0x%x, base[3] = 0x%x\n",
		base[0], base[1], base[2], base[3]);
#endif

	// start address low 15 bits  --> base[0] high 15bit
	base[0] &= 0x0001ffff;
	base[0] |= (p_start & 0x7fff) << 17;

	// start address high 20 bits  --> base[1] low 20 bits
	base[1] &= 0xfff00000;
	base[1] |= ((p_start >> 15) & 0xfffff);

	// end address low 12 bits --> base[1] high 12 bits
	base[1] &= 0x000fffff;
	base[1] |= (p_end & 0xfff) << 20;

	// end address high 23 bits  --> base[2] low 23 bits
	base[2] &= 0xff800000;
	base[2] |= (p_end >> 12) & 0x7fffff;

	// config cfg_cmpt_en
	base[2] |= (1 << 23);

	// set cfg_cmpt_val as 1
	base[2] &= 0x00ffffff;
	base[3] &= 0xffffff00;
	base[2] |= (1 << 24);

	// config cfg_rd_instr_en/cfg_rd_instr_stall_en/cfg_wr_instr_en
	base[3] |= (7 << 8);

	// enable tpu PM(bit 16)
	base[0] |= (1 << 16);
#ifdef BMPERF_DEBUG
	pr_info("after enabling TPU monitor: base[0] = 0x%x, base[1] = 0x%x, base[2] = 0x%x, base[3] = 0x%x\n",
		base[0], base[1], base[2], base[3]);
#endif

	// get result
	v_cur = v_start;
	while (!kthread_should_stop()) {
		if (v_cur->inst_start_time != 0xffffffff) {
			pr_debug("inst_id = %-5u start_time = %-10u end_time = %-10u cmpt_load = %-6lu num_read = %-6u num_read_stall = %-6u num_write = %-6u\n",
				v_cur->inst_id, v_cur->inst_start_time, v_cur->inst_end_time,
				(unsigned long)v_cur->computation_load,
				v_cur->num_read, v_cur->num_read_stall, v_cur->num_write);
			memset((void *)v_cur, 0xff, sizeof(*v_cur));
			v_cur++;
			if (v_cur > v_end)
				v_cur = v_start;
		} else {
			msleep(SLEEP_MS);
		}
	}

	// clear all tpu monitor settings
	base[0] &= 0x0000ffff;
	base[1] &= 0x00000000;
	base[2] &= 0x00000000;
	base[3] &= 0xfffff800;
#ifdef BMPERF_DEBUG
	pr_info("after disabling TPU monitor: base[0] = 0x%x, base[1] = 0x%x, base[2] = 0x%x, base[3] = 0x%x\n",
		base[0], base[1], base[2], base[3]);
#endif

	iounmap(base);
	msleep(SLEEP_MS * 2);
	dma_free_coherent(NULL, 4096 * 2, buffer, p_buffer);

	return 0;
}

// GDMA
static int kthread_gdma(void *data)
{
	char *buffer = NULL;
	struct gdma_data_pattern *v_start = NULL, *v_end = NULL, *v_cur = NULL;
	unsigned long p_start = 0, p_end = 0, p_buffer = 0;
	void __iomem *single_step, *monitor_enable, *monitor_address;
	unsigned int addresses[4] = {0};
	unsigned long long busy_start = 0, busy_period;
	unsigned long gsize = 0, gcount = 0, gcycle = 0, scount = 0, bandw;
	unsigned long bandw_max = 0, bandw_min = 0xFFFFFFFF; // MBps, GDMA at 575MHz

	buffer = dma_alloc_coherent(&tsk_pdev->dev, 4096 * 2, (dma_addr_t *)&p_buffer, GFP_KERNEL);
	if (!buffer) {
		// pr_err("kmalloc_array %lu bytes failed\n", GDMA_DATA_NUM * sizeof(struct gdma_data_pattern));
		return -ENOMEM;
	}
	v_start = (struct gdma_data_pattern *)((unsigned long)(buffer + 4096) & ~(4096));
	memset((void *)v_start, 0xff, GDMA_DATA_NUM * sizeof(struct gdma_data_pattern));
	v_end = v_start + GDMA_DATA_NUM;

	p_start = p_buffer + ((unsigned long)v_start - (unsigned long)buffer);
	p_end = p_buffer + ((unsigned long)v_end - (unsigned long)buffer);

	pr_info("GDMA data buffer: virt = 0x%lx, phys = 0x%lx\n", (unsigned long)buffer, p_buffer);
	pr_info("GDMA data start: virt = 0x%lx, phys = 0x%lx\n", (unsigned long)v_start, p_start);
	pr_info("GDMA data end  : virt = 0x%lx, phys = 0x%lx\n", (unsigned long)v_end, p_end);

	single_step = ioremap(gdma_base_address - 4, 4);
	writel(readl(single_step) | 1, single_step);

	// configure result start and end address
	monitor_address = ioremap(gdma_base_address + 4, sizeof(addresses));
	addresses[0] = (p_start >> 32) & 0x07;
	addresses[1] = p_start & 0xffffffff;
	addresses[2] = (p_end >> 32) & 0x07;
	addresses[3] = p_end & 0xffffffff;
	memcpy_toio(monitor_address, addresses, sizeof(addresses));

	// enable gdma PM
	monitor_enable = ioremap(gdma_base_address, sizeof(unsigned int));
	writel(readl(monitor_enable) | 1, monitor_enable);

	// get result
	v_cur = v_start;
	while (!kthread_should_stop()) {
		if (v_cur->inst_start_time != 0xffffffff) {
			if (!busy_start)
				busy_start = sched_clock();
#if 0
			pr_debug("instruction_id   = %-10u    start_time       = %-10u    end_time         = %-10u\n",
				v_cur->inst_id, v_cur->inst_start_time, v_cur->inst_end_time);
			pr_debug("d0_aw_bytes      = %-10u    d0_wr_bytes      = %-10u    d0_ar_bytes      = %-10u\n",
				v_cur->d0_aw_bytes, v_cur->d0_wr_bytes, v_cur->d0_ar_bytes);
			pr_debug("d1_aw_bytes      = %-10u    d1_wr_bytes      = %-10u    d1_ar_bytes      = %-10u\n",
				v_cur->d1_aw_bytes, v_cur->d1_wr_bytes, v_cur->d1_ar_bytes);
			pr_debug("gif_aw_bytes     = %-10u    gif_wr_bytes     = %-10u    gif_ar_bytes     = %-10u\n",
				v_cur->gif_aw_bytes, v_cur->gif_wr_bytes, v_cur->gif_ar_bytes);
			pr_debug("d0_wr_valid_cyc  = %-10u    d0_rd_valid_cyc  = %-10u    d1_wr_valid_cyc  = %-10u    d1_rd_valid_cyc  = %-10u\n",
				v_cur->d0_wr_valid_cyc, v_cur->d0_rd_valid_cyc,
				v_cur->d1_wr_valid_cyc, v_cur->d1_rd_valid_cyc);
			pr_debug("gif_wr_valid_cyc = %-10u    gif_rd_valid_cyc = %-10u    d0_wr_stall_cyc  = %-10u    d0_rd_stall_cyc  = %-10u\n",
				v_cur->gif_wr_valid_cyc, v_cur->gif_rd_valid_cyc,
				v_cur->d0_wr_stall_cyc, v_cur->d0_rd_stall_cyc);
			pr_debug("d1_wr_stall_cyc  = %-10u    d1_rd_stall_cyc  = %-10u    gif_wr_stall_cyc = %-10u    gif_rd_stall_cyc = %-10u\n",
				v_cur->d1_wr_stall_cyc, v_cur->d1_rd_stall_cyc,
				v_cur->gif_wr_stall_cyc, v_cur->gif_rd_stall_cyc);
			pr_debug("d0_aw_end        = %-10u    d0_aw_st         = %-10u    d0_ar_end        = %-10u    d0_ar_st         = %-10u\n",
				v_cur->d0_aw_end, v_cur->d0_aw_st, v_cur->d0_ar_end, v_cur->d0_ar_st);
			pr_debug("d0_wr_end        = %-10u    d0_wr_st         = %-10u    d0_rd_end        = %-10u    d0_rd_st         = %-10u\n",
				v_cur->d0_wr_end, v_cur->d0_wr_st, v_cur->d0_rd_end, v_cur->d0_rd_st);
			pr_debug("d1_aw_end        = %-10u    d1_aw_st         = %-10u    d1_ar_end        = %-10u    d1_ar_st         = %-10u\n",
				v_cur->d1_aw_end, v_cur->d1_aw_st, v_cur->d1_ar_end, v_cur->d1_ar_st);
			pr_debug("d1_wr_end        = %-10u    d1_wr_st         = %-10u    d1_rd_end        = %-10u    d1_rd_st         = %-10u\n",
				v_cur->d1_wr_end, v_cur->d1_wr_st, v_cur->d1_rd_end, v_cur->d1_rd_st);
			pr_debug("gif_aw_reserved1 = %-10u    gif_aw_reserved2 = %-10u    gif_ar_end       = %-10u    gif_ar_st        = %-10u\n",
				v_cur->gif_aw_reserved1, v_cur->gif_aw_reserved2, v_cur->gif_ar_end, v_cur->gif_ar_st);
			pr_debug("gif_wr_end       = %-10u    gif_wr_st        = %-10u    gif_rd_end       = %-10u    gif_rd_st        = %-10u\n\n",
				v_cur->gif_wr_end, v_cur->gif_wr_st, v_cur->gif_rd_end, v_cur->gif_rd_st);
#endif
			gsize += (v_cur->d0_wr_bytes + v_cur->d0_ar_bytes + v_cur->d1_wr_bytes + v_cur->d1_ar_bytes);
			gcount++;
			gcycle += (v_cur->inst_end_time - v_cur->inst_start_time);
			bandw = (v_cur->d0_wr_bytes + v_cur->d0_ar_bytes + v_cur->d1_wr_bytes + v_cur->d1_ar_bytes) *
				575 / (v_cur->inst_end_time - v_cur->inst_start_time);
			if (bandw_max < bandw)
				bandw_max = bandw;
			if (bandw_min > bandw)
				bandw_min = bandw;

			memset((void *)v_cur, 0xff, sizeof(*v_cur));
			v_cur++;
			if (v_cur >= v_end)
				v_cur = v_start;
		} else {
			scount++;
			busy_period = sched_clock() - busy_start;
			if (!busy_start || sched_clock() - busy_start > 10 * 1000000000ULL) {
				if (gcount != 0) {
					pr_info("GDMA: bytes %ld, cycle %ld, time %lld, bandw %lld/%ld/%ld/%ld, count %ld/%ld\n",
						gsize, gcycle, busy_period,
						gsize / (busy_period / 1000),
						gsize * 575 / gcycle, bandw_max, bandw_min,
						gcount, scount);
					busy_start = 0;
					gsize = 0;
					gcount = 0;
					gcycle = 0;
					scount = 0;
					bandw_max = 0;
					bandw_min = 0xFFFFFFFF;
				}

				msleep(SLEEP_MS);
			}
		}
	}

	// clear all gdma monitor settings
	writel(readl(monitor_enable) & ~1, monitor_enable);
	memset_io(monitor_address, 0x00, sizeof(addresses));

	iounmap(monitor_address);
	iounmap(monitor_enable);
	iounmap(single_step);
	dma_free_coherent(NULL, 4096 * 2, buffer, p_buffer);

	return 0;
}

static int bmperf_probe(struct platform_device *pdev)
{
	pr_info("%s: axi_base_address = 0x%lx, axi_port = %u, axi_period = %lu\n",
		__func__, axi_base_address, axi_port, axi_period);

	tsk_pdev = pdev;

	// AXI bus kthread
	tsk_axi_bus = kthread_run(kthread_axi, (void *)0, "AXI bus kthread");

	// TPU kthread
	tsk_tpu = kthread_run(kthread_tpu, (void *)0, "TPU kthread");

	// GDMA kthread
	tsk_gdma = kthread_run(kthread_gdma, (void *)0, "GDMA kthread");

	return 0;
}

static int bmperf_remove(struct platform_device *pdev)
{
	pr_info("%s is called.\n", __func__);

	if (tsk_axi_bus != NULL) {
		kthread_stop(tsk_axi_bus);
		tsk_axi_bus = NULL;
	}

	if (tsk_tpu != NULL) {
		kthread_stop(tsk_tpu);
		tsk_tpu = NULL;
	}

	if (tsk_gdma != NULL) {
		kthread_stop(tsk_gdma);
		tsk_gdma = NULL;
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
