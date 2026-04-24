/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _HDMI_DISPLAY_H_
#define _HDMI_DISPLAY_H_

#include <linux/list.h>
#include <drm/sde_drm.h>

#include "hdmi_drm.h"
#include "hdmi_panel.h"

#define MAX_HDMI_ACTIVE_DISPLAY 8

struct hdmi_display {
	struct drm_device *drm_dev;
	struct hdmi_bridge *bridge;
	struct drm_connector *base_connector;

	const char *name;

	bool non_pluggable;
	bool dsc_cont_pps;
	bool connected;
	u32 pixclk;
	u32 max_pclk_khz;
	u32 max_mixer_count;
	u32 max_dsc_count;

	int (*enable)(struct hdmi_display *hdmi_display, void *panel);
	int (*post_enable)(struct hdmi_display *hdmi_display, void *panel);

	int (*pre_disable)(struct hdmi_display *hdmi_display, void *panel);
	int (*disable)(struct hdmi_display *hdmi_display, void *panel);

	int (*set_mode)(struct hdmi_display *hdmi_display, void *panel,
					const struct drm_display_mode *mode);
	enum drm_mode_status (*validate_mode)(struct hdmi_display *hdmi_display,
						void *panel, struct drm_display_mode *mode,
						const struct msm_resource_caps_info *avail_res);
	int (*get_modes)(struct hdmi_display *hdmi_display, void *panel);
	int (*prepare)(struct hdmi_display *hdmi_display, void *panel);
	int (*unprepare)(struct hdmi_display *hdmi_display, void *panel);
	int (*request_irq)(struct hdmi_display *hdmi_display);

	struct hdmi_debug *(*get_debug)(struct hdmi_display *hdmi_display);
	void (*post_open)(struct hdmi_display *hdmi_display);
	int (*config_hdr)(struct hdmi_display *hdmi_display, void *panel,
				struct drm_msm_ext_hdr_metadata *hdr_meta,
				bool dhdr_update);
	int (*set_colorspace)(struct hdmi_display *hdmi_display, void *panel,
				u32 colorspace);
	int (*post_init)(struct hdmi_display *hdmi_display);
	int (*update_pps)(struct hdmi_display *hdmi_display,
			struct drm_connector *connector, char *pps_cmd);
	int (*get_available_hdmi_resources)(struct hdmi_display *hdmi_display,
			const struct msm_resource_caps_info *avail_res,
			struct msm_resource_caps_info *max_hdmi_avail_res);
	int (*get_display_type)(struct hdmi_display *hdmi_display,
			const char **display_type);
};

#if IS_ENABLED(CONFIG_DRM_MSM_HDMI)
int hdmi_display_get_num_of_displays(struct drm_device *dev);
int hdmi_display_get_displays(struct drm_device *dev, void **displays, int count);
#else
static inline int hdmi_display_get_num_of_displays(struct drm_device *dev)
{
	return 0;
}
static inline int hdmi_display_get_displays(struct drm_device *dev, void **displays, int count)
{
	return 0;
}
static inline int hdmi_connector_update_pps(struct drm_connector *connector,
		char *pps_cmd, void *display)
{
	return 0;
}
#endif /* CONFIG_DRM_MSM_HDMI */

int hdmi_display_get_num_of_displays(struct drm_device *dev);
#endif /* _HDMI_DISPLAY_H_ */
