// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/version.h>
#include <drm/drm_probe_helper.h>

#include "msm_drv.h"
#include "sde_connector.h"
#include "hdmi_display.h"
#include "hdmi_audio.h"
#include "hdmi_parser.h"
#include "hdmi_power.h"
#include "hdmi_debug.h"
#include "hdmi_pll.h"
#include "hdmi_phy.h"
//#include "hdmi_cec.h"
#include "sde_hdcp.h"
#include "hdmi_util.h"
#include "sde_edid_parser.h"

#define hdmi_display_state_show(x) { \
	HDMI_ERR("%s: state (0x%x): %s\n", x, hdmi->state, \
		hdmi_display_state_name(hdmi->state)); \
	SDE_EVT32_EXTERNAL(hdmi->state); }

#define hdmi_display_state_warn(x) { \
	HDMI_WARN("%s: state (0x%x): %s\n", x, hdmi->state, \
		hdmi_display_state_name(hdmi->state)); \
	SDE_EVT32_EXTERNAL(hdmi->state); }

#define hdmi_display_state_log(x) { \
	HDMI_DEBUG("%s: state (0x%x): %s\n", x, hdmi->state, \
		hdmi_display_state_name(hdmi->state)); \
	SDE_EVT32_EXTERNAL(hdmi->state); }

#define hdmi_display_state_is(x) (hdmi->state & (x))
#define hdmi_display_state_add(x) { \
	(hdmi->state |= (x)); \
	hdmi_display_state_log("add "#x); }
#define hdmi_display_state_remove(x) { \
	(hdmi->state &= ~(x)); \
	hdmi_display_state_log("remove "#x); }

#define hdmi_read(x, off) \
	readl_relaxed((x)->io_data->io.base + (off))

#define hdmi_write(x, off, value)  \
	writel_relaxed(value, (x)->io_data->io.base + (off))

static inline uint32_t HDMI_HPD_CTRL_TIMEOUT(uint32_t val)
{
	return ((val) << HDMI_HPD_CTRL_TIMEOUT__SHIFT) &
		HDMI_HPD_CTRL_TIMEOUT__MASK;
}

#define HPD_STRING_SIZE 30
#define MAX_RETRIES 5

enum hdmi_display_states {
	HDMI_STATE_DISCONNECTED           = 0,
	HDMI_STATE_CONFIGURED             = BIT(0),
	HDMI_STATE_INITIALIZED            = BIT(1),
	HDMI_STATE_READY                  = BIT(2),
	HDMI_STATE_CONNECTED              = BIT(3),
	HDMI_STATE_CONNECT_NOTIFIED       = BIT(4),
	HDMI_STATE_DISCONNECT_NOTIFIED    = BIT(5),
	HDMI_STATE_ENABLED                = BIT(6),
	HDMI_STATE_SUSPENDED              = BIT(7),
	HDMI_STATE_ABORTED                = BIT(8),
	HDMI_STATE_HDCP_ABORTED           = BIT(9),
	HDMI_STATE_SRC_PWRDN              = BIT(10),
	HDMI_STATE_TUI_ACTIVE             = BIT(11),
};

static struct hdmi_display *g_hdmi_display[MAX_HDMI_ACTIVE_DISPLAY];

static char *hdmi_display_state_name(enum hdmi_display_states state)
{
	static char buf[SZ_1K];
	u32 len = 0;

	memset(buf, 0, SZ_1K);

	if (state & HDMI_STATE_CONFIGURED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"CONFIGURED");

	if (state & HDMI_STATE_INITIALIZED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"INITIALIZED");

	if (state & HDMI_STATE_READY)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"READY");

	if (state & HDMI_STATE_CONNECTED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"CONNECTED");

	if (state & HDMI_STATE_CONNECT_NOTIFIED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"CONNECT_NOTIFIED");

	if (state & HDMI_STATE_DISCONNECT_NOTIFIED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"DISCONNECT_NOTIFIED");

	if (state & HDMI_STATE_ENABLED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"ENABLED");

	if (state & HDMI_STATE_SUSPENDED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"SUSPENDED");

	if (state & HDMI_STATE_ABORTED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"ABORTED");

	if (state & HDMI_STATE_HDCP_ABORTED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"HDCP_ABORTED");

	if (state & HDMI_STATE_SRC_PWRDN)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"SRC_PWRDN");

	if (state & HDMI_STATE_TUI_ACTIVE)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"TUI_ACTIVE");

	if (!strlen(buf))
		return "DISCONNECTED";

	return buf;
}


struct hdmi_hdcp_dev {
	void *fd;
	struct sde_hdcp_ops *ops;
	enum sde_hdcp_version ver;
};

struct hdmi_hdcp {
	void *data;
	struct sde_hdcp_ops *ops;

	u32 source_cap;

	struct hdmi_hdcp_dev dev[HDCP_VERSION_MAX];
};

struct hdmi_display_private {
	char *name;
	int irq;

	enum hdmi_display_states state;

	struct platform_device *pdev;

	struct hdmi_parser  *parser;
	struct hdmi_power   *power;
	struct hdmi_panel   *panel;
	struct hdmi_debug   *debug;
	struct hdmi_pll     *pll;
	struct hdmi_phy     *phy;
	struct hdmi_ddc     *ddc;
	struct hdmi_audio   *audio;
	//struct hdmi_cec     *cec;
	struct hdmi_io_data *io_data;

	bool hdmi_mode;

	struct hdmi_hdcp hdcp;

	struct hdmi_display hdmi_display;
	struct msm_drm_private *priv;

	struct workqueue_struct *wq;
	struct work_struct hpd_work;
	struct mutex session_lock;	// TODO same as display_lock in 4.4
	struct mutex accounting_lock;

	u32 tot_dsc_blks_in_use;
	u32 tot_lm_blks_in_use;

	bool cont_splash_enabled;

	bool process_hpd_connect;
	struct dev_pm_qos_request pm_qos_req[NR_CPUS];
	bool pm_qos_requested;

	u32 cell_idx;
	u32 phy_idx;
	/*
	 * spinlock to protect registers shared by different execution
	 * HDMI_CTRL
	 * HDMI_DDC_ARBITRATION
	 * HDMI_HDCP_INT_CTRL
	 * HDMI_HPD_CTRL
	 */
	spinlock_t reg_lock;

};

/* hdmi ctrl helper functions */
static void hdmi_phy_reset(struct hdmi_display_private *hdmi)
{
	unsigned int val;

	val = hdmi_read(hdmi, HDMI_PHY_CTRL);

	if (val & HDMI_PHY_CTRL_SW_RESET_LOW) {
		/* pull low */
		hdmi_write(hdmi, HDMI_PHY_CTRL,
				val & ~HDMI_PHY_CTRL_SW_RESET);
	} else {
		/* pull high */
		hdmi_write(hdmi, HDMI_PHY_CTRL,
				val | HDMI_PHY_CTRL_SW_RESET);
	}

	if (val & HDMI_PHY_CTRL_SW_RESET_PLL_LOW) {
		/* pull low */
		hdmi_write(hdmi, HDMI_PHY_CTRL,
				val & ~HDMI_PHY_CTRL_SW_RESET_PLL);
	} else {
		/* pull high */
		hdmi_write(hdmi, HDMI_PHY_CTRL,
				val | HDMI_PHY_CTRL_SW_RESET_PLL);
	}

	msleep(100);

	if (val & HDMI_PHY_CTRL_SW_RESET_LOW) {
		/* pull high */
		hdmi_write(hdmi, HDMI_PHY_CTRL,
				val | HDMI_PHY_CTRL_SW_RESET);
	} else {
		/* pull low */
		hdmi_write(hdmi, HDMI_PHY_CTRL,
				val & ~HDMI_PHY_CTRL_SW_RESET);
	}

	if (val & HDMI_PHY_CTRL_SW_RESET_PLL_LOW) {
		/* pull high */
		hdmi_write(hdmi, HDMI_PHY_CTRL,
				val | HDMI_PHY_CTRL_SW_RESET_PLL);
	} else {
		/* pull low */
		hdmi_write(hdmi, HDMI_PHY_CTRL,
				val & ~HDMI_PHY_CTRL_SW_RESET_PLL);
	}
}

static void hdmi_set_mode(struct hdmi_display_private *hdmi, bool power_on)
{
	uint32_t ctrl = 0;
	unsigned long flags;

	spin_lock_irqsave(&hdmi->reg_lock, flags);
	if (power_on) {
		ctrl |= HDMI_CTRL_ENABLE;
		if (!hdmi->hdmi_mode) {
			ctrl |= HDMI_CTRL_HDMI;
			hdmi_write(hdmi, HDMI_CTRL, ctrl);
			ctrl &= ~HDMI_CTRL_HDMI;
		} else {
			ctrl |= HDMI_CTRL_HDMI;
		}
	} else {
		ctrl = HDMI_CTRL_HDMI;
	}

	hdmi_write(hdmi, HDMI_CTRL, ctrl);
	spin_unlock_irqrestore(&hdmi->reg_lock, flags);
	HDMI_DEBUG("HDMI Core: %s, HDMI_CTRL=0x%08x",
			power_on ? "Enable" : "Disable", ctrl);
}

static int hdmi_hpd_enable(struct hdmi_display_private *hdmi)
{
	unsigned long flags;
	uint32_t hpd_ctrl;

	//TODO: check the seq in power->init
	hdmi->power->init(hdmi->power);

	hdmi_set_mode(hdmi, false);
	hdmi_phy_reset(hdmi);
	hdmi_set_mode(hdmi, true);

	hdmi_write(hdmi, HDMI_USEC_REFTIMER, 0x00010013);


	/* enable HPD events: */
	hdmi_write(hdmi, HDMI_HPD_INT_CTRL,
			HDMI_HPD_INT_CTRL_INT_CONNECT |
			HDMI_HPD_INT_CTRL_INT_EN);

	/* set timeout to 4.1ms (max) for hardware debounce */
	spin_lock_irqsave(&hdmi->reg_lock, flags);
	hpd_ctrl = hdmi_read(hdmi, HDMI_HPD_CTRL);
	hpd_ctrl |= HDMI_HPD_CTRL_TIMEOUT(0x09C4);

	/* Toggle HPD circuit to trigger HPD sense */
	hdmi_write(hdmi, HDMI_HPD_CTRL,
			~HDMI_HPD_CTRL_ENABLE & hpd_ctrl);
	hdmi_write(hdmi, HDMI_HPD_CTRL,
			HDMI_HPD_CTRL_ENABLE | hpd_ctrl);
	spin_unlock_irqrestore(&hdmi->reg_lock, flags);

	hdmi_display_state_add(HDMI_STATE_CONFIGURED);

	return 0;
}

static void hdmi_hpd_disable(struct hdmi_display_private *hdmi)
{

	hdmi_display_state_remove(HDMI_STATE_CONFIGURED);
	/* Disable HPD interrupt */
	hdmi_write(hdmi, HDMI_HPD_INT_CTRL, 0);

	hdmi_set_mode(hdmi, false);

	//TODO: check the seq in power->init
	hdmi->power->deinit(hdmi->power);
}

static void hdmi_display_isr(struct hdmi_display_private *hdmi)
{
	u32 hpd_int_status, hpd_int_ctrl;

	/* Process HPD: */
	hpd_int_status = hdmi_read(hdmi, HDMI_HPD_INT_STATUS);
	hpd_int_ctrl = hdmi_read(hdmi, HDMI_HPD_INT_CTRL);

	if ((hpd_int_ctrl & HDMI_HPD_INT_CTRL_INT_EN) &&
		(hpd_int_status & HDMI_HPD_INT_STATUS_INT)) {
		hdmi->hdmi_display.connected = !!(hpd_int_status &
					HDMI_HPD_INT_STATUS_CABLE_DETECTED);
		/* ack & disable (temporarily) HPD events: */
		hdmi_write(hdmi, HDMI_HPD_INT_CTRL,
			HDMI_HPD_INT_CTRL_INT_ACK);

		HDMI_DEBUG("status=%04x, ctrl=%04x",
			hpd_int_status, hpd_int_ctrl);

		/* detect disconnect if we are connected or vice versa */
		hpd_int_ctrl = HDMI_HPD_INT_CTRL_INT_EN;
		if (!hdmi->hdmi_display.connected)
			hpd_int_ctrl |= HDMI_HPD_INT_CTRL_INT_CONNECT;
		hdmi_write(hdmi, HDMI_HPD_INT_CTRL, hpd_int_ctrl);

		queue_work(hdmi->wq, &hdmi->hpd_work);
	}
}

static int hdmi_init_sub_modules(struct hdmi_display_private *hdmi)
{
	int rc = 0;
	struct hdmi_parser *parser;
	struct drm_connector *connector;
	struct device *dev = &hdmi->pdev->dev;
	struct hdmi_debug_in debug_in = {
		.dev = dev,
	};

	mutex_init(&hdmi->session_lock);
	mutex_init(&hdmi->accounting_lock);

	connector = hdmi->hdmi_display.base_connector;
	dev = &hdmi->pdev->dev;

	hdmi->parser = hdmi_parser_get(hdmi->pdev);
	if (IS_ERR(hdmi->parser)) {
		rc = PTR_ERR(hdmi->parser);
		HDMI_ERR("failed to initialize parser, rc = %d\n", rc);
		hdmi->parser = NULL;
		goto error;
	}

	parser = hdmi->parser;
	rc = parser->parse(parser);
	if (rc) {
		HDMI_ERR("device tree parsing failed\n");
		goto error_parser;
	}

	hdmi->io_data = parser->get_io(parser, "hdmi_ctrl");
	if (!hdmi->io_data) {
		HDMI_ERR("get io for hdmi_ctrl init failed\n");
		rc = -EINVAL;
		goto error_parser;
	}

	hdmi->ddc = hdmi_ddc_get(dev, parser);
	if (IS_ERR(hdmi->ddc)) {
		rc = PTR_ERR(hdmi->ddc);
		HDMI_ERR("ddc get failed, rc = %d\n", rc);
		goto error_ddc;
	}

	hdmi->pll = hdmi_pll_get(hdmi->pdev, parser);
	if (IS_ERR(hdmi->pll)) {
		rc = PTR_ERR(hdmi->pll);
		HDMI_ERR("pll get failed, rc = %d\n", rc);
		goto error_ddc;
	}

	hdmi->power = hdmi_power_get(parser, hdmi->pll);
	if (IS_ERR(hdmi->power)) {
		rc = PTR_ERR(hdmi->power);
		HDMI_ERR("power get failed, rc = %d\n", rc);
		goto error_power;
	}

	rc = hdmi->power->power_client_init(hdmi->power, &hdmi->priv->phandle,
			hdmi->hdmi_display.drm_dev);
	if (rc) {
		HDMI_ERR("Power client create failed\n");
		goto error_power;
	}

	hdmi->panel = hdmi_panel_get(dev, connector, hdmi->parser);
	if (IS_ERR(hdmi->panel)) {
		rc = PTR_ERR(hdmi->panel);
		HDMI_ERR("failed to initialize panel, rc = %d\n", rc);
		hdmi->panel = NULL;
		goto error_panel;
	}

	hdmi->panel->edid_ctrl = sde_edid_init();
	if (!hdmi->panel->edid_ctrl) {
		HDMI_ERR("sde edid init failed\n");
		rc = -ENOMEM;
		goto error_edid;
	}

	hdmi->phy = hdmi_phy_get(hdmi->pdev, hdmi->parser);
	if (IS_ERR(hdmi->phy)) {
		rc = PTR_ERR(hdmi->phy);
		HDMI_ERR("failed to initialize phy, rc = %d\n", rc);
		hdmi->phy = NULL;
		goto error_phy;
	}

	//hdmi->cec = hdmi_cec_get(hdmi->pdev, hdmi->parser);
	//if (IS_ERR(hdmi->cec)) {
	//	rc = PTR_ERR(hdmi->cec);
	//	HDMI_ERR("failed to initialize cec, rc = %d\n", rc);
	//	hdmi->cec = NULL;
	//	goto error_phy;
	//}

	debug_in.panel = hdmi->panel;
	debug_in.connector = connector;
	debug_in.parser = hdmi->parser;
	debug_in.pll = hdmi->pll;
	debug_in.display = &hdmi->hdmi_display;

	hdmi->debug = hdmi_debug_get(&debug_in);
	if (IS_ERR(hdmi->debug)) {
		rc = PTR_ERR(hdmi->debug);
		HDMI_ERR("failed to initialize debug, rc = %d\n", rc);
		hdmi->debug = NULL;
		goto error_debug;
	}

	/* configure hpd */
	hdmi_hpd_enable(hdmi);

	//TODO: only hpd is enabled here. what about others
	/* enable hdmi irq*/
	enable_irq(hdmi->irq);
	hdmi->audio = hdmi_audio_get(hdmi->pdev, hdmi->panel, hdmi->parser);
	if (IS_ERR(hdmi->audio)) {
		rc = PTR_ERR(hdmi->audio);
		HDMI_ERR("failed to initialize audio, rc = %d\n", rc);
		hdmi->audio = NULL;
		goto error_audio;
	}

	hdmi->audio->display = &hdmi->hdmi_display;
	return 0;

error_audio:
	hdmi_audio_put(hdmi->audio);
error_debug:
	hdmi_debug_put(hdmi->debug);
error_phy:
	hdmi_phy_put(hdmi->phy);
error_edid:
	sde_edid_deinit((void **)&hdmi->panel->edid_ctrl);
error_panel:
	hdmi_panel_put(hdmi->panel);
error_power:
	hdmi_power_put(hdmi->power);
error_ddc:
	hdmi_ddc_put(hdmi->ddc);
error_parser:
	hdmi_parser_put(parser);
error:
	return rc;
}

static void hdmi_display_deinit_sub_modules(struct hdmi_display_private *hdmi)
{
	disable_irq(hdmi->irq);
	hdmi_hpd_disable(hdmi);
	hdmi_audio_put(hdmi->audio);
	hdmi_debug_put(hdmi->debug);
	hdmi_phy_put(hdmi->phy);
	sde_edid_deinit((void **)&hdmi->panel->edid_ctrl);
	//TODO:hdmi_cec_put(hdmi->cec);
	hdmi_panel_put(hdmi->panel);
	hdmi_power_put(hdmi->power);
	hdmi_ddc_put(hdmi->ddc);
	hdmi_parser_put(hdmi->parser);
	mutex_destroy(&hdmi->session_lock);
}

int hdmi_display_get_num_of_displays(struct drm_device *dev)
{
	int i, j;

	for (i = 0, j = 0; i < MAX_HDMI_ACTIVE_DISPLAY; i++) {
		if (!g_hdmi_display[i])
			break;

		if (!dev || g_hdmi_display[i]->drm_dev == dev)
			j++;
	}

	return j;
}

static int hdmi_display_post_init(struct hdmi_display *hdmi_display)
{
	int rc = 0;
	struct hdmi_display_private *hdmi;

	if (!hdmi_display) {
		HDMI_ERR("invalid input\n");
		rc = -EINVAL;
		goto end;
	}

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);
	if (IS_ERR_OR_NULL(hdmi)) {
		HDMI_ERR("invalid params\n");
		rc = -EINVAL;
		goto end;
	}

	rc = hdmi_init_sub_modules(hdmi);
	if (rc)
		goto end;

	hdmi_display->post_init = NULL;
end:
	HDMI_DEBUG("%s\n", rc ? "failed" : "success");
	return rc;
}

static int hdmi_read_edid_retry(struct hdmi_display_private *hdmi,
	struct drm_connector *connector)
{
	int edid_read_retry = 0;
	int ret = 0;

	// Free any previous failed attempt's memory if it exists
	if (hdmi->panel->edid_ctrl && !hdmi->panel->edid_ctrl->edid)
		sde_free_edid((void **)&hdmi->panel->edid_ctrl);

	/*
	 * Adding retries and wait helps ensure the EDID block is
	 * read in full.
	 * For monitors without timing issues, the read operation
	 * typically succeeds on the first attempt.
	 */

	do {
		if (hdmi->panel->edid_ctrl && !hdmi->panel->edid_ctrl->edid)
			sde_free_edid((void **)&hdmi->panel->edid_ctrl);

		sde_get_edid(connector, hdmi->ddc->i2c,
			(void **)&hdmi->panel->edid_ctrl);
		if (!hdmi->panel->edid_ctrl ||
			!hdmi->panel->edid_ctrl->edid) {
			edid_read_retry++;
			// Small delay before retrying
			if (edid_read_retry < MAX_RETRIES)
				usleep_range(15000, 20000);
		}
	// Keep Retrying as per MAX_RETRIES
	} while (hdmi->panel->edid_ctrl && !hdmi->panel->edid_ctrl->edid
		&& edid_read_retry < MAX_RETRIES);

	if (!hdmi->panel->edid_ctrl || !hdmi->panel->edid_ctrl->edid) {
		HDMI_WARN("EDID read failed, Continuing without EDID.");
		if (hdmi->panel->edid_ctrl)
			sde_free_edid((void **)&hdmi->panel->edid_ctrl);

		return -EINVAL;
	}

	return ret;
}

static void hdmi_display_hotplug_work(struct work_struct *work)
{
	struct hdmi_display_private *hdmi =
		container_of(work, struct hdmi_display_private, hpd_work);
	struct drm_connector *connector;
	u32 hdmi_ctrl;
	int rc = 0;

	if (!hdmi || !hdmi->panel) {
		HDMI_ERR("Invalid param\n");
		return;
	}

	connector = hdmi->hdmi_display.base_connector;

	if (hdmi->hdmi_display.connected) {
		hdmi_ctrl = hdmi_read(hdmi, HDMI_CTRL);
		hdmi_write(hdmi, HDMI_CTRL, hdmi_ctrl | HDMI_CTRL_ENABLE);

		rc = hdmi_read_edid_retry(hdmi, connector);

		hdmi_write(hdmi, HDMI_CTRL, hdmi_ctrl);
		if (!rc) {
			hdmi->hdmi_mode =
			sde_detect_hdmi_monitor(hdmi->panel->edid_ctrl);
		} else {
			hdmi->hdmi_mode = false;
		}
		//TODO: default value of hdmi_mode must be set to true

		//cec_notifier_set_phys_addr_from_edid(hdmi->cec->notifier,
		//		hdmi->edid_ctrl->edid);
		//Enable Audio post HPD connect
		rc = hdmi->audio->on(hdmi->audio);
		if (rc)
			HDMI_ERR("HDMI audio On failed status %d\n", rc);

		hdmi_display_state_add(HDMI_STATE_CONNECTED);
		hdmi_display_state_remove(HDMI_STATE_DISCONNECTED);
	} else {
		sde_free_edid((void **)&hdmi->panel->edid_ctrl);
		//cec_notifier_set_phys_addr_from_edid(hdmi->cec->notifier,
		//		NULL);
		//Disable Audio post HPD disconnect
		rc = hdmi->audio->off(hdmi->audio, 0);
		if (rc)
			HDMI_ERR("HDMI audio Off failed status %d\n", rc);

		hdmi_display_state_add(HDMI_STATE_DISCONNECTED);
		hdmi_display_state_remove(HDMI_STATE_CONNECTED);
	}

	//TODO: check hotplug notification
	drm_helper_hpd_irq_event(connector->dev);
}

static int hdmi_display_create_workqueue(struct hdmi_display_private *hdmi)
{
	hdmi->wq = create_singlethread_workqueue("drm_hdmi");
	if (IS_ERR_OR_NULL(hdmi->wq)) {
		HDMI_ERR("Error creating wq\n");
		return -EPERM;
	}

	INIT_WORK(&hdmi->hpd_work, hdmi_display_hotplug_work);
	return 0;
}

static irqreturn_t hdmi_display_irq(int irq, void *dev_id)
{
	struct hdmi_display_private *hdmi = dev_id;

	if (!hdmi) {
		HDMI_ERR("invalid data\n");
		return IRQ_NONE;
	}

	/* Process HPD: */
	hdmi_display_isr(hdmi);

	 //Process Scrambling: ISR
	hdmi->ddc->scrambling_isr((void *)hdmi->ddc);

	//sde_hdmi_ddc_hdcp2p2_isr

	hdmi->ddc->isr(hdmi->ddc->i2c);

	/* Process CEC: */
	//hdmi->cec->isr(hdmi->cec);

	return IRQ_HANDLED;
}

static int hdmi_display_enable(struct hdmi_display *hdmi_display, void *panel)
{
	int rc = 0;
	struct hdmi_display_private *hdmi;
	struct hdmi_panel *hdmi_panel;
	enum hdmi_pm_type type = HDMI_PCLK_PM;

	if (!hdmi_display || !panel) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	hdmi_panel = panel;
	if (!hdmi_panel->connector) {
		HDMI_ERR("invalid connector input\n");
		return -EINVAL;
	}

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, hdmi->state);
	mutex_lock(&hdmi->session_lock);

	/*
	 * If HDMI_STATE_READY is not set, we should not do any HW
	 * programming.
	 */
	if (!hdmi_display_state_is(HDMI_STATE_READY)) {
		hdmi_display_state_show("[host not ready]");
		goto end;
	}

	/*
	 * It is possible that by the time we get call back to establish
	 * the HDMI pipeline e2e, the physical HDMI connection to the sink is
	 * already lost. In such cases, the HDMI_STATE_ABORTED would be set.
	 * However, it is necessary to NOT abort the display setup here so as
	 * to ensure that the rest of the system is in a stable state prior to
	 * handling the disconnect notification.
	 */
	if (hdmi_display_state_is(HDMI_STATE_ABORTED))
		hdmi_display_state_log("[aborted, but continue on]");

	if (hdmi->pll->prepare) {
		rc = hdmi->pll->prepare(hdmi->pll);
		if (rc < 0) {
			HDMI_ERR("HDMI pll prepare failed\n");
			goto end;
		}
	}

	rc = hdmi->power->clk_enable(hdmi->power, type, true);
	if (rc) {
		HDMI_ERR("Unabled to start link clocks, rc=%d\n", rc);
		goto end;
	}


	rc = hdmi->phy->enable(hdmi->phy);
	if (rc) {
		HDMI_ERR("phy enable failed, rc=%d\n", rc);
		goto end;
	}

	rc = hdmi_panel->enable(hdmi_panel);
	if (rc)
		HDMI_ERR("panel enable failed, rc=%d\n", rc);

	hdmi_display_state_add(HDMI_STATE_ENABLED);
end:
	mutex_unlock(&hdmi->session_lock);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, hdmi->state, rc);
	return rc;
}

static int hdmi_display_post_enable(struct hdmi_display *hdmi_display,
				void *panel)
{
	struct hdmi_display_private *hdmi;
	struct hdmi_panel *hdmi_panel;

	if (!hdmi_display || !panel) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);
	hdmi_panel = panel;

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, hdmi->state);
	mutex_lock(&hdmi->session_lock);

	/*
	 * If HDMI_STATE_READY is not set, we should not do any HW
	 * programming.
	 */
	if (!hdmi_display_state_is(HDMI_STATE_ENABLED)) {
		hdmi_display_state_show("[not enabled]");
		goto end;
	}

	/*
	 * If the physical connection to the sink is already lost by the time
	 * we try to set up the connection, we can just skip all the steps
	 * here safely.
	 */
	if (hdmi_display_state_is(HDMI_STATE_ABORTED)) {
		hdmi_display_state_log("[aborted]");
		goto end;
	}

	HDMI_DEBUG("display post enable complete. state: 0x%x\n", hdmi->state);
end:
	mutex_unlock(&hdmi->session_lock);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, hdmi->state);

	return 0;
}

static int hdmi_display_pre_disable(struct hdmi_display *hdmi_display,
				void *panel)
{
	struct hdmi_display_private *hdmi;
	struct hdmi_panel *hdmi_panel;
	int rc = 0;

	if (!hdmi_display || !panel) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	hdmi_panel = panel;

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, hdmi->state);
	mutex_lock(&hdmi->session_lock);

	if (!hdmi_display_state_is(HDMI_STATE_ENABLED)) {
		hdmi_display_state_show("[not enabled]");
		goto end;
	}

	rc = hdmi->phy->pre_disable(hdmi->phy);
	if (rc) {
		HDMI_ERR("phy pre disable failed, rc=%d\n", rc);
		goto end;
	}

end:
	mutex_unlock(&hdmi->session_lock);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, hdmi->state);

	return rc;
}

static int hdmi_display_disable(struct hdmi_display *hdmi_display, void *panel)
{
	struct hdmi_display_private *hdmi = NULL;
	struct hdmi_panel *hdmi_panel = NULL;

	if (!hdmi_display || !panel) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);
	hdmi_panel = panel;

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, hdmi->state);
	mutex_lock(&hdmi->session_lock);

	if (!hdmi_display_state_is(HDMI_STATE_ENABLED)) {
		hdmi_display_state_show("[not enabled]");
		goto end;
	}

	if (!hdmi_display_state_is(HDMI_STATE_READY)) {
		hdmi_display_state_show("[not ready]");
		goto end;
	}

	hdmi_display_state_remove(HDMI_STATE_HDCP_ABORTED);
	hdmi_display_state_remove(HDMI_STATE_ENABLED);
end:

	mutex_unlock(&hdmi->session_lock);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, hdmi->state);
	return 0;
}

static int hdmi_display_get_display_type(struct hdmi_display *hdmi_display,
		const char **display_type)
{
	struct hdmi_display_private *hdmi;

	if (!hdmi_display || !display_type) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);

	if (hdmi->parser)
		*display_type = hdmi->parser->display_type;

	return 0;
}

static int hdmi_display_get_modes(struct hdmi_display *hdmi_display,
			void *panel)
{
	struct hdmi_display_private *hdmi;
	struct hdmi_panel *hdmi_panel;
	int ret = 0;

	if (!hdmi_display || !panel) {
		HDMI_ERR("invalid params\n");
		return 0;
	}

	hdmi_panel = panel;
	if (!hdmi_panel->connector) {
		HDMI_ERR("invalid connector\n");
		return 0;
	}

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);

	ret = hdmi_panel->get_modes(hdmi_panel,
			hdmi_panel->connector);

	hdmi_display->max_pclk_khz = hdmi->parser->max_pclk_khz;

	return ret;
}

static void hdmi_set_clock_rate(struct hdmi_display_private *hdmi,
		char *name, enum hdmi_pm_type clk_type, u32 rate)
{
	u32 num = hdmi->parser->mp[clk_type].num_clk;
	struct dss_clk *cfg = hdmi->parser->mp[clk_type].clk_config;

	/* convert to HZ for byte2 ops */
	rate *= hdmi->pll->clk_factor;

	while (num && strcmp(cfg->clk_name, name)) {
		num--;
		cfg++;
	}

	HDMI_DEBUG("setting rate=%d on clk=%s\n", rate, name);

	if (num)
		cfg->rate = rate;
	else
		HDMI_ERR("%s clk couldn't be set with rate %d\n", name, rate);
}

static int hdmi_display_config_pclk(struct hdmi_display_private *hdmi,
				u32 rate, u32 bpp)
{
	int rc = 0;
	enum hdmi_pm_type type = HDMI_PCLK_PM;

	HDMI_DEBUG("rate=%d\n", rate);


	rc = hdmi->power->set_pixel_clk_parent(hdmi->power);
	if (rc) {
		HDMI_ERR("set parent failed for pixel clk, rc=%d\n", rc);
		return rc;
	}

	hdmi_set_clock_rate(hdmi, "pixel_clk_rcg", type, rate);
	hdmi_set_clock_rate(hdmi, "pixel_clk", type, rate);

	/* intf_clk = pclk/2 */
	hdmi_set_clock_rate(hdmi, "pixel_iface_clk", type, rate/2);
	hdmi_set_clock_rate(hdmi, "pixel_iface_clk_rcg", type, rate/2);

	if (hdmi->pll->config) {
		rc = hdmi->pll->config(hdmi->pll, rate, bpp);
		if (rc < 0) {
			HDMI_ERR("HDMI pll cfg failed\n");
			return rc;
		}
	}

	return rc;
}

static void hdmi_display_disable_pclk(struct hdmi_display_private *hdmi)
{
	int rc = 0;

	hdmi->power->clk_enable(hdmi->power, HDMI_PCLK_PM, false);
	if (hdmi->pll->unprepare) {
		rc = hdmi->pll->unprepare(hdmi->pll);
		if (rc < 0)
			HDMI_ERR("pll unprepare failed\n");
	}
}

static int hdmi_display_prepare(struct hdmi_display *hdmi_display, void *panel)
{
	struct hdmi_display_private *hdmi;
	struct hdmi_panel *hdmi_panel;
	int rc;
	u32 rate, bpp;

	if (!hdmi_display || !panel) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	hdmi_panel = panel;
	if (!hdmi_panel->connector) {
		HDMI_ERR("invalid connector input\n");
		return -EINVAL;
	}

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);

	rate = hdmi_panel->pinfo.pixel_clk_khz;
	bpp = hdmi_panel->pinfo.bpp;

	rc = hdmi->phy->config(hdmi->phy, rate);
	if (rc) {
		HDMI_ERR("phy pre enable failed, rc=%d\n", rc);
		return rc;
	}

	/* enable pclks */
	rc = hdmi_display_config_pclk(hdmi, rate, bpp);
	if (rc) {
		HDMI_ERR("pclk enable failed, rc=%d\n", rc);
		return rc;
	}

	hdmi_display->pixclk = rate * hdmi->pll->clk_factor;

	rc = hdmi->phy->pre_enable(hdmi->phy);
	if (rc) {
		HDMI_ERR("phy enable failed, rc=%d\n", rc);
		return rc;
	}

	hdmi_display_state_add(HDMI_STATE_READY);

	return 0;
}

static int hdmi_display_unprepare(struct hdmi_display *hdmi_display,
				void *panel)
{
	struct hdmi_display_private *hdmi;
	int rc = 0;

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);

	hdmi_display_disable_pclk(hdmi);

	rc = hdmi->phy->disable(hdmi->phy);
	if (rc) {
		HDMI_ERR("panel disable failed, rc=%d\n", rc);
		goto end;
	}

	hdmi_display_state_remove(HDMI_STATE_READY);
	hdmi_display_state_remove(HDMI_STATE_ENABLED);

end:
	return rc;
}

static int hdmi_display_set_mode(struct hdmi_display *hdmi_display,
			void *panel, const struct drm_display_mode *mode)
{
	struct hdmi_display_private *hdmi;
	struct hdmi_panel *hdmi_panel;
	int rc;

	if (!hdmi_display || !panel) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	hdmi_panel = panel;
	if (!hdmi_panel->connector) {
		HDMI_ERR("invalid connector input\n");
		return -EINVAL;
	}

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, hdmi->state,
			mode->hdisplay, mode->vdisplay,
			drm_mode_vrefresh(mode));

	mutex_lock(&hdmi->session_lock);

	rc = hdmi_panel->set_mode(hdmi_panel, mode);
	if (rc)
		HDMI_ERR("panel set mode failed, rc=%d\n", rc);

	mutex_unlock(&hdmi->session_lock);

	//TODO: check anything pending

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, hdmi->state);
	return rc;
}

static struct hdmi_debug *hdmi_get_debug(struct hdmi_display *hdmi_display)
{
	struct hdmi_display_private *hdmi;

	if (!hdmi_display) {
		HDMI_ERR("invalid input\n");
		return ERR_PTR(-EINVAL);
	}

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);

	return hdmi->debug;
}

static int hdmi_display_get_available_hdmi_resources(
		struct hdmi_display *hdmi_display,
		const struct msm_resource_caps_info *avail_res,
		struct msm_resource_caps_info *max_hdmi_avail_res)
{
	if (!hdmi_display || !avail_res || !max_hdmi_avail_res) {
		HDMI_ERR("invalid arguments\n");
		return -EINVAL;
	}

	memcpy(max_hdmi_avail_res, avail_res,
			sizeof(struct msm_resource_caps_info));

	max_hdmi_avail_res->num_lm = min(avail_res->num_lm,
			hdmi_display->max_mixer_count);
	max_hdmi_avail_res->num_dsc = min(avail_res->num_dsc,
			hdmi_display->max_dsc_count);


	HDMI_DEBUG_V("max_lm:%d, avail_lm:%d, hdmi_avail_lm:%d\n",
			hdmi_display->max_mixer_count, avail_res->num_lm,
			max_hdmi_avail_res->num_lm);

	HDMI_DEBUG_V("max_dsc:%d, avail_dsc:%d, hdmi_avail_dsc:%d\n",
			hdmi_display->max_dsc_count, avail_res->num_dsc,
			max_hdmi_avail_res->num_dsc);

	return 0;
}

static int hdmi_display_validate_pixel_clock(struct drm_display_mode *mode,
		u32 max_pclk_khz, u32 pclk_factor)
{
	u32 pclk_khz = mode->clock;

	pclk_khz = pclk_khz / pclk_factor;
	if (pclk_khz > max_pclk_khz) {
		HDMI_DEBUG("clk: %d kHz, max: %d kHz\n",
				pclk_khz, max_pclk_khz);
		return -EPERM;
	}

	return 0;
}

static enum drm_mode_status hdmi_display_validate_mode(
		struct hdmi_display *hdmi_display,
		void *panel, struct drm_display_mode *mode,
		const struct msm_resource_caps_info *avail_res)
{
	struct hdmi_display_private *hdmi;
	struct hdmi_panel *hdmi_panel;
	struct hdmi_debug *debug;
	enum drm_mode_status mode_status = MODE_BAD;
	int rc = 0;

	if (!hdmi_display || !mode || !panel ||
			!avail_res || !avail_res->max_mixer_width) {
		HDMI_ERR("invalid params\n");
		return mode_status;
	}

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);

	mutex_lock(&hdmi->session_lock);

	hdmi_panel = panel;
	if (!hdmi_panel->connector) {
		HDMI_ERR("invalid connector\n");
		goto end;
	}

	debug = hdmi->debug;
	if (!debug)
		goto end;

	/* As per spec, 640x480 mode should always be present as fail-safe */
	if ((mode->hdisplay == 640) &&
			(mode->vdisplay == 480) &&
			(mode->clock == 25175)) {
		goto skip_validation;
	}

	rc = hdmi_display_validate_pixel_clock(mode,
			hdmi_display->max_pclk_khz,
			hdmi_panel->pclk_factor);
	if (rc)
		goto end;

	rc = hdmi_panel->validate_mode(hdmi_panel, mode);
	if (rc)
		goto end;

skip_validation:
	mode_status = MODE_OK;

	if (!avail_res->num_lm_in_use) {
		mutex_lock(&hdmi->accounting_lock);
		hdmi->tot_lm_blks_in_use -= hdmi_panel->max_lm;
		hdmi->tot_lm_blks_in_use += hdmi_panel->max_lm;
		mutex_unlock(&hdmi->accounting_lock);
	}

end:
	mutex_unlock(&hdmi->session_lock);

	HDMI_DEBUG_V("[%s clk:%d] mode is %s\n", mode->name, mode->clock,
			(mode_status == MODE_OK) ? "valid" : "invalid");

	return mode_status;
}

static int hdmi_display_setup_colospace(struct hdmi_display *hdmi_display,
		void *panel,
		u32 colorspace)
{
	struct hdmi_panel *hdmi_panel;
	struct hdmi_display_private *hdmi;

	if (!hdmi_display || !panel) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);

	if (!hdmi_display_state_is(HDMI_STATE_ENABLED)) {
		hdmi_display_state_show("[not enabled]");
		return 0;
	}

	hdmi_panel = panel;

	return hdmi_panel->set_colorspace(hdmi_panel, colorspace);
}

static int hdmi_display_config_hdr(struct hdmi_display *hdmi_display, void *panel,
			struct drm_msm_ext_hdr_metadata *hdr, bool dhdr_update)
{
	struct hdmi_panel *hdmi_panel;
	struct sde_connector *sde_conn;
	struct hdmi_display_private *hdmi;
	u64 core_clk_rate;
	bool flush_hdr;

	if (!hdmi_display || !panel) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	hdmi_panel = panel;
	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);
	sde_conn =  to_sde_connector(hdmi_panel->connector);

	if (sde_cesta_is_enabled(DPUID(hdmi_display->drm_dev)))
		core_clk_rate = sde_cesta_get_core_clk_rate(
					DPUID(hdmi_display->drm_dev));
	else
		core_clk_rate = hdmi->power->clk_get_rate(hdmi->power,
						"core_clk");
	if (!core_clk_rate) {
		HDMI_ERR("invalid rate for core_clk\n");
		return -EINVAL;
	}

	if (!hdmi_display_state_is(HDMI_STATE_ENABLED)) {
		hdmi_display_state_show("[not enabled]");
		return 0;
	}

	/*
	 * In rare cases where HDR metadata is updated independently
	 * flush the HDR metadata immediately instead of relying on
	 * the colorspace
	 */
	flush_hdr = !sde_conn->colorspace_updated;

	if (flush_hdr)
		HDMI_DEBUG("flushing the HDR metadata\n");
	else
		HDMI_DEBUG("piggy-backing with colorspace\n");

	return hdmi_panel->setup_hdr(hdmi_panel, hdr, dhdr_update,
		core_clk_rate, flush_hdr);
}

static int hdmi_display_update_pps(struct hdmi_display *hdmi_display,
		struct drm_connector *connector, char *pps_cmd)
{
	struct sde_connector *sde_conn;
	struct hdmi_panel *hdmi_panel;
	struct hdmi_display_private *hdmi;

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);

	sde_conn = to_sde_connector(connector);
	if (!sde_conn->drv_panel) {
		HDMI_ERR("invalid panel for connector:%d\n",
				connector->base.id);
		return -EINVAL;
	}

	if (!hdmi_display_state_is(HDMI_STATE_ENABLED)) {
		hdmi_display_state_show("[not enabled]");
		return 0;
	}

	hdmi_panel = sde_conn->drv_panel;
	hdmi_panel->update_pps(hdmi_panel, pps_cmd);
	return 0;
}


static int hdmi_request_irq(struct hdmi_display *hdmi_display)
{
	int rc = 0;
	struct hdmi_display_private *hdmi;

	if (!hdmi_display) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);

	hdmi->irq = irq_of_parse_and_map(hdmi->pdev->dev.of_node, 0);
	if (hdmi->irq < 0) {
		rc = hdmi->irq;
		HDMI_ERR("failed to get irq: %d\n", rc);
		return rc;
	}

	rc = devm_request_irq(&hdmi->pdev->dev, hdmi->irq, hdmi_display_irq,
				IRQF_TRIGGER_HIGH, "hdmi_display_isr", hdmi);
	if (rc < 0) {
		HDMI_ERR("failed to request IRQ%u: %d\n",
					hdmi->irq, rc);
		return rc;
	}

	disable_irq(hdmi->irq);

	return 0;

}


int hdmi_display_get_displays(struct drm_device *dev, void **displays,
		int count)
{
	int i, j;

	if (!displays) {
		HDMI_ERR("invalid data\n");
		return -EINVAL;
	}

	for (i = 0, j = 0; i < MAX_HDMI_ACTIVE_DISPLAY && j < count; i++) {
		if (!g_hdmi_display[i])
			break;

		if (g_hdmi_display[i]->drm_dev == dev) {
			displays[j] = g_hdmi_display[i];
			j++;
		}
	}

	return j;
}

static int hdmi_display_bind(struct device *dev, struct device *master,
		void *data)
{
	int rc = 0;
	struct hdmi_display_private *hdmi;
	struct drm_device *drm;
	struct platform_device *pdev = to_platform_device(dev);

	if (!dev || !pdev || !master) {
		HDMI_ERR("invalid param(s), dev %pK, pdev %pK, master %pK\n",
				dev, pdev, master);
		rc = -EINVAL;
		goto end;
	}

	drm = dev_get_drvdata(master);
	hdmi = platform_get_drvdata(pdev);
	if (!drm || !hdmi) {
		HDMI_ERR("invalid param(s), drm %pK, hdmi %pK\n",
				drm, hdmi);
		rc = -EINVAL;
		goto end;
	}

	hdmi->hdmi_display.drm_dev = drm;
	hdmi->priv = drm->dev_private;
end:
	return rc;
}

static void hdmi_display_unbind(struct device *dev, struct device *master,
		void *data)
{
	struct hdmi_display_private *hdmi;
	struct platform_device *pdev = to_platform_device(dev);

	if (!dev || !pdev) {
		HDMI_ERR("invalid param(s)\n");
		return;
	}

	hdmi = platform_get_drvdata(pdev);
	if (!hdmi) {
		HDMI_ERR("Invalid params\n");
		return;
	}

	if (hdmi->power)
		(void)hdmi->power->power_client_deinit(hdmi->power);
}

static const struct component_ops hdmi_display_comp_ops = {
	.bind = hdmi_display_bind,
	.unbind = hdmi_display_unbind,
};

static const struct of_device_id hdmi_dt_match[] = {
	{ .compatible = "qcom,hdmi-display"},
	{}
};


static int hdmi_display_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct hdmi_display_private *hdmi;
	struct hdmi_display *hdmi_display;
	const struct of_device_id *id;
	int index;

	if (!pdev || !pdev->dev.of_node) {
		HDMI_ERR("pdev not found\n");
		rc = -ENODEV;
		goto bail;
	}


	id = of_match_node(hdmi_dt_match, pdev->dev.of_node);
	if (!id)
		return -ENODEV;

	index = hdmi_display_get_num_of_displays(NULL);
	if (index >= MAX_HDMI_ACTIVE_DISPLAY) {
		HDMI_ERR("exceeds max hdmi count\n");
		rc = -EINVAL;
		goto bail;
	}

	hdmi = kzalloc(sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi) {
		rc = -ENOMEM;
		goto bail;
	}

	hdmi->pdev = pdev;
	hdmi->name = "drm_hdmi";

	rc = hdmi_display_create_workqueue(hdmi);
	if (rc) {
		HDMI_ERR("Failed to create workqueue\n");
		goto error;
	}

	platform_set_drvdata(pdev, hdmi);

	hdmi_display = &hdmi->hdmi_display;
	g_hdmi_display[index] = hdmi_display;

	hdmi_display->enable        = hdmi_display_enable;
	hdmi_display->post_enable   = hdmi_display_post_enable;
	hdmi_display->pre_disable   = hdmi_display_pre_disable;
	hdmi_display->disable       = hdmi_display_disable;
	hdmi_display->set_mode      = hdmi_display_set_mode;
	hdmi_display->validate_mode = hdmi_display_validate_mode;
	hdmi_display->get_modes     = hdmi_display_get_modes;
	hdmi_display->prepare       = hdmi_display_prepare;
	hdmi_display->unprepare     = hdmi_display_unprepare;
	hdmi_display->request_irq   = hdmi_request_irq;
	hdmi_display->get_debug     = hdmi_get_debug;
	hdmi_display->post_open     = NULL;
	hdmi_display->post_init     = hdmi_display_post_init;
	hdmi_display->config_hdr    = hdmi_display_config_hdr;
	hdmi_display->update_pps = hdmi_display_update_pps;

	hdmi_display->set_colorspace = hdmi_display_setup_colospace;
	hdmi_display->get_available_hdmi_resources =
				hdmi_display_get_available_hdmi_resources;
	hdmi_display->get_display_type = hdmi_display_get_display_type;

	spin_lock_init(&hdmi->reg_lock);

	rc = component_add(&pdev->dev, &hdmi_display_comp_ops);
	if (rc) {
		HDMI_ERR("component add failed, rc=%d\n", rc);
		goto error;
	}

	HDMI_DEBUG("success");
	return 0;
error:
	g_hdmi_display[index] = NULL;
bail:
	return rc;
}

#if (KERNEL_VERSION(6, 12, 0) <= LINUX_VERSION_CODE)
static void hdmi_display_remove(struct platform_device *pdev)
#else
static int hdmi_display_remove(struct platform_device *pdev)
#endif
{
	int rc = 0;
	struct hdmi_display_private *hdmi;

	if (!pdev) {
		rc = -EINVAL;
		HDMI_ERR("invalid param\n");
		goto end;
	}

	hdmi = platform_get_drvdata(pdev);

	hdmi_display_deinit_sub_modules(hdmi);

	if (hdmi->wq)
		destroy_workqueue(hdmi->wq);

	platform_set_drvdata(pdev, NULL);
	kfree(hdmi);
end:
#if (KERNEL_VERSION(6, 12, 0) > LINUX_VERSION_CODE)
	return rc;
#else
	return;
#endif
}


static int hdmi_pm_prepare(struct device *dev)
{
	// pm_prepare
	return 0;
}
static void hdmi_pm_complete(struct device *dev)
{
	// pm_complete
}

static const struct dev_pm_ops hdmi_pm_ops = {
	.prepare = hdmi_pm_prepare,
	.complete = hdmi_pm_complete,
};

static struct platform_driver hdmi_display_driver = {
	.probe  = hdmi_display_probe,
	.remove = hdmi_display_remove,
	.driver = {
		.name = "msm-hdmi-display",
		.of_match_table = hdmi_dt_match,
		.suppress_bind_attrs = true,
		.pm = &hdmi_pm_ops,
	},
};
void __init hdmi_display_register(void)
{
	hdmi_pll_drv_register();
	platform_driver_register(&hdmi_display_driver);
}
void __exit hdmi_display_unregister(void)
{
	platform_driver_unregister(&hdmi_display_driver);
	hdmi_pll_drv_unregister();
}
