/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _HDMI_AUDIO_H_
#define _HDMI_AUDIO_H_

#include <linux/platform_device.h>

#include "hdmi_panel.h"

/**
 * struct hdmi_audio
 * @lane_count: number of lanes configured in current session
 * @bw_code: link rate's bandwidth code for current session
 * @tui_active: set to true if TUI is active in the system
 */
struct hdmi_audio {
	u32 lane_count;
	u32 bw_code;
	u32 pixclk;
	bool tui_active;
	struct hdmi_display *display;

	/**
	 * on()
	 *
	 * Notifies user mode clients that HDMI is powered on, and that audio
	 * playback can start on the external display.
	 *
	 * @audio: an instance of struct hdmi_audio.
	 *
	 * Returns the error code in case of failure, 0 in success case.
	 */
	int (*on)(struct hdmi_audio *audio);

	/**
	 * off()
	 *
	 * Notifies user mode clients that HDMI is shutting down, and audio
	 * playback should be stopped on the external display.
	 *
	 * @audio: an instance of struct hdmi_audio.
	 * @skip_wait: flag to skip any waits
	 *
	 * Returns the error code in case of failure, 0 in success case.
	 */
	int (*off)(struct hdmi_audio *audio, bool skip_wait);
};

/**
 * hdmi_audio_get()
 *
 * Creates and instance of hdmi audio.
 *
 * @pdev: caller's platform device instance.
 * @panel: an instance of hdmi_panel module.
 * @parser: an instance of hdmi_parser.
 *
 * Returns the error code in case of failure, otherwize
 * an instance of newly created hdmi_module.
 */
struct hdmi_audio *hdmi_audio_get(struct platform_device *pdev,
			struct hdmi_panel *panel, struct hdmi_parser *parser);

/**
 * hdmi_audio_put()
 *
 * Cleans the hdmi_audio instance.
 *
 * @hdmi_audio: an instance of hdmi_audio.
 */
void hdmi_audio_put(struct hdmi_audio *audio);

#endif /* _HDMI_AUDIO_H_ */
