#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "vpp_platform.h"

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

