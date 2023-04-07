#ifndef _SOPHGO_CDMA_H_
#define _SOPHGO_CDMA_H_

/* CDMA bit definition */
#define SOPHGO_CDMA_ENABLE_BIT 0
#define SOPHGO_CDMA_INT_ENABLE_BIT 3

/*cdma register*/
#define CDMA_MAIN_CTRL 0x800
#define CDMA_INT_MASK 0x808
#define CDMA_INT_STATUS 0x80c
#define CDMA_SYNC_STAT 0x814
#define CDMA_CMD_ACCP0 0x878
#define CDMA_CMD_ACCP1 0x87c
#define CDMA_CMD_ACCP2 0x880
#define CDMA_CMD_ACCP3 0x884
#define CDMA_CMD_ACCP4 0x888
#define CDMA_CMD_ACCP5 0x88c
#define CDMA_CMD_ACCP6 0x890
#define CDMA_CMD_ACCP7 0x894
#define CDMA_CMD_ACCP8 0x898
#define CDMA_CMD_ACCP9 0x89c
#define CDMA_CMD_9F8 0x9f8

#define TOP_CDMA_LOCK 0x040

enum sg_memcpy_dir {
	HOST2CHIP,
	CHIP2HOST,
	CHIP2CHIP
};

struct sg_cdma_arg {
	unsigned long long src;
	unsigned long long dst;
	unsigned long long size;
	enum sg_memcpy_dir dir;
};

void sg_construct_cdma_arg(struct sg_cdma_arg *p_arg, u64 src, u64 dst, u64 size, enum sg_memcpy_dir dir);

#endif
