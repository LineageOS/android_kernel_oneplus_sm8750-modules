// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/consumer.h>
#include "hdmi_parser.h"
#include "hdmi_debug.h"
#include "hdmi_pll.h"
#include "hdmi_phy.h"

static void hdmi_parser_unmap_io_resources(struct hdmi_parser *parser)
{
	int i = 0;
	struct hdmi_io *io = &parser->io;

	for (i = 0; i < io->len; i++)
		msm_dss_iounmap(&io->data[i].io);
}

static int hdmi_parser_reg(struct hdmi_parser *parser)
{
	int rc = 0, i = 0;
	u32 reg_count;
	struct platform_device *pdev = parser->pdev;
	struct hdmi_io *io = &parser->io;
	struct device *dev = &pdev->dev;
	size_t alloc_size;

	reg_count = of_property_count_strings(dev->of_node, "reg-names");
	if (reg_count <= 0) {
		HDMI_ERR("no reg defined\n");
		return -EINVAL;
	}

	io->len = reg_count;
	alloc_size = sizeof(struct hdmi_io_data) * reg_count;
	io->data = kzalloc(alloc_size, GFP_KERNEL);
	if (!io->data)
		return -ENOMEM;

	for (i = 0; i < reg_count; i++) {
		of_property_read_string_index(dev->of_node,
				"reg-names", i, &io->data[i].name);
		rc = msm_dss_ioremap_byname(pdev, &io->data[i].io,
				io->data[i].name);
		if (rc) {
			HDMI_ERR("unable to remap %s resources\n",
					io->data[i].name);
			goto err;
		}
	}

	return 0;
err:
	hdmi_parser_unmap_io_resources(parser);
	return rc;
}

static void hdmi_parser_put_clk_data(struct device *dev,
		struct dss_module_power *mp)
{
	if (!mp) {
		DEV_ERR("%s: invalid input\n", __func__);
		return;
	}

	kfree(mp->clk_config);
	mp->clk_config = NULL;

	mp->num_clk = 0;
}

static bool hdmi_parser_check_prefix(const char *clk_prefix, const char *clk_name)
{
	return !!strnstr(clk_name, clk_prefix, strlen(clk_name));
}

static int hdmi_parser_clock(struct hdmi_parser *parser)
{
	int i = 0;
	size_t alloc_size;
	struct device *dev = &parser->pdev->dev;
	struct dss_module_power *core_mp = &parser->mp[HDMI_CORE_PM];
	struct dss_module_power *pclk_mp = &parser->mp[HDMI_PCLK_PM];
	const char *core_clk = "core";
	const char *pclk_clk = "pixel";
	int core_clk_count = 0, pclk_clk_count = 0;
	int core_clk_index = 0, pclk_clk_index = 0;
	const char *clk_name;
	int num_clk, rc;

	num_clk = of_property_count_strings(dev->of_node, "clock-names");
	if (num_clk <= 0) {
		HDMI_ERR("no clocks are defined\n");
		rc = -EINVAL;
		goto exit;
	}

	for (i = 0; i < num_clk; i++) {
		of_property_read_string_index(dev->of_node,
				"clock-names", i, &clk_name);

		if (hdmi_parser_check_prefix(core_clk, clk_name))
			core_clk_count++;

		if (hdmi_parser_check_prefix(pclk_clk, clk_name))
			pclk_clk_count++;
	}

	core_mp->num_clk = core_clk_count;
	alloc_size = sizeof(struct dss_clk) * core_clk_count;
	core_mp->clk_config = kzalloc(alloc_size, GFP_KERNEL);
	if (!core_mp->clk_config) {
		rc = -ENOMEM;
		goto exit;
	}

	pclk_mp->num_clk = pclk_clk_count;
	alloc_size = sizeof(struct dss_clk) * pclk_clk_count;
	pclk_mp->clk_config = kzalloc(alloc_size, GFP_KERNEL);
	if (!pclk_mp->clk_config) {
		rc = -ENOMEM;
		goto error_core_clk;
	}

	for (i = 0; i < num_clk; i++) {
		of_property_read_string_index(dev->of_node,
				"clock-names", i, &clk_name);

		if (hdmi_parser_check_prefix(core_clk, clk_name) &&
				core_clk_index < core_clk_count) {
			struct dss_clk *clk =
				&core_mp->clk_config[core_clk_index];

			strscpy(clk->clk_name, clk_name, sizeof(clk->clk_name));
			clk->type = DSS_CLK_AHB;
			core_clk_index++;
		}

		if (hdmi_parser_check_prefix(pclk_clk, clk_name) &&
				pclk_clk_index < pclk_clk_count) {
			struct dss_clk *clk =
				&pclk_mp->clk_config[pclk_clk_index];

			strscpy(clk->clk_name, clk_name, sizeof(clk->clk_name));
			clk->type = DSS_CLK_PCLK;
			pclk_clk_index++;
		}
	}

	return 0;
error_core_clk:
	hdmi_parser_put_clk_data(&parser->pdev->dev, core_mp);
exit:
	return rc;
}

static struct hdmi_io_data *hdmi_parser_get_io(struct hdmi_parser *parser,
		char *name)
{
	int i = 0;
	struct hdmi_io *io;

	if (!parser) {
		HDMI_ERR("invalid input\n");
		goto err;
	}

	io = &parser->io;

	for (i = 0; i < io->len; i++) {
		struct hdmi_io_data *data = &io->data[i];

		if (!strcmp(data->name, name))
			return data;
	}
err:
	return NULL;
}

static void hdmi_parser_get_io_buf(struct hdmi_parser *parser, char *name)
{
	int i = 0;
	struct hdmi_io *io;

	if (!parser) {
		HDMI_ERR("invalid input\n");
		return;
	}

	io = &parser->io;

	for (i = 0; i < io->len; i++) {
		struct hdmi_io_data *data = &io->data[i];

		if (!strcmp(data->name, name)) {
			if (!data->buf)
				data->buf = kzalloc(data->io.len, GFP_KERNEL);
		}
	}
}

static void hdmi_parser_clear_io_buf(struct hdmi_parser *parser)
{
	int i = 0;
	struct hdmi_io *io;

	if (!parser) {
		HDMI_ERR("invalid input\n");
		return;
	}

	io = &parser->io;

	for (i = 0; i < io->len; i++) {
		struct hdmi_io_data *data = &io->data[i];

		kfree(data->buf);
		data->buf = NULL;
	}
}


static int hdmi_parser_ddc(struct hdmi_parser *parser)
{
	return 0;
}


static int hdmi_parser_misc(struct hdmi_parser *parser)
{
	int rc = 0;
	struct device_node *of_node = parser->pdev->dev.of_node;

	rc = of_property_read_u32(of_node,
			"qcom,max-pclk-frequency-khz", &parser->max_pclk_khz);
	if (rc)
		parser->max_pclk_khz = HDMI_MAX_PIXEL_CLK_KHZ;

	parser->non_pluggable = of_property_read_bool(of_node,
			"qcom,non-pluggable");

	parser->skip_ddc = of_property_read_bool(of_node,
			"qcom,skip-ddc");

	parser->display_type = of_get_property(of_node,
			"qcom,display-type", NULL);
	if (!parser->display_type)
		parser->display_type = "unknown";

	if (of_property_read_bool(of_node,
			"qcom,hdmi-phy-snps-4nm"))
		parser->phy_version = HDMI_PHY_SNPS_V4NM;

	return 0;
}

static const char *hdmi_parser_supply_node_name(enum hdmi_pm_type module)
{
	switch (module) {
	case HDMI_CORE_PM:	return "qcom,core-supply-entries";
	case HDMI_CTRL_PM:	return "qcom,ctrl-supply-entries";
	case HDMI_PHY_PM:	return "qcom,phy-supply-entries";
	case HDMI_PLL_PM:	return "qcom,pll-supply-entries";
	case HDMI_HPD_PM:	return "qcom,hpd-supply-entries";
	default:		return "???";
	}
}

static int hdmi_parser_get_vreg(struct hdmi_parser *parser,
		enum hdmi_pm_type module)
{
	int i = 0, rc = 0;
	u32 tmp = 0;
	size_t alloc_size;
	const char *pm_supply_name = NULL;
	struct device_node *supply_node = NULL;
	struct device_node *of_node = parser->pdev->dev.of_node;
	struct device_node *supply_root_node = NULL;
	struct dss_module_power *mp = &parser->mp[module];

	mp->num_vreg = 0;
	pm_supply_name = hdmi_parser_supply_node_name(module);
	supply_root_node = of_get_child_by_name(of_node, pm_supply_name);
	if (!supply_root_node) {
		HDMI_DEBUG("no supply entry present: %s\n", pm_supply_name);
		goto novreg;
	}

	mp->num_vreg = of_get_available_child_count(supply_root_node);

	if (mp->num_vreg == 0) {
		HDMI_DEBUG("no vreg\n");
		goto novreg;
	} else {
		HDMI_DEBUG("vreg found. count=%d\n", mp->num_vreg);
	}

	alloc_size = sizeof(struct dss_vreg) * mp->num_vreg;
	mp->vreg_config = kzalloc(alloc_size, GFP_KERNEL);
	if (!mp->vreg_config) {
		rc = -ENOMEM;
		goto error;
	}

	for_each_child_of_node(supply_root_node, supply_node) {
		const char *st = NULL;
		/* vreg-name */
		rc = of_property_read_string(supply_node,
			"qcom,supply-name", &st);
		if (rc) {
			HDMI_ERR("error reading name. rc=%d\n",
				 rc);
			goto error;
		}
		snprintf(mp->vreg_config[i].vreg_name,
			ARRAY_SIZE((mp->vreg_config[i].vreg_name)), "%s", st);
		/* vreg-min-voltage */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-min-voltage", &tmp);
		if (rc) {
			HDMI_ERR("error reading min volt. rc=%d\n",
				rc);
			goto error;
		}
		mp->vreg_config[i].min_voltage = tmp;

		/* vreg-max-voltage */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-max-voltage", &tmp);
		if (rc) {
			HDMI_ERR("error reading max volt. rc=%d\n",
				rc);
			goto error;
		}
		mp->vreg_config[i].max_voltage = tmp;

		/* enable-load */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-enable-load", &tmp);
		if (rc) {
			HDMI_ERR("error reading enable load. rc=%d\n",
				rc);
			goto error;
		}
		mp->vreg_config[i].enable_load = tmp;

		/* disable-load */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-disable-load", &tmp);
		if (rc) {
			HDMI_ERR("error reading disable load. rc=%d\n",
				rc);
			goto error;
		}
		mp->vreg_config[i].disable_load = tmp;

		HDMI_DEBUG("%s min=%d, max=%d, enable=%d, disable=%d\n",
			mp->vreg_config[i].vreg_name,
			mp->vreg_config[i].min_voltage,
			mp->vreg_config[i].max_voltage,
			mp->vreg_config[i].enable_load,
			mp->vreg_config[i].disable_load
			);
		++i;
	}

	return rc;

error:
	kfree(mp->vreg_config);
	mp->vreg_config = NULL;
novreg:
	mp->num_vreg = 0;

	return rc;
}

static void hdmi_parser_put_vreg_data(struct device *dev,
	struct dss_module_power *mp)
{
	if (!mp) {
		DEV_ERR("invalid input\n");
		return;
	}

	kfree(mp->vreg_config);
	mp->vreg_config = NULL;
	mp->num_vreg = 0;
}

static int hdmi_parser_regulator(struct hdmi_parser *parser)
{
	int i, rc = 0;
	struct platform_device *pdev = parser->pdev;

	/* Parse the regulator information */
	for (i = HDMI_CORE_PM; i < HDMI_MAX_PM; i++) {
		rc = hdmi_parser_get_vreg(parser, i);
		if (rc) {
			HDMI_ERR("get_dt_vreg_data failed for %s. rc=%d\n",
				hdmi_parser_pm_name(i), rc);
			i--;
			for (; i >= HDMI_CORE_PM; i--)
				hdmi_parser_put_vreg_data(&pdev->dev,
					&parser->mp[i]);
			break;
		}
	}

	return rc;
}

static void hdmi_parser_put_gpio_data(struct device *dev,
	struct dss_module_power *mp)
{
	if (!mp) {
		DEV_ERR("%s: invalid input\n", __func__);
		return;
	}

	kfree(mp->gpio_config);
	mp->gpio_config = NULL;
	mp->num_gpio = 0;
}

static int hdmi_parser_gpio(struct hdmi_parser *parser)
{
	int i = 0;
	size_t alloc_size;
	struct device *dev = &parser->pdev->dev;
	struct device_node *of_node = dev->of_node;
	struct dss_module_power *mp = &parser->mp[HDMI_CORE_PM];
	static const char * const hdmi_gpios[HDMI_GPIO_MAX] = {
		"qcom,ddc-clk-gpio",
		"qcom,ddc-data-gpio",
		"qcom,hdmi-hpd-gpio",
		"qcom,mux-en-gpio",
		"qcom,mux-sel-gpio",
		"qcom,mux-lpm-gpio",
		"qcom,hpd5v-gpio",
		"qcom,hdmi-cec-gpio",
	};

	alloc_size = sizeof(struct dss_gpio) * HDMI_GPIO_MAX;
	mp->gpio_config = kzalloc(alloc_size, GFP_KERNEL);

	if (!mp->gpio_config)
		return -ENOMEM;

	mp->num_gpio = ARRAY_SIZE(hdmi_gpios);

	for (i = 0; i < ARRAY_SIZE(hdmi_gpios); i++) {
		mp->gpio_config[i].gpio = of_get_named_gpio(of_node,
			hdmi_gpios[i], 0);

		if (!gpio_is_valid(mp->gpio_config[i].gpio)) {
			HDMI_DEBUG("%s gpio not specified\n", hdmi_gpios[i]);
			continue;
		}

		strscpy(mp->gpio_config[i].gpio_name, hdmi_gpios[i],
			sizeof(mp->gpio_config[i].gpio_name));

		mp->gpio_config[i].value = 0;
	}

	return 0;
}

static int hdmi_parser_pinctrl(struct hdmi_parser *parser)
{
	int rc = 0;
	struct hdmi_pinctrl *pinctrl = &parser->pinctrl;

	pinctrl->pin = devm_pinctrl_get(&parser->pdev->dev);

	if (IS_ERR_OR_NULL(pinctrl->pin)) {
		HDMI_DEBUG("failed to get pinctrl, rc=%d\n", rc);
		goto error;
	}

	pinctrl->state_active = pinctrl_lookup_state(pinctrl->pin,
					"mdss_hdmi_active");
	if (IS_ERR_OR_NULL(pinctrl->state_active)) {
		rc = PTR_ERR(pinctrl->state_active);
		HDMI_ERR("failed to get pinctrl active state, rc=%d\n", rc);
		goto error;
	}

	pinctrl->state_suspend = pinctrl_lookup_state(pinctrl->pin,
					"mdss_hdmi_sleep");
	if (IS_ERR_OR_NULL(pinctrl->state_suspend)) {
		rc = PTR_ERR(pinctrl->state_suspend);
		HDMI_ERR("failed to get pinctrl suspend state, rc=%d\n", rc);
		goto error;
	}
error:
	return rc;
}

static int hdmi_parser_msm_hdcp_dev(struct hdmi_parser *parser)
{
	struct device_node *node;
	struct platform_device *pdev;

	node = of_find_compatible_node(NULL, NULL, "qcom,msm-hdcp");
	if (!node) {
		// This is a non-fatal error, module initialization can proceed
		HDMI_WARN("couldn't find msm-hdcp node\n");
		return 0;
	}

	pdev = of_find_device_by_node(node);
	if (!pdev) {
		// This is a non-fatal error, module initialization can proceed
		HDMI_WARN("couldn't find msm-hdcp pdev\n");
		return 0;
	}

	parser->msm_hdcp_dev = &pdev->dev;

	return 0;
}


static int hdmi_parser_parse(struct hdmi_parser *parser)
{
	int rc = 0;

	if (!parser) {
		HDMI_ERR("invalid input\n");
		rc = -EINVAL;
		goto err;
	}

	rc = hdmi_parser_reg(parser);
	if (rc)
		goto err;

	rc = hdmi_parser_ddc(parser);
	if (rc)
		goto err;

	rc = hdmi_parser_misc(parser);
	if (rc)
		goto err;

	rc = hdmi_parser_clock(parser);
	if (rc)
		goto err;

	rc = hdmi_parser_regulator(parser);
	if (rc)
		goto err;

	rc = hdmi_parser_gpio(parser);
	if (rc)
		goto err;

	rc = hdmi_parser_pinctrl(parser);
	if (rc)
		goto err;

	rc = hdmi_parser_msm_hdcp_dev(parser);
	if (rc)
		goto err;

err:
	return rc;
}

struct hdmi_parser *hdmi_parser_get(struct platform_device *pdev)
{
	struct hdmi_parser *parser;

	parser = kzalloc(sizeof(*parser), GFP_KERNEL);
	if (!parser)
		return ERR_PTR(-ENOMEM);

	parser->parse = hdmi_parser_parse;
	parser->get_io = hdmi_parser_get_io;
	parser->get_io_buf = hdmi_parser_get_io_buf;
	parser->clear_io_buf = hdmi_parser_clear_io_buf;
	parser->pdev = pdev;

	return parser;
}

void hdmi_parser_put(struct hdmi_parser *parser)
{
	int i = 0;
	struct dss_module_power *power = NULL;

	if (!parser) {
		HDMI_ERR("invalid parser module\n");
		return;
	}

	power = parser->mp;

	for (i = 0; i < HDMI_MAX_PM; i++) {
		hdmi_parser_put_clk_data(&parser->pdev->dev, &power[i]);
		hdmi_parser_put_vreg_data(&parser->pdev->dev, &power[i]);
		hdmi_parser_put_gpio_data(&parser->pdev->dev, &power[i]);
	}

	hdmi_parser_clear_io_buf(parser);
	kfree(parser->io.data);
	kfree(parser);
}
