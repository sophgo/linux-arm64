#include <ce_port.h>
#include <ce_cipher_walk.h>

static inline void walk_ptr_init(struct ce_cipher_walk_ptr *p,
				 struct scatterlist *sg)
{
	p->sg = sg;
	p->offset = 0;
}

static inline unsigned long walk_ptr_sg_left(struct ce_cipher_walk_ptr *p)
{
	return sg_dma_len(p->sg) - p->offset;
}

void ce_cipher_walk_init(struct ce_cipher_walk *walk,
			 void *temp_buf_virt,
			 dma_addr_t temp_buf_dma,
			 unsigned long (*dmacpy)(dma_addr_t dst, dma_addr_t src,
						 unsigned long len, void *priv),
			 void *dmacpy_priv,
			 int block_len,
			 unsigned long nbytes,
			 struct scatterlist *src,
			 struct scatterlist *dst)
{
	walk_ptr_init(&walk->src, src);
	walk_ptr_init(&walk->dst, dst);
	walk->src_temp_virt = temp_buf_virt;
	walk->dst_temp_virt = (char *)temp_buf_virt + WALK_MAX_BLOCK_LEN;
	walk->src_temp_dma = temp_buf_dma;
	walk->dst_temp_dma = temp_buf_dma + WALK_MAX_BLOCK_LEN;
	walk->block_len = block_len;
	walk->bytes_left = nbytes;
	walk->dst_map_len = 0;
	walk->dmacpy = dmacpy;
	walk->dmacpy_priv = dmacpy_priv;
}

static inline dma_addr_t walk_ptr_dma_addr(struct ce_cipher_walk_ptr *p)
{
	return sg_dma_address(p->sg) + p->offset;
}

static inline void *walk_ptr_virt_addr(struct ce_cipher_walk_ptr *p)
{
	return ((char *)sg_virt(p->sg)) + p->offset;
}


static inline int walk_ptr_sg_is_last(struct ce_cipher_walk_ptr *p)
{
	return p->sg == NULL;
}

static inline struct scatterlist *walk_ptr_sg_next(struct ce_cipher_walk_ptr *p)
{
	struct scatterlist *next = sg_next(p->sg);

	p->sg = next;
	p->offset = 0;
	return next;
}

static inline dma_addr_t walk_ptr_sg_step(struct ce_cipher_walk_ptr *p,
					  unsigned long n)
{
	dma_addr_t addr;

	if (p->offset + n > sg_dma_len(p->sg)) {
		ce_err("out of sg\n");
		return 0;
	}
	addr = walk_ptr_dma_addr(p);
	p->offset += n;
	while (walk_ptr_sg_left(p) == 0)
		if (walk_ptr_sg_next(p) == NULL)
			break;
	return addr;
}

static dma_addr_t walk_gather_src(struct ce_cipher_walk *walk,
				  unsigned long *len)
{
	unsigned long src_sg_left;
	unsigned long used = 0;
	unsigned long tmp;
	unsigned char *buf = walk->src_temp_virt;

	while (used < walk->block_len) {
		src_sg_left = walk_ptr_sg_left(&walk->src);
		tmp = min(walk->block_len - used, src_sg_left);
		memcpy(buf, walk_ptr_virt_addr(&walk->src), tmp);
		walk_ptr_sg_step(&walk->src, tmp);

		buf += tmp;
		used += tmp;
		if (walk_ptr_sg_is_last(&walk->src))
			break;
	}

	*len = used;

	return walk->src_temp_dma;
}

static dma_addr_t walk_gather_dst(struct ce_cipher_walk *walk)
{
	/* create temp buffer to sg list map */
	/* 1st block */
	struct ce_cipher_dst_map *dst_map = walk->dst_map;
	unsigned long dst_sg_left = walk_ptr_sg_left(&walk->dst);
	unsigned long block_left;
	int i;

	dst_map[0].src = walk->dst_temp_dma;
	dst_map[0].dst = walk_ptr_sg_step(&walk->dst, dst_sg_left);
	dst_map[0].len = dst_sg_left;
	block_left = walk->block_len - dst_sg_left;
	/* one block should feed done */
	for (i = 1; block_left && !walk_ptr_sg_is_last(&walk->dst); ++i) {
		dst_sg_left = walk_ptr_sg_left(&walk->dst);
		dst_map[i].src =
			dst_map[i - 1].src + dst_map[i - 1].len;
		dst_map[i].len = min(dst_sg_left, block_left);
		dst_map[i].dst =
			walk_ptr_sg_step(&walk->dst, dst_map[i].len);
		block_left -= dst_map[i].len;
	}
	walk->dst_map_len = i;
	return walk->dst_temp_dma;
}

unsigned long ce_cipher_walk_next(struct ce_cipher_walk *walk,
				  struct ce_cipher_walk_buffer *dma_buf)
{
	unsigned long len;
	unsigned long src_sg_left;
	unsigned long dst_sg_left;

	if (walk->bytes_left == 0)
		return 0;

	if (walk->src.sg == NULL || walk->dst.sg == NULL) {
		ce_err("scatterlist donnot have enough data\n");
		return 0;
	}

	src_sg_left = walk_ptr_sg_left(&walk->src);
	dst_sg_left = walk_ptr_sg_left(&walk->dst);

	len = rounddown(min(src_sg_left, dst_sg_left), walk->block_len);
	if (len) {
		dma_buf->len = len;
		dma_buf->src = walk_ptr_sg_step(&walk->src, len);
		dma_buf->dst = walk_ptr_sg_step(&walk->dst, len);
	} else {
		dma_buf->len = walk->block_len;

		if (src_sg_left >= walk->block_len) {
			dma_buf->src =
				walk_ptr_sg_step(&walk->src, walk->block_len);
			len = walk->block_len;
		} else
			/* gather source */
			dma_buf->src = walk_gather_src(walk, &len);

		if (dst_sg_left >= walk->block_len)
			dma_buf->dst =
				walk_ptr_sg_step(&walk->dst, walk->block_len);
		else
			/* gather destination */
			dma_buf->dst = walk_gather_dst(walk);
	}
	walk->bytes_left -= len;
	return len;
}

int ce_cipher_walk_done(struct ce_cipher_walk *walk)
{
	int i;
	struct ce_cipher_dst_map *map;

	for (i = 0; i < walk->dst_map_len; ++i) {
		map = walk->dst_map + i;
		if (walk->dmacpy(map->dst, map->src,
				 map->len, walk->dmacpy_priv) != map->len) {
			return -1;
		}
	}
	walk->dst_map_len = 0;
	return 0;
}
