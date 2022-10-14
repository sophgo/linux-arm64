#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/ctype.h>
#include <linux/miscdevice.h>
#include <linux/timer.h>
#include <linux/io.h>
#include <linux/reset.h>
#include <linux/delay.h>

#define CTRL0			0
#define INT_ENABLE_SHIFT	1
#define INT_ENABLE_MASK		0x00000006
#define INT_ENABLE_VALUE	0x1

#define STOP_RXU_SHIFT		10
#define STOP_RXU_MASK		0x00000400
#define STOP_RXU_VALUE		0x1

#define STOP_FINISH_SHIFT	11
#define STOP_FINISH_MASK	0x00000800
#define STOP_FINISH_VALUE	0x1

#define FPRC_SET_SHIFT		16
#define FPRC_SET_MASK		0x00010000

#define FPRC_VALUE_SHIFT	17
#define FPRC_VALUE_MASK		0x00060000


#define MISC_CONFIG0		0x5

#define EXE_STALL_VEC		0x8
#define HANG_FLAG_SHIFT		31
#define HANG_FLAG_MASK		0x80000000

#define DATASET_ADDR		0x18

#define MSG_VLD_NONCE		0x54c
#define MSG_VALUE		0x5d0
#define MSG_DIFF		0x624
#define MSG_NONCE_CTRL		0x628

#define MSG_ID_SHIFT		0
#define MSG_ID_MASK		0x000000ff
#define MSG_LEN_SHIFT		8
#define MSG_LEN_MASK		0x0000ff00
#define NONCE_INCR_SHIFT	16
#define NONCE_INCR_MASK		0xffff0000

#define MSG_START_NONCE		0x620

#define CTRL1			0x62c

#define MSG_HASH_ID_SHIFT	0
#define MSG_HASH_ID_MASK	0xff
#define MSG_VLD_SHIFT		8
#define MSG_VLD_MASK		0x00000100
#define MSG_HASH_VLD_SHIFT	13
#define MSG_HASH_VLD_MASK	0x00002000

#define MSG_HASH_VALUE		0x630

#define MANGO_SOFT_RESET_BASE	0x7030013000
#define MANGO_SOFT_RESET_SIZE	0xc

static inline void __iomem *rxu_addr(void __iomem *base, unsigned int offset)
{
	return (void __iomem *)((unsigned long)base + offset);
}

static void rxu_set_fprc(void __iomem *base, u8 fprc)
{
	u32 tmp;
	void __iomem *reg = rxu_addr(base, CTRL0);

	pr_debug("set fprc to %u\n", fprc);

	tmp = readl(reg);
	tmp &= ~FPRC_VALUE_MASK;
	tmp |= (fprc << FPRC_VALUE_SHIFT) & FPRC_VALUE_MASK;
	tmp |= FPRC_SET_MASK;
	writel(tmp, reg);
}

static u8 rxu_get_fprc(void __iomem *base)
{
	return (readl(rxu_addr(base, CTRL0)) & FPRC_VALUE_MASK)
		>> FPRC_VALUE_SHIFT;
}

static void rxu_set_dataset(void __iomem *base, u64 addr)
{
	pr_debug("set dataset to %llu\n", addr);

	writeq(addr, rxu_addr(base, DATASET_ADDR));
}

static u64 rxu_get_dataset(void __iomem *base)
{
	return readq(rxu_addr(base, DATASET_ADDR));
}

static void rxu_set_msg_value(void __iomem *base, void *msg, u8 len)
{
	u32 tmp;

	pr_debug("msg value set len=%d\n", len);

	memcpy_toio(rxu_addr(base, MSG_VALUE), msg, len);

	tmp = readl(rxu_addr(base, MSG_NONCE_CTRL));
	tmp &= ~MSG_LEN_MASK;
	tmp |= (len << MSG_LEN_SHIFT) & MSG_LEN_MASK;
	writel(tmp, rxu_addr(base, MSG_NONCE_CTRL));
}

static void rxu_set_msg_diff(void __iomem *base, u32 diff)
{
	pr_debug("msg diff set %u\n", diff);

	writel(diff, rxu_addr(base, MSG_DIFF));
}

static void rxu_set_msg_nonce_incr(void __iomem *base, u16 incr)
{
	u32 tmp;

	pr_debug("msg nonce incr set %u\n", incr);

	tmp = readl(rxu_addr(base, MSG_NONCE_CTRL));
	tmp &= ~NONCE_INCR_MASK;
	tmp |= (incr << NONCE_INCR_SHIFT) & NONCE_INCR_MASK;
	writel(tmp, rxu_addr(base, MSG_NONCE_CTRL));
}

static void rxu_set_msg_id(void __iomem *base, u8 id)
{
	u32 tmp;

	pr_debug("msg id set %u\n", id);

	tmp = readl(rxu_addr(base, MSG_NONCE_CTRL));
	tmp &= ~MSG_ID_MASK;
	tmp |= (id << MSG_ID_SHIFT) & MSG_ID_MASK;
	writel(tmp, rxu_addr(base, MSG_NONCE_CTRL));
}

static void rxu_set_msg_start_nonce(void __iomem *base, u32 nonce)
{
	pr_debug("msg start nonce set %u\n", nonce);

	writel(nonce, rxu_addr(base, MSG_START_NONCE));
}

static int rxu_result_ready(void *base)
{
	return !!(readl(rxu_addr(base, CTRL1)) & MSG_HASH_VLD_MASK);
}

static void rxu_set_hash_vld(void *base, unsigned int val)
{
	u32 tmp;

	pr_debug("hash vld set %d\n", val);

	tmp = readl(rxu_addr(base, CTRL1));
	if (val)
		tmp |= MSG_HASH_VLD_MASK;
	else
		tmp &= ~MSG_HASH_VLD_MASK;
	writel(tmp, rxu_addr(base, CTRL1));
}

static void rxu_get_hash(void *base, unsigned int *id, u32 *nonce, void *hash)
{
	*id = (readl(rxu_addr(base, CTRL1)) & MSG_HASH_ID_MASK)
		>> MSG_HASH_ID_SHIFT;
	*nonce = readl(rxu_addr(base, MSG_VLD_NONCE));
	memcpy_fromio(hash, rxu_addr(base, MSG_HASH_VALUE), 32);
}

static void rxu_set_rxu_id(void __iomem *base, u8 id)
{
	writeb(id, rxu_addr(base, MISC_CONFIG0));
}

static bool rxu_is_hang(void __iomem *base)
{
	u32 tmp;
	void __iomem *reg = rxu_addr(base, EXE_STALL_VEC);

	tmp = readl(reg);

	return tmp & HANG_FLAG_MASK;
}

static void rxu_interrupt_enable(void __iomem *base)
{
	u32 tmp;
	void __iomem *reg = rxu_addr(base, CTRL0);

	tmp = readl(reg);
	tmp &= ~INT_ENABLE_MASK;
	tmp |= (INT_ENABLE_VALUE << INT_ENABLE_SHIFT);
	writel(tmp, reg);
}

static void rxu_interrupt_disable(void __iomem *base)
{
	u32 tmp;
	void __iomem *reg = rxu_addr(base, CTRL0);

	tmp = readl(reg);
	tmp &= ~INT_ENABLE_MASK;
	writel(tmp, reg);
}

static int rxu_get_stop_finish(void __iomem *base)
{
	void __iomem *reg = rxu_addr(base, CTRL0);

	return !!(readl(reg) & STOP_FINISH_MASK);
}

static void rxu_clean_stop_finish(void __iomem *base)
{
	u32 tmp;
	void __iomem *reg = rxu_addr(base, CTRL0);

	tmp = readl(reg);
	tmp &= ~STOP_FINISH_MASK;
	writel(tmp, reg);
}

static void rxu_set_stop(void __iomem *base, int val)
{
	u32 tmp;
	void __iomem *reg = rxu_addr(base, CTRL0);

	tmp = readl(reg);
	if (val)
		tmp |= STOP_RXU_VALUE << STOP_RXU_SHIFT;
	else
		tmp &= ~STOP_RXU_MASK;

	writel(tmp, reg);
}

static void rxu_start(void __iomem *base)
{
	u32 tmp;

	pr_debug("rxu start\n");

	tmp = readl(rxu_addr(base, CTRL1));
	tmp |= MSG_VLD_MASK;
	writel(tmp, rxu_addr(base, CTRL1));
}

static void rxu_stop(void __iomem *base)
{
	/* enable rxu stop */
	rxu_set_stop(base, true);

	/* wati rxu stop finished */
	while (!rxu_get_stop_finish(base))
		;

	rxu_set_hash_vld(base, false);
	/* clean stop bit */
	rxu_set_stop(base, false);
	rxu_clean_stop_finish(base);
}

struct msg_info {
	uint8_t msg_id;
	uint8_t msg_len;
	uint8_t diff;
	uint8_t rxu_id;
	uint32_t start_nonce;
	uint16_t nonce_incr;
	uint8_t reserve1[6];
	uint64_t dataset_pa;
	uint8_t msg[80];
};

struct rxu_result {
	uint8_t msg_id;
	uint8_t reserved[3];
	uint32_t nonce;
	uint8_t hash[32];
};

struct rxu_dev {
	struct device *dev;
	struct miscdevice miscdev;
	struct rxu_reg __iomem *reg;

	/* rxu clk & reset control*/
	struct clk *clk_rxu;
	struct reset_control *riscv_rst;
	struct reset_control *rxu_rst;

	/* rxu watchdog */
	struct timer_list timer;
	wait_queue_head_t wq;
	int hash_vld;
	spinlock_t lock;

	/* rxu init flag*/
	int init_flag;
	/* msg info backup */
	struct msg_info current_msg;
};

static struct rxu_dev *to_rxu_dev(struct file *file)
{
	struct miscdevice *miscdev = file->private_data;

	return container_of(miscdev, struct rxu_dev, miscdev);
}

static void rxu_reset(struct rxu_dev *rxu_dev)
{
	dev_dbg(rxu_dev->dev, "rxu reset\n");

	reset_control_deassert(rxu_dev->riscv_rst);

	reset_control_assert(rxu_dev->rxu_rst);

	udelay(2);

	reset_control_deassert(rxu_dev->rxu_rst);
}

static void rxu_config(struct rxu_dev *rxu_dev, struct msg_info *info)
{
	spin_lock(&rxu_dev->lock);
	rxu_set_rxu_id(rxu_dev->reg, info->rxu_id);
	rxu_set_dataset(rxu_dev->reg, info->dataset_pa);
	rxu_set_msg_id(rxu_dev->reg, info->msg_id);
	rxu_set_msg_diff(rxu_dev->reg, info->diff);
	rxu_set_msg_start_nonce(rxu_dev->reg, info->start_nonce);
	rxu_set_msg_value(rxu_dev->reg, info->msg, info->msg_len);
	rxu_set_msg_nonce_incr(rxu_dev->reg, info->nonce_incr);
	spin_unlock(&rxu_dev->lock);
}

static void rxu_recovery(struct rxu_dev *rxu_dev)
{
	rxu_reset(rxu_dev);
	rxu_config(rxu_dev, &rxu_dev->current_msg);
	rxu_interrupt_enable(rxu_dev->reg);
	rxu_start(rxu_dev->reg);
}

static void rxu_wtd(struct timer_list *timer)
{
	struct rxu_dev *rxu_dev = from_timer(rxu_dev, timer, timer);

	if (rxu_is_hang(rxu_dev->reg))
		rxu_recovery(rxu_dev);

	mod_timer(&rxu_dev->timer, jiffies + 1);
}

static ssize_t rxu_read(struct file *filp, char __user *buf, size_t size,
			loff_t *ppos)
{
	struct rxu_result res;
	struct rxu_dev *rxu_dev;
	unsigned int id;
	unsigned int nonce;
	int err;

	rxu_dev = to_rxu_dev(filp);

	wait_event_interruptible(rxu_dev->wq, rxu_dev->hash_vld != false);
	if (rxu_dev->hash_vld == false)
		return 0;

	rxu_dev->hash_vld = false;
	rxu_get_hash(rxu_dev->reg, &id, &nonce, res.hash);
	rxu_set_hash_vld(rxu_dev->reg, false);
	res.msg_id = id;
	res.nonce = nonce;

	err = copy_to_user(buf, &res, size);

	return err ? -EFAULT : size;
}

static ssize_t rxu_write(struct file *filp, const char __user *buf, size_t size,
			 loff_t *ppos)
{
	struct rxu_dev *rxu_dev;
	struct msg_info *msg;
	int err;

	rxu_dev = to_rxu_dev(filp);
	msg = &rxu_dev->current_msg;

	err = copy_from_user(msg, buf, size);
	if (err)
		return -EFAULT;

	rxu_config(rxu_dev, msg);
	rxu_start(rxu_dev->reg);

	return 0;
}

static int rxu_open(struct inode *inode, struct file *filp)
{
	struct rxu_dev *rxu_dev = to_rxu_dev(filp);

	rxu_reset(rxu_dev);
	rxu_interrupt_enable(rxu_dev->reg);

	rxu_dev->timer.expires = jiffies + 1;
	add_timer(&rxu_dev->timer);

	return 0;
}

static int rxu_close(struct inode *inode, struct file *filp)
{
	struct rxu_dev *rxu_dev = to_rxu_dev(filp);

	rxu_interrupt_disable(rxu_dev->reg);
	rxu_stop(rxu_dev->reg);

	del_timer_sync(&rxu_dev->timer);

	return 0;
}

static const struct file_operations rxu_fops = {
	.owner = THIS_MODULE,
	.open =  rxu_open,
	.read = rxu_read,
	.write = rxu_write,
	.release = rxu_close,
};

static void init_miscdevice(struct device *dev, struct miscdevice *miscdev)
{
	char name[32] = {0};
	const char *dn;
	long reg;

	dn = dev_name(dev);
	if (sscanf(dn, "%lx.%s", &reg, name) < 1)
		dev_err(dev, "get rxu dev name failed\n");

	miscdev->name = kstrdup(name, GFP_KERNEL);
	miscdev->minor = MISC_DYNAMIC_MINOR;
	miscdev->fops = &rxu_fops;
}

static irqreturn_t rxu_irq(int irq, void *dev)
{
	struct rxu_dev *rxu_dev = (struct rxu_dev *)dev;

	rxu_set_hash_vld(rxu_dev->reg, false);

	rxu_dev->hash_vld = true;
	wake_up_interruptible(&rxu_dev->wq);

	return IRQ_HANDLED;
};

static ssize_t fprc_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct rxu_dev *rxu_dev = dev_get_drvdata(dev);

	return sprintf(buf, "%u", rxu_get_fprc(rxu_dev->reg));
}

static ssize_t fprc_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t len)
{
	struct rxu_dev *rxu_dev = dev_get_drvdata(dev);
	char tmp[32];
	unsigned long val;
	int err;

	len = min(sizeof(tmp) - 1, len);
	tmp[len] = 0;
	err = kstrtoul(buf, 0, &val);

	if (err)
		return err;

	dev_dbg(dev, "set fprc to %lu\n", val);

	rxu_set_fprc(rxu_dev->reg, val);

	return len;
}

static ssize_t dataset_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct rxu_dev *rxu_dev = dev_get_drvdata(dev);

	return sprintf(buf, "%llu\n", rxu_get_dataset(rxu_dev->reg));

}

static ssize_t dataset_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct rxu_dev *rxu_dev = dev_get_drvdata(dev);
	char tmp[32];
	unsigned long val;
	int err;

	len = min(sizeof(tmp) - 1, len);
	tmp[len] = 0;
	err = kstrtoul(buf, 0, &val);

	if (err)
		return err;

	dev_dbg(dev, "set dataset to %lu\n", val);

	rxu_set_dataset(rxu_dev->reg, val);

	return len;
}

static ssize_t init_rxu_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct rxu_dev *rxu_dev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", rxu_dev->init_flag);
}

static ssize_t init_rxu_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t len)
{
	struct rxu_dev *rxu_dev = dev_get_drvdata(dev);
	unsigned long val = 0;
	int err;

	err = kstrtoul(buf, 0, &val);
	if (err)
		return err;

	if (val == 1) {
		rxu_start(rxu_dev->reg);
		rxu_dev->init_flag = 1;
	} else if (val == 0) {
		rxu_reset(rxu_dev);
		rxu_dev->init_flag = 0;
	} else {
		dev_dbg(dev, "error,please input 1 to start, input 0 to reset\n");
	}

	return len;
}

static ssize_t msg_value_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t len)
{
	struct rxu_dev *rxu_dev = dev_get_drvdata(dev);
	const char *begin, *end;
	size_t left = len;
	char data[80] = {0};
	int err;

	/* filter out leading unused characters */
	for (begin = buf; !isxdigit(*begin) && left; ++begin)
		--left;

	/* filter out tailing unused characters */
	for (end = buf + len - 1; !isxdigit(*end) && left; --end)
		--left;

	/* all characters are spaces */
	if (left == 0)
		return 0;

	/* must 2 character aligned */
	if (left % 2)
		return -EINVAL;

	/* left too long */
	if (left / 2 > sizeof(data))
		return -EINVAL;

	left = left / 2;
	memset(data, 0, sizeof(data));
	err = hex2bin(data, begin, left);
	if (err)
		return -EINVAL;

	/* TODO: convert ascii message to binary format */
	rxu_set_msg_value(rxu_dev->reg, (void *)data, left);

	return len;
}

static ssize_t msg_value_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	return 0;
}

static ssize_t msg_diff_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t len)
{
	struct rxu_dev *rxu_dev = dev_get_drvdata(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 0, &val);

	if (err)
		return err;

	rxu_set_msg_diff(rxu_dev->reg, val);

	return len;
}

static ssize_t msg_diff_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return 0;
}

static ssize_t msg_nonce_incr_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t len)
{
	struct rxu_dev *rxu_dev = dev_get_drvdata(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 0, &val);

	if (err)
		return err;

	rxu_set_msg_nonce_incr(rxu_dev->reg, val);

	return len;
}

static ssize_t msg_nonce_incr_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t msg_id_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t len)
{
	struct rxu_dev *rxu_dev = dev_get_drvdata(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 0, &val);

	if (err)
		return err;

	rxu_set_msg_id(rxu_dev->reg, val);

	return len;
}

static ssize_t msg_id_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	return 0;
}

static ssize_t msg_start_nonce_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	struct rxu_dev *rxu_dev = dev_get_drvdata(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 0, &val);

	if (err)
		return err;

	rxu_set_msg_start_nonce(rxu_dev->reg, val);

	return len;
}

static ssize_t msg_start_nonce_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	return 0;
}

static ssize_t get_hash_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t len)
{
	return 0;
}

static ssize_t get_hash_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct rxu_result info;
	struct rxu_dev *rxu_dev = dev_get_drvdata(dev);
	int len = 0;
	uint32_t id;
	uint32_t nonce;

	rxu_get_hash(rxu_dev->reg, &id, &nonce, info.hash);
	info.msg_id = id;
	info.nonce = nonce;

	len = sizeof(info);
	memcpy((void *)buf, (const void *)&info, len);

	return len;
}

static ssize_t msg_hash_vld_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct rxu_dev *rxu_dev = dev_get_drvdata(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 0, &val);

	if (err)
		return err;

	rxu_set_hash_vld(rxu_dev->reg, val);

	return len;
}

static ssize_t msg_hash_vld_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct rxu_dev *rxu_dev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", rxu_result_ready(rxu_dev->reg));
}

static ssize_t rxu_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t len)
{
	struct rxu_dev *rxu_dev = dev_get_drvdata(dev);

	struct msg_info *msg_info = (struct msg_info *)buf;

	rxu_set_msg_id(rxu_dev->reg, msg_info->msg_id);
	rxu_set_msg_diff(rxu_dev->reg, msg_info->diff);
	rxu_set_msg_start_nonce(rxu_dev->reg, msg_info->start_nonce);
	rxu_set_dataset(rxu_dev->reg, msg_info->dataset_pa);
	rxu_set_msg_value(rxu_dev->reg, msg_info->msg, msg_info->msg_len);
	rxu_set_msg_nonce_incr(rxu_dev->reg, msg_info->nonce_incr);

	rxu_start(rxu_dev->reg);

	return len;
}

static ssize_t rxu_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct rxu_result *rxu_result = (struct rxu_result *)buf;
	struct rxu_dev *rxu_dev = dev_get_drvdata(dev);
	unsigned int id;
	unsigned int nonce;

	if (rxu_result_ready(rxu_dev->reg)) {
		rxu_get_hash(rxu_dev->reg, &id, &nonce, rxu_result->hash);
		rxu_set_hash_vld(rxu_dev->reg, 0);
		rxu_result->msg_id = id;
		rxu_result->nonce = nonce;

		return sizeof(struct rxu_result);
	}

	dev_dbg(dev, "no valid rxu data\n");

	return 0;
}

const struct device_attribute rxu_attrs[] =  {
	{{"fprc", 0666}, fprc_show, fprc_store},
	{{"dataset", 0666}, dataset_show, dataset_store},
	{{"init_rxu", 0666}, init_rxu_show, init_rxu_store},
	{{"msg_value", 0666}, msg_value_show, msg_value_store},
	{{"msg_diff", 0666}, msg_diff_show, msg_diff_store},
	{{"msg_nonce_incr", 0666}, msg_nonce_incr_show, msg_nonce_incr_store},
	{{"msg_id", 0666}, msg_id_show, msg_id_store},
	{{"msg_start_nonce", 0666}, msg_start_nonce_show, msg_start_nonce_store},
	{{"msg_hash_vld", 0666}, msg_hash_vld_show, msg_hash_vld_store},
	{{"get_hash", 0666}, get_hash_show, get_hash_store},
	{{"rxu", 0666}, rxu_show, rxu_store},
};

static int rxu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rxu_reg __iomem *reg;
	struct rxu_dev *rxu_dev;
	int irq;
	int err;
	int i;

	rxu_dev = devm_kzalloc(dev, sizeof(struct rxu_dev), GFP_KERNEL);
	if (!rxu_dev)
		return -ENOMEM;

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

	reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg)) {
		dev_err(dev, "get register base failed\n");
		return PTR_ERR(reg);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "get irq number failed\n");
		return irq;
	}
#ifdef MANGO_CLOCK
	rxu_dev->clk_rxu = devm_clk_get(dev, NULL);
	if (IS_ERR(rxu_dev->clk_rxu)) {
		dev_err(dev, "failed to get rxu clock\n");
		return PTR_ERR(rxu_dev->rxu_clk);
	}
#endif
	rxu_dev->riscv_rst = devm_reset_control_get_shared(dev, "riscv");
	if (IS_ERR(rxu_dev->riscv_rst)) {
		dev_err(dev, "failed to get riscv reset control\n");
		return PTR_ERR(rxu_dev->riscv_rst);
	}

	rxu_dev->rxu_rst = devm_reset_control_get_exclusive(dev, "rxu");
	if (IS_ERR(rxu_dev->rxu_rst)) {
		dev_err(dev, "failed to get rxu reset control\n");
		return PTR_ERR(rxu_dev->rxu_rst);
	}

	spin_lock_init(&rxu_dev->lock);
	init_waitqueue_head(&rxu_dev->wq);
	rxu_dev->reg = reg;
	rxu_dev->dev = dev;
	rxu_dev->init_flag = 0;
	dev_set_drvdata(dev, rxu_dev);

	timer_setup(&rxu_dev->timer, rxu_wtd, 0);

	err = devm_request_irq(dev, irq, rxu_irq, 0, dev_name(dev), rxu_dev);
	if (err) {
		dev_err(dev, "request irq failed\n");
		return err;
	}

	init_miscdevice(dev, &rxu_dev->miscdev);
	err = misc_register(&rxu_dev->miscdev);
	if (err != 0) {
		dev_err(dev, "could not register rxu device\n");
		return err;
	}

	rxu_reset(rxu_dev);
#ifdef MANGO_CLOCK
	err = clk_prepare_enable(rxu_dev->clk_rxu);
	if (err < 0) {
		dev_err(dev, "failed to enable rxu clock\n");
		misc_deregister(&rxu_dev->miscdev);
		return err;
	}
#endif

	/* create sysfs */
	for (i = 0; i < ARRAY_SIZE(rxu_attrs); ++i) {
		err = device_create_file(dev, &rxu_attrs[i]);
		if (err) {
			dev_err(dev, "create attribute %s failed",
				rxu_attrs[i].attr.name);

			device_remove_file(dev, &rxu_attrs[i]);
			return err;
		}
	}

	return 0;
}

static int rxu_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rxu_dev *rxu_dev;
	int i = 0;

	dev_dbg(dev, "removed\n");

	rxu_dev = dev_get_drvdata(dev);

	for (i = 0; i < ARRAY_SIZE(rxu_attrs); ++i)
		device_remove_file(dev, &rxu_attrs[i]);

	misc_deregister(&rxu_dev->miscdev);

	return 0;
}

static const struct of_device_id rxu_match[] = {
	{.compatible = "sophgo,rxu"},
	{},
};

static struct platform_driver rxu_driver = {
	.probe = rxu_probe,
	.remove = rxu_remove,
	.driver = {
		.name = "rxu",
		.of_match_table = rxu_match,
	},
};

static int __init rxu_init(void)
{
	int err;

	pr_debug("RXU init!\n");

	err = platform_driver_register(&rxu_driver);

	if (err)
		return err;

	return 0;
}

static void __exit rxu_exit(void)
{
	platform_driver_unregister(&rxu_driver);

	pr_debug("RXU exit!\n");
}

module_init(rxu_init);
module_exit(rxu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chao.Wei");
MODULE_DESCRIPTION("SOPHGO RXU");
MODULE_VERSION("0.01");

