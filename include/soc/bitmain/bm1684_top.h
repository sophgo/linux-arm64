#ifndef __BM1684_TOP_H
#define __BM1684_TOP_H

#define CHIP_ID_VERSION			0x0
#define TOP_CTRL_REG			0x08
#define VMON_TMON_MUX_SEL		0x14
#define AUTO_CLOCK_GATING               0x1C0


void efuse_ft_get_video_cap(int *cap);
void efuse_ft_get_bin_type(int *cap);

#endif
