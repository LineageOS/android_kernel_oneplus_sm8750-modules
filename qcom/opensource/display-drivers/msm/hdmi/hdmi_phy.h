/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _HDMI_PHY_H_
#define _HDMI_PHY_H_

#include <linux/of.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "hdmi_parser.h"
#include "hdmi_debug.h"

enum hdmi_phy_version {
	HDMI_PHY_UNKNOWN,
	HDMI_PHY_SNPS_V4NM,
};

struct hdmi_phy {
	u32 version;
	u32 tmds_clk;
	struct hdmi_parser *parser;
	struct hdmi_io_data *io_data;

	int (*init)(struct hdmi_phy *phy);
	int (*config)(struct hdmi_phy *phy, u32 rate);
	int (*pre_enable)(struct hdmi_phy *phy);
	int (*enable)(struct hdmi_phy *phy);
	int (*pre_disable)(struct hdmi_phy *phy);
	int (*disable)(struct hdmi_phy *phy);
};

static inline bool hdmi_phy_read_poll_timeout(
			struct hdmi_phy *phy, u32 reg, u32 val)
{
	u8 state;
	void __iomem *base;
	u32 const poll_sleep_us = 2000;
	//Wait for 3 sec until read doesn't done.
	u32 const poll_timeout_us = 3000000;

	base = phy->io_data->io.base;

	if (readl_poll_timeout_atomic((base + reg), state,
			((state & val) > 0),
			poll_sleep_us, poll_timeout_us)) {
		HDMI_ERR("poll timeout crossed, reg: 0x%x status=0x%x", reg,
				state);

		return false;
	}
	return true;
}

struct hdmi_phy *hdmi_phy_get(struct platform_device *pdev,
			struct hdmi_parser *parser);
void hdmi_phy_put(struct hdmi_phy *phy);
void hdmi_phy_snps_register(struct hdmi_phy *phy);
void hdmi_phy_snps_unregister(struct hdmi_phy *phy);
#endif /* _HDMI_PHY_H_ */
