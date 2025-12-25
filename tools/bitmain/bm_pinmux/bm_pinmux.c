#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <stdint.h>

#define STS_CTRL_PHY_BASE 0x50010000
#define SYS_CTRL_SIZE 0x800
// Top misc: 0x50010000, Pinmux: 0x50010400
#define GEN_SYS_REG(offset) (offset)
#define GEN_PINMUX_REG(offset) (offset + 0x400)

#define ARRAY_LEN(name) (sizeof(name) / sizeof(name[0]))
#define BIT(nr) ((1) << (nr))

#define PULL_UP_ENABLE_MASK BIT(2)
#define PULL_DOWN_ENABLE_MASK BIT(3)
#define PINMUX_SELECTOR_MASK (BIT(4) | BIT(5))
#define DRIVE_SELECTOR_MASK (BIT(6) | BIT(7) | BIT(8) | BIT(9))
#define SCHMIT_TRIGGER_ENABLE_MASK BIT(10)
#define PAD_OEX_ENABLE BIT(11)

#define PINMUX_SELECTOR_SHIFT 4

#define PINMUX_FUNC_NUM 4

typedef uint32_t u32;
typedef uint16_t u16;

void *sys_ctrl_base;

struct bm_pinmux
{
	char *signal_name;
	u32 reg;
	u32 offset;
	char *func_name[PINMUX_FUNC_NUM];
};

struct bm_pinmux pinlist_map[] = {
	{"SPIF_CLK_SEL1", 0xC, 0, "SPIF_CLK_SEL1", "GPIO32", NULL, NULL},
	{"SPIF_WP_X", 0xC, 0x10, "SPIF_WP_X", "GPIO33", NULL, NULL},
	{"SPIF_HOLD_X", 0x10, 0, "SPIF_HOLD_X", "GPIO34", NULL, NULL},
	{"SPIF_SDI", 0x10, 0x10, "SPIF_SDI", "GPIO35", NULL, NULL},
	{"SPIF_CS_X", 0x14, 0, "SPIF_CS_X", "GPIO36", NULL, NULL},
	{"SPIF_SCK", 0x14, 0x10, "SPIF_SCK", "GPIO37", NULL, NULL},
	{"SPIF_SDO", 0x18, 0, "SPIF_SDO", "GPIO38", NULL, NULL},
	{"SDIO_CD_X", 0x20, 0x10, "SDIO_CD_X", "GPIO39", NULL, NULL},
	{"SDIO_WP", 0x24, 0, "SDIO_WP", "GPIO40", NULL, NULL},
	{"SDIO_RST_X", 0x24, 0x10, "SDIO_RST_X", "GPIO41", NULL, NULL},
	{"SDIO_PWR_EN", 0x28, 0, "SDIO_PWR_EN", "GPIO42", NULL, NULL},
	{"RGMII0_TXD0", 0x28, 0x10, "GPIO43", "RGMII0_TXD0", NULL, NULL},
	{"RGMII0_TXD1", 0x2C, 0, "GPIO44", "RGMII0_TXD1", NULL, NULL},
	{"RGMII0_TXD2", 0x2C, 0x10, "GPIO45", "RGMII0_TXD2", NULL, NULL},
	{"RGMII0_TXD3", 0x30, 0, "GPIO46", "RGMII0_TXD3", NULL, NULL},
	{"RGMII0_TXCTRL", 0x30, 0x10, "GPIO47", "RGMII0_TXCTRL", NULL, NULL},
	{"RGMII0_RXD0", 0x34, 0, "GPIO48", "RGMII0_RXD0", NULL, NULL},
	{"RGMII0_RXD1", 0x34, 0x10, "GPIO49", "RGMII0_RXD1", NULL, NULL},
	{"RGMII0_RXD2", 0x38, 0, "GPIO50", "RGMII0_RXD2", NULL, NULL},
	{"RGMII0_RXD3", 0x38, 0x10, "GPIO51", "RGMII0_RXD3", NULL, NULL},
	{"RGMII0_RXCTRL", 0x3C, 0, "GPIO52", "RGMII0_RXCTRL", NULL, NULL},
	{"RGMII0_TXC", 0x3C, 0x10, "GPIO53", "RGMII0_TXC", NULL, NULL},
	{"RGMII0_RXC", 0x40, 0, "GPIO54", "RGMII0_RXC", NULL, NULL},
	{"RGMII0_REFCLK", 0x40, 0x10, "GPIO55", "RGMII0_REFCLK", NULL, NULL},
	{"RGMII0_IRQ", 0x44, 0, "GPIO56", "RGMII0_IRQ", NULL, NULL},
	{"RGMII0_MDC", 0x44, 0x10, "GPIO57", "RGMII0_MDC", NULL, NULL},
	{"RGMII0_MDIO", 0x48, 0, "GPIO58", "RGMII0_MDIO", NULL, NULL},
	{"RGMII1_TXD0", 0x48, 0x10, "GPIO59", "RGMII1_TXD0", NULL, NULL},
	{"RGMII1_TXD1", 0x4C, 0, "GPIO60", "RGMII1_TXD1", NULL, NULL},
	{"RGMII1_TXD2", 0x4C, 0x10, "GPIO61", "RGMII1_TXD2", NULL, NULL},
	{"RGMII1_TXD3", 0x50, 0, "GPIO62", "RGMII1_TXD3", NULL, NULL},
	{"RGMII1_TXCTRL", 0x50, 0x10, "GPIO63", "RGMII1_TXCTRL", NULL, NULL},
	{"RGMII1_RXD0", 0x54, 0, "GPIO64", "RGMII1_RXD0", NULL, NULL},
	{"RGMII1_RXD1", 0x54, 0x10, "GPIO65", "RGMII1_RXD1", NULL, NULL},
	{"RGMII1_RXD2", 0x58, 0, "GPIO66", "RGMII1_RXD2", NULL, NULL},
	{"RGMII1_RXD3", 0x58, 0x10, "GPIO67", "RGMII1_RXD3", NULL, NULL},
	{"RGMII1_RXCTRL", 0x5C, 0, "GPIO68", "RGMII1_RXCTRL", NULL, NULL},
	{"RGMII1_TXC", 0x5C, 0x10, "GPIO69", "RGMII1_TXC", NULL, NULL},
	{"RGMII1_RXC", 0x60, 0, "GPIO70", "RGMII1_RXC", NULL, NULL},
	{"RGMII1_REFCLK", 0x60, 0x10, "GPIO71", "RGMII1_REFCLK", NULL, NULL},
	{"RGMII1_IRQ", 0x64, 0, "GPIO72", "RGMII1_IRQ", NULL, NULL},
	{"RGMII1_MDC", 0x64, 0x10, "GPIO73", "RGMII1_MDC", NULL, NULL},
	{"RGMII1_MDIO", 0x68, 0, "GPIO74", "RGMII1_MDIO", NULL, NULL},
	{"PWM0", 0x68, 0x10, "PWM0", "GPIO75", NULL, NULL},
	{"PWM1", 0x6C, 0, "PWM1", "GPIO76", NULL, NULL},
	{"FAN0", 0x6C, 0x10, "GPIO77", "FAN0", NULL, NULL},
	{"FAN1", 0x70, 0, "GPIO78", "FAN1", NULL, NULL},
	{"IIC0_SDA", 0x70, 0x10, "IIC0_SDA", "GPIO79", NULL, NULL},
	{"IIC0_SCL", 0x74, 0, "IIC0_SCL", "GPIO80", NULL, NULL},
	{"IIC1_SDA", 0x74, 0x10, "IIC1_SDA", "GPIO81", NULL, NULL},
	{"IIC1_SCL", 0x78, 0, "IIC1_SCL", "GPIO82", NULL, NULL},
	{"IIC2_SDA", 0x78, 0x10, "GPIO83", "IIC2_SDA", NULL, NULL},
	{"IIC2_SCL", 0x7C, 0, "GPIO84", "IIC2_SCL", NULL, NULL},
	{"UART0_TX", 0x7C, 0x10, "UART0_TX", "GPIO85", NULL, NULL},
	{"UART0_RX", 0x80, 0, "UART0_RX", "GPIO86", NULL, NULL},
	{"UART1_TX", 0x80, 0x10, "UART1_TX", "GPIO87", NULL, NULL},
	{"UART1_RX", 0x84, 0, "UART1_RX", "GPIO88", NULL, NULL},
	{"UART2_TX", 0x84, 0x10, "GPIO89", "UART2_TX", NULL, NULL},
	{"UART2_RX", 0x88, 0, "GPIO90", "UART2_RX", NULL, NULL},
	{"GPIO1", 0x8C, 0, "TPUMEM_PWR_GOOD", "GPIO1", NULL, NULL},
	{"GPIO2", 0x8C, 0x10, "PCIE_PWR_GOOD", "GPIO2", NULL, NULL},
	{"GPIO3", 0x90, 0, "TPU_PWR_GOOD", "GPIO3", NULL, NULL},
	{"GPIO4", 0x90, 0x10, "PLL_LOCK", "VD0_WARE0_TX", NULL, "GPIO4"},
	{"GPIO5", 0x94, 0, "GPIO5", "VD0_WARE0_RX", NULL, NULL},
	{"GPIO6", 0x94, 0x10, "GPIO6", "VD0_WAVE1_TX", NULL, NULL},
	{"GPIO7", 0x98, 0, "GPIO7", "VD0_WAVE1_TX", NULL, NULL},
	{"GPIO8", 0x98, 0x10, "GPIO8", NULL, NULL, NULL},
	{"GPIO9", 0x9C, 0, "GPIO9", "VD1_WARE0_TX", NULL, NULL},
	{"GPIO10", 0x9C, 0x10, "GPIO10", "VD1_WARE0_RX", NULL, NULL},
	{"GPIO11", 0xA0, 0, "GPIO11", "VD1_WARE1_TX", NULL, NULL},
	{"GPIO12", 0xA0, 0x10, "GPIO12", "VD1_WARE1_RX", NULL, NULL},
	{"GPIO13", 0xA4, 0, "GPIO13", "VDE_WARE_TX", NULL, "UART0_RTS"},
	{"GPIO14", 0xA4, 0x10, "GPIO14", "VDE_WARE_RX", NULL, "UART0_CTS"},
	{"GPIO15", 0xA8, 0, "JTAG1_2_SEL", "UART1_RTS", NULL, "GPIO15"},
	{"GPIO16", 0xA8, 0x10, "GPIO16", "UART1_CTS", NULL, NULL},
	{"GPIO17", 0xAC, 0, "JTAG0_TDO", "GPIO17", NULL, NULL},
	{"GPIO18", 0xAC, 0x10, "JTAG0_TCK", "GPIO18", NULL, NULL},
	{"GPIO19", 0xB0, 0, "JTAG0_TDI", "GPIO19", NULL, NULL},
	{"GPIO20", 0xB0, 0x10, "JTAG0_TMS", "GPIO20", NULL, NULL},
	{"GPIO21", 0xB4, 0, "JTAG0_TRST_X", "GPIO21", NULL, NULL},
	{"GPIO22", 0xB4, 0x10, "JTAG0_SRST_X", "GPIO22", NULL, NULL},
	{"GPIO23", 0xB8, 0, "JTAG1_2_TDO", "GPIO23", NULL, NULL},
	{"GPIO24", 0xB8, 0x10, "JTAG1_2_TCK", "GPIO24", NULL, NULL},
	{"GPIO25", 0xBC, 0, "JTAG1_2_TDI", "GPIO25", NULL, NULL},
	{"GPIO26", 0xBC, 0x10, "JTAG1_2_TMS", "GPIO26", NULL, NULL},
	{"GPIO27", 0xC0, 0, "JTAG1_2_TRST_X", "GPIO27", NULL, NULL},
	{"GPIO28", 0xC0, 0x10, "JTAG1_2_SRST_X", "GPIO28", NULL, NULL},
	{"GPIO29", 0xC4, 0, "DBG_I2C_SCL", "GPIO29", NULL, NULL},
	{"GPIO30", 0xC4, 0x10, "DBG_I2C_SDA", "GPIO30", NULL, NULL},
	{"GPIO31", 0xC8, 0, "DBG_I2C_SDA_OE", "GPIO31", NULL, NULL},
};

int get_pin_index(const char *name)
{
	int i = 0;

	for (; i < ARRAY_LEN(pinlist_map); i++)
	{
		if (strcmp(name, pinlist_map[i].signal_name) == 0)
			return i;
	}

	return -1;
}

u32 sys_ctrl_read(u32 reg)
{
	return *(volatile u32 *)(sys_ctrl_base + reg);
}

void sys_ctrl_write(u32 reg, u32 val)
{
	*(volatile u32 *)(sys_ctrl_base + reg) = val;
}

u32 pinmux_read(u32 reg)
{
	return sys_ctrl_read(GEN_PINMUX_REG(reg));
}

void pinmux_write(u32 reg, u32 val)
{
	sys_ctrl_write(GEN_PINMUX_REG(reg), val);
}

int sys_ctrl_map(void)
{
	int sys_ctrl_fd;
	off_t offset;

	sys_ctrl_fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (sys_ctrl_fd == -1)
	{
		printf("Open /dev/mem/ failed!\n");
		return -1;
	}

	offset = (STS_CTRL_PHY_BASE & ~(sysconf(_SC_PAGE_SIZE) - 1));
	sys_ctrl_base = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED,
						 sys_ctrl_fd, offset);
	// printf("%s %d: base address is 0x%p\n", __func__, __LINE__,
		//    sys_ctrl_base);
	if (sys_ctrl_base == MAP_FAILED)
	{
		close(sys_ctrl_fd);
		printf("mmap sys ctrl base failed!\n");
		return -1;
	}

	close(sys_ctrl_fd);
	return 0;
}

void sys_ctrl_unmap(void)
{
	munmap(sys_ctrl_base, SYS_CTRL_SIZE);
}

/**
 * show_reg_info -Display the reg info for pinmux
 * @val: the value of the register
 *
 */
void show_reg_info(u16 val)
{
	printf("pull up enable: 0x%x\t pull down enable: 0x%x\n",
		   (PULL_UP_ENABLE_MASK & val) >> 2,
		   (PULL_DOWN_ENABLE_MASK & val) >> 3);
	printf("pinmux mux: 0x%x\tdrive selector: 0x%x\n",
		   (PINMUX_SELECTOR_MASK & val) >> 4,
		   (DRIVE_SELECTOR_MASK & val) >> 6);
	printf("schmit trigger enable: 0x%x\t pad_oex_enable: 0x%x\n",
		   (SCHMIT_TRIGGER_ENABLE_MASK & val) >> 10,
		   (PAD_OEX_ENABLE & val) >> 11);
}

int set_pinmux(const char *pin, const char *func)
{
	int i = 0;
	int index = 0;

	index = get_pin_index(pin);
	if (index == -1) {
		printf("Invalid pin: %s\n", pin);
		return -1;
	}

	for (i = 0; i < PINMUX_FUNC_NUM; ++i) {
		if (pinlist_map[index].func_name[i] && strcmp(func, pinlist_map[index].func_name[i]) == 0) 
			break;
	}
	if (i == PINMUX_FUNC_NUM) {
		printf("Invalid func %s \n", func);
		return -1;
	}

	u32 val = pinmux_read(pinlist_map[index].reg);
	val &= ~(PINMUX_SELECTOR_MASK << pinlist_map[index].offset);
	val |= (i << PINMUX_SELECTOR_SHIFT) << pinlist_map[index].offset;
	pinmux_write(pinlist_map[index].reg, val);
	return 0;
}

/**
 * @description: print the funcs of the index(th) pin of pinlist_map
 * @param {char} *pin
 * @param {uint32_t} value
 * @param {int} index
 * @param {uint32_t} group
 * @return {*}
 */
void print_fun(int index)
{
	int i = 0;
	u32 reg_val = 0;
	u32 func = 0;

	if (index < 0 || index >= ARRAY_LEN(pinlist_map))
	{
		printf("Error: Invalid pin index\n");
		return;
	}

	printf("%s function:\n", pinlist_map[index].signal_name);
	reg_val = pinmux_read(pinlist_map[index].reg) >> pinlist_map[index].offset;
	func = (reg_val & PINMUX_SELECTOR_MASK) >> PINMUX_SELECTOR_SHIFT;

	for (i = 0; i < PINMUX_FUNC_NUM; i++)
	{
		if (func == i)
		{
			if (pinlist_map[index].func_name[i])
				printf("[v] %s\n", pinlist_map[index].func_name[i]);
			else
				printf("[o] %s. Warnning: invalid func.\n", "NULL");
		}
		else if(pinlist_map[index].func_name[i])
			printf("[ ] %s\n", pinlist_map[index].func_name[i]);
	}
	printf("\n");
}

void print_usage(void)
{
	printf("bm_pinmux for 1684&1684x\n");
	printf("bm_pinmux -p          <== List all pins\n");
	printf("bm_pinmux -l          <== List all pins and its func\n");
	printf("bm_pinmux -r pin      <== Get func from pin\n");
	printf("bm_pinmux -w pin/func <== Set func to pin\n");
	exit(-1);
}

int main(int argc, char *argv[])
{
	int opt = 0;
	uint32_t i = 0;
	uint32_t pinmux_val;
	char pin[32];
	char func[32];
	int pull_down, driving;
	char power_domain[32];
	int vol;
	int ret;

	ret = sys_ctrl_map();
	if (ret != 0)
	{
		printf("map sys ctrl reg failed!\n");
		return -1;
	}

	if (argc == 1)
	{
		print_usage();
	}

	while ((opt = getopt(argc, argv, "hplr:w:c:d:")) != -1)
	{
		switch (opt)
		{
		case 'r':
			ret = get_pin_index(optarg);
			if (ret != -1)
				print_fun(ret);
			else
				printf("\nInvalid pin: %s\n", optarg);
			break;

		case 'w':
			if (sscanf(optarg, "%[^/]/%s", pin, func) != 2)
			{
				print_usage();
				break;
			}

			ret = set_pinmux(pin, func);
			if (ret == 0) {
				printf("Set %s to %s\n", pin, func);
				print_fun(get_pin_index(pin));
			}
			break;

		case 'p':
			printf("Pinlist:\n");
			for (i = 0; i < ARRAY_LEN(pinlist_map); i++)
				printf("%s\n", pinlist_map[i].signal_name);
			break;

		case 'l':
			for (i = 0; i < ARRAY_LEN(pinlist_map); i++)
				print_fun(i);
			break;

		case 'h':
			print_usage();
			break;

		case '?':
			print_usage();
			break;

		default:
			print_usage();
			break;
		}
	}

	return 0;
}
