/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _HDMI_PARSER_H_
#define _HDMI_PARSER_H_

#include <linux/sde_io_util.h>

#define HDMI_MAX_PIXEL_CLK_KHZ    675000

enum hdmi_pm_type {
	HDMI_CORE_PM,
	HDMI_CTRL_PM,
	HDMI_PHY_PM,
	HDMI_PLL_PM,
	HDMI_HPD_PM,
	HDMI_PCLK_PM,
	HDMI_MAX_PM
};

enum hdmi_pin_states {
	HDMI_GPIO_DDC_CLK,
	HDMI_GPIO_DDC_DATA,
	HDMI_GPIO_HPD,
	HDMI_GPIO_MUX_EN,
	HDMI_GPIO_MUX_SEL,
	HDMI_GPIO_MUX_LPM,
	HDMI_GPIO_HPD5V,
	HDMI_GPIO_CEC,
	HDMI_GPIO_MAX,
};

enum hdmi_core_clks {
	HDMI_CORE_MDP_CLK,
	HDMI_CORE_IFACE_DATA,
	HDMI_CORE_ALT_IFACE_CLK,
	HDMI_CORE_CLK,
	HDMI_CORE_BUS_CLK,
	HDMI_CORE_MMSS_CLK,
	HDMI_CORE_MNOC_CLK,
	HDMI_CORE_MISC_AHB_CLK,
	HDMI_CORE_EXTP_CLK,
	HDMI_CORE_CLK_MAX,
};


/**
 * struct hdmi_io_data - data structure to store HDMI IO related info
 * @name: name of the IO
 * @buf: buffer corresponding to IO for debugging
 * @io: io data which give len and mapped address
 */
struct hdmi_io_data {
	const char *name;
	u8 *buf;
	struct dss_io_data io;
};

/**
 * struct hdmi_io - data struct to store array of HDMI IO info
 * @len: total number of IOs
 * @data: pointer to an array of HDMI IO data structures.
 */
struct hdmi_io {
	u32 len;
	struct hdmi_io_data *data;
};


/**
 * struct hdmi_pinctrl - HDMI's pin control
 *
 * @pin: pin-controller's instance
 * @state_active: active state pin control
 * @state_hpd_active: hpd active state pin control
 * @state_suspend: suspend state pin control
 */
struct hdmi_pinctrl {
	struct pinctrl *pin;
	struct pinctrl_state *state_active;
	struct pinctrl_state *state_hpd_active;
	struct pinctrl_state *state_hpd_tlmm;
	struct pinctrl_state *state_hpd_ctrl;
	struct pinctrl_state *state_suspend;
};

/**
 * struct hdmi_core_clk_info - Core clock information for HDMI hardware
 * @mdp_core_clk:        Handle to MDP core clock.
 * @iface_clk:           Handle to MDP interface clock.
 * @core_mmss_clk:       Handle to MMSS core clock.
 * @bus_clk:             Handle to bus clock.
 * @mnoc_clk:            Handle to MMSS NOC clock.
 * @drm:                 Pointer to drm device node
 */
struct hdmi_core_clk_info {
	struct clk *hdmi_core_clk;
	struct clk *mdp_core_clk;
	struct clk *iface_clk;
	struct clk *alt_iface_clk;
	struct clk *extp_clk;
	struct clk *core_mmss_clk;
	struct clk *bus_clk;
	struct clk *misc_bus_clk;
	struct clk *mnoc_clk;
	struct drm_device *drm;
};

struct hdmi_clk_info {
	/* Clocks parsed from DT */
	struct hdmi_core_clk_info core_clks;
};

struct hdmi_parser {
	struct platform_device *pdev;
	struct device *msm_hdcp_dev;
	struct dss_module_power mp[HDMI_MAX_PM];
	struct hdmi_clk_info clk_info;
	struct hdmi_pinctrl pinctrl;
	struct hdmi_io io;

	u32 max_pclk_khz;
	bool alternate_pll;

	const char *display_type;
	bool non_pluggable;
	u32 display_topology;
	bool skip_ddc;
	u32 num_of_modes;
	int phy_version;

	/* gpio's: */
	int ddc_clk_gpio, ddc_data_gpio;
	int hpd_gpio, mux_en_gpio;
	int mux_sel_gpio, hpd5v_gpio;
	int mux_lpm_gpio;

	int (*parse)(struct hdmi_parser *parser);
	struct hdmi_io_data *(*get_io)(struct hdmi_parser *parser, char *name);
	void (*get_io_buf)(struct hdmi_parser *parser, char *name);
	void (*clear_io_buf)(struct hdmi_parser *parser);
};

static inline const char *hdmi_parser_pm_name(enum hdmi_pm_type module)
{
	switch (module) {
	case HDMI_CORE_PM:	return "HDMI_CORE_PM";
	case HDMI_CTRL_PM:	return "HDMI_CTRL_PM";
	case HDMI_PHY_PM:	return "HDMI_PHY_PM";
	case HDMI_PLL_PM:	return "HDMI_PLL_PM";
	case HDMI_HPD_PM:	return "HDMI_HPD_PM";
	case HDMI_PCLK_PM:	return "HDMI_PCLK_PM";
	default:		return "???";
	}
}

/**
 * hdmi_parser_get() - get the HDMI's device tree parser module
 *
 * @pdev: platform data of the client
 * return: pointer to hdmi_parser structure.
 *
 * This function provides client capability to parse the
 * device tree and populate the data structures. The data
 * related to clock, regulators, pin-control and other
 * can be parsed using this module.
 */
struct hdmi_parser *hdmi_parser_get(struct platform_device *pdev);

/**
 * hdmi_parser_put() - cleans the hdmi_parser module
 *
 * @parser: pointer to the parser's data.
 */
void hdmi_parser_put(struct hdmi_parser *parser);

#endif /* _HDMI_PARSER_H_ */

