// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/version.h>

#include "hdmi_pll.h"

#define HDMI_PLL_DRIVER "hdmi_pll"

struct hdmi_pll_verspec_info {
	u32 revision;
};

static const struct hdmi_pll_verspec_info hdmi_pll_snps_4nm = {
	.revision	= HDMI_PLL_SNPS_4NM,
};

static const struct of_device_id hdmi_pll_of_match[] = {
	{
		.compatible	= "qcom,hdmi-pll-snps-4nm",
		.data		= &hdmi_pll_snps_4nm,
	},
	{}
};

static int hdmi_pll_clock_register(struct hdmi_pll *pll)
{
	int rc = 0;

	switch (pll->revision) {
	case HDMI_PLL_SNPS_4NM:
		rc = hdmi_pll_snps_register(pll);
		break;
	default:
	case HDMI_PLL_UNKNOWN:
		rc = -EOPNOTSUPP;
		break;
	}

	return rc;
}

static void hdmi_pll_clock_unregister(struct hdmi_pll *pll)
{
	switch (pll->revision) {
	case HDMI_PLL_SNPS_4NM:
		hdmi_pll_snps_unregister(pll);
		break;
	default:
	case HDMI_PLL_UNKNOWN:
		HDMI_ERR("invalid pll type");
		break;
	}
}

static struct hdmi_io_data *hdmi_pll_get_io_handle(struct hdmi_pll *pll)
{
	char name[20] = {0};

	switch (pll->revision) {
	case HDMI_PLL_SNPS_4NM:
		memcpy(name, "hdmi_phy", sizeof("hdmi_phy"));
		break;
	default:
	case HDMI_PLL_UNKNOWN:
		HDMI_ERR("invalid pll type");
		return NULL;
	}

	return pll->parser->get_io(pll->parser, name);
}

int hdmi_pll_clock_register_helper(struct hdmi_pll *pll,
		struct hdmi_pll_vco_clk *clks, int num_clks)
{
	int i;
	struct platform_device *pdev;
	struct clk *clk;

	if (!pll || !clks) {
		HDMI_ERR("input not initialized");
		return -EINVAL;
	}

	pdev = pll->pdev;

	for (i = 0; i < num_clks; i++) {
		clks[i].priv = pll;

		/*
		 * Alternative is to use clk_hw_register().
		 * Since clk_register() api has been depreciated, and
		 * clk_hw_register() is recommended to use,
		 * but the support for clk_register() is still existent.
		 */
		clk = clk_register(&pdev->dev, &clks[i].hw);
		if (IS_ERR(clk)) {
			HDMI_ERR("%s registration failed for HDMI: %i",
				clk_hw_get_name(&clks[i].hw), pll->index);
			return -EINVAL;
		}
		pll->clk_data->clks[i] = clk;
	}
	return 0;
}

struct hdmi_pll *hdmi_pll_get(struct platform_device *pdev,
		struct hdmi_parser *parser)
{
	int rc = 0, val = 0;
	struct device_node *node;
	struct platform_device *lpdev;
	struct hdmi_pll *pll;

	if (!pdev) {
		HDMI_ERR("invalid source pdev");
		return ERR_PTR(-EINVAL);
	}

	node = of_parse_phandle(pdev->dev.of_node, "qcom,hdmi-pll", 0);
	if (!node) {
		HDMI_ERR("unable to find hdmi-pll node");
		return ERR_PTR(-EINVAL);
	}

	lpdev = of_find_device_by_node(node);
	if (!lpdev) {
		HDMI_ERR("unable to find hdmi-pll device");
		return ERR_PTR(-ENODEV);
	}

	pll = platform_get_drvdata(lpdev);
	if (!pll) {
		HDMI_ERR("unable to find pll");
		return ERR_PTR(-ENODEV);
	}

	pll->parser = parser;

	rc = of_property_read_u32(pdev->dev.of_node,
			"qcom,pll-clk-factor", &val);
	if (!rc)
		pll->clk_factor = val;
	else
		pll->clk_factor = 1000; /* default value */

	pll->io_data = hdmi_pll_get_io_handle(pll);
	if (!pll->io_data) {
		HDMI_ERR("invalid pll i/o");
		return ERR_PTR(-EINVAL);
	}

	return pll;
}

static int hdmi_pll_driver_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct hdmi_pll *pll;
	const struct of_device_id *id;
	const struct hdmi_pll_verspec_info *ver_info;

	HDMI_DEBUG("Probing");
	if (!pdev || !pdev->dev.of_node) {
		HDMI_ERR("pdev not found");
		return -ENODEV;
	}

	id = of_match_node(hdmi_pll_of_match, pdev->dev.of_node);
	if (!id)
		return -ENODEV;

	ver_info = id->data;
	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll) {
		HDMI_ERR("pll allocation failed");
		return -ENOMEM;
	}

	pll->revision = ver_info->revision;
	pll->name = of_get_property(pdev->dev.of_node, "label", NULL);
	pll->pdev = pdev;

	rc = hdmi_pll_clock_register(pll);
	if (rc)
		goto error;

	HDMI_INFO("revision=%s", hdmi_pll_get_revision(pll->revision));

	platform_set_drvdata(pdev, pll);

	HDMI_DEBUG("Probe success");

	return 0;
error:
	HDMI_ERR("pll clock registration failed: %i", rc);
	kfree(pll);
	return rc;
}

#if (KERNEL_VERSION(6, 12, 0) <= LINUX_VERSION_CODE)
static void hdmi_pll_driver_remove(struct platform_device *pdev)
#else
static int hdmi_pll_driver_remove(struct platform_device *pdev)
#endif
{
	int rc = 0;
	struct hdmi_pll *pll;

	if (!pdev) {
		rc = -EINVAL;
		goto end;
	}

	pll = platform_get_drvdata(pdev);

	hdmi_pll_clock_unregister(pll);

	kfree(pll);
	platform_set_drvdata(pdev, NULL);
end:
#if (KERNEL_VERSION(6, 12, 0) > LINUX_VERSION_CODE)
	return rc;
#else
	return;
#endif
}

static struct platform_driver hdmi_pll_platform_driver = {
	.probe	= hdmi_pll_driver_probe,
	.remove	= hdmi_pll_driver_remove,
	.driver	= {
		.name	= HDMI_PLL_DRIVER,
		.of_match_table	= hdmi_pll_of_match,
	},
};

void hdmi_pll_drv_register(void)
{
	platform_driver_register(&hdmi_pll_platform_driver);
}

void hdmi_pll_drv_unregister(void)
{
	platform_driver_unregister(&hdmi_pll_platform_driver);
}
