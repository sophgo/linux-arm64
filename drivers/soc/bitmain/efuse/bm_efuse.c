/* #define DEBUG */

#define pr_fmt(fmt) "bm-efuse: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/rtc.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/nvmem-provider.h>
#include <linux/nvmem-consumer.h>
#include "kmemac.h"

#define DEV_NAME "bm_efuse"

static void nvmem_destroy(void);
static int nvmem_create(void);
static int sysfs_create(struct device *dev);

/*
 * Organization of EFUSE_ADR register:
 *   [  i:   0]: i <= 6, address of 32-bit cell
 *   [i+5: i+1]: 5-bit bit index within the cell
 */


#define EFUSE_MODE					0x0
#define EFUSE_ADR					0x4
#define EFUSE_RD_DATA				0xc
#define REG_EFUSE_ECCSRAM_ADR		0x10
#define REG_EFUSE_ECCSRAM_RDPORT	0x14


#define FLAG_MASK		0xff000000
#define YEAR_MASK		0x00e00000
#define DAY_MASK		0x001f0000
#define MONTH_MASK		0x0000f000
#define INDEX_MASK		0x00000fff

#define SECURE_LOCK_BIT_MAX	8
#define SECURE_BIT_MAX		8
#define SECURE_REGION_MAX	8

#define FT_BIN_TYPE_ADDR_83 83

#define FT_TEST_RESULT_PASS 0
#define FT_TEST_RESULT_FAIL 1

#define EFUSE_FT_VIDEO_0_BIT        0
#define EFUSE_FT_VIDEO_0_BIT_BAK    1
#define EFUSE_FT_VIDEO_1_BIT        2
#define EFUSE_FT_VIDEO_1_BIT_BAK    3
#define EFUSE_FT_HARD_BIN_0_BIT     4
#define EFUSE_FT_HARD_BIN_0_BIT_BAK 5
#define EFUSE_FT_HARD_BIN_1_BIT     6
#define EFUSE_FT_HARD_BIN_1_BIT_BAK 7

#define EFUSE_FT_FAST_BIN          0
#define EFUSE_FT_NORMAL_BIN        1
#define EFUSE_FT_BORMAL_BIN_NOPCIE 3

struct secure_bit {
	u32 offset, mask;
};

struct secure_region {
	u32 offset, size;
};

struct bm_efuse_device {
	struct cdev cdev;
	struct device *dev;
	struct device *parent;
	struct class *class;
	dev_t devno;
	u32 __iomem *regs;

	struct nvmem_config nvcfg;
	struct nvmem_device *nvdev;
	atomic_t ready;
	atomic_t nvlock;
	struct list_head *memac;
	int protect_secure_key;

	/* secure areas */
	struct secure_bit secure_lock_bits[SECURE_LOCK_BIT_MAX];
	int num_secure_lock_bits;

	struct secure_bit secure_bits[SECURE_BIT_MAX];
	int num_secure_bits;
	struct secure_region secure_regions[SECURE_REGION_MAX];
	int num_secure_regions;
};

struct efuse_ioctl_data {
	uint32_t addr;
	uint32_t val;
};

#define EFUSE_IOCTL_READ	_IOWR('y', 0x20, struct efuse_ioctl_data)
#define EFUSE_IOCTL_WRITE	_IOWR('y', 0x21, struct efuse_ioctl_data)


#define DBG_MSG(fmt, args...) \
do { \
	if (debug_enable == 1) \
		pr_info("[bm_efuse]%s:" fmt, __func__, ##args); \
} while (0)
#define INF_MSG(fmt, args...) pr_err("[bm_efuse]%s:" fmt, __func__, ##args)
#define ERR_MSG(fmt, args...) pr_err("[bm_efuse]%s:" fmt, __func__, ##args)

static struct bm_efuse_device bm_efuse_dev;
static int debug_enable;
static u32 num_address_bits = 7;
static int sram_read_enable;

static struct mutex efuse_mutex;

static u32 efuse_num_cells(void)
{
	return (u32)1 << num_address_bits;
}

static void efuse_mode_md_write(u32 val)
{
	u32 mode = readl(bm_efuse_dev.regs + EFUSE_MODE/4);
	u32 new = (mode & 0xfffffffc) | (val & 0x3);

	writel(new, bm_efuse_dev.regs + EFUSE_MODE/4);
}

static void efuse_mode_wait_ready(void)
{
	int loop = 100;

	while (readl(bm_efuse_dev.regs + EFUSE_MODE/4) != 0x80) {
		if (loop-- > 0)
			mdelay(1);
		else {
			ERR_MSG(" wait ready timeout\n");
			break;
		}
	}
}

static uint32_t make_adr_val(uint32_t address, uint32_t bit_i)
{
	const uint32_t address_mask = (1 << num_address_bits) - 1;

	return (address & address_mask) | ((bit_i & 0x1f) << num_address_bits);
}


static void efuse_mode_reset(void)
{
	writel(0, bm_efuse_dev.regs + EFUSE_MODE/4);
	efuse_mode_wait_ready();
}

u32 efuse_cook(u32 addr, u32 val)
{
	int i;
	u32 start, end;

	if (!bm_efuse_dev.protect_secure_key)
		return val;

	/* check if addr falling into secure area */
	for (i = 0; i < bm_efuse_dev.num_secure_regions; ++i) {

		start = bm_efuse_dev.secure_regions[i].offset;
		end = start + bm_efuse_dev.secure_regions[i].size;

		if (addr >= start && addr < end)
			return 0;
	}

	for (i = 0; i < bm_efuse_dev.num_secure_bits; ++i)
		if (addr == bm_efuse_dev.secure_bits[i].offset)
			return val & ~bm_efuse_dev.secure_bits[i].mask;

	return val;
}

void efuse_trigger_secure_state(u32 addr, u32 val)
{
	int i;

	/* we alwready in protected status */
	if (bm_efuse_dev.protect_secure_key)
		return;

	/* check if addr falling into secure lock bits */

	for (i = 0; i < bm_efuse_dev.num_secure_lock_bits; ++i) {
		if (addr == bm_efuse_dev.secure_lock_bits[i].offset) {
			if (val & bm_efuse_dev.secure_lock_bits[i].mask) {
				bm_efuse_dev.protect_secure_key = true;
				return;
			}
		}
	}
}

static u32 efuse_embedded_sram_read(u32 address)
{
	u32 ret =  0;

	if (address >= efuse_num_cells()) {
		ERR_MSG("invailed address = 0x%x\n", address);
		return -EINVAL;
	}

	mutex_lock(&efuse_mutex);

	if (sram_read_enable == 0 || address == 0 || address == 1) {
		efuse_mode_reset();
		writel(address, bm_efuse_dev.regs + EFUSE_ADR/4);
		efuse_mode_md_write(0x2);
		efuse_mode_wait_ready();
		ret = readl(bm_efuse_dev.regs + EFUSE_RD_DATA/4);
	} else { // sram read
		writel(address - 2, bm_efuse_dev.regs + REG_EFUSE_ECCSRAM_ADR/4);
		ret = readl(bm_efuse_dev.regs + REG_EFUSE_ECCSRAM_RDPORT/4);
	}

	ret = efuse_cook(address, ret);

	mutex_unlock(&efuse_mutex);

	return ret;
}

static void efuse_set_bit(u32 address, u32 bit_i)
{
	u32 adr_val;

	efuse_mode_reset();
	adr_val = make_adr_val(address, bit_i);
	writel(adr_val, bm_efuse_dev.regs + EFUSE_ADR/4);
	efuse_mode_md_write(0x3);
	efuse_mode_wait_ready();
}

static void bm_efuse_sram_reload(void)
{
	int loop = 100;
	unsigned int val;

	// wait eFuse controller idle
	efuse_mode_reset();

	// SRAM loading, do it once when bootup
	val = readl(bm_efuse_dev.regs + EFUSE_MODE / 4);
	writel(val | (0x3 << 30) | (0x1 << 28), bm_efuse_dev.regs + EFUSE_MODE / 4);
	while (readl(bm_efuse_dev.regs + EFUSE_MODE / 4) & (1 << 31)) {
		if (loop-- > 0)
			mdelay(1);
		else {
			ERR_MSG("reload efuse timeout\n");
			break;
		}
	}
}

static void efuse_embedded_write(u32 address, u32 val)
{
	int i = 0;

	mutex_lock(&efuse_mutex);

	efuse_trigger_secure_state(address, val);

	for (i = 0; i < 32; i++) {
		if ((val >> i) & 1)
			efuse_set_bit(address, i);
	}
	bm_efuse_sram_reload();

	mutex_unlock(&efuse_mutex);
}



//para address: the cell number of efuse, such as  0~127
u32 efuse_read(u32 address)
{
	return efuse_embedded_sram_read(address);
}
EXPORT_SYMBOL_GPL(efuse_read);


//get the numbers of efuse cells
u32 efuse_num(void)
{
	return efuse_num_cells();
}
EXPORT_SYMBOL_GPL(efuse_num);


struct rtc_time efuse_get_date(u32 product_flag)
{
	int i = 0;
	u32 val = 0;
	struct rtc_time efuse_date = {0};
	u32 efuse_size = efuse_num_cells();

	for (i = efuse_size - 1; i >= 0; i--) {
		val = efuse_embedded_sram_read(i);
		if ((val & FLAG_MASK)>>24 == product_flag) {
			efuse_date.tm_year = 2018 + ((val&YEAR_MASK)>>21);
			efuse_date.tm_mday = (val & DAY_MASK) >> 16;
			efuse_date.tm_mon = (val & MONTH_MASK) >> 12;
			INF_MSG("product date: %04d-%02d-%02d\n",
				efuse_date.tm_year, efuse_date.tm_mon, efuse_date.tm_mday);
			return efuse_date;
		}
	}
	return efuse_date;
}
EXPORT_SYMBOL_GPL(efuse_get_date);

/*	cap
 *	0: subsys 0,1 both can use
 *	1: subsys 0 can not be use£¬subsys 1 can use
 *	2: subsys 0 can be use£¬subsys 1 can not use
 *	3: subsys 0£¬1 both can not be use
 */
void efuse_ft_get_video_cap(int *cap)
{
	u32 value;

	value = efuse_read(FT_BIN_TYPE_ADDR_83);

	if ((((value>>EFUSE_FT_VIDEO_0_BIT)&0x1) == FT_TEST_RESULT_FAIL) ||
		(((value>>EFUSE_FT_VIDEO_0_BIT_BAK)&0x1) == FT_TEST_RESULT_FAIL))
		*cap = FT_TEST_RESULT_FAIL;
	else
		*cap = FT_TEST_RESULT_PASS;

	if ((((value>>EFUSE_FT_VIDEO_1_BIT)&0x1) == FT_TEST_RESULT_FAIL) ||
		(((value>>EFUSE_FT_VIDEO_1_BIT_BAK)&0x1) == FT_TEST_RESULT_FAIL))
		*cap |= (FT_TEST_RESULT_FAIL<<1);
	else
		*cap |= (FT_TEST_RESULT_PASS<<1);
}
EXPORT_SYMBOL(efuse_ft_get_video_cap);

/*	cap
 *	0: fast bin
 *	1: normal bin
 *	0x11:normal bin without pcie
 */
void efuse_ft_get_bin_type(int *cap)
{
	u32 value;

	value = efuse_read(FT_BIN_TYPE_ADDR_83);

	if ((((value>>EFUSE_FT_HARD_BIN_0_BIT)&0x1) == FT_TEST_RESULT_FAIL) ||
		(((value>>EFUSE_FT_HARD_BIN_0_BIT_BAK)&0x1) == FT_TEST_RESULT_FAIL))
		*cap = FT_TEST_RESULT_FAIL;
	else
		*cap = FT_TEST_RESULT_PASS;

	if ((((value>>EFUSE_FT_HARD_BIN_1_BIT)&0x1) == FT_TEST_RESULT_FAIL) ||
		(((value>>EFUSE_FT_HARD_BIN_1_BIT_BAK)&0x1) == FT_TEST_RESULT_FAIL))
		*cap |= FT_TEST_RESULT_FAIL;
	else
		*cap |= (FT_TEST_RESULT_PASS<<1);
}
EXPORT_SYMBOL(efuse_ft_get_bin_type);

static int  efuse_get_product_sn(char *product_sn)
{
	int i, ret = -1;
	u32 val, index, flag_int;
	struct rtc_time efuse_date = {0};
	u32 efuse_size = efuse_num_cells();
	static const char encrypt_s[] = "JABCDEFGHI";
	char flag_s[8] = {0};
	char date_s[8] = {0};

	for (i = efuse_size - 1; i >= 0; i--) {
		val = efuse_embedded_sram_read(i);
		flag_int = (val & FLAG_MASK)>>24;
		if (flag_int == 0xf3 || flag_int == 0xf2) {
			efuse_date.tm_year = 2018 + ((val&YEAR_MASK)>>21) - 2000;
			efuse_date.tm_mday = (val & DAY_MASK) >> 16;
			efuse_date.tm_mon = (val & MONTH_MASK) >> 12;
			index = val & INDEX_MASK;
			ret = snprintf(flag_s, 8, "%s", (flag_int == 0xf3) ? "SZCCKQR":"SZCCKHX");
			if (ret <= 0 || ret > 8) {
				ERR_MSG("snprintf failed %d, flag_s=%s\n", ret, flag_s);
				return -EFAULT;
			}
			ret = snprintf(date_s, 7, "%c%c%c%c%c%c",
				encrypt_s[efuse_date.tm_year/10],
				encrypt_s[efuse_date.tm_year%10],
				encrypt_s[efuse_date.tm_mon/10],
				encrypt_s[efuse_date.tm_mon%10],
				encrypt_s[efuse_date.tm_mday/10],
				encrypt_s[efuse_date.tm_mday%10]);
			if (ret <= 0 || ret > 7) {
				ERR_MSG("snprintf failed %d, date_s=%s\n", ret, date_s);
				return -EFAULT;
			}

			ret = snprintf(product_sn, 18, "%s%s%04d", flag_s, date_s, index);
			if (ret <= 0 || ret > 18) {
				ERR_MSG("snprintf failed %d, product_sn=%s\n", ret, product_sn);
				return -EFAULT;
			}
			INF_MSG("product sn: %s\n", product_sn);
			return ret;
		}
	}

	return ret;
}


static int  efuse_get_chip_sn(char *chip_sn)
{
	// to do
	strncpy(chip_sn, "unknown", 7);
	return 0;
}


static ssize_t sn_show(struct device *parent, struct device_attribute *attr, char *buf)
{
	int ret = -1;
	char product_sn[32] = {0};
	char chip_sn[32] = {0};

	efuse_get_product_sn(product_sn);
	efuse_get_chip_sn(chip_sn);
	ret = snprintf(buf, 128, "product sn:%s, chip sn:%s\n", product_sn, chip_sn);
	if (ret <= 0 || ret > 128) {
		ERR_MSG("snprintf failed %d\n", ret);
		return -EFAULT;
	}
	DBG_MSG("%s = %s\n", __func__, buf);
	return ret;
}

static ssize_t video_cap_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	int ret = -1;
	int value;

	efuse_ft_get_video_cap(&value);
	ret = snprintf(buf, 8, "%d\n", value);
	if (ret <= 0 || ret > 64) {
		ERR_MSG("video cap show snprintf failed %d\n", ret);
		return -EFAULT;
	}
	DBG_MSG("%s: %s\n", __func__, buf);

	return ret;
}

static ssize_t bin_type_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	int ret = -1;
	int value;

	efuse_ft_get_bin_type(&value);
	ret = snprintf(buf, 8, "%d\n", value);
	if (ret <= 0 || ret > 32) {
		ERR_MSG("video cap show snprintf failed %d\n", ret);
		return -EFAULT;
	}
	DBG_MSG("%s: %s\n", __func__, buf);

	return ret;
}

static DEVICE_ATTR_RO(sn);
static DEVICE_ATTR_RO(video_cap);
static DEVICE_ATTR_RO(bin_type);

static struct attribute *efuse_attrs[] = {
	&dev_attr_sn.attr,
	&dev_attr_video_cap.attr,
	&dev_attr_bin_type.attr,
	NULL,
};
ATTRIBUTE_GROUPS(efuse);

static ssize_t bm_efuse_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	return 0;
}

static ssize_t bm_efuse_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	return 0;
}

static int bm_efuse_open(struct inode *inode, struct file *file)
{
	file->private_data = &bm_efuse_dev;
	return 0;
}

static int bm_efuse_close(struct inode *inode, struct file *file)
{
	return 0;
}

static int bm_efuse_mmap(struct file *file, struct vm_area_struct *vma)
{
	return -EINVAL;
}

static long bm_efuse_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct efuse_ioctl_data data;

	switch (cmd) {
	case EFUSE_IOCTL_READ:
		if (copy_from_user(&data, (struct efuse_ioctl_data __user *)arg,
			sizeof(struct efuse_ioctl_data))) {
			ERR_MSG("read, copy_from_user err\n");
			return -EFAULT;
		}

		if (data.addr > efuse_num_cells()) {
			ERR_MSG("read, invalid addr = 0x%x\n", data.addr);
			return -EINVAL;
		}

		data.val = efuse_embedded_sram_read(data.addr);
		if (copy_to_user((struct efuse_ioctl_data __user *)arg, &data,
			sizeof(struct efuse_ioctl_data))) {
			ERR_MSG("read, copy_to_user err\n");
			return -EFAULT;
		}

		break;
	case EFUSE_IOCTL_WRITE:
		if (copy_from_user(&data, (struct efuse_ioctl_data __user *)arg,
			sizeof(struct efuse_ioctl_data))) {
			ERR_MSG("write, copy_from_user err\n");
			return -EFAULT;
		}

		if (data.addr > efuse_num_cells()) {
			ERR_MSG("write, invalid addr = 0x%x\n", data.addr);
			return -EINVAL;
		}
		efuse_embedded_write(data.addr, data.val);
		break;
	default:
		ERR_MSG("unknown ioctl command %d\n", cmd);
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations bm_efuse_fops = {
	.read = bm_efuse_read,
	.write = bm_efuse_write,
	.open = bm_efuse_open,
	.release = bm_efuse_close,
	.unlocked_ioctl = bm_efuse_ioctl,
	.mmap = bm_efuse_mmap,
	.owner = THIS_MODULE,
};


static int create_bm_efuse_cdev(void)
{
	int ret;

	ret = alloc_chrdev_region(&bm_efuse_dev.devno, 0, 1, DEV_NAME);
	if (ret < 0) {
		ERR_MSG("register chrdev error\n");
		return ret;
	}
	bm_efuse_dev.class = class_create(THIS_MODULE, DEV_NAME);
	bm_efuse_dev.class->dev_groups = efuse_groups;
	if (IS_ERR(bm_efuse_dev.class)) {
		ERR_MSG("create class error\n");
		return -ENOENT;
	}
	bm_efuse_dev.dev = device_create(bm_efuse_dev.class, bm_efuse_dev.parent,
					bm_efuse_dev.devno, NULL, DEV_NAME);
	cdev_init(&bm_efuse_dev.cdev, &bm_efuse_fops);
	bm_efuse_dev.cdev.owner = THIS_MODULE;
	cdev_add(&bm_efuse_dev.cdev, bm_efuse_dev.devno, 1);

	return 0;
}

static void destroy_bm_efuse_cdev(void)
{
	cdev_del(&bm_efuse_dev.cdev);
	device_destroy(bm_efuse_dev.class, bm_efuse_dev.devno);
	class_destroy(bm_efuse_dev.class);
	unregister_chrdev_region(bm_efuse_dev.devno, 1);
}

static int bm_efuse_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct clk *base_clk, *apb_clk;
	int ret;

	atomic_set(&bm_efuse_dev.ready, false);

	bm_efuse_dev.parent = &pdev->dev;

	create_bm_efuse_cdev();
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	bm_efuse_dev.memac = kmemac_add_protected_region(res->start,
							 res->end - res->start);
	if (!bm_efuse_dev.memac)
		return -ENOMEM;

	bm_efuse_dev.regs = devm_ioremap_resource(&pdev->dev, res);

	if (IS_ERR(bm_efuse_dev.regs))
		return PTR_ERR(bm_efuse_dev.regs);

	device_property_read_u32(&pdev->dev, "num_address_bits", &num_address_bits);
	device_property_read_u32(&pdev->dev, "sram_read_enable", &sram_read_enable);
	mutex_init(&efuse_mutex);

	base_clk = devm_clk_get(&pdev->dev, "base_clk");
	if (IS_ERR(base_clk)) {
		dev_warn(&pdev->dev, "cannot get efuse base clk\n");
	} else {
		ret = clk_prepare_enable(base_clk);
		if (ret < 0)
			dev_err(&pdev->dev, "failed to enable base clock\n");
	}

	apb_clk = devm_clk_get(&pdev->dev, "apb_clk");
	if (IS_ERR(apb_clk)) {
		dev_warn(&pdev->dev, "cannot get efuse apb clk\n");
	} else {
		ret = clk_prepare_enable(apb_clk);
		if (ret < 0)
			dev_err(&pdev->dev, "failed to enable apb clock\n");
	}

	atomic_set(&bm_efuse_dev.nvlock, true);
	atomic_set(&bm_efuse_dev.ready, true);
	bm_efuse_dev.protect_secure_key = false;

	/* load secure lock bits */
	ret = device_property_read_u32_array(
			&pdev->dev, "secure-lock-bits", NULL, 0);
	if (ret > 0) {
		if (ret * sizeof(u32) > sizeof(bm_efuse_dev.secure_lock_bits))
			return -ENOMEM;
		bm_efuse_dev.num_secure_lock_bits = ret /
			(sizeof(struct secure_bit) / sizeof(u32));

		device_property_read_u32_array(
			&pdev->dev, "secure-lock-bits",
			(u32 *)bm_efuse_dev.secure_lock_bits, ret);
	}

	/* load secure bits */
	ret = device_property_read_u32_array(
			&pdev->dev, "secure-bits", NULL, 0);
	if (ret > 0) {
		if (ret * sizeof(u32) > sizeof(bm_efuse_dev.secure_bits))
			return -ENOMEM;
		bm_efuse_dev.num_secure_bits = ret /
			(sizeof(struct secure_bit) / sizeof(u32));

		device_property_read_u32_array(
			&pdev->dev, "secure-bits",
			(u32 *)bm_efuse_dev.secure_bits, ret);
	}

	/* load secure regions */
	ret = device_property_read_u32_array(
			&pdev->dev, "secure-regions", NULL, 0);
	if (ret > 0) {
		if (ret * sizeof(u32) > sizeof(bm_efuse_dev.secure_regions))
			return -ENOMEM;
		bm_efuse_dev.num_secure_regions = ret /
			(sizeof(struct secure_region) / sizeof(u32));

		device_property_read_u32_array(
			&pdev->dev, "secure-regions",
			(u32 *)bm_efuse_dev.secure_regions, ret);
	}
	/* when probe reload sram */
	bm_efuse_sram_reload();
	return 0;
}

static int bm_efuse_remove(struct platform_device *pdev)
{
	atomic_set(&bm_efuse_dev.ready, false);

	if (bm_efuse_dev.nvdev)
		nvmem_destroy();

	destroy_bm_efuse_cdev();
	kmemac_remove_protected_region(bm_efuse_dev.memac);
	return 0;
}

static const struct of_device_id bm_efuse_of_match[] = {
	{
		.compatible = "bitmain,bm-efuse",
	},
	{}
};

static struct platform_driver bm_efuse_driver = {
	.probe  = bm_efuse_probe,
	.remove = bm_efuse_remove,
	.driver = {
		.name = "bitmain,bm_efuse",
		.of_match_table = bm_efuse_of_match,
	},
};


static int __init bm_efuse_init(void)
{
	return platform_driver_register(&bm_efuse_driver);
}

arch_initcall(bm_efuse_init);

/* nvmem implementation */

static u32 nvmem_read_cell(int offset)
{
	return efuse_embedded_sram_read(offset);
}

static void nvmem_write_cell(int offset, u32 value)
{
	if (!atomic_read(&bm_efuse_dev.nvlock))
		efuse_embedded_write(offset, value);
}

static int nvmem_read(void *priv, unsigned int off, void *val, size_t count)
{
	int size, left, start, i;
	u32 tmp;
	u8 *dst;

	left = count;
	dst = val;
	/* head */
	if (off & 0x03) {
		size = min_t(int, 4 - (off & 0x03), left);
		start = (off & 0x03);
		tmp = nvmem_read_cell(off >> 2);
		memcpy(dst, &((u8 *)&tmp)[start], size);
		dst += size;
		left -= size;
		off += size;
	}

	/* body */
	size = left >> 2;
	for (i = 0; i < size; ++i) {
		tmp = nvmem_read_cell(off >> 2);
		memcpy(dst, &tmp, 4);
		dst += 4;
		left -= 4;
		off += 4;
	}

	/* tail */
	if (left) {
		tmp = nvmem_read_cell(off >> 2);
		memcpy(dst, &tmp, left);
	}

	return 0;
}

static int nvmem_write(void *priv, unsigned int off, void *val, size_t count)
{
	int size, left, start, i;
	u32 tmp;
	u8 *dst;

	left = count;
	dst = val;
	/* head */
	if (off & 0x03) {
		tmp = 0;
		size = min_t(int, 4 - (off & 0x03), left);
		start = (off & 0x03);
		memcpy(&((u8 *)&tmp)[start], dst, size);
		nvmem_write_cell(off >> 2, tmp);
		dst += size;
		left -= size;
		off += size;
	}

	/* body */
	size = left >> 2;
	for (i = 0; i < size; ++i) {
		memcpy(&tmp, dst, 4);
		nvmem_write_cell(off >> 2, tmp);
		dst += 4;
		left -= 4;
		off += 4;
	}

	/* tail */
	if (left) {
		tmp = 0;
		memcpy(&tmp, dst, left);
		nvmem_write_cell(off >> 2, tmp);
	}

	return count;
}

static int nvmem_create(void)
{
	struct nvmem_config *cfg;
	struct nvmem_device *dev;

	cfg = &bm_efuse_dev.nvcfg;
	cfg->name = dev_name(bm_efuse_dev.parent);
	cfg->dev = bm_efuse_dev.parent;
	cfg->read_only = false;
	cfg->root_only = true;
	cfg->owner = THIS_MODULE;
	cfg->compat = true;
	cfg->base_dev = bm_efuse_dev.parent;
	cfg->reg_read = nvmem_read;
	cfg->reg_write = nvmem_write;
	cfg->priv = &bm_efuse_dev;
	/* efuse is 32bits per cell, but symmetric keys in real word usually
	 * byte sequence, so i implements a least significant byte first scheme.
	 * for symmetric key stored msb in efuse.
	 */
	cfg->stride = 1;
	cfg->word_size = 1;
	cfg->size = efuse_num_cells() * 4;
	dev = nvmem_register(cfg);
	if (IS_ERR(dev)) {
		ERR_MSG("failed to register nvmem device\n");
		return PTR_ERR(dev);
	}

	bm_efuse_dev.nvdev = dev;

	sysfs_create(bm_efuse_dev.parent);

	return 0;
}

static void nvmem_destroy(void)
{
	nvmem_unregister(bm_efuse_dev.nvdev);
}

static int __init nvmem_init(void)
{
	if (!atomic_read(&bm_efuse_dev.ready)) {
		ERR_MSG("efuse device probe failed\n");
		return -ENODEV;
	}

	return nvmem_create();
}

module_init(nvmem_init);

/* sysfs interface, create device file in platform device */

static ssize_t nvmem_lock_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	strcpy(buf, atomic_read(&bm_efuse_dev.nvlock) ? "1\n" : "0\n");
	return 2;
}

static ssize_t nvmem_lock_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	long nvlock;
	int err;

	err = kstrtol(buf, 0, &nvlock);

	if (err)
		return err;

	atomic_set(&bm_efuse_dev.nvlock, !!nvlock);

	return count;
}

static struct device_attribute nvmem_lock_attr = {
	.attr = {
		.name = "nvmem-lock",
		.mode = 0644,
	},
	.show = nvmem_lock_show,
	.store = nvmem_lock_store,
};

static int cell_read8(struct device *dev, const char *name)
{
	struct nvmem_cell *cell;
	size_t len;
	int err;
	u8 *efuse;

	cell = devm_nvmem_cell_get(dev, name);
	if (IS_ERR(cell)) {
		err = PTR_ERR(cell);
		if (err == -ENOENT)
			dev_err(dev, "cell %s not found\n", name);
		else
			dev_err(dev, "get cell %s failed\n", name);

		return err;
	}

	efuse = nvmem_cell_read(cell, &len);
	if (IS_ERR(efuse)) {
		dev_err(dev, "cell read failed\n");
		devm_nvmem_cell_put(dev, cell);
		return PTR_ERR(efuse);
	}

	devm_nvmem_cell_put(dev, cell);

	err = efuse[0];

	kfree(efuse);

	return err;
}

static int cell_write8(struct device *dev, const char *name, u8 data)
{
	struct nvmem_cell *cell;
	int err;

	cell = devm_nvmem_cell_get(dev, name);
	if (IS_ERR(cell)) {
		err = PTR_ERR(cell);
		if (err == -ENOENT)
			dev_err(dev, "cell %s not found\n", name);
		else
			dev_err(dev, "get cell %s failed\n", name);

		return err;
	}

	err = nvmem_cell_write(cell, &data, 1);
	if (err < 0) {
		dev_err(dev, "cell %s write failed\n", name);
		devm_nvmem_cell_put(dev, cell);
		return err;
	}

	devm_nvmem_cell_put(dev, cell);

	return 0;
}

static int cell_write(struct device *dev, const char *name,
		      void *data, size_t len)
{
	struct nvmem_cell *cell;
	int err;

	cell = devm_nvmem_cell_get(dev, name);
	if (IS_ERR(cell)) {
		dev_err(dev, "get cell %s failed\n", name);
		return PTR_ERR(cell);
	}

	err = nvmem_cell_write(cell, data, len);
	if (err < 0)
		dev_err(dev, "write cell %s with length %ld failed", name, len);

	devm_nvmem_cell_put(dev, cell);

	return err < 0 ? err : 0;
}

static ssize_t secure_key_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	/* secure key cannot read out by software, show this node means show
	 * enable status
	 */
	int err;
	char *status;

	err = cell_read8(dev, "secure-key-enable");
	if (err < 0)
		return err;

	status = err ? "enabled\n" : "disabled\n";

	strcpy(buf, status);

	return strlen(status);
}

static ssize_t secure_key_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	/* ascii maybe better, binary format can be used by nvmem attribute */
	const char *begin, *end;
	size_t left = count;
	u8 key[32];
	int err, key_len;

	/* filter out leading unused characters */
	for (begin = buf; !isxdigit(*begin) && left; ++begin)
		--left;

	/* filter out tailing unused characters */
	for (end = buf + count - 1; !isxdigit(*end) && left; --end)
		--left;

	/* all characters are spaces */
	if (left == 0)
		return count;

	/* must 2 character aligned */
	if (left % 2)
		return -EINVAL;

	/* key too long */
	if (left / 2 > sizeof(key))
		return -EINVAL;

	key_len = left / 2;
	memset(key, 0, sizeof(key));
	err = hex2bin(key, begin, key_len);
	if (err)
		return -EINVAL;

	/* enable secure key should prevent a read of secure key area */
	err = cell_write8(dev, "secure-key-enable", 0x03);
	if (err)
		return err;

	err = cell_write(dev, "secure-key", key, sizeof(key));
	if (err)
		return err;

	err = cell_write(dev, "secure-key-backup", key, sizeof(key));
	if (err)
		return err;

	return count;
}

static struct device_attribute secure_key_attr = {
	.attr = {
		.name = "secure-key",
		.mode = 0644,
	},
	.show = secure_key_show,
	.store = secure_key_store,
};

static ssize_t uid_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	struct nvmem_cell *cell, *cell_backup;
	size_t len, len_backup;
	int err, i;
	u8 *efuse, *efuse_backup;
	const char *cell_name;

	cell_name = "uid";
	cell = devm_nvmem_cell_get(dev, cell_name);
	if (IS_ERR(cell)) {
		err = PTR_ERR(cell);
		if (err == -ENOENT)
			/* no uid enable field found */
			dev_err(dev, "cell %s not found\n", cell_name);
		else
			dev_err(dev, "get cell %s failed\n", cell_name);

		return err;
	}

	efuse = nvmem_cell_read(cell, &len);
	if (IS_ERR(efuse)) {
		dev_err(dev, "cell read failed\n");
		devm_nvmem_cell_put(dev, cell);
		return PTR_ERR(efuse);
	}

	devm_nvmem_cell_put(dev, cell);

	cell = devm_nvmem_cell_get(dev, cell_name);
	if (IS_ERR(cell)) {
		err = PTR_ERR(cell);
		if (err == -ENOENT)
			/* no uid enable field found */
			dev_err(dev, "cell %s not found\n", cell_name);
		else
			dev_err(dev, "get cell %s failed\n", cell_name);

		return err;
	}

	cell_name = "uid-backup";
	cell_backup = devm_nvmem_cell_get(dev, cell_name);
	if (IS_ERR(cell_backup)) {
		err = PTR_ERR(cell_backup);
		if (err == -ENOENT)
			/* no uid enable field found */
			dev_err(dev, "cell %s not found\n", cell_name);
		else
			dev_err(dev, "get cell %s failed\n", cell_name);

		return err;
	}
	efuse_backup = nvmem_cell_read(cell_backup, &len_backup);
	if (IS_ERR(efuse_backup)) {
		dev_err(dev, "cell read failed\n");
		devm_nvmem_cell_put(dev, cell_backup);
		return PTR_ERR(efuse_backup);
	}

	devm_nvmem_cell_put(dev, cell_backup);

	if (len != len_backup) {
		kfree(efuse);
		kfree(efuse_backup);
		dev_err(dev,
		"sizes of backup cell and original cell are not equal\n");
		return -EINVAL;
	}

	for (i = 0; i < len; ++i)
		efuse[i] |= efuse_backup[i];

	bin2hex(buf, efuse, len);

	buf[len * 2] = '\n';

	kfree(efuse);
	kfree(efuse_backup);

	return len * 2 + 1;
}

static ssize_t uid_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	/* ascii maybe better, binary format can be used by nvmem attribute */
	const char *begin, *end;
	size_t left = count;
	u8 uid[16];
	int err, uid_len;

	/* filter out leading unused characters */
	for (begin = buf; !isxdigit(*begin) && left; ++begin)
		--left;

	/* filter out tailing unused characters */
	for (end = buf + count - 1; !isxdigit(*end) && left; --end)
		--left;

	/* all characters are spaces */
	if (left == 0)
		return count;

	/* must 2 character aligned */
	if (left % 2)
		return -EINVAL;

	/* uid too long */
	if (left / 2 > sizeof(uid))
		return -EINVAL;

	uid_len = left / 2;
	memset(uid, 0, sizeof(uid));
	err = hex2bin(uid, begin, uid_len);
	if (err)
		return -EINVAL;

	err = cell_write(dev, "uid", uid, sizeof(uid));
	if (err)
		return err;

	err = cell_write(dev, "uid-backup", uid, sizeof(uid));
	if (err)
		return err;

	return count;
}

static struct device_attribute uid_attr = {
	.attr = {
		.name = "uid",
		.mode = 0644,
	},
	.show = uid_show,
	.store = uid_store,
};

static int sysfs_create(struct device *dev)
{
	struct nvmem_cell *cell;
	int err;

	/* create nvmem lock attribute */
	err = device_create_file(dev, &nvmem_lock_attr);
	if (err)
		return err;

	/* check if we have secure-key cell */
	cell = devm_nvmem_cell_get(dev, "secure-key");
	if (IS_ERR(cell)) {
		err = PTR_ERR(cell);
		if (err != -ENOENT)
			return err;
	} else {
		err = device_create_file(dev, &secure_key_attr);
		if (err)
			return err;
	}

	/* check if we have uid cell */
	cell = devm_nvmem_cell_get(dev, "uid");
	if (IS_ERR(cell)) {
		err = PTR_ERR(cell);
		if (err != -ENOENT)
			return err;
	} else {
		err = device_create_file(dev, &uid_attr);
		if (err)
			return err;
	}

	return 0;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yaxun.chen");
MODULE_DESCRIPTION("minimal module");
MODULE_VERSION("ALPHA");
