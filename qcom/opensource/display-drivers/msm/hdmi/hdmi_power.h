/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _HDMI_POWER_H_
#define _HDMI_POWER_H_

#include "hdmi_parser.h"
#include "hdmi_pll.h"
#include "sde_power_handle.h"

/**
 * sruct hdmi_power - DisplayPort's power related data
 *
 * @hdmi_phy_gdsc: GDSC regulator
 * @init: initializes the regulators/core clocks/GPIOs/pinctrl
 * @deinit: turns off the regulators/core clocks/GPIOs/pinctrl
 * @clk_enable: enable/disable the HDMI clocks
 * @clk_status: check for clock status
 * @set_pixel_clk_parent: set the parent of HDMI pixel clock
 * @park_clocks: park all clocks driven by PLL
 * @clk_get_rate: get the current rate for provided clk_name
 * @power_client_init: configures clocks and regulators
 * @power_client_deinit: frees clock and regulator resources
 * @power_mmrm_init: configures mmrm client registration
 */
struct hdmi_power {
	struct drm_device *drm_dev;
	struct sde_power_handle *phandle;
	struct regulator *hdmi_phy_gdsc;
	int (*init)(struct hdmi_power *power);
	int (*deinit)(struct hdmi_power *power);
	int (*clk_enable)(struct hdmi_power *power, enum hdmi_pm_type pm_type,
				bool enable);
	bool (*clk_status)(struct hdmi_power *power, enum hdmi_pm_type pm_type);
	int (*set_pixel_clk_parent)(struct hdmi_power *power);
	int (*park_clocks)(struct hdmi_power *power);
	u64 (*clk_get_rate)(struct hdmi_power *power, char *clk_name);
	int (*power_client_init)(struct hdmi_power *power,
		struct sde_power_handle *phandle,
		struct drm_device *drm_dev);
	void (*power_client_deinit)(struct hdmi_power *power);
	int (*power_mmrm_init)(struct hdmi_power *power,
			struct sde_power_handle *phandle, void *hdmi,
			int (*hdmi_display_mmrm_callback)(
			struct mmrm_client_notifier_data *notifier_data));
};

/**
 * hdmi_power_get() - configure and get the DisplayPort power module data
 *
 * @parser: instance of parser module
 * @pll: instance of pll module
 * return: pointer to allocated power module data
 *
 * This API will configure the DisplayPort's power module and provides
 * methods to be called by the client to configure the power related
 * modueles.
 */
struct hdmi_power *hdmi_power_get(struct hdmi_parser *parser,
				struct hdmi_pll *pll);

/**
 * hdmi_power_put() - release the power related resources
 *
 * @power: pointer to the power module's data
 */
void hdmi_power_put(struct hdmi_power *power);
#endif /* _HDMI_POWER_H_ */
