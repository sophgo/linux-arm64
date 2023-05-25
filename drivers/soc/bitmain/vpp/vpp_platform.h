extern unsigned int sophon_get_chip_id(void);
extern struct platform_driver bm_vpp_driver;
extern struct platform_driver bm1684x_vpp_driver;
#define VPP_CORE_MAX  2
#define MAX_VPP_STAT_WIN_SIZE  100
struct vpp_statistic_info {
	uint64_t vpp_working_time_in_ms[VPP_CORE_MAX];
	uint64_t vpp_total_time_in_ms[VPP_CORE_MAX];
	atomic_t vpp_busy_status[VPP_CORE_MAX];
	char vpp_status_array[VPP_CORE_MAX][MAX_VPP_STAT_WIN_SIZE];
	int vpp_status_index[VPP_CORE_MAX];
	int vpp_core_usage[VPP_CORE_MAX];
	int vpp_instant_interval;
};
int vpp_monitor_thread(void *data);
int vpp_set_interval(struct vpp_statistic_info *vpp_usage_info, int time_interval);
unsigned long msleep_interruptible(unsigned int msecs);
