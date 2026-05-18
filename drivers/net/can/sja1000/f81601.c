// SPDX-License-Identifier: GPL-2.0
/* Fintek F81601 PCIE to 2 CAN controller driver
 *
 * Copyright (C) 2019 Peter Hong <peter_hong@fintek.com.tw>
 * Copyright (C) 2019 Linux Foundation
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/can/dev.h>
#include <linux/io.h>
#include <linux/version.h>
#include <linux/irqreturn.h>
#include <linux/can/dev.h>
#include <linux/can/platform/sja1000.h>

#define DIABLE_CANLED 1 // for RHEL9 kernel 5.14

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
#include <linux/can/skb.h>
#endif

#define DRV_VER		"v1.21_20250311"
#define DRV_NAME	"f81601"

#ifndef GENMASK
#define GENMASK(h, l) \
	(((~0UL) - (1UL << (l)) + 1) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#endif

#ifndef DEVICE_ATTR_RO
#define DEVICE_ATTR_RO(_name) \
	struct device_attribute dev_attr_##_name = __ATTR_RO(_name)
#endif

#define F81601_PCI_MAX_CHAN	2
#define F81601_ACCESS_MEM_MODE	0	// if non-x86, may change to 1
#define F81601_REG_SAVE_SIZE	0x20
#define F81601_DECODE_REG	0x209
#define F81601_TX_GUARD_TIME	msecs_to_jiffies(1/*00*/)
//#define F81601_RX_GUARD_TIME	usecs_to_jiffies(50)
#define F81601_RX_GUARD_TIME	0UL // 100L // us
#define F81601_BUSOFF_GUARD_TIME	msecs_to_jiffies(100)
#define DEBUG_IRQ_DELAY		0
#define RMC_CHANGE_RELEASE	1
#define REDUCE_HANG		0
#define REDUCE_HANG_CNT		20000//3000//5000//5000//20000//20000
#define F81601_OE_RESET		0
#define F81601_OE_RESET_CNT	2
#define F81601_DEFAULT_CLK	24000000

#define USE_CUSTOM_SJA1000

#define F81601_IS_TXING		BIT(0)

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
#define CAN_CTRLMODE_PRESUME_ACK	0x40	/* Ignore missing CAN ACKs */
#endif

#define SJA1000_ECHO_SKB_MAX	1 /* the SJA1000 has one TX buffer object */

/* SJA1000 registers - manual section 6.4 (Pelican Mode) */
#define SJA1000_MOD		0x00
#define SJA1000_CMR		0x01
#define SJA1000_SR		0x02
#define SJA1000_IR		0x03
#define SJA1000_IER		0x04
#define SJA1000_ALC		0x0B
#define SJA1000_ECC		0x0C
#define SJA1000_EWL		0x0D
#define SJA1000_RXERR		0x0E
#define SJA1000_TXERR		0x0F
#define SJA1000_ACCC0		0x10
#define SJA1000_ACCC1		0x11
#define SJA1000_ACCC2		0x12
#define SJA1000_ACCC3		0x13
#define SJA1000_ACCM0		0x14
#define SJA1000_ACCM1		0x15
#define SJA1000_ACCM2		0x16
#define SJA1000_ACCM3		0x17
#define SJA1000_RMC		0x1D
#define SJA1000_RBSA		0x1E

/* Common registers - manual section 6.5 */
#define SJA1000_BTR0		0x06
#define SJA1000_BTR1		0x07
#define SJA1000_OCR		0x08
#define SJA1000_CDR		0x1F

#define SJA1000_FI		0x10
#define SJA1000_SFF_BUF		0x13
#define SJA1000_EFF_BUF		0x15

#define SJA1000_FI_FF		0x80
#define SJA1000_FI_RTR		0x40

#define SJA1000_ID1		0x11
#define SJA1000_ID2		0x12
#define SJA1000_ID3		0x13
#define SJA1000_ID4		0x14

#define SJA1000_CAN_RAM		0x20

/* mode register */
#define MOD_RM		0x01
#define MOD_LOM		0x02
#define MOD_STM		0x04
#define MOD_AFM		0x08
#define MOD_SM		0x10

/* commands */
#define CMD_SRR		0x10
#define CMD_CDO		0x08
#define CMD_RRB		0x04
#define CMD_AT		0x02
#define CMD_TR		0x01

/* interrupt sources */
#define IRQ_BEI		0x80
#define IRQ_ALI		0x40
#define IRQ_EPI		0x20
#define IRQ_WUI		0x10
#define IRQ_DOI		0x08
#define IRQ_EI		0x04
#define IRQ_TI		0x02
#define IRQ_RI		0x01
#define IRQ_ALL		0xFF
#define IRQ_OFF		0x00

/* status register content */
#define SR_BS		0x80
#define SR_ES		0x40
#define SR_TS		0x20
#define SR_RS		0x10
#define SR_TCS		0x08
#define SR_TBS		0x04
#define SR_DOS		0x02
#define SR_RBS		0x01

#define SR_CRIT (SR_BS|SR_ES)

/* ECC register */
#define ECC_SEG		0x1F
#define ECC_DIR		0x20
#define ECC_ERR		6
#define ECC_BIT		0x00
#define ECC_FORM	0x40
#define ECC_STUFF	0x80
#define ECC_MASK	0xc0

/*
 * SJA1000 private data structure
 */
struct sja1000_priv {
	struct can_priv can;	/* must be the first member */
	struct sk_buff *echo_skb;

	/* the lower-layer is responsible for appropriate locking */
	u8 (*read_reg) (const struct sja1000_priv *priv, int reg);
	void (*write_reg) (const struct sja1000_priv *priv, int reg, u8 val);
	void (*write_mask_reg) (const struct sja1000_priv *priv, int reg, u8 mask, u8 val);
	void (*pre_irq) (const struct sja1000_priv *priv);
	void (*post_irq) (const struct sja1000_priv *priv);

	void *priv;		/* for board-specific data */
	struct net_device *dev;

	void __iomem *reg_base;	 /* ioremap'ed address to registers */
	unsigned long irq_flags; /* for request_irq() */
	raw_spinlock_t rx_lock;
	spinlock_t tx_lock;

	u16 flags;		/* custom mode flags */
	u8 ocr;			/* output control register */
	u8 cdr;			/* clock divider register */

	struct delayed_work tx_delayed_work;
	struct delayed_work busoff_delayed_work;
	bool is_read_more_rx;
	unsigned int force_tx_resend, tx_resend_cnt, tx_size;
#if REDUCE_HANG
	unsigned int rx_wait_release_cnt;
	unsigned int max_rx_wait_release_cnt;
#endif

	struct tasklet_struct rx_tasklet;
	unsigned int max_rmc;

#if F81601_OE_RESET
	int rx_buff_start_addr;
	int rx_oe_cnt;
#endif
};

struct f81601_pci_card {
	int channels;			/* detected channels count */
	u8 decode_cfg;
	u8 reg_table[F81601_PCI_MAX_CHAN][F81601_REG_SAVE_SIZE];
	//void __iomem *addr_io;
	//void __iomem *addr_mem;
	void __iomem *addr;
	spinlock_t lock;
	struct pci_dev *dev;
	struct net_device *net_dev[F81601_PCI_MAX_CHAN];

	bool is_internal;
	bool is_new_ic;
	uint chosen_clock;
};

static const struct pci_device_id f81601_pci_tbl[] = {
	{PCI_DEVICE(0x1c29, 0x1703), .driver_data = 2},
	{},
};

MODULE_DEVICE_TABLE(pci, f81601_pci_tbl);

static bool enable_mem_access = F81601_ACCESS_MEM_MODE;
module_param(enable_mem_access, bool, S_IRUGO);
MODULE_PARM_DESC(enable_mem_access, "Enable device MMIO access, default 0");

static bool enable_msi = 1;
module_param(enable_msi, bool, S_IRUGO);
MODULE_PARM_DESC(enable_msi, "Enable device MSI handle, default 1");

static unsigned int max_msi_ch = 2;
module_param(max_msi_ch, uint, S_IRUGO);
MODULE_PARM_DESC(max_msi_ch, "Max MSI channel, default 2");

static int internal_clk = -1;
module_param(internal_clk, int, S_IRUGO);
MODULE_PARM_DESC(internal_clk, "Use internal clock, default -1 (self detect int/ext clk)");

static unsigned int external_clk = F81601_DEFAULT_CLK;
module_param(external_clk, uint, S_IRUGO);
MODULE_PARM_DESC(external_clk, "External Clock, must spec when internal_clk = 0, default = 24000000");

static unsigned int bus_restart_ms = 3000;//0;
module_param(bus_restart_ms, uint, S_IRUGO);
MODULE_PARM_DESC(bus_restart_ms, "override default bus_restart_ms timer");

static unsigned int force_tx_send_cnt = 0;
module_param(force_tx_send_cnt, uint, S_IRUGO);
MODULE_PARM_DESC(force_tx_send_cnt, "force_tx_send_cnt");

static unsigned int rx_guard_time = F81601_RX_GUARD_TIME;
module_param(rx_guard_time, uint, S_IRUGO);
MODULE_PARM_DESC(rx_guard_time, "rx_guard_time");

static unsigned int bitrate_protect = 0;//250000;
module_param(bitrate_protect, uint, S_IRUGO);
MODULE_PARM_DESC(bitrate_protect, "bitrate_protect");

static unsigned int rx_normal_mode = 0;
module_param(rx_normal_mode, uint, S_IRUGO);
MODULE_PARM_DESC(rx_normal_mode, "RX not direct read FIFO, using SJA1000 method");

static unsigned int bypass_parent_bridge_check = 0;
module_param(bypass_parent_bridge_check, uint, S_IRUGO);
MODULE_PARM_DESC(bypass_parent_bridge_check, "Don't check parent PCIe switch");

static unsigned int rx_tasklet_en = 1;
module_param(rx_tasklet_en, uint, S_IRUGO);
MODULE_PARM_DESC(rx_tasklet_en, "RX change from ISR to defer work");

static unsigned int rx_release_poll_cnt = REDUCE_HANG_CNT;
module_param(rx_release_poll_cnt, uint, S_IRUGO);
MODULE_PARM_DESC(rx_release_poll_cnt, "RX release poll count");

static unsigned int rx_fifo_rmc = 0;
module_param(rx_fifo_rmc, uint, S_IRUGO);
MODULE_PARM_DESC(rx_fifo_rmc, "rx_fifo_rmc");

static unsigned int auto_affinity = 1;
module_param(auto_affinity, uint, S_IRUGO);
MODULE_PARM_DESC(auto_affinity, "auto_affinity");

static unsigned int force_sjw_max = 1;
module_param(force_sjw_max, uint, S_IRUGO);
MODULE_PARM_DESC(force_sjw_max, "force_sjw_max");

static unsigned int more_err_report = 0;
module_param(more_err_report, uint, S_IRUGO);
MODULE_PARM_DESC(more_err_report, "more_err_report");

static irqreturn_t f81601_interrupt(int irq, void *dev_id);
static void sja1000_start(struct net_device *dev);

#define USEC	1000LL
#define MSEC	(1000 * USEC)
#define SEC	(1000 * MSEC)

#ifndef get_can_dlc
#define get_can_dlc can_cc_dlc2len
#endif

#if 0
static void time_start(struct timespec *start)
{
	getnstimeofday(start);
}

static unsigned long long time_end(struct timespec *start)
{
	long long secs, nsecs;
	struct timespec stop;

	getnstimeofday(&stop);
	
	secs = stop.tv_sec - start->tv_sec;
	nsecs = stop.tv_nsec - start->tv_nsec;
	if (nsecs < 0) {
		secs--;
		nsecs += SEC;
	}

	return secs * SEC + nsecs;
}
#endif

static bool is_f81601_can_running(struct sja1000_priv *priv)
{
	switch (priv->can.state) {
	case CAN_STATE_ERROR_ACTIVE:
	case CAN_STATE_ERROR_WARNING:
	case CAN_STATE_ERROR_PASSIVE:
		return true;
	default:
		return false;
	}

	return false;
}

static void sja1000_write_cmdreg_rx_multiple(struct sja1000_priv *priv, u8 val, int count)
{
	struct can_bittiming *bt = &priv->can.bittiming;
	int i = rx_release_poll_cnt, j;
	u8 rmc;

	if (val == CMD_RRB) {
		if (priv->is_read_more_rx && bt->bitrate > bitrate_protect) {
#if RMC_CHANGE_RELEASE
			rmc = priv->read_reg(priv, SJA1000_RMC);

			while ((priv->read_reg(priv, SJA1000_SR) & SR_RS) && --i) {
				if (priv->read_reg(priv, SJA1000_RMC) != rmc)
					break;
			}
#else
			while ((priv->read_reg(priv, SJA1000_SR) & SR_RS) && --i)
				;//udelay(1);
#endif
		}

		for (j = 0; j < count; ++j)
			priv->write_reg(priv, SJA1000_CMR, CMD_RRB);

#if REDUCE_HANG
		if (rx_release_poll_cnt)
			priv->rx_wait_release_cnt += (rx_release_poll_cnt - i);

		if (priv->rx_wait_release_cnt >= priv->max_rx_wait_release_cnt)
			priv->max_rx_wait_release_cnt = priv->rx_wait_release_cnt;
#endif

		return;

	}

	priv->write_reg(priv, SJA1000_CMR, val);
}

static void sja1000_write_cmdreg(struct sja1000_priv *priv, u8 val)
{
	sja1000_write_cmdreg_rx_multiple(priv, val, 1);
}

static int sja1000_err(struct net_device *dev, uint8_t isrc, uint8_t status)
{
	struct sja1000_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	enum can_state state = priv->can.state;
	enum can_state rx_state, tx_state;
	unsigned int rxerr, txerr;
	uint8_t ecc, alc;
#if F81601_OE_RESET
	bool is_oe = false;
#endif

	skb = alloc_can_err_skb(dev, &cf);
	if (skb == NULL)
		return -ENOMEM;

	txerr = priv->read_reg(priv, SJA1000_TXERR);
	rxerr = priv->read_reg(priv, SJA1000_RXERR);

	cf->data[6] = txerr;
	cf->data[7] = rxerr;

	if (isrc & IRQ_DOI) {
		/* data overrun interrupt */
#if F81601_OE_RESET
		u8 rbsa = priv->read_reg(priv, SJA1000_RBSA);

		if (priv->rx_buff_start_addr == rbsa) {
			priv->rx_oe_cnt++;

			if (priv->rx_oe_cnt >= F81601_OE_RESET_CNT)
				is_oe = true;
		} else {
			priv->rx_buff_start_addr = rbsa;
			priv->rx_oe_cnt = 0;
		}
#endif
		netdev_dbg(dev, "data overrun interrupt\n");
		cf->can_id |= CAN_ERR_CRTL;
		cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
		stats->rx_over_errors++;
		stats->rx_errors++;
		sja1000_write_cmdreg(priv, CMD_CDO);	/* clear bit */
	}

	if (isrc & IRQ_EI) {
		/* error warning interrupt */
		netdev_dbg(dev, "error warning interrupt\n");

		if (status & SR_BS)
			state = CAN_STATE_BUS_OFF;
		else if (status & SR_ES)
			state = CAN_STATE_ERROR_WARNING;
		else
			state = CAN_STATE_ERROR_ACTIVE;
	}
	if (isrc & IRQ_BEI) {
		/* bus error interrupt */
		priv->can.can_stats.bus_error++;
		stats->rx_errors++;

		ecc = priv->read_reg(priv, SJA1000_ECC);

		cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;

		/* set error type */
		switch (ecc & ECC_MASK) {
		case ECC_BIT:
			cf->data[2] |= CAN_ERR_PROT_BIT;
			break;
		case ECC_FORM:
			cf->data[2] |= CAN_ERR_PROT_FORM;
			break;
		case ECC_STUFF:
			cf->data[2] |= CAN_ERR_PROT_STUFF;
			break;
		default:
			break;
		}

		/* set error location */
		cf->data[3] = ecc & ECC_SEG;

		/* Error occurred during transmission? */
		if ((ecc & ECC_DIR) == 0)
			cf->data[2] |= CAN_ERR_PROT_TX;
	}
	if (isrc & IRQ_EPI) {
		/* error passive interrupt */
		netdev_dbg(dev, "error passive interrupt\n");

		if (state == CAN_STATE_ERROR_PASSIVE)
			state = CAN_STATE_ERROR_WARNING;
		else
			state = CAN_STATE_ERROR_PASSIVE;
	}
	if (isrc & IRQ_ALI) {
		/* arbitration lost interrupt */
		netdev_dbg(dev, "arbitration lost interrupt\n");
		alc = priv->read_reg(priv, SJA1000_ALC);
		priv->can.can_stats.arbitration_lost++;
		stats->tx_errors++;
		cf->can_id |= CAN_ERR_LOSTARB;
		cf->data[0] = alc & 0x1f;
	}

	if (state != priv->can.state) {
		tx_state = txerr >= rxerr ? state : 0;
		rx_state = txerr <= rxerr ? state : 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
		can_change_state(dev, cf, tx_state, rx_state);
#else
		if (state == CAN_STATE_ERROR_WARNING) {
			priv->can.can_stats.error_warning++;
			cf->data[1] = (txerr > rxerr) ?
				CAN_ERR_CRTL_TX_WARNING :
				CAN_ERR_CRTL_RX_WARNING;
		} else {
			priv->can.can_stats.error_passive++;
			cf->data[1] = (txerr > rxerr) ?
				CAN_ERR_CRTL_TX_PASSIVE :
				CAN_ERR_CRTL_RX_PASSIVE;
		}
#endif

		if(state == CAN_STATE_BUS_OFF)
			can_bus_off(dev);
	}

#if F81601_OE_RESET
	if (is_oe && priv->can.state != CAN_STATE_BUS_OFF) {
		netdev_dbg(dev, "oe\n");
		priv->rx_oe_cnt = 0;
		priv->can.state = CAN_STATE_BUS_OFF;
		priv->write_reg(priv, SJA1000_IER, 0);
		cf->can_id |= CAN_ERR_BUSOFF;

#if 0
		struct can_priv *priv_can = netdev_priv(dev);

		netif_carrier_off(dev);

		schedule_delayed_work(&priv_can->restart_work,
			msecs_to_jiffies(3000));
#else
		can_bus_off(dev);
#endif
	}
#endif

	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;
	netif_rx(skb);

	return 0;
}

#define FIFO_ADDR(x)	((x) % 64)

static void sja1000_rx_from_fifo(struct net_device *dev)
{
	struct sja1000_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	uint8_t fi, rmc, rbsa, remain_rmc, cur_rmc, q_rmc;
	uint8_t dreg;
	canid_t id;
	int i, j;
	bool debug_en = false;
	bool rx_rmc_release = false;

	//if (dev->dev_id == 0)
	//	debug_en = true;

	remain_rmc = rmc = priv->read_reg(priv, SJA1000_RMC);
	rbsa = priv->read_reg(priv, SJA1000_RBSA);
	q_rmc = 0;

#if 0
	if (remain_rmc > priv->max_rmc) {
		priv->max_rmc = remain_rmc;
		netdev_info(dev, "max_rmc: %d\n", remain_rmc);
	}
#endif

	//if (remain_rmc >= 2)
	//	remain_rmc = rmc = 2;

	if (debug_en)
		netdev_info(dev, "rmc: %d 000\n", rmc);

	for (i = 0; i < rmc; ++i) {
		skb = alloc_can_skb(dev, &cf);
		if (skb == NULL) {
			netdev_err(dev, "alloc_can_skb %d failed\n", i);
			return;
		}

		cur_rmc = priv->read_reg(priv, SJA1000_RMC);

		fi = priv->read_reg(priv, 32 + FIFO_ADDR(rbsa));
		if (fi & SJA1000_FI_FF) {
			/* extended frame format (EFF) */
			dreg = (SJA1000_EFF_BUF - SJA1000_FI) + rbsa;
			id = (priv->read_reg(priv, 32 + FIFO_ADDR(rbsa + 1)) << 21)
			    | (priv->read_reg(priv, 32 + FIFO_ADDR(rbsa + 2)) << 13)
			    | (priv->read_reg(priv, 32 + FIFO_ADDR(rbsa + 3)) << 5)
			    | (priv->read_reg(priv, 32 + FIFO_ADDR(rbsa + 4)) >> 3);
			id |= CAN_EFF_FLAG;
		} else {
			/* standard frame format (SFF) */
			dreg = (SJA1000_SFF_BUF - SJA1000_FI) + rbsa;
			id = (priv->read_reg(priv, 32 + FIFO_ADDR(rbsa + 1)) << 3)
			    | (priv->read_reg(priv, 32 + FIFO_ADDR(rbsa + 2)) >> 5);
		}

		cf->can_dlc = get_can_dlc(fi & 0x0F);
		if (fi & SJA1000_FI_RTR) {
			id |= CAN_RTR_FLAG;
		} else {
			for (j = 0; j < cf->can_dlc; j++)
				cf->data[j] = priv->read_reg(priv, 32 + FIFO_ADDR(dreg++));
		}

		q_rmc++;

		if (rx_fifo_rmc && cur_rmc != priv->read_reg(priv, SJA1000_RMC))
			rx_rmc_release = true;

		if (rx_rmc_release || !(priv->read_reg(priv, SJA1000_SR) & SR_RS)) {
			// release multiple
			for (j = 0; j < q_rmc; ++j) {
				priv->write_reg(priv, SJA1000_CMR, CMD_RRB);
				remain_rmc--;
			}

			q_rmc = 0;

			if (debug_en)
				netdev_info(dev, "rmc: %d, remain_rmc: %d 222\n", rmc, remain_rmc);
		}

		cf->can_id = id;
		rbsa = dreg;
		stats->rx_packets++;
		stats->rx_bytes += cf->can_dlc;
		netif_rx(skb);	
	}

	/* release receive buffer */
#if 0
	if (remain_rmc) {
		for (i = 0; i < remain_rmc; ++i) {
			sja1000_write_cmdreg(priv, CMD_RRB);
		}
	} else {
#if REDUCE_HANG
		if (priv->rx_wait_release_cnt > rx_release_poll_cnt / 4)
			priv->rx_wait_release_cnt -= rx_release_poll_cnt / 4;
		else
			priv->rx_wait_release_cnt = 0;
#endif
	}
#else
	if (remain_rmc) {
		sja1000_write_cmdreg_rx_multiple(priv, CMD_RRB, remain_rmc);
	} else {
#if REDUCE_HANG
		if (priv->rx_wait_release_cnt > rx_release_poll_cnt / 4)
			priv->rx_wait_release_cnt -= rx_release_poll_cnt / 4;
		else
			priv->rx_wait_release_cnt = 0;
#endif
	}
#endif

	if (debug_en)
		netdev_info(dev, "remain_rmc: %d all released\n", remain_rmc);
#if !DIABLE_CANLED && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
	can_led_event(dev, CAN_LED_EVENT_RX);
#endif
}

static void sja1000_rx_from_normal(struct net_device *dev)
{
	struct sja1000_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	uint8_t fi;
	uint8_t dreg;
	canid_t id;
	int i;

	/* create zero'ed CAN frame buffer */
	skb = alloc_can_skb(dev, &cf);
	if (skb == NULL) {
		netdev_err(dev, "alloc_can_skb failed\n");
		return;
	}

	fi = priv->read_reg(priv, SJA1000_FI);

	if (fi & SJA1000_FI_FF) {
		/* extended frame format (EFF) */
		dreg = SJA1000_EFF_BUF;
		id = (priv->read_reg(priv, SJA1000_ID1) << 21)
		    | (priv->read_reg(priv, SJA1000_ID2) << 13)
		    | (priv->read_reg(priv, SJA1000_ID3) << 5)
		    | (priv->read_reg(priv, SJA1000_ID4) >> 3);
		id |= CAN_EFF_FLAG;
	} else {
		/* standard frame format (SFF) */
		dreg = SJA1000_SFF_BUF;
		id = (priv->read_reg(priv, SJA1000_ID1) << 3)
		    | (priv->read_reg(priv, SJA1000_ID2) >> 5);
	}

	cf->can_dlc = get_can_dlc(fi & 0x0F);
	if (fi & SJA1000_FI_RTR) {
		id |= CAN_RTR_FLAG;
	} else {
		for (i = 0; i < cf->can_dlc; i++)
			cf->data[i] = priv->read_reg(priv, dreg++);
	}

	cf->can_id = id;

	/* release receive buffer */
	sja1000_write_cmdreg(priv, CMD_RRB);

	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;
	netif_rx(skb);
#if !DIABLE_CANLED && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
	can_led_event(dev, CAN_LED_EVENT_RX);
#endif
}

static void sja1000_rx(struct net_device *dev)
{
	struct sja1000_priv *priv = netdev_priv(dev);

	if (priv->is_read_more_rx && !rx_normal_mode)
		sja1000_rx_from_fifo(dev);
	else
		sja1000_rx_from_normal(dev);
}

static int sja1000_is_absent(struct sja1000_priv *priv)
{
	return (priv->read_reg(priv, SJA1000_MOD) == 0xFF);
}

static void set_reset_mode(struct net_device *dev)
{
	struct sja1000_priv *priv = netdev_priv(dev);
	unsigned char status = priv->read_reg(priv, SJA1000_MOD);
	int i;

	/* disable interrupts */
	priv->write_reg(priv, SJA1000_IER, IRQ_OFF);

	for (i = 0; i < 100; i++) {
		/* check reset bit */
		if (status & MOD_RM) {
			priv->can.state = CAN_STATE_STOPPED;
			return;
		}

		/* reset chip */
		priv->write_reg(priv, SJA1000_MOD, MOD_RM);
		udelay(10);
		status = priv->read_reg(priv, SJA1000_MOD);
	}

	netdev_err(dev, "setting SJA1000 into reset mode failed!\n");
}

#ifdef USE_CUSTOM_SJA1000
static const struct can_bittiming_const sja1000_bittiming_const = {
	.name = DRV_NAME,
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 64,
	.brp_inc = 1,
};

static const struct can_bittiming_const sja1000_new_ic_bittiming_const = {
	.name = DRV_NAME,
	.tseg1_min = 1,
	.tseg1_max = 16 * 4,
	.tseg2_min = 1,
	.tseg2_max = 8 * 4,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 64,
	.brp_inc = 1,
};

#if 0
static void start_hrtimer_us(struct hrtimer *hrt, unsigned long usec)
{
	//unsigned long sec = usec / 1000000;
	//unsigned long nsec = (usec % 1000000) * 1000;
	//ktime_t t = ktime_set(sec, nsec);

	//pr_info("%s: %lld\n", __func__, ktime_to_ns(t));
	hrtimer_start(hrt, ns_to_ktime(usec * 1000L), HRTIMER_MODE_REL_PINNED);
}
#endif

static int sja1000_probe_chip(struct net_device *dev)
{
	struct sja1000_priv *priv = netdev_priv(dev);

	if (priv->reg_base && sja1000_is_absent(priv)) {
		netdev_err(dev, "probing failed\n");
		return 0;
	}
	return -1;
}

static void set_normal_mode(struct net_device *dev)
{
	struct sja1000_priv *priv = netdev_priv(dev);
	u8 mod_reg_val = 0x00, ier;
	int i, retry_cnt = 10;
	unsigned char status = priv->read_reg(priv, SJA1000_MOD);

#if F81601_OE_RESET	
	priv->rx_buff_start_addr = priv->rx_oe_cnt = 0;
#endif

#if REDUCE_HANG
	priv->rx_wait_release_cnt = priv->max_rx_wait_release_cnt = 0;
#endif

	for (i = 0; i < retry_cnt; i++) {
		/* check reset bit */
		if ((status & MOD_RM) == 0) {
			priv->can.state = CAN_STATE_ERROR_ACTIVE;
			/* enable interrupts */
			if (priv->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING)
				ier = IRQ_ALL;
			else
				ier = IRQ_ALL & ~IRQ_BEI;

			if (!more_err_report)
				ier &= ~(IRQ_ALI | IRQ_BEI);
			else
				ier |= IRQ_ALI | IRQ_BEI;

			priv->write_reg(priv, SJA1000_IER, ier);
			return;
		}

		/* set chip to normal mode */	
		if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
			mod_reg_val |= MOD_LOM;
		if ((priv->can.ctrlmode & CAN_CTRLMODE_PRESUME_ACK) ||
			priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK)
			mod_reg_val |= MOD_STM;
		priv->write_reg(priv, SJA1000_MOD, mod_reg_val);

		udelay(10);

		status = priv->read_reg(priv, SJA1000_MOD);
	}

	netdev_err(dev, "setting SJA1000 into normal mode failed!\n");
}

/*
 * initialize SJA1000 chip:
 *   - reset chip
 *   - set output mode
 *   - set baudrate
 *   - enable interrupts
 *   - start operating mode
 */
static void chipset_init(struct net_device *dev)
{
	struct sja1000_priv *priv = netdev_priv(dev);

	/* set clock divider and output control register */
	priv->write_reg(priv, SJA1000_CDR, priv->cdr | CDR_PELICAN);

	/* set acceptance filter (accept all) */
	priv->write_reg(priv, SJA1000_ACCC0, 0x00);
	priv->write_reg(priv, SJA1000_ACCC1, 0x00);
	priv->write_reg(priv, SJA1000_ACCC2, 0x00);
	priv->write_reg(priv, SJA1000_ACCC3, 0x00);

	priv->write_reg(priv, SJA1000_ACCM0, 0xFF);
	priv->write_reg(priv, SJA1000_ACCM1, 0xFF);
	priv->write_reg(priv, SJA1000_ACCM2, 0xFF);
	priv->write_reg(priv, SJA1000_ACCM3, 0xFF);

	priv->write_reg(priv, SJA1000_OCR, priv->ocr | OCR_MODE_NORMAL);
}

static void sja1000_start(struct net_device *dev)
{
	struct sja1000_priv *priv = netdev_priv(dev);
	//struct can_bittiming *bt = &priv->can.bittiming;

	/* leave reset mode */
	//if (priv->can.state != CAN_STATE_STOPPED)
	set_reset_mode(dev);

	/* Initialize chip if uninitialized at this stage */
	//if (!(priv->read_reg(priv, SJA1000_CDR) & CDR_PELICAN))
	chipset_init(dev);

	/* Clear error counters and error code capture */
	priv->write_reg(priv, SJA1000_TXERR, 0x0);
	priv->write_reg(priv, SJA1000_RXERR, 0x0);
	priv->read_reg(priv, SJA1000_ECC);
	priv->read_reg(priv, SJA1000_ALC);

	/* clear interrupt flags */
	priv->read_reg(priv, SJA1000_IR);

	//cancel_delayed_work_sync(&priv->tx_delayed_work);
	cancel_delayed_work(&priv->tx_delayed_work);

	/* leave reset mode */
	set_normal_mode(dev);

	cancel_delayed_work(&priv->busoff_delayed_work);
	schedule_delayed_work(&priv->busoff_delayed_work, F81601_BUSOFF_GUARD_TIME);
	//cancel_delayed_work_sync(&priv->busoff_delayed_work);
}

static int sja1000_set_mode(struct net_device *dev, enum can_mode mode)
{
	switch (mode) {
	case CAN_MODE_START:
		sja1000_start(dev);
		if (netif_queue_stopped(dev))
			netif_wake_queue(dev);
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int sja1000_set_bittiming(struct net_device *dev)
{
	struct sja1000_priv *priv = netdev_priv(dev);
	struct can_bittiming *bt = &priv->can.bittiming;
	struct pci_dev *pdev = to_pci_dev(dev->dev.parent);
	struct f81601_pci_card *card = pci_get_drvdata(pdev);
	u8 btr0, btr1, force_sjw, seg1, seg2;

	seg1 = bt->prop_seg + bt->phase_seg1 - 1;
	seg2 = bt->phase_seg2 - 1;

	btr0 = ((bt->brp - 1) & 0x3f) | (((bt->sjw - 1) & 0x3) << 6);
	btr1 = ((bt->prop_seg + bt->phase_seg1 - 1) & 0xf) |
		(((bt->phase_seg2 - 1) & 0x7) << 4);
	if (priv->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES)
		btr1 |= 0x80;

	netdev_info(dev, "brp: (%d)%xh, seg1: (%d)%xh, seg2: (%d)%xh\n", bt->brp,
			bt->brp, bt->prop_seg + bt->phase_seg1,
			bt->prop_seg + bt->phase_seg1,
			bt->phase_seg2, bt->phase_seg2);

	if (force_sjw_max) {
		force_sjw = min_t(int, bt->phase_seg2 - 1, 3);

		btr0 &= ~(BIT(7) | BIT(6));
		btr0 |= force_sjw << 6;

		//pr_info("%s: %x %x\n", __func__, btr0, force_sjw << 6);
	}

	netdev_info(dev, "setting BTR0=0x%02x BTR1=0x%02x\n", btr0, btr1);

	priv->write_reg(priv, SJA1000_BTR0, btr0);
	priv->write_reg(priv, SJA1000_BTR1, btr1);

	if (card->is_new_ic) {
		priv->write_mask_reg(priv, 0x79, GENMASK(1, 0), seg1 >> 4);
		priv->write_mask_reg(priv, 0x79, GENMASK(3, 2), seg2 >> 1);
	}

	return 0;
}

static int sja1000_get_berr_counter(const struct net_device *dev,
				    struct can_berr_counter *bec)
{
	struct sja1000_priv *priv = netdev_priv(dev);

	bec->txerr = priv->read_reg(priv, SJA1000_TXERR);
	bec->rxerr = priv->read_reg(priv, SJA1000_RXERR);

	return 0;
}

/*
 * transmit a CAN message
 * message layout in the sk_buff should be like this:
 * xx xx xx xx	 ff	 ll   00 11 22 33 44 55 66 77
 * [  can-id ] [flags] [len] [can data (up to 8 bytes]
 */
static netdev_tx_t sja1000_start_xmit(struct sk_buff *skb,
					    struct net_device *dev)
{
	//struct pci_dev *pdev = to_pci_dev(dev->dev.parent);
	//struct f81601_pci_card *card = pci_get_drvdata(pdev);
	struct sja1000_priv *priv = netdev_priv(dev);
	struct can_frame *cf = (struct can_frame *)skb->data;
	uint8_t fi;
	uint8_t dlc;
	canid_t id;
	uint8_t dreg;
	u8 cmd_reg_val = 0x00;
	int i;
	unsigned char status;
	int max_wait = 20000;
	unsigned long flags;

	if (can_dropped_invalid_skb(dev, skb)) {
		netdev_info(dev, "%s: can_dropped_invalid_skb\n", __func__);
		return NETDEV_TX_OK;
	}

	spin_lock_irqsave(&priv->tx_lock, flags);

	cancel_delayed_work(&priv->tx_delayed_work);
	netif_stop_queue(dev);

	for (i = 0; i < max_wait; ++i) {
		if (priv->can.state >= CAN_STATE_BUS_OFF) {
			netdev_info(dev, "%s: busoff\n", __func__);
			spin_unlock_irqrestore(&priv->tx_lock, flags);
			return NETDEV_TX_OK;
		}

		status = priv->read_reg(priv, SJA1000_SR);
		if ((status & (SR_TBS | SR_TS)) == SR_TBS)
			break;
	}

	if (i >= max_wait) {
		dev->stats.tx_dropped++;
		netdev_dbg(dev, "%s: bus busy: %d, %d\n", __func__, i, priv->can.state);
		spin_unlock_irqrestore(&priv->tx_lock, flags);

		schedule_delayed_work(&priv->tx_delayed_work, F81601_TX_GUARD_TIME);

		return NETDEV_TX_BUSY;
	}

	priv->tx_size = fi = dlc = cf->can_dlc;
	id = cf->can_id;

	if (id & CAN_RTR_FLAG)
		fi |= SJA1000_FI_RTR;

	if (id & CAN_EFF_FLAG) {
		fi |= SJA1000_FI_FF;
		dreg = SJA1000_EFF_BUF;
		priv->write_reg(priv, SJA1000_FI, fi);
		priv->write_reg(priv, SJA1000_ID1, (id & 0x1fe00000) >> 21);
		priv->write_reg(priv, SJA1000_ID2, (id & 0x001fe000) >> 13);
		priv->write_reg(priv, SJA1000_ID3, (id & 0x00001fe0) >> 5);
		priv->write_reg(priv, SJA1000_ID4, (id & 0x0000001f) << 3);
	} else {
		dreg = SJA1000_SFF_BUF;
		priv->write_reg(priv, SJA1000_FI, fi);
		priv->write_reg(priv, SJA1000_ID1, (id & 0x000007f8) >> 3);
		priv->write_reg(priv, SJA1000_ID2, (id & 0x00000007) << 5);
	}

	for (i = 0; i < dlc; i++)
		priv->write_reg(priv, dreg++, cf->data[i]);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
	can_put_echo_skb(skb, dev, 0, 0);
#else
	can_put_echo_skb(skb, dev, 0);
#endif

	if (priv->can.ctrlmode & CAN_CTRLMODE_ONE_SHOT)
		cmd_reg_val |= CMD_AT;

	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK)
		cmd_reg_val |= CMD_SRR;
	else
		cmd_reg_val |= CMD_TR;

	priv->flags |= F81601_IS_TXING;
	sja1000_write_cmdreg(priv, cmd_reg_val);
	schedule_delayed_work(&priv->tx_delayed_work, F81601_TX_GUARD_TIME);

	spin_unlock_irqrestore(&priv->tx_lock, flags);

	return NETDEV_TX_OK;
}

static int sja1000_open(struct net_device *dev)
{
	//struct pci_dev *pdev = to_pci_dev(dev->dev.parent);
	//struct sja1000_priv *priv = netdev_priv(dev);
	//struct f81601_pci_card *card = pci_get_drvdata(pdev);
	int err;

	//netdev_info(dev, "%s: in\n", __func__);

	/* set chip into reset mode */
	set_reset_mode(dev);

	/* common open */
	err = open_candev(dev);
	if (err)
		return err;

#if 0
	/* register interrupt handler, if not done by the device driver */
	//err = request_threaded_irq(dev->irq, NULL, handler, priv->irq_flags,
	//				dev->name, (void *)dev);
	err = request_irq(dev->irq, f81601_interrupt, priv->irq_flags,
				dev->name, (void *)dev);
	if (err) {
		close_candev(dev);
		return -EAGAIN;
	}
#endif

	/* init and start chi */
	sja1000_start(dev);
#if !DIABLE_CANLED && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
	can_led_event(dev, CAN_LED_EVENT_OPEN);
#endif
	netif_start_queue(dev);

	return 0;
}

static int sja1000_close(struct net_device *dev)
{
	struct sja1000_priv *priv = netdev_priv(dev);

	//netdev_info(dev, "%s: in\n", __func__);

	set_reset_mode(dev);
	cancel_delayed_work_sync(&priv->tx_delayed_work);
	netif_stop_queue(dev);

	//synchronize_irq(dev->irq);
	//free_irq(dev->irq, (void *)dev);
	cancel_delayed_work(&priv->busoff_delayed_work);
	close_candev(dev);

#if !DIABLE_CANLED && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
	can_led_event(dev, CAN_LED_EVENT_STOP);
#endif
	return 0;
}

static struct net_device *alloc_sja1000dev(int sizeof_priv, bool is_new_ic)
{
	struct net_device *dev;
	struct sja1000_priv *priv;

	dev = alloc_candev(sizeof(struct sja1000_priv) + sizeof_priv,
		SJA1000_ECHO_SKB_MAX);
	if (!dev)
		return NULL;

	priv = netdev_priv(dev);

	priv->dev = dev;

	if (is_new_ic)
		priv->can.bittiming_const = &sja1000_new_ic_bittiming_const;
	else
		priv->can.bittiming_const = &sja1000_bittiming_const;
	priv->can.do_set_bittiming = sja1000_set_bittiming;
	priv->can.do_set_mode = sja1000_set_mode;
	priv->can.do_get_berr_counter = sja1000_get_berr_counter;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK |
				       CAN_CTRLMODE_LISTENONLY |
				       CAN_CTRLMODE_3_SAMPLES |
				       CAN_CTRLMODE_ONE_SHOT |
				       CAN_CTRLMODE_BERR_REPORTING |
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
				       CAN_CTRLMODE_CC_LEN8_DLC |
#endif
				       CAN_CTRLMODE_PRESUME_ACK;

	raw_spin_lock_init(&priv->rx_lock);
	spin_lock_init(&priv->tx_lock);

	if (sizeof_priv)
		priv->priv = (void *)priv + sizeof(struct sja1000_priv);

	return dev;
}

static void free_sja1000dev(struct net_device *dev)
{
	free_candev(dev);
}

static void f81601_busoff_delayed_work(struct work_struct *work)
{
	struct sja1000_priv *priv;
	struct net_device *netdev;
	struct net_device_stats *stats;
	u8 sr;
	//bool debug = false;
	struct sk_buff *skb;
	struct can_frame *cf;

	priv = container_of(work, struct sja1000_priv, busoff_delayed_work.work);
	netdev = priv->dev;
	stats = &netdev->stats;

	//if (netdev->dev_id == 0)
	//	debug = true;

	cancel_delayed_work(&priv->busoff_delayed_work);

	if (priv->can.state >= CAN_STATE_BUS_OFF) {
		netdev_dbg(netdev, "%s: busoff\n", __func__);
		return;
	}

	sr = priv->read_reg(priv, SJA1000_SR);
	if ((sr & SR_CRIT) != SR_CRIT) {
		schedule_delayed_work(&priv->busoff_delayed_work, F81601_BUSOFF_GUARD_TIME);
		return;
	}

	skb = alloc_can_err_skb(netdev, &cf);
	if (skb == NULL) {
		schedule_delayed_work(&priv->busoff_delayed_work, F81601_BUSOFF_GUARD_TIME);
		netdev_warn(netdev, "%s: nomem\n", __func__);
		return;
	}

	priv->can.state = CAN_STATE_BUS_OFF;

	cf->can_id |= CAN_ERR_BUSOFF;
	cf->data[6] = priv->read_reg(priv, SJA1000_TXERR);
	cf->data[7] = priv->read_reg(priv, SJA1000_RXERR);

	can_bus_off(netdev);

	//if (debug)
	netdev_warn(netdev, "%s: busoff, restart timer: %d\n", __func__, priv->can.restart_ms);

	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;
	netif_rx(skb);
}

static const struct net_device_ops sja1000_netdev_ops = {
	.ndo_open	= sja1000_open,
	.ndo_stop	= sja1000_close,
	.ndo_start_xmit	= sja1000_start_xmit,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)	
	.ndo_change_mtu	= can_change_mtu,
#endif	
};

static int register_sja1000dev(struct net_device *dev)
{
	int ret;

	if (!sja1000_probe_chip(dev))
		return -ENODEV;

	dev->flags |= IFF_ECHO;	/* we support local echo */
	dev->netdev_ops = &sja1000_netdev_ops;

	set_reset_mode(dev);
	chipset_init(dev);

	ret = register_candev(dev);

#if !DIABLE_CANLED && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
	if (!ret)
		devm_can_led_init(dev);
#endif
	return ret;
}

static void unregister_sja1000dev(struct net_device *dev)
{
	set_reset_mode(dev);
	unregister_candev(dev);
}
#endif

#if REDUCE_HANG
static void f81601_disable_rx_int(struct sja1000_priv *priv)
{
	priv->write_reg(priv, SJA1000_IER, priv->read_reg(priv, SJA1000_IER) & ~IRQ_RI);
}
#endif

static void f81601_enable_rx_int(struct sja1000_priv *priv)
{
	priv->write_reg(priv, SJA1000_IER, priv->read_reg(priv, SJA1000_IER) | IRQ_RI);
}

static irqreturn_t f81601_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	//struct pci_dev *pdev = to_pci_dev(dev->dev.parent);
	//struct f81601_pci_card *card = pci_get_drvdata(pdev);
	struct sja1000_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	uint8_t isrc, status, mod;
	int n = 0, r = 0, result;
	bool is_err = false;
	unsigned long flags = 0;
	bool en_debuf = false;
	int do_wakeup;

#if DEBUG_IRQ_DELAY
	unsigned long long elapse;
	struct timespec start;

	if (dev->dev_id == 0) {
		//en_debuf = true;
		time_start(&start);
	}
#endif

	/* Shared interrupts and IRQ off? */
	if (priv->read_reg(priv, SJA1000_IER) == IRQ_OFF) {
		//netdev_info(dev, "IRQ_OFF\n");
		goto out;
	}

	if (en_debuf)
		netdev_info(dev, "IRQ in, sr: %02x, 00h: %02x\n", priv->read_reg(priv, SJA1000_SR), priv->read_reg(priv, SJA1000_MOD));

	while (/*(n < max_retry) &&*/
			(isrc = priv->read_reg(priv, SJA1000_IR))) {
		status = priv->read_reg(priv, SJA1000_SR);
		if (en_debuf)
			netdev_info(dev, "isrc: %02x, sr: %02x, 00h: %02x\n", isrc, status, priv->read_reg(priv, SJA1000_MOD));

		/* check for absent controller due to hw unplug */
		if (status == 0xFF && sja1000_is_absent(priv)) {
			netdev_info(dev, "sja1000_is_absent\n");
			goto out;
		}
		
		if (isrc & (IRQ_DOI | IRQ_EI | IRQ_BEI | IRQ_EPI | IRQ_ALI)) {
			n++;
			is_err = true;

			/* error interrupt */
			sja1000_err(dev, isrc, status);
		}

		if (isrc & IRQ_RI) {
#if 0
			// rx_tasklet_en discr
			if (!rx_tasklet_en) {
				raw_spin_lock_irqsave(&priv->rx_lock, flags);

				status = priv->read_reg(priv, SJA1000_SR);

				/* receive interrupt */
				while (status & SR_RBS) {
					//netdev_info(dev, "RX\n");
					sja1000_rx(dev);

					status = priv->read_reg(priv, SJA1000_SR);
					/* check for absent controller */
					if (status == 0xFF && sja1000_is_absent(priv)) {
						netdev_info(dev, "sja1000_is_absent\n");
						raw_spin_unlock_irqrestore(&priv->rx_lock, flags);
						goto out;
					}
					
					n++;
					r++;
#if REDUCE_HANG
					if (priv->rx_wait_release_cnt >= rx_release_poll_cnt) {
						priv->write_reg(priv, SJA1000_IER, priv->read_reg(priv, SJA1000_IER) & ~IRQ_RI);
						break;
					}
#endif
				}

				raw_spin_unlock_irqrestore(&priv->rx_lock, flags);
			} else {
				if (is_f81601_can_running(priv)) {
					f81601_disable_rx_int(priv);
					tasklet_schedule(&priv->rx_tasklet);
					n++;
					r++;
				}
			}
#else

			if (priv->is_read_more_rx)
				raw_spin_lock_irqsave(&priv->rx_lock, flags);

			status = priv->read_reg(priv, SJA1000_SR);

			/* receive interrupt */
			while (status & SR_RBS) {
				//netdev_info(dev, "RX\n");
				sja1000_rx(dev);

				status = priv->read_reg(priv, SJA1000_SR);
				/* check for absent controller */
				if (status == 0xFF && sja1000_is_absent(priv)) {
					netdev_info(dev, "sja1000_is_absent\n");

					if (priv->is_read_more_rx)
						raw_spin_unlock_irqrestore(&priv->rx_lock, flags);
					goto out;
				}
				
				n++;
				r++;
#if REDUCE_HANG
				
				if (rx_tasklet_en && priv->rx_wait_release_cnt >= rx_release_poll_cnt) {
					f81601_disable_rx_int(priv);

					if (is_f81601_can_running(priv)) {
						tasklet_schedule(&priv->rx_tasklet);
					} else {
						netdev_info(dev, "rx retrigger\n");
					}
					
					break;
				}
				
#endif
			}

			if (priv->is_read_more_rx)
				raw_spin_unlock_irqrestore(&priv->rx_lock, flags);
			

#endif
		}

		if (isrc & IRQ_WUI) {
			netdev_dbg(dev, "wakeup interrupt\n");
			n++;
		}

		if (isrc & IRQ_TI) {
			n++;
			do_wakeup = 0;

			if ((priv->can.ctrlmode & CAN_CTRLMODE_ONE_SHOT) || priv->force_tx_resend) {
				priv->force_tx_resend = 0;
				do_wakeup = 1;
			} else if (status & SR_TCS) {
				do_wakeup = 1;
			} else {
				priv->tx_resend_cnt++;
				stats->tx_errors++;
				netdev_dbg(dev, "%s: tx_resend_cnt: %d\n", __func__, priv->tx_resend_cnt);
			}

			if (force_tx_send_cnt > 1 && priv->tx_resend_cnt >= force_tx_send_cnt) {
				priv->force_tx_resend = 1;
				priv->tx_resend_cnt = 0;
				netdev_dbg(dev, "%s: force abort\n", __func__);
				sja1000_write_cmdreg(priv, CMD_AT | CMD_TR);
			}

			if (do_wakeup) {
				spin_lock_irqsave(&priv->tx_lock, flags);

				if (priv->flags & F81601_IS_TXING) {
					/* transmission buffer released */
					if ((priv->can.ctrlmode & CAN_CTRLMODE_ONE_SHOT) || (status & SR_TCS)) {
						/* transmission complete */
						stats->tx_bytes += priv->tx_size;
						stats->tx_packets++;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
						result = can_get_echo_skb(dev, 0, NULL);
#else
						result = can_get_echo_skb(dev, 0);
#endif
						//netdev_info(dev, "%s: tx int tcs success\n", __func__);
					} else {
						stats->tx_errors++;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0)
						can_free_echo_skb(dev, 0, NULL);
#else
						can_free_echo_skb(dev, 0);
#endif
						//netdev_info(dev, "%s: tx int tcs fail\n", __func__);
					}

					cancel_delayed_work(&priv->tx_delayed_work);
					netif_wake_queue(dev);
#if !DIABLE_CANLED && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
					can_led_event(dev, CAN_LED_EVENT_TX);
#endif
					priv->flags &= ~F81601_IS_TXING;
				}

				spin_unlock_irqrestore(&priv->tx_lock, flags);				
			}

		}

#if 0
		if (isrc & IRQ_DOI) {
			netdev_info(dev, "IRQ_DOI, isrc: %02x, n: %d, r: %d\n", isrc, n, r);
			set_reset_mode(dev);
		}
#endif
	}

	if (isrc == 0 && n == 0) {
		struct can_frame *cf;
		struct sk_buff *skb;
		//enum can_state state = priv->can.state;
		//enum can_state rx_state, tx_state;

		status = priv->read_reg(priv, SJA1000_SR);
		mod = priv->read_reg(priv, SJA1000_MOD);

		if (mod & BIT(0)) {
			skb = alloc_can_err_skb(dev, &cf);

			priv->can.state = CAN_STATE_BUS_OFF;
			can_bus_off(dev);

			if (skb == NULL) {
				netdev_warn(dev, "%s: nomem\n", __func__);
			} else {
				cf->can_id |= CAN_ERR_BUSOFF;
				cf->data[6] = priv->read_reg(priv, SJA1000_TXERR);
				cf->data[7] = priv->read_reg(priv, SJA1000_RXERR);

				stats->rx_packets++;
				stats->rx_bytes += cf->can_dlc;
				netif_rx(skb);
			}
		}
	}

	if (!rx_tasklet_en)
		priv->write_reg(priv, SJA1000_IER, priv->read_reg(priv, SJA1000_IER) | IRQ_RI);

out:

#if DEBUG_IRQ_DELAY
	if (en_debuf) {
		elapse = time_end(&start);

		netdev_info(dev, "IRQ OUT elapse: %llu, n: %d, r: %d\n", elapse, n, r);
	}
#else
	if (en_debuf)
		netdev_info(dev, "IRQ OUT\n");

#endif

	return (n) ? IRQ_HANDLED : IRQ_NONE;
}

static void f81601_tx_delayed_work(struct work_struct *work)
{
	struct sja1000_priv *priv;
	struct net_device *netdev;
	unsigned long flags;
	u8 sr;
	int r;

	priv = container_of(work, struct sja1000_priv, tx_delayed_work.work);
	netdev = priv->dev;

	netdev_dbg(netdev, "%s: into\n", __func__);

	if (priv->can.state >= CAN_STATE_BUS_OFF) {
		netdev_dbg(netdev, "%s: busoff\n", __func__);
		return;
	}

	spin_lock_irqsave(&priv->tx_lock, flags);

	if (!(priv->flags & F81601_IS_TXING)) {
		spin_unlock_irqrestore(&priv->tx_lock, flags);
		return;
	}

	sr = priv->read_reg(priv, SJA1000_SR);
	if ((sr & (SR_TBS | SR_TS)) != SR_TBS) {
		netdev_dbg(netdev, "%s: not idle, schedule next\n", __func__);

		cancel_delayed_work(&priv->tx_delayed_work);
		schedule_delayed_work(&priv->tx_delayed_work, F81601_TX_GUARD_TIME);
		spin_unlock_irqrestore(&priv->tx_lock, flags);
		return;
	}

	if (!(priv->can.ctrlmode & CAN_CTRLMODE_ONE_SHOT) && !(sr & SR_TCS)) {
		netdev_dbg(netdev, "%s: not completed tx, schedule next\n", __func__);

		cancel_delayed_work(&priv->tx_delayed_work);
		schedule_delayed_work(&priv->tx_delayed_work, F81601_TX_GUARD_TIME);
		spin_unlock_irqrestore(&priv->tx_lock, flags);
		return;
	}

	netdev_dbg(netdev, "%s: wake tx queue, %x %x %x %x\n", __func__,
		priv->read_reg(priv, SJA1000_MOD),
		priv->read_reg(priv, SJA1000_SR),
		priv->read_reg(priv, SJA1000_TXERR),
		priv->read_reg(priv, SJA1000_RXERR));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
	r = can_get_echo_skb(netdev, 0, NULL);
#else
	r = can_get_echo_skb(netdev, 0);
#endif

	netif_wake_queue(netdev);
#if !DIABLE_CANLED && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
	can_led_event(netdev, CAN_LED_EVENT_TX);
#endif

	priv->flags &= ~F81601_IS_TXING;
	spin_unlock_irqrestore(&priv->tx_lock, flags);
}

static u8 f81601_pci_io_read_reg(const struct sja1000_priv *priv, int port)
{
	return ioread8(priv->reg_base + port);
}

static void f81601_pci_io_write_reg(const struct sja1000_priv *priv, int port, u8 val)
{
	iowrite8(val, priv->reg_base + port);
}

static u8 f81601_pci_mmio_read_reg(const struct sja1000_priv *priv, int port)
{
	return readb(priv->reg_base + port);
}

static u8 f81601_raw_can_reg_read(struct pci_dev *pdev, u8 can_id, u8 port)
{
	struct f81601_pci_card *card = pci_get_drvdata(pdev);

	if (enable_mem_access)
		return readb(card->addr + 0x80 * can_id + port);
	else
		return ioread8(card->addr + 0x80 * can_id + port);
}

#if 0
static u32 f81601_raw_can_reg_dword_read(struct pci_dev *pdev, u8 can_id, u8 port)
{
	struct f81601_pci_card *card = pci_get_drvdata(pdev);

	if (enable_mem_access)
		return readq(card->addr + 0x80 * can_id + port);
	else
		return ioread32(card->addr + 0x80 * can_id + port);
}
#endif

static void f81601_raw_can_reg_write(struct pci_dev *pdev, u8 can_id, u8 port, u8 data)
{
	struct f81601_pci_card *card = pci_get_drvdata(pdev);
	unsigned long flags;

	if (enable_mem_access) {
		spin_lock_irqsave(&card->lock, flags);
		writeb(data, card->addr + 0x80 * can_id + port);
		readb(card->addr + 0x80 * can_id + port);
		spin_unlock_irqrestore(&card->lock, flags);
	} else {
		iowrite8(data, card->addr + 0x80 * can_id + port);
	}
}

static void f81601_pci_mmio_write_lock_reg(const struct sja1000_priv *priv, int port, u8 val)
{
	struct f81601_pci_card *card = priv->priv;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	writeb(val, priv->reg_base + port);
	readb(priv->reg_base);
	spin_unlock_irqrestore(&card->lock, flags);
}

static void f81601_pci_mmio_write_reg(const struct sja1000_priv *priv, int port, u8 val)
{
	writeb(val, priv->reg_base + port);
}

static void f81601_write_mask_reg(const struct sja1000_priv *priv, int reg, u8 mask, u8 val)
{
	u8 tmp;

	if (!priv->read_reg || !priv->write_reg) {
		netdev_err(priv->dev, "read/write reg func null\n");
		return;
	}

	tmp = priv->read_reg(priv, reg);

	tmp &= ~mask;
	tmp |= (mask & val);

	priv->write_reg(priv, reg, tmp);
}

static ssize_t read_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
	u8 tmp;

	pci_read_config_byte(pdev, 0x20a, &tmp);
	tmp &= GENMASK(2, 0);

	return sprintf(buf, "%x\n", tmp);
}

static DEVICE_ATTR_RO(read_id);

static ssize_t rx_release_cnt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
#if REDUCE_HANG
	struct net_device *netdev = container_of(dev, struct net_device, dev);
	struct sja1000_priv *priv = netdev_priv(netdev);
	return sprintf(buf, "%d\n", priv->rx_wait_release_cnt);
#else
	return sprintf(buf, "none\n");
#endif
}

static DEVICE_ATTR_RO(rx_release_cnt);

static ssize_t rx_release_cnt_max_show(struct device *dev, struct device_attribute *attr, char *buf)
{
#if REDUCE_HANG
	struct net_device *netdev = container_of(dev, struct net_device, dev);
	struct sja1000_priv *priv = netdev_priv(netdev);

	return sprintf(buf, "%d\n", priv->max_rx_wait_release_cnt);
#else
	return sprintf(buf, "none\n");
#endif
}

static DEVICE_ATTR_RO(rx_release_cnt_max);

static void f81601_rx_tasklet(unsigned long data)
{
	struct sja1000_priv *priv = (void*) data;
	struct net_device *dev = priv->dev;
	unsigned long flags = 0;
	bool is_break = false;

	//netdev_info(dev, "%s\n", __func__);

	while (priv->read_reg(priv, SJA1000_SR) & SR_RBS) {
		if (!is_f81601_can_running(priv))
			break;

		if (priv->is_read_more_rx)
			raw_spin_lock_irqsave(&priv->rx_lock, flags);

		sja1000_rx(dev);

#if REDUCE_HANG
		if (priv->rx_wait_release_cnt >= rx_release_poll_cnt) {
			if (priv->is_read_more_rx)
				raw_spin_unlock_irqrestore(&priv->rx_lock, flags);
			is_break = true;
			break;
		}
#endif

		if (priv->is_read_more_rx)
			raw_spin_unlock_irqrestore(&priv->rx_lock, flags);
	}

	if (is_break) {
		tasklet_schedule(&priv->rx_tasklet);
	} else {
		if (is_f81601_can_running(priv))
			f81601_enable_rx_int(priv);
	}
}

static void f81601_pci_del_card(struct pci_dev *pdev)
{
	struct f81601_pci_card *card = pci_get_drvdata(pdev);
	struct sja1000_priv *priv;
	struct net_device *dev;
	int i = 0;

	device_remove_file(&pdev->dev, &dev_attr_read_id);

	for (i = 0; i < F81601_PCI_MAX_CHAN; i++) {
		dev = card->net_dev[i];
		if (!dev)
			continue;

		dev_info(&pdev->dev, "Removing %s\n", dev->name);

		priv = netdev_priv(dev);
		tasklet_disable(&priv->rx_tasklet);
		tasklet_kill(&priv->rx_tasklet);

		device_remove_file(&dev->dev, &dev_attr_rx_release_cnt);
		device_remove_file(&dev->dev, &dev_attr_rx_release_cnt_max);

		synchronize_irq(dev->irq);
		free_irq(dev->irq, (void *)dev);

		unregister_sja1000dev(dev);
		free_sja1000dev(dev);
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
	if (pdev->msi_enabled)
		pci_free_irq_vectors(pdev);
	
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
	if (pdev->msi_enabled)
		pci_disable_msi(pdev);
#else
	if (pdev->msi_enabled)
		pci_disable_msi(pdev);
#endif
}

static int f81601_detect_default_clock_src(struct pci_dev *pdev)
{
	struct f81601_pci_card *card = pci_get_drvdata(pdev);
	static unsigned char key[4] = {0x32, 0x5d, 0x42, 0xac};
	u8 tmp, mask, dev_st = 0;
	//u32 read_fifo_data[2];
	//int detect_retry = 300000;
	int i, j;

	pci_write_config_byte(pdev, F81601_DECODE_REG, card->decode_cfg);
	mask = BIT(7) | BIT(6);

	card->chosen_clock = F81601_DEFAULT_CLK; // default internal clk
	card->decode_cfg &= ~0x0c;

	for (i = 0; i < F81601_PCI_MAX_CHAN; ++i) {
		for (j = 0; j < ARRAY_SIZE(key); ++j)
			f81601_raw_can_reg_write(pdev, i, 0x7c, key[j]);

		tmp = f81601_raw_can_reg_read(pdev, i, 0x7f);
		dev_dbg(&pdev->dev, "can%d: old tmp: %x\n", i, tmp);
		tmp |= BIT(5) | BIT(4);
		f81601_raw_can_reg_write(pdev, i, 0x7f, tmp);

		dev_st |= f81601_raw_can_reg_read(pdev, i, 0x7f);
		f81601_raw_can_reg_write(pdev, i, 0x7c, 0x35);
	}

	card->is_new_ic = false;
	if (dev_st & mask)
		card->is_new_ic = true;

	if (internal_clk == -1) {
		// force change to external
		card->decode_cfg &= ~0x0c;
		dev_dbg(&pdev->dev, "%s: internal: %d, dev_st: %x\n", __func__, card->is_internal, dev_st);

		switch (dev_st & mask) {
		case 0x00:
			card->decode_cfg |= 0x0f; // default internal
			card->is_internal = true;
			dev_info(&pdev->dev, "detected internal clock\n");
			break;

		case 0xc0:
			card->decode_cfg |= 0x0f; // default internal
			card->is_internal = true;
			dev_info(&pdev->dev, "detected internal clock\n");

			// original
			pci_write_config_byte(pdev, 0x296, 0x00);

			// 2x clk
			//pci_write_config_byte(pdev, 0x296, 0xC0);
			//card->chosen_clock *= 2;
			//card->decode_cfg |= BIT(4);
			//card->chosen_clock *= 2;			
			break;

		case 0x40:
			card->decode_cfg |= 0x03; // default external
			card->is_internal = false;
			card->chosen_clock = external_clk;
			dev_info(&pdev->dev, "detected external clock\n");
			break;
		}

		if (0) {
			// force internal 80M clk
			card->decode_cfg &= ~0x0c;
			card->decode_cfg |= 0x0f; // default internal

			card->chosen_clock = 80000000 / 2; // due to 73h default=2
			pci_write_config_byte(pdev, 0x296, 0x84);
			pci_write_config_byte(pdev, 0x296, 0xd4);
			
			//f81601_raw_can_reg_write(pdev, 0, 0x73, 2); // can0 clk div
			//f81601_raw_can_reg_write(pdev, 1, 0x73, 2); // can1 clk div
			msleep(1000);
			dev_info(&pdev->dev, "detected internal clock\n");
		}

	} else if (internal_clk == 0) {
		/* force external */
		card->decode_cfg |= 0x03; // force external
		card->is_internal = false;
		card->chosen_clock = external_clk;

		dev_info(&pdev->dev, "%s: force external clock\n", __func__);
	} else {
		/* force internal */
		card->decode_cfg |= 0x0f; // force internal
		card->is_internal = true;
		card->chosen_clock = external_clk;

		dev_info(&pdev->dev, "%s: force internal clock\n", __func__);
	}

#if 0
	if (internal_clk == -1 && card->is_internal) {
		// force change to external
		card->decode_cfg &= ~0x0c;
		dev_dbg(&pdev->dev, "%s: internal: %d, dev_st: %x\n", __func__, card->is_internal, dev_st);

		switch (dev_st & mask) {
		case 0x00:
			card->decode_cfg |= 0x0f; // default internal
			card->is_internal = true;
			dev_info(&pdev->dev, "detected internal clock\n");
			break;

		case 0xc0:
			card->decode_cfg |= 0x0f; // default internal
			card->is_internal = true;
			dev_info(&pdev->dev, "detected internal clock\n");

			card->chosen_clock = 80000000 / 2;
			pci_write_config_byte(pdev, 0x296, 0x84);
			pci_write_config_byte(pdev, 0x296, 0xd4);

			f81601_raw_can_reg_write(pdev, 0, 0x73, 2); // can0 clk div
			f81601_raw_can_reg_write(pdev, 1, 0x73, 2); // can1 clk div
			msleep(1000);

			break;

		case 0x40:
			card->decode_cfg |= 0x03; // default external
			card->is_internal = false;
			card->chosen_clock = external_clk;
			dev_info(&pdev->dev, "detected external clock\n");
			break;
		}
	}
#endif

	dev_dbg(&pdev->dev, "dev_st: %x\n", dev_st);
	dev_dbg(&pdev->dev, "%s: write 209h: %x\n", __func__, card->decode_cfg);

	dev_info(&pdev->dev, "F81601 running with %s clock: %dhz\n",
		card->is_internal ? "internal" : "external",
		card->chosen_clock);

	pci_write_config_byte(pdev, F81601_DECODE_REG, card->decode_cfg);

	return 0;
}

#if 0
static void f81601_dump_parameter(struct pci_dev *pdev)
{
	dev_info(&pdev->dev, "%s: rx_tasklet_en: %d\n", __func__, rx_tasklet_en);
	dev_info(&pdev->dev, "%s: rx_release_poll_cnt: %d\n", __func__, rx_release_poll_cnt);
	dev_info(&pdev->dev, "%s: rx_fifo_rmc: %d\n", __func__, rx_fifo_rmc);
	dev_info(&pdev->dev, "%s: rx_normal_mode: %d\n", __func__, rx_normal_mode);
	dev_info(&pdev->dev, "%s: bitrate_protect: %d\n", __func__, bitrate_protect);
}
#endif

/*
 * Probe F8160x based device for the SJA1000 chips and register each
 * available CAN channel to SJA1000 Socket-CAN subsystem.
 */
static int f81601_pci_add_card(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	struct sja1000_priv *priv;
	struct net_device *dev;
	struct f81601_pci_card *card;
	struct pci_dev *parent;
	unsigned int int_flag;
	int err, i;
	int irq_count = 1;
	u8 tmp;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
	int_flag = PCI_IRQ_INTX;
#else
	int_flag = PCI_IRQ_LEGACY;
#endif

	//f81601_dump_parameter(pdev);

	parent = pci_upstream_bridge(pdev);
	if (parent && !bypass_parent_bridge_check && enable_msi) {
		switch (parent->vendor) {
		case 0x12d8: // pericom
			if (parent->device == 0x2304)
				enable_msi = 0;
			break;
		}

		if (!enable_msi) {
			dev_info(&pdev->dev, "Detected bridge: vendor: %04x, dev: %04x\n",
					parent->vendor, parent->device);
			dev_info(&pdev->dev, "Disable MSI supports\n");
		}
	}

	dev_info(&pdev->dev, "Fintek F81601 Driver version: %s\n", DRV_VER);

	if (pcim_enable_device(pdev) < 0) {
		dev_err(&pdev->dev, "Failed to enable PCI device\n");
		return -ENODEV;
	}

	/* Allocate card structures to hold addresses, ... */
	card = devm_kzalloc(&pdev->dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	pci_set_drvdata(pdev, card);

	card->channels = 0;
	card->decode_cfg = 0x0f;//0x03; // enable all can with internal clk.
	card->dev = pdev;
	spin_lock_init(&card->lock);

	if (enable_mem_access) {
		dev_info(&pdev->dev, "Using MMIO interface\n");
		card->addr = pcim_iomap(pdev, 0, pci_resource_len(pdev, 0));
		card->decode_cfg |= BIT(6);
	} else {
		dev_info(&pdev->dev, "Using IO interface\n");
		card->addr = pcim_iomap(pdev, 1, pci_resource_len(pdev, 1));
		card->decode_cfg |= BIT(7);
	}

	pci_write_config_byte(pdev, F81601_DECODE_REG, card->decode_cfg);

	if (!card->addr) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "Failed to remap BAR\n");
		goto failure_cleanup;
	}

	err = f81601_detect_default_clock_src(pdev);
	if (err) {
		dev_err(&pdev->dev, "select default clock err\n");
		return err;
	}

	if (enable_msi) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
		if (auto_affinity) {
			irq_count = pci_alloc_irq_vectors(pdev, 1, max_msi_ch,
					int_flag | PCI_IRQ_MSI | PCI_IRQ_AFFINITY);
		} else {
			irq_count = pci_alloc_irq_vectors(pdev, 1, max_msi_ch,
					int_flag | PCI_IRQ_MSI);
		}

		if (irq_count < 0)
			return irq_count;

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
		irq_count = pci_enable_msi_range(pdev, 1, max_msi_ch);
		if (irq_count < 0) {
			irq_count = 1;
			pci_intx(pdev, 1);
		}

#else
		irq_count = pci_enable_msi_block(pdev, max_msi_ch);
		if (irq_count < 0) {
			irq_count = 1;
			pci_intx(pdev, 1);
		} else if (irq_count == 0) { // fully support
			irq_count = max_msi_ch;
		} else {
			// partial support, irq_count is count
		}
#endif
	}

	/* Detect available channels */
	for (i = 0; i < ent->driver_data; i++) {
		/* read CAN2_HW_EN strap pin */
		err = pci_read_config_byte(pdev, 0x20a, &tmp);
		if (!err && i == 1 && !(tmp & BIT(4)))
			break;

		dev = alloc_sja1000dev(0, card->is_new_ic);
		if (!dev) {
			err = -ENOMEM;
			goto failure_cleanup;
		}

		card->net_dev[i] = dev;
		priv = netdev_priv(dev);
		priv->priv = card;
		priv->irq_flags = /*IRQF_ONESHOT |*/ IRQF_SHARED ;

		INIT_DELAYED_WORK(&priv->tx_delayed_work, f81601_tx_delayed_work);
		INIT_DELAYED_WORK(&priv->busoff_delayed_work, f81601_busoff_delayed_work);
		tasklet_init(&priv->rx_tasklet, f81601_rx_tasklet, (unsigned long) priv);

		if (pci_dev_msi_enabled(pdev))
			priv->irq_flags |= IRQF_NO_SUSPEND;

		if (pdev->msi_enabled) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
			dev->irq = pci_irq_vector(pdev, i % irq_count);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
			dev->irq = pdev->irq + i % irq_count;
#else
			dev->irq = pdev->irq + i % irq_count;
#endif
		} else {
			dev->irq = pdev->irq;
		}

#if 0
		priv->is_read_more_rx = true;
		tmp = f81601_raw_can_reg_read(pdev, i, 0x7f);
		if (tmp & BIT(6))
			priv->is_read_more_rx = false;
#else
		priv->is_read_more_rx = !card->is_new_ic;
#endif
		priv->reg_base = card->addr + 0x80 * i;

		if (enable_mem_access) {
			priv->read_reg = f81601_pci_mmio_read_reg;

			if (priv->is_read_more_rx)
				priv->write_reg = f81601_pci_mmio_write_lock_reg;
			else
				priv->write_reg = f81601_pci_mmio_write_reg;
		} else {
			priv->read_reg = f81601_pci_io_read_reg;
			priv->write_reg = f81601_pci_io_write_reg;
		}

		priv->write_mask_reg = f81601_write_mask_reg;

		priv->can.clock.freq = card->chosen_clock / 2;
		priv->ocr = OCR_TX0_PUSHPULL | OCR_TX1_PUSHPULL;
		priv->cdr = CDR_CBP;

		SET_NETDEV_DEV(dev, &pdev->dev);
		dev->dev_id = i;

		dev_dbg(&pdev->dev, "i: %d, 7fh: %x\n", i, priv->read_reg(priv, 0x7f));

		if (bus_restart_ms && !priv->can.restart_ms)
			priv->can.restart_ms = bus_restart_ms;
/*
		memset(dev->name, 0, sizeof(dev->name));

		if (dev->dev_id == 0)
			memcpy(dev->name, "can1", strlen("can1"));
		else
			memcpy(dev->name, "can2", strlen("can2"));
*/		
		/* Register SJA1000 device */
		err = register_sja1000dev(dev);
		if (err) {
			dev_err(&pdev->dev, "Registering device failed "
				"(err=%d)\n", err);
			goto failure_cleanup;
		}

		/* force into reset mode */
		priv->write_reg(priv, SJA1000_MOD, MOD_RM);
#if 0
		err = request_threaded_irq(dev->irq, NULL, f81601_interrupt, priv->irq_flags,
						dev->name, (void *)dev);
#else
		err = request_irq(dev->irq, f81601_interrupt, priv->irq_flags,
					dev->name, (void *)dev);
#endif

		card->channels++;

		dev_info(&pdev->dev, "Channel #%d at 0x%p, irq %d "
			 "registered as %s\n", i + 1, priv->reg_base,
			 dev->irq, dev->name);

		device_create_file(&dev->dev, &dev_attr_rx_release_cnt);
		device_create_file(&dev->dev, &dev_attr_rx_release_cnt_max);
	}

	if (!card->channels) {
		err = -ENODEV;
		goto failure_cleanup;
	}

	
	device_create_file(&pdev->dev, &dev_attr_read_id);
	pdev->dev_flags |= PCI_DEV_FLAGS_NO_D3;

	return 0;

failure_cleanup:
	dev_err(&pdev->dev, "Error: %d. Cleaning Up.\n", err);
	f81601_pci_del_card(pdev);

	return err;
}

static int f81601_pci_suspend(struct device *device)
{
	struct f81601_pci_card *card;
	struct sja1000_priv *priv;
	struct net_device *dev;
	struct pci_dev *pdev;
	int i, j;

	pdev = to_pci_dev(device);
	card = pci_get_drvdata(pdev);

	for (i = 0; i < ARRAY_SIZE(card->net_dev); i++) {
		dev = card->net_dev[i];
		if (!dev)
			continue;

		priv = netdev_priv(dev);
		netif_stop_queue(dev);

		/* force into reset mode */
		priv->write_reg(priv, SJA1000_MOD, MOD_RM);

		/* save necessary register data */
		for (j = 0; j < ARRAY_SIZE(card->reg_table[i]); ++j)
			card->reg_table[i][j] = priv->read_reg(priv, j);

		/* disable interrupt */
		priv->write_reg(priv, SJA1000_IER, 0);
		synchronize_irq(dev->irq);
	}

	return 0;
}

static int f81601_pci_resume(struct device *device)
{
	struct pci_dev *pdev;
	struct f81601_pci_card *card;
	struct sja1000_priv *priv;
	struct net_device *dev;
	int i, j;
	u8 reg;
	u8 restore_reg_table[] = { SJA1000_BTR0, SJA1000_BTR1, SJA1000_CDR,
		SJA1000_OCR, SJA1000_ACCC0, SJA1000_ACCC1, SJA1000_ACCC2,
		SJA1000_ACCC3, SJA1000_ACCM0, SJA1000_ACCM1, SJA1000_ACCM2,
		SJA1000_ACCM3, SJA1000_IER,
	};

	pdev = to_pci_dev(device);
	card = pci_get_drvdata(pdev);

	/* recovery all needed configure */
	f81601_detect_default_clock_src(pdev);
	//pci_write_config_byte(pdev, F81601_DECODE_REG, card->decode_cfg);

	for (i = 0; i < ARRAY_SIZE(card->net_dev); i++) {
		dev = card->net_dev[i];
		if (!dev)
			continue;

		priv = netdev_priv(dev);
		if (priv->can.state == CAN_STATE_STOPPED ||
		    priv->can.state == CAN_STATE_BUS_OFF)
			continue;

		/* force into reset mode */
		priv->write_reg(priv, SJA1000_MOD, MOD_RM);

		/* clear error counters and error code capture */
		priv->write_reg(priv, SJA1000_TXERR, 0x0);
		priv->write_reg(priv, SJA1000_RXERR, 0x0);
		priv->read_reg(priv, SJA1000_ECC);
		priv->read_reg(priv, SJA1000_ALC);
		
		/* clear interrupt flags */
		priv->read_reg(priv, SJA1000_IR);

		/* restore necessary register data */
		for (j = 0; j < ARRAY_SIZE(restore_reg_table); ++j) {
			reg = restore_reg_table[j];
			priv->write_reg(priv, reg, card->reg_table[i][reg]);
		}

		/* re-enable device */
		reg = 0;
		if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
			reg |= MOD_LOM;
		if (priv->can.ctrlmode & CAN_CTRLMODE_PRESUME_ACK)
			reg |= MOD_STM;

		priv->write_reg(priv, SJA1000_MOD, reg);

		if (netif_queue_stopped(dev))
			netif_wake_queue(dev);
	}

	return 0;
}

static const struct dev_pm_ops f81601_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(f81601_pci_suspend, f81601_pci_resume)
};

static struct pci_driver f81601_pci_driver = {
	.name = DRV_NAME,
	.id_table = f81601_pci_tbl,
	.probe = f81601_pci_add_card,
	.remove = f81601_pci_del_card,
	.driver.pm = &f81601_pm_ops,
};

MODULE_DESCRIPTION("Fintek F81601 PCIE to 2 CANBUS adaptor driver");
MODULE_AUTHOR("Peter Hong <peter_hong@fintek.com.tw>");
MODULE_LICENSE("GPL v2");

#if 1
module_pci_driver(f81601_pci_driver);
#else
static int __init f81601_pci_driver_init(void)
{
	return pci_register_driver(&f81601_pci_driver);
}

static void __exit f81601_pci_driver_exit(void)
{
	pci_unregister_driver(&f81601_pci_driver);
}

module_init(f81601_pci_driver_init);
module_exit(f81601_pci_driver_exit);
#endif
