/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _HDMI_PLL_H_
#define _HDMI_PLL_H_

#include <linux/clk-provider.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include "hdmi_parser.h"
#include "hdmi_debug.h"

#define HDMI_PLL_NUM_CLKS	1
#define HDMI_PLL_NAME_MAX_SIZE	32

struct hdmi_pll;

struct hdmi_pll_vco_clk {
	void *priv;
	struct clk_hw hw;
	struct clk_init_data init_data;
	char name[HDMI_PLL_NAME_MAX_SIZE];
};

enum hdmi_pll_revision {
	HDMI_PLL_UNKNOWN,
	HDMI_PLL_SNPS_4NM,
};

struct hdmi_pll {
	u32 revision;
	void *mpll;
	int clk_factor;

	/*
	 * Save vco's current rate in kHz.
	 */
	unsigned long vco_rate;

	/*
	 * PLL index if multiple index are available.
	 * E.g., in case of DSI, we have 2 plls.
	 */
	uint32_t index;

	/*
	 * clk relevant declarations.
	 */
	struct clk_onecell_data *clk_data;
	struct hdmi_pll_vco_clk pll_clks[HDMI_PLL_NUM_CLKS];
	const char *name;

	struct platform_device *pdev;

	const struct hdmi_pll_fops *fops;
	struct hdmi_parser *parser;
	struct hdmi_io_data *io_data;

	int (*config)(struct hdmi_pll *pll,
			u32 clock_khz, u32 bpp);
	int (*prepare)(struct hdmi_pll *pll);
	int (*unprepare)(struct hdmi_pll *pll);
};

static inline struct hdmi_pll_vco_clk *to_hdmi_vco_hw(struct clk_hw *hw)
{
	return container_of(hw, struct hdmi_pll_vco_clk, hw);
}

int hdmi_pll_snps_register(struct hdmi_pll *pll);

struct hdmi_pll *hdmi_pll_get(struct platform_device *pdev,
			struct hdmi_parser *parser);
int hdmi_pll_clock_register_helper(struct hdmi_pll *pll,
		struct hdmi_pll_vco_clk *clks, int num_clks);
void hdmi_pll_snps_unregister(struct hdmi_pll *pll);
int hdmi_pll_clk_register_helper(struct hdmi_pll *pll,
		struct hdmi_pll_vco_clk *clks, int num_clks);

void hdmi_pll_drv_unregister(void);
void hdmi_pll_drv_register(void);
static inline const char *hdmi_pll_get_revision(enum hdmi_pll_revision rev)
{
	switch (rev) {
	case HDMI_PLL_UNKNOWN:	return "HDMI_PLL_UNKNOWN";
	case HDMI_PLL_SNPS_4NM:	return "HDMI_PLL_SNPS_4NM";
	default:		return "???";
	}
}
#endif /* _HDMI_PLL_H_ */
