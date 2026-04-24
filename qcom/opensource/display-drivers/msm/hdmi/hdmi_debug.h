/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _HDMI_DEBUG_H_
#define _HDMI_DEBUG_H_

#include <linux/sizes.h>
#include <linux/types.h>
#include <drm/drm_print.h>

#define HDMI_DEBUG(fmt, ...) HDMI_DEBUG_V(fmt, ##__VA_ARGS__)

#define HDMI_INFO(fmt, ...) HDMI_INFO_V(fmt, ##__VA_ARGS__)


#define HDMI_WARN(fmt, ...) HDMI_WARN_V(fmt, ##__VA_ARGS__)

#define HDMI_ERR(fmt, ...) HDMI_ERR_V(fmt, ##__VA_ARGS__)

#define HDMI_DEBUG_V(fmt, ...) \
	do { \
		if (drm_debug_enabled(DRM_UT_KMS))                             \
			DRM_DEBUG("[msm-hdmi-debug][%-4d]"fmt, current->pid,   \
					##__VA_ARGS__);                        \
		else                                                           \
			pr_debug("[drm:%s][msm-hdmi-debug][%-4d]"fmt, __func__,\
				       current->pid, ##__VA_ARGS__);           \
	} while (0)

#define HDMI_INFO_V(fmt, ...)                                                 \
	do {                                                                  \
		if (drm_debug_enabled(DRM_UT_KMS))                            \
			DRM_INFO("[msm-hdmi-info][%-4d]"fmt, current->pid,    \
					##__VA_ARGS__);                       \
		else                                                          \
			pr_info("[drm:%s][msm-hdmi-info][%-4d]"fmt, __func__, \
				       current->pid, ##__VA_ARGS__);          \
	} while (0)

#define HDMI_WARN_V(fmt, ...)                                          \
		pr_warn("[drm:%s][msm-hdmi-warn][%-4d]"fmt, __func__,  \
				current->pid, ##__VA_ARGS__)

#define HDMI_WARN_RATELIMITED_V(fmt, ...)                                          \
		pr_warn_ratelimited("[drm:%s][msm-hdmi-warn][%-4d]"fmt, __func__,  \
				current->pid, ##__VA_ARGS__)

#define HDMI_ERR_V(fmt, ...)                                          \
		pr_err("[drm:%s][msm-hdmi-err][%-4d]"fmt, __func__,   \
				current->pid, ##__VA_ARGS__)

#define HDMI_ERR_RATELIMITED_V(fmt, ...)                                        \
		pr_err_ratelimited("[drm:%s][msm-hdmi-err][%-4d]"fmt, __func__, \
				current->pid, ##__VA_ARGS__)

/**
 * struct hdmi_debug
 * @sim_mode: specifies whether sim mode enabled
 * @psm_enabled: specifies whether psm enabled
 * @hdcp_disabled: specifies if hdcp is disabled
 * @hdcp_wait_sink_sync: used to wait for sink synchronization before HDCP auth
 * @tpg_pattern: selects tpg pattern on the controller
 * @max_pclk_khz: max pclk supported
 * @force_encryption: enable/disable forced encryption for HDCP 2.2
 * @skip_uevent: skip hotplug uevent to the user space
 * @hdcp_status: string holding hdcp status information
 * @mst_sim_add_con: specifies whether new sim connector is to be added
 * @mst_sim_remove_con: specifies whether sim connector is to be removed
 * @mst_sim_remove_con_id: specifies id of sim connector to be removed
 * @connect_notification_delay_ms: time (in ms) to wait for any attention
 *              messages before sending the connect notification uevent
 * @disconnect_delay_ms: time (in ms) to wait before turning off the mainlink
 *              in response to HPD low of cable disconnect event
 */
struct hdmi_debug {
	bool sim_mode;
	bool psm_enabled;
	bool hdcp_disabled;
	bool hdcp_wait_sink_sync;
	u32 tpg_pattern;
	u32 max_pclk_khz;
	bool force_encryption;
	bool skip_uevent;
	char hdcp_status[SZ_128];
	bool mst_sim_add_con;
	bool mst_sim_remove_con;
	int mst_sim_remove_con_id;
	unsigned long connect_notification_delay_ms;
	u32 disconnect_delay_ms;

	void (*abort)(struct hdmi_debug *hdmi_debug);
	void (*set_mst_con)(struct hdmi_debug *hdmi_debug, int con_id);
};

/**
 * struct hdmi_debug_in
 * @dev: device instance of the caller
 * @panel: instance of panel module
 * @connector: double pointer to display connector
 * @parser: instance of parser module
 * @ctrl: instance of controller module
 * @pll: instance of pll module
 * @display: instance of display module
 */
struct hdmi_debug_in {
	struct device *dev;
	struct hdmi_panel *panel;
	struct drm_connector *connector;
	struct hdmi_parser *parser;
	struct hdmi_pll *pll;
	struct hdmi_display *display;
};

/**
 * hdmi_debug_get() - configure and get the HDMI debug module data
 *
 * @in: input structure containing data to initialize the debug module
 * return: pointer to allocated debug module data
 *
 * This function sets up the debug module and provides a way
 * for debugfs input to be communicated with existing modules
 */
struct hdmi_debug *hdmi_debug_get(struct hdmi_debug_in *in);

/**
 * hdmi_debug_put()
 *
 * Cleans up hdmi_debug instance
 *
 * @hdmi_debug: instance of hdmi_debug
 */
void hdmi_debug_put(struct hdmi_debug *hdmi_debug);
#endif /* _HDMI_DEBUG_H_ */
