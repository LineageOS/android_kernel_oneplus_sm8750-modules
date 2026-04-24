// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "hdmi_phy.h"

#define HDMI_PHY_DRIVER "hdmi_phy"

struct hdmi_phy_verspec_info {
	int version;
};

static const struct hdmi_phy_verspec_info hdmi_phy_vsnps = {
	.version = HDMI_PHY_SNPS_V4NM,
};

static inline void hdmi_phy_register(struct hdmi_phy *phy)
{
	int version = phy->version;

	switch (version) {
	case HDMI_PHY_SNPS_V4NM:
		hdmi_phy_snps_register(phy);
		break;
	default:
	case HDMI_PHY_UNKNOWN:
		HDMI_ERR("invalid phy version");
		break;
	}
}

static inline void hdmi_phy_unregister(struct hdmi_phy *phy)
{
	int version = phy->version;

	switch (version) {
	case HDMI_PHY_SNPS_V4NM:
		hdmi_phy_snps_unregister(phy);
		break;
	default:
	case HDMI_PHY_UNKNOWN:
		HDMI_ERR("invalid phy version");
		break;
	}
}

struct hdmi_phy *hdmi_phy_get(struct platform_device *pdev,
			struct hdmi_parser *parser)
{
	struct hdmi_phy *phy;

	if (!pdev || !parser) {
		HDMI_ERR("invalid input");
		return ERR_PTR(-EINVAL);
	}

	phy = kzalloc(sizeof(*phy), GFP_KERNEL);
	if (!phy) {
		HDMI_ERR("phy allocation failed");
		return ERR_PTR(-ENOMEM);
	}

	phy->version = parser->phy_version;
	phy->parser = parser;
	phy->io_data = parser->get_io(parser, "hdmi_phy");
	phy->parser->alternate_pll = false;

	hdmi_phy_register(phy);

	HDMI_DEBUG("success");

	return phy;
}

void hdmi_phy_put(struct hdmi_phy *phy)
{
	if (!phy) {
		HDMI_ERR("invalid input");
		return;
	}

	hdmi_phy_unregister(phy);
	kfree(phy);
}
