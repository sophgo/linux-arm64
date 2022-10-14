#include <ce_port.h>
#include <ce_types.h>
#include <ce.h>
#include <ce_dev.h>
#include <ce_inst.h>
#include <ce_soft_hash.h>
#include <crypto/sm3.h>
#include <crypto/sm3_base.h>
#include <linux/kernel.h>

struct ce_hash_ctx {
	int alg; /* hash algorithm */
	u8 hash[MAX_HASHLEN];
	/* count in bytes of data processed*/
	unsigned int count;
	/* data wait to process */
	u8 buf[MAX_HASH_BLOCKLEN];
	int final;
	int err;
};

static void __maybe_unused ctx_info(struct ce_hash_ctx *this)
{
	ce_info("type %s\n", "hash");
	ce_info("err %d\n", this->err);
	ce_info("alg %d\n", this->alg);
	ce_info("hash");
	dumpp(this->hash, MAX_HASHLEN);
	ce_info("count (%u bytes)", this->count);
	dumpp(this->buf, this->count);
}

static inline void *ahash_request_tfmctx(struct ahash_request *req)
{
	return crypto_ahash_ctx(crypto_ahash_reqtfm(req));
}

static const u8 sha1_init_state[] = {
	0x67, 0x45, 0x23, 0x01,
	0xef, 0xcd, 0xab, 0x89,
	0x98, 0xba, 0xdc, 0xfe,
	0x10, 0x32, 0x54, 0x76,
	0xc3, 0xd2, 0xe1, 0xf0,
};

static const u8 sha256_init_state[] = {
	0x6a, 0x09, 0xe6, 0x67,
	0xbb, 0x67, 0xae, 0x85,
	0x3c, 0x6e, 0xf3, 0x72,
	0xa5, 0x4f, 0xf5, 0x3a,
	0x51, 0x0e, 0x52, 0x7f,
	0x9b, 0x05, 0x68, 0x8c,
	0x1f, 0x83, 0xd9, 0xab,
	0x5b, 0xe0, 0xcd, 0x19,
};

static const u8 sm3_init_state[] = {
	0x73, 0x80, 0x16, 0x6f,
	0x49, 0x14, 0xb2, 0xb9,
	0x17, 0x24, 0x42, 0xd7,
	0xda, 0x8a, 0x06, 0x00,
	0xa9, 0x6f, 0x30, 0xbc,
	0x16, 0x31, 0x38, 0xaa,
	0xe3, 0x8d, 0xee, 0x4d,
	0xb0, 0xfb, 0x0e, 0x4e,
};

static const u8 *hash_init_state_table[] = {
	[CE_SHA1] = sha1_init_state,
	[CE_SHA256] = sha256_init_state,
	[CE_SM3] = sm3_init_state,
};

static void ctx_transform(struct ce_hash_ctx *hash_ctx)
{
	u32 hash[MAX_HASHLEN / 4];
	unsigned long hashlen = ce_hash_hash_len(hash_ctx->alg);
	int i;

	ce_assert(hashlen % 4 == 0);

	for (i = 0; i < hashlen / 4; ++i)
		hash[i] = be32_to_cpu(((u32 *)hash_ctx->hash)[i]);

	if (hash_ctx->alg == CE_SHA1)
		ce_sha1_transform(hash, hash_ctx->buf);
	else if (hash_ctx->alg == CE_SHA256)
		ce_sha256_transform(hash, hash_ctx->buf);
	else
		ce_sm3_transform(hash, hash_ctx->buf);

	for (i = 0; i < hashlen / 4; ++i)
		((u32 *)hash_ctx->hash)[i] = cpu_to_be32(hash[i]);
}

static int ce_hash_process_single_sg(struct ce_inst *inst, struct ce_hash_ctx *hash_ctx,
				     struct ce_hash *cfg,
				     struct scatterlist *src)
{
	u8 *vaddr = sg_virt(src);
	unsigned long len = sg_dma_len(src);
	dma_addr_t paddr = sg_dma_address(src);
	unsigned long blocklen = ce_hash_block_len(hash_ctx->alg);
	unsigned int partial = hash_ctx->count % blocklen;
	struct ce_ctx ctx = {0};

	hash_ctx->count += len;
	ctx.reg = inst->reg;
	memcpy(ctx.is_support, inst->alg_attribute->hash_map,
			sizeof(ctx.is_support));
	ctx.write_burst_length = inst->alg_attribute->write_burst_length;
	ctx.read_burst_length = inst->alg_attribute->read_burst_length;

	if (unlikely((partial + len) >= blocklen)) {
		int blocks;

		if (partial) {
			int p = blocklen - partial;

			memcpy(hash_ctx->buf + partial, vaddr, p);
			vaddr += p;
			paddr += p;
			len -= p;

			ctx_transform(hash_ctx);
		}

		blocks = len / blocklen;
		len %= blocklen;

		if (blocks) {
			cfg->src = (void *)paddr;
			cfg->len = blocks * blocklen;
			hash_ctx->err = ce_hash_do(&ctx, cfg);
			if (hash_ctx->err) {
				hash_ctx->err = -EINVAL;
				return -EINVAL;
			}

			ce_inst_wait(inst);
			ce_save_hash(inst->reg, cfg);

			vaddr += blocks * blocklen;
			paddr += blocks * blocklen;
		}
		partial = 0;
	}
	if (len)
		memcpy(hash_ctx->buf + partial, vaddr, len);

	return 0;
}

void ce_hash_handle_request(struct ce_inst *inst,
			    struct crypto_async_request *req)
{
	struct ahash_request *hash_req;
	struct scatterlist *src;
	struct ce_hash cfg;
	struct ce_hash_ctx *hash_ctx;
	struct device *dev;

	hash_req = ahash_request_cast(req);
	hash_ctx = ahash_request_tfmctx(hash_req);
	src = hash_req->src;
	dev = &inst->dev->dev;

	ce_assert(hash_ctx->err == 0);

	if (hash_req->nbytes == 0) {
		hash_ctx->err = 0;
		goto err_nop;
	}

	if (dma_map_sg(dev, hash_req->src, sg_nents(hash_req->src),
		       DMA_TO_DEVICE) == 0) {
		ce_err("map source scatter list failed\n");
		hash_ctx->err = -ENOMEM;
		goto err_nop;
	}

	cfg.alg = hash_ctx->alg;
	cfg.init = hash_ctx->hash;
	cfg.hash = hash_ctx->hash;

	for (; src; src = sg_next(src)) {
		if (ce_hash_process_single_sg(inst, hash_ctx, &cfg, src))
			goto err_unmap;
	}

err_unmap:
	dma_unmap_sg(dev, hash_req->src, sg_nents(hash_req->src),
		     DMA_TO_DEVICE);
err_nop:
	req->complete(req, hash_ctx->err);
}

/*****************************************************************************/

struct ahash_alg_container {
	int registered;
	struct ahash_alg alg;
};

static const char * const hash_name_table[] = {
	[CE_SHA1] = "sha1",
	[CE_SHA256] = "sha256",
	[CE_SM3] = "sm3",
};

static int get_type_by_name(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hash_name_table); ++i)
		if (strcmp(name, hash_name_table[i]) == 0)
			break;
	ce_assert(i < CE_HASH_MAX);
	return i;
}

static int ce_ahash_init(struct ahash_request *req)
{
	struct ce_hash_ctx *hash_ctx = ahash_request_tfmctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	const char *cra_name = crypto_ahash_alg_name(tfm);

	hash_ctx->alg = get_type_by_name(cra_name);
	memcpy(hash_ctx->hash, hash_init_state_table[hash_ctx->alg],
	       ce_hash_hash_len(hash_ctx->alg));
	hash_ctx->count = 0;
	hash_ctx->err = 0;
	return hash_ctx->err;
}
static int ce_ahash_final(struct ahash_request *req)
{
	struct ce_hash_ctx *hash_ctx = ahash_request_tfmctx(req);
	unsigned int blocklen = ce_hash_block_len(hash_ctx->alg);
	unsigned int hashlen = ce_hash_hash_len(hash_ctx->alg);
	const int bit_offset = blocklen - sizeof(__be64);
	__be64 *bits = (__be64 *)(hash_ctx->buf + bit_offset);
	unsigned int partial = hash_ctx->count % blocklen;

	ce_assert(hash_ctx->err == 0);

	hash_ctx->buf[partial++] = 0x80;
	if (partial > bit_offset) {
		memset(hash_ctx->buf + partial, 0x0, blocklen - partial);
		partial = 0;
		ctx_transform(hash_ctx);
	}

	memset(hash_ctx->buf + partial, 0x0, bit_offset - partial);
	*bits = cpu_to_be64(hash_ctx->count << 3);
	ctx_transform(hash_ctx);
	ce_assert(req->result);
	memcpy(req->result, hash_ctx->hash, hashlen);
	return hash_ctx->err;
}

static int init(struct ahash_request *req)
{
	return ce_ahash_init(req);
}

static int update(struct ahash_request *req)
{
	return ce_enqueue_request(&req->base);
}

static int final(struct ahash_request *req)
{
	return ce_ahash_final(req);
}

static int finup(struct ahash_request *req)
{
	struct ce_hash_ctx *hash_ctx = ahash_request_tfmctx(req);

	hash_ctx->final = true;
	return ce_enqueue_request(&req->base);
}

static int digest(struct ahash_request *req)
{
	ce_ahash_init(req);
	return finup(req);
}

static int export(struct ahash_request *req, void *out)
{
	struct ce_hash_ctx *hash_ctx = ahash_request_tfmctx(req);
	unsigned int blocklen = ce_hash_block_len(hash_ctx->alg);

	if (hash_ctx->alg == CE_SHA1) {
		struct sha1_state *state = out;

		memcpy(state->state, hash_ctx->hash, sizeof(state->state));
		state->count = hash_ctx->count;
		memcpy(state->buffer, hash_ctx->buf, state->count % blocklen);
	} else if (hash_ctx->alg == CE_SHA256) {
		struct sha256_state *state = out;

		memcpy(state->state, hash_ctx->hash, sizeof(state->state));
		state->count = hash_ctx->count;
		memcpy(state->buf, hash_ctx->buf, state->count % blocklen);
	} else {
		struct sm3_state *state = out;

		memcpy(state->state, hash_ctx->hash, sizeof(state->state));
		state->count = hash_ctx->count;
		memcpy(state->buffer, hash_ctx->buf, state->count % blocklen);

	}

	return 0;
}

static int import(struct ahash_request *req, const void *in)
{
	struct ce_hash_ctx *hash_ctx = ahash_request_tfmctx(req);
	unsigned int blocklen = ce_hash_block_len(hash_ctx->alg);

	if (hash_ctx->alg == CE_SHA1) {
		const struct sha1_state *state = in;

		memcpy(hash_ctx->hash, state->state, sizeof(state->state));
		hash_ctx->count = state->count;
		memcpy(hash_ctx->buf, state->buffer, state->count % blocklen);
	} else if (hash_ctx->alg == CE_SHA256) {
		const struct sha256_state *state = in;

		memcpy(hash_ctx->hash, state->state, sizeof(state->state));
		hash_ctx->count = state->count;
		memcpy(hash_ctx->buf, state->buf, state->count % blocklen);
	} else {
		const struct sm3_state *state = in;

		memcpy(hash_ctx->hash, state->state, sizeof(state->state));
		hash_ctx->count = state->count;
		memcpy(hash_ctx->buf, state->buffer, state->count % blocklen);
	}
	return 0;
}

#undef HASH
#define HASH(hash)							\
{									\
	.alg = {							\
		.init = init,						\
		.update = update,					\
		.final = final,						\
		.finup = finup,						\
		.digest = digest,					\
		.export = export,					\
		.import = import,					\
		.setkey = NULL,						\
		.halg = {						\
			.digestsize = hash##_DIGEST_SIZE,		\
			.statesize = sizeof(struct sha256_state),	\
			.base = {					\
				.cra_name = #hash,			\
				.cra_driver_name = #hash"-bitmain-ce",	\
				.cra_priority = CRA_PRIORITY,		\
				.cra_flags = CRYPTO_ALG_ASYNC |		\
					CRYPTO_ALG_TYPE_AHASH,		\
				.cra_blocksize = hash##_BLOCK_SIZE,	\
				.cra_ctxsize = sizeof(struct ce_hash_ctx),	\
				.cra_module = THIS_MODULE,		\
				.cra_init = NULL,			\
			}						\
		}							\
	},								\
},

static struct ahash_alg_container algs[] = {
	#include <alg.def>
};

#undef HASH

int unregister_all_hash(int map[])
{
	int i;
	struct ahash_alg_container *ctn;
	struct ahash_alg *alg;

	for (i = 0; i < ARRAY_SIZE(algs); ++i) {
		if (!map[i])
			continue;

		ctn = algs + i;
		alg = &ctn->alg;

		if (ctn->registered) {
			ce_info("unregistering algrithm %s : %s",
				alg->halg.base.cra_name,
				alg->halg.base.cra_driver_name);
			ce_assert(crypto_unregister_ahash(alg) == 0);
			ctn->registered = 0;
		}
	}
	return 0;
}

static void alg_2nd_init(struct crypto_alg *alg)
{
	strnlower(alg->cra_name, alg->cra_name, sizeof(alg->cra_name));
	strnlower(alg->cra_driver_name, alg->cra_driver_name,
		  sizeof(alg->cra_driver_name));
}

int register_all_hash(int map[])
{
	int i;
	struct ahash_alg_container *ctn;
	struct ahash_alg *alg;

	for (i = 0; i < ARRAY_SIZE(algs); ++i) {
		if (!map[i])
			continue;

		ctn = algs + i;
		alg = &ctn->alg;

		if (!ctn->registered) {
			alg_2nd_init(&(alg->halg.base));
			ce_info("registering algrithm %s : %s",
				alg->halg.base.cra_name,
				alg->halg.base.cra_driver_name);
			ce_assert(crypto_register_ahash(alg) == 0);
		}
	}
	return 0;
}

