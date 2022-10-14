#ifndef __BCE_H__
#define __BCE_H__

/* ce is short for crypto-engine */

enum {
	CE_BASE_IOC_ALG_BASE64,
	CE_BASE_IOC_ALG_MAX,
};

/**
 * struct ce_base_ioc_op_phy - baseX arguments for operation CE_BASE_IOC_OP_PHY
 * @alg: - algothm of baseX operation, only CE_BASE_IOC_ALG_BASE64 is supported
 * @enc: - encode or decode, 0 means decode otherwise is encode
 * @src: - input data address of crypto engine, physical address
 * @dst: - output data address of crypto engine, physical address
 * @len: - input data length
 * @dstlen: - output data length, only available in baseX decode
 *
 * operation CE_BASE_IOC_OP_PHY
 *
 * 1. wonnot do any cache coherent maintain.
 * that means if you use a buffer with none cache coherent,
 * you should flush input buffer and invalidate output
 * buffer before calling this command
 *
 * 2. wonnot remap physical address to kernel virtual.
 * that means i cannot know how many padding characters at the tail of alphabet
 * stream. you should tell me how many data should i write out when baseX
 * decoding
 */
struct ce_base_ioc_op_phy {
	__u32 alg;
	__u32 enc;
	__u64 src;
	__u64 dst;
	__u64 len;
	__u64 dstlen;
};

#define	CE_BASE_IOC_TYPE		('C')
#define CE_BASE_IOC_IOR(nr, type)	_IOR(CE_BASE_IOC_TYPE, nr, type)
/* do one shot operation on physical address */
#define CE_BASE_IOC_OP_PHY	\
	CE_BASE_IOC_IOR(0, struct ce_base_ioc_op_phy)

#endif
