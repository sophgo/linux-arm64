#ifndef __CE_INST_H__
#define __CE_INST_H__

#include "ce_port.h"
#include "ce_types.h"
#include "ce.h"

#define CE_INST_ALLOC_ALIGN	(8)

struct alg_attribute {
	int cipher_map[CE_CIPHER_MAX];
	int hash_map[CE_HASH_MAX];
	int write_burst_length;
	int read_burst_length;
};

struct ce_inst {
	/* get from probe */
	struct platform_device *dev;
	void *reg;
	int irq;

	/* kernel resources */
	struct work_struct qwork;
	struct crypto_queue queue; /* request queue */
	spinlock_t lock;
	struct completion comp;

	/* status */
	int busy;

	/* clock & reset framework */
	struct clk *clk;
	unsigned long rate;
	struct reset_control *rst;

	/* cache coherent temporary buffer */
	/* for scatterlist node which not block length aligned */
	dma_addr_t tmp_buf_dma;
	void *tmp_buf_cpu;

	/* current pointer, for instance allocator */
	unsigned long allocator_offset;

	struct alg_attribute *alg_attribute;
};

/* allocate and init an instance */
int ce_inst_new(struct ce_inst **inst, struct platform_device *dev,
		struct alg_attribute *alg_attribute);
void ce_inst_free(struct ce_inst *this);
int ce_inst_enqueue_request(struct ce_inst *this,
			    struct crypto_async_request *req);
void ce_inst_wait(struct ce_inst *this);
/* allocate memory from device poll */
void *ce_inst_alloc(struct ce_inst *this, unsigned long size);

#endif
