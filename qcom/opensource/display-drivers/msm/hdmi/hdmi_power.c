// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/version.h>
#include <drm/drm_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl.h>
#include "hdmi_power.h"
#include "hdmi_debug.h"
#include "hdmi_pll.h"

#define XO_CLK_KHZ	19200

struct hdmi_power_private {
	struct hdmi_parser *parser;
	struct hdmi_pll *pll;
	struct platform_device *pdev;
	struct clk *pixel_clk_rcg;
	struct clk *pixel_parent;
	struct clk *xo_clk;

	struct hdmi_power hdmi_power;

	bool core_clks_on;
	bool pclk_clks_on;
	bool pclk_clks_parked;
};

static int hdmi_power_regulator_init(struct hdmi_power_private *power)
{
	int rc = 0, i = 0, j = 0;
	struct platform_device *pdev;
	struct hdmi_parser *parser;

	parser = power->parser;
	pdev = power->pdev;

	for (i = HDMI_CORE_PM; !rc && (i < HDMI_MAX_PM); i++) {
		rc = msm_dss_get_vreg(&pdev->dev,
			parser->mp[i].vreg_config,
			parser->mp[i].num_vreg, 1);
		if (rc) {
			HDMI_ERR("failed to init vregs for %s\n",
				hdmi_parser_pm_name(i));
			for (j = i - 1; j >= HDMI_CORE_PM; j--) {
				msm_dss_get_vreg(&pdev->dev,
				parser->mp[j].vreg_config,
				parser->mp[j].num_vreg, 0);
			}

			goto error;
		}
	}
error:
	return rc;
}

static void hdmi_power_regulator_deinit(struct hdmi_power_private *power)
{
	int rc = 0, i = 0;
	struct platform_device *pdev;
	struct hdmi_parser *parser;

	parser = power->parser;
	pdev = power->pdev;

	for (i = HDMI_CORE_PM; (i < HDMI_MAX_PM); i++) {
		rc = msm_dss_get_vreg(&pdev->dev,
			parser->mp[i].vreg_config,
			parser->mp[i].num_vreg, 0);
		if (rc)
			HDMI_ERR("failed to deinit vregs for %s\n",
				hdmi_parser_pm_name(i));
	}
}

static void hdmi_power_phy_gdsc(struct hdmi_power *hdmi_power, bool on)
{
	int rc = 0;

	if (IS_ERR_OR_NULL(hdmi_power->hdmi_phy_gdsc))
		return;

	if (on)
		rc = regulator_enable(hdmi_power->hdmi_phy_gdsc);
	else
		rc = regulator_disable(hdmi_power->hdmi_phy_gdsc);

	if (rc)
		HDMI_ERR("Fail to %s hdmi_phy_gdsc regulator ret =%d\n",
				on ? "enable" : "disable", rc);
}

static int hdmi_power_regulator_ctrl(struct hdmi_power_private *power,
				bool enable)
{
	int rc = 0, i = 0, j = 0;
	struct hdmi_parser *parser;

	parser = power->parser;

	for (i = HDMI_CORE_PM; i < HDMI_MAX_PM; i++) {
		/*
		 * The HDMI_PLL_PM regulator is controlled by hdmi_display
		 * based on the link configuration.
		 */
		if (i == HDMI_PLL_PM) {
			/* HDMI GDSC vote is needed for new chipsets,
			 * define gdsc phandle if needed
			 */
			hdmi_power_phy_gdsc(&power->hdmi_power, enable);
			HDMI_DEBUG("skipping: '%s' vregs for %s\n",
					enable ? "enable" : "disable",
					hdmi_parser_pm_name(i));
			continue;
		}

		rc = msm_dss_enable_vreg(
			parser->mp[i].vreg_config,
			parser->mp[i].num_vreg, enable);
		if (rc) {
			HDMI_ERR("failed to '%s' vregs for %s\n",
					enable ? "enable" : "disable",
					hdmi_parser_pm_name(i));
			if (enable) {
				for (j = i-1; j >= HDMI_CORE_PM; j--) {
					msm_dss_enable_vreg(
					parser->mp[j].vreg_config,
					parser->mp[j].num_vreg, 0);
				}
			}
			goto error;
		}
	}
error:
	return rc;
}

static int hdmi_power_pinctrl_set(struct hdmi_power_private *power,
				bool active)
{
	int rc = -EFAULT;
	struct pinctrl_state *pin_state;
	struct hdmi_parser *parser;

	parser = power->parser;

	if (IS_ERR_OR_NULL(parser->pinctrl.pin))
		return 0;

	pin_state = active ? parser->pinctrl.state_active
				: parser->pinctrl.state_suspend;
	if (!IS_ERR_OR_NULL(pin_state)) {
		rc = pinctrl_select_state(parser->pinctrl.pin,
				pin_state);
		if (rc)
			HDMI_ERR("can not set %s pins\n",
			       active ? "hdmi_active"
			       : "hdmi_sleep");
	} else {
		HDMI_ERR("invalid '%s' pinstate\n",
		       active ? "hdmi_active"
		       : "hdmi_sleep");
	}

	return rc;
}

static void hdmi_power_clk_put(struct hdmi_power_private *power)
{
	enum hdmi_pm_type module;

	for (module = HDMI_CORE_PM; module < HDMI_MAX_PM; module++) {
		struct dss_module_power *pm = &power->parser->mp[module];

		if (!pm->num_clk)
			continue;

		msm_dss_mmrm_deregister(&power->pdev->dev, pm);

		msm_dss_put_clk(pm->clk_config, pm->num_clk);
	}
}

static int hdmi_power_clk_init(struct hdmi_power_private *power, bool enable)
{
	int rc = 0;
	struct device *dev;
	enum hdmi_pm_type module;

	dev = &power->pdev->dev;

	if (enable) {
		for (module = HDMI_CORE_PM; module < HDMI_MAX_PM; module++) {
			struct dss_module_power *pm =
				&power->parser->mp[module];

			if (!pm->num_clk)
				continue;

			rc = msm_dss_get_clk(dev, pm->clk_config, pm->num_clk);
			if (rc) {
				HDMI_ERR("failed to get %s clk. err=%d\n",
					hdmi_parser_pm_name(module), rc);
				goto exit;
			}
		}

		power->pixel_clk_rcg = clk_get(dev, "pixel_clk_rcg");
		if (IS_ERR(power->pixel_clk_rcg)) {
			HDMI_ERR("Unable to get HDMI pixel clk RCG: %ld\n",
					PTR_ERR(power->pixel_clk_rcg));
			rc = PTR_ERR(power->pixel_clk_rcg);
			power->pixel_clk_rcg = NULL;
			goto err_pixel_clk_rcg;
		}

		power->pixel_parent = clk_get(dev, "pixel_parent");
		if (IS_ERR(power->pixel_parent)) {
			HDMI_ERR("Unable to get HDMI pixel RCG parent: %ld\n",
					PTR_ERR(power->pixel_parent));
			rc = PTR_ERR(power->pixel_parent);
			power->pixel_parent = NULL;
			goto err_pixel_parent;
		}

		power->xo_clk = clk_get(dev, "rpmh_cxo_clk");
		if (IS_ERR(power->xo_clk)) {
			HDMI_ERR("Unable to get XO clk: %ld\n",
					PTR_ERR(power->xo_clk));
			rc = PTR_ERR(power->xo_clk);
			power->xo_clk = NULL;
			goto err_xo_clk;
		}

	} else {
		if (power->pixel_parent)
			clk_put(power->pixel_parent);

		if (power->pixel_clk_rcg)
			clk_put(power->pixel_clk_rcg);

		if (power->xo_clk)
			clk_put(power->xo_clk);

		hdmi_power_clk_put(power);
	}

	return rc;
err_xo_clk:
	clk_put(power->pixel_parent);
err_pixel_parent:
	clk_put(power->pixel_clk_rcg);
err_pixel_clk_rcg:
	hdmi_power_clk_put(power);
exit:
	return rc;
}

static int hdmi_power_park_module(struct hdmi_power_private *power,
			enum hdmi_pm_type module)
{
	struct dss_module_power *mp;
	struct clk *clk = NULL;
	int rc = 0;
	bool *parked;

	// TODO: check optimizing this func
	mp = &power->parser->mp[module];

	if (module == HDMI_PCLK_PM) {
		clk = power->pixel_clk_rcg;
		parked = &power->pclk_clks_parked;
	} else {
		goto exit;
	}

	if (!clk) {
		HDMI_WARN("clk type %d not supported\n", module);
		rc = -EINVAL;
		goto exit;
	}

	if (!power->xo_clk) {
		rc = -EINVAL;
		goto exit;
	}

	if (*parked)
		goto exit;

	rc = clk_set_parent(clk, power->xo_clk);
	if (rc) {
		HDMI_ERR("unable to set xo parent on clk %d\n", module);
		goto exit;
	}

	mp->clk_config->rate = XO_CLK_KHZ * power->pll->clk_factor;
	rc = msm_dss_clk_set_rate(mp->clk_config, mp->num_clk);
	if (rc) {
		HDMI_ERR("failed to set clk rate.\n");
		goto exit;
	}

	*parked = true;

exit:
	return rc;
}

static int hdmi_power_set_pixel_clk_parent(struct hdmi_power *hdmi_power)
{
	int rc = 0;
	struct hdmi_power_private *power;

	if (!hdmi_power) {
		HDMI_ERR("invalid power data.\n");
		rc = -EINVAL;
		goto exit;
	}

	power = container_of(hdmi_power,
			struct hdmi_power_private, hdmi_power);

	if (power->pixel_clk_rcg && power->pixel_parent)
		rc = clk_set_parent(power->pixel_clk_rcg,
				power->pixel_parent);
	else
		HDMI_WARN("pixel parent not set\n");

	if (rc)
		HDMI_ERR("failed. rc=%d\n", rc);
exit:
	return rc;
}

static int hdmi_power_park_clocks(struct hdmi_power *hdmi_power)
{
	int rc = 0;
	struct hdmi_power_private *power;

	if (!hdmi_power) {
		HDMI_ERR("invalid power data\n");
		return -EINVAL;
	}

	power = container_of(hdmi_power, struct hdmi_power_private, hdmi_power);

	rc = hdmi_power_park_module(power, HDMI_PCLK_PM);
	if (rc) {
		HDMI_ERR("failed to park pixel clock. err=%d\n", rc);
		goto error;
	}

error:
	return rc;
}

static int hdmi_power_clk_set_rate(struct hdmi_power_private *power,
		enum hdmi_pm_type module, bool enable)
{
	int rc = 0;
	struct dss_module_power *mp;

	if (!power) {
		HDMI_ERR("invalid power data\n");
		rc = -EINVAL;
		goto exit;
	}

	mp = &power->parser->mp[module];

	if (enable) {
		rc = msm_dss_clk_set_rate(mp->clk_config, mp->num_clk);
		if (rc) {
			HDMI_ERR("failed to set clks rate.\n");
			goto exit;
		}

		rc = msm_dss_enable_clk(mp->clk_config, mp->num_clk, 1);
		if (rc) {
			HDMI_ERR("failed to enable clks\n");
			goto exit;
		}
	} else {
		rc = msm_dss_enable_clk(mp->clk_config, mp->num_clk, 0);
		if (rc) {
			HDMI_ERR("failed to disable clks\n");
				goto exit;
		}

		hdmi_power_park_module(power, module);
	}
exit:
	return rc;
}

static bool hdmi_power_clk_status(struct hdmi_power *hdmi_power,
			enum hdmi_pm_type pm_type)
{
	struct hdmi_power_private *power;

	if (!hdmi_power) {
		HDMI_ERR("invalid power data\n");
		return false;
	}

	power = container_of(hdmi_power,
				struct hdmi_power_private, hdmi_power);

	if (pm_type == HDMI_PCLK_PM)
		return power->pclk_clks_on;
	else if (pm_type == HDMI_CORE_PM)
		return power->core_clks_on;
	else
		return false;
}


static int hdmi_power_clk_enable(struct hdmi_power *hdmi_power,
		enum hdmi_pm_type pm_type, bool enable)
{
	int rc = 0;
	struct dss_module_power *mp;
	struct hdmi_power_private *power;

	if (!hdmi_power) {
		HDMI_ERR("invalid power data\n");
		rc = -EINVAL;
		goto error;
	}

	power = container_of(hdmi_power,
			struct hdmi_power_private, hdmi_power);

	mp = &power->parser->mp[pm_type];

	if (pm_type >= HDMI_MAX_PM) {
		HDMI_ERR("unsupported power module: %s\n",
				hdmi_parser_pm_name(pm_type));
		return -EINVAL;
	}

	if (enable) {
		if (hdmi_power_clk_status(hdmi_power, pm_type)) {
			HDMI_DEBUG("%s clks already enabled\n",
					hdmi_parser_pm_name(pm_type));
			return 0;
		}

		if ((pm_type == HDMI_CTRL_PM) && (!power->core_clks_on)) {
			HDMI_DEBUG("Need to enable core clks before pclk\n");

			rc = hdmi_power_clk_set_rate(power, pm_type, enable);
			if (rc) {
				HDMI_ERR("failed to enable clks: %s. err=%d\n",
					hdmi_parser_pm_name(HDMI_CORE_PM), rc);
				goto error;
			} else {
				power->core_clks_on = true;
			}
		}

		if (pm_type == HDMI_PCLK_PM && power->pixel_parent) {
			rc = clk_set_parent(power->pixel_clk_rcg,
					power->pixel_parent);
			if (rc) {
				HDMI_ERR("failed to set pixel parent\n");
				goto error;
			}
		}
	}

	rc = hdmi_power_clk_set_rate(power, pm_type, enable);
	if (rc) {
		HDMI_ERR("failed to '%s' clks for: %s. err=%d\n",
			enable ? "enable" : "disable",
			hdmi_parser_pm_name(pm_type), rc);
			goto error;
	}

	if (pm_type == HDMI_CORE_PM)
		power->core_clks_on = enable;
	else if (pm_type == HDMI_PCLK_PM)
		power->pclk_clks_on = enable;

	if (pm_type == HDMI_PCLK_PM)
		power->pclk_clks_parked = false;

	/*
	 * This log is printed only when user connects or disconnects
	 * a HDMI cable. As this is a user-action and not a frequent
	 * usecase, it is not going to flood the kernel logs. Also,
	 * helpful in debugging the NOC issues.
	 */
	HDMI_INFO("core:%s pclk:%s\n",
		power->core_clks_on ? "on" : "off",
		power->pclk_clks_on ? "on" : "off");
error:
	return rc;
}

static bool hdmi_power_find_gpio(const char *gpio1, const char *gpio2)
{
	return !!strnstr(gpio1, gpio2, strlen(gpio1));
}

static void hdmi_power_set_gpio(struct hdmi_power_private *power)
{
	int i;
	struct dss_module_power *mp = &power->parser->mp[HDMI_CORE_PM];
	struct dss_gpio *config = mp->gpio_config;

	for (i = 0; i <= HDMI_GPIO_MAX; i++) {
		if (gpio_is_valid(config->gpio)) {
			HDMI_DEBUG("gpio %s, value %d\n", config->gpio_name,
				config->value);

			if (hdmi_power_find_gpio(config->gpio_name, "mux-en") ||
			    hdmi_power_find_gpio(config->gpio_name, "mux-lpm") ||
			    hdmi_power_find_gpio(config->gpio_name, "mux-sel"))
				gpio_direction_output(config->gpio,
					config->value);
		}
		config++;
	}

}

static int hdmi_power_config_gpios(struct hdmi_power_private *power,
				bool enable)
{
	int i;
	struct dss_module_power *mp;
	struct dss_gpio *config;

	mp = &power->parser->mp[HDMI_CORE_PM];
	config = mp->gpio_config;

	if (enable) {
		hdmi_power_set_gpio(power);
	} else {
		for (i = 0; i < mp->num_gpio; i++) {
			if (gpio_is_valid(config[i].gpio)) {
				gpio_set_value(config[i].gpio, 0);
				gpio_free(config[i].gpio);
			}
		}
	}

	return 0;
}

static int hdmi_power_mmrm_init(struct hdmi_power *hdmi_power,
		struct sde_power_handle *phandle,
		void *hdmi, int (*hdmi_display_mmrm_callback)
		(struct mmrm_client_notifier_data *notifier_data))
{
	// TODO need to check if we need incase of hdmi
	return 0;
}

static u64 hdmi_power_clk_get_rate(struct hdmi_power *hdmi_power,
				char *clk_name)
{
	size_t i;
	enum hdmi_pm_type j;
	struct dss_module_power *mp;
	struct hdmi_power_private *power;
	bool clk_found = false;
	u64 rate = 0;

	if (!clk_name) {
		HDMI_ERR("invalid pointer for clk_name\n");
		return 0;
	}

	power = container_of(hdmi_power,
			struct hdmi_power_private, hdmi_power);
	mp = &hdmi_power->phandle->mp;
	for (i = 0; i < mp->num_clk; i++) {
		if (!strcmp(mp->clk_config[i].clk_name, clk_name)) {
			rate = clk_get_rate(mp->clk_config[i].clk);
			clk_found = true;
			break;
		}
	}

	for (j = HDMI_CORE_PM; j < HDMI_MAX_PM && !clk_found; j++) {
		mp = &power->parser->mp[j];
		for (i = 0; i < mp->num_clk; i++) {
			if (!strcmp(mp->clk_config[i].clk_name, clk_name)) {
				rate = clk_get_rate(mp->clk_config[i].clk);
				clk_found = true;
				break;
			}
		}
	}

	return rate;
}

static int hdmi_power_init(struct hdmi_power *hdmi_power)
{
	int rc = 0;
	struct hdmi_power_private *power;

	if (!hdmi_power) {
		HDMI_ERR("invalid power data\n");
		rc = -EINVAL;
		goto exit;
	}

	power = container_of(hdmi_power,
			struct hdmi_power_private, hdmi_power);

	rc = hdmi_power_regulator_ctrl(power, true);
	if (rc) {
		HDMI_ERR("failed to enable regulators\n");
		goto exit;
	}

	rc = hdmi_power_pinctrl_set(power, true);
	if (rc) {
		HDMI_ERR("failed to set pinctrl state\n");
		goto err_pinctrl;
	}

	rc = hdmi_power_config_gpios(power, true);
	if (rc) {
		HDMI_ERR("failed to enable gpios\n");
		goto err_gpio;
	}

	rc = pm_runtime_resume_and_get(hdmi_power->drm_dev->dev);
	if (rc < 0) {
		HDMI_ERR("failed to enable power resource %d\n", rc);
		goto err_sde_power;
	}

	rc = hdmi_power_clk_enable(hdmi_power, HDMI_CORE_PM, true);
	if (rc) {
		HDMI_ERR("failed to enable HDMI core clocks\n");
		goto err_clk;
	}

	return 0;

err_clk:
	pm_runtime_put_sync(hdmi_power->drm_dev->dev);
err_sde_power:
	hdmi_power_config_gpios(power, false);
err_gpio:
	hdmi_power_pinctrl_set(power, false);
err_pinctrl:
	hdmi_power_regulator_ctrl(power, false);
exit:
	return rc;
}

static int hdmi_power_deinit(struct hdmi_power *hdmi_power)
{
	int rc = 0;
	struct hdmi_power_private *power;

	if (!hdmi_power) {
		HDMI_ERR("invalid power data\n");
		rc = -EINVAL;
		goto exit;
	}

	power = container_of(hdmi_power,
			struct hdmi_power_private, hdmi_power);

	if (power->pclk_clks_on)
		hdmi_power_clk_enable(hdmi_power, HDMI_PCLK_PM, false);

	hdmi_power_clk_enable(hdmi_power, HDMI_CORE_PM, false);
	pm_runtime_put_sync(hdmi_power->drm_dev->dev);

	hdmi_power_config_gpios(power, false);
	hdmi_power_pinctrl_set(power, false);
	hdmi_power_regulator_ctrl(power, false);
exit:
	return rc;

	return 0;
}

static int hdmi_power_client_init(struct hdmi_power *hdmi_power,
	struct sde_power_handle *phandle, struct drm_device *drm_dev)
{
	int rc = 0;
	struct hdmi_power_private *power;

	if (!drm_dev) {
		HDMI_ERR("invalid drm_dev\n");
		return -EINVAL;
	}

	power = container_of(hdmi_power,
				struct hdmi_power_private, hdmi_power);

	rc = hdmi_power_regulator_init(power);
	if (rc) {
		HDMI_ERR("failed to init regulators\n");
		goto error_power;
	}

	rc = hdmi_power_clk_init(power, true);
	if (rc) {
		HDMI_ERR("failed to init clocks\n");
		goto error_clk;
	}
	hdmi_power->phandle = phandle;
	hdmi_power->drm_dev = drm_dev;

	return 0;

error_clk:
	hdmi_power_regulator_deinit(power);
error_power:
	return rc;
}

static void hdmi_power_client_deinit(struct hdmi_power *hdmi_power)
{
	struct hdmi_power_private *power;

	if (!hdmi_power) {
		HDMI_ERR("invalid power data\n");
		return;
	}

	power = container_of(hdmi_power, struct hdmi_power_private, hdmi_power);

	hdmi_power_clk_init(power, false);
	hdmi_power_regulator_deinit(power);
}

struct hdmi_power *hdmi_power_get(struct hdmi_parser *parser, struct hdmi_pll *pll)
{
	int rc = 0;
	struct hdmi_power_private *power;
	struct hdmi_power *hdmi_power;
	struct device *dev;

	if (!parser || !pll) {
		HDMI_ERR("invalid input\n");
		rc = -EINVAL;
		goto error;
	}

	power = kzalloc(sizeof(*power), GFP_KERNEL);
	if (!power) {
		rc = -ENOMEM;
		goto error;
	}

	power->parser = parser;
	power->pll = pll;
	power->pdev = parser->pdev;

	hdmi_power = &power->hdmi_power;
	dev = &power->pdev->dev;

	hdmi_power->init = hdmi_power_init;
	hdmi_power->deinit = hdmi_power_deinit;
	hdmi_power->clk_enable = hdmi_power_clk_enable;
	hdmi_power->clk_status = hdmi_power_clk_status;
	hdmi_power->set_pixel_clk_parent = hdmi_power_set_pixel_clk_parent;
	hdmi_power->park_clocks = hdmi_power_park_clocks;
	hdmi_power->clk_get_rate = hdmi_power_clk_get_rate;
	hdmi_power->power_client_init = hdmi_power_client_init;
	hdmi_power->power_client_deinit = hdmi_power_client_deinit;
	hdmi_power->power_mmrm_init = hdmi_power_mmrm_init;


	hdmi_power->hdmi_phy_gdsc = devm_regulator_get(dev, "hdmi_phy_gdsc");
	if (IS_ERR(hdmi_power->hdmi_phy_gdsc)) {
		hdmi_power->hdmi_phy_gdsc = NULL;
		HDMI_DEBUG("Optional GDSC regulator is missing\n");
	}

	return hdmi_power;
error:
	return ERR_PTR(rc);
}

void hdmi_power_put(struct hdmi_power *hdmi_power)
{
	struct hdmi_power_private *power = NULL;

	if (!hdmi_power)
		return;

	power = container_of(hdmi_power, struct hdmi_power_private, hdmi_power);

	kfree(power);
}
