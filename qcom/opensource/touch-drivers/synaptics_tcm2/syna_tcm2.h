/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Synaptics TouchComm touchscreen driver
 *
 * Copyright (C) 2017-2025 Synaptics Incorporated. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * INFORMATION CONTAINED IN THIS DOCUMENT IS PROVIDED "AS-IS," AND SYNAPTICS
 * EXPRESSLY DISCLAIMS ALL EXPRESS AND IMPLIED WARRANTIES, INCLUDING ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE,
 * AND ANY WARRANTIES OF NON-INFRINGEMENT OF ANY INTELLECTUAL PROPERTY RIGHTS.
 * IN NO EVENT SHALL SYNAPTICS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, PUNITIVE, OR CONSEQUENTIAL DAMAGES ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OF THE INFORMATION CONTAINED IN THIS DOCUMENT, HOWEVER CAUSED
 * AND BASED ON ANY THEORY OF LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, AND EVEN IF SYNAPTICS WAS ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE. IF A TRIBUNAL OF COMPETENT JURISDICTION DOES
 * NOT PERMIT THE DISCLAIMER OF DIRECT DAMAGES OR ANY OTHER DAMAGES, SYNAPTICS'
 * TOTAL CUMULATIVE LIABILITY TO ANY PARTY SHALL NOT EXCEED ONE HUNDRED U.S.
 * DOLLARS.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

/*
 * The header file for the Synaptics TouchComm reference driver.
 */

#ifndef _SYNAPTICS_TCM2_DRIVER_H_
#define _SYNAPTICS_TCM2_DRIVER_H_

#include "syna_tcm2_platform.h"
#include "tcm/synaptics_touchcom_core_dev.h"
#include "tcm/synaptics_touchcom_func_base.h"

#define PLATFORM_DRIVER_NAME "synaptics_tcm"

#define TOUCH_INPUT_NAME "synaptics_tcm_touch"
#define TOUCH_INPUT_PHYS_PATH "synaptics_tcm/touch_input"

#define CHAR_DEVICE_NAME "tcm"
#define CHAR_DEVICE_MODE (0x0600)

#define SYNAPTICS_TCM_DRIVER_ID (1 << 0)
#define SYNAPTICS_TCM_DRIVER_VERSION 1
#define SYNAPTICS_TCM_DRIVER_SUBVER "11.0"


/*
 * Module Configuration Flags
 */

/* Enable sysfs interface */
#if defined(CONFIG_TOUCHSCREEN_SYNA_TCM2_SYSFS)
#define HAS_SYSFS_INTERFACE
#endif

/* Enable firmware reflash support */
#if defined(CONFIG_TOUCHSCREEN_SYNA_TCM2_REFLASH)
#define HAS_REFLASH_FEATURE
#ifdef TOUCHCOMM_TDDI
#define REFLASH_TDDI
#else
#define REFLASH_DISCRETE_TOUCH
#endif
#endif
/* Enable testing features (requires sysfs interface) */
#if defined(CONFIG_TOUCHSCREEN_SYNA_TCM2_TESTING) && defined(HAS_SYSFS_INTERFACE)
#define HAS_TESTING_FEATURE
#endif


/*
 * Driver Configuration Flags
 */

/* Use Type B (multi-touch) input protocol */
#define TYPE_B_PROTOCOL

/* Reset the touch controller on device connect */
#define RESET_ON_CONNECT

/* Reset the touch controller on system resume */
/* #define RESET_ON_RESUME */

#if defined(RESET_ON_RESUME)
/* Use hardware reset on resume */
/* #define HW_RESET_ON_RESUME */
#endif

/* Enter low power mode during system suspend */
/* #define LOW_POWER_MODE */

#if defined(LOW_POWER_MODE)
/* Enable wake-up gesture support */
/* #define ENABLE_WAKEUP_GESTURE */
#endif

/* Transform reported touch coordinates before input event processing */
/* #define REPORT_SWAP_XY */
/* #define REPORT_FLIP_X */
/* #define REPORT_FLIP_Y */

/* Include touch width in input reports */
#define REPORT_TOUCH_WIDTH

#if defined(TOUCHCOMM_TDDI)
/* Report knob input events */
#define REPORT_KNOB

#ifdef REPORT_KNOB
#define KNOB_INPUT_NAME "synaptics_tcm_knob"
#define KNOB_INPUT_PHYS_PATH "synaptics_tcm/knob_input"
#endif

/* Support a second knob input */
/* #define HAVE_THE_SECOND_KNOB */
#endif

/* Use a custom touch report format defined in syna_tcm2.c */
/* #define USE_CUSTOM_TOUCH_REPORT_CONFIG */

/* Parse custom touch entity codes */
/* #define ENABLE_CUSTOM_TOUCH_ENTITY */

/* Check and update firmware at startup */
#if defined(HAS_REFLASH_FEATURE)
/* #define STARTUP_REFLASH */

#define FW_IMAGE_NAME "synaptics/firmware.img"


/* Enable recovery mode through firmware flashing */
/* #define FLASH_RECOVERY */
#endif

/* Listen to display driver events via notifier */
#if defined(CONFIG_FB) || defined(CONFIG_DRM_BRIDGE)
/* #define ENABLE_DISP_NOTIFIER */
#endif
/* Use framebuffer notifier (if CONFIG_FB is enabled) */
#if defined(ENABLE_DISP_NOTIFIER) && defined(CONFIG_FB)
#define USE_FB
#endif
/* Resume early during display unblanking */
#ifdef ENABLE_DISP_NOTIFIER
/* #define RESUME_EARLY_UNBLANK */
#endif
/* Use DRM bridge notifier (if CONFIG_DRM_BRIDGE is enabled) */
#if defined(ENABLE_DISP_NOTIFIER) && defined(CONFIG_DRM_BRIDGE)
#define USE_DRM_BRIDGE
#endif

/* Forward frame data to userspace */
#define ENABLE_EXTERNAL_FRAME_PROCESS

/* Allow driver installation even if errors occur */
#define FORCE_CONNECTION

/* Perform additional tasks in background workqueue */
/* #define ENABLE_HELPER */

/* Enable support for TDDI multichip architecture */
#if defined(TOUCHCOMM_TDDI)
/* #define IS_TDDI_MULTICHIP */
#endif

/* Enable to store the PID of user application */
/* #define PID_TASK */


/*
 * Definitions for TouchComm device driver
 */


/* Enumeration for the power states */
enum power_state {
	PWR_OFF = 0,
	PWR_ON,
	LOW_PWR,
	LOW_PWR_GESTURE,
	BARE_MODE,
};

#if defined(ENABLE_HELPER)
/* Definitions of the background helper thread */
enum helper_task {
	HELP_NONE = 0,
	HELP_RESET_DETECTED,
};

struct syna_tcm_helper {
	syna_pal_atomic_t task;
	struct work_struct work;
	struct workqueue_struct *workqueue;
};
#endif

/*
 * Synaptics TouchComm driver context
 *
 * The structure defines the kernel specific data and driver-relevant state data
 */
struct syna_tcm {

	/* Core TouchComm device context */
	struct tcm_dev *tcm_dev;

	/* Platform device handle */
	struct platform_device *pdev;

	/* Touch data and status tracking */
	struct tcm_touch_data_blob tp_data;
	unsigned char prev_obj_status[MAX_NUM_OBJECTS];

	/* Hardware interface abstraction */
	struct syna_hw_interface *hw_if;

	/* IRQ handling */
	struct mutex tp_event_mutex;
	struct tcm_buffer event_data;
	pid_t isr_pid;
	bool irq_wake;

	/* Character device interface */
	struct cdev char_dev;
	dev_t char_dev_num;
	int char_dev_ref_count;
	struct class *device_class;
	struct device *device;

#if defined(HAS_SYSFS_INTERFACE)
	/* Sysfs attribute nodes */
	struct kobject *sysfs_dir;
	struct kobject *sysfs_dbg_dir;
#if defined(HAS_TESTING_FEATURE)
	struct kobject *sysfs_testing_dir;
#endif
#endif

	/* Input device registration */
	struct input_dev *input_dev;
	struct input_params {
		unsigned int max_x;
		unsigned int max_y;
		unsigned int max_objects;
	} input_dev_params;
#ifdef REPORT_KNOB
	struct input_dev *input_knob_dev[MAX_NUM_KNOB_OBJECTS];
#endif

#if defined(STARTUP_REFLASH)
	/* Workqueue for firmware update */
	struct delayed_work reflash_work;
	struct workqueue_struct *reflash_workqueue;
#endif

#if defined(ENABLE_DISP_NOTIFIER)
#if defined(USE_FB)
	struct notifier_block fb_notifier;
	unsigned char fb_ready;
#endif
#if defined(USE_DRM_BRIDGE)
	struct drm_bridge panel_bridge;
	struct drm_connector *connector;
	bool is_panel_lp_mode;
#endif
#endif

#ifdef PID_TASK
	pid_t proc_pid;
	struct task_struct *proc_task;
#endif

#if defined(ENABLE_EXTERNAL_FRAME_PROCESS)
	/* Kernel FIFO for data forwarding to userspace */
	unsigned int fifo_remaining_frame;
	struct list_head frame_fifo_queue;
	wait_queue_head_t wait_frame;
	struct mutex fifo_queue_mutex;
	unsigned int fifo_depth;
#endif

#if defined(ENABLE_HELPER)
	/* Background workqueue */
	struct syna_tcm_helper helper;
#endif

	/* Driver state flags */
	int pwr_state;
	bool lpwg_enabled;
	bool is_connected;
	bool init_done;
#if defined(TOUCHCOMM_TDDI)
	bool is_tddi_multichip;
#endif
	bool concurrent_reporting;
	struct completion init_completed;

	/* Misc. for the character device access */
	void *userspace_app_info;
	struct tcm_buffer cdev_buffer;
	struct mutex cdev_mutex;
	unsigned int cdev_polling_interval;
	int cdev_extra_bytes;
	unsigned int cdev_origin_max_wr_size;
	unsigned int cdev_origin_max_rd_size;

	/* Abstraction helpers */
	int (*dev_connect)(struct syna_tcm *tcm);
	int (*dev_disconnect)(struct syna_tcm *tcm);
	int (*dev_set_up_app_fw)(struct syna_tcm *tcm);
	int (*dev_resume)(struct device *dev);
	int (*dev_suspend)(struct device *dev);

	/* TVM-specific fields */
	struct mutex tui_transition_lock;
	bool qts_enabled;

#if defined(CONFIG_DRM)
	/* Panel notifier for power management */
	void *notifier_cookie;
#endif
};

/* Helpers for the character device registration */
int syna_cdev_create(struct syna_tcm *ptcm);
void syna_cdev_remove(struct syna_tcm *ptcm);

#if defined(REFLASH_DISCRETE_TOUCH) || defined(REFLASH_TDDI)
/* Helper to perform firmware update */
int syna_dev_do_reflash(struct syna_tcm *tcm, bool force);
#endif

#ifdef HAS_SYSFS_INTERFACE
/* Helpers for the sysfs attributes registration */
int syna_sysfs_create_dir(struct syna_tcm *tcm);
void syna_sysfs_remove_dir(struct syna_tcm *tcm);

#ifdef HAS_TESTING_FEATURE
/* Helpers for the sysfs attributes of production testing */
int syna_testing_create_dir(struct syna_tcm *tcm);
void syna_testing_remove_dir(struct syna_tcm *tcm);
#endif

#endif

#endif /* end of _SYNAPTICS_TCM2_DRIVER_H_ */
