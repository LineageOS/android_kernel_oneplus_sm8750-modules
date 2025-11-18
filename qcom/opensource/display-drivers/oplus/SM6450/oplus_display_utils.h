/***************************************************************
** Copyright (C), 2024, OPLUS Mobile Comm Corp., Ltd
**
** File : oplus_display_utils.h
** Description : display driver private utils
** Version : 1.0
** Date : 2024/05/09
** Author : Display
******************************************************************/
#ifndef _OPLUS_DISPLAY_UTILS_H_
#define _OPLUS_DISPLAY_UTILS_H_

#include <linux/soc/qcom/panel_event_notifier.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/notifier.h>
#include "dsi_display.h"
#include "dsi_panel.h"
#include "oplus_panel.h"

/* A hardware display blank change occurred */
#define OPLUS_DISPLAY_EVENT_BLANK           0x01

/* A hardware display blank early change occurred */
#define OPLUS_DISPLAY_EARLY_EVENT_BLANK     0x02

enum oplus_display_support_list {
	OPLUS_SAMSUNG_ANA6706_DISPLAY_FHD_DSC_CMD_PANEL = 0,
	OPLUS_SAMSUNG_ONEPLUS_DISPLAY_FHD_DSC_CMD_PANEL,
	OPLUS_DISPLAY_UNKNOW,
};

enum oplus_display_dre_status {
	OPLUS_DISPLAY_DRE_OFF = 0,
	OPLUS_DISPLAY_DRE_ON,
	OPLUS_DISPLAY_DRE_UNKNOW,
};

typedef struct panel_serial_info {
	int reg_index;
	uint64_t year;
	uint64_t month;
	uint64_t day;
	uint64_t hour;
	uint64_t minute;
	uint64_t second;
	uint64_t reserved[2];
} PANEL_SERIAL_INFO;

enum oplus_display_power_status {
	OPLUS_DISPLAY_POWER_OFF = 0,
	OPLUS_DISPLAY_POWER_DOZE,
	OPLUS_DISPLAY_POWER_ON,
	OPLUS_DISPLAY_POWER_DOZE_SUSPEND,
	OPLUS_DISPLAY_POWER_ON_UNKNOW,
};

typedef struct oplus_display_notifier_event {
	enum oplus_display_power_status status;
	void *data;
} OPLUS_DISPLAY_NOTIFIER_EVENT;

int oplus_display_register_client(struct notifier_block *nb);
int oplus_display_unregister_client(struct notifier_block *nb);
void oplus_display_notifier_early_status(enum oplus_display_power_status
		power_status);
void oplus_display_notifier_status(enum oplus_display_power_status power_status);
bool oplus_is_correct_display(enum oplus_display_support_list lcd_name);
bool oplus_is_silence_reboot(void);
bool oplus_is_factory_boot(void);
enum oplus_display_power_status __oplus_get_power_status(void);
void __oplus_set_power_status(enum oplus_display_power_status power_status);
int oplus_display_get_resolution(unsigned int *xres, unsigned int *yres);

/* add for dual panel */
void oplus_display_set_display(void *dsi_display);
void oplus_display_set_current_display(void *dsi_display);
void oplus_display_update_current_display(void);
struct dsi_display *oplus_display_get_current_display(void);
int oplus_display_set_power(struct drm_connector *connector, int power_mode,
		void *disp);

/**
 * oplus_panel_event_data_notifier_trigger() - oplus panel event notification with data
 * @panel:         Display panel
 * @notif_type:    Type of notifier
 * @data:          Data to be notified
 * @early_trigger: Whether support early trigger
 * Return: Zero on Success
 */
int oplus_panel_event_data_notifier_trigger(struct dsi_panel *panel,
		enum panel_event_notification_type notif_type,
		u32 data,
		bool early_trigger);

/**
 * oplus_event_data_notifier_trigger() - oplus event notification with data
 * @notif_type:    Type of notifier
 * @data:          Data to be notified
 * @early_trigger: Whether support early trigger
 * Return: Zero on Success
 */
int oplus_event_data_notifier_trigger(
		enum panel_event_notification_type notif_type,
		u32 data,
		bool early_trigger);

/**
 * oplus_panel_backlight_notifier() - oplus panel backlight notifier
 * @panel:  Display panel
 * @bl_lvl: Backlight level
 * Return: Zero on Success
 */
int oplus_panel_backlight_notifier(struct dsi_panel *panel, u32 bl_lvl);

#endif /* _OPLUS_DISPLAY_UTILS_H_ */
