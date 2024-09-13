/*
 * Testcase for crypto engine
 */
#include <ce.h>
#include <ce_port.h>
#include <ce_dev.h>
#include <ce_types.h>
#include <ce_inst.h>
#include <ce_cipher_walk.h>

struct ce_cipher_ctx {
	int alg, mode, keylen, enc, use_secure_key;
	u8 iv[MAX_BLOCKLEN];
	u8 key[MAX_KEYLEN];
	int err;
};


static void __maybe_unused dump_sg(struct scatterlist *sg)
{
	struct scatterlist *p;
	int i;

	for (p = sg, i = 0; p; p = sg_next(p), ++i) {
		ce_info("%d sg %p, virt %p, phys %p, len 0x%x\n",
			i, p, sg_virt(p), (void *)sg_phys(p), p->length);
		if (sg_virt(p))
			dumpp(sg_virt(p), p->length);
	}
}

static void __maybe_unused ctx_info(struct ce_cipher_ctx *this)
{
	ce_info("type %s\n", "cipher");
	ce_info("err %d\n", this->err);
	ce_info("alg %d\n", this->alg);
	ce_info("mode %d\n", this->mode);
	ce_info("keylen %d\n", this->keylen);
	ce_info("enc %d\n", this->enc);
	ce_info("iv\n");
	dumpp(this->iv, MAX_BLOCKLEN);
	ce_info("key\n");
	if (this->use_secure_key)
		ce_info("use secure key\n");
	else
		dumpp(this->key, MAX_KEYLEN);
}

static void __maybe_unused cipher_req_info(struct ablkcipher_request *req)
{
	ce_info("nbytes %u\n", req->nbytes);
	ce_info("info %p\n", req->info);
	ce_info("src scatter list %p\n", req->src);
	dump_sg(req->src);
	ce_info("dst scatter list %p\n", req->dst);
	dump_sg(req->dst);
}

static void ctx_set_key(struct ce_cipher_ctx *this, const void *key, int keylen)
{
	this->keylen = keylen;
	memcpy(this->key, key, keylen);
}

static void ctx_set_iv(struct ce_cipher_ctx *this, void *iv)
{
	int ivlen;

	ce_assert(this->alg >= 0 && this->alg < CE_ALG_MAX);

	if (this->mode == CE_ECB)
		return;

	switch (this->alg) {
	case CE_DES:
	case CE_DES_EDE3:
		ivlen = DES_BLOCKLEN;
		break;
	case CE_AES:
		ivlen = AES_BLOCKLEN;
		break;
	case CE_SM4:
		ivlen = SM4_BLOCKLEN;
		break;
	default:
		ce_err("unknown cipher %d\n", this->alg);
		ivlen = 0;
		break;
	}
	memcpy(this->iv, iv, ivlen);
}

enum {
	CE_AES128,
	CE_AES192,
	CE_AES256,
	CE_AESKEYLEN_MAX,
};

static const int cipher_index_des[CE_MODE_MAX] = {
	CE_DES_ECB, CE_DES_CBC, CE_DES_CTR,
};

static const int cipher_index_des_ede3[CE_MODE_MAX] = {
	CE_DES_EDE3_ECB, CE_DES_EDE3_CBC, CE_DES_EDE3_CTR,
};

static const int cipher_index_sm4[CE_MODE_MAX] = {
	CE_SM4_ECB, CE_SM4_CBC, CE_SM4_CTR, CE_SM4_OFB,
};

static const int cipher_index_aes[CE_AESKEYLEN_MAX][CE_MODE_MAX] = {
	{ CE_AES_128_ECB, CE_AES_128_CBC, CE_AES_128_CTR, },
	{ CE_AES_192_ECB, CE_AES_192_CBC, CE_AES_192_CTR, },
	{ CE_AES_256_ECB, CE_AES_256_CBC, CE_AES_256_CTR, },
};

static int get_cipher_index(unsigned int cipher, unsigned int mode,
			    unsigned int keylen)
{
	switch (cipher) {
	case CE_DES:
		return cipher_index_des[mode];
	case CE_DES_EDE3:
		return cipher_index_des_ede3[mode];
	case CE_SM4:
		return cipher_index_sm4[mode];
	case CE_AES:
		switch (keylen) {
		case 128 / 8:
			keylen = CE_AES128;
			break;
		case 192 / 8:
			keylen = CE_AES192;
			break;
		case 256 / 8:
			keylen = CE_AES256;
			break;
		default:
			return -1;
		}
		return cipher_index_aes[keylen][mode];
	default:
		return -1;
	}
}

static void cipher_unmap_sg(struct device *dev,
			    struct scatterlist *src, struct scatterlist *dst)
{
	if (src == dst) {
		dma_unmap_sg(dev, src, sg_nents(src),
			     DMA_BIDIRECTIONAL);
	} else {
		dma_unmap_sg(dev, src, sg_nents(src), DMA_TO_DEVICE);
		dma_unmap_sg(dev, dst, sg_nents(dst), DMA_FROM_DEVICE);
	}
}

static int cipher_map_sg(struct device *dev,
			 struct scatterlist *src, struct scatterlist *dst)
{
	if (src == dst) {
		if (dma_map_sg(dev, src, sg_nents(src),
			       DMA_BIDIRECTIONAL) == 0) {
			ce_err("map scatter list(in place) failed\n");
			goto err_nop;
		}
	} else {
		if (dma_map_sg(dev, src, sg_nents(src),
			       DMA_TO_DEVICE) == 0) {
			ce_err("map source scatter list failed\n");
			goto err_nop;
		}
		if (dma_map_sg(dev, dst, sg_nents(dst),
			       DMA_FROM_DEVICE) == 0) {
			ce_err("map destination scatter list failed\n");
			goto err_unmap_src;
		}
	}
	return 0;
err_unmap_src:
	dma_unmap_sg(dev, src, sg_nents(src), DMA_TO_DEVICE);
err_nop:
	return -ENOMEM;
}

static unsigned long dmacpy(dma_addr_t dst, dma_addr_t src,
			    unsigned long len, void *priv)
{
	if (ce_dmacpy(priv, (void *)dst, (void *)src, len)) {
		ce_err("dma copy failed\n");
		return 0;
	}
	return len;
}

void ce_cipher_handle_request(struct ce_inst *inst,
			      struct crypto_async_request *req)
{
	struct ablkcipher_request *cipher_req;
	struct crypto_ablkcipher *tfm;
	struct ce_cipher_walk walk;
	struct ce_cipher_walk_buffer dma_buf;
	struct ce_cipher cfg;
	struct ce_cipher_ctx *cipher_ctx;
	int blocklen, ivsize;
	struct ce_ctx ctx = {0};

	cipher_req = ablkcipher_request_cast(req);
	tfm = crypto_ablkcipher_reqtfm(cipher_req);
	cipher_ctx = crypto_ablkcipher_ctx(tfm);

	if (cipher_req->nbytes == 0) {
		cipher_ctx->err = 0;
		goto err;
	}

#ifdef CE_DBG
	ctx_info(cipher_ctx);
	cipher_req_info(cipher_req);
#endif

	cipher_ctx->err = cipher_map_sg(&inst->dev->dev,
				 cipher_req->src, cipher_req->dst);
	if (cipher_ctx->err)
		goto err;

	ctx.reg = inst->reg;
	memcpy(ctx.is_support, inst->alg_attribute->cipher_map,
		sizeof(ctx.is_support));
	ctx.write_burst_length = inst->alg_attribute->write_burst_length;
	ctx.read_burst_length = inst->alg_attribute->read_burst_length;

	cfg.alg = get_cipher_index(cipher_ctx->alg, cipher_ctx->mode,
				    cipher_ctx->keylen);
	blocklen = ce_cipher_block_len(cfg.alg);
	ivsize = crypto_ablkcipher_ivsize(tfm);
	cfg.enc = cipher_ctx->enc;
	cfg.iv = cipher_ctx->iv;
	cfg.key = cipher_ctx->use_secure_key ? NULL : cipher_ctx->key;
	cfg.iv_out = cfg.iv; //save iv in place

	ce_cipher_walk_init(&walk, inst->tmp_buf_cpu, inst->tmp_buf_dma,
		  dmacpy, inst->reg, blocklen, cipher_req->nbytes,
		  cipher_req->src, cipher_req->dst);

	while (ce_cipher_walk_next(&walk, &dma_buf)) {
		cfg.src = (void *)dma_buf.src;
		cfg.dst = (void *)dma_buf.dst;
		cfg.len = dma_buf.len;
		cipher_ctx->err = ce_cipher_do(&ctx, &cfg);
		if (cipher_ctx->err) {
			ce_err("hardware configuration failed\n");
			cipher_ctx->err = -EINVAL;
			goto err_unmap;
		}
		ce_inst_wait(inst);
		if (cipher_ctx->mode != CE_ECB)
			ce_save_iv(&ctx, &cfg);
		if (ce_cipher_walk_done(&walk)) {
			ce_err("invalid argument\n");
			cipher_ctx->err = -EINVAL;
			goto err_unmap;
		}
	}

	cipher_ctx->err = 0;

err_unmap:
	cipher_unmap_sg(&inst->dev->dev, cipher_req->src, cipher_req->dst);
	/* save iv to cipher request */
	if (cipher_ctx->err == 0 && cipher_req->info && ivsize)
		memcpy(cipher_req->info, cfg.iv_out, ivsize);

err:
	req->complete(req, cipher_ctx->err);
}

/*****************************************************************************/

struct crypto_alg_container {
	int cipher, mode, use_secure_key, registered;
	struct crypto_alg alg;
};

static int cipher_op(struct ablkcipher_request *req, int enc)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct ce_cipher_ctx *cipher_ctx = crypto_ablkcipher_ctx(tfm);

	cipher_ctx->enc = enc;
	ctx_set_iv(cipher_ctx, req->info);
	return ce_enqueue_request(&req->base);
}

static int encrypt(struct ablkcipher_request *req)
{
	return cipher_op(req, true);
}

static int decrypt(struct ablkcipher_request *req)
{
	return cipher_op(req, false);
}

static int setkey(struct crypto_ablkcipher *tfm,
		  const u8 *key, unsigned int keylen)
{
	struct ce_cipher_ctx *cipher_ctx = crypto_ablkcipher_ctx(tfm);

	ctx_set_key(cipher_ctx, key, keylen);
	return 0;
}

static int init(struct crypto_tfm *tfm)
{
	struct ce_cipher_ctx *cipher_ctx = crypto_tfm_ctx(tfm);
	struct crypto_alg *alg = tfm->__crt_alg;
	struct crypto_alg_container *ctn = container_of(alg,
					struct crypto_alg_container, alg);

	cipher_ctx->alg = ctn->cipher;
	cipher_ctx->mode = ctn->mode;
	cipher_ctx->use_secure_key = ctn->use_secure_key;
	return 0;
}

#undef ALG
#define ALG(xmode, xcipher)						\
{									\
	.cipher = CE_##xcipher,						\
	.mode = CE_##xmode,						\
	.use_secure_key = false,					\
	.registered = 0,						\
	.alg = {							\
		.cra_name = #xmode"("#xcipher")",			\
		.cra_driver_name = #xmode"("#xcipher")-bitmain-ce",	\
		.cra_priority = CRA_PRIORITY,				\
		.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,		\
		.cra_blocksize = xcipher##_BLOCKLEN,			\
		.cra_ctxsize = sizeof(struct ce_cipher_ctx),			\
		.cra_type = &crypto_ablkcipher_type,			\
		.cra_init = init,					\
		.cra_module = THIS_MODULE,				\
		.cra_u = {						\
			.ablkcipher = {					\
				.min_keysize =				\
					xcipher##_MIN_KEYLEN,		\
				.max_keysize =				\
					xcipher##_MAX_KEYLEN,		\
				.setkey = setkey,			\
				.encrypt = encrypt,			\
				.decrypt = decrypt,			\
			},						\
		},							\
	},								\
},									\
{									\
	.cipher = CE_##xcipher,						\
	.mode = CE_##xmode,						\
	.use_secure_key = true,						\
	.registered = 0,						\
	.alg = {							\
		.cra_name = #xmode"("#xcipher"-secure-key)",		\
		.cra_driver_name =					\
			#xmode"("#xcipher"-secure-key)-bitmain-ce",	\
		.cra_priority = CRA_PRIORITY,				\
		.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,		\
		.cra_blocksize = xcipher##_BLOCKLEN,			\
		.cra_ctxsize = sizeof(struct ce_cipher_ctx),			\
		.cra_type = &crypto_ablkcipher_type,			\
		.cra_init = init,					\
		.cra_module = THIS_MODULE,				\
		.cra_u = {						\
			.ablkcipher = {					\
				.min_keysize =				\
					xcipher##_MIN_KEYLEN,		\
				.max_keysize =				\
					xcipher##_MAX_KEYLEN,		\
				.setkey = setkey,			\
				.encrypt = encrypt,			\
				.decrypt = decrypt,			\
			},						\
		},							\
	},								\
},

static struct crypto_alg_container algs[] = {
	#include <alg.def>
};
#undef ALG

static int is_support_alg(int index, int map[])
{
	index = index / 2;

	if (index < 6)
		return map[index];
	else if (index > 8)
		return map[index + 6];
	else
		return (map[index] && map[index + 3] && map[index + 6]);
}



int unregister_all_alg(int map[])
{
	int i;
	struct crypto_alg_container *ctn;
	struct crypto_alg *alg;

	for (i = 0; i < ARRAY_SIZE(algs); ++i) {
		if (i % 2 == 0) {
			if (!is_support_alg(i, map)) {
				i += 2;
				continue;
			}
		}

		ctn = algs + i;
		alg = &ctn->alg;

		if (ctn->registered) {
			ce_info("unregistering algrithm %s : %s",
				alg->cra_name, alg->cra_driver_name);
			ce_assert(crypto_unregister_alg(alg) == 0);
			ctn->registered = 0;
		}
	}
	return 0;
}

static void alg_2nd_init(struct crypto_alg *alg)
{
	struct crypto_alg_container *ctn = container_of(alg,
					struct crypto_alg_container,
					alg);

	strnlower(alg->cra_name, alg->cra_name, sizeof(alg->cra_name));
	strnlower(alg->cra_driver_name, alg->cra_driver_name,
		  sizeof(alg->cra_driver_name));
	if (ctn->mode != CE_ECB)
		alg->cra_u.ablkcipher.ivsize = alg->cra_blocksize;
	if (ctn->mode == CE_CTR || ctn->mode == CE_OFB)
		alg->cra_blocksize = 1;
}

int register_all_alg(int map[])
{
	int i;
	struct crypto_alg_container *ctn;
	struct crypto_alg *alg;

	for (i = 0; i < ARRAY_SIZE(algs); ++i) {
		if (i % 2 == 0) {
			if (!is_support_alg(i, map)) {
				i += 2;
				continue;
			}
		}

		ctn = algs + i;
		alg = &ctn->alg;

		if (!ctn->registered) {
			alg_2nd_init(alg);
			ce_info("registering algrithm %s : %s",
				alg->cra_name, alg->cra_driver_name);
			ce_assert(crypto_register_alg(alg) == 0);
			ctn->registered = 1;
		}
	}
	return 0;
}
