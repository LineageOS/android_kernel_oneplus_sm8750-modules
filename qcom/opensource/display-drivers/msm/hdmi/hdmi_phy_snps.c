// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */


#include "hdmi_phy.h"
#include "hdmi_reg_snps.h"

#define PHY_TXn_CONTROL	4

#define hdmi_read(x, off) \
	readl_relaxed((x)->io_data->io.base + (off))

#define hdmi_write(x, off, value)  \
	writel_relaxed(value, (x)->io_data->io.base + (off))

/* Operations for the register configuration.
 * READ		0
 * WRITE	1
 * operations are imported from the header kernel.h
 */
enum {
	NO_ROM	= 0,
	SRAM    = 1,
	SET_BIT	= 2,
	CLEAR_BIT = 3,
	ASSERT_RESET,
	DEASSERT_RESET,
};

static inline u32 hdmi_phy_snps_operation(u32 read, u32 write, int operation)
{
	switch (operation) {
	case (SET_BIT):
		return read | write;
	case (CLEAR_BIT):
		return read & ~write;
	default:
	case (WRITE):
		return write;
	}
}

static inline void HDMI_PHY_CONF_REG(struct hdmi_phy *phy, u32 off, u32 val, int op)
{
	u32 reg_val;

	reg_val  = hdmi_read(phy, off);
	reg_val = hdmi_phy_snps_operation(reg_val, val, op);
	hdmi_write(phy, off, reg_val);
	usleep_range(200, 50000);
}

static inline void HDMI_PHY_CONF_REG_GRP(struct hdmi_phy *phy, u32 reg, u32 val, int op)
{
	u32 i;

	for (i = 0; i < PHY_TXn_CONTROL; i++)
		HDMI_PHY_CONF_REG(phy, reg+i*0x34, val, op);
}

static void hdmi_phy_snps_pa_iso_config(struct hdmi_phy *phy, bool en)
{
	int operation = CLEAR_BIT;

	if (en)
		operation = SET_BIT;

	HDMI_PHY_CONF_REG(phy, HDMI_PHY_MISC_CONTROL_3,
			BIT(3) | BIT(2) | BIT(1), operation);
}

static bool hdmi_phy_snps_tx_ack_wait(struct hdmi_phy *phy);

static void hdmi_phy_snps_phy_init_config(struct hdmi_phy *phy)
{
	int operation = CLEAR_BIT;
	u32 offset = HDMI_PHY_MPLLB_CONTROL_0 - HDMI_PHY_MPLLA_CONTROL_0;

	HDMI_PHY_CONF_REG_GRP(phy, HDMI_PHY_TXn_CONTROL_8, BIT(2), SET_BIT);

	HDMI_PHY_CONF_REG(phy, HDMI_PHY_TX_COMMON_CONTROL_0, BIT(0), SET_BIT);

	usleep_range(5, 5000);

	HDMI_PHY_CONF_REG(phy, HDMI_PHY_REFCLK_CONTROL_0, BIT(0), SET_BIT);

	if (phy->parser->alternate_pll) {
		operation = SET_BIT;
		offset = 0;
	}

	HDMI_PHY_CONF_REG(phy, HDMI_PHY_MISC_CONTROL_3, BIT(0), operation);

	/*
	 * TODO: Confirm MPLLB configuration instead of below registers
	 * in case of MPLLB.
	 */
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_MPLLA_CONTROL_11 + offset,
			(0x18 & 0xff), WRITE);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_MPLLA_CONTROL_12 + offset,
			(0x12 & 0xff), WRITE); // Missing in phy.xlx

	HDMI_PHY_CONF_REG(phy, HDMI_PHY_MPLLA_CONTROL_13 + offset,
			(0x18 & 0xff), WRITE);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_MPLLA_CONTROL_16 + offset,
			BIT(1), SET_BIT);
	hdmi_write(phy, HDMI_PHY_MISC_CONTROL_2, 0x4d);
}

static void hdmi_phy_snps_tx_lane_reset(struct hdmi_phy *phy, int com)
{
	int op = CLEAR_BIT;

	if (com == ASSERT_RESET)
		op = SET_BIT;

	HDMI_PHY_CONF_REG_GRP(phy, HDMI_PHY_TXn_CONTROL_9, BIT(5), op);
}

static void hdmi_phy_snps_tx_lane_config(struct hdmi_phy *phy, u32 tmds_clk)
{
	u8 invert = 0;
	int op;
	/*
	 * Max bit rate in kHz
	 */
	const u32 max_bit_rate = 2.5e6;
	const u32 bpp = 24;

	if (phy->parser->alternate_pll)
		op = CLEAR_BIT;
	else
		op = SET_BIT;

	HDMI_PHY_CONF_REG_GRP(phy, HDMI_PHY_TXn_CONTROL_8, BIT(3), op);

	if (invert)
		op = SET_BIT;
	else
		op = CLEAR_BIT;

	HDMI_PHY_CONF_REG_GRP(phy, HDMI_PHY_TXn_CONTROL_8, BIT(0), op);

	HDMI_PHY_CONF_REG_GRP(phy, HDMI_PHY_TXn_CONTROL_9,
			BIT(2) | BIT(1) | BIT(0), CLEAR_BIT);

	HDMI_PHY_CONF_REG_GRP(phy, HDMI_PHY_TXn_CONTROL_10,
			BIT(5) | BIT(4), SET_BIT);

	HDMI_PHY_CONF_REG_GRP(phy, HDMI_PHY_TXn_CONTROL_7, 0, WRITE);

	HDMI_PHY_CONF_REG_GRP(phy, HDMI_PHY_TXn_CONTROL_6, 0, WRITE);

	HDMI_PHY_CONF_REG_GRP(phy, HDMI_PHY_TXn_CONTROL_5, 0x3e, WRITE);

	HDMI_PHY_CONF_REG_GRP(phy, HDMI_PHY_TXn_CONTROL_10, BIT(3), CLEAR_BIT);

	if (bpp * tmds_clk >= max_bit_rate)
		op = CLEAR_BIT;
	else
		op = SET_BIT;

	HDMI_PHY_CONF_REG(phy, HDMI_PHY_TX_COMMON_CONTROL_0, BIT(2), SET_BIT);

	HDMI_PHY_CONF_REG_GRP(phy, HDMI_PHY_TXn_CONTROL_9,
			BIT(3) | BIT(4), SET_BIT);

}

static void hdmi_phy_snps_qcsram_init(struct hdmi_phy *phy)
{
	hdmi_write(phy, HDMI_PHY_QCSRAM_CONFIG_3, BIT(0));
}

static void hdmi_phy_snps_qcsram_deinit(struct hdmi_phy *phy)
{
	hdmi_write(phy, HDMI_PHY_QCSRAM_CONFIG_3, 0);
}

static void hdmi_phy_snps_disable_hstx_reset(struct hdmi_phy *phy)
{

	HDMI_PHY_CONF_REG_GRP(phy, HDMI_PHY_TXn_CONTROL_9, 0x18, WRITE);

	HDMI_PHY_CONF_REG_GRP(phy, HDMI_PHY_TXn_CONTROL_0, 0, WRITE);

	HDMI_PHY_CONF_REG(phy, HDMI_PHY_RX1_CONFIG_15, 0, WRITE);

	HDMI_PHY_CONF_REG(phy, HDMI_PHY_RX2_CONFIG_15, 0, WRITE);

	HDMI_PHY_CONF_REG(phy, HDMI_PHY_REXT_CONTROL_2, 0, WRITE);
}

static void hdmi_phy_snps_sw_reset(struct hdmi_phy *phy, bool reset)
{
	u8 val = 0;

	if (reset)
		val = 1;

	HDMI_PHY_CONF_REG(phy, HDMI_PHY_RESET_CONTROL_0, val, WRITE);
}

static void hdmi_phy_snps_sram_init_wait(struct hdmi_phy *phy,
		bool no_rom_copy)
{
	int op = SRAM;

	if (no_rom_copy)
		op = NO_ROM;

	/*
	 * FIXME: Add conditional check for the return value
	 * of 'hdmi_phy_read_poll_timeout' function, and provide
	 * rollback functionality in case of failure.
	 */
	hdmi_phy_read_poll_timeout(phy, HDMI_PHY_SRAM_CONTROL_1, BIT(0));

	HDMI_PHY_CONF_REG(phy, HDMI_PHY_CR_ADDRESS_LSB, 0x03, WRITE);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_CR_ADDRESS_MSB, 0xe0, WRITE);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_CR_WRDATA_LSB, 0x18, WRITE);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_CR_WRDATA_MSB, 0x00, WRITE);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_CR_ACCESS_CMD, 0x03, WRITE);
	ndelay(400);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_CR_ACCESS_CMD, 0x0, WRITE);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_CR_ADDRESS_LSB, 0x05, WRITE);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_CR_ADDRESS_MSB, 0x90, WRITE);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_CR_WRDATA_LSB, 0x0c, WRITE);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_CR_WRDATA_MSB, 0x00, WRITE);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_CR_ACCESS_CMD, 0x03, WRITE);
	ndelay(400);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_CR_ACCESS_CMD, 0x0, WRITE);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_MISC_CONTROL_0, 0x07, WRITE);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_CR_ADDRESS_LSB, 0x1a, WRITE);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_CR_ADDRESS_MSB, 0xe0, WRITE);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_CR_WRDATA_LSB, 0xa0, WRITE);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_CR_WRDATA_MSB, 0x00, WRITE);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_CR_ACCESS_CMD, 0x03, WRITE);
	ndelay(400);
	HDMI_PHY_CONF_REG(phy, HDMI_PHY_CR_ACCESS_CMD, 0x0, WRITE);

	if (op == NO_ROM)
		HDMI_PHY_CONF_REG(phy, HDMI_PHY_SRAM_CONTROL_0, BIT(1), op);
	else
		HDMI_PHY_CONF_REG(phy, HDMI_PHY_SRAM_CONTROL_0, BIT(1), op);
}

static bool hdmi_phy_snps_tx_ack_wait(struct hdmi_phy *phy)
{
	bool rv = true;

	for (int i = 0; i < PHY_TXn_CONTROL; i++) {
		rv &= hdmi_phy_read_poll_timeout(phy,
				HDMI_PHY_TXn_CONTROL_2 + i*0x34, BIT(0));
		if (!rv)
			break;
	}

	return rv;
}

static void hdmi_phy_snps_tx_data_en(struct hdmi_phy *phy, bool en)
{
	int op = CLEAR_BIT;

	if (en)
		op = SET_BIT;

	HDMI_PHY_CONF_REG(phy, HDMI_PHY_TX_COMMON_CONTROL_0, BIT(1), op);
}

static int hdmi_phy_snps_init(struct hdmi_phy *phy)
{
	hdmi_phy_snps_pa_iso_config(phy, false);
	hdmi_phy_snps_sw_reset(phy, false);
	hdmi_phy_snps_tx_lane_reset(phy, ASSERT_RESET);
	hdmi_phy_snps_phy_init_config(phy);

	return 0;
}

static int hdmi_phy_snps_config(struct hdmi_phy *phy, u32 rate)
{
	hdmi_phy_snps_init(phy);
	phy->tmds_clk = rate;
	/* TODO: add a condition check for no_rom_copy state
	 * by default hardcoded to false, but would be configured
	 * based on the power state.
	 */

	hdmi_phy_snps_tx_lane_config(phy, rate);
	hdmi_phy_snps_qcsram_init(phy);
	usleep_range(5, 5000);
	hdmi_phy_snps_qcsram_deinit(phy);
	hdmi_phy_snps_tx_lane_reset(phy, DEASSERT_RESET);
	return 0;
}


static int hdmi_phy_snps_pre_enable(struct hdmi_phy *phy)
{
	hdmi_phy_snps_disable_hstx_reset(phy);
	hdmi_phy_snps_sw_reset(phy, false);
	hdmi_phy_snps_sram_init_wait(phy, false);
	/*
	 * FIXME: Add conditional check for the return value
	 * of 'hdmi_phy_snps_tx_ack_wait' function, and provide
	 * rollback functionality in case of failure.
	 */
	hdmi_phy_snps_tx_ack_wait(phy);

	return 0;
}

static int hdmi_phy_snps_enable(struct hdmi_phy *phy)
{
	hdmi_phy_snps_tx_data_en(phy, true);

	return 0;
}

static int hdmi_phy_snps_pre_disable(struct hdmi_phy *phy)
{
	hdmi_phy_snps_tx_data_en(phy, false);
	/*
	 * TODO: Disable clock pattern at this point.
	 * Clock pattern is separately handled by
	 * SNPS PLL.
	 * Align the call flows in this respective order
	 * for smooth execution.
	 */
	return 0;
}

static int hdmi_phy_snps_disable(struct hdmi_phy *phy)
{
	hdmi_phy_snps_sw_reset(phy, true);
	hdmi_phy_snps_tx_lane_reset(phy, ASSERT_RESET);
	hdmi_phy_snps_pa_iso_config(phy, true);
	/*
	 * TODO: Disable AOSS config.
	 */

	phy->tmds_clk = 0;
	return 0;
}

void hdmi_phy_snps_register(struct hdmi_phy *phy)
{
	if (!phy) {
		HDMI_ERR("invalid input");
		return;
	}

	phy->init		= hdmi_phy_snps_init;
	phy->config		= hdmi_phy_snps_config;
	phy->pre_enable		= hdmi_phy_snps_pre_enable;
	phy->enable		= hdmi_phy_snps_enable;
	phy->pre_disable	= hdmi_phy_snps_pre_disable;
	phy->disable		= hdmi_phy_snps_disable;

}

void hdmi_phy_snps_unregister(struct hdmi_phy *phy)
{
	if (!phy) {
		HDMI_ERR("invalid input");
		return;
	}

	hdmi_phy_snps_pre_disable(phy);
	hdmi_phy_snps_disable(phy);
	HDMI_DEBUG("complete");
}
