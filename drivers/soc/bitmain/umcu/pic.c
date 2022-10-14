#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <asm/system_misc.h>
#include <linux/nvmem-provider.h>

#define REG_BOARD_TYPE		0
#define REG_SW_VERSION		1
#define REG_HW_VERSION		2
#define REG_CTRL		3
#define REG_SOC_TEMP		4
#define REG_HEATER_CTRL		5
#define REG_REQUEST		6
#define REG_POWER_LO		7
#define REG_POWER_HI		8

#define REG_FLASH_CMD		0x1d
#define REG_FLASH_ADRL		0x1e
#define REG_FLASH_ADRH		0x1f
#define REG_FLASH_DATA		0x20

#define BOARD_TYPE_SE5		3

#define FLASH_SUPPORT_VERSION	5
#define FLASH_ROW_SIZE		32
#define FLASH_ROW_MASK		(FLASH_ROW_SIZE - 1)
#define FLASH_CMD_LOAD		1
#define FLASH_CMD_STORE		2

#define FLASH_OP_DELAY		4

struct pic_func {
	struct device_attribute attr;
	u8 reg;
	u8 len;
};

static ssize_t sysfs_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int err;
	int len;
	struct pic_func *func;
	struct i2c_client *i2c;
	u64 val = 0;

	func = container_of(attr, struct pic_func, attr);
	i2c = container_of(dev, struct i2c_client, dev);

	WARN_ON(func->len > sizeof(val));

	err = i2c_smbus_read_i2c_block_data(i2c, func->reg,
					    func->len, (u8 *)&val);

	if (err < 0)
		return err;

	switch (func->len) {
	case 2:
		val = le16_to_cpu(val);
		break;
	case 4:
		val = le32_to_cpu(val);
		break;
	case 8:
		val = le64_to_cpu(val);
		break;
	}

	len = sprintf(buf, "%llu", val);

	/* donot including null terminator */
	return len;
}

static struct pic_func pic_funcs[] = {
	{{{"board-type", 0444}, sysfs_show, NULL}, REG_BOARD_TYPE, 1},
	{{{"software-version", 0444}, sysfs_show, NULL}, REG_SW_VERSION, 1},
	{{{"hardware-version", 0444}, sysfs_show, NULL}, REG_HW_VERSION, 1},
	{{{"power-now", 0444}, sysfs_show, NULL}, REG_POWER_LO, 2},
};

struct pic_nvm_ctx {
	struct nvmem_config nvcfg;
	struct nvmem_device *nvdev;
	struct i2c_client *i2c;
};

static int pic_nvm_set_base(struct i2c_client *i2c, uint16_t base)
{
	struct i2c_msg msg;
	uint8_t data[3];
	int err;

	data[0] = REG_FLASH_ADRL;
	data[1] = base & 0xff;
	data[2] = base >> 8;

	msg.addr = i2c->addr;
	msg.buf = data;
	msg.len = 3;
	msg.flags = 0;	/* write message */
	err = i2c_transfer(i2c->adapter, &msg, 1);
	if (err != 1)
		return err < 0 ? err : -EIO;

	return 0;
}

static int pic_nvm_set_cmd(struct i2c_client *i2c, uint8_t cmd)
{
	int err;

	err = i2c_smbus_write_byte_data(i2c, REG_FLASH_CMD, cmd);

	if (err == 0)
		mdelay(FLASH_OP_DELAY);

	return err;
}

static int pic_nvm_get_data(struct i2c_client *i2c, void *data,
			    int offset, int len)
{
	struct i2c_msg msg[2];
	u8 cmd = REG_FLASH_DATA + offset;
	int err;

	memset(msg, 0, sizeof(msg));
	msg[0].addr = i2c->addr;
	msg[0].buf = &cmd;
	msg[0].len = 1;
	msg[0].flags = 0;

	/* data to be read */
	msg[1].addr = i2c->addr;
	msg[1].buf = data;
	msg[1].len = len;
	msg[1].flags = I2C_M_RD;

	err = i2c_transfer(i2c->adapter, msg, ARRAY_SIZE(msg));
	if (err != ARRAY_SIZE(msg))
		return err < 0 ? err : -EIO;

	return 0;
}

static int pic_nvm_set_data(struct i2c_client *i2c, void *data,
			    int offset, int len)
{
	struct i2c_msg msg;
	u8 tmp[FLASH_ROW_SIZE + 1];
	int err;

	memcpy(tmp + 1, data, len);
	tmp[0] = REG_FLASH_DATA + offset;

	memset(&msg, 0, sizeof(msg));
	msg.addr = i2c->addr;
	msg.buf = tmp;
	msg.len = len + 1;
	msg.flags = 0;

	err = i2c_transfer(i2c->adapter, &msg, 1);
	if (err != 1)
		return err < 0 ? err : -EIO;

	return 0;
}

static int pic_nvm_read_block(struct pic_nvm_ctx *ctx,
			      unsigned int base, unsigned int offset,
			      void *val, unsigned int count)
{
	struct i2c_client *i2c = ctx->i2c;
	int err;

	WARN_ON(offset + count > FLASH_ROW_SIZE);

	err = pic_nvm_set_base(i2c, base);
	if (err)
		return err;

	err = pic_nvm_set_cmd(i2c, FLASH_CMD_LOAD);
	if (err)
		return err;

	err = pic_nvm_get_data(i2c, val, offset, count);
	if (err)
		return err;

	return 0;
}

static int pic_nvm_write_block(struct pic_nvm_ctx *ctx,
			       unsigned int base, unsigned int offset,
			       void *val, unsigned int count)
{
	struct i2c_client *i2c = ctx->i2c;
	int err;

	err = pic_nvm_set_base(i2c, base);
	if (err)
		return err;

	/* first load row to ram */
	err = pic_nvm_set_cmd(i2c, FLASH_CMD_LOAD);
	if (err)
		return err;

	/* change ram data */
	err = pic_nvm_set_data(i2c, val, offset, count);
	if (err)
		return err;

	/* save changed data */
	err = pic_nvm_set_cmd(i2c, FLASH_CMD_STORE);
	if (err)
		return err;

	return count;
}

static int pic_nvm_read(void *priv, unsigned int off, void *val, size_t len)
{
	unsigned int base = off & ~FLASH_ROW_MASK;
	unsigned int offset = off & FLASH_ROW_MASK;
	struct pic_nvm_ctx *ctx = priv;
	int err;
	size_t tmp;
	u8 *data = val;

	/* read unaligned prefix data */
	if (offset) {
		tmp = min_t(size_t, (FLASH_ROW_SIZE - offset), len);
		err = pic_nvm_read_block(ctx, base, offset, data, tmp);
		if (err)
			return err;
		len -= tmp;
		data += tmp;
		base += FLASH_ROW_SIZE;
	}

	while (len) {
		tmp = min_t(size_t, FLASH_ROW_SIZE, len);
		err = pic_nvm_read_block(ctx, base, 0, data, tmp);
		if (err)
			return err;
		len -= tmp;
		data += tmp;
		base += FLASH_ROW_SIZE;
	}

	return len;
}

static int pic_nvm_write(void *priv, unsigned int off, void *val, size_t len)
{
	unsigned int base = off & ~FLASH_ROW_MASK;
	unsigned int offset = off & FLASH_ROW_MASK;
	struct pic_nvm_ctx *ctx = priv;
	int err;
	size_t tmp;
	u8 *data = val;

	/* read unaligned prefix data */
	if (offset) {
		tmp = min_t(size_t, (FLASH_ROW_SIZE - offset), len);
		err = pic_nvm_write_block(ctx, base, offset, data, tmp);
		if (err)
			return err;
		len -= tmp;
		data += tmp;
		base += FLASH_ROW_SIZE;
	}

	while (len) {
		tmp = min_t(size_t, FLASH_ROW_SIZE, len);
		err = pic_nvm_write_block(ctx, base, 0, data, tmp);
		if (err)
			return err;
		len -= tmp;
		data += tmp;
		base += FLASH_ROW_SIZE;
	}

	return len;
}

static int pic_nvm_probe(struct i2c_client *i2c,
			 const struct i2c_device_id *ignored)
{
	struct pic_nvm_ctx *ctx;
	struct nvmem_config *cfg;
	int size = 128; /* default size */
	struct device_node *node = i2c->dev.of_node;
	const __be32 *val;
	int err;

	err = i2c_smbus_read_byte_data(i2c, REG_SW_VERSION);

	if (err < 0)
		return -ENODEV;

	if (err < FLASH_SUPPORT_VERSION)
		return 0;

	if (node) {
		val =  of_get_property(node, "size", NULL);
		if (val)
			size = be32_to_cpup(val);
	}

	ctx = devm_kzalloc(&i2c->dev, sizeof(struct pic_nvm_ctx), GFP_KERNEL);
	if (ctx == NULL)
		return -ENOMEM;

	/* remember i2c client */
	ctx->i2c = i2c;
	cfg = &ctx->nvcfg;
	cfg->name = dev_name(&i2c->dev);
	cfg->dev = &i2c->dev;
	cfg->read_only = false;
	cfg->root_only = true;
	cfg->owner = THIS_MODULE;
	cfg->compat = true;
	cfg->base_dev = &i2c->dev;
	cfg->reg_read = pic_nvm_read;
	cfg->reg_write = pic_nvm_write;
	cfg->priv = ctx;
	cfg->stride = 1;
	cfg->word_size = 1;
	cfg->size = size;
	ctx->nvdev = nvmem_register(cfg);
	if (IS_ERR(ctx->nvdev)) {
		dev_err(&i2c->dev, "failed to register nvmem device\n");
		return PTR_ERR(ctx->nvdev);
	}

	return 0;

}

static int pic_probe(struct i2c_client *i2c,
		     const struct i2c_device_id *ignored)
{
	int err, i;

	err = i2c_smbus_read_byte_data(i2c, REG_BOARD_TYPE);

	if (err < 0)
		return -ENODEV;

	if (err != BOARD_TYPE_SE5)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(pic_funcs); ++i) {
		err = device_create_file(&i2c->dev, &pic_funcs[i].attr);
		if (err)
			return err;
	}

	/* try nvm probe */
	return pic_nvm_probe(i2c, ignored);
}

static int pic_remove(struct i2c_client *i2c)
{
	return 0;
}

static const struct of_device_id pic_dt_table[] = {
	{ .compatible = "bitmain,bm16xx-pic" },
	{},
};

static const struct i2c_device_id pic_id_table[] = {
	{ "bm16xx-pic", 0 },
	{},
};

static struct i2c_driver pic_drv = {
	.driver = {
		.name = "bm16xx-pic",
		.of_match_table = pic_dt_table,
	},
	.probe = pic_probe,
	.remove = pic_remove,
	.id_table = pic_id_table,
};

module_i2c_driver(pic_drv);

MODULE_DESCRIPTION("PIC Driver for BM16xx SoC Platform");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chao Wei <chao.wei@bitmain.com>");
