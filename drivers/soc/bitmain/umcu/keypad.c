#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/input.h>
#include <uapi/linux/input-event-codes.h>
#include <linux/input/matrix_keypad.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/workqueue.h>

#define SUPPORT_KEYS_MAX (8 * 256)
/* in ms */
#define CHECK_INTERVAL	100

/*
 * interval is in jiffies
 */
struct ctx {
	struct delayed_work work;
	unsigned int interval;
	struct input_dev *input;
	struct i2c_client *i2c;
	unsigned int cols;
	unsigned int rows;
	unsigned int keys;
};

static inline struct device *i2c2dev(struct i2c_client *i2c)
{
	return &i2c->dev;
}

static void work_handler(struct work_struct *work)
{
	struct ctx *ctx = container_of(work, struct ctx, work.work);
	int i, j, err, key;
	unsigned int code;
	unsigned short *keycode_map = ctx->input->keycode;

	/* LSB and LSb */
	for (i = 0; i < SUPPORT_KEYS_MAX; i += 8) {
		err = i2c_smbus_read_byte_data(ctx->i2c, i / 8);
		if (err < 0)
			break;

		for (j = 0; j < 8; ++j) {
			if (err & (1 << j)) {
				/* to bit string */
				/* calculate bit position */
				code = i * 8 + j;
				key = keycode_map[code];

				input_report_key(ctx->input, key, 1);
				input_report_key(ctx->input, key, 0);
			}
		}
		if (i2c_smbus_write_byte_data(ctx->i2c, i, err))
			break;
	}

	input_sync(ctx->input);
	schedule_delayed_work(&ctx->work, ctx->interval);
}

static int keypad_open(struct input_dev *input)
{
	struct ctx *ctx = input_get_drvdata(input);

	INIT_DELAYED_WORK(&ctx->work, work_handler);
	schedule_delayed_work(&ctx->work, ctx->interval);
	return 0;
}

static void keypad_close(struct input_dev *input)
{
	struct ctx *ctx = input_get_drvdata(input);

	cancel_delayed_work_sync(&ctx->work);
}

static int keypad_probe(struct i2c_client *i2c,
			const struct i2c_device_id *ignore)
{
	int err;
	struct input_dev *input;
	struct ctx *ctx;
	struct device *dev;
	unsigned int rows, cols;

	dev = i2c2dev(i2c);

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(dev, "%s adapter cannot support i2c smbus byte data access\n",
			dev_driver_string(&i2c->adapter->dev));
		return -ENODEV;
	}

	ctx = devm_kzalloc(dev, sizeof(struct ctx), GFP_KERNEL);
	if (ctx == NULL)
		return -ENOMEM;

	memset(ctx, 0, sizeof(*ctx));

	input = devm_input_allocate_device(dev);

	if (input == NULL) {
		err = -ENOMEM;
		goto free_ctx;
	}

	ctx->interval = msecs_to_jiffies(CHECK_INTERVAL);
	ctx->input = input;
	ctx->i2c = i2c;

	input->name = "bitmain-virtual-keypad";
	input->phys = "bitmain-virtual-kaypad/input0";

	input_set_drvdata(input, ctx);
	i2c_set_clientdata(i2c, ctx);

	input->id.bustype = BUS_I2C;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0;

	input->open = keypad_open;
	input->close = keypad_close;

	/* phase keymap */
	err = matrix_keypad_parse_of_params(dev, &rows, &cols);
	if (err) {
		dev_err(dev, "phase keymap failed\n");
		goto free_input;
	}

	ctx->rows = rows;
	ctx->cols = cols;
	ctx->keys = rows * cols;

	if (ctx->keys > SUPPORT_KEYS_MAX) {
		dev_err(dev, "cannot support %u keys, the limitation is %u\n",
			ctx->keys, SUPPORT_KEYS_MAX);
		err = -EINVAL;
		goto free_input;
	}

	err = matrix_keypad_build_keymap(NULL, NULL, rows, cols, NULL, input);
	if (err) {
		dev_err(dev, "build keymap failed\n");
		goto free_input;
	}

	err = input_register_device(input);
	if (err) {
		dev_err(dev, "register input device failed\n");
		goto free_input;
	}

	dev_dbg(dev, "input device created\n");
	return 0;

free_input:
	input_free_device(input);
free_ctx:
	vfree(ctx);
	return err;
}

static int keypad_remove(struct i2c_client *i2c)
{
	struct ctx *ctx = i2c_get_clientdata(i2c);

	input_unregister_device(ctx->input);
	return 0;
}

static const struct of_device_id keypad_dt_table[] = {
	{ .compatible = "bitmain,bm16xx-virtual-keypad" },
	{},
};

static const struct i2c_device_id keypad_id_table[] = {
	{ "bm-virt-key", 0 },
	{},
};

static struct i2c_driver keypad_drv = {
	.driver = {
		.name = "bm16xx-virtual-keypad",
		.of_match_table = keypad_dt_table,
	},
	.probe = keypad_probe,
	.remove = keypad_remove,
	.id_table = keypad_id_table,
};

module_i2c_driver(keypad_drv);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chao Wei <chao.wei@bitmain.com>");
MODULE_DESCRIPTION("BITMAIN Virtual Keypad Device Driver");
MODULE_VERSION("0.1");
