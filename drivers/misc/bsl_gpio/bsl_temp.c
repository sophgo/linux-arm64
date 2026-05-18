#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/sysfs.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>


extern int mcu_get_temperature_ext(struct i2c_client *i2c, int ch, long *val);

struct bsl_temp_data {
	struct i2c_client *client;
	u32 channel_config[4];
	struct hwmon_channel_info temp_info;
	const struct hwmon_channel_info *info[3];
	struct hwmon_chip_info chip;
	struct mutex update_lock;

	struct device_node *mcu_np;
	struct i2c_client *mcu_i2c;
};


static int bsl_temp_temp_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct bsl_temp_data *data = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&data->update_lock);
	mutex_unlock(&data->update_lock);

	switch (attr) {
	case hwmon_temp_input:
		if (data->mcu_i2c) {
			ret = mcu_get_temperature_ext(data->mcu_i2c, channel, val);
			if (ret < 0)
				return ret;

			if (val)
				*val = *val * 1000;
		} else {
			ret = -ENODEV;
		}
		break;
	case hwmon_temp_min_alarm:
	case hwmon_temp_max_alarm:
	case hwmon_temp_crit_alarm:
	case hwmon_temp_emergency_alarm:
	case hwmon_temp_fault:
	case hwmon_temp_min:
	case hwmon_temp_max:
	case hwmon_temp_crit:
	case hwmon_temp_crit_hyst:
	case hwmon_temp_emergency:
	case hwmon_temp_emergency_hyst:
	case hwmon_temp_offset:
		dev_info(dev, "## %s attr=%d ch=%d\n", __func__, attr, channel);
		*val = 0;
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int bsl_temp_temp_write(struct device *dev, u32 attr, int channel, long val)
{
	struct bsl_temp_data *data = dev_get_drvdata(dev);
	int err;

	mutex_lock(&data->update_lock);


	switch (attr) {
	case hwmon_temp_min:
	case hwmon_temp_max:
	case hwmon_temp_crit:
	case hwmon_temp_crit_hyst:
	case hwmon_temp_emergency:
	case hwmon_temp_offset:
		err = 0;
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}
error:
	mutex_unlock(&data->update_lock);

	return err;
}

static umode_t bsl_temp_temp_is_visible(const void *data, u32 attr, int channel)
{
	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_min_alarm:
	case hwmon_temp_max_alarm:
	case hwmon_temp_crit_alarm:
	case hwmon_temp_emergency_alarm:
	case hwmon_temp_emergency_hyst:
	case hwmon_temp_fault:
		return 0444;
	case hwmon_temp_min:
	case hwmon_temp_max:
	case hwmon_temp_crit:
	case hwmon_temp_emergency:
	case hwmon_temp_offset:
		return 0644;
	case hwmon_temp_crit_hyst:
		if (channel == 0)
			return 0644;
		return 0444;
	default:
		return 0;
	}
}

static int bsl_temp_chip_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct bsl_temp_data *data = dev_get_drvdata(dev);

	dev_info(dev, "## %s attr=%d ch=%d\n", __func__, attr, channel);

	mutex_lock(&data->update_lock);
	mutex_unlock(&data->update_lock);

	switch (attr) {
	case hwmon_chip_update_interval:
		*val = 0;
		break;
	case hwmon_chip_alarms:
		*val = 0;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int bsl_temp_chip_write(struct device *dev, u32 attr, int channel, long val)
{
	struct bsl_temp_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int err;

	mutex_lock(&data->update_lock);

	switch (attr) {
	case hwmon_chip_update_interval:
		err = 0;
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}
error:
	mutex_unlock(&data->update_lock);

	return err;
}

static umode_t bsl_temp_chip_is_visible(const void *data, u32 attr, int channel)
{
	switch (attr) {
	case hwmon_chip_update_interval:
		return 0644;
	case hwmon_chip_alarms:
		return 0444;
	default:
		return 0;
	}
}

static umode_t bsl_temp_is_visible(const void *data, enum hwmon_sensor_types type,
				   u32 attr, int channel)
{
	switch (type) {
	case hwmon_chip:
		return bsl_temp_chip_is_visible(data, attr, channel);
	case hwmon_temp:
		return bsl_temp_temp_is_visible(data, attr, channel);
	default:
		return 0;
	}
}

static int bsl_temp_read(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_chip:
		return bsl_temp_chip_read(dev, attr, channel, val);
	case hwmon_temp:
		return bsl_temp_temp_read(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int bsl_temp_write(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_chip:
		return bsl_temp_chip_write(dev, attr, channel, val);
	case hwmon_temp:
		return bsl_temp_temp_write(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}
static const struct hwmon_ops bsl_temp_ops = {
	.is_visible = bsl_temp_is_visible,
	.read = bsl_temp_read,
	.write = bsl_temp_write,
};



static int bsl_temp_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct i2c_adapter *adapter = client->adapter;
	struct hwmon_channel_info *info;
	struct regulator *regulator;
	struct device *hwmon_dev;
	struct bsl_temp_data *data;
	int err;


	data = devm_kzalloc(dev, sizeof(struct bsl_temp_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	data->chip.ops = &bsl_temp_ops;
	data->chip.info = data->info;

	data->info[0] = HWMON_CHANNEL_INFO(chip,
		HWMON_C_REGISTER_TZ | HWMON_C_UPDATE_INTERVAL | HWMON_C_ALARMS);
	data->info[1] = &data->temp_info;

	info = &data->temp_info;
	info->type = hwmon_temp;
	info->config = data->channel_config;

	data->channel_config[0] = HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
		HWMON_T_MIN_ALARM | HWMON_T_MAX_ALARM;
	data->channel_config[1] = HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
		HWMON_T_MIN_ALARM | HWMON_T_MAX_ALARM | HWMON_T_FAULT;

	data->channel_config[0] |= HWMON_T_CRIT | HWMON_T_CRIT_ALARM | HWMON_T_CRIT_HYST;
	data->channel_config[1] |= HWMON_T_CRIT | HWMON_T_CRIT_ALARM | HWMON_T_CRIT_HYST;

	data->channel_config[1] |= HWMON_T_OFFSET;


	/* Initialize the BSL TEMP chip */

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name,
							 data, &data->chip,
							 NULL);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	if (client->dev.of_node) {
		u32 phandle;

		if (of_property_read_u32(client->dev.of_node, "mcu", &phandle)) {
			dev_err(dev, "Failed to read phandle from 'mcu'\n");
			return -EINVAL;
		}
		data->mcu_np = of_find_node_by_phandle(phandle);
		if (!data->mcu_np) {
			dev_info(dev, "Failed to find mcu node\n");
			return -ENODEV;
		}
		data->mcu_i2c = of_find_i2c_device_by_node(data->mcu_np);
		if (!data->mcu_i2c) {
			dev_info(dev, "Failed to find mcu i2c device\n");
			return -ENODEV;
		}
	}

	return 0;
}

static const struct i2c_device_id bsl_temp_id[] = {
	{ "bsl_temp_sensor", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bsl_temp_id);

static const struct of_device_id __maybe_unused bsl_temp_of_match[] = {
	{
		.compatible = "bsl,tempsensor",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, bsl_temp_of_match);

static struct i2c_driver bsl_temp_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "bsl_temp_sensor",
		.of_match_table = of_match_ptr(bsl_temp_of_match),
	},
	.probe		= bsl_temp_probe,
	.id_table	= bsl_temp_id,
};

module_i2c_driver(bsl_temp_driver);

MODULE_AUTHOR("ken@vm");
MODULE_DESCRIPTION("bsl temp sensor driver");
MODULE_LICENSE("GPL");
