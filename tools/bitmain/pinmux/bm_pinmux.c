#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <unistd.h>

#define STS_CTRL_PHY_BASE 0x50010000
#define SYS_CTRL_SIZE 0x800
// Top misc: 0x50010000, Pinmux: 0x50010400
#define GEN_SYS_REG(offset) (offset)
#define GEN_PINMUX_REG(offset) (offset + 0x400)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define BIT(nr)	((1) << (nr))

#define PULL_UP_ENABLE_MASK BIT(2)
#define PULL_DOWN_ENABLE_MASK BIT(3)
#define PINMUX_SELECTOR_MASK (BIT(4) | BIT(5))
#define DRIVE_SELECTOR_MASK (BIT(6) | BIT(7) | BIT(8) | BIT(9))
#define SCHMIT_TRIGGER_ENABLE_MASK BIT(10)
#define PAD_OEX_ENABLE BIT(11)

typedef unsigned int u32;
typedef unsigned short u16;

void *sys_ctrl_base;

struct bm_pinmux {
	u32 reg;
	u32 offset;
	char *pin_name[4];
};

struct bm_pinmux bm1686_pin[] = {
	{0xc, 0, "spif_clk_sel1", "GPIO32", NULL, NULL},
	{0xc, 0x10, "spif_wp_x", "GPIO33", NULL, NULL},
	{0x10, 0, "spif_hold_x", "GPIO34", NULL, NULL},
	{0x10, 0x10, "spif_sdi", "GPIO35", NULL, NULL},
	{0x14, 0, "spif_cs_x", "GPIO36", NULL, NULL},
	{0x14, 0x10, "spif_sck", "GPIO37", NULL, NULL},
	{0x18, 0, "spif_sdo", "GPIO38", NULL, NULL},
	{0x20, 0x10, "sdio_cd_x", "GPIO39", NULL, NULL},
	{0x24, 0, "sdio_wp", "GPIO40", NULL, NULL},
	{0x24, 0x10, "sdio_rst_x", "GPIO41", NULL, NULL},
	{0x28, 0, "sdio_pwr_en", "GPIO42", NULL, NULL},
	{0x28, 0x10, "GPIO43", "rgmii0_txd0", NULL, NULL},
	{0x2c, 0, "GPIO44", "rgmii0_txd1", NULL, NULL},
	{0x2c, 0x10, "GPIO45", "rgmii0_txd2", NULL, NULL},
	{0x30, 0, "GPIO46", "rgmii0_txd3", NULL, NULL},
	{0x30, 0x10, "GPIO47", "rgmii0_txctrl", NULL, NULL},
	{0x34, 0, "GPIO48", "rgmii0_rxd0", NULL, NULL},
	{0x34, 0x10, "GPIO49", "rgmii0_rxd1", NULL, NULL},
	{0x38, 0, "GPIO50", "rgmii0_rxd2", NULL, NULL},
	{0x38, 0x10, "GPIO51", "rgmii0_rxd3", NULL, NULL},
	{0x3c, 0, "GPIO52", "rgmii0_rxctrl", NULL, NULL},
	{0x3c, 0x10, "GPIO53", "rgmii0_txc", NULL, NULL},
	{0x40, 0, "GPIO54", "rgmii0_rxc", NULL, NULL},
	{0x40, 0x10, "GPIO55", "rgmii0_refclk", NULL, NULL},
	{0x44, 0, "GPIO56", "rgmii0_irq", NULL, NULL},
	{0x44, 0x10, "GPIO57", "rgmii0_mdc", NULL, NULL},
	{0x48, 0, "GPIO58", "rgmii0_mdio", NULL, NULL},
	{0x48, 0x10, "GPIO59", "rgmii1_txd0", NULL, NULL},
	{0x4c, 0, "GPIO60", "rgmii1_txd1", NULL, NULL},
	{0x4c, 0x10, "GPIO61", "rgmii1_txd2", NULL, NULL},
	{0x50, 0, "GPIO62", "rgmii1_txd3", NULL, NULL},
	{0x50, 0x10, "GPIO63", "rgmii1_txctrl", NULL, NULL},
	{0x54, 0, "GPIO64", "rgmii1_rxd0", NULL, NULL},
	{0x54, 0x10, "GPIO65", "rgmii1_rxd1", NULL, NULL},
	{0x58, 0, "GPIO66", "rgmii1_rxd2", NULL, NULL},
	{0x58, 0x10, "GPIO67", "rgmii1_rxd3", NULL, NULL},
	{0x5c, 0, "GPIO68", "rgmii1_rxctrl", NULL, NULL},
	{0x5c, 0x10, "GPIO69", "rgmii1_txc", NULL, NULL},
	{0x60, 0, "GPIO70", "rgmii1_rxc", NULL, NULL},
	{0x60, 0x10, "GPIO71", "rgmii1_refclk", NULL, NULL},
	{0x64, 0, "GPIO72", "rgmii1_irq", NULL, NULL},
	{0x64, 0x10, "GPIO73", "rgmii1_mdc", NULL, NULL},
	{0x68, 0, "GPIO74", "rgmii1_mdio", NULL, NULL},
	{0x68, 0x10, "pwm0", "GPIO75", NULL, NULL},
	{0x6c, 0, "pwm1", "GPIO76", NULL, NULL},
	{0x6c, 0x10, "GPIO77", "fan0", NULL, NULL},
	{0x70, 0, "GPIO78", "fan1", NULL, NULL},
	{0x70, 0x10, "iic0_sda", "GPIO79", NULL, NULL},
	{0x74, 0, "iic0_scl", "GPIO80", NULL, NULL},
	{0x74, 0x10, "iic1_sda", "GPIO81", NULL, NULL},
	{0x78, 0, "iic1_scl", "GPIO82", NULL, NULL},
	{0x78, 0x10, "GPIO83", "iic2_sda", NULL, NULL},
	{0x7c, 0, "GPIO84", "iic2_scl", NULL, NULL},
	{0x7c, 0x10, "uart0_tx", "GPIO85", NULL, NULL},
	{0x80, 0, "uart0_rx", "GPIO86", NULL, NULL},
	{0x80, 0x10, "uart1_tx", "GPIO87", NULL, NULL},
	{0x84, 0, "uart1_rx", "GPIO88", NULL, NULL},
	{0x84, 0x10, "GPIO89", "uart2_tx", NULL, NULL},
	{0x88, 0, "GPIO90", "uart2_rx", NULL, NULL},
	{0x8c, 0, "tpumem_pwr_good", "GPIO1", NULL, NULL},
	{0x8c, 0x10, "pcie_pwr_good", "GPIO2", NULL, NULL},
	{0x90, 0, "tpu_pwr_good", "GPIO3", NULL, NULL},
	{0x90, 0x10, "pll_lock", "vd0_ware0_tx", NULL, "GPIO4"},
	{0x94, 0, "GPIO5", "vd0_ware0_rx", NULL, NULL},
	{0x94, 0x10, "GPIO6", "vd0_wave1_tx", NULL, NULL},
	{0x98, 0, "GPIO7", "vd0_wave1_tx", NULL, NULL},
	{0x9c, 0, "GPIO9", "vd1_ware0_tx", NULL, NULL},
	{0x9c, 0x10, "GPIO10", "vd1_ware0_rx", NULL, NULL},
	{0xa0, 0, "GPIO11", "vd1_ware1_tx", NULL, NULL},
	{0xa0, 0x10, "GPIO12", "vd1_ware1_rx", NULL, NULL},
	{0xa4, 0, "GPIO13", "vde_ware_tx", NULL, "uart0_rts"},
	{0xa4, 0x10, "GPIO14", "vde_ware_rx", NULL, "uart0_cts"},
	{0xa8, 0, "jtag1_2_sel", "uart1_rts", NULL, "GPIO15"},
	{0xa8, 0x10, "GPIO16", "uart1_cts", NULL, NULL},
	{0xac, 0, "jtag0_tdo", "GPIO17", NULL, NULL},
	{0xac, 0x10, "jtag0_tck", "GPIO18", NULL, NULL},
	{0xb0, 0, "jtag0_tdi", "GPIO19", NULL, NULL},
	{0xb0, 0x10, "jtag0_tms", "GPIO20", NULL, NULL},
	{0xb4, 0, "jtag0_trst_x", "GPIO21", NULL, NULL},
	{0xb4, 0x10, "jtag0_srst_x", "GPIO22", NULL, NULL},
	{0xb8, 0, "jtag1_2_tdo", "GPIO23", NULL, NULL},
	{0xb8, 0x10, "jtag1_2_tck", "GPIO24", NULL, NULL},
	{0xbc, 0, "jtag1_2_tdi", "GPIO25", NULL, NULL},
	{0xbc, 0x10, "jtag1_2_tms", "GPIO26", NULL, NULL},
	{0xc0, 0, "jtag1_2_trst_x", "GPIO27", NULL, NULL},
	{0xc0, 0x10, "jtag1_2_srst_x", "GPIO28", NULL, NULL},
	{0xc4, 0, "dbgiic_scl", "GPIO29", NULL, NULL},
	{0xc4, 0x10, "dbgiic_sda", "GPIO30", NULL, NULL},
};

u32 sys_ctrl_read(u32 reg)
{
	return *(u32 *)(sys_ctrl_base + reg);
}

void sys_ctrl_write(u32 reg, u32 val)
{
	*(u32 *)(sys_ctrl_base + reg) = val;
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
	if (sys_ctrl_fd == -1) {
		printf("Open /dev/mem/ failed!\n");
		return -1;
	}

	offset = (STS_CTRL_PHY_BASE & ~(sysconf(_SC_PAGE_SIZE) - 1));
	sys_ctrl_base = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
			     MAP_SHARED, sys_ctrl_fd, offset);
	printf("%s %d: base address is 0x%p\n", __func__, __LINE__, sys_ctrl_base);
	if (sys_ctrl_base == MAP_FAILED) {
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

u32 get_pinmux_val_from_reg(u32 reg, u32 offset)
{
	u32 val;

	val = reg >> offset;
	val &= PINMUX_SELECTOR_MASK;

	return val >> 4;
}
/**
 * show_reg_info -Display the reg info for pinmux
 * @val: the value of the register
 *
 */
void show_reg_info(u16 val)
{
	printf("pull up enable: 0x%x\t pull down enable: 0x%x\n",
		(PULL_UP_ENABLE_MASK & val) >> 2, (PULL_DOWN_ENABLE_MASK & val) >> 3);
	printf("pinmux mux: 0x%x\tdrive selector: 0x%x\n",
		(PINMUX_SELECTOR_MASK & val) >> 4, (DRIVE_SELECTOR_MASK & val) >> 6);
	printf("schmit trigger enable: 0x%x\t pad_oex_enable: 0x%x\n",
		(SCHMIT_TRIGGER_ENABLE_MASK & val) >> 10, (PAD_OEX_ENABLE & val) >> 11);
}

int main(int argc, char *argv[])
{
	int ret, val;

	ret = sys_ctrl_map();
	if (ret != 0) {
		printf("map sys ctrl reg failed!\n");
		return -1;
	}

	for (int i = 0; i < ARRAY_SIZE(bm1686_pin); i++) {
		printf("register: 0x%x\n", STS_CTRL_PHY_BASE + GEN_PINMUX_REG(bm1686_pin[i].reg));
		printf("support pin function: %s\t%s\t%s\t%s\n", bm1686_pin[i].pin_name[0],
			bm1686_pin[i].pin_name[1], bm1686_pin[i].pin_name[2], bm1686_pin[i].pin_name[3]);
		val = pinmux_read(bm1686_pin[i].reg);
		printf("pinmux current state is %s\n",
			bm1686_pin[i].pin_name[get_pinmux_val_from_reg(val, bm1686_pin[i].offset)]);
		show_reg_info(val >> bm1686_pin[i].offset);
		printf("+++++++++++++++++++++++++++++++++++++++++++++++++\n");
	}

	sys_ctrl_unmap();
	return 0;
}
