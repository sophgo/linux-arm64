#include <ce_port.h>
#include <ce.h>
#include <ce_inst.h>

#include <linux/rwlock.h>
#include <linux/rwsem.h>
#include <linux/mutex.h>
#include <../../soc/bitmain/vpp/vpp_platform.h>

#define CE_BASE_TFM_MAX		24

struct ce_base_device;

struct ce_base_tfm {
	int err, used;
	struct ce_base_device *dev;
	struct ce_base op;
	struct completion comp;
	struct crypto_async_request creq;
};

struct ce_base_device {
	struct miscdevice misc;
	spinlock_t lock;
	struct ce_base_tfm tfm[CE_BASE_TFM_MAX];
};

void ce_base_handle_request(struct ce_inst *inst,
			    struct crypto_async_request *req)
{
	struct ce_base_tfm *tfm;
	struct ce_base *op;
	int err;
	struct ce_ctx ctx = {0};

	tfm = req->data;
	op = &tfm->op;
	ctx.write_burst_length = inst->alg_attribute->write_burst_length;
	ctx.read_burst_length = inst->alg_attribute->read_burst_length;
	ctx.reg = inst->reg;

	err = ce_base_do(&ctx, op);
	if (err) {
		ce_err("hardware configuration failed\n");
		err = -EINVAL;
		goto err0;
	}
	ce_inst_wait(inst);
err0:
	req->complete(req, err);
}

static void ce_base_complete(struct crypto_async_request *creq, int err)
{
	struct ce_base_tfm *tfm;

	tfm = creq->data;

	tfm->err = err;
	if (err != -EINPROGRESS)
		complete(&tfm->comp);
}

static struct crypto_async_request *ce_base_alloc_request(
	struct ce_base_device *ce_base_dev)
{
	struct crypto_async_request *creq = NULL;
	struct ce_base_tfm *tfm = NULL;
	int i;
	unsigned long flag;

	spin_lock_irqsave(&ce_base_dev->lock, flag);

	for (i = 0; i < ARRAY_SIZE(ce_base_dev->tfm); ++i) {
		if (!ce_base_dev->tfm[i].used) {
			tfm = &(ce_base_dev->tfm[i]);
			break;
		}
	}

	if (tfm) {
		tfm->err = -EINPROGRESS;
		tfm->used = true;
		tfm->dev = ce_base_dev;

		init_completion(&tfm->comp);

		creq = &tfm->creq;
		memset(creq, 0x00, sizeof(*creq));
		creq->data = tfm;
		creq->flags = CRYPTO_TFM_REQ_MAY_BACKLOG;
		creq->complete = ce_base_complete;
	}

	spin_unlock_irqrestore(&ce_base_dev->lock, flag);

	return creq;
}

static void ce_base_free_request(struct crypto_async_request *creq)
{
	struct ce_base_tfm *tfm;
	struct ce_base_device *ce_base_dev;
	unsigned long flag;

	if (!creq)
		return;

	tfm = creq->data;

	if (!tfm)
		return;

	ce_base_dev = tfm->dev;

	spin_lock_irqsave(&ce_base_dev->lock, flag);
	tfm->used = false;
	spin_unlock_irqrestore(&ce_base_dev->lock, flag);
}

static int ce_base_ioc_func_op_phy(struct ce_base_device *ce_base_dev,
				   unsigned long arg)
{
	struct miscdevice *misc;
	struct device *dev;
	struct ce_inst *inst;
	struct crypto_async_request *creq;
	struct ce_base_tfm *ce_base_tfm;
	struct ce_base_ioc_op_phy ce_base_ioc_op_phy;
	struct ce_base *ce_base_op;
	int err;

	if (copy_from_user(&ce_base_ioc_op_phy,
		       (void *)arg,
		       sizeof(ce_base_ioc_op_phy))) {
		ce_err("copy from user failed\n");
		return -EFAULT;
	}

	if (ce_base_ioc_op_phy.alg >= CE_BASE_IOC_ALG_MAX) {
		ce_err("invalid baseN algorithm\n");
		return -EINVAL;
	}

	misc = &ce_base_dev->misc;
	dev = misc->parent;
	inst = dev_get_drvdata(dev);

	creq = ce_base_alloc_request(ce_base_dev);
	if (creq == NULL) {
		ce_err("cannot allocate request\n");
		return -ENOMEM;
	}

	ce_base_tfm = (struct ce_base_tfm *)creq->data;
	ce_base_op = &ce_base_tfm->op;

	ce_base_op->alg = ce_base_ioc_op_phy.alg;
	ce_base_op->enc = ce_base_ioc_op_phy.enc;
	ce_base_op->src = (void *)ce_base_ioc_op_phy.src;
	ce_base_op->dst = (void *)ce_base_ioc_op_phy.dst;
	ce_base_op->len = ce_base_ioc_op_phy.len;
	ce_base_op->dstlen = ce_base_ioc_op_phy.dstlen;

	down_write(&my_rwlock);

	err = ce_inst_enqueue_request(inst, creq);
	if (err != -EINPROGRESS) {
		/* success insert request to queue */
		ce_err("cannot insert request to queue\n");
		goto err0;
	}

	/* wait this op done */
	wait_for_completion(&ce_base_tfm->comp);
	/* we never use this completion again, just init it every transaction */
	/* reinit_completion(&ce_base_tfm->comp); */

	up_write(&my_rwlock);

	err = ce_base_tfm->err;
err0:
	ce_base_free_request(creq);

	return err;
}

static long ce_base_ioctl(struct file *file,
			  unsigned int request,
			  unsigned long arg)
{
	struct ce_base_device *ce_base_dev;
	int err;

	ce_base_dev = file->private_data;

	switch (request) {
	case CE_BASE_IOC_OP_PHY:
		err = ce_base_ioc_func_op_phy(ce_base_dev, arg);
		if (err)
			goto err0;
		break;
	default:
		ce_err("invalid request\n");
		err = -EINVAL;
		goto err0;
	}
	err = 0;
err0:
	return err;
}

static const struct file_operations ce_base_op = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ce_base_ioctl,
};

int register_all_base(struct device *dev)
{
	struct ce_inst *inst;
	struct miscdevice *misc;
	struct ce_base_device *ce_base_dev;
	int err;

	ce_dbg("registering base algorithm\n");
	inst = dev_get_drvdata(dev);
	/* device data should be set before */
	ce_assert(inst);
	ce_base_dev = ce_inst_alloc(inst, sizeof(struct ce_base_device));
	if (ce_base_dev == NULL) {
		ce_err("cannot allocate memory from instance\n");
		return -ENOMEM;
	}

	misc = &ce_base_dev->misc;

	memset(misc, 0x00, sizeof(*misc));

	spin_lock_init(&ce_base_dev->lock);

	misc->minor = MISC_DYNAMIC_MINOR;
	misc->name = "bce-base";
	misc->fops = &ce_base_op;
	misc->parent = dev;
	misc->mode = 0666;
	err = misc_register(misc);

	if (err) {
		ce_err("cannot register misc device for base algorithm\n");
		return err;
	}

	return 0;
}

int unregister_all_base(struct miscdevice *misc)
{
	if (misc) {
		misc_deregister(misc);
		return 0;
	}
	ce_err("invalid misc device pointer\n");
	return -EINVAL;
}

