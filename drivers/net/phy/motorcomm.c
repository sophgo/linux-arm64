// SPDX-License-Identifier: GPL-2.0+
/*
 * Motorcomm 8511/8521/8531/8531S PHY driver.
 *
 * Author: Peter Geis <pgwipeout@gmail.com>
 * Author: Frank <Frank.Sae@motor-comm.com>
 */

#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/of.h>
#include <linux/bitfield.h>

#define PHY_ID_YT8511 0x0000010a
#define PHY_ID_YT8531 0x4f51e91b

/* YT8521/YT8531S Register Overview
 *	UTP Register space	|	FIBER Register space
 *  ------------------------------------------------------------
 * |	UTP MII			|	FIBER MII		|
 * |	UTP MMD			|				|
 * |	UTP Extended		|	FIBER Extended		|
 *  ------------------------------------------------------------
 * |			Common Extended				|
 *  ------------------------------------------------------------
 */

/* 0x10 ~ 0x15 , 0x1E and 0x1F are common MII registers of yt phy */

/* Specific Function Control Register */
#define YTPHY_SPECIFIC_FUNCTION_CONTROL_REG 0x10

/* 2b00 Manual MDI configuration
 * 2b01 Manual MDIX configuration
 * 2b10 Reserved
 * 2b11 Enable automatic crossover for all modes  *default*
 */
#define YTPHY_SFCR_MDI_CROSSOVER_MODE_MASK (BIT(6) | BIT(5))
#define YTPHY_SFCR_CROSSOVER_EN BIT(3)
#define YTPHY_SFCR_SQE_TEST_EN BIT(2)
#define YTPHY_SFCR_POLARITY_REVERSAL_EN BIT(1)
#define YTPHY_SFCR_JABBER_DIS BIT(0)

/* Specific Status Register */
#define YTPHY_SPECIFIC_STATUS_REG 0x11
#define YTPHY_SSR_SPEED_MODE_OFFSET 14

#define YTPHY_SSR_SPEED_MODE_MASK (BIT(15) | BIT(14))
#define YTPHY_SSR_SPEED_10M 0x0
#define YTPHY_SSR_SPEED_100M 0x1
#define YTPHY_SSR_SPEED_1000M 0x2
#define YTPHY_SSR_DUPLEX_OFFSET 13
#define YTPHY_SSR_DUPLEX BIT(13)
#define YTPHY_SSR_PAGE_RECEIVED BIT(12)
#define YTPHY_SSR_SPEED_DUPLEX_RESOLVED BIT(11)
#define YTPHY_SSR_LINK BIT(10)
#define YTPHY_SSR_MDIX_CROSSOVER BIT(6)
#define YTPHY_SSR_DOWNGRADE BIT(5)
#define YTPHY_SSR_TRANSMIT_PAUSE BIT(3)
#define YTPHY_SSR_RECEIVE_PAUSE BIT(2)
#define YTPHY_SSR_POLARITY BIT(1)
#define YTPHY_SSR_JABBER BIT(0)

/* Interrupt enable Register */
#define YTPHY_INTERRUPT_ENABLE_REG 0x12
#define YTPHY_IER_WOL BIT(6)

/* Interrupt Status Register */
#define YTPHY_INTERRUPT_STATUS_REG 0x13
#define YTPHY_ISR_AUTONEG_ERR BIT(15)
#define YTPHY_ISR_SPEED_CHANGED BIT(14)
#define YTPHY_ISR_DUPLEX_CHANGED BIT(13)
#define YTPHY_ISR_PAGE_RECEIVED BIT(12)
#define YTPHY_ISR_LINK_FAILED BIT(11)
#define YTPHY_ISR_LINK_SUCCESSED BIT(10)
#define YTPHY_ISR_WOL BIT(6)
#define YTPHY_ISR_WIRESPEED_DOWNGRADE BIT(5)
#define YTPHY_ISR_SERDES_LINK_FAILED BIT(3)
#define YTPHY_ISR_SERDES_LINK_SUCCESSED BIT(2)
#define YTPHY_ISR_POLARITY_CHANGED BIT(1)
#define YTPHY_ISR_JABBER_HAPPENED BIT(0)

/* Speed Auto Downgrade Control Register */
#define YTPHY_SPEED_AUTO_DOWNGRADE_CONTROL_REG 0x14
#define YTPHY_SADCR_SPEED_DOWNGRADE_EN BIT(5)

/* If these bits are set to 3, the PHY attempts five times ( 3(set value) +
 * additional 2) before downgrading, default 0x3
 */
#define YTPHY_SADCR_SPEED_RETRY_LIMIT (0x3 << 2)

/* Rx Error Counter Register */
#define YTPHY_RX_ERROR_COUNTER_REG 0x15

/* Extended Register's Address Offset Register */
#define YTPHY_PAGE_SELECT 0x1E

/* Extended Register's Data Register */
#define YTPHY_PAGE_DATA 0x1F

/* FIBER Auto-Negotiation link partner ability */
#define YTPHY_FLPA_PAUSE (0x3 << 7)
#define YTPHY_FLPA_ASYM_PAUSE (0x2 << 7)

#define YT8511_PAGE_SELECT 0x1e
#define YT8511_PAGE 0x1f
#define YT8511_EXT_CLK_GATE 0x0c
#define YT8511_EXT_DELAY_DRIVE 0x0d
#define YT8511_EXT_SLEEP_CTRL 0x27

/* 2b00 25m from pll
 * 2b01 25m from xtl *default*
 * 2b10 62.m from pll
 * 2b11 125m from pll
 */
#define YT8511_CLK_125M (BIT(2) | BIT(1))
#define YT8511_PLLON_SLP BIT(14)

/* RX Delay enabled = 1.8ns 1000T, 8ns 10/100T */
#define YT8511_DELAY_RX BIT(0)

/* TX Gig-E Delay is bits 7:4, default 0x5
 * TX Fast-E Delay is bits 15:12, default 0xf
 * Delay = 150ps * N - 250ps
 * On = 2000ps, off = 50ps
 */
#define YT8511_DELAY_GE_TX_EN (0xf << 4)
#define YT8511_DELAY_GE_TX_DIS (0x2 << 4)
#define YT8511_DELAY_FE_TX_EN (0xf << 12)
#define YT8511_DELAY_FE_TX_DIS (0x2 << 12)

/* Extended register is different from MMD Register and MII Register.
 * We can use ytphy_read_ext/ytphy_write_ext/ytphy_modify_ext function to
 * operate extended register.
 * Extended Register  start
 */

/* Phy gmii clock gating Register */
#define YT8521_CLOCK_GATING_REG 0xC
#define YT8521_CGR_RX_CLK_EN BIT(12)

#define YT8521_EXTREG_SLEEP_CONTROL1_REG 0x27
#define YT8521_ESC1R_SLEEP_SW BIT(15)
#define YT8521_ESC1R_PLLON_SLP BIT(14)

/* Phy fiber Link timer cfg2 Register */
#define YT8521_LINK_TIMER_CFG2_REG 0xA5
#define YT8521_LTCR_EN_AUTOSEN BIT(15)

/* 0xA000, 0xA001, 0xA003, 0xA006 ~ 0xA00A and 0xA012 are common ext registers
 * of yt8521 phy. There is no need to switch reg space when operating these
 * registers.
 */

#define YT8521_REG_SPACE_SELECT_REG 0xA000
#define YT8521_RSSR_SPACE_MASK BIT(1)
#define YT8521_RSSR_FIBER_SPACE (0x1 << 1)
#define YT8521_RSSR_UTP_SPACE (0x0 << 1)
#define YT8521_RSSR_TO_BE_ARBITRATED (0xFF)

#define YT8521_CHIP_CONFIG_REG 0xA001
#define YT8521_CCR_SW_RST BIT(15)
#define YT8531_RGMII_LDO_VOL_MASK GENMASK(5, 4)
#define YT8531_LDO_VOL_3V3 0x0
#define YT8531_LDO_VOL_1V8 0x2

/* 1b0 disable 1.9ns rxc clock delay  *default*
 * 1b1 enable 1.9ns rxc clock delay
 */
#define YT8521_CCR_RXC_DLY_EN BIT(8)
#define YT8521_CCR_RXC_DLY_1_900_NS 1900

#define YT8521_CCR_MODE_SEL_MASK (BIT(2) | BIT(1) | BIT(0))
#define YT8521_CCR_MODE_UTP_TO_RGMII 0
#define YT8521_CCR_MODE_FIBER_TO_RGMII 1
#define YT8521_CCR_MODE_UTP_FIBER_TO_RGMII 2
#define YT8521_CCR_MODE_UTP_TO_SGMII 3
#define YT8521_CCR_MODE_SGPHY_TO_RGMAC 4
#define YT8521_CCR_MODE_SGMAC_TO_RGPHY 5
#define YT8521_CCR_MODE_UTP_TO_FIBER_AUTO 6
#define YT8521_CCR_MODE_UTP_TO_FIBER_FORCE 7

/* 3 phy polling modes,poll mode combines utp and fiber mode*/
#define YT8521_MODE_FIBER 0x1
#define YT8521_MODE_UTP 0x2
#define YT8521_MODE_POLL 0x3

#define YT8521_RGMII_CONFIG1_REG 0xA003
/* 1b0 use original tx_clk_rgmii  *default*
 * 1b1 use inverted tx_clk_rgmii.
 */
#define YT8521_RC1R_TX_CLK_SEL_INVERTED BIT(14)
#define YT8521_RC1R_RX_DELAY_MASK GENMASK(13, 10)
#define YT8521_RC1R_FE_TX_DELAY_MASK GENMASK(7, 4)
#define YT8521_RC1R_GE_TX_DELAY_MASK GENMASK(3, 0)
#define YT8521_RC1R_RGMII_0_000_NS 0
#define YT8521_RC1R_RGMII_0_150_NS 1
#define YT8521_RC1R_RGMII_0_300_NS 2
#define YT8521_RC1R_RGMII_0_450_NS 3
#define YT8521_RC1R_RGMII_0_600_NS 4
#define YT8521_RC1R_RGMII_0_750_NS 5
#define YT8521_RC1R_RGMII_0_900_NS 6
#define YT8521_RC1R_RGMII_1_050_NS 7
#define YT8521_RC1R_RGMII_1_200_NS 8
#define YT8521_RC1R_RGMII_1_350_NS 9
#define YT8521_RC1R_RGMII_1_500_NS 10
#define YT8521_RC1R_RGMII_1_650_NS 11
#define YT8521_RC1R_RGMII_1_800_NS 12
#define YT8521_RC1R_RGMII_1_950_NS 13
#define YT8521_RC1R_RGMII_2_100_NS 14
#define YT8521_RC1R_RGMII_2_250_NS 15

#define YTPHY_MISC_CONFIG_REG 0xA006
#define YTPHY_MCR_FIBER_SPEED_MASK BIT(0)
#define YTPHY_MCR_FIBER_1000BX (0x1 << 0)
#define YTPHY_MCR_FIBER_100FX (0x0 << 0)

/* WOL MAC ADDR: MACADDR2(highest), MACADDR1(middle), MACADDR0(lowest) */
#define YTPHY_WOL_MACADDR2_REG 0xA007
#define YTPHY_WOL_MACADDR1_REG 0xA008
#define YTPHY_WOL_MACADDR0_REG 0xA009

#define YTPHY_WOL_CONFIG_REG 0xA00A
#define YTPHY_WCR_INTR_SEL BIT(6)
#define YTPHY_WCR_ENABLE BIT(3)

/* 2b00 84ms
 * 2b01 168ms  *default*
 * 2b10 336ms
 * 2b11 672ms
 */
#define YTPHY_WCR_PULSE_WIDTH_MASK (BIT(2) | BIT(1))
#define YTPHY_WCR_PULSE_WIDTH_672MS (BIT(2) | BIT(1))

/* 1b0 Interrupt and WOL events is level triggered and active LOW  *default*
 * 1b1 Interrupt and WOL events is pulse triggered and active LOW
 */
#define YTPHY_WCR_TYPE_PULSE BIT(0)

#define YTPHY_LED0_CONFIG_REG 0xA00C
#define YTPHY_LED1_CONFIG_REG 0xA00D
#define YTPHY_LED2_CONFIG_REG 0xA00E

#define YTPHY_PAD_DRIVE_STRENGTH_REG 0xA010
#define YT8531_RGMII_RXC_DS_MASK GENMASK(15, 13)
#define YT8531_RGMII_RXD_DS_HI_MASK BIT(12) /* Bit 2 of rxd_ds */
#define YT8531_RGMII_RXD_DS_LOW_MASK GENMASK(5, 4) /* Bit 1/0 of rxd_ds */
#define YT8531_RGMII_RX_DS_DEFAULT 0x3

#define YTPHY_SYNCE_CFG_REG 0xA012
#define YT8521_SCR_SYNCE_ENABLE BIT(5)
/* 1b0 output 25m clock
 * 1b1 output 125m clock  *default*
 */
#define YT8521_SCR_CLK_FRE_SEL_125M BIT(3)
#define YT8521_SCR_CLK_SRC_MASK GENMASK(2, 1)
#define YT8521_SCR_CLK_SRC_PLL_125M 0
#define YT8521_SCR_CLK_SRC_UTP_RX 1
#define YT8521_SCR_CLK_SRC_SDS_RX 2
#define YT8521_SCR_CLK_SRC_REF_25M 3
#define YT8531_SCR_SYNCE_ENABLE BIT(6)
/* 1b0 output 25m clock   *default*
 * 1b1 output 125m clock
 */
#define YT8531_SCR_CLK_FRE_SEL_125M BIT(4)
#define YT8531_SCR_CLK_SRC_MASK GENMASK(3, 1)
#define YT8531_SCR_CLK_SRC_PLL_125M 0
#define YT8531_SCR_CLK_SRC_UTP_RX 1
#define YT8531_SCR_CLK_SRC_SDS_RX 2
#define YT8531_SCR_CLK_SRC_CLOCK_FROM_DIGITAL 3
#define YT8531_SCR_CLK_SRC_REF_25M 4
#define YT8531_SCR_CLK_SRC_SSC_25M 5

/* Extended Register  end */

#define YTPHY_DTS_OUTPUT_CLK_DIS 0
#define YTPHY_DTS_OUTPUT_CLK_25M 25000000
#define YTPHY_DTS_OUTPUT_CLK_125M 125000000

static inline void phy_lock_mdio_bus(struct phy_device *phydev)
{
	mutex_lock(&phydev->mdio.bus->mdio_lock);
}

static inline void phy_unlock_mdio_bus(struct phy_device *phydev)
{
	mutex_unlock(&phydev->mdio.bus->mdio_lock);
}


/**
 * ytphy_read_ext() - read a PHY's extended register
 * @phydev: a pointer to a &struct phy_device
 * @regnum: register number to read
 *
 * NOTE:The caller must have taken the MDIO bus lock.
 *
 * returns the value of regnum reg or negative error code
 */
static int ytphy_read_ext(struct phy_device *phydev, u16 regnum)
{
	int ret;

	ret = __phy_write(phydev, YTPHY_PAGE_SELECT, regnum);
	if (ret < 0)
		return ret;

	return __phy_read(phydev, YTPHY_PAGE_DATA);
}

/**
 * ytphy_read_ext_with_lock() - read a PHY's extended register
 * @phydev: a pointer to a &struct phy_device
 * @regnum: register number to read
 *
 * returns the value of regnum reg or negative error code
 */
static int ytphy_read_ext_with_lock(struct phy_device *phydev, u16 regnum)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = ytphy_read_ext(phydev, regnum);
	phy_unlock_mdio_bus(phydev);

	return ret;
}

/**
 * ytphy_write_ext() - write a PHY's extended register
 * @phydev: a pointer to a &struct phy_device
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * NOTE:The caller must have taken the MDIO bus lock.
 *
 * returns 0 or negative error code
 */
static int ytphy_write_ext(struct phy_device *phydev, u16 regnum, u16 val)
{
	int ret;

	ret = __phy_write(phydev, YTPHY_PAGE_SELECT, regnum);
	if (ret < 0)
		return ret;

	return __phy_write(phydev, YTPHY_PAGE_DATA, val);
}

/**
 * ytphy_write_ext_with_lock() - write a PHY's extended register
 * @phydev: a pointer to a &struct phy_device
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * returns 0 or negative error code
 */
static int ytphy_write_ext_with_lock(struct phy_device *phydev, u16 regnum,
				     u16 val)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = ytphy_write_ext(phydev, regnum, val);
	phy_unlock_mdio_bus(phydev);

	return ret;
}

/**
 * ytphy_modify_ext() - bits modify a PHY's extended register
 * @phydev: a pointer to a &struct phy_device
 * @regnum: register number to write
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * NOTE: Convenience function which allows a PHY's extended register to be
 * modified as new register value = (old register value & ~mask) | set.
 * The caller must have taken the MDIO bus lock.
 *
 * returns 0 or negative error code
 */
static int ytphy_modify_ext(struct phy_device *phydev, u16 regnum, u16 mask,
			    u16 set)
{
	int ret;

	ret = __phy_write(phydev, YTPHY_PAGE_SELECT, regnum);
	if (ret < 0)
		return ret;

	return __phy_modify(phydev, YTPHY_PAGE_DATA, mask, set);
}

/**
 * ytphy_modify_ext_with_lock() - bits modify a PHY's extended register
 * @phydev: a pointer to a &struct phy_device
 * @regnum: register number to write
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * NOTE: Convenience function which allows a PHY's extended register to be
 * modified as new register value = (old register value & ~mask) | set.
 *
 * returns 0 or negative error code
 */
static int ytphy_modify_ext_with_lock(struct phy_device *phydev, u16 regnum,
				      u16 mask, u16 set)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = ytphy_modify_ext(phydev, regnum, mask, set);
	phy_unlock_mdio_bus(phydev);

	return ret;
}

/**
 * ytphy_get_wol() - report whether wake-on-lan is enabled
 * @phydev: a pointer to a &struct phy_device
 * @wol: a pointer to a &struct ethtool_wolinfo
 *
 * NOTE: YTPHY_WOL_CONFIG_REG is common ext reg.
 */
static void ytphy_get_wol(struct phy_device *phydev,
			  struct ethtool_wolinfo *wol)
{
	int wol_config;

	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;

	wol_config = ytphy_read_ext_with_lock(phydev, YTPHY_WOL_CONFIG_REG);
	if (wol_config < 0)
		return;

	if (wol_config & YTPHY_WCR_ENABLE)
		wol->wolopts |= WAKE_MAGIC;
}

static int yt8531_set_wol(struct phy_device *phydev,
			  struct ethtool_wolinfo *wol)
{
	const u16 mac_addr_reg[] = {
		YTPHY_WOL_MACADDR2_REG,
		YTPHY_WOL_MACADDR1_REG,
		YTPHY_WOL_MACADDR0_REG,
	};
	const u8 *mac_addr;
	u16 mask, val;
	int ret;
	u8 i;

	if (wol->wolopts & WAKE_MAGIC) {
		mac_addr = phydev->attached_dev->dev_addr;

		/* Store the device address for the magic packet */
		for (i = 0; i < 3; i++) {
			ret = ytphy_write_ext_with_lock(
				phydev, mac_addr_reg[i],
				((mac_addr[i * 2] << 8)) |
					(mac_addr[i * 2 + 1]));
			if (ret < 0)
				return ret;
		}

		/* Enable WOL feature */
		mask = YTPHY_WCR_PULSE_WIDTH_MASK | YTPHY_WCR_INTR_SEL;
		val = YTPHY_WCR_ENABLE | YTPHY_WCR_INTR_SEL;
		val |= YTPHY_WCR_TYPE_PULSE | YTPHY_WCR_PULSE_WIDTH_672MS;
		ret = ytphy_modify_ext_with_lock(phydev, YTPHY_WOL_CONFIG_REG,
						 mask, val);
		if (ret < 0)
			return ret;

		/* Enable WOL interrupt */
		ret = phy_modify(phydev, YTPHY_INTERRUPT_ENABLE_REG, 0,
				 YTPHY_IER_WOL);
		if (ret < 0)
			return ret;
	} else {
		/* Disable WOL feature */
		mask = YTPHY_WCR_ENABLE | YTPHY_WCR_INTR_SEL;
		ret = ytphy_modify_ext_with_lock(phydev, YTPHY_WOL_CONFIG_REG,
						 mask, 0);

		/* Disable WOL interrupt */
		ret = phy_modify(phydev, YTPHY_INTERRUPT_ENABLE_REG,
				 YTPHY_IER_WOL, 0);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int yt8511_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, YT8511_PAGE_SELECT);
};

static int yt8511_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, YT8511_PAGE_SELECT, page);
};

static int yt8511_config_init(struct phy_device *phydev)
{
	int oldpage, ret = 0;
	unsigned int ge, fe;

	oldpage = phy_select_page(phydev, YT8511_EXT_CLK_GATE);
	if (oldpage < 0)
		goto err_restore_page;

	/* set rgmii delay mode */
	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		ge = YT8511_DELAY_GE_TX_DIS;
		fe = YT8511_DELAY_FE_TX_DIS;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		ge = YT8511_DELAY_RX | YT8511_DELAY_GE_TX_DIS;
		fe = YT8511_DELAY_FE_TX_DIS;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		ge = YT8511_DELAY_GE_TX_EN;
		fe = YT8511_DELAY_FE_TX_EN;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		ge = YT8511_DELAY_RX | YT8511_DELAY_GE_TX_EN;
		fe = YT8511_DELAY_FE_TX_EN;
		break;
	default: /* do not support other modes */
		ret = -EOPNOTSUPP;
		goto err_restore_page;
	}

	ret = __phy_modify(phydev, YT8511_PAGE,
			   (YT8511_DELAY_RX | YT8511_DELAY_GE_TX_EN), ge);
	if (ret < 0)
		goto err_restore_page;

	/* set clock mode to 125mhz */
	ret = __phy_modify(phydev, YT8511_PAGE, 0, YT8511_CLK_125M);
	if (ret < 0)
		goto err_restore_page;

	/* fast ethernet delay is in a separate page */
	ret = __phy_write(phydev, YT8511_PAGE_SELECT, YT8511_EXT_DELAY_DRIVE);
	if (ret < 0)
		goto err_restore_page;

	ret = __phy_modify(phydev, YT8511_PAGE, YT8511_DELAY_FE_TX_EN, fe);
	if (ret < 0)
		goto err_restore_page;

	/* leave pll enabled in sleep */
	ret = __phy_write(phydev, YT8511_PAGE_SELECT, YT8511_EXT_SLEEP_CTRL);
	if (ret < 0)
		goto err_restore_page;

	ret = __phy_modify(phydev, YT8511_PAGE, 0, YT8511_PLLON_SLP);
	if (ret < 0)
		goto err_restore_page;

err_restore_page:
	return phy_restore_page(phydev, oldpage, ret);
}

/**
 * struct ytphy_cfg_reg_map - map a config value to a register value
 * @cfg: value in device configuration
 * @reg: value in the register
 */
struct ytphy_cfg_reg_map {
	u32 cfg;
	u32 reg;
};

static const struct ytphy_cfg_reg_map ytphy_rgmii_delays[] = {
	/* for tx delay / rx delay with YT8521_CCR_RXC_DLY_EN is not set. */
	{ 0, YT8521_RC1R_RGMII_0_000_NS },
	{ 150, YT8521_RC1R_RGMII_0_150_NS },
	{ 300, YT8521_RC1R_RGMII_0_300_NS },
	{ 450, YT8521_RC1R_RGMII_0_450_NS },
	{ 600, YT8521_RC1R_RGMII_0_600_NS },
	{ 750, YT8521_RC1R_RGMII_0_750_NS },
	{ 900, YT8521_RC1R_RGMII_0_900_NS },
	{ 1050, YT8521_RC1R_RGMII_1_050_NS },
	{ 1200, YT8521_RC1R_RGMII_1_200_NS },
	{ 1350, YT8521_RC1R_RGMII_1_350_NS },
	{ 1500, YT8521_RC1R_RGMII_1_500_NS },
	{ 1650, YT8521_RC1R_RGMII_1_650_NS },
	{ 1800, YT8521_RC1R_RGMII_1_800_NS },
	{ 1950, YT8521_RC1R_RGMII_1_950_NS }, /* default tx/rx delay */
	{ 2100, YT8521_RC1R_RGMII_2_100_NS },
	{ 2250, YT8521_RC1R_RGMII_2_250_NS },

	/* only for rx delay with YT8521_CCR_RXC_DLY_EN is set. */
	{ 0 + YT8521_CCR_RXC_DLY_1_900_NS, YT8521_RC1R_RGMII_0_000_NS },
	{ 150 + YT8521_CCR_RXC_DLY_1_900_NS, YT8521_RC1R_RGMII_0_150_NS },
	{ 300 + YT8521_CCR_RXC_DLY_1_900_NS, YT8521_RC1R_RGMII_0_300_NS },
	{ 450 + YT8521_CCR_RXC_DLY_1_900_NS, YT8521_RC1R_RGMII_0_450_NS },
	{ 600 + YT8521_CCR_RXC_DLY_1_900_NS, YT8521_RC1R_RGMII_0_600_NS },
	{ 750 + YT8521_CCR_RXC_DLY_1_900_NS, YT8521_RC1R_RGMII_0_750_NS },
	{ 900 + YT8521_CCR_RXC_DLY_1_900_NS, YT8521_RC1R_RGMII_0_900_NS },
	{ 1050 + YT8521_CCR_RXC_DLY_1_900_NS, YT8521_RC1R_RGMII_1_050_NS },
	{ 1200 + YT8521_CCR_RXC_DLY_1_900_NS, YT8521_RC1R_RGMII_1_200_NS },
	{ 1350 + YT8521_CCR_RXC_DLY_1_900_NS, YT8521_RC1R_RGMII_1_350_NS },
	{ 1500 + YT8521_CCR_RXC_DLY_1_900_NS, YT8521_RC1R_RGMII_1_500_NS },
	{ 1650 + YT8521_CCR_RXC_DLY_1_900_NS, YT8521_RC1R_RGMII_1_650_NS },
	{ 1800 + YT8521_CCR_RXC_DLY_1_900_NS, YT8521_RC1R_RGMII_1_800_NS },
	{ 1950 + YT8521_CCR_RXC_DLY_1_900_NS, YT8521_RC1R_RGMII_1_950_NS },
	{ 2100 + YT8521_CCR_RXC_DLY_1_900_NS, YT8521_RC1R_RGMII_2_100_NS },
	{ 2250 + YT8521_CCR_RXC_DLY_1_900_NS, YT8521_RC1R_RGMII_2_250_NS }
};

static u32 ytphy_get_delay_reg_value(struct phy_device *phydev,
				     const char *prop_name,
				     const struct ytphy_cfg_reg_map *tbl,
				     int tb_size, u16 *rxc_dly_en, u32 dflt)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	int tb_size_half = tb_size / 2;
	u32 val;
	int i;

	if (of_property_read_u32(node, prop_name, &val))
		goto err_dts_val;

	/* when rxc_dly_en is NULL, it is get the delay for tx, only half of
	 * tb_size is valid.
	 */
	if (!rxc_dly_en)
		tb_size = tb_size_half;

	for (i = 0; i < tb_size; i++) {
		if (tbl[i].cfg == val) {
			if (rxc_dly_en && i < tb_size_half)
				*rxc_dly_en = 0;
			return tbl[i].reg;
		}
	}

	phydev_warn(phydev, "Unsupported value %d for %s using default (%u)\n",
		    val, prop_name, dflt);

err_dts_val:
	/* when rxc_dly_en is not NULL, it is get the delay for rx.
	 * The rx default in dts and ytphy_rgmii_clk_delay_config is 1950 ps,
	 * so YT8521_CCR_RXC_DLY_EN should not be set.
	 */
	if (rxc_dly_en)
		*rxc_dly_en = 0;

	return dflt;
}

static int ytphy_rgmii_clk_delay_config(struct phy_device *phydev)
{
	int tb_size = ARRAY_SIZE(ytphy_rgmii_delays);
	u16 rxc_dly_en = YT8521_CCR_RXC_DLY_EN;
	u32 rx_reg, tx_reg;
	u16 mask, val = 0;
	int ret;

	rx_reg = ytphy_get_delay_reg_value(phydev, "rx-internal-delay-ps",
					   ytphy_rgmii_delays, tb_size,
					   &rxc_dly_en,
					   YT8521_RC1R_RGMII_1_950_NS);
	tx_reg = ytphy_get_delay_reg_value(phydev, "tx-internal-delay-ps",
					   ytphy_rgmii_delays, tb_size, NULL,
					   YT8521_RC1R_RGMII_1_950_NS);

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		rxc_dly_en = 0;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		val |= FIELD_PREP(YT8521_RC1R_RX_DELAY_MASK, rx_reg);
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		rxc_dly_en = 0;
		val |= FIELD_PREP(YT8521_RC1R_GE_TX_DELAY_MASK, tx_reg);
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		val |= FIELD_PREP(YT8521_RC1R_RX_DELAY_MASK, rx_reg) |
		       FIELD_PREP(YT8521_RC1R_GE_TX_DELAY_MASK, tx_reg);
		break;
	default: /* do not support other modes */
		return -EOPNOTSUPP;
	}

	ret = ytphy_modify_ext(phydev, YT8521_CHIP_CONFIG_REG,
			       YT8521_CCR_RXC_DLY_EN, rxc_dly_en);
	if (ret < 0)
		return ret;

	/* Generally, it is not necessary to adjust YT8521_RC1R_FE_TX_DELAY */
	mask = YT8521_RC1R_RX_DELAY_MASK | YT8521_RC1R_GE_TX_DELAY_MASK;
	return ytphy_modify_ext(phydev, YT8521_RGMII_CONFIG1_REG, mask, val);
}

static int ytphy_rgmii_clk_delay_config_with_lock(struct phy_device *phydev)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = ytphy_rgmii_clk_delay_config(phydev);
	phy_unlock_mdio_bus(phydev);

	return ret;
}

static void ytphy_led_config(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	int led0_config, led1_config, led2_config;

	if (of_property_read_u32(node, "led0_config", &led0_config))
		led0_config = 0;

	if (of_property_read_u32(node, "led1_config", &led1_config))
		led1_config = 0;

	if (of_property_read_u32(node, "led2_config", &led2_config))
		led2_config = 0;

	ytphy_write_ext_with_lock(phydev, YTPHY_LED0_CONFIG_REG, led0_config);
	ytphy_write_ext_with_lock(phydev, YTPHY_LED1_CONFIG_REG, led1_config);
	ytphy_write_ext_with_lock(phydev, YTPHY_LED2_CONFIG_REG, led2_config);
}

/**
 * struct ytphy_ldo_vol_map - map a current value to a register value
 * @vol: ldo voltage
 * @ds:  value in the register
 * @cur: value in device configuration
 */
struct ytphy_ldo_vol_map {
	u32 vol;
	u32 ds;
	u32 cur;
};

static const struct ytphy_ldo_vol_map yt8531_ldo_vol[] = {
	{ .vol = YT8531_LDO_VOL_1V8, .ds = 0, .cur = 1200 },
	{ .vol = YT8531_LDO_VOL_1V8, .ds = 1, .cur = 2100 },
	{ .vol = YT8531_LDO_VOL_1V8, .ds = 2, .cur = 2700 },
	{ .vol = YT8531_LDO_VOL_1V8, .ds = 3, .cur = 2910 },
	{ .vol = YT8531_LDO_VOL_1V8, .ds = 4, .cur = 3110 },
	{ .vol = YT8531_LDO_VOL_1V8, .ds = 5, .cur = 3600 },
	{ .vol = YT8531_LDO_VOL_1V8, .ds = 6, .cur = 3970 },
	{ .vol = YT8531_LDO_VOL_1V8, .ds = 7, .cur = 4350 },
	{ .vol = YT8531_LDO_VOL_3V3, .ds = 0, .cur = 3070 },
	{ .vol = YT8531_LDO_VOL_3V3, .ds = 1, .cur = 4080 },
	{ .vol = YT8531_LDO_VOL_3V3, .ds = 2, .cur = 4370 },
	{ .vol = YT8531_LDO_VOL_3V3, .ds = 3, .cur = 4680 },
	{ .vol = YT8531_LDO_VOL_3V3, .ds = 4, .cur = 5020 },
	{ .vol = YT8531_LDO_VOL_3V3, .ds = 5, .cur = 5450 },
	{ .vol = YT8531_LDO_VOL_3V3, .ds = 6, .cur = 5740 },
	{ .vol = YT8531_LDO_VOL_3V3, .ds = 7, .cur = 6140 },
};

static u32 yt8531_get_ldo_vol(struct phy_device *phydev)
{
	u32 val;

	val = ytphy_read_ext_with_lock(phydev, YT8521_CHIP_CONFIG_REG);
	val = FIELD_GET(YT8531_RGMII_LDO_VOL_MASK, val);

	return val <= YT8531_LDO_VOL_1V8 ? val : YT8531_LDO_VOL_1V8;
}

static int yt8531_get_ds_map(struct phy_device *phydev, u32 cur)
{
	u32 vol;
	int i;

	vol = yt8531_get_ldo_vol(phydev);
	for (i = 0; i < ARRAY_SIZE(yt8531_ldo_vol); i++) {
		if (yt8531_ldo_vol[i].vol == vol &&
		    yt8531_ldo_vol[i].cur == cur)
			return yt8531_ldo_vol[i].ds;
	}

	return -EINVAL;
}

static int yt8531_set_ds(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	u32 ds_field_low, ds_field_hi, val;
	int ret, ds;

	/* set rgmii rx clk driver strength */
	if (!of_property_read_u32(node, "motorcomm,rx-clk-drv-microamp",
				  &val)) {
		ds = yt8531_get_ds_map(phydev, val);
		if (ds < 0) {
			pr_warn("No matching current value was found.");
			return -EINVAL;
		}
	} else {
		ds = YT8531_RGMII_RX_DS_DEFAULT;
	}

	ret = ytphy_modify_ext_with_lock(
		phydev, YTPHY_PAD_DRIVE_STRENGTH_REG, YT8531_RGMII_RXC_DS_MASK,
		FIELD_PREP(YT8531_RGMII_RXC_DS_MASK, ds));
	if (ret < 0)
		return ret;

	/* set rgmii rx data driver strength */
	if (!of_property_read_u32(node, "motorcomm,rx-data-drv-microamp",
				  &val)) {
		ds = yt8531_get_ds_map(phydev, val);
		if (ds < 0) {
			pr_warn("No matching current value was found.");
			return -EINVAL;
		}
	} else {
		ds = YT8531_RGMII_RX_DS_DEFAULT;
	}

	ds_field_hi = FIELD_GET(BIT(2), ds);
	ds_field_hi = FIELD_PREP(YT8531_RGMII_RXD_DS_HI_MASK, ds_field_hi);

	ds_field_low = FIELD_GET(GENMASK(1, 0), ds);
	ds_field_low = FIELD_PREP(YT8531_RGMII_RXD_DS_LOW_MASK, ds_field_low);

	ret = ytphy_modify_ext_with_lock(phydev, YTPHY_PAD_DRIVE_STRENGTH_REG,
					 YT8531_RGMII_RXD_DS_LOW_MASK |
						 YT8531_RGMII_RXD_DS_HI_MASK,
					 ds_field_low | ds_field_hi);
	if (ret < 0)
		return ret;

	return 0;
}


static int yt8531_probe(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	u16 mask, val;
	u32 freq;

	if (of_property_read_u32(node, "motorcomm,clk-out-frequency-hz", &freq))
		freq = YTPHY_DTS_OUTPUT_CLK_DIS;

	switch (freq) {
	case YTPHY_DTS_OUTPUT_CLK_DIS:
		mask = YT8531_SCR_SYNCE_ENABLE;
		val = 0;
		break;
	case YTPHY_DTS_OUTPUT_CLK_25M:
		mask = YT8531_SCR_SYNCE_ENABLE | YT8531_SCR_CLK_SRC_MASK |
		       YT8531_SCR_CLK_FRE_SEL_125M;
		val = YT8531_SCR_SYNCE_ENABLE |
		      FIELD_PREP(YT8531_SCR_CLK_SRC_MASK,
				 YT8531_SCR_CLK_SRC_REF_25M);
		break;
	case YTPHY_DTS_OUTPUT_CLK_125M:
		mask = YT8531_SCR_SYNCE_ENABLE | YT8531_SCR_CLK_SRC_MASK |
		       YT8531_SCR_CLK_FRE_SEL_125M;
		val = YT8531_SCR_SYNCE_ENABLE | YT8531_SCR_CLK_FRE_SEL_125M |
		      FIELD_PREP(YT8531_SCR_CLK_SRC_MASK,
				 YT8531_SCR_CLK_SRC_PLL_125M);
		break;
	default:
		phydev_warn(phydev, "Freq err:%u\n", freq);
		return -EINVAL;
	}

	return ytphy_modify_ext_with_lock(phydev, YTPHY_SYNCE_CFG_REG, mask,
					  val);
}

static int yt8531_config_init(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	int ret;

	ret = ytphy_rgmii_clk_delay_config_with_lock(phydev);
	if (ret < 0)
		return ret;

	if (of_property_read_bool(node, "motorcomm,auto-sleep-disabled")) {
		/* disable auto sleep */
		ret = ytphy_modify_ext_with_lock(
			phydev, YT8521_EXTREG_SLEEP_CONTROL1_REG,
			YT8521_ESC1R_SLEEP_SW, 0);
		if (ret < 0)
			return ret;
	}

	if (of_property_read_bool(node, "motorcomm,keep-pll-enabled")) {
		/* enable RXC clock when no wire plug */
		ret = ytphy_modify_ext_with_lock(phydev,
						 YT8521_CLOCK_GATING_REG,
						 YT8521_CGR_RX_CLK_EN, 0);
		if (ret < 0)
			return ret;
	}

	ret = yt8531_set_ds(phydev);
	if (ret < 0)
		return ret;

	ytphy_led_config(phydev);

	return 0;
}

/**
 * yt8531_link_change_notify() - Adjust the tx clock direction according to
 * the current speed and dts config.
 * @phydev: a pointer to a &struct phy_device
 *
 * NOTE: This function is only used to adapt to VF2 with JH7110 SoC. Please
 * keep "motorcomm,tx-clk-adj-enabled" not exist in dts when the soc is not
 * JH7110.
 */
static void yt8531_link_change_notify(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	bool tx_clk_1000_inverted = false;
	bool tx_clk_100_inverted = false;
	bool tx_clk_10_inverted = false;
	bool tx_clk_adj_enabled = false;
	u16 val = 0;
	int ret;

	if (of_property_read_bool(node, "motorcomm,tx-clk-adj-enabled"))
		tx_clk_adj_enabled = true;

	if (!tx_clk_adj_enabled)
		return;

	if (of_property_read_bool(node, "motorcomm,tx-clk-10-inverted"))
		tx_clk_10_inverted = true;
	if (of_property_read_bool(node, "motorcomm,tx-clk-100-inverted"))
		tx_clk_100_inverted = true;
	if (of_property_read_bool(node, "motorcomm,tx-clk-1000-inverted"))
		tx_clk_1000_inverted = true;

	if (phydev->speed < 0)
		return;

	switch (phydev->speed) {
	case SPEED_1000:
		if (tx_clk_1000_inverted)
			val = YT8521_RC1R_TX_CLK_SEL_INVERTED;
		break;
	case SPEED_100:
		if (tx_clk_100_inverted)
			val = YT8521_RC1R_TX_CLK_SEL_INVERTED;
		break;
	case SPEED_10:
		if (tx_clk_10_inverted)
			val = YT8521_RC1R_TX_CLK_SEL_INVERTED;
		break;
	default:
		return;
	}

	ret = ytphy_modify_ext_with_lock(phydev, YT8521_RGMII_CONFIG1_REG,
					 YT8521_RC1R_TX_CLK_SEL_INVERTED, val);
	if (ret < 0)
		phydev_warn(phydev, "Modify TX_CLK_SEL err:%d\n", ret);
}

static struct phy_driver motorcomm_phy_drvs[] = {
	{
		PHY_ID_MATCH_EXACT(PHY_ID_YT8511),
		.name = "YT8511 Gigabit Ethernet",
		.config_init = yt8511_config_init,
		.suspend = genphy_suspend,
		.resume = genphy_resume,
		.read_page = yt8511_read_page,
		.write_page = yt8511_write_page,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_YT8531),
		.name = "YT8531 Gigabit Ethernet",
		.probe = yt8531_probe,
		.config_init = yt8531_config_init,
		.suspend = genphy_suspend,
		.resume = genphy_resume,
		.get_wol = ytphy_get_wol,
		.set_wol = yt8531_set_wol,
		.link_change_notify = yt8531_link_change_notify,
	},
};

module_phy_driver(motorcomm_phy_drvs);

MODULE_DESCRIPTION("Motorcomm 8511/8521/8531/8531S PHY driver");
MODULE_AUTHOR("Peter Geis");
MODULE_AUTHOR("Frank");
MODULE_LICENSE("GPL");

static const struct mdio_device_id __maybe_unused motorcomm_tbl[] = {
	{ PHY_ID_MATCH_EXACT(PHY_ID_YT8511) },
	{ PHY_ID_MATCH_EXACT(PHY_ID_YT8531) },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(mdio, motorcomm_tbl);
