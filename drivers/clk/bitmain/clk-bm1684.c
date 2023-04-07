/*
 * Copyright (c) 2019 BITMAIN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/of_address.h>
#include <linux/mfd/syscon.h>
#include <dt-bindings/clock/bm1684-clock.h>

#include "clk.h"

/* fixed clocks */
struct bm_pll_clock bm1684_root_pll_clks[] = {
	{
		.id = FPLL_CLK,
		.name = "fpll_clock",
		.parent_name = "osc",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.ini_flags = BM_CLK_RO,
	}, {
		.id = DPLL0_CLK,
		.name = "dpll0_clock",
		.parent_name = "osc",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.ini_flags = BM_CLK_RO,
		.status_offset = 0xc0,
		.enable_offset = 0xc4,
	}, {
		.id = DPLL1_CLK,
		.name = "dpll1_clock",
		.parent_name = "osc",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.ini_flags = BM_CLK_RO,
		.status_offset = 0xc0,
		.enable_offset = 0xc4,
	}, {
		.id = MPLL_CLK,
		.name = "mpll_clock",
		.parent_name = "osc",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.status_offset = 0xc0,
		.enable_offset = 0xc4,
	}, {
		.id = TPLL_CLK,
		.name = "tpll_clock",
		.parent_name = "osc",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.status_offset = 0xc0,
		.enable_offset = 0xc4,
	}, {
		.id = VPLL_CLK,
		.name = "vpll_clock",
		.parent_name = "osc",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.status_offset = 0xc0,
		.enable_offset = 0xc4,
	},
};

/* divider clocks */
static const struct bm_divider_clock div_clks[] = {
	{ DIV_CLK_MPLL_A53_1, "clk_div_a53_1", "clk_gate_a53_div1",
		0, 0x840, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_MPLL_AXI_DDR_1, "clk_div_axi_ddr_1", "clk_gate_axi_ddr_div1",
		0, 0x8c8, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, 5},
	{ DIV_CLK_FPLL_DDR0_0, "clk_div_ddr0_0", "clk_gate_ddr0_div0",
		0, 0x8d4, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_INIT_VAL, },
	{ DIV_CLK_FPLL_DDR12_0, "clk_div_ddr12_0", "clk_gate_ddr12_div0",
		0, 0x8dc, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_INIT_VAL, },
	{ DIV_CLK_FPLL_A53_0, "clk_div_a53_0", "clk_gate_a53_div0",
		0, 0x844, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_50M_A53, "clk_div_50m_a53", "fpll_clock",
		0, 0x848, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_EMMC, "clk_div_emmc", "fpll_clock",
		0, 0x84c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_SD, "clk_div_sd", "fpll_clock",
		0, 0x854, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_TX_ETH0, "clk_div_tx_eth0", "fpll_clock",
		0, 0x85c, 16, 11, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_PTP_REF_I_ETH0, "clk_div_ptp_ref_i_eth0", "fpll_clock",
		0, 0x860, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_REF_ETH0, "clk_div_ref_eth0", "fpll_clock",
		0, 0x864, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_TX_ETH1, "clk_div_tx_eth1", "fpll_clock",
		0, 0x868, 16, 11, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_PTP_REF_I_ETH1, "clk_div_ptp_ref_i_eth1", "fpll_clock",
		0, 0x86c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_REF_ETH1, "clk_div_ref_eth1", "fpll_clock",
		0, 0x870, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_UART500M, "clk_div_uart500m", "fpll_clock",
		0, 0x874, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_EFUSE, "clk_div_efuse", "fpll_clock",
		0, 0x898, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_TPLL_TPU_0, "clk_div_tpu_0", "clk_gate_tpu_div0",
		0, 0x8a0, 16, 5, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_FIXED_TPU_CLK, "clk_div_fixed_tpu_clk", "fpll_clock",
		0, 0x8a8, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_AXI_VDE_WAVE_0, "clk_div_axi_vde_wave_0", "clk_gate_axi_vde_wave_div0",
		0, 0x8b0, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	/* clk axi3 is a readonly clock, any setting to axi3 is invalid
	 * but in order to show right clk frequency in debugfs,
	 * here we set axi3 rw attribute, so the CCF can write axi3's
	 * divison bits, even it take no effect.
	 */
	{ DIV_CLK_FPLL_AXI3, "clk_div_axi3", "fpll_clock",
		0, 0x8b4, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_AXI6, "clk_div_axi6", "fpll_clock",
		0, 0x8b8, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_AXI8, "clk_div_axi8", "fpll_clock",
		0, 0x8bc, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_AXI10_0, "clk_div_axi10_0", "clk_gate_axi10_div0",
		0, 0x8c4, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_AXI_DDR_0, "clk_div_axi_ddr_0", "clk_gate_axi_ddr_div0",
		0, 0x8cc, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, 5},
	{ DIV_CLK_FPLL_DIV_TIMER1, "clk_div_timer1", "clk_div_50m_a53",
		0, 0x878, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_DIV_TIMER2, "clk_div_timer2", "clk_div_50m_a53",
		0, 0x87c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_DIV_TIMER3, "clk_div_timer3", "clk_div_50m_a53",
		0, 0x880, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_DIV_TIMER4, "clk_div_timer4", "clk_div_50m_a53",
		0, 0x884, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_DIV_TIMER5, "clk_div_timer5", "clk_div_50m_a53",
		0, 0x888, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_DIV_TIMER6, "clk_div_timer6", "clk_div_50m_a53",
		0, 0x88c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_DIV_TIMER7, "clk_div_timer7", "clk_div_50m_a53",
		0, 0x890, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_DIV_TIMER8, "clk_div_timer8", "clk_div_50m_a53",
		0, 0x894, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_100K_EMMC, "clk_div_100k_emmc", "clk_div_ref_eth0",
		0, 0x850, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_100K_SD, "clk_div_100k_sd", "clk_div_ref_eth0",
		0, 0x858, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_GPIO_DB, "clk_div_gpio_db", "clk_div_ref_eth0",
		0, 0x89c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_TPU_1, "clk_div_tpu_1", "clk_gate_tpu_div1",
		0, 0x8a4, 16, 5, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_VPLL_AXI_VDE_WAVE_1, "clk_div_axi_vde_wave_1", "clk_gate_axi_vde_wave_div1",
		0, 0x8ac, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_VPLL_AXI10_1, "clk_div_axi10_1", "clk_gate_axi10_div1",
		0, 0x8c0, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_REG_VAL, },
	{ DIV_CLK_DPLL0_DDR0_1, "clk_div_ddr0_1", "clk_gate_ddr0_div1",
		0, 0x8d0, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_INIT_VAL, },
	{ DIV_CLK_DPLL1_DDR12_1, "clk_div_ddr12_1", "clk_gate_ddr12_div1",
		0, 0x8d8, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, BM_CLK_USE_INIT_VAL, },
};

/* gate clocks */
static const struct bm_gate_clock gate_clks[] = {
	{ GATE_CLK_A53_DIV1, "clk_gate_a53_div1", "mpll_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x820, 16, 0 },
	{ GATE_CLK_AXI_DDR_DIV1, "clk_gate_axi_ddr_div1", "mpll_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x820, 24, 0 },
	{ GATE_CLK_A53_DIV0, "clk_gate_a53_div0", "fpll_clock",
		CLK_IGNORE_UNUSED | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x820, 17, 0 },
	{ GATE_CLK_TPU_DIV0, "clk_gate_tpu_div0", "tpll_clock",
		0, 0x820, 18, 0 },
	{ GATE_CLK_AXI_VDE_WAVE_DIV0, "clk_gate_axi_vde_wave_div0", "fpll_clock",
		CLK_SET_RATE_PARENT, 0x820, 21, 0 },
	{ GATE_CLK_AXI10_DIV0, "clk_gate_axi10_div0", "fpll_clock",
		CLK_IGNORE_UNUSED, 0x820, 23, 0 },
	{ GATE_CLK_AXI_DDR_DIV0, "clk_gate_axi_ddr_div0", "fpll_clock",
		CLK_IGNORE_UNUSED, 0x820, 25, 0 },
	{ GATE_CLK_DDR0_DIV0, "clk_gate_ddr0_div0", "fpll_clock",
		CLK_IGNORE_UNUSED, 0x820, 27, 0 },
	{ GATE_CLK_DDR12_DIV0, "clk_gate_ddr12_div0", "fpll_clock",
		CLK_IGNORE_UNUSED, 0x820, 29, 0 },
	{ GATE_CLK_A53_50M, "clk_gate_a53_50m", "clk_div_50m_a53",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x800, 1, 0 },
	{ GATE_CLK_EMMC_200M, "clk_gate_emmc", "clk_div_emmc",
		CLK_SET_RATE_PARENT, 0x800, 3, 0 },
	{ GATE_CLK_SD_200M, "clk_gate_sd", "clk_div_sd",
		CLK_SET_RATE_PARENT, 0x800, 6, 0 },
	{ GATE_CLK_ETH0_TX, "clk_gate_tx_eth0", "clk_div_tx_eth0",
		CLK_SET_RATE_PARENT, 0x800, 8, 0 },
	{ GATE_CLK_PTP_REF_I_ETH0, "clk_gate_ptp_ref_i_eth0", "clk_div_ptp_ref_i_eth0",
		CLK_SET_RATE_PARENT, 0x800, 10, 0 },
	{ GATE_CLK_REF_ETH0, "clk_gate_ref_eth0", "clk_div_ref_eth0",
		CLK_SET_RATE_PARENT, 0x800, 11, 0 },
	{ GATE_CLK_ETH1_TX, "clk_gate_tx_eth1", "clk_div_tx_eth1",
		CLK_SET_RATE_PARENT, 0x800, 12, 0 },
	{ GATE_CLK_PTP_REF_I_ETH1, "clk_gate_ptp_ref_i_eth1", "clk_div_ptp_ref_i_eth1",
		CLK_SET_RATE_PARENT, 0x800, 14, 0 },
	{ GATE_CLK_REF_ETH1, "clk_gate_ref_eth1", "clk_div_ref_eth1",
		CLK_SET_RATE_PARENT, 0x800, 15, 0 },
	{ GATE_CLK_UART_500M, "clk_gate_uart_500m", "clk_div_uart500m",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x800, 17, 0 },
	{ GATE_CLK_EFUSE, "clk_gate_efuse", "clk_div_efuse",
		CLK_SET_RATE_PARENT, 0x800, 28, 0},
	{ GATE_CLK_FIXED_TPU_CLK, "clk_gate_fixed_tpu_clk", "clk_div_fixed_tpu_clk",
		CLK_SET_RATE_PARENT, 0x804, 14, 0 },
	{ GATE_CLK_AXI3, "clk_gate_axi3", "clk_div_axi3",
		0 | CLK_IS_CRITICAL, 0x808, 10, 0 },
	{ GATE_CLK_AXI6, "clk_gate_axi6", "clk_div_axi6",
		CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0x808, 11, 0 },
	{ GATE_CLK_AXI8, "clk_gate_axi8", "clk_div_axi8",
		CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0x808, 12, 0 },
	{ GATE_CLK_APB_INTC, "clk_gate_apb_intc", "clk_gate_axi3",
		CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x800, 18, 0 },
	{ GATE_CLK_APB_TIMER, "clk_gate_apb_timer", "clk_gate_axi3",
		0, 0x800, 19, 0 },
	{ GATE_CLK_APB_EFUSE, "clk_gate_apb_efuse", "clk_gate_axi3",
		0, 0x800, 29, 0 },
	{ GATE_CLK_APB_ROM, "clk_gate_apb_rom", "clk_gate_axi3",
		0, 0x800, 30, 0 },
	{ GATE_CLK_APB_GPIO, "clk_gate_apb_gpio", "clk_gate_axi3",
		0, 0x800, 31, 0 },
	{ GATE_CLK_AHB_SF, "clk_gate_ahb_sf", "clk_gate_axi3",
		0, 0x804, 2, 0 },
	{ GATE_CLK_APB_I2C, "clk_gate_apb_i2c", "clk_gate_axi3",
		0, 0x804, 3, 0 },
	{ GATE_CLK_APB_WDT, "clk_gate_apb_wdt", "clk_gate_axi3",
		0, 0x804, 4, 0 },
	{ GATE_CLK_APB_PWM, "clk_gate_apb_pwm", "clk_gate_axi3",
		0, 0x804, 5, 0 },
	{ GATE_CLK_APB_TRNG, "clk_gate_apb_trng", "clk_gate_axi3",
		0, 0x804, 6, 0 },
	{ GATE_CLK_APB_VD1_WAVE0, "clk_gate_apb_vd1_wave0", "clk_gate_axi3",
		0, 0x808, 0, 0 },
	{ GATE_CLK_APB_VD1_WAVE1, "clk_gate_apb_vd1_wave1", "clk_gate_axi3",
		0, 0x808, 2, 0 },
	{ GATE_CLK_APB_VD1_JPEG0, "clk_gate_apb_vd1_jpeg0", "clk_gate_axi3",
		0, 0x808, 4, 0 },
	{ GATE_CLK_APB_VD1_JPEG1, "clk_gate_apb_vd1_jpeg1", "clk_gate_axi3",
		0, 0x808, 6, 0 },
	{ GATE_CLK_APB_VD1_VPP, "clk_gate_apb_vd1_vpp", "clk_gate_axi3",
		0, 0x808, 8, 0 },
	{ GATE_CLK_APB_GPIO_INTR, "clk_gate_apb_gpio_intr", "clk_gate_axi3",
		0, 0x804, 0, 0 },
	{ GATE_CLK_APB_VDE_WAVE, "clk_gate_apb_vde_wave", "clk_gate_axi3",
		0, 0x804, 20, 0 },
	{ GATE_CLK_APB_VD0_WAVE0, "clk_gate_apb_vd0_wave0", "clk_gate_axi3",
		0, 0x804, 22, 0 },
	{ GATE_CLK_APB_VD0_WAVE1, "clk_gate_apb_vd0_wave1", "clk_gate_axi3",
		0, 0x804, 24, 0 },
	{ GATE_CLK_APB_VD0_JPEG0, "clk_gate_apb_vd0_jpeg0", "clk_gate_axi3",
		0, 0x804, 26, 0 },
	{ GATE_CLK_APB_VD0_JPEG1, "clk_gate_apb_vd0_jpeg1", "clk_gate_axi3",
		0, 0x804, 28, 0 },
	{ GATE_CLK_APB_VD0_VPP, "clk_gate_apb_vd0_vpp", "clk_gate_axi3",
		0, 0x804, 30, 0 },
	{ GATE_CLK_AXI_EMMC, "clk_gate_axi_emmc", "clk_gate_axi6",
		CLK_SET_RATE_PARENT, 0x800, 2, 0 },
	{ GATE_CLK_AXI_SD, "clk_gate_axi_sd", "clk_gate_axi6",
		CLK_SET_RATE_PARENT, 0x800, 5, 0 },
	{ GATE_CLK_AXI6_ETH0, "clk_gate_axi6_eth0", "clk_gate_axi6",
		CLK_SET_RATE_PARENT, 0x800, 9, 0 },
	{ GATE_CLK_AXI6_ETH1, "clk_gate_axi6_eth1", "clk_gate_axi6",
		CLK_SET_RATE_PARENT, 0x800, 13, 0 },
	{ GATE_CLK_SDMA_AXI, "clk_gate_sdma_axi", "clk_gate_axi6",
		CLK_SET_RATE_PARENT, 0x800, 16, 0, "pclk" },
	{ GATE_CLK_AXI8_MMU, "clk_gate_axi8_mmu", "clk_gate_axi8",
		CLK_SET_RATE_PARENT, 0x804, 7, 0 },
	{ GATE_CLK_AXI8_PCIE, "clk_gate_axi8_pcie", "clk_gate_axi8",
		CLK_SET_RATE_PARENT, 0x804, 8, 0 },
	{ GATE_CLK_AXI8_CDMA, "clk_gate_axi8_cdma", "clk_gate_axi8",
		CLK_SET_RATE_PARENT, 0x804, 9, 0 },
	{ GATE_CLK_TIMER1, "clk_gate_timer1", "clk_div_timer1",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x800, 20, 0 },
	{ GATE_CLK_TIMER2, "clk_gate_timer2", "clk_div_timer2",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x800, 21, 0 },
	{ GATE_CLK_TIMER3, "clk_gate_timer3", "clk_div_timer3",
		CLK_SET_RATE_PARENT, 0x800, 22, 0 },
	{ GATE_CLK_TIMER4, "clk_gate_timer4", "clk_div_timer4",
		CLK_SET_RATE_PARENT, 0x800, 23, 0 },
	{ GATE_CLK_TIMER5, "clk_gate_timer5", "clk_div_timer5",
		CLK_SET_RATE_PARENT, 0x800, 24, 0 },
	{ GATE_CLK_TIMER6, "clk_gate_timer6", "clk_div_timer6",
		CLK_SET_RATE_PARENT, 0x800, 25, 0 },
	{ GATE_CLK_TIMER7, "clk_gate_timer7", "clk_div_timer7",
		CLK_SET_RATE_PARENT, 0x800, 26, 0 },
	{ GATE_CLK_TIMER8, "clk_gate_timer8", "clk_div_timer8",
		CLK_SET_RATE_PARENT, 0x800, 27, 0 },
	{ GATE_CLK_100K_EMMC, "clk_gate_100k_emmc", "clk_div_100k_emmc",
		CLK_SET_RATE_PARENT, 0x800, 4, 0 },
	{ GATE_CLK_100K_SD, "clk_gate_100k_sd", "clk_div_100k_sd",
		CLK_SET_RATE_PARENT, 0x800, 7, 0 },
	{ GATE_CLK_GPIO_DB, "clk_gate_gpio_db", "clk_div_gpio_db",
		CLK_SET_RATE_PARENT, 0x804, 1, 0 },
	{ GATE_CLK_TPU_DIV1, "clk_gate_tpu_div1", "fpll_clock",
		CLK_SET_RATE_PARENT, 0x820, 19, 0 },
	{ GATE_CLK_AXI_VDE_WAVE_DIV1, "clk_gate_axi_vde_wave_div1",
		"vpll_clock", CLK_SET_RATE_PARENT, 0x820, 20, 0 },
	{ GATE_CLK_AXI10_DIV1, "clk_gate_axi10_div1", "vpll_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x820, 22, 0 },
	{ GATE_CLK_DDR0_DIV1, "clk_gate_ddr0_div1", "dpll0_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x820, 26, 0 },
	{ GATE_CLK_DDR12_DIV1, "clk_gate_ddr12_div1", "dpll1_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x820, 28, 0 },
	{ GATE_CLK_DDR0, "clk_gate_ddr0", "clk_mux_ddr0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x808, 15, 0 },
	{ GATE_CLK_DDR12, "clk_gate_ddr12", "clk_mux_ddr12",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x808, 16, 0 },
	{ GATE_CLK_A53, "clk_gate_a53", "clk_mux_a53",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x800, 0, 0 },
	{ GATE_CLK_TPU, "clk_gate_tpu", "clk_mux_tpu",
		CLK_SET_RATE_PARENT, 0x804, 13, 0 },
	{ GATE_CLK_AXI_VDE_WAVE, "clk_gate_axi_vde_wave", "clk_mux_vde_wave",
		CLK_SET_RATE_PARENT, 0x804, 19, 0 },
	{ GATE_CLK_AXI10, "clk_gate_axi10", "clk_mux_axi10",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x808, 13, 0 },
	{ GATE_CLK_AXI_VD1_VPP, "clk_gate_axi_vd1_vpp", "clk_gate_axi10",
		CLK_SET_RATE_PARENT, 0x808, 9, 0 },
	{ GATE_CLK_AXI_VD1_JPEG1, "clk_gate_axi_vd1_jpeg1", "clk_gate_axi10",
		CLK_SET_RATE_PARENT, 0x808, 7, 0 },
	{ GATE_CLK_AXI_VD1_JPEG0, "clk_gate_axi_vd1_jpeg0", "clk_gate_axi10",
		CLK_SET_RATE_PARENT, 0x808, 5, 0 },
	{ GATE_CLK_AXI_VD1_WAVE1, "clk_gate_axi_vd1_wave1", "clk_gate_axi10",
		CLK_SET_RATE_PARENT, 0x808, 3, 0 },
	{ GATE_CLK_AXI_VD1_WAVE0, "clk_gate_axi_vd1_wave0", "clk_gate_axi10",
		CLK_SET_RATE_PARENT, 0x808, 1, 0 },
	{ GATE_CLK_AXI_VD0_VPP, "clk_gate_axi_vd0_vpp", "clk_gate_axi10",
		CLK_SET_RATE_PARENT, 0x804, 31, 0 },
	{ GATE_CLK_AXI_VD0_JPEG1, "clk_gate_axi_vd0_jpeg1", "clk_gate_axi10",
		CLK_SET_RATE_PARENT, 0x804, 29, 0 },
	{ GATE_CLK_AXI_VD0_JPEG0, "clk_gate_axi_vd0_jpeg0", "clk_gate_axi10",
		CLK_SET_RATE_PARENT, 0x804, 27, 0 },
	{ GATE_CLK_AXI_VD0_WAVE1, "clk_gate_axi_vd0_wave1", "clk_gate_axi10",
		CLK_SET_RATE_PARENT, 0x804, 25, 0 },
	{ GATE_CLK_AXI_VD0_WAVE0, "clk_gate_axi_vd0_wave0", "clk_gate_axi10",
		CLK_SET_RATE_PARENT, 0x804, 23, 0 },
	{ GATE_CLK_AXI_VDE_AXI_BRIDGE, "clk_gate_axi_vde_axi_brige", "clk_gate_axi10",
		CLK_SET_RATE_PARENT, 0x804, 21, 0 },
	{ GATE_CLK_AXI_DDR, "clk_gate_axi_ddr", "clk_mux_axi_ddr",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x808, 14, 0 },
	{ GATE_CLK_RISCV, "clk_gate_riscv", "clk_gate_axi_ddr",
		CLK_SET_RATE_PARENT, 0x804, 18, 0 },
	{ GATE_CLK_AXI_SPACC, "clk_gate_axi_spacc", "clk_gate_axi_ddr",
		CLK_SET_RATE_PARENT, 0x804, 17, 0 },
	{ GATE_CLK_AXI_PKA, "clk_gate_axi_pka", "clk_gate_axi_ddr",
		CLK_SET_RATE_PARENT, 0x804, 16, 0 },
	{ GATE_CLK_AXI_DBG_I2C, "clk_gate_axi_dbg_i2c", "clk_gate_axi_ddr",
		CLK_SET_RATE_PARENT, 0x804, 15, 0 },
	{ GATE_CLK_GDMA, "clk_gate_gdma", "clk_gate_axi_ddr",
		CLK_SET_RATE_PARENT, 0x804, 12, 0 },
	{ GATE_CLK_AXISRAM, "clk_gate_axisram", "clk_gate_axi_ddr",
		CLK_SET_RATE_PARENT, 0x804, 11, 0 },
	{ GATE_CLK_ARM, "clk_gate_arm", "clk_gate_axi_ddr",
		CLK_SET_RATE_PARENT, 0x804, 10, 0 },
	{ GATE_CLK_HAU_NGS, "clk_gate_hau_ngs", "clk_gate_tpu",
		CLK_SET_RATE_PARENT, 0x808, 17, 0 },
	{ GATE_CLK_TSDMA, "clk_gate_tsdma", "clk_gate_tpu",
		CLK_SET_RATE_PARENT, 0x808, 18, 0 },
	{ GATE_CLK_TPU_FOR_TPU_ONLY, "clk_gate_tpu_for_tpu_only", "clk_gate_tpu",
		CLK_SET_RATE_PARENT, 0x808, 19, 0 },
};

/* mux clocks */
static const char *const clk_mux_ddr0_p[] = {
			"clk_div_ddr0_0", "clk_div_ddr0_1"};
static const char *const clk_mux_ddr12_p[] = {
			"clk_div_ddr12_0", "clk_div_ddr12_1"};
static const char *const clk_mux_a53_p[] = {
			"clk_div_a53_0", "clk_div_a53_1"};
// div1 is FPLL with selector bit = 0; div0 is TPLL with selector bit = 1 (default)
static const char *const clk_mux_tpu_p[] = {
			"clk_div_tpu_1", "clk_div_tpu_0"};
static const char *const clk_mux_vde_wave_p[] = {
			"clk_div_axi_vde_wave_0", "clk_div_axi_vde_wave_1"};
static const char *const clk_mux_axi10_p[] = {
			"clk_div_axi10_0", "clk_div_axi10_1"};
static const char *const clk_mux_axi_ddr_p[] = {
			"clk_div_axi_ddr_0", "clk_div_axi_ddr_1"};

struct bm_mux_clock mux_clks[] = {
	{
		MUX_CLK_DDR0, "clk_mux_ddr0", clk_mux_ddr0_p,
		ARRAY_SIZE(clk_mux_ddr0_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT |
			CLK_MUX_READ_ONLY,
		0x820, 5, 1, 0,
	},
	{
		MUX_CLK_DDR12, "clk_mux_ddr12", clk_mux_ddr12_p,
		ARRAY_SIZE(clk_mux_ddr12_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT |
			CLK_MUX_READ_ONLY,
		0x820, 6, 1, 0,
	},
	{
		MUX_CLK_A53, "clk_mux_a53", clk_mux_a53_p,
		ARRAY_SIZE(clk_mux_a53_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
		0x820, 0, 1, 0,
	},
	{
		MUX_CLK_TPU, "clk_mux_tpu", clk_mux_tpu_p,
		ARRAY_SIZE(clk_mux_tpu_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
		0x820, 1, 1, 0,
	},
	{
		MUX_CLK_VDE_WAVE, "clk_mux_vde_wave", clk_mux_vde_wave_p,
		ARRAY_SIZE(clk_mux_vde_wave_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
		0x820, 2, 1, 0,
	},
	{
		MUX_CLK_AXI10, "clk_mux_axi10", clk_mux_axi10_p,
		ARRAY_SIZE(clk_mux_axi10_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
		0x820, 3, 1, 0,
	},
	{
		MUX_CLK_AXI_DDR, "clk_mux_axi_ddr", clk_mux_axi_ddr_p,
		ARRAY_SIZE(clk_mux_axi_ddr_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
		0x820, 4, 1, 0,
	},
};

struct bm_clk_table pll_clk_tables = {
	.pll_clks_num = ARRAY_SIZE(bm1684_root_pll_clks),
	.pll_clks = bm1684_root_pll_clks,
};

struct bm_clk_table div_clk_tables[] = {
	{
		.id = DIV_CLK_TABLE,
		.div_clks_num = ARRAY_SIZE(div_clks),
		.div_clks = div_clks,
		.gate_clks_num = ARRAY_SIZE(gate_clks),
		.gate_clks = gate_clks,
	},
};

struct bm_clk_table mux_clk_tables[] = {
	{
		.id = MUX_CLK_TABLE,
		.mux_clks_num = ARRAY_SIZE(mux_clks),
		.mux_clks = mux_clks,
	},
};

static const struct of_device_id bm_clk_match_ids_tables[] = {
	{
		.compatible = "bitmain, pll-clock",
		.data = &pll_clk_tables,
	},
	{
		.compatible = "bitmain, pll-child-clock",
		.data = div_clk_tables,
	},
	{
		.compatible = "bitmain, pll-mux-clock",
		.data = mux_clk_tables,
	},
	{
		.compatible = "bitmain, clk-default-rates",
	},
	{}
};

static void __init bm1684_clk_init(struct device_node *node)
{
	struct device_node *np_top;
	struct bm_clk_data *clk_data = NULL;
	const struct bm_clk_table *dev_data;
	static struct regmap *syscon;
	static void __iomem *base;
	int i, ret = 0;
	unsigned int id;
	const struct of_device_id *match = NULL;

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data) {
		ret = -ENOMEM;
		goto out;
	}
	match = of_match_node(bm_clk_match_ids_tables, node);
	if (match) {
		dev_data = (struct bm_clk_table *)match->data;
	} else {
		pr_err("%s did't match node data\n", __func__);
		ret = -ENODEV;
		goto no_match_data;
	}

	spin_lock_init(&clk_data->lock);
	if (of_device_is_compatible(node, "bitmain, pll-clock")) {
		np_top = of_parse_phandle(node, "subctrl-syscon", 0);
		if (!np_top) {
			pr_err("%s can't get subctrl-syscon node\n",
				__func__);
			ret = -EINVAL;
			goto no_match_data;
		}

		if (!dev_data->pll_clks_num) {
			ret = -EINVAL;
			goto no_match_data;
		}

		syscon = syscon_node_to_regmap(np_top);
		if (IS_ERR_OR_NULL(syscon)) {
			pr_err("%s cannot get regmap %ld\n", __func__, PTR_ERR(syscon));
			ret = -ENODEV;
			goto no_match_data;
		}

		base = of_iomap(np_top, 0);
		clk_data->table = dev_data;
		clk_data->base = base;
		clk_data->syscon_top = syscon;

		if (of_property_read_u32(node, "id", &id)) {
			pr_err("%s cannot get pll id for %s\n",
				__func__, node->full_name);
			ret = -ENODEV;
			goto no_match_data;
		}
		ret = bm_register_pll_clks(node, clk_data, id);
	}

	if (of_device_is_compatible(node, "bitmain, pll-child-clock")) {
		ret = of_property_read_u32(node, "id", &id);
		if (ret) {
			pr_err("not assigned id for %s\n", node->full_name);
			ret = -ENODEV;
			goto no_match_data;
		}

		/* Below brute-force to check dts property "id"
		 * whether match id of array
		 */
		for (i = 0; i < ARRAY_SIZE(div_clk_tables); i++) {
			if (id == dev_data[i].id)
				break; /* found */
		}
		clk_data->table = &dev_data[i];
		clk_data->base = base;
		clk_data->syscon_top = syscon;
		ret = bm_register_div_clks(node, clk_data);
	}

	if (of_device_is_compatible(node, "bitmain, pll-mux-clock")) {
		ret = of_property_read_u32(node, "id", &id);
		if (ret) {
			pr_err("not assigned id for %s\n", node->full_name);
			ret = -ENODEV;
			goto no_match_data;
		}

		/* Below brute-force to check dts property "id"
		 * whether match id of array
		 */
		for (i = 0; i < ARRAY_SIZE(mux_clk_tables); i++) {
			if (id == dev_data[i].id)
				break; /* found */
		}
		clk_data->table = &dev_data[i];
		clk_data->base = base;
		clk_data->syscon_top = syscon;
		ret = bm_register_mux_clks(node, clk_data);
	}

	if (of_device_is_compatible(node, "bitmain, clk-default-rates"))
		ret = set_default_clk_rates(node);

	if (!ret)
		return;

no_match_data:
	kfree(clk_data);

out:
	pr_err("%s failed error number %d\n", __func__, ret);
}

CLK_OF_DECLARE(bm_clk_pll, "bitmain, pll-clock", bm1684_clk_init);
CLK_OF_DECLARE(bm_clk_pll_child, "bitmain, pll-child-clock", bm1684_clk_init);
CLK_OF_DECLARE(bm_clk_pll_mux, "bitmain, pll-mux-clock", bm1684_clk_init);
CLK_OF_DECLARE(bm_clk_default_rate, "bitmain, clk-default-rates", bm1684_clk_init);
