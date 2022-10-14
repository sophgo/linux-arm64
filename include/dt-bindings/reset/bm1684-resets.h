/*
 * Bitmain SoCs Reset definitions
 *
 * Copyright (c) 2018 Bitmain Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _DT_BINDINGS_RST_BM1684_H_
#define _DT_BINDINGS_RST_BM1684_H_

#define RST_MAIN_AP                            0
#define RST_SECOND_AP                          1
#define RST_RISCV                              2
#define RST_DDR0_A_AXI                         3
#define RST_DDR0_B_AXI                         4
#define RST_DDR1_AXI                           5
#define RST_DDR2_AXI                           6
#define RST_DDR0_A_APB                         7
#define RST_DDR0_B_APB                         8
#define RST_DDR1_APB                           9
#define RST_DDR2_APB                           10
#define RST_DDR0_A_POWER_OK                    11
#define RST_DDR0_B_POWER_OK                    12
#define RST_DDR1_POWER_OK                      13
#define RST_DDR2_POWER_OK                      14
#define RST_GDMA                               15
#define RST_AXI_SRAM                           16
#define RST_TPU                                17
#define RST_ETH0                               18
#define RST_ETH1                               19
#define RST_EMMC                               20
#define RST_SD                                 21
#define RST_SDMA                               22
#define RST_UART0                              23
#define RST_UART1                              24
#define RST_UART2                              25
#define RST_I2C0                               26
#define RST_I2C1                               27
#define RST_I2C2                               28
#define RST_PWM                                29
#define RST_SPI                                30
#define RST_GPIO0                              31

#define RST_GPIO1                              32
#define RST_GPIO2                              33
#define RST_EFUSE                              34
#define RST_WDT                                35
#define RST_AHB_ROM                            36
#define RST_SPIC                               37
#define RST_TIMER                              38
#define RST_INTC0                              39
#define RST_INTC1                              40
#define RST_INTC2                              41
#define RST_INTC3                              42
#define RST_CDMA                               43
#define RST_MMU                                44
#define RST_PCIE                               45
#define RST_DBG_I2C                            46
#define RST_PKA                                47
#define RST_SPACC                              48
#define RST_TRNG                               49
#define RST_VIDEO_SYSTEM_0_WAVE0               50
#define RST_VIDEO_SYSTEM_0_WAVE1               51
#define RST_VIDEO_SYSTEM_0_JPEG0               52
#define RST_VIDEO_SYSTEM_0_JPEG1               53
#define RST_VIDEO_SYSTEM_0_VPP                 54
#define RST_VIDEO_SYSTEM_0_VMMU                55
#define RST_VIDEO_SYSTEM_0_JMMU                56
#define RST_VIDEO_SYSTEM_0_WMMU0               57
#define RST_VIDEO_SYSTEM_0_WMMU1               58
#define RST_VIDEO_SYSTEM_0_VMMU_DMA            59
#define RST_VIDEO_SYSTEM_1_WAVE0               60
#define RST_VIDEO_SYSTEM_1_WAVE1               61
#define RST_VIDEO_SYSTEM_1_JPEG0               62
#define RST_VIDEO_SYSTEM_1_JPEG1               63

#define RST_VIDEO_SYSTEM_1_VPP                 64
#define RST_VIDEO_SYSTEM_1_VMMU                65
#define RST_VIDEO_SYSTEM_1_JMMU                66
#define RST_VIDEO_SYSTEM_1_WMMU0               67
#define RST_VIDEO_SYSTEM_1_WMMU1               68
#define RST_VIDEO_SYSTEM_1_VMMU_DMA            69
#define RST_DDR0_A_DFI_CLOCK_DIV               70
#define RST_DDR0_B_DFI_CLOCK_DIV               71
#define RST_DDR1_DFI_CLOCK_DIV                 72
#define RST_DDR2_DFI_CLOCK_DIV                 73
#define RST_VIDEO_ENCODER_SUBSYS_WAVE521       74
#define RST_VIDEO_ENCODER_SUBSYS_WMMU          75
#define RST_VIDEO_ENCODER_SUBSYS_DEBUG_RESET   76
#define RST_VIDEO_SYSTEM_0_DEBUG_RESET         77
#define RST_VIDEO_SYSTEM_1_DEBUG_RESET         78
#define RST_HAU_NGS			       79
#define RST_TSDMA			       80

#define RST_MAX_NUM	(RST_TSDMA + 1)

#endif
