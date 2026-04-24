// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */


#include <drm/drm_bridge.h>
#include "hdmi_drm.h"
#include "hdmi_debug.h"

#define HDMI_DISPLAY_MAX_WIDTH          4096
#define HDMI_DISPLAY_MAX_HEIGHT         2160

#define to_hdmi_bridge(x)     container_of((x), struct hdmi_bridge, base)

int hdmi_connector_set_info_blob(struct drm_connector *connector,
		void *info, void *display, struct msm_mode_info *mode_info)
{
	struct hdmi_display *hdmi_display = display;
	const char *display_type = NULL;

	hdmi_display->get_display_type(hdmi_display, &display_type);
	sde_kms_info_add_keystr(info, "display type", display_type);

	return 0;
}

int hdmi_connector_post_init(struct drm_connector *connector, void *display)
{
	int rc = 0;
	struct hdmi_display *hdmi_display = display;
	struct sde_connector *sde_conn;

	if (!hdmi_display || !connector)
		return -EINVAL;

	hdmi_display->base_connector = connector;
	hdmi_display->bridge->connector = connector;

	if (hdmi_display->post_init) {
		rc = hdmi_display->post_init(hdmi_display);
		if (rc)
			goto end;
	}

	sde_conn = to_sde_connector(connector);
	hdmi_display->bridge->hdmi_panel = sde_conn->drv_panel;

	if (hdmi_display->dsc_cont_pps)
		sde_conn->ops.update_pps = NULL;

end:
	return rc;
}


int hdmi_connector_get_info(struct drm_connector *connector,
			struct msm_display_info *info, void *data)
{
	int rc = 0;
	struct hdmi_display *display = data;

	if (!display || !info || !display->drm_dev) {
		HDMI_ERR("invalid params\n");
		return -EINVAL;
	}

	info->intf_type = DRM_MODE_CONNECTOR_HDMIA;
	info->num_of_h_tiles = 1;
	info->h_tile_instance[0] = 0;
	if (display->non_pluggable) {
		info->capabilities = MSM_DISPLAY_CAP_VID_MODE;
		display->connected = true;
	} else {
		info->capabilities = MSM_DISPLAY_CAP_HOT_PLUG |
			MSM_DISPLAY_CAP_EDID | MSM_DISPLAY_CAP_VID_MODE;
	}
	info->is_connected = display->connected;
	info->curr_panel_mode = MSM_DISPLAY_VIDEO_MODE;
	info->max_width = HDMI_DISPLAY_MAX_WIDTH;
	info->max_height = HDMI_DISPLAY_MAX_HEIGHT;

	return rc;
}

enum drm_connector_status hdmi_connector_detect(struct drm_connector *conn,
		bool force,
		void *display)
{
	enum drm_connector_status status = connector_status_unknown;
	struct msm_display_info info;
	struct hdmi_display *hdmi_disp;
	int rc;

	if (!conn || !display)
		return status;

	hdmi_disp = display;
	/* get display hdmi_info */
	memset(&info, 0x0, sizeof(info));
	rc = hdmi_connector_get_info(conn, &info, display);
	if (rc) {
		HDMI_ERR("failed to get display info, rc=%d\n", rc);
		return connector_status_disconnected;
	}

	if (info.capabilities & MSM_DISPLAY_CAP_HOT_PLUG) {
		status = (info.is_connected ? connector_status_connected :
					      connector_status_disconnected);
	} else
		status = connector_status_connected;

	conn->display_info.width_mm = info.width_mm;
	conn->display_info.height_mm = info.height_mm;

	return status;
}

int hdmi_connector_atomic_check(struct drm_connector *connector,
	void *display,
	struct drm_atomic_state *a_state)
{
	struct sde_connector *sde_conn;
	struct drm_connector_state *old_state;
	struct drm_connector_state *c_state;

	if (!connector || !display || !a_state)
		return -EINVAL;

	c_state = drm_atomic_get_new_connector_state(a_state, connector);
	old_state =
		drm_atomic_get_old_connector_state(a_state, connector);

	if (!old_state || !c_state)
		return -EINVAL;

	sde_conn = to_sde_connector(connector);

	/*
	 * Marking the colorspace has been changed
	 * the flag shall be checked in the pre_kickoff
	 * to configure the new colorspace in HW
	 */
	if (c_state->colorspace != old_state->colorspace) {
		HDMI_DEBUG("colorspace has been updated\n");
		sde_conn->colorspace_updated = true;
	}

	return 0;
}

int hdmi_connector_get_modes(struct drm_connector *connector,
		void *display, const struct msm_resource_caps_info *avail_res)
{
	int rc = 0;
	struct hdmi_display *hdmi;
	struct sde_connector *sde_conn;

	if (!connector || !display)
		return 0;

	sde_conn = to_sde_connector(connector);
	if (!sde_conn->drv_panel) {
		HDMI_ERR("invalid hdmi panel\n");
		return 0;
	}

	hdmi = display;

	/* pluggable case assumes EDID is read when HPD */
	if (hdmi->connected) {
		rc = hdmi->get_modes(hdmi, sde_conn->drv_panel);
		if (!rc)
			HDMI_WARN("failed to get HDMI sink modes, adding failsafe");
	} else {
		HDMI_ERR("No sink connected\n");
	}

	return rc;
}

enum drm_mode_status hdmi_connector_mode_valid(struct drm_connector *connector,
		struct drm_display_mode *mode, void *display,
		const struct msm_resource_caps_info *avail_res)
{
	int rc = 0, vrefresh;
	struct hdmi_display *hdmi_disp;
	struct sde_connector *sde_conn;
	struct msm_resource_caps_info avail_hdmi_res;
	struct hdmi_panel *hdmi_panel;

	if (!mode || !display || !connector) {
		HDMI_ERR("invalid params\n");
		return MODE_ERROR;
	}

	sde_conn = to_sde_connector(connector);
	if (!sde_conn->drv_panel) {
		HDMI_ERR("invalid hdmi panel\n");
		return MODE_ERROR;
	}

	hdmi_disp = display;
	hdmi_panel = sde_conn->drv_panel;

	vrefresh = drm_mode_vrefresh(mode);

	rc = hdmi_disp->get_available_hdmi_resources(hdmi_disp, avail_res,
			&avail_hdmi_res);
	if (rc) {
		HDMI_ERR("error getting max hdmi resources. rc:%d\n", rc);
		return MODE_ERROR;
	}

	/* As per spec, failsafe mode should always be present */
	if ((mode->hdisplay == 640) &&
		(mode->vdisplay == 480) &&
		(mode->clock == 25175))
		goto validate_mode;

	if (hdmi_panel->mode_override &&
			(mode->hdisplay != hdmi_panel->hdisplay ||
			mode->vdisplay != hdmi_panel->vdisplay ||
			vrefresh != hdmi_panel->vrefresh))
		return MODE_BAD;
	else if (hdmi_panel->mode_override)
		mode->type |= DRM_MODE_TYPE_PREFERRED;

validate_mode:
	return hdmi_disp->validate_mode(hdmi_disp, sde_conn->drv_panel,
			mode, &avail_hdmi_res);
}

int hdmi_connector_get_mode_info(struct drm_connector *connector,
		const struct drm_display_mode *drm_mode,
		struct msm_sub_mode *sub_mode,
		struct msm_mode_info *mode_info,
		void *display, const struct msm_resource_caps_info *avail_res)
{
	const u32 single_intf = 1;
	const u32 no_enc = 0;
	struct msm_display_topology *topology;
	struct sde_connector *sde_conn;
	struct hdmi_panel *hdmi_panel;
	struct hdmi_display *hdmi_disp = display;
	struct msm_drm_private *priv;
	struct msm_resource_caps_info avail_hdmi_res;
	int rc = 0;

	if (!drm_mode || !mode_info || !avail_res ||
			!avail_res->max_mixer_width || !connector || !display ||
			!connector->dev || !connector->dev->dev_private) {
		HDMI_ERR("invalid params\n");
		return -EINVAL;
	}

	memset(mode_info, 0, sizeof(*mode_info));

	sde_conn = to_sde_connector(connector);
	hdmi_panel = sde_conn->drv_panel;
	priv = connector->dev->dev_private;

	topology = &mode_info->topology;

	rc = hdmi_disp->get_available_hdmi_resources(hdmi_disp, avail_res,
			&avail_hdmi_res);
	if (rc) {
		HDMI_ERR("error getting max hdmi resources. rc:%d\n", rc);
		return rc;
	}

	rc = msm_get_mixer_count(priv, drm_mode, &avail_hdmi_res,
			&topology->num_lm);
	if (rc) {
		HDMI_ERR("error getting mixer count. rc:%d\n", rc);
		return rc;
	}
	/* reset hdmi connector lm_mask for every connection event and
	 * this will get re-populated in resource manager based on
	 * resolution and topology of hdmi display.
	 */
	sde_conn->lm_mask = 0;

	topology->num_enc = no_enc;
	topology->num_intf = single_intf;

	mode_info->frame_rate = drm_mode_vrefresh(drm_mode);
	mode_info->vtotal = drm_mode->vtotal;

	mode_info->pclk_factor = hdmi_panel->pclk_factor;

	return 0;
}

void hdmi_connector_post_open(struct drm_connector *connector, void *display)
{
	struct hdmi_display *hdmi;

	if (!display) {
		HDMI_ERR("invalid input\n");
		return;
	}

	hdmi = display;

	if (hdmi->post_open)
		hdmi->post_open(hdmi);
}

int hdmi_connector_set_colorspace(struct drm_connector *connector,
	void *display)
{
	struct hdmi_display *hdmi_display = display;
	struct sde_connector *sde_conn;

	if (!hdmi_display || !connector)
		return -EINVAL;

	sde_conn = to_sde_connector(connector);
	if (!sde_conn->drv_panel) {
		pr_err("invalid hdmi panel\n");
		return -EINVAL;
	}

	return hdmi_display->set_colorspace(hdmi_display,
		sde_conn->drv_panel, connector->state->colorspace);
}

int hdmi_connector_config_hdr(struct drm_connector *connector, void *display,
	struct sde_connector_state *c_state)
{
	struct hdmi_display *hdmi = display;
	struct sde_connector *sde_conn;

	if (!display || !c_state || !connector) {
		HDMI_ERR("invalid params\n");
		return -EINVAL;
	}

	sde_conn = to_sde_connector(connector);
	if (!sde_conn->drv_panel) {
		HDMI_ERR("invalid hdmi panel\n");
		return -EINVAL;
	}

	return hdmi->config_hdr(hdmi, sde_conn->drv_panel, &c_state->hdr_meta,
			c_state->dyn_hdr_meta.dynamic_hdr_update);
}

int hdmi_connector_update_pps(struct drm_connector *connector,
		char *pps_cmd, void *display)
{
	struct hdmi_display *hdmi_disp;
	struct sde_connector *sde_conn;

	if (!display || !connector) {
		HDMI_ERR("invalid params\n");
		return -EINVAL;
	}

	sde_conn = to_sde_connector(connector);
	if (!sde_conn->drv_panel) {
		HDMI_ERR("invalid hdmi panel\n");
		return MODE_ERROR;
	}

	hdmi_disp = display;
	return hdmi_disp->update_pps(hdmi_disp, connector, pps_cmd);
}


int hdmi_connector_install_properties(void *display, struct drm_connector *conn)
{
	struct hdmi_display *hdmi_display = display;
	struct drm_connector *base_conn;
	int rc;

	if (!display || !conn) {
		HDMI_ERR("invalid params\n");
		return -EINVAL;
	}

	base_conn = hdmi_display->base_connector;

	/*
	 * Create the property on the base connector during probe time and then
	 * attach the same property onto new connector objects created for MST
	 */
	if (!base_conn->colorspace_property) {
		/* This is the base connector. create the drm property */
		rc = drm_mode_create_hdmi_colorspace_property(base_conn, 0);
		if (rc)
			return rc;
	} else {
		conn->colorspace_property = base_conn->colorspace_property;
	}

	drm_object_attach_property(&conn->base, conn->colorspace_property, 0);

	return 0;
}

static int hdmi_bridge_attach(struct drm_bridge *hdmi_bridge,
				enum drm_bridge_attach_flags flags)
{
	struct hdmi_bridge *bridge = to_hdmi_bridge(hdmi_bridge);

	if (!hdmi_bridge) {
		HDMI_ERR("Invalid params\n");
		return -EINVAL;
	}

	HDMI_DEBUG("[%d] attached\n", bridge->id);

	return 0;
}


static void hdmi_bridge_pre_enable(struct drm_bridge *drm_bridge)
{
	int rc = 0;
	struct hdmi_bridge *bridge;
	struct hdmi_display *hdmi;

	if (!drm_bridge) {
		HDMI_ERR("Invalid params\n");
		return;
	}

	bridge = to_hdmi_bridge(drm_bridge);
	hdmi = bridge->display;

	if (!bridge->connector) {
		HDMI_ERR("Invalid connector\n");
		return;
	}

	if (!bridge->hdmi_panel) {
		HDMI_ERR("Invalid hdmi_panel\n");
		return;
	}

	rc = hdmi->prepare(hdmi, bridge->hdmi_panel);
	if (rc) {
		HDMI_ERR("[%d] HDMI display prepare failed, rc=%d\n",
		       bridge->id, rc);
		return;
	}

	rc = hdmi->enable(hdmi, bridge->hdmi_panel);
	if (rc)
		HDMI_ERR("[%d] HDMI display enable failed, rc=%d\n",
		       bridge->id, rc);
}

static void hdmi_bridge_enable(struct drm_bridge *drm_bridge)
{
	int rc = 0;
	struct hdmi_bridge *bridge;
	struct hdmi_display *hdmi;

	if (!drm_bridge) {
		HDMI_ERR("Invalid params\n");
		return;
	}

	bridge = to_hdmi_bridge(drm_bridge);
	if (!bridge->connector) {
		HDMI_ERR("Invalid connector\n");
		return;
	}

	if (!bridge->hdmi_panel) {
		HDMI_ERR("Invalid hdmi_panel\n");
		return;
	}

	hdmi = bridge->display;

	rc = hdmi->post_enable(hdmi, bridge->hdmi_panel);
	if (rc)
		HDMI_ERR("[%d] HDMI display post enable failed, rc=%d\n",
		       bridge->id, rc);
}

static void hdmi_bridge_disable(struct drm_bridge *drm_bridge)
{
	int rc = 0;
	struct hdmi_bridge *bridge;
	struct hdmi_display *hdmi;

	if (!drm_bridge) {
		HDMI_ERR("Invalid params\n");
		return;
	}

	bridge = to_hdmi_bridge(drm_bridge);
	if (!bridge->connector) {
		HDMI_ERR("Invalid connector\n");
		return;
	}

	if (!bridge->hdmi_panel) {
		HDMI_ERR("Invalid hdmi_panel\n");
		return;
	}

	hdmi = bridge->display;

	if (!hdmi) {
		HDMI_ERR("hdmi is null\n");
		return;
	}

	sde_connector_helper_bridge_disable(bridge->connector);

	rc = hdmi->pre_disable(hdmi, bridge->hdmi_panel);
	if (rc)
		HDMI_ERR("[%d] HDMI display pre disable failed, rc=%d\n",
		       bridge->id, rc);
}

static void hdmi_bridge_post_disable(struct drm_bridge *drm_bridge)
{
	int rc = 0;
	struct hdmi_bridge *bridge;
	struct hdmi_display *hdmi;

	if (!drm_bridge) {
		HDMI_ERR("Invalid params\n");
		return;
	}

	bridge = to_hdmi_bridge(drm_bridge);
	if (!bridge->connector) {
		HDMI_ERR("Invalid connector\n");
		return;
	}

	if (!bridge->hdmi_panel) {
		HDMI_ERR("Invalid hdmi_panel\n");
		return;
	}

	hdmi = bridge->display;
	DRM_DEBUG("power down");

	rc = hdmi->disable(hdmi, bridge->hdmi_panel);
	if (rc) {
		HDMI_ERR("[%d] HDMI display disable failed, rc=%d\n",
					bridge->id, rc);
		return;
	}

	rc = hdmi->unprepare(hdmi, bridge->hdmi_panel);
	if (rc) {
		HDMI_ERR("[%d] HDMI display unprepare failed, rc=%d\n",
					bridge->id, rc);
		return;
	}
}


static void hdmi_bridge_mode_set(struct drm_bridge *drm_bridge,
				const struct drm_display_mode *mode,
				const struct drm_display_mode *adjusted_mode)
{
	int rc = 0;
	struct hdmi_bridge *bridge;
	struct hdmi_display *hdmi;

	if (!drm_bridge) {
		HDMI_ERR("Invalid params\n");
		return;
	}

	bridge = to_hdmi_bridge(drm_bridge);
	if (!bridge->connector) {
		HDMI_ERR("Invalid connector\n");
		return;
	}

	if (!bridge->hdmi_panel) {
		HDMI_ERR("Invalid hdmi_panel\n");
		return;
	}

	hdmi = bridge->display;
	DRM_DEBUG("mode set");

	/* By this point mode should have been validated through mode_fixup */
	rc = hdmi->set_mode(hdmi, bridge->hdmi_panel, mode);
	if (rc)
		HDMI_ERR("[%d] failed to perform a mode set, rc=%d\n",
		       bridge->id, rc);

	//FIXME: check what to do with adj mode
	return;

}

static bool hdmi_bridge_mode_fixup(struct drm_bridge *drm_bridge,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	bool ret = true;
	struct hdmi_bridge *bridge;
	struct hdmi_display *hdmi;

	if (!drm_bridge || !mode || !adjusted_mode) {
		HDMI_ERR("Invalid params\n");
		ret = false;
		goto end;
	}

	bridge = to_hdmi_bridge(drm_bridge);
	if (!bridge->connector) {
		HDMI_ERR("Invalid connector\n");
		ret = false;
		goto end;
	}

	if (!bridge->hdmi_panel) {
		HDMI_ERR("Invalid hdmi_panel\n");
		ret = false;
		goto end;
	}

	hdmi = bridge->display;

	//FIXME: copy mode to adj mode
end:
	return ret;
}

static const struct drm_bridge_funcs hdmi_bridge_ops = {
	.attach       = hdmi_bridge_attach,
	.mode_fixup   = hdmi_bridge_mode_fixup,
	.pre_enable   = hdmi_bridge_pre_enable,
	.enable       = hdmi_bridge_enable,
	.disable      = hdmi_bridge_disable,
	.post_disable = hdmi_bridge_post_disable,
	.mode_set     = hdmi_bridge_mode_set,
};



int hdmi_drm_bridge_init(void *data, struct drm_encoder *encoder,
		u32 max_mixer_count, u32 max_dsc_count)
{
	int rc = 0;
	struct hdmi_bridge *bridge;
	struct drm_device *dev;
	struct hdmi_display *display = data;
	struct msm_drm_private *priv = NULL;

	bridge = kzalloc(sizeof(*bridge), GFP_KERNEL);
	if (!bridge) {
		rc = -ENOMEM;
		goto error;
	}

	dev = display->drm_dev;
	bridge->display = display;
	bridge->base.funcs = &hdmi_bridge_ops;
	bridge->base.encoder = encoder;

	priv = dev->dev_private;

	rc = drm_bridge_attach(encoder, &bridge->base, NULL, 0);
	if (rc) {
		HDMI_ERR("failed to attach bridge, rc=%d\n", rc);
		goto error_free_bridge;
	}

	rc = display->request_irq(display);
	if (rc) {
		HDMI_ERR("request_irq failed, rc=%d\n", rc);
		goto error_free_bridge;
	}

	priv->bridges[priv->num_bridges++] = &bridge->base;
	display->bridge = bridge;
	// currently this is equal to total mixer - dsi mixers.
	// avail_res is not consolidated between hdmi and dp.
	display->max_mixer_count = max_mixer_count;
	display->max_dsc_count = max_dsc_count;

	return 0;
error_free_bridge:
	kfree(bridge);
error:

	return rc;
}

void hdmi_drm_bridge_deinit(void *data)
{
	struct hdmi_display *display = data;
	struct hdmi_bridge *bridge = display->bridge;

	kfree(bridge);

}
