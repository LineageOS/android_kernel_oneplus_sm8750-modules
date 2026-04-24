/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _HDMI_PANEL_H_
#define _HDMI_PANEL_H_

#include "msm_drv.h"
#include "hdmi_parser.h"

struct hdmi_panel_info {
	u32 htotal;
	u32 vtotal;
	u32 h_active;
	u32 v_active;
	u32 h_start;
	u32 h_end;
	u32 v_start;
	u32 v_end;
	u32 h_back_porch;
	u32 h_front_porch;
	u32 h_sync_width;
	u32 h_active_low;
	u32 v_back_porch;
	u32 v_front_porch;
	u32 v_sync_width;
	u32 v_active_low;
	u32 h_skew;
	u32 refresh_rate;
	u32 pixel_clk_khz;
	u32 bpp;
	bool interlace;
	struct msm_compression_info comp_info;
};


struct hdmi_display_mode {
	struct hdmi_panel_info timing;
	u32 capabilities;
	/**
	 * @output_format:
	 *
	 * This is used to indicate DP output format.
	 * The output format can be read from drm_mode.
	 */
	//enum hdmi_output_format output_format;
	u32 lm_count;

	bool interlace;
};

struct hdmi_panel {
	int stream_id;
	struct hdmi_panel_info pinfo;
	struct sde_edid_ctrl *edid_ctrl;
	u32 pclk_factor;
	u32 max_lm;
	bool mode_override;
	bool dc_enable;
	int hdisplay;
	int vdisplay;
	int vrefresh;
	int aspect_ratio;

	/* DRM connector assosiated with this panel */
	struct drm_connector *connector;

	enum drm_mode_status (*validate_mode)(struct hdmi_panel *hdmi_panel,
			const struct drm_display_mode *drm_mode);
	int (*set_mode)(struct hdmi_panel *hdmi_panel,
			const struct drm_display_mode *mode);
	int (*get_modes)(struct hdmi_panel *hdmi_panel,
			struct drm_connector *connector);
	int (*enable)(struct hdmi_panel *hdmi_panel);
	int (*disable)(struct hdmi_panel *hdmi_panel);
	int (*get_panel_on)(struct hdmi_panel *hdmi_panel);
	int (*setup_hdr)(struct hdmi_panel *hdmi_panel,
		struct drm_msm_ext_hdr_metadata *hdr_meta,
		bool dhdr_update, u64 core_clk_rate, bool flush);
	void (*update_pps)(struct hdmi_panel *hdmi_panel, char *pps_cmd);
	int (*set_colorspace)(struct hdmi_panel *hdmi_panel,
		u32 colorspace);
};

struct hdmi_panel *hdmi_panel_get(struct device *dev,
		struct drm_connector *connector, struct hdmi_parser *parser);
void hdmi_panel_put(struct hdmi_panel *hdmi_panel);

#endif /* _HDMI_PANEL_H_ */
