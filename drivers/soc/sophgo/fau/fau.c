#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/miscdevice.h>

/********************* low level driver start *********************/

//#define DEBUG_FAU
#define FAU_INTR_STATUS          0x00
#define FAU_INTR_MASK            0x04
#define FAU_INTR_RAW             0x08
#define FAU_INTR_CLR             0x0C

#define FAU_MERKLE_EN            0x20
#define FAU_MERKLE_RST           0x24
#define FAU_MERKLE_L0_ADDR_L     0x28
#define FAU_MERKLE_L0_ADDR_H     0x2C
#define FAU_MERKLE_L1_ADDR_L     0x30
#define FAU_MERKLE_L1_ADDR_H     0x34
#define FAU_MERKLE_L2_ADDR_L     0x38
#define FAU_MERKLE_L2_ADDR_H     0x3C
#define FAU_MERKLE_L3_ADDR_L     0x40
#define FAU_MERKLE_L3_ADDR_H     0x44
#define FAU_MERKLE_L4_ADDR_L     0x48
#define FAU_MERKLE_L4_ADDR_H     0x4C
#define FAU_MERKLE_CHILD_TREE    0x50
#define FAU_MERKLE_HOR_LOOP_CNT  0x54

#define FAU_SDR_EN               0x80
#define FAU_SDR_RST              0x84
#define FAU_SDR_LAYER            0x88
#define FAU_SDR_NODE_ID          0x8C
#define FAU_SDR_BASE_ADDR_L      0x90
#define FAU_SDR_BASE_ADDR_H      0x94
#define FAU_SDR_EXP_ADDR_L       0x98
#define FAU_SDR_EXP_ADDR_H       0x9C
#define FAU_SDR_COORD_ADDR_L     0xA0
#define FAU_SDR_COORD_ADDR_H     0xA4
#define FAU_SDR_NODE_CNT         0xA8
#define FAU_SDR_REP_ID0          0xB0
#define FAU_SDR_REP_ID1          0xB4
#define FAU_SDR_REP_ID2          0xB8
#define FAU_SDR_REP_ID3          0xBC
#define FAU_SDR_REP_ID4          0xC0
#define FAU_SDR_REP_ID5          0xC4
#define FAU_SDR_REP_ID6          0xC8
#define FAU_SDR_REP_ID7          0xCC

#define FAU0_CFG_BASE		0X70B8000000
#define FAU1_CFG_BASE		0X70BC000000
#define FAU2_CFG_BASE		0X70C0000000
#define FAU_CORE_MAX		3

enum {
	FAU_EINVAL = 100,	/* invalid argument */
	FAU_EALG,		/* invalid algrithm */
	FAU_ENULL,		/* null pointer */
	FAU_ERR,
	FAU_TIMEOUT,
};

enum fau_type {
	MERKEL = 0,
	SDR,
};

struct fau_batch {
	enum fau_type fau_type;
	u8 index;
	union {
		struct fau_merkel {
			void *addr;
			u64 sector_size;
		} fau_merkel_t;
		struct fau_sdr {
			void *base;
			void *exp;
			void *coord;
			u8 repid[32];
			u8 layerid;
			u32 nodeid;
			u32 loops;
		} fau_sdr_t;
	} u;
};

struct fau_dev {
	/* get from probe */
	struct miscdevice miscdev;
	struct device *dev;

	struct {
		void *reg;
		int irq;

		/* kernel resources */
		spinlock_t lock;
		struct mutex fau_mutex;
		struct completion comp;

		/* clock & reset framework */
		struct clk *clk;
		unsigned long rate;
		struct reset_control *rst;

		/* cache coherent temporary buffer */
		/* for scatterlist node which not block length aligned */
		dma_addr_t tmp_buf_dma;
		void *tmp_buf_cpu;
	} fau[FAU_CORE_MAX];
};

#define FAU_OP_PHY	_IOWR('F', 0x01, unsigned long)
#define MAX_LEVEL_NUM 5
#define assert(A) WARN_ON(!(A))

static unsigned long fau_base[3] = {
	FAU0_CFG_BASE, FAU1_CFG_BASE, FAU2_CFG_BASE};

static inline uint32_t log2N(uint32_t N)
{
	uint32_t L = 0;

	assert(N > 0 && (N & (N - 1)) == 0x0);
	while (N > 1) {
		++L;
		N = N >> 1;
	}
	return L;
}

static void __maybe_unused dump(void *p, unsigned long l)
{
	unsigned long i;

	for (i = 0; i < l; ++i) {
		if (i % 16 == 0)
			pr_info("\n %04lx: ", i);
		pr_info("%02x ", ((unsigned char *)p)[i]);
	}
	pr_info("\n");
}

static inline uint32_t mmio_read_32(uint64_t addr)
{
	return readl((void *)addr);
}

static inline void mmio_write_32(uint64_t addr, u32 value)
{
	writel(value, (void *)addr);
}

static void fau_int_clear(void *dev)
{
	int status;

	status = mmio_read_32((uint64_t)dev + FAU_INTR_STATUS);
	mmio_write_32((uint64_t)dev + FAU_INTR_CLR, status);
}

static int start_fau_merkel(int i)
{
	mmio_write_32(fau_base[i] + FAU_INTR_MASK, 0x0);
	mmio_write_32(fau_base[i] + FAU_MERKLE_EN, 0x1);
	return 0;
}

static int start_fau_sdr(int i)
{
	mmio_write_32(fau_base[i] + FAU_INTR_MASK, 0x0);
	mmio_write_32(fau_base[i] + FAU_SDR_EN, 0x1);
	return 0;
}

static int merkle_p1_core(const uint64_t *addrs, uint8_t levels, uint32_t loops, int index)
{
	uint8_t i;

	assert(levels > 1 && levels <= MAX_LEVEL_NUM);
	assert(loops > 0);
	for (i = 0; i < levels; ++i) {
		assert((addrs[i] & 0x1f) == 0x0);
		pr_debug("level%d addr= 0x%llx\n", i, addrs[i]);
	}
	pr_debug("fau_base= %lx, levels = %d, loop=%d\n", fau_base[index], levels, loops);

	mmio_write_32(fau_base[index] + FAU_MERKLE_L0_ADDR_L, addrs[0]);
	mmio_write_32(fau_base[index] + FAU_MERKLE_L0_ADDR_H, addrs[0] >> 32);
	mmio_write_32(fau_base[index] + FAU_MERKLE_L1_ADDR_L, addrs[1]);
	mmio_write_32(fau_base[index] + FAU_MERKLE_L1_ADDR_H, addrs[1] >> 32);
	if (levels > 2) {
		mmio_write_32(fau_base[index] + FAU_MERKLE_L2_ADDR_L, addrs[2]);
		mmio_write_32(fau_base[index] + FAU_MERKLE_L2_ADDR_H, addrs[2] >> 32);
	}
	if (levels > 3) {
		mmio_write_32(fau_base[index] + FAU_MERKLE_L3_ADDR_L, addrs[3]);
		mmio_write_32(fau_base[index] + FAU_MERKLE_L3_ADDR_H, addrs[3] >> 32);
	}
	if (levels > 4) {
		mmio_write_32(fau_base[index] + FAU_MERKLE_L4_ADDR_L, addrs[4]);
		mmio_write_32(fau_base[index] + FAU_MERKLE_L4_ADDR_H, addrs[4] >> 32);
	}
	mmio_write_32(fau_base[index] + FAU_MERKLE_CHILD_TREE, levels << 8 | 1 << (levels - 1));
	mmio_write_32(fau_base[index] + FAU_MERKLE_HOR_LOOP_CNT, loops);
	return 0;
}

struct merkle_p1_data {
	uint32_t nodenum;
	uint8_t layer;
};

static int Merkle_p1_calc(uint64_t data_addr, uint64_t sector_size, int index, struct fau_dev *fau_dev)
{
	struct merkle_p1_data data;
	uint32_t i;
	uint64_t addrs[5];
	uint64_t M;

	data.nodenum = sector_size >> 5;
	data.layer = log2N(data.nodenum) + 1;
#ifdef DEBUG_FAU
	pr_info("data_addr = 0x%lx, sector_size=%ld, index=%d\n", data_addr, sector_size, index);
#endif
	assert(data.layer >= 2);
	M = data.nodenum >> 1;
	addrs[0] = data_addr;
	addrs[1] = data_addr + sector_size;

	for (i = 2; i < (data.layer < 5 ? data.layer : 5); ++i) {
		addrs[i] = addrs[i - 1] + M * 32UL;
		M = M >> 1;
	}
	merkle_p1_core(addrs, data.layer < 5 ? data.layer : 5, M, index);
	start_fau_merkel(index);
	wait_for_completion(&fau_dev->fau[index].comp);
	data.layer = data.layer - (data.layer < 5 ? data.layer : 5) + 1;
	while (data.layer > 1) {
		addrs[0] = addrs[4];
		for (i = 1; i < (data.layer < 5 ? data.layer : 5); ++i) {
			addrs[i] = addrs[i - 1] + M * 32UL;
			M = M >> 1;
		}
		merkle_p1_core(addrs, data.layer < 5 ? data.layer : 5, M, index);
		data.layer = data.layer - (data.layer < 5 ? data.layer : 5) + 1;
		start_fau_merkel(index);
		wait_for_completion(&fau_dev->fau[index].comp);
	}
	assert(M == 1);
	return 0;
}

static int SDR_core(uint64_t base, uint64_t exp, uint64_t coord, uint8_t *repid,
	     uint8_t layerid, uint32_t nodeid, uint32_t loops, int index)
{
	assert(layerid >= 1 && layerid <= 11);
	assert(nodeid > 0);
	assert(nodeid + loops <= (1 << 30));
	assert((base & 0x1f) == 0x0);
	assert((exp & 0x1f) == 0x0);

#ifdef DEBUG_FAU
	pr_info("base_addr = %lx, exp_addr=%lx, coord=%lx, layerid=%d, nodeid=%d, loops=%d\n",
	       (uint64_t)base, (uint64_t)exp, (uint64_t)coord, layerid, nodeid, loops);
	dump(repid, 32);
#endif

	mmio_write_32(fau_base[index] + FAU_SDR_LAYER, layerid);
	mmio_write_32(fau_base[index] + FAU_SDR_NODE_ID, nodeid);
	mmio_write_32(fau_base[index] + FAU_SDR_BASE_ADDR_L, base);
	mmio_write_32(fau_base[index] + FAU_SDR_BASE_ADDR_H, base >> 32);

	if (layerid >= 2) {
		mmio_write_32(fau_base[index] + FAU_SDR_EXP_ADDR_L, exp);
		mmio_write_32(fau_base[index] + FAU_SDR_EXP_ADDR_H, exp >> 32);
	}
	mmio_write_32(fau_base[index] + FAU_SDR_COORD_ADDR_L, coord);
	mmio_write_32(fau_base[index] + FAU_SDR_COORD_ADDR_H, coord >> 32);
	mmio_write_32(fau_base[index] + FAU_SDR_NODE_CNT, loops);
	mmio_write_32(fau_base[index] + FAU_SDR_REP_ID0, *((uint32_t *)repid));
	mmio_write_32(fau_base[index] + FAU_SDR_REP_ID1, *((uint32_t *)repid + 1));
	mmio_write_32(fau_base[index] + FAU_SDR_REP_ID2, *((uint32_t *)repid + 2));
	mmio_write_32(fau_base[index] + FAU_SDR_REP_ID3, *((uint32_t *)repid + 3));
	mmio_write_32(fau_base[index] + FAU_SDR_REP_ID4, *((uint32_t *)repid + 4));
	mmio_write_32(fau_base[index] + FAU_SDR_REP_ID5, *((uint32_t *)repid + 5));
	mmio_write_32(fau_base[index] + FAU_SDR_REP_ID6, *((uint32_t *)repid + 6));
	mmio_write_32(fau_base[index] + FAU_SDR_REP_ID7, *((uint32_t *)repid + 7));

	return 0;
}

static int SDR_calc(uint8_t *base, uint8_t *exp, uint8_t *coord, uint8_t *repid,
	     uint8_t layerid, uint32_t nodeid, uint32_t loops, int index)
{

	int ret = 0;
	uint64_t coord_index;

	assert(layerid >= 1 && layerid <= 11);
	assert(nodeid > 0);
	assert(nodeid + loops <= (1 << 30));
	assert(((uint64_t)base & 0x1f) == 0x0);
	assert(((uint64_t)coord & 0x1f) == 0x0);
	if (layerid > 1)
		assert(((uint64_t)exp & 0x1f) == 0x0);

#ifdef DEBUG_FAU
	pr_info("base_addr = %lx, exp_addr=%lx, coord=%lx, layerid=%d, nodeid=%d, loops=%d\n",
	       (uint64_t)base, (uint64_t)exp, (uint64_t)coord, layerid, nodeid, loops);
#endif
	coord_index = (uint64_t)coord + nodeid * 56UL;
	ret = SDR_core((uint64_t)base, (uint64_t)exp, coord_index, repid, layerid, nodeid, loops, index);
	start_fau_sdr(index);
	return ret;
}

static void __maybe_unused fau_reset(void *base)
{
	pr_info("fau reset\n");
}

/********************* low level driver end *********************/

static irqreturn_t __maybe_unused fau_irq(int irq, void *_pdev, int core_id)
{
	struct fau_dev *this = _pdev;

	fau_int_clear(this->fau[core_id].reg);//clear interrupt
	complete(&this->fau[core_id].comp);
	return IRQ_HANDLED;

};

static irqreturn_t __maybe_unused fau_irq0(int irq, void *_pdev)
{
	return fau_irq(irq, _pdev, 0);
};

static irqreturn_t __maybe_unused fau_irq1(int irq, void *_pdev)
{
	return fau_irq(irq, _pdev, 1);
};

static irqreturn_t __maybe_unused fau_irq2(int irq, void *_pdev)
{
	return fau_irq(irq, _pdev, 2);
};


static int fau_trigger(struct fau_dev *fau_dev, unsigned long arg)
{
	struct fau_batch batch;
	int ret = 0;

	ret = copy_from_user(&batch, (void *)arg, sizeof(batch));
	if (ret != 0) {
		pr_err("%s copy_from_user wrong,the number of bytes not copied %d, sizeof(batch) total need %lu\n",
			__func__, ret, sizeof(batch));
		ret = FAU_ERR;
		return ret;
	}

	if ((batch.fau_type < 0) || (batch.fau_type > 1)) {
		pr_err("wrong batch.fau_type  %d\n", batch.fau_type);
		ret = FAU_EINVAL;
		return ret;
	}

	if ((batch.index < 0) || (batch.index > 2)) {
		pr_err("wrong batch.fau_index  %d\n", batch.index);
		ret = FAU_EINVAL;
		return ret;
	}

	mutex_lock(&fau_dev->fau[batch.index].fau_mutex);
	fau_base[batch.index] = (u64)fau_dev->fau[batch.index].reg;
	switch (batch.fau_type) {
	case 0:
		ret = Merkle_p1_calc((uint64_t)batch.u.fau_merkel_t.addr, batch.u.fau_merkel_t.sector_size,
				     batch.index, fau_dev);
		if (ret != 0) {
			pr_err("fau merkel calc failed, return value = %d, line= %d\n", ret, __LINE__);
			ret = FAU_ERR;
			mutex_unlock(&fau_dev->fau[batch.index].fau_mutex);
			return ret;
		}
	break;
	case 1:
		ret = SDR_calc(batch.u.fau_sdr_t.base, batch.u.fau_sdr_t.exp, batch.u.fau_sdr_t.coord,
			       batch.u.fau_sdr_t.repid, batch.u.fau_sdr_t.layerid, batch.u.fau_sdr_t.nodeid,
			       batch.u.fau_sdr_t.loops, batch.index);
		if (ret != 0) {
			pr_err("fau sdr calc failed, return value = %d, layerid = %d, line= %d\n", ret,
			       batch.u.fau_sdr_t.layerid, __LINE__);
			ret = FAU_ERR;
			mutex_unlock(&fau_dev->fau[batch.index].fau_mutex);
			return ret;
		}
		wait_for_completion(&fau_dev->fau[batch.index].comp);
	break;
	default:
		pr_err("invalid fau_type\n");
	break;
	}
	mutex_unlock(&fau_dev->fau[batch.index].fau_mutex);
	return ret;
}

static long fau_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct fau_dev *fau;
	int err;

	fau = container_of(file->private_data, struct fau_dev, miscdev);

	switch (cmd) {
	case FAU_OP_PHY:
		err = fau_trigger(fau, arg);
		if (err)
			goto err0;
		break;
	default:
		pr_err("invalid request\n");
		err = -EINVAL;
		goto err0;
	}
	err = 0;
err0:
	return err;
}

static int fau_open(struct inode *inode, struct file *file)
{
	return 0;
}
static int fau_close(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations fau_op = {
	.owner          = THIS_MODULE,
	.open           = fau_open,
	.release        = fau_close,
	.unlocked_ioctl = fau_ioctl,
};

static int fau_device_register(struct fau_dev *fdev)
{
	struct miscdevice *miscdev = &fdev->miscdev;
	int ret;

	miscdev->minor  = MISC_DYNAMIC_MINOR;
	miscdev->name   = "mango-fau";
	miscdev->fops   = &fau_op;
	miscdev->parent = NULL;

	ret = misc_register(miscdev);
	if (ret) {
		pr_err("fau: failed to register misc device.\n");
		return ret;
	}

	return 0;
}

static int fau_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fau_dev *fau_dev;
	int err;
	int i;

	err = dma_set_mask(dev, DMA_BIT_MASK(64));
	if (err) {
		dev_err(dev, "cannot set dma mask\n");
		return err;
	}

	err = dma_set_coherent_mask(dev, DMA_BIT_MASK(64));
	if (err) {
		dev_err(dev, "cannot set dma coherent mask\n");
		return err;
	}

	fau_dev = devm_kzalloc(dev, sizeof(struct fau_dev), GFP_KERNEL);
	if (!fau_dev)
		return -ENOMEM;

	fau_dev->dev = dev;

	for (i = 0; i < FAU_CORE_MAX; i++) {
		spin_lock_init(&fau_dev->fau[i].lock);
		mutex_init(&fau_dev->fau[i].fau_mutex);
		init_completion(&fau_dev->fau[i].comp);
	}
	dev_set_drvdata(dev, fau_dev);

	fau_dev->fau[0].reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(fau_dev->fau[0].reg)) {
		dev_err(dev, "get register fau0 base failed\n");
		return PTR_ERR(fau_dev->fau[0].reg);
	}

	fau_dev->fau[1].reg = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(fau_dev->fau[0].reg)) {
		dev_err(dev, "get register fau1 base failed\n");
		return PTR_ERR(fau_dev->fau[0].reg);
	}

	fau_dev->fau[2].reg = devm_platform_ioremap_resource(pdev, 2);
	if (IS_ERR(fau_dev->fau[0].reg)) {
		dev_err(dev, "get register fau2 base failed\n");
		return PTR_ERR(fau_dev->fau[0].reg);
	}

	pr_debug("fau[0]: %p, 0x%x\n",
		fau_dev->fau[0].reg, readl(fau_dev->fau[0].reg));
	pr_debug("fau[1]: %p, 0x%x\n",
		fau_dev->fau[1].reg, readl(fau_dev->fau[1].reg));
	pr_debug("fau[2]: %p, 0x%x\n",
		fau_dev->fau[2].reg, readl(fau_dev->fau[2].reg));

	err = fau_device_register(fau_dev);
	if (err < 0) {
		pr_err("register fau device error\n");
		return err;
	}

	fau_dev->fau[0].irq = platform_get_irq(pdev, 0);
	if (fau_dev->fau[0].irq < 0) {
		dev_err(dev, "get irq number failed\n");
		return fau_dev->fau[0].irq;
	}

	err = devm_request_irq(dev, fau_dev->fau[0].irq, fau_irq0, 0, "fau0", fau_dev);
	if (err) {
		dev_err(dev, "request fau0 irq failed\n");
		return err;
	}

	fau_dev->fau[1].irq = platform_get_irq(pdev, 1);
	if (fau_dev->fau[1].irq < 0) {
		dev_err(dev, "get irq number failed\n");
		return fau_dev->fau[1].irq;
	}

	err = devm_request_irq(dev, fau_dev->fau[1].irq, fau_irq1, 0, "fau1", fau_dev);
	if (err) {
		dev_err(dev, "request fau1 irq failed\n");
		return err;
	}

	fau_dev->fau[2].irq = platform_get_irq(pdev, 2);
	if (fau_dev->fau[2].irq < 0) {
		dev_err(dev, "get irq number failed\n");
		return fau_dev->fau[2].irq;
	}

	err = devm_request_irq(dev, fau_dev->fau[2].irq, fau_irq2, 0, "fau2", fau_dev);
	if (err) {
		dev_err(dev, "request fau2 irq failed\n");
		return err;
	}

#if 0
	/* reset control */
	fau_dev->fau[0].rst = devm_reset_control_get(dev, "fau0");
	pr_info("get fau0 rst\n");

	if (IS_ERR(fau_dev->fau[0].rst)) {
		ret = PTR_ERR(fau_dev->fau[0].rst);
		dev_err(dev, "failed to retrieve fau0 reset");
		return ret;
	}

	fau_dev->fau[1].rst = devm_reset_control_get(dev, "fau1");
	pr_info("get fau1 rst\n");

	if (IS_ERR(fau_dev->fau[1].rst)) {
		ret = PTR_ERR(fau_dev->fau[1].rst);
		dev_err(dev, "failed to retrieve fau1 reset");
		return ret;
	}

	fau_dev->fau[2].rst = devm_reset_control_get(dev, "fau2");
	pr_info("get fau2 rst\n");

	if (IS_ERR(fau_dev->fau[2].rst)) {
		ret = PTR_ERR(fau_dev->fau[2].rst);
		dev_err(dev, "failed to retrieve fau2 reset");
		return ret;
	}

#endif
	return 0;
}

static int fau_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	dev_dbg(dev, "removed\n");
	return 0;
}

static const struct of_device_id fau_match[] = {
	{.compatible = "sophgo,fau"},
	{},
};

static struct platform_driver fau_driver = {
	.probe = fau_probe,
	.remove = fau_remove,
	.driver = {
		.name = "fau",
		.of_match_table = fau_match,
	},
};

static int __init fau_init(void)
{
	int err;

	err = platform_driver_register(&fau_driver);

	if (err)
		return err;

	return 0;
}

static void __exit fau_exit(void)
{
	platform_driver_unregister(&fau_driver);

}

module_init(fau_init);
module_exit(fau_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SOPHGO FAU");
MODULE_VERSION("0.01");

