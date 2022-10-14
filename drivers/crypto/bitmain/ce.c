#include <ce.h>
#include <ce_port.h>

struct ce_desc {
	ce_u32_t     ctrl;
	ce_u32_t     alg;
	ce_u64_t     next;
	ce_u64_t     src;
	ce_u64_t     dst;
	ce_u64_t     len;
	union {
		ce_u8_t      key[32];
		ce_u64_t     dstlen; //destination amount, only used in base64
	};
	ce_u8_t      iv[16];
};

struct ce_reg {
	ce_u32_t ctrl; //control
	ce_u32_t intr_enable; //interrupt mask
	ce_u32_t desc_low; //descriptor base low 32bits
	ce_u32_t desc_high; //descriptor base high 32bits
	ce_u32_t intr_raw; //interrupt raw, write 1 to clear
	ce_u32_t se_key_valid; //secure key valid, read only
	ce_u32_t cur_desc_low; //current reading descriptor
	ce_u32_t cur_desc_high; //current reading descriptor
	ce_u32_t r1[24]; //reserved
	ce_u32_t desc[22]; //PIO mode descriptor register
	ce_u32_t r2[10]; //reserved
	ce_u32_t key0[8];//keys
	ce_u32_t key1[8];//keys
	ce_u32_t key2[8];//keys
	ce_u32_t r3[8]; //reserved
	ce_u32_t iv[12]; //iv
	ce_u32_t r4[4]; //reserved
	ce_u32_t sha_param[8]; //sha parameter
	ce_u32_t sm3_param[8];//sm3 parameter
};

struct cipher_info {
	ce_u32_t	ctrl;
	ce_u32_t	alg;
	unsigned int	block_len;
	unsigned int	key_len;
};

struct hash_info {
	ce_u32_t	ctrl;
	ce_u32_t	alg;
	unsigned int	block_len;
	unsigned int	hash_len;
};

struct base_info {
	ce_u32_t	ctrl;
	unsigned int	data_align;
	unsigned int	encoded_align;
};

struct cipher_info const cipher_info[] = {
	[CE_DES_ECB] = {
		.ctrl = 0x01880407,
		.alg = 0x00000000,
		.block_len = 8,
		.key_len = 8,
	},
	[CE_DES_CBC] = {
		.ctrl = 0x01880407,
		.alg = 0x00000002,
		.block_len = 8,
		.key_len = 8,
	},
	[CE_DES_CTR] = {
		.ctrl = 0x01880407,
		.alg = 0x00000004,
		.block_len = 8,
		.key_len = 8,
	},
	[CE_DES_EDE3_ECB] = {
		.ctrl = 0x01880407,
		.alg = 0x00000008,
		.block_len = 8,
		.key_len = 24,
	},
	[CE_DES_EDE3_CBC] = {
		.ctrl = 0x01880407,
		.alg = 0x0000000a,
		.block_len = 8,
		.key_len = 24,
	},
	[CE_DES_EDE3_CTR] = {
		.ctrl = 0x01880407,
		.alg = 0x0000000c,
		.block_len = 8,
		.key_len = 24,
	},
	[CE_AES_128_ECB] = {
		.ctrl = 0x01880207,
		.alg = 0x00000020,
		.block_len = 16,
		.key_len = 16,
	},
	[CE_AES_128_CBC] = {
		.ctrl = 0x01880207,
		.alg = 0x00000022,
		.block_len = 16,
		.key_len = 16,
	},
	[CE_AES_128_CTR] = {
		.ctrl = 0x01880207,
		.alg = 0x00000024,
		.block_len = 16,
		.key_len = 16,
	},
	[CE_AES_192_ECB] = {
		.ctrl = 0x01880207,
		.alg = 0x00000010,
		.block_len = 16,
		.key_len = 24,
	},
	[CE_AES_192_CBC] = {
		.ctrl = 0x01880207,
		.alg = 0x00000012,
		.block_len = 16,
		.key_len = 24,
	},
	[CE_AES_192_CTR] = {
		.ctrl = 0x01880207,
		.alg = 0x00000014,
		.block_len = 16,
		.key_len = 24,
	},
	[CE_AES_256_ECB] = {
		.ctrl = 0x01880207,
		.alg = 0x00000008,
		.block_len = 16,
		.key_len = 32,
	},
	[CE_AES_256_CBC] = {
		.ctrl = 0x01880207,
		.alg = 0x0000000a,
		.block_len = 16,
		.key_len = 32,
	},
	[CE_AES_256_CTR] = {
		.ctrl = 0x01880207,
		.alg = 0x0000000c,
		.block_len = 16,
		.key_len = 32,
	},
	[CE_SM4_ECB] = {
		.ctrl = 0x01880807,
		.alg = 0x00000000,
		.block_len = 16,
		.key_len = 16,
	},
	[CE_SM4_CBC] = {
		.ctrl = 0x01880807,
		.alg = 0x00000002,
		.block_len = 16,
		.key_len = 16,
	},
	[CE_SM4_CTR] = {
		.ctrl = 0x01880807,
		.alg = 0x00000004,
		.block_len = 16,
		.key_len = 16,
	},
	[CE_SM4_OFB] = {
		.ctrl = 0x01880807,
		.alg = 0x00000008,
		.block_len = 16,
		.key_len = 16,
	},
};


struct hash_info const hash_info[] = {
	[CE_SHA1] = {
		.ctrl = 0x01881007,
		.alg = 0x00000001,
		.block_len = 64,
		.hash_len = 20,
	},
	[CE_SHA256] = {
		.ctrl = 0x01881007,
		.alg = 0x00000003,
		.block_len = 64,
		.hash_len = 32,
	},
	[CE_SM3] = {
		.ctrl = 0x01884007,
		.alg = 0x00000001,
		.block_len = 64,
		.hash_len = 32,
	},
};


struct base_info const base_info[] = {
	[CE_BASE64] = {
		.ctrl		= 0x01882007,
		.data_align	= 3,
		.encoded_align	= 4,
	},
};

static void trigger(void *dev, void *desc,
		    int write_burst_length, int read_burst_length)
{
	struct ce_reg *reg;
	int i;

	reg = dev;

	ce_writel(reg->ctrl, (write_burst_length << 16) |
			     (read_burst_length << 24));
	/* clear all pending interruts */
	ce_writel(reg->intr_raw, 0xffffffff);
	/* enable all interrupts */
	ce_writel(reg->intr_enable, 0xffffffff);

	for (i = 0; i < sizeof(reg->desc) / sizeof(ce_u32_t); ++i)
		ce_writel(reg->desc[i], ((ce_u32_t *)desc)[i]);

	ce_setbit(reg->ctrl, 0);
}

unsigned int ce_int_status(void *dev)
{
	struct ce_reg *reg;

	reg = dev;
	return ce_readl(reg->intr_raw);
}

void ce_int_clear(void *dev)
{
	struct ce_reg *reg;

	reg = dev;
	ce_writel(reg->intr_raw, 0xffffffff);
}

int ce_status(void *dev)
{
	struct ce_reg *reg;

	reg = dev;
	return ce_getbit(reg->ctrl, 0);
}

int ce_is_secure_key_valid(void *dev)
{
	struct ce_reg *reg = dev;

	return (ce_readl(reg->se_key_valid)) == 0 ? 0 : 1;
}

int ce_cipher_do(struct ce_ctx *ctx, struct ce_cipher *cipher)
{
	struct ce_reg *reg;
	struct ce_desc desc;
	struct cipher_info const *info;

	if (!ctx->is_support[cipher->alg])
		return -CE_EALG;

	info = cipher_info + cipher->alg;

	ce_dbg("algrithm\t%d\t%s\t%lu\n",
	       cipher->alg, cipher->enc ? "encrypt" : "decrypt", cipher->len);
	if (cipher->iv) {
		ce_dbg("iv:\n");
	}
	if (cipher->key) {
		ce_dbg("key:\n");
	}
	ce_dbg("source address %p, destination address %p, length %lu\n",
		cipher->src, cipher->dst, cipher->len);

	if (cipher->len % info->block_len) {
		ce_err("only support block aligned operations\n");
		return -CE_EBLKLEN;
	}

	reg = ctx->reg;

	if (!ce_is_secure_key_valid(reg) && cipher->key == NULL) {
		ce_err("secure key not valid, you should pass a key to me\n");
		return -CE_ESKEY;
	}

	memset(&desc, 0x00, sizeof(desc));
	desc.ctrl = info->ctrl;
	desc.alg = info->alg | (cipher->enc ? 1:0);
	desc.next = 0; //no next descriptor
	desc.src = (ce_u64_t)cipher->src;
	desc.dst = (ce_u64_t)cipher->dst;
	desc.len = cipher->len;
	if (cipher->key) {
		memcpy((void *)desc.key, cipher->key, info->key_len);
	} else {
		/* bit 16:19 and 27 should be one hot, bit 27 means select
		 * secure key which comes from efuse
		 */
		desc.ctrl &= ~(0xf << 16);
		desc.ctrl |= 1 << 27;
	}

	if (cipher->iv)
		memcpy((void *)desc.iv, cipher->iv, info->block_len);
	else
		memset((void *)desc.iv, 0x00, info->block_len);

	trigger(reg, &desc, ctx->write_burst_length, ctx->read_burst_length);

	return 0;
}

int ce_cipher_block_len(unsigned int alg)
{
	return cipher_info[alg].block_len;
}

int ce_save_iv(struct ce_ctx *ctx, struct ce_cipher *cipher)
{
	struct ce_reg *reg;
	struct cipher_info const *info;
	unsigned long cnt;
	unsigned long i;
	ce_u8_t *mem;
	ce_u32_t tmp;

	if (!ctx->is_support[cipher->alg])
		return -CE_EINVAL;

	if (cipher->iv_out == NULL)
		return -CE_EINVAL;

	reg = ctx->reg;
	info = cipher_info + cipher->alg;

	mem = cipher->iv_out;
	cnt = info->block_len / 4;

	for (i = 0; i < cnt; ++i) {
		tmp = ce_readl(reg->iv[i]);
		mem[i * 4 + 0] = (tmp >> 0) & 0xff;
		mem[i * 4 + 1] = (tmp >> 8) & 0xff;
		mem[i * 4 + 2] = (tmp >> 16) & 0xff;
		mem[i * 4 + 3] = (tmp >> 24) & 0xff;
	}

	return 0;
}

int ce_hash_do(struct ce_ctx *ctx, struct ce_hash *hash)
{
	struct ce_reg *reg;
	struct ce_desc desc;
	struct hash_info const *info;
	int i, j;

	if (!ctx->is_support[hash->alg])
		return -CE_EALG;

	info = hash_info + hash->alg;

	if (hash->len % info->block_len) {
		ce_err("only support aligned length and no padding\n");
		return -CE_EBLKLEN;
	}
	if (hash->init == NULL) {
		ce_err("no standard initialize parameter support\n");
		ce_err("Init yourself\n");
		return -CE_EINVAL;
	}
	if (hash->hash == NULL) {
		ce_err("no output buffer\n");
		return -CE_ENULL;
	}

	reg = ctx->reg;

	desc.ctrl = info->ctrl;
	desc.alg = info->alg;
	desc.next = 0; //no next descriptor
	desc.src = (ce_u64_t)hash->src;
	desc.dst = 0;
	desc.len = hash->len;
	if (hash->alg != CE_SM3)
		memcpy((void *)desc.key, hash->init, info->hash_len);
	else {
		for (i = 0; i < info->hash_len / 4; i++)
			for (j = 0; j < 4; j++)
				*(desc.key + i * 4 + j)
				= *((unsigned char *)(hash->init) + 4 * i + 3 - j);
	}

	trigger(reg, &desc, ctx->write_burst_length, ctx->read_burst_length);

	return 0;
}

int ce_hash_hash_len(unsigned int alg)
{
	return hash_info[alg].hash_len;
}

int ce_hash_block_len(unsigned int alg)
{
	return hash_info[alg].block_len;
}

int ce_save_hash(void *dev, struct ce_hash *hash)
{
	struct ce_reg *reg;
	struct hash_info const *info;
	unsigned long cnt;
	unsigned long i;
	ce_u8_t *mem;
	ce_u32_t tmp;

	if (hash->hash == NULL)
		return -CE_EINVAL;

	reg = dev;
	info = hash_info + hash->alg;

	cnt = info->hash_len / 4;
	mem = hash->hash;

	if (hash->alg != CE_SM3) {
		for (i = 0; i < cnt; ++i) {
			tmp = ce_readl(reg->sha_param[i]);
			mem[i * 4 + 0] = (tmp >> 0) & 0xff;
			mem[i * 4 + 1] = (tmp >> 8) & 0xff;
			mem[i * 4 + 2] = (tmp >> 16) & 0xff;
			mem[i * 4 + 3] = (tmp >> 24) & 0xff;
		}
	} else {
		for (i = 0; i < cnt; ++i) {
			tmp = ce_readl(reg->sm3_param[i]);
			mem[i * 4 + 0] = (tmp >> 24) & 0xff;
			mem[i * 4 + 1] = (tmp >> 16) & 0xff;
			mem[i * 4 + 2] = (tmp >> 8) & 0xff;
			mem[i * 4 + 3] = (tmp >> 0) & 0xff;
		}
	}

	return 0;
}

int ce_base_do(struct ce_ctx *ctx, struct ce_base *base)
{
	struct base_info const *info;
	struct ce_reg *reg;
	struct ce_desc desc;
	unsigned long dst_len_min, dst_len_max;

	if (base->alg >= CE_BASE_MAX) {
		ce_err("unknown base N method [%d]\n", base->alg);
		return -1;
	}

	ce_dbg("algorithm %d, %s\n",
	       base->alg, base->enc ? "encode" : "decode");
	ce_dbg("src %p, dst %p, len %lu\n",
	       base->src, base->dst, base->len);

	reg = ctx->reg;
	info = base_info + base->alg;

	if (!base->enc)
		if (base->len % info->encoded_align) {
			ce_err("encoded data must %d bytes aligned\n",
			       info->encoded_align);
			return -1;
		}

	memset(&desc, 0x00, sizeof(desc));
	desc.ctrl = info->ctrl;
	desc.alg = base->enc ? 1 : 0;
	desc.next = 0; //no next descriptor
	desc.src = (ce_u64_t)base->src;
	desc.dst = (ce_u64_t)base->dst;
	desc.len = base->len;
	if (base->enc)
		desc.dstlen = (base->len + info->data_align - 1) /
			info->data_align * info->encoded_align;
	else {
		/* check dest length
		 * i donnot know the data in last block
		 * but dest length should located in a reasonable range
		 */
		dst_len_max =
			base->len / info->encoded_align * info->data_align;
		dst_len_min = dst_len_max - info->data_align;
		if (base->dstlen > dst_len_min && base->dstlen <= dst_len_max)
			desc.dstlen = base->dstlen;
		else {
			ce_err("destination length not in a reasonable range");
			return -1;
		}
	}

	trigger(reg, &desc, ctx->write_burst_length, ctx->read_burst_length);

	return 0;
}

int ce_dmacpy(void *dev, void *dst, void *src, unsigned long len)
{
	struct ce_reg *reg = dev;
	struct ce_desc *desc = (struct ce_desc *)reg->desc;

	ce_dbg("dma copy from %p to %p with %lu bytes long\n",
	       src, dst, len);

	//pio mode, read max burst 16, write mas burst 16
	ce_writel(reg->ctrl, (16 << 16) | (16 << 24));
	/* clear all pending interruts */
	ce_writel(reg->intr_raw, 0xffffffff);
	/* disable all interrupts */
	/* we use polling mode */
	ce_writel(reg->intr_enable, 0);

	/* bypass cipher op */
	desc->ctrl = 0x00000107;
	desc->next = 0;
	desc->src = (ce_u64_t)src;
	desc->dst = (ce_u64_t)dst;
	desc->len = len;
	ce_setbit(reg->ctrl, 0);
	while (ce_getbit(reg->ctrl, 0))
		/* crypto engine is busy */
		;
	return 0;
}
