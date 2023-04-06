#ifndef _SOPHGO_COMMON_H_
#define _SOPHGO_COMMON_H_

#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>
#include <linux/types.h>

#include "sophgo_pt.h"
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef u64 dma_addr_t;

#define HOST_READY_FLAG 0x01234567
#define DEVICE_READY_FLAG 0x89abcde

#define PT_ALIGN 8
#define VETH_DEFAULT_MTU 65536

#define SHM_HOST_PHY_OFFSET 0x00
#define SHM_HOST_LEN_OFFSET 0x08
#define SHM_HOST_HEAD_OFFSET 0x10
#define SHM_HOST_TAIL_OFFSET 0x14
#define SHM_HANDSHAKE_OFFSET 0x40

#define TOP_MISC_GP_REG14_STS_OFFSET 0x0b8
#define TOP_MISC_GP_REG14_SET_OFFSET 0x190
#define TOP_MISC_GP_REG14_CLR_OFFSET 0x194

#define TOP_MISC_GP_REG15_STS_OFFSET 0x0bc
#define TOP_MISC_GP_REG15_SET_OFFSET 0x198
#define TOP_MISC_GP_REG15_CLR_OFFSET 0x19c

struct veth_dev {
	struct platform_device *pdev;

	void __iomem *shm_cfg_reg;
	void __iomem *top_misc_reg;
	void __iomem *cdma_cfg_reg;
	void __iomem *intc_cfg_reg;

	struct resource *shm_cfg_res;
	struct resource *top_misc_res;
	struct resource *cdma_cfg_res;
	struct resource *intc_cfg_res;

	int rx_irq;
	atomic_t link;
	struct pt *pt;
	/* lock for operate cdma */
	spinlock_t lock_cdma;

	struct work_struct net_state_worker;
	struct napi_struct napi;
	struct net_device *ndev;
};

void sg_write32(void __iomem *base, u32 offset, u32 value);
u32 sg_read32(void __iomem *base, u32 offset);
#endif
