#include <ce_inst.h>
#include <ce_hash.h>
#include <ce_cipher.h>
#include <ce_base.h>

#define CE_REQUEST_QUEUE_SIZE	(50)

void ce_inst_initpool(struct ce_inst *this)
{
	/* instance is in top of pool */
	unsigned long allocator_start_offset = roundup(sizeof(*this),
						       CE_INST_ALLOC_ALIGN);
	/*
	 * it shouldnot go here, PAGE_SIZE is large enough to handle one
	 * instance.
	 */
	ce_assert(allocator_start_offset < PAGE_SIZE);
	this->allocator_offset = allocator_start_offset;
}

static void qtask(struct work_struct *work)
{
	struct ce_inst *this = container_of(work, struct ce_inst, qwork);
	struct crypto_async_request *req, *backlog;
	unsigned long flag;
	int err;

	spin_lock_irqsave(&this->lock, flag);
	backlog = crypto_get_backlog(&this->queue);
	req = crypto_dequeue_request(&this->queue);
	if (req == NULL) {
		this->busy = false; /* all requests have been done */
		spin_unlock_irqrestore(&this->lock, flag);
		return;
	}
	spin_unlock_irqrestore(&this->lock, flag);
	if (backlog)
		backlog->complete(backlog, -EINPROGRESS);

	if (this->clk) {
		ce_dbg("enable spacc clock\n");
		err = clk_prepare_enable(this->clk);
		if (err) {
			ce_err("prepare enable clock failed\n");
			req->complete(req, err);
			goto schedule;
		}
		ce_dbg("get spacc clock rate\n");
		this->rate = clk_get_rate(this->clk);
		if (this->rate == 0) {
			ce_err("clock frequency is 0\n");
			err = -ENODEV;
			req->complete(req, err);
			goto disclk;
		}
		ce_dbg("spacc clock rate is %lu\n", this->rate);
	}

	if (this->rst) {
		ce_dbg("deassert spacc reset\n");
		err = reset_control_deassert(this->rst);
		if (err) {
			ce_err("deassert reset failed\n");
			req->complete(req, err);
			goto disclk;
		}
	}
	enable_irq(this->irq);

	if (ce_base_is_base_request(req)) {
		ce_base_handle_request(this, req);
	} else {
		/* get cra_type */
		switch (crypto_async_request_type(req)) {
		case CRYPTO_ALG_TYPE_ABLKCIPHER:
			ce_cipher_handle_request(this, req);
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			ce_hash_handle_request(this, req);
			break;
		default:
			ce_err("unsupport crypto graphic algorithm\n");
			ce_assert(0);
		}
	}
	disable_irq(this->irq);
	reset_control_assert(this->rst);
disclk:
	clk_disable_unprepare(this->clk);
schedule:
	schedule_work(&this->qwork);
}

static irqreturn_t __maybe_unused ce_irq(int irq, void *dev_id)
{
	struct ce_inst *this = dev_id;

	ce_int_clear(this->reg);
	complete(&this->comp);
	return IRQ_HANDLED;
}

void ce_inst_wait(struct ce_inst *this)
{
	wait_for_completion(&this->comp);
	reinit_completion(&this->comp);
}

/* allocate and init a instance */
int ce_inst_new(struct ce_inst **inst, struct platform_device *dev,
		struct alg_attribute *alg_attribute)
{
	struct ce_inst *this;
	struct resource *res;
	int err;

	this = devm_kzalloc(&dev->dev, PAGE_SIZE, GFP_KERNEL);
	if (this == NULL)
		return -ENOMEM;

	ce_inst_initpool(this);

	/* get resources delcared in DT */
	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	this->reg = devm_ioremap_resource(&dev->dev, res);
	if (IS_ERR(this->reg)) {
		ce_err("map register failed\n");
		return -ENOMEM;
	}

	this->irq = platform_get_irq(dev, 0);
	if (this->irq < 0) {
		ce_err("no interrupt resource\n");
		return -EINVAL;
	}
	if (devm_request_irq(&dev->dev, this->irq, ce_irq, 0,
			     "bitmain crypto engine", this)) {
		ce_err("request irq failed\n");
		return -EBUSY;
	}
	/* disable system irq on init
	 * enable it before using
	 */
	disable_irq(this->irq);

	this->clk = devm_clk_get(&dev->dev, NULL);
	if (IS_ERR(this->clk)) {
		ce_err("cannot get clock\n");
		return PTR_ERR(this->clk);
	}

	this->rst = devm_reset_control_get_optional_shared(&dev->dev, NULL);
	if (IS_ERR(this->rst)) {
		ce_err("cannot get reset control\n");
		return PTR_ERR(this->rst);
	}
	/* crypto engine has no limitation on dma address space */
	err = dma_set_mask(&dev->dev, DMA_BIT_MASK(64));
	if (err) {
		ce_err("cannot set dma mask\n");
		return err;
	}
	err = dma_set_coherent_mask(&dev->dev, DMA_BIT_MASK(64));
	if (err) {
		ce_err("cannot set dma coherent mask\n");
		return err;
	}
	dev->dev.dma_coherent = false;
	/* allocate temporary dma buffer */
	this->tmp_buf_cpu = dma_alloc_coherent(&dev->dev, PAGE_SIZE,
					       &this->tmp_buf_dma, GFP_KERNEL);
	if (this->tmp_buf_cpu == NULL) {
		ce_err("canot allocate cache coherent temporary dma buffer\n");
		return PTR_ERR(this->tmp_buf_cpu);
	}

	this->dev = dev;
	this->alg_attribute = alg_attribute;

	INIT_WORK(&this->qwork, qtask);
	crypto_init_queue(&this->queue, CE_REQUEST_QUEUE_SIZE);
	spin_lock_init(&this->lock);
	init_completion(&this->comp);
	this->busy = false;
	platform_set_drvdata(dev, this);
	*inst = this;
	return 0;
}

int ce_inst_enqueue_request(struct ce_inst *this,
			    struct crypto_async_request *req)
{
	unsigned long flag;
	int err;

	spin_lock_irqsave(&this->lock, flag);
	err = crypto_enqueue_request(&this->queue, req);
	if (this->busy) {
		spin_unlock_irqrestore(&this->lock, flag);
		return err;
	}
	this->busy = true;
	spin_unlock_irqrestore(&this->lock, flag);
	schedule_work(&this->qwork);

	return err;
}

void *ce_inst_alloc(struct ce_inst *this, unsigned long size)
{
	unsigned long alloc_size;
	void *alloc_base;

	alloc_size = roundup(size, CE_INST_ALLOC_ALIGN);
	if (alloc_size + this->allocator_offset > PAGE_SIZE) {
		ce_err("cannot get memory from instance allocator\n");
		return NULL;
	}

	alloc_base = ((u8 *)this) + this->allocator_offset;
	this->allocator_offset += alloc_size;

	return alloc_base;
}
