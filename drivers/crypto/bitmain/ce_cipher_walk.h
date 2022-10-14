#ifndef __CE_CIPHER_WALK_H__
#define __CE_CIPHER_WALK_H__

#define WALK_MAX_BLOCK_LEN	(32)

struct ce_cipher_walk_ptr {
	struct scatterlist *sg;
	unsigned int offset;
};

struct ce_cipher_dst_map {
	dma_addr_t src, dst;
	unsigned int len;
};

struct ce_cipher_walk {
	struct ce_cipher_walk_ptr src, dst;
	void *src_temp_virt, *dst_temp_virt;
	dma_addr_t src_temp_dma, dst_temp_dma;
	struct ce_cipher_dst_map dst_map[WALK_MAX_BLOCK_LEN];
	unsigned int dst_map_len;
	int block_len;
	unsigned long bytes_left;
	unsigned long (*dmacpy)(dma_addr_t dst, dma_addr_t src,
				unsigned long len, void *priv);
	void *dmacpy_priv;
};

struct ce_cipher_walk_buffer {
	dma_addr_t src;
	dma_addr_t dst;
	unsigned long len;
};

void ce_cipher_walk_init(struct ce_cipher_walk *walk,
			 void *temp_buf_virt,
			 dma_addr_t temp_buf_dma,
			 unsigned long (*dmacpy)(dma_addr_t dst, dma_addr_t src,
						 unsigned long len, void *priv),
			 void *dmacpy_priv,
			 int block_len,
			 unsigned long nbytes,
			 struct scatterlist *src,
			 struct scatterlist *dst);
unsigned long ce_cipher_walk_next(struct ce_cipher_walk *walk,
				  struct ce_cipher_walk_buffer *dma_buf);
int ce_cipher_walk_done(struct ce_cipher_walk *walk);


#endif
