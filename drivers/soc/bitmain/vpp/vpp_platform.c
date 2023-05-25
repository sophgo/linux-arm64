#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include "vpp_platform.h"

static DEFINE_MUTEX(s_vpp_proc_lock);

static int __init bm_vpp_init(void)
{
	unsigned int reg_val = sophon_get_chip_id();
	int err = 0;

	pr_info("0x%x,%s\n", reg_val, __func__);

	if (reg_val == 0x16840000)
		err = platform_driver_register(&bm_vpp_driver);
	else if (reg_val == 0x16860000)
		err = platform_driver_register(&bm1684x_vpp_driver);
	else
		err = -1;

	return err;
}

module_init(bm_vpp_init);
MODULE_DESCRIPTION("VPP driver");
MODULE_LICENSE("GPL");

static int check_vpp_core_busy(struct vpp_statistic_info *vpp_usage_info, int coreIdx)
{
	int ret = 0;

	if (atomic_read(&vpp_usage_info->vpp_busy_status[coreIdx]) > 0)
		ret = 1;

	return ret;
}

static int bm_vpp_check_usage_info(struct vpp_statistic_info *vpp_usage_info)
{
	int ret = 0, i;

	mutex_lock(&s_vpp_proc_lock);
	/* update usage */
	for (i = 0; i < VPP_CORE_MAX; i++) {
		int busy = check_vpp_core_busy(vpp_usage_info, i);
		int vpp_core_usage = 0;
		int j;

		vpp_usage_info->vpp_status_array[i][vpp_usage_info->vpp_status_index[i]] = busy;
		vpp_usage_info->vpp_status_index[i]++;
		vpp_usage_info->vpp_status_index[i] %= MAX_VPP_STAT_WIN_SIZE;

		if (busy == 1)
			vpp_usage_info->vpp_working_time_in_ms[i] +=
				vpp_usage_info->vpp_instant_interval / MAX_VPP_STAT_WIN_SIZE;

		vpp_usage_info->vpp_total_time_in_ms[i] += vpp_usage_info->vpp_instant_interval / MAX_VPP_STAT_WIN_SIZE;

		for (j = 0; j < MAX_VPP_STAT_WIN_SIZE; j++)
			vpp_core_usage += vpp_usage_info->vpp_status_array[i][j];

		vpp_usage_info->vpp_core_usage[i] = vpp_core_usage;

	}
	mutex_unlock(&s_vpp_proc_lock);

	return ret;
}

int vpp_set_interval(struct vpp_statistic_info *vpp_usage_info, int time_interval)
{
	mutex_lock(&s_vpp_proc_lock);
	vpp_usage_info->vpp_instant_interval = time_interval;
	mutex_unlock(&s_vpp_proc_lock);

	return 0;
}

static void vpp_usage_info_init(struct vpp_statistic_info *vpp_usage_info)
{
	memset(vpp_usage_info, 0, sizeof(struct vpp_statistic_info));

	vpp_usage_info->vpp_instant_interval = 500;

	bm_vpp_check_usage_info(vpp_usage_info);
}

int vpp_monitor_thread(void *data)
{
	struct vpp_statistic_info *vpp_usage_info = (struct vpp_statistic_info *)data;

	set_current_state(TASK_INTERRUPTIBLE);
	vpp_usage_info_init(vpp_usage_info);

	while (!kthread_should_stop()) {
		bm_vpp_check_usage_info(vpp_usage_info);
		msleep_interruptible(100);
	}

	return 0;
}
