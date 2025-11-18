/***************************************************************
** Copyright (C), 2024, OPLUS Mobile Comm Corp., Ltd
**
** File : oplus_display_interface.c
** Description : oplus display interface
** Version : 1.0
** Date : 2024/05/09
** Author : Display
******************************************************************/
#include <drm/drm_print.h>
#include <drm/drm_connector.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include "oplus_display_bl.h"
#include "oplus_display_interface.h"
#include "oplus_display_device_ioctl.h"
#include "oplus_display_sysfs_attrs.h"
#include "oplus_display_pwm.h"
#include "sde_color_processing.h"
#include "sde_encoder_phys.h"
#include "sde_trace.h"
#include "oplus_debug.h"
#include "oplus_display_ext.h"
#include "oplus_display_effect.h"
#include "oplus_display_esd.h"
#include "oplus_display_parse.h"
#include "oplus_display_panel_cmd.h"
#include "oplus_display_power.h"
#include "oplus_display_device.h"

#define OPLUS_BACKLIGHT_WINDOW_SIZE 5

extern bool is_lhbm_panel;
extern int lcd_closebl_flag;
extern const char *cmd_set_prop_map[];
extern const char *cmd_set_state_map[];
extern bool oplus_enhance_mipi_strength;
extern int dc_apollo_enable;
extern struct dc_apollo_pcc_sync dc_apollo;
extern int oplus_display_private_api_init(void);
extern void oplus_display_private_api_exit(void);
extern struct panel_id panel_id;
extern bool oplus_ofp_get_aod_state(void);

unsigned int oplus_bl_print_window = OPLUS_BACKLIGHT_WINDOW_SIZE;
extern char oplus_global_hbm_flags;
extern int dcc_flags;

static DEFINE_SPINLOCK(g_bk_lock);

struct oplus_display_ops oplus_display_ops = {};

void oplus_display_set_backlight_pre(struct dsi_display *display, int *bl_lvl, int brightness)
{
	*bl_lvl = oplus_panel_mult_frac(brightness);

	return;
}

void oplus_display_set_backlight_post(struct sde_connector *c_conn,
		struct dsi_display *display, struct drm_event *event, int brightness, int bl_lvl)
{
	int rc = 0;

	if (c_conn->ops.set_backlight) {
		/* skip notifying user space if bl is 0 */
		if (brightness != 0) {
			event->type = DRM_EVENT_SYS_BACKLIGHT;
			event->length = sizeof(u32);
			msm_mode_object_event_notify(&c_conn->base.base,
				c_conn->base.dev, event, (u8 *)&brightness);
		}

		if (display->panel->oplus_panel.is_apollo_support && backlight_smooth_enable) {
			if ((is_spread_backlight(display, bl_lvl)) && !dc_apollo_sync_hbmon(display)) {
				if (display->panel->oplus_panel.dc_apollo_sync_enable) {
					if ((display->panel->bl_config.bl_level >= display->panel->oplus_panel.sync_brightness_level
						&& display->panel->bl_config.bl_level < display->panel->oplus_panel.dc_apollo_sync_brightness_level)
						|| display->panel->bl_config.bl_level == 4) {
						if (bl_lvl == display->panel->oplus_panel.dc_apollo_sync_brightness_level
							&& dc_apollo_enable
							&& dc_apollo.pcc_last == display->panel->oplus_panel.dc_apollo_sync_brightness_level_pcc) {
							rc = wait_event_timeout(dc_apollo.bk_wait, dc_apollo.dc_pcc_updated, msecs_to_jiffies(17));
							if (!rc) {
								pr_err("dc wait timeout\n");
							}
							else {
								oplus_backlight_wait_vsync(c_conn->encoder);
							}
							dc_apollo.dc_pcc_updated = 0;
						}
					}
					else if (display->panel->bl_config.bl_level < display->panel->oplus_panel.sync_brightness_level
							&& display->panel->bl_config.bl_level > 4) {
						if (bl_lvl == display->panel->oplus_panel.dc_apollo_sync_brightness_level
							&& dc_apollo_enable
							&& dc_apollo.pcc_last >= display->panel->oplus_panel.dc_apollo_sync_brightness_level_pcc_min) {
							rc = wait_event_timeout(dc_apollo.bk_wait, dc_apollo.dc_pcc_updated, msecs_to_jiffies(17));
							if (!rc) {
								pr_err("dc wait timeout\n");
							}
							else {
								oplus_backlight_wait_vsync(c_conn->encoder);
							}
							dc_apollo.dc_pcc_updated = 0;
						}
					}
				}
				spin_lock(&g_bk_lock);
				update_pending_backlight(display, bl_lvl);
				spin_unlock(&g_bk_lock);
			} else {
				spin_lock(&g_bk_lock);
				update_pending_backlight(display, bl_lvl);
				spin_unlock(&g_bk_lock);
				rc = c_conn->ops.set_backlight(&c_conn->base,
				c_conn->display, bl_lvl);
				c_conn->unset_bl_level = 0;
			}
		} else {
			rc = c_conn->ops.set_backlight(&c_conn->base,
			c_conn->display, bl_lvl);
			c_conn->unset_bl_level = 0;
		}
	}

	return;
}

void oplus_panel_set_backlight_pre(struct dsi_display *display, int *bl_lvl)
{
	struct dsi_panel *panel = NULL;

	panel = display->panel;

	if ((*bl_lvl == 0) || (panel->bl_config.bl_level == 0))
		oplus_bl_print_window = OPLUS_BACKLIGHT_WINDOW_SIZE;

	oplus_panel_post_on_backlight(display, panel, *bl_lvl);

	*bl_lvl = oplus_panel_silence_backlight(panel, *bl_lvl);

	oplus_printf_backlight_log(display, *bl_lvl);

	return;
}

void oplus_panel_set_backlight_post(struct dsi_panel *panel, u64 bl_lvl)
{
	oplus_panel_backlight_notifier(panel, (u32)bl_lvl);

	if (oplus_bl_print_window > 0)
		oplus_bl_print_window--;

	return;
}

void oplus_bridge_pre_enable(struct dsi_display *display, struct dsi_display_mode *mode)
{
	oplus_panel_switch_vid_mode(display, mode);

	return;
}

void oplus_bridge_post_enable(struct dsi_display *display, struct dsi_display_mode *mode)
{
	oplus_panel_switch_vid_mode_post(display, mode);

	return;
}

void oplus_display_enable_pre(struct dsi_display *display)
{
	oplus_display_update_current_display();
	__oplus_set_power_status(OPLUS_DISPLAY_POWER_ON);
	display->panel->power_mode = SDE_MODE_DPMS_ON;
	__oplus_read_apl_thread_ctl(true);

	if (display->panel->panel_mode == DSI_OP_VIDEO_MODE) {
		OPLUS_DSI_INFO("panel video mode skip first read serial_number\n");
		return;
	}

	if (display->oplus_display.panel_sn != 0) {
		OPLUS_DSI_INFO("panel serial_number have read in UEFI, serial_number = [%016lX]\n",
					display->oplus_display.panel_sn);
	} else {
		oplus_display_read_serial_number(display, &display->oplus_display.panel_sn);
		OPLUS_DSI_INFO("panel serial_number don't read in UEFI, read panel serial_number = [%016lX]\n",
					display->oplus_display.panel_sn);
	}

	return;
}

void oplus_display_enable_mid(struct dsi_display *display)
{
	oplus_display_update_current_display();

	return;
}

void oplus_display_enable_post(struct dsi_display *display)
{
	if (display->panel->oplus_panel.wait_te_config & BIT(0)) {
		oplus_display_wait_for_event(display, MSM_ENC_VBLANK);
		if (display->panel->cur_mode->timing.refresh_rate == 60)
			oplus_need_to_sync_te(display->panel);
	}

	return;
}

int oplus_panel_enable_post(struct dsi_panel *panel)
{
	int rc = 0;

	if (panel->oplus_panel.ffc_enabled) {
		oplus_panel_set_ffc_mode_unlock(panel);
	}

	oplus_panel_pwm_panel_on_handle(panel);

	rc = dsi_panel_seed_mode(panel, __oplus_get_seed_mode());
	if (rc) {
		OPLUS_DSI_ERR("Failed to set seed mode: %d\n", __oplus_get_seed_mode());
		return rc;
	}
	if (dcc_flags == 1) {
		OPLUS_DSI_ERR("DCCompensate send dc command\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_DC_ON, false);
		if (rc)
			OPLUS_DSI_ERR("[%s] failed to send DSI_CMD_SET_DC_ON cmds rc=%d\n",
				panel->name, rc);
	}

	panel->oplus_panel.need_power_on_backlight = true;
	__oplus_set_power_status(OPLUS_DISPLAY_POWER_ON);
	panel->power_mode = SDE_MODE_DPMS_ON;

	return rc;
}

void oplus_panel_switch_pre(struct dsi_panel *panel)
{
	panel->oplus_panel.ts_timestamp = ktime_get();

	return;
}

void oplus_panel_switch_post(struct dsi_panel *panel)
{
	/* pwm switch due to timming switch */
	oplus_panel_pwm_switch_timing_switch(panel);

	return;
}

void oplus_panel_enable_init(struct dsi_panel *panel)
{
	/* initialize panel status */
	oplus_panel_init(panel);
	/* Force update of demurra2 offset from UEFI stage to Kernel stage or panel power on*/
	oplus_panel_need_to_set_demura2_offset(panel);

	return;
}

void oplus_display_disable_post(struct dsi_display *display)
{
	oplus_display_update_current_display();

	return;
}

void oplus_panel_disable_post(struct dsi_panel *panel)
{
	__oplus_set_power_status(OPLUS_DISPLAY_POWER_OFF);
	oplus_global_hbm_flags = 0;

	return;
}

void oplus_encoder_kickoff(struct drm_encoder *drm_enc, struct sde_encoder_virt *sde_enc)
{
	/* Add for backlight smooths */
	if (sde_enc->cur_master && (is_support_apollo_bk(sde_enc->cur_master->connector) == true)
        && backlight_smooth_enable && !dc_apollo_sync_hbmon(get_main_display())) {
		if (sde_enc->num_phys_encs > 0) {
			oplus_sync_panel_brightness(OPLUS_POST_KICKOFF_METHOD, drm_enc);
		}
	} else {
		oplus_sync_panel_brightness_v2(drm_enc);
	}
	oplus_set_osc_status(drm_enc);

	return;
}

void oplus_encoder_kickoff_post(struct drm_encoder *drm_enc, struct sde_encoder_virt *sde_enc)
{
	oplus_sync_panel_brightness_video(drm_enc);

	return;
}

int oplus_display_read_status(struct dsi_panel *panel)
{
	int rc = 0;

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_ESD_SWITCH_PAGE, false);
	if (rc) {
		DSI_ERR("[%s] failed to send DSI_CMD_ESD_SWITCH_PAGE, rc=%d\n", panel->name, rc);
		return rc;
	}

	return rc;
}

bool oplus_display_check_status_pre(struct dsi_panel *panel)
{
	if (atomic_read(&panel->oplus_panel.esd_pending)) {
		if (panel->panel_mode == DSI_OP_VIDEO_MODE)
			atomic_set(&panel->oplus_panel.esd_pending, 0);
		return true;
	}

	if (panel->power_mode != SDE_MODE_DPMS_ON) {
		OPLUS_DSI_WARN("Skip the check because panel power mode isn't power on!\n");
		return true;
	}

	if ((!strcmp(panel->name, "AC292 P 3 A0025 vid mode panel") || (!strcmp(panel->name, "AC292 P 7 A0025 vid mode panel"))
	  ||(!strcmp(panel->name, "AC240 P B A0012 dsc cmd mode panel")) || (!strcmp(panel->name, "AC240 P 3 A0004 dsc cmd mode panel")))
	    && oplus_ofp_get_aod_state()) {
		OPLUS_DSI_INFO("Skip the check because aod mode\n");
		return true;
	}

	return false;
}

int oplus_display_check_status_post(struct dsi_display *display)
{
	int rc = 0;

	rc = oplus_display_status_check_error_flag(display);

	return rc;
}

int oplus_display_parse_cmdline_topology(struct dsi_display *display,
			char *boot_str, unsigned int display_type)
{
	char *str = NULL;
	unsigned int panel_id = NO_OVERRIDE;
	unsigned long panel_sn = NO_OVERRIDE;

	str = strnstr(boot_str, ":PanelID-0x", strlen(boot_str));

	if (str) {
		if (sscanf(str, ":PanelID-0x%08X", &panel_id) != 1) {
			OPLUS_DSI_ERR("invalid PanelID override: %s\n",
					boot_str);
			return -1;
		}
		display->oplus_display.panel_flag = (panel_id >> 24) & 0xFF;
		display->oplus_display.panel_id1 = (panel_id >> 16) & 0xFF;
		display->oplus_display.panel_id2 = (panel_id >> 8) & 0xFF;
		display->oplus_display.panel_id3 = panel_id & 0xFF;
	}

	OPLUS_DSI_INFO("Parse cmdline display%d PanelID-0x%08X, Flag=0x%02X, ID1=0x%02X, ID2=0x%02X, ID3=0x%02X\n",
			display_type,
			panel_id,
			display->oplus_display.panel_flag,
			display->oplus_display.panel_id1,
			display->oplus_display.panel_id2,
			display->oplus_display.panel_id3);

	str = strnstr(boot_str, ":PanelSN-0x", strlen(boot_str));

	if (str) {
		if (sscanf(str, ":PanelSN-0x%016lX", &panel_sn) != 1) {
			OPLUS_DSI_ERR("invalid PanelSN override: %s\n",
					boot_str);
			return -1;
		}
		display->oplus_display.panel_sn = panel_sn;
	}

	OPLUS_DSI_INFO("Parse cmdline display%d PanelSN-0x%016lX\n",
			display_type,
			panel_sn);

	return 0;
}

void oplus_display_res_init(struct dsi_display *display)
{
	oplus_panel_parse_features_config(display->panel);
}

void oplus_display_bind_pre(struct dsi_display *display)
{
	if(0 != oplus_display_set_vendor(display)) {
		pr_err("maybe send a null point to oplus display manager\n");
	}

	/* Add for SUA feature request */
	if(oplus_is_silence_reboot()) {
		lcd_closebl_flag = 1;
	}
}

void oplus_display_bind_post(struct dsi_display *display)
{
	if (!strcmp(display->display_type, "primary")) {
		oplus_display_private_api_init();
	}
}

void oplus_display_unbind(struct dsi_display *display)
{
	oplus_display_private_api_exit();
}

void oplus_display_dev_probe(struct dsi_display *display)
{
	oplus_display_set_display(display);
}

void oplus_display_validate_mode_change_pre(struct dsi_display *display)
{
	if (display->panel->oplus_panel.ffc_enabled &&
			display->panel->power_mode == SDE_MODE_DPMS_ON &&
			display->panel->oplus_panel.ffc_delay_frames) {
		oplus_panel_set_ffc_kickoff_lock(display->panel);
	}
}

void oplus_display_validate_mode_change_post(struct dsi_display *display,
			struct dsi_display_mode *cur_mode,
			struct dsi_display_mode *adj_mode)
{
	if (display->panel->oplus_panel.ffc_enabled) {
		oplus_display_update_clk_ffc(display, cur_mode, adj_mode);
	}
}

void oplus_display_register(void)
{
	if(oplus_display_panel_init())
		pr_err("fail to init oplus_display_panel_init\n");
}

void oplus_panel_tx_cmd_set_pre(struct dsi_panel *panel,
				enum dsi_cmd_set_type *type)
{
	oplus_panel_cmd_switch(panel, type);
	oplus_panel_cmdq_pack_handle(panel, *type, true);
	oplus_panel_cmd_print(panel, *type);

	return;
}

void oplus_panel_tx_cmd_set_mid(struct dsi_panel *panel,
				enum dsi_cmd_set_type type)
{
	oplus_panel_cmdq_pack_handle(panel, type, false);

	return;
}

void oplus_panel_tx_cmd_set_post(struct dsi_panel *panel,
				enum dsi_cmd_set_type type, int rc)
{
	if (rc) {
		DSI_ERR("[LCD][%s] failed to send %s, rc=%d\n",
				panel->oplus_panel.vendor_name, cmd_set_prop_map[type], rc);
		WARN_ON(rc);
	}

	return;
}

int oplus_panel_parse_bl_config_post(struct dsi_panel *panel)
{
	int rc = 0;

	rc = oplus_panel_parse_bl_cfg(panel);
	if (rc) {
		DSI_ERR("[%s] failed to parse oplus backlight config, rc=%d\n",
			panel->name, rc);
	}

	return rc;
}

int oplus_panel_parse_esd_reg_read_configs_post(struct dsi_panel *panel)
{
	int rc = 0;

	rc = oplus_panel_parse_esd_reg_read_configs(panel);
	if (rc) {
		DSI_ERR("failed to parse oplus esd reg read config, rc=%d\n", rc);
	}

	return rc;
}

void oplus_panel_parse_esd_config_post(struct dsi_panel *panel)
{
	int ret = 0;
	struct dsi_parser_utils *utils = &panel->utils;

	panel->esd_config.oplus_esd_cfg.esd_error_flag_gpio = utils->get_named_gpio(utils->data,
			"qcom,error-flag-gpio", 0);
	panel->esd_config.oplus_esd_cfg.esd_error_flag_gpio_slave = utils->get_named_gpio(utils->data,
			"qcom,error-flag-gpio-slave", 0);
	ret = utils->read_u32(utils->data, "qcom,error-flag-gpio-expect-value",
			&panel->esd_config.oplus_esd_cfg.esd_error_flag_expect_value);
	if (ret) {
		OPLUS_DSI_INFO("failed to get qcom,error-flag-gpio-expect-value\n");
		panel->esd_config.oplus_esd_cfg.esd_error_flag_expect_value = 1;
	}
	ret = utils->read_u32(utils->data, "qcom,error-flag-gpio-expect-value-slave",
			&panel->esd_config.oplus_esd_cfg.esd_error_flag_expect_value_slave);
	if (ret) {
		OPLUS_DSI_INFO("failed to get qcom,error-flag-gpio-expect-value-slave\n");
		panel->esd_config.oplus_esd_cfg.esd_error_flag_expect_value_slave = 1;
	}
	DSI_INFO("%s:get esd_error_flag_gpio[%d], esd_error_flag_gpio_slave[%d], esd_error_flag_expect_value[%d], esd_error_flag_expect_value_slave[%d]\n",
			__func__, panel->esd_config.oplus_esd_cfg.esd_error_flag_gpio, panel->esd_config.oplus_esd_cfg.esd_error_flag_gpio_slave,
			panel->esd_config.oplus_esd_cfg.esd_error_flag_expect_value, panel->esd_config.oplus_esd_cfg.esd_error_flag_expect_value_slave);
	return;
}

void oplus_panel_get_pre(struct dsi_panel *panel)
{
	return;
}

int oplus_panel_get_post(struct dsi_panel *panel)
{
	int rc = 0;

	rc = oplus_panel_parse_config(panel);
	if (rc)
		DSI_ERR("failed to parse panel config, rc=%d\n", rc);

	return rc;
}

int oplus_panel_get_mode(struct dsi_display_mode *mode, struct dsi_parser_utils *utils)
{
	int rc = 0;

	rc = oplus_panel_parse_vsync_config(mode, utils);
	if (rc) {
		DSI_ERR("failed to parse vsync params, rc=%d\n", rc);
	}

	return rc;
}

void oplus_dsi_phy_hw_dphy_enable(u32 *glbl_str_swi_cal_sel_ctrl,
				u32 *glbl_hstx_str_ctrl_0)
{
	if (oplus_enhance_mipi_strength) {
		*glbl_str_swi_cal_sel_ctrl = 0x01;
		*glbl_hstx_str_ctrl_0 = 0xFF;
	} else {
		*glbl_str_swi_cal_sel_ctrl = 0x00;
		*glbl_hstx_str_ctrl_0 = 0x88;
	}

	return;
}

void oplus_backlight_setup_pre(struct backlight_properties *props, struct dsi_display *display)
{
	props->brightness = display->panel->oplus_panel.bl_cfg.brightness_default_level;

	return;
}

void oplus_backlight_setup_post(struct dsi_display *display)
{
	if (display->panel->oplus_panel.dc_apollo_sync_enable) {
		init_waitqueue_head(&dc_apollo.bk_wait);
		mutex_init(&dc_apollo.lock);
	}

	return;
}

void oplus_connector_update_dirty_properties(struct sde_connector *c_conn, int idx)
{
	switch (idx) {
	case CONNECTOR_PROP_SYNC_BACKLIGHT_LEVEL:
		if (c_conn) {
			c_conn->oplus_conn.bl_need_sync = true;
		}
		break;
	case CONNECTOR_PROP_SET_OSC_STATUS:
		if (c_conn) {
			c_conn->oplus_conn.osc_need_update = true;
		}
		break;
	default:
			/* nothing to do for most properties */
		break;
	}

	return;
}

void oplus_encoder_off_work(struct sde_encoder_virt *sde_enc)
{
	struct sde_connector *c_conn = NULL;

	if (sde_enc->cur_master && sde_enc->cur_master->connector) {
		c_conn = to_sde_connector(sde_enc->cur_master->connector);
		if (c_conn) {
			oplus_panel_cmdq_pack_status_reset(c_conn);
		}
	}

	return;
}

void oplus_encoder_trigger_start(void)
{
	/* sending commands asynchronously, it is necessary to ensure that
		   the next frame mipi sends the image */
	oplus_panel_send_asynchronous_cmd();

	return;
}

void oplus_encoder_phys_cmd_te_rd_ptr_irq_pre(struct sde_encoder_phys *phys_enc,
			struct sde_encoder_phys_cmd_te_timestamp *te_timestamp)
{
	struct sde_connector *conn;

	conn = to_sde_connector(phys_enc->connector);
	if (conn && te_timestamp) {
		oplus_save_te_timestamp(conn, te_timestamp->timestamp);
	}

	return;
}

void oplus_encoder_phys_cmd_te_rd_ptr_irq_post(struct sde_encoder_phys *phys_enc)
{
	struct sde_connector *conn;

	conn = to_sde_connector(phys_enc->connector);
	if (conn) {
		oplus_panel_cmdq_pack_status_reset(conn);
	}

	return;
}

void oplus_setup_dspp_pccv4_pre(struct drm_msm_pcc *pcc_cfg)
{
	if (pcc_cfg)
		dc_apollo.pcc_current = pcc_cfg->r.r;

	return;
}

void oplus_setup_dspp_pccv4_post(struct drm_msm_pcc *pcc_cfg)
{
	static struct drm_msm_pcc *pcc_cfg_last;
	struct dsi_display *display = get_main_display();

	if (display != NULL && display->panel != NULL) {
		if (display->panel->oplus_panel.dc_apollo_sync_enable) {
			mutex_lock(&dc_apollo.lock);
			if (pcc_cfg_last && pcc_cfg) {
				if (dc_apollo.pcc_last != dc_apollo.pcc_current) {
					dc_apollo.pcc_last = dc_apollo.pcc_current;
					dc_apollo.dc_pcc_updated = 1;
				}
			}
			pcc_cfg_last = pcc_cfg;
			mutex_unlock(&dc_apollo.lock);
		}
	}

	return;
}

void oplus_kms_drm_check_dpms_pre(int old_fps, int new_fps)
{
	oplus_check_refresh_rate(old_fps, new_fps);

	return;
}

void oplus_kms_drm_check_dpms_post(struct sde_connector *c_conn, bool is_pre_commit)
{
	struct dsi_display *display;
	struct dsi_panel *panel;

	display = (struct dsi_display *)c_conn->display;
	if (!display) {
		OPLUS_DSI_ERR("Invalid display\n");
		return;
	}

	panel = display->panel;
	if (!panel) {
		OPLUS_DSI_ERR("Invalid panel\n");
		return;
	}

	if (is_pre_commit) {
		panel->oplus_panel.need_trigger_event = false;
	} else {
		panel->oplus_panel.need_trigger_event = true;
	}

	return;
}

void oplus_dsi_message_tx_pre(struct dsi_ctrl *dsi_ctrl, struct dsi_cmd_desc *cmd_desc)
{
	oplus_ctrl_print_cmd_desc(dsi_ctrl, cmd_desc);

	return;
}

void oplus_dsi_message_tx_post(struct dsi_ctrl *dsi_ctrl, struct dsi_cmd_desc *cmd_desc)
{
	return;
}

int oplus_display_validate_status(struct dsi_display *display)
{
	int rc = 0;

	rc = oplus_panel_validate_reg_read(display->panel);

	return rc;
}

int oplus_panel_parse_cmd_sets_sub(struct dsi_panel_cmd_set *cmd, const char *state)
{
	if (!state || !strcmp(state, "dsi_lp_mode")) {
		cmd->state = DSI_CMD_SET_STATE_LP;
	} else if (!strcmp(state, "dsi_hs_mode")) {
		cmd->state = DSI_CMD_SET_STATE_HS;
	} else if (!strcmp(state, "dsi_lp_pack_mode")) {
		cmd->state = DSI_CMD_SET_STATE_LP;
		cmd->oplus_cmd_set.pack = true;
	} else if (!strcmp(state, "dsi_hs_pack_mode")) {
		cmd->state = DSI_CMD_SET_STATE_HS;
		cmd->oplus_cmd_set.pack = true;
	} else {
		return -1;
	}

	return 0;
}

void oplus_panel_set_lp1(struct dsi_panel *panel)
{
	__oplus_set_power_status(OPLUS_DISPLAY_POWER_DOZE);

	return;
}

void oplus_panel_set_lp2(struct dsi_panel *panel)
{
	__oplus_set_power_status(OPLUS_DISPLAY_POWER_DOZE_SUSPEND);
	return;
}

void oplus_panel_set_nolp(struct dsi_panel *panel)
{
	oplus_panel_set_aod_off_te_timestamp(panel);
	__oplus_set_power_status(OPLUS_DISPLAY_POWER_ON);

	return;
}

void oplus_display_ops_init(struct oplus_display_ops *oplus_display_ops)
{
	DRM_INFO("oplus display ops init\n");

	/* backlight update */
	oplus_display_ops->panel_parse_bl_config_post = oplus_panel_parse_bl_config_post;
	oplus_display_ops->display_set_backlight_pre = oplus_display_set_backlight_pre;
	oplus_display_ops->display_set_backlight_post = oplus_display_set_backlight_post;
	oplus_display_ops->panel_set_backlight_pre = oplus_panel_set_backlight_pre;
	oplus_display_ops->panel_set_backlight_post = oplus_panel_set_backlight_post;
	oplus_display_ops->panel_update_backlight = oplus_panel_update_backlight;
	oplus_display_ops->backlight_setup_pre = oplus_backlight_setup_pre;
	oplus_display_ops->backlight_setup_post = oplus_backlight_setup_post;

	/* commit */
	oplus_display_ops->encoder_kickoff = oplus_encoder_kickoff;
	oplus_display_ops->encoder_kickoff_post = oplus_encoder_kickoff_post;
	oplus_display_ops->display_validate_mode_change_pre = oplus_display_validate_mode_change_pre;
	oplus_display_ops->display_validate_mode_change_post = oplus_display_validate_mode_change_post;
	oplus_display_ops->dsi_phy_hw_dphy_enable = oplus_dsi_phy_hw_dphy_enable;
	oplus_display_ops->connector_update_dirty_properties = oplus_connector_update_dirty_properties;
	oplus_display_ops->encoder_off_work = oplus_encoder_off_work;
	oplus_display_ops->encoder_trigger_start = oplus_encoder_trigger_start;
	oplus_display_ops->encoder_phys_cmd_te_rd_ptr_irq_pre = oplus_encoder_phys_cmd_te_rd_ptr_irq_pre;
	oplus_display_ops->encoder_phys_cmd_te_rd_ptr_irq_post = oplus_encoder_phys_cmd_te_rd_ptr_irq_post;
	oplus_display_ops->setup_dspp_pccv4_pre = oplus_setup_dspp_pccv4_pre;
	oplus_display_ops->setup_dspp_pccv4_post = oplus_setup_dspp_pccv4_post;
	oplus_display_ops->kms_drm_check_dpms_pre = oplus_kms_drm_check_dpms_pre;
	oplus_display_ops->kms_drm_check_dpms_post = oplus_kms_drm_check_dpms_post;

	/* power on */
	oplus_display_ops->bridge_pre_enable = oplus_bridge_pre_enable;
	oplus_display_ops->bridge_post_enable = oplus_bridge_post_enable;
	oplus_display_ops->display_enable_pre = oplus_display_enable_pre;
	oplus_display_ops->display_enable_mid = oplus_display_enable_mid;
	oplus_display_ops->display_enable_post = oplus_display_enable_post;
	oplus_display_ops->panel_enable_post = oplus_panel_enable_post;
	oplus_display_ops->panel_init = oplus_panel_enable_init;
	oplus_display_ops->panel_set_pinctrl_state = oplus_panel_set_pinctrl_state;
	oplus_display_ops->panel_prepare = oplus_panel_prepare;
	oplus_display_ops->panel_power_on = oplus_panel_power_on;
	oplus_display_ops->panel_pre_prepare = oplus_panel_pre_prepare;

	/* power off */
	oplus_display_ops->display_disable_post = oplus_display_disable_post;
	oplus_display_ops->panel_disable_post = oplus_panel_disable_post;
	oplus_display_ops->panel_power_off = oplus_panel_power_off;

	/* timing switch */
	oplus_display_ops->panel_switch_pre = oplus_panel_switch_pre;
	oplus_display_ops->panel_switch_post = oplus_panel_switch_post;

	/* esd */
	oplus_display_ops->panel_parse_esd_reg_read_configs_post = oplus_panel_parse_esd_reg_read_configs_post;
	oplus_display_ops->panel_parse_esd_config_post = oplus_panel_parse_esd_config_post;
	oplus_display_ops->display_read_status = oplus_display_read_status;
	oplus_display_ops->display_check_status_pre = oplus_display_check_status_pre;
	oplus_display_ops->display_check_status_post = oplus_display_check_status_post;
	oplus_display_ops->display_validate_status = oplus_display_validate_status;

	/* starting up/down */
	oplus_display_ops->display_parse_cmdline_topology = oplus_display_parse_cmdline_topology;
	oplus_display_ops->display_res_init = oplus_display_res_init;
	oplus_display_ops->display_dev_probe = oplus_display_dev_probe;
	oplus_display_ops->display_register = oplus_display_register;
	oplus_display_ops->panel_get_pre = oplus_panel_get_pre;
	oplus_display_ops->panel_get_post = oplus_panel_get_post;
	oplus_display_ops->panel_get_mode = oplus_panel_get_mode;
	oplus_display_ops->display_bind_pre = oplus_display_bind_pre;
	oplus_display_ops->display_bind_post = oplus_display_bind_post;
	oplus_display_ops->panel_pinctrl_init = oplus_panel_pinctrl_init;
	oplus_display_ops->panel_parse_gpios = oplus_panel_parse_gpios;
	oplus_display_ops->panel_gpio_request = oplus_panel_gpio_request;
	oplus_display_ops->panel_gpio_release = oplus_panel_gpio_release;
	oplus_display_ops->display_unbind = oplus_display_unbind;

	/* tx cmd */
	oplus_display_ops->panel_tx_cmd_set_pre = oplus_panel_tx_cmd_set_pre;
	oplus_display_ops->panel_tx_cmd_set_mid = oplus_panel_tx_cmd_set_mid;
	oplus_display_ops->panel_tx_cmd_set_post = oplus_panel_tx_cmd_set_post;
	oplus_display_ops->dsi_message_tx_pre = oplus_dsi_message_tx_pre;
	oplus_display_ops->dsi_message_tx_post = oplus_dsi_message_tx_post;
	oplus_display_ops->panel_parse_cmd_sets_sub = oplus_panel_parse_cmd_sets_sub;

	/* aod */
	oplus_display_ops->panel_set_lp1 = oplus_panel_set_lp1;
	oplus_display_ops->panel_set_lp2 = oplus_panel_set_lp2;
	oplus_display_ops->panel_set_nolp = oplus_panel_set_nolp;
}
