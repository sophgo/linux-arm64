#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/device.h>
#include <linux/of.h>

#include "../pinctrl-utils.h"
#include "pinctrl-bitmain.h"

#define DRV_PINCTRL_NAME "bm_pinctrl"
#define DRV_PINMUX_NAME "bm_pinmux"

#define FUNCTION(fname, gname, fmode) \
	{ \
		.name = #fname, \
		.groups = gname##_group, \
		.num_groups = ARRAY_SIZE(gname##_group), \
		.mode = fmode, \
	}

#define PIN_GROUP(gname) \
	{ \
		.name = #gname "_grp", \
		.pins = gname##_pins, \
		.num_pins = ARRAY_SIZE(gname##_pins), \
	}

static const struct pinctrl_pin_desc bm1684_pins[] = {
	PINCTRL_PIN(0,   "MIO0"),
	PINCTRL_PIN(1,   "MIO1"),
	PINCTRL_PIN(2,   "MIO2"),
	PINCTRL_PIN(3,   "MIO3"),
	PINCTRL_PIN(4,   "MIO4"),
	PINCTRL_PIN(5,   "MIO5"),
	PINCTRL_PIN(6,   "MIO6"),
	PINCTRL_PIN(7,   "MIO7"),
	PINCTRL_PIN(8,   "MIO8"),
	PINCTRL_PIN(9,   "MIO9"),
	PINCTRL_PIN(10,   "MIO10"),
	PINCTRL_PIN(11,   "MIO11"),
	PINCTRL_PIN(12,   "MIO12"),
	PINCTRL_PIN(13,   "MIO13"),
	PINCTRL_PIN(14,   "MIO14"),
	PINCTRL_PIN(15,   "MIO15"),
	PINCTRL_PIN(16,   "MIO16"),
	PINCTRL_PIN(17,   "MIO17"),
	PINCTRL_PIN(18,   "MIO18"),
	PINCTRL_PIN(19,   "MIO19"),
	PINCTRL_PIN(20,   "MIO20"),
	PINCTRL_PIN(21,   "MIO21"),
	PINCTRL_PIN(22,   "MIO22"),
	PINCTRL_PIN(23,   "MIO23"),
	PINCTRL_PIN(24,   "MIO24"),
	PINCTRL_PIN(25,   "MIO25"),
	PINCTRL_PIN(26,   "MIO26"),
	PINCTRL_PIN(27,   "MIO27"),
	PINCTRL_PIN(28,   "MIO28"),
	PINCTRL_PIN(29,   "MIO29"),
	PINCTRL_PIN(30,   "MIO30"),
	PINCTRL_PIN(31,   "MIO31"),
	PINCTRL_PIN(32,   "MIO32"),
	PINCTRL_PIN(33,   "MIO33"),
	PINCTRL_PIN(34,   "MIO34"),
	PINCTRL_PIN(35,   "MIO35"),
	PINCTRL_PIN(36,   "MIO36"),
	PINCTRL_PIN(37,   "MIO37"),
	PINCTRL_PIN(38,   "MIO38"),
	PINCTRL_PIN(39,   "MIO39"),
	PINCTRL_PIN(40,   "MIO40"),
	PINCTRL_PIN(41,   "MIO41"),
	PINCTRL_PIN(42,   "MIO42"),
	PINCTRL_PIN(43,   "MIO43"),
	PINCTRL_PIN(44,   "MIO44"),
	PINCTRL_PIN(45,   "MIO45"),
	PINCTRL_PIN(46,   "MIO46"),
	PINCTRL_PIN(47,   "MIO47"),
	PINCTRL_PIN(48,   "MIO48"),
	PINCTRL_PIN(49,   "MIO49"),
	PINCTRL_PIN(50,   "MIO50"),
	PINCTRL_PIN(51,   "MIO51"),
	PINCTRL_PIN(52,   "MIO52"),
	PINCTRL_PIN(53,   "MIO53"),
	PINCTRL_PIN(54,   "MIO54"),
	PINCTRL_PIN(55,   "MIO55"),
	PINCTRL_PIN(56,   "MIO56"),
	PINCTRL_PIN(57,   "MIO57"),
	PINCTRL_PIN(58,   "MIO58"),
	PINCTRL_PIN(59,   "MIO59"),
	PINCTRL_PIN(60,   "MIO60"),
	PINCTRL_PIN(61,   "MIO61"),
	PINCTRL_PIN(62,   "MIO62"),
	PINCTRL_PIN(63,   "MIO63"),
	PINCTRL_PIN(64,   "MIO64"),
	PINCTRL_PIN(65,   "MIO65"),
	PINCTRL_PIN(66,   "MIO66"),
	PINCTRL_PIN(67,   "MIO67"),
	PINCTRL_PIN(68,   "MIO68"),
	PINCTRL_PIN(69,   "MIO69"),
	PINCTRL_PIN(70,   "MIO70"),
	PINCTRL_PIN(71,   "MIO71"),
	PINCTRL_PIN(72,   "MIO72"),
	PINCTRL_PIN(73,   "MIO73"),
	PINCTRL_PIN(74,   "MIO74"),
	PINCTRL_PIN(75,   "MIO75"),
	PINCTRL_PIN(76,   "MIO76"),
	PINCTRL_PIN(77,   "MIO77"),
	PINCTRL_PIN(78,   "MIO78"),
	PINCTRL_PIN(79,   "MIO79"),
	PINCTRL_PIN(80,   "MIO80"),
	PINCTRL_PIN(81,   "MIO81"),
	PINCTRL_PIN(82,   "MIO82"),
	PINCTRL_PIN(83,   "MIO83"),
	PINCTRL_PIN(84,   "MIO84"),
	PINCTRL_PIN(85,   "MIO85"),
	PINCTRL_PIN(86,   "MIO86"),
	PINCTRL_PIN(87,   "MIO87"),
	PINCTRL_PIN(88,   "MIO88"),
	PINCTRL_PIN(89,   "MIO89"),
	PINCTRL_PIN(90,   "MIO90"),
	PINCTRL_PIN(91,   "MIO91"),
	PINCTRL_PIN(92,   "MIO92"),
	PINCTRL_PIN(93,   "MIO93"),
	PINCTRL_PIN(94,   "MIO94"),
	PINCTRL_PIN(95,   "MIO95"),
	PINCTRL_PIN(96,   "MIO96"),
	PINCTRL_PIN(97,   "MIO97"),
	PINCTRL_PIN(98,   "MIO98"),
	PINCTRL_PIN(99,   "MIO99"),
};

static const unsigned int pcie_pins[] = {0, 1, 2, 3, 4, 5};
static const unsigned int spif_pins[] = {6, 7, 8, 9, 10, 11, 12};
static const unsigned int emmc_pins[] = {13, 14, 15, 16};
static const unsigned int sdio_pins[] = {17, 18, 19, 20};
static const unsigned int eth0_pins[] = {21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36};
static const unsigned int eth1_pins[] = {37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 52, 52};
static const unsigned int pwm0_pins[] = {53};
static const unsigned int pwm1_pins[] = {54};
static const unsigned int fan0_pins[] = {55};
static const unsigned int fan1_pins[] = {56};
static const unsigned int i2c0_pins[] = {57, 58};
static const unsigned int i2c1_pins[] = {59, 60};
static const unsigned int i2c2_pins[] = {61, 62};
static const unsigned int uart0_pins[] = {63, 64, 82, 83};
static const unsigned int uart1_pins[] = {65, 66, 84, 85};
static const unsigned int uart2_pins[] = {67, 68};
// GPIO0 is pin 69
static const unsigned int jtag0_pins[] = {86, 87, 88, 89, 90, 91}; // GPIO 17~22
static const unsigned int jtag1_pins[] = {92, 93, 94, 95, 96, 97}; // GPIO 23~28
static const unsigned int dbgi2c_pins[] = {98, 99};

static const char * const pcie_group[] = {"pcie_grp"};
static const char * const spif_group[] = {"spif_grp"};
static const char * const emmc_group[] = {"emmc_grp"};
static const char * const sdio_group[] = {"sdio_grp"};
static const char * const eth0_group[] = {"eth0_grp"};
static const char * const eth1_group[] = {"eth1_grp"};
static const char * const pwm0_group[] = {"pwm0_grp"};
static const char * const pwm1_group[] = {"pwm1_grp"};
static const char * const fan0_group[] = {"fan0_grp"};
static const char * const fan1_group[] = {"fan1_grp"};
static const char * const i2c0_group[] = {"i2c0_grp"};
static const char * const i2c1_group[] = {"i2c1_grp"};
static const char * const i2c2_group[] = {"i2c2_grp"};
static const char * const uart0_group[] = {"uart0_grp"};
static const char * const uart1_group[] = {"uart1_grp"};
static const char * const uart2_group[] = {"uart2_grp"};
static const char * const jtag0_group[] = {"jtag0_grp"};
static const char * const jtag1_group[] = {"jtag1_grp"};
static const char * const dbgi2c_group[] = {"dbgi2c_grp"};

static struct bm_group bm1684_groups[] = {
	PIN_GROUP(pcie),
	PIN_GROUP(spif),
	PIN_GROUP(emmc),
	PIN_GROUP(sdio),
	PIN_GROUP(eth0),
	PIN_GROUP(eth1),
	PIN_GROUP(pwm0),
	PIN_GROUP(pwm1),
	PIN_GROUP(fan0),
	PIN_GROUP(fan1),
	PIN_GROUP(i2c0),
	PIN_GROUP(i2c1),
	PIN_GROUP(i2c2),
	PIN_GROUP(uart0),
	PIN_GROUP(uart1),
	PIN_GROUP(uart2),
	PIN_GROUP(jtag0),
	PIN_GROUP(jtag1),
	PIN_GROUP(dbgi2c),
};

static const struct bm_pmx_func bm1684_funcs[] = {
	FUNCTION(pcie_a, pcie, FUNC_MODE0),
	FUNCTION(pcie_r, pcie, FUNC_MODE1),
	FUNCTION(spif_a, spif, FUNC_MODE0),
	FUNCTION(spif_r, spif, FUNC_MODE1),
	FUNCTION(emmc_a, emmc, FUNC_MODE0),
	FUNCTION(emmc_r, emmc, FUNC_MODE1),
	FUNCTION(sdio_a, sdio, FUNC_MODE0),
	FUNCTION(sdio_r, sdio, FUNC_MODE1),
	FUNCTION(eth0_a, eth0, FUNC_MODE1),
	FUNCTION(eth0_r, eth0, FUNC_MODE0),
	FUNCTION(eth1_a, eth1, FUNC_MODE1),
	FUNCTION(eth1_r, eth1, FUNC_MODE0),
	FUNCTION(pwm0_a, pwm0, FUNC_MODE0),
	FUNCTION(pwm0_r, pwm0, FUNC_MODE1),
	FUNCTION(pwm1_a, pwm1, FUNC_MODE0),
	FUNCTION(pwm1_r, pwm1, FUNC_MODE1),
	FUNCTION(fan0_a, fan0, FUNC_MODE1),
	FUNCTION(fan0_r, fan0, FUNC_MODE0),
	FUNCTION(fan1_a, fan1, FUNC_MODE1),
	FUNCTION(fan1_r, fan1, FUNC_MODE0),
	FUNCTION(i2c0_a, i2c0, FUNC_MODE0),
	FUNCTION(i2c0_r, i2c0, FUNC_MODE1),
	FUNCTION(i2c1_a, i2c1, FUNC_MODE0),
	FUNCTION(i2c1_r, i2c1, FUNC_MODE1),
	FUNCTION(i2c2_a, i2c2, FUNC_MODE1),
	FUNCTION(i2c2_r, i2c2, FUNC_MODE0),
	FUNCTION(uart0_a, uart0, FUNC_MODE0),
	FUNCTION(uart0_r, uart0, FUNC_MODE1),
	FUNCTION(uart1_a, uart1, FUNC_MODE0),
	FUNCTION(uart1_r, uart1, FUNC_MODE1),
	FUNCTION(uart2_a, uart2, FUNC_MODE1),
	FUNCTION(uart2_r, uart2, FUNC_MODE0),
	FUNCTION(jtag0_a, jtag0, FUNC_MODE0),
	FUNCTION(jtag0_r, jtag0, FUNC_MODE1),
	FUNCTION(jtag1_a, jtag1, FUNC_MODE1),
	FUNCTION(jtag1_r, jtag1, FUNC_MODE1),
	FUNCTION(dbgi2c_a, dbgi2c, FUNC_MODE0),
	FUNCTION(dbgi2c_r, dbgi2c, FUNC_MODE1),
};

static struct device_attribute pcie_attr =	__ATTR(pcie, 0664, pinmux_show, pinmux_store);
static struct device_attribute spif_attr =	__ATTR(spif, 0664, pinmux_show, pinmux_store);
static struct device_attribute emmc_attr =	__ATTR(emmc, 0664, pinmux_show, pinmux_store);
static struct device_attribute sdio_attr =	__ATTR(sdio, 0664, pinmux_show, pinmux_store);
static struct device_attribute eth0_attr =	__ATTR(eth0, 0664, pinmux_show, pinmux_store);
static struct device_attribute eth1_attr =	__ATTR(eth1, 0664, pinmux_show, pinmux_store);
static struct device_attribute pwm0_attr =	__ATTR(pwm0, 0664, pinmux_show, pinmux_store);
static struct device_attribute pwm1_attr =	__ATTR(pwm1, 0664, pinmux_show, pinmux_store);
static struct device_attribute fan0_attr =	__ATTR(fan0, 0664, pinmux_show, pinmux_store);
static struct device_attribute fan1_attr =	__ATTR(fan1, 0664, pinmux_show, pinmux_store);
static struct device_attribute i2c0_attr =	__ATTR(i2c0, 0664, pinmux_show, pinmux_store);
static struct device_attribute i2c1_attr =	__ATTR(i2c1, 0664, pinmux_show, pinmux_store);
static struct device_attribute i2c2_attr =	__ATTR(i2c2, 0664, pinmux_show, pinmux_store);
static struct device_attribute uart0_attr =	__ATTR(uart0, 0664, pinmux_show, pinmux_store);
static struct device_attribute uart1_attr =	__ATTR(uart1, 0664, pinmux_show, pinmux_store);
static struct device_attribute uart2_attr =	__ATTR(uart2, 0664, pinmux_show, pinmux_store);
static struct device_attribute jtag0_attr =	__ATTR(jtag0, 0664, pinmux_show, pinmux_store);
static struct device_attribute jtag1_attr =	__ATTR(jtag1, 0664, pinmux_show, pinmux_store);
static struct device_attribute dbgi2c_attr =	__ATTR(dbgi2c, 0664, pinmux_show, pinmux_store);

static struct attribute *pinmux_attrs[] = {
	&pcie_attr.attr,
	&spif_attr.attr,
	&emmc_attr.attr,
	&sdio_attr.attr,
	&eth0_attr.attr,
	&eth1_attr.attr,
	&pwm0_attr.attr,
	&pwm1_attr.attr,
	&fan0_attr.attr,
	&fan1_attr.attr,
	&i2c0_attr.attr,
	&i2c1_attr.attr,
	&i2c2_attr.attr,
	&uart0_attr.attr,
	&uart1_attr.attr,
	&uart2_attr.attr,
	&jtag0_attr.attr,
	&jtag1_attr.attr,
	&dbgi2c_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(pinmux);

static struct class  pinmux_class = {
	.name = "pinmux",
	.owner = THIS_MODULE,
	.dev_groups = pinmux_groups,
};

static struct bm_soc_pinctrl_data bm1684_pinctrl_data = {
	.pins = bm1684_pins,
	.npins = ARRAY_SIZE(bm1684_pins),
	.groups = bm1684_groups,
	.groups_count = ARRAY_SIZE(bm1684_groups),
	.functions = bm1684_funcs,
	.functions_count = ARRAY_SIZE(bm1684_funcs),
	.p_class = &pinmux_class,
};

static const struct of_device_id bm_pinctrl_of_table[] = {
	{
		.compatible = "bitmain,pinctrl-bm1684",
		.data = &bm1684_pinctrl_data,
	},
	{}
};

static int bm_pinctrl_probe(struct platform_device *pdev)
{
	return bitmain_pinctrl_probe(pdev);
}

static struct platform_driver bm_pinctrl_driver = {
	.probe = bm_pinctrl_probe,
	.driver = {
		.name = DRV_PINCTRL_NAME,
		.of_match_table = of_match_ptr(bm_pinctrl_of_table),
	},
};

static int __init bm_pinctrl_init(void)
{
	return platform_driver_register(&bm_pinctrl_driver);
}
postcore_initcall(bm_pinctrl_init);

