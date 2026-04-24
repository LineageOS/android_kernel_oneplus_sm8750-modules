// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <drm/drm_edid.h>
#include <drm/display/drm_hdmi_helper.h>

#include "hdmi_panel.h"
#include "hdmi_debug.h"
#include "sde_edid_parser.h"
#include "sde_connector.h"
#include "hdmi_regs.h"

#define FULL_COLORIMETRY_MASK           0x1FF
#define NORMAL_COLORIMETRY_MASK         0x3
#define EXTENDED_COLORIMETRY_MASK       0x7
#define EXTENDED_ACE_COLORIMETRY_MASK   0xF

#define C(x)	((x) << 0)
#define EC(x)	((x) << 2)
#define ACE(x)	((x) << 5)

#define HDMI_COLORIMETRY_NO_DATA		0x0
#define HDMI_COLORIMETRY_SMPTE_170M_YCC		(C(1) | EC(0) | ACE(0))
#define HDMI_COLORIMETRY_BT709_YCC		(C(2) | EC(0) | ACE(0))
#define HDMI_COLORIMETRY_XVYCC_601		(C(3) | EC(0) | ACE(0))
#define HDMI_COLORIMETRY_XVYCC_709		(C(3) | EC(1) | ACE(0))
#define HDMI_COLORIMETRY_SYCC_601		(C(3) | EC(2) | ACE(0))
#define HDMI_COLORIMETRY_OPYCC_601		(C(3) | EC(3) | ACE(0))
#define HDMI_COLORIMETRY_OPRGB			(C(3) | EC(4) | ACE(0))
#define HDMI_COLORIMETRY_BT2020_CYCC		(C(3) | EC(5) | ACE(0))
#define HDMI_COLORIMETRY_BT2020_RGB		(C(3) | EC(6) | ACE(0))
#define HDMI_COLORIMETRY_BT2020_YCC		(C(3) | EC(6) | ACE(0))
#define	HDMI_COLORIMETRY_DCI_P3_RGB_D65		(C(3) | EC(7) | ACE(0))
#define HDMI_COLORIMETRY_DCI_P3_RGB_THEATER	(C(3) | EC(7) | ACE(1))

static const u32 hdmi_panel_colorimetry_val[] = {
	[DRM_MODE_COLORIMETRY_NO_DATA]		= HDMI_COLORIMETRY_NO_DATA,
	[DRM_MODE_COLORIMETRY_SMPTE_170M_YCC]	= HDMI_COLORIMETRY_SMPTE_170M_YCC,
	[DRM_MODE_COLORIMETRY_BT709_YCC]	= HDMI_COLORIMETRY_BT709_YCC,
	[DRM_MODE_COLORIMETRY_XVYCC_601]	= HDMI_COLORIMETRY_XVYCC_601,
	[DRM_MODE_COLORIMETRY_XVYCC_709]	= HDMI_COLORIMETRY_XVYCC_709,
	[DRM_MODE_COLORIMETRY_SYCC_601]		= HDMI_COLORIMETRY_SYCC_601,
	[DRM_MODE_COLORIMETRY_OPYCC_601]	= HDMI_COLORIMETRY_OPYCC_601,
	[DRM_MODE_COLORIMETRY_OPRGB]		= HDMI_COLORIMETRY_OPRGB,
	[DRM_MODE_COLORIMETRY_BT2020_CYCC]	= HDMI_COLORIMETRY_BT2020_CYCC,
	[DRM_MODE_COLORIMETRY_BT2020_RGB]	= HDMI_COLORIMETRY_BT2020_RGB,
	[DRM_MODE_COLORIMETRY_BT2020_YCC]	= HDMI_COLORIMETRY_BT2020_YCC,
};

#undef C
#undef EC
#undef ACE

#define HDMI_VS_INFOFRAME_BUFFER_SIZE	(HDMI_INFOFRAME_HEADER_SIZE + 6)
#define LEFT_SHIFT_BYTE(x)	((x) << 8)
#define LEFT_SHIFT_WORD(x)	((x) << 16)
#define LEFT_SHIFT_24BITS(x)	((x) << 24)

#define HDMI_GET_MSB(x)	(x >> 8)
#define HDMI_GET_LSB(x)	(x & 0xff)

#define HDMI_DEFAULT_PRODUCT_NAME	"msm"
#define HDMI_DEFAULT_VENDOR_NAME	"unknown"


#define	MAX_REG_HDMI_GENERIC1_INDEX	6

#define hdmi_read(off) ({ \
	readl_relaxed(panel->io_data->io.base + off); \
})

#define hdmi_write(off, value) ({ \
	writel_relaxed(value, panel->io_data->io.base + off); \
})

enum {
	HDMI_AVI_IFRAME_LINE_NUMBER	= 0x1,
	HDMI_VENDOR_IFRAME_LINE_NUMBER	= 0x3,
};

enum hdmi_panel_hdr_state {
	HDR_DISABLED,
	HDR_ENABLED,
};

struct hdmi_panel_private {
	bool is_hdmi_mode;
	struct device *dev;
	struct hdmi_panel hdmi_panel;
	struct hdmi_io_data *io_data;

	enum hdmi_panel_hdr_state hdr_state;
	/* DRM Connector associated with this panel. */
	struct drm_connector *connector;
};


/* Add these register definitions to support the latest chipsets. These
 * are going to be replaced by a chipset-based mask approach.
 */
#define HDMI_ACTIVE_HSYNC_START__MASK	0x00001fff
#define HDMI_ACTIVE_HSYNC_START__SHIFT	0
static inline u32 HDMI_ACTIVE_HSYNC_START(u32 val)
{
	return ((val) << HDMI_ACTIVE_HSYNC_START__SHIFT) &
		HDMI_ACTIVE_HSYNC_START__MASK;
}

#define HDMI_ACTIVE_HSYNC_END__MASK	0x1fff0000
#define HDMI_ACTIVE_HSYNC_END__SHIFT	16
static inline u32 HDMI_ACTIVE_HSYNC_END(u32 val)
{
	return ((val) << HDMI_ACTIVE_HSYNC_END__SHIFT) &
		HDMI_ACTIVE_HSYNC_END__MASK;
}

#define HDMI_ACTIVE_VSYNC_START__MASK	0x00001fff
#define HDMI_ACTIVE_VSYNC_START__SHIFT	0
static inline u32 HDMI_ACTIVE_VSYNC_START(u32 val)
{
	return ((val) << HDMI_ACTIVE_VSYNC_START__SHIFT) &
		HDMI_ACTIVE_VSYNC_START__MASK;
}

#define HDMI_ACTIVE_VSYNC_END__MASK	0x1fff0000
#define HDMI_ACTIVE_VSYNC_END__SHIFT	16
static inline u32 HDMI_ACTIVE_VSYNC_END(u32 val)
{
	return ((val) << HDMI_ACTIVE_VSYNC_END__SHIFT) &
		HDMI_ACTIVE_VSYNC_END__MASK;
}

#define HDMI_VSYNC_ACTIVE_F2_START__MASK	0x00001fff
#define HDMI_VSYNC_ACTIVE_F2_START__SHIFT	0
static inline u32 HDMI_VSYNC_ACTIVE_F2_START(u32 val)
{
	return ((val) << HDMI_VSYNC_ACTIVE_F2_START__SHIFT) &
		HDMI_VSYNC_ACTIVE_F2_START__MASK;
}

#define HDMI_VSYNC_ACTIVE_F2_END__MASK		0x00001fff
#define HDMI_VSYNC_ACTIVE_F2_END__SHIFT		16
static inline u32 HDMI_VSYNC_ACTIVE_F2_END(u32 val)
{
	return ((val) << HDMI_VSYNC_ACTIVE_F2_END__SHIFT) &
		HDMI_VSYNC_ACTIVE_F2_END__MASK;
}

#define HDMI_TOTAL_H_TOTAL__MASK	0x00001fff
#define HDMI_TOTAL_H_TOTAL__SHIFT	0
static inline u32 HDMI_TOTAL_H_TOTAL(u32 val)
{
	return ((val) << HDMI_TOTAL_H_TOTAL__SHIFT) &
		HDMI_TOTAL_H_TOTAL__MASK;
}

#define HDMI_TOTAL_V_TOTAL__MASK	0x1fff0000
#define HDMI_TOTAL_V_TOTAL__SHIFT	16
static inline u32 HDMI_TOTAL_V_TOTAL(u32 val)
{
	return ((val) << HDMI_TOTAL_V_TOTAL__SHIFT) &
		HDMI_TOTAL_V_TOTAL__MASK;
}

#define HDMI_VSYNC_TOTAL_F2_V_TOTAL__MASK	0x00001fff
#define HDMI_VSYNC_TOTAL_F2_V_TOTAL__SHIFT	0
static inline u32 HDMI_VSYNC_TOTAL_F2_V_TOTAL(u32 val)
{
	return ((val) << HDMI_VSYNC_TOTAL_F2_V_TOTAL__SHIFT) &
		HDMI_VSYNC_TOTAL_F2_V_TOTAL__MASK;
}

static inline u32 HDMI_AVI_INFO(u32 i0)
{
	return HDMI_AVI_INFO_0 + 0x4*i0;
}

static inline u32 HDMI_GENERIC0(u32 i0)
{
	return HDMI_GENERIC0_0 + 0x4*i0;
}

static inline u32 HDMI_GENERIC1(u32 i0)
{
	return HDMI_GENERIC1_0 + 0x4*i0;
}

static inline void hdmi_panel_update_pps(
		struct hdmi_panel *hdmi_panel, char *pps_cmd)
{
}

static inline int hdmi_panel_set_colorspace(
		struct hdmi_panel *hdmi_panel, u32 colorspace)
{
	return 0;
}

static inline int hdmi_pabel_get_panel_on(struct hdmi_panel *hdmi_panel)
{
	return 0;
}

static void hdmi_panel_avi_infoframe_colorimetry(
		struct hdmi_avi_infoframe *frame,
		const struct drm_connector_state *conn_state)
{
	u32 colorimetry_val;
	u32 colorimetry_index = conn_state->colorspace & FULL_COLORIMETRY_MASK;

	if (colorimetry_index >= ARRAY_SIZE(hdmi_panel_colorimetry_val))
		colorimetry_val = HDMI_COLORIMETRY_NO_DATA;
	else
		colorimetry_val = hdmi_panel_colorimetry_val[colorimetry_index];

	frame->colorimetry = colorimetry_val & NORMAL_COLORIMETRY_MASK;

	frame->extended_colorimetry = (colorimetry_val >> 2) &
						EXTENDED_COLORIMETRY_MASK;
}

static void hdmi_panel_avi_infoframe_bars(struct hdmi_avi_infoframe *frame,
				const struct drm_connector_state *conn_state)
{
	frame->right_bar = conn_state->tv.margins.right;
	frame->left_bar = conn_state->tv.margins.left;
	frame->top_bar = conn_state->tv.margins.top;
	frame->bottom_bar = conn_state->tv.margins.bottom;
}

static void _hdmi_panel_config_avi_iframe(
		struct hdmi_panel_private *panel, u8 *buffer)
{
	u32 reg_val;
	u8 checksum;
	u8 *avi_frame = &buffer[HDMI_INFOFRAME_HEADER_SIZE];

	checksum = buffer[HDMI_INFOFRAME_HEADER_SIZE - 1];
	reg_val = checksum |
		LEFT_SHIFT_BYTE(avi_frame[0]) |
		LEFT_SHIFT_WORD(avi_frame[1]) |
		LEFT_SHIFT_24BITS(avi_frame[2]);
	hdmi_write(HDMI_AVI_INFO(0), reg_val);

	reg_val = avi_frame[3] |
		LEFT_SHIFT_BYTE(avi_frame[4]) |
		LEFT_SHIFT_WORD(avi_frame[5]) |
		LEFT_SHIFT_24BITS(avi_frame[6]);
	hdmi_write(HDMI_AVI_INFO(1), reg_val);

	reg_val = avi_frame[7] |
		LEFT_SHIFT_BYTE(avi_frame[8]) |
		LEFT_SHIFT_WORD(avi_frame[9]) |
		LEFT_SHIFT_24BITS(avi_frame[10]);
	hdmi_write(HDMI_AVI_INFO(2), reg_val);

	reg_val = avi_frame[11] |
		LEFT_SHIFT_BYTE(avi_frame[12]) |
		LEFT_SHIFT_24BITS(buffer[1]);
	hdmi_write(HDMI_AVI_INFO(3), reg_val);
}

static void _hdmi_panel_config_vs_iframe(
		struct hdmi_panel_private *panel,
		struct hdmi_vendor_infoframe *frame,
		u8 *buffer)
{
	u32 reg_val;

	reg_val = LEFT_SHIFT_24BITS(frame->s3d_struct) |
		LEFT_SHIFT_WORD(frame->vic) |
		LEFT_SHIFT_BYTE(buffer[3]) |
		(buffer[7] << 5) | buffer[2];
	hdmi_write(HDMI_VENSPEC_INFO0, reg_val);
}

static void _hdmi_panel_config_spd_iframe(
		struct hdmi_panel_private *panel, u8 *buffer)
{
	int i;
	u32 packet_payload, packet_header;

	packet_header = buffer[0]
			| LEFT_SHIFT_BYTE(buffer[1] & 0x7f)
			| LEFT_SHIFT_WORD(buffer[2] & 0x7f);
	hdmi_write(HDMI_GENERIC1_HDR, packet_header);

	for (i = 0; i < MAX_REG_HDMI_GENERIC1_INDEX; i++) {
		packet_payload = buffer[3 + i * 4]
			| LEFT_SHIFT_BYTE(buffer[4 + i * 4] & 0x7f)
			| LEFT_SHIFT_WORD(buffer[5 + i * 4] & 0x7f)
			| LEFT_SHIFT_24BITS(buffer[6 + i * 4] & 0x7f);
		hdmi_write(HDMI_GENERIC1(i), packet_payload);
	}

	packet_payload = (buffer[27] & 0x7f)
			| LEFT_SHIFT_BYTE(buffer[28] & 0x7f);
	hdmi_write(HDMI_GENERIC1(MAX_REG_HDMI_GENERIC1_INDEX),
			packet_payload);
}

static void _hdmi_panel_config_mode(struct hdmi_panel_private *panel)
{
	u32 frame_ctrl = 0;
	struct hdmi_panel_info *pinfo = &panel->hdmi_panel.pinfo;

	/* TODO: Confirm the register configuration.
	 * is -1 really required, if so, WHY?
	 */
	hdmi_write(HDMI_TOTAL,
			HDMI_TOTAL_H_TOTAL(pinfo->htotal - 1) |
			HDMI_TOTAL_V_TOTAL(pinfo->vtotal - 1));

	hdmi_write(HDMI_ACTIVE_HSYNC,
			HDMI_ACTIVE_HSYNC_START(pinfo->h_start) |
			HDMI_ACTIVE_HSYNC_END(pinfo->h_end));

	hdmi_write(HDMI_ACTIVE_VSYNC,
			HDMI_ACTIVE_VSYNC_START(pinfo->v_start) |
			HDMI_ACTIVE_VSYNC_END(pinfo->v_end));

	if (pinfo->interlace) {
		hdmi_write(HDMI_VSYNC_TOTAL_F2,
			HDMI_VSYNC_TOTAL_F2_V_TOTAL(pinfo->vtotal));
		hdmi_write(HDMI_VSYNC_ACTIVE_F2,
			HDMI_VSYNC_ACTIVE_F2_START(pinfo->v_start + 1) |
			HDMI_VSYNC_ACTIVE_F2_END(pinfo->v_end + 1));
	} else {
		hdmi_write(HDMI_VSYNC_TOTAL_F2,
			HDMI_VSYNC_TOTAL_F2_V_TOTAL(0));
		hdmi_write(HDMI_VSYNC_ACTIVE_F2,
			HDMI_VSYNC_ACTIVE_F2_START(0) |
			HDMI_VSYNC_ACTIVE_F2_END(0));
	}

	if (pinfo->v_active == 480) {
		if (pinfo->h_active_low)
			frame_ctrl |= HDMI_FRAME_CTRL_HSYNC_LOW;
		if (pinfo->v_active_low)
			frame_ctrl |= HDMI_FRAME_CTRL_VSYNC_LOW;
	}

	if (pinfo->interlace)
		frame_ctrl |= HDMI_FRAME_CTRL_INTERLACED_EN;

	HDMI_DEBUG("frame_ctrl=0x%08x", frame_ctrl);
	hdmi_write(HDMI_FRAME_CTRL, frame_ctrl);
}

static void _hdmi_panel_manage_deep_color(
		struct hdmi_panel_private *panel,
		bool deep_color_en)
{
	u32 hdmi_ctrl_reg, vbi_pkt_reg;

	HDMI_DEBUG("Deep Color: %s", deep_color_en ? "On" : "Off");

	if (deep_color_en) {
		hdmi_ctrl_reg = hdmi_read(HDMI_CTRL);

		/* GC CD override */
		hdmi_ctrl_reg |= BIT(27);

		/*
		 * enable deep color for RGB888/YUV444/YUV420 30 bits
		 *
		 * TODO: Enable color depth (CD) configuration for
		 * 1. YUV422 all bpp
		 * 2. RGB888/YUV444/YUV420 36 bits
		 */
		hdmi_ctrl_reg |= BIT(24);
		hdmi_write(HDMI_CTRL, hdmi_ctrl_reg);
		/* Enable GC_CONT and GC_SEND in General Control Packet
		 * (GCP) register so that deep color data is transmitted
		 * to the sink on every frame, allowing the sink to decode
		 * the data correctly.
		 *
		 * GC_CONT: 0x1 - Send GCP on every frame
		 * GC_SEND: 0x1 - Enable GCP Transmission
		 */
		vbi_pkt_reg = hdmi_read(HDMI_VBI_PKT_CTRL);
		vbi_pkt_reg |= BIT(5) | BIT(4);
		hdmi_write(HDMI_VBI_PKT_CTRL, vbi_pkt_reg);
	} else {
		hdmi_ctrl_reg = hdmi_read(HDMI_CTRL);

		/* disable GC CD override */
		hdmi_ctrl_reg &= ~BIT(27);

		/* enable deep color for RGB888/YUV444/YUV420 30 bits */
		hdmi_ctrl_reg &= ~BIT(24);
		hdmi_write(HDMI_CTRL, hdmi_ctrl_reg);

		/* disable the GC packet sending */
		vbi_pkt_reg = hdmi_read(HDMI_VBI_PKT_CTRL);
		vbi_pkt_reg &= ~(BIT(5) | BIT(4));
		hdmi_write(HDMI_VBI_PKT_CTRL, vbi_pkt_reg);
	}
}

static u8 _hdmi_panel_hdr_set_checksum(struct drm_msm_ext_hdr_metadata *meta)
{
	u8 i, checksum = 0;
	u8 *ptr, *buff;
	u32 length, size;
	u32 const type_code = 0x87;
	u32 const version = 0x01;
	u32 const descriptor_id = 0x00;

	/* length of metadata is 26 bytes. */
	length = 0x1a;
	/* add 4 bytes for the header. */
	size = length + HDMI_INFOFRAME_HEADER_SIZE;

	buff = kzalloc(size, GFP_KERNEL);

	if (!buff) {
		HDMI_ERR("invalid input");
		return checksum;
	}

	ptr = buff;

	buff[0] = type_code;
	buff[1] = version;
	buff[2] = length;
	buff[3] = 0;

	/* Start information payload */
	buff += HDMI_INFOFRAME_HEADER_SIZE;

	buff[0] = meta->eotf;
	buff[1] = descriptor_id;

	buff[2] = HDMI_GET_LSB(meta->display_primaries_x[0]);
	buff[3] = HDMI_GET_MSB(meta->display_primaries_x[0]);

	buff[4] = HDMI_GET_LSB(meta->display_primaries_x[1]);
	buff[5] = HDMI_GET_MSB(meta->display_primaries_x[1]);

	buff[6] = HDMI_GET_LSB(meta->display_primaries_x[2]);
	buff[7] = HDMI_GET_MSB(meta->display_primaries_x[2]);

	buff[8] = HDMI_GET_LSB(meta->display_primaries_y[0]);
	buff[9] = HDMI_GET_MSB(meta->display_primaries_y[0]);

	buff[10] = HDMI_GET_LSB(meta->display_primaries_y[1]);
	buff[11] = HDMI_GET_MSB(meta->display_primaries_y[1]);

	buff[12] = HDMI_GET_LSB(meta->display_primaries_y[2]);
	buff[13] = HDMI_GET_MSB(meta->display_primaries_y[2]);

	buff[14] = HDMI_GET_LSB(meta->white_point_x);
	buff[15] = HDMI_GET_MSB(meta->white_point_x);
	buff[16] = HDMI_GET_LSB(meta->white_point_y);
	buff[17] = HDMI_GET_MSB(meta->white_point_y);

	buff[18] = HDMI_GET_LSB(meta->max_luminance);
	buff[19] = HDMI_GET_MSB(meta->max_luminance);

	buff[20] = HDMI_GET_LSB(meta->min_luminance);
	buff[21] = HDMI_GET_MSB(meta->min_luminance);

	buff[22] = HDMI_GET_LSB(meta->max_content_light_level);
	buff[23] = HDMI_GET_MSB(meta->max_content_light_level);

	buff[24] = HDMI_GET_LSB(meta->max_average_light_level);
	buff[25] = HDMI_GET_MSB(meta->max_average_light_level);


	print_hex_dump_debug("[HDR InfoFrame]: ", DUMP_PREFIX_NONE,
			16, 16, buff, size, false);

	/* compute checksum */
	for (i = 0; i < size; i++)
		checksum += ptr[i];

	kfree(buff);

	return 256 - checksum;
}

static void _hdmi_panel_config_hdr_iframe(struct hdmi_panel_private *panel,
		struct drm_msm_ext_hdr_metadata *meta)
{
	u32 packet_payload = 0;
	u32 packet_header = 0;
	u32 packet_control = 0;
	u32 const type_code = 0x87;
	u32 const version = 0x01;
	u32 const length = 0x1a;
	u8 checksum;
	struct drm_connector *connector;

	connector = panel->connector;

	/* Setup the line number to send the packet on */
	packet_control = hdmi_read(HDMI_GEN_PKT_CTRL);
	packet_control |= BIT(16);
	hdmi_write(HDMI_GEN_PKT_CTRL, packet_control);

	/* Setup the packet to be sent every frame */
	packet_control = hdmi_read(HDMI_GEN_PKT_CTRL);
	packet_control |= BIT(1);
	hdmi_write(HDMI_GEN_PKT_CTRL, packet_control);

	/* Setup packet header and payload */
	packet_header = type_code
		| LEFT_SHIFT_BYTE(version)
		| LEFT_SHIFT_WORD(length);
	hdmi_write(HDMI_GENERIC0_HDR, packet_header);

	/**
	 * Checksum is not a mandatory field for
	 * the HDR infoframe as per CEA-861-3 specification.
	 * However, some HDMI Sinks still expect a valid
	 * checksum to be included as part of the infoframe.
	 * Hence, compute and add the checksum to improve
	 * sink interoperability for our HDR solution on HDMI.
	 */
	checksum = _hdmi_panel_hdr_set_checksum(meta);

	packet_payload = LEFT_SHIFT_BYTE(meta->eotf) | checksum;

	/*
	 * TODO: validate the presence of this variable inside connector.
	 * this variable "hdr_metadata_type_one" is missing in
	 * drm_connector data structure; therefore, the below commendted
	 * set of code requires modification.
	 *
	 * Reference - msm-v-4.4/hdmi_staging
	 */
	packet_payload =
	HDMI_GET_MSB(meta->display_primaries_x[0])
	| LEFT_SHIFT_BYTE(HDMI_GET_LSB(meta->display_primaries_y[0]))
	| LEFT_SHIFT_WORD(HDMI_GET_MSB(meta->display_primaries_y[0]))
	| LEFT_SHIFT_24BITS(HDMI_GET_LSB(meta->display_primaries_x[1]));
	hdmi_write(HDMI_GENERIC0(1), packet_payload);

	packet_payload =
	HDMI_GET_MSB(meta->display_primaries_x[1])
	| LEFT_SHIFT_BYTE(HDMI_GET_LSB(meta->display_primaries_y[1]))
	| LEFT_SHIFT_WORD(HDMI_GET_MSB(meta->display_primaries_y[1]))
	| LEFT_SHIFT_24BITS(HDMI_GET_LSB(meta->display_primaries_x[2]));
	hdmi_write(HDMI_GENERIC0(2), packet_payload);

	packet_payload =
	HDMI_GET_MSB(meta->display_primaries_x[2])
	| LEFT_SHIFT_BYTE(HDMI_GET_LSB(meta->display_primaries_y[2]))
	| LEFT_SHIFT_WORD(HDMI_GET_MSB(meta->display_primaries_y[2]))
	| LEFT_SHIFT_24BITS(HDMI_GET_LSB(meta->white_point_x));
	hdmi_write(HDMI_GENERIC0(3), packet_payload);

	packet_payload =
	HDMI_GET_MSB(meta->white_point_x)
	| LEFT_SHIFT_BYTE(HDMI_GET_LSB(meta->white_point_y))
	| LEFT_SHIFT_WORD(HDMI_GET_MSB(meta->white_point_y))
	| LEFT_SHIFT_24BITS(HDMI_GET_LSB(meta->max_luminance));
	hdmi_write(HDMI_GENERIC0(4), packet_payload);

	packet_payload =
	HDMI_GET_MSB(meta->max_luminance)
	| LEFT_SHIFT_BYTE(HDMI_GET_LSB(meta->min_luminance))
	| LEFT_SHIFT_WORD(HDMI_GET_MSB(meta->min_luminance))
	| LEFT_SHIFT_24BITS(HDMI_GET_LSB(meta->max_content_light_level));
	hdmi_write(HDMI_GENERIC0(5), packet_payload);

	packet_payload =
	HDMI_GET_MSB(meta->max_content_light_level)
	| LEFT_SHIFT_BYTE(HDMI_GET_LSB(meta->max_average_light_level))
	| LEFT_SHIFT_WORD(HDMI_GET_MSB(meta->max_average_light_level));
	hdmi_write(HDMI_GENERIC0(6), packet_payload);

}

static void _hdmi_panel_flush_hdr_iframe(struct hdmi_panel_private *panel)
{
	u32 packet_control;

	/* Flush the contents to the register. */
	packet_control = hdmi_read(HDMI_GEN_PKT_CTRL);
	packet_control |= BIT(2);
	hdmi_write(HDMI_GEN_PKT_CTRL, packet_control);

	/* Clear the flush bit of the register. */
	packet_control = hdmi_read(HDMI_GEN_PKT_CTRL);
	packet_control &= ~BIT(2);
	hdmi_write(HDMI_GEN_PKT_CTRL, packet_control);

	/* Start sending the packets. */
	packet_control = hdmi_read(HDMI_GEN_PKT_CTRL);
	packet_control |= BIT(0);
	hdmi_write(HDMI_GEN_PKT_CTRL, packet_control);

	HDMI_DEBUG("OK");
}

static void _hdmi_panel_manage_iframe(struct hdmi_panel_private *panel)
{
	u32 reg_val;
	/*
	 * VENSPEC_INFO_CONT | VENSPEC_INFO_SEND
	 * | AVI_INFO_CONT | AVI_INFO_SEND
	 * Setup HDMI TX VSIF and AVI IF packet control
	 * Enable this packet to transmit every frame
	 * Enable HDMI TX engine to transmit VS and AVI packet
	 */
	reg_val = hdmi_read(HDMI_INFOFRAME_CTRL0);
	reg_val |= (BIT(13) | BIT(12) | BIT(1) | BIT(0));
	hdmi_write(HDMI_INFOFRAME_CTRL0, reg_val);
	reg_val = hdmi_read(HDMI_INFOFRAME_CTRL1);
	reg_val &= ~(LEFT_SHIFT_24BITS(0x3F) | 0x3F);
	reg_val |= HDMI_AVI_IFRAME_LINE_NUMBER |
		LEFT_SHIFT_24BITS(HDMI_VENDOR_IFRAME_LINE_NUMBER);
	hdmi_write(HDMI_INFOFRAME_CTRL1, reg_val);
	/*
	 * GENERIC1_LINE | GENERIC1_CONT | GENERIC1_SEND
	 * Setup HDMI TX generic packet control
	 * Enable this packet to transmit every frame
	 * Enable HDMI TX engine to transmit Generic packet 1
	 */
	reg_val = hdmi_read(HDMI_GEN_PKT_CTRL);
	reg_val |= (LEFT_SHIFT_24BITS(0x1) | (1 << 5) | (1 << 4));
	hdmi_write(HDMI_GEN_PKT_CTRL, reg_val);
}

static void _hdmi_panel_manage_ctrl(struct hdmi_panel_private *panel, bool en)
{
	u32 reg_val;

	/* If enable == true; then enable
	 * 1. HDMI CTRL
	 * 2. AVI IF
	 * 3. VS IF
	 * 4. SPD IF
	 */

	reg_val = hdmi_read(HDMI_CTRL);

	if (en) {
		/* Enable HDMI TX controller */
		reg_val |= HDMI_CTRL_ENABLE;

		if (!panel->is_hdmi_mode) {
			/* TODO: Understand why are we setting HDMI_CTRL_HDMI */
			reg_val |= HDMI_CTRL_HDMI;
			hdmi_write(HDMI_CTRL, reg_val);
			reg_val &= ~HDMI_CTRL_HDMI;
		} else {
			_hdmi_panel_manage_iframe(panel);
			reg_val |= HDMI_CTRL_HDMI;
		}
	} else {
		/* TODO: recheck the below step and confirm what is the
		 * correct action/bit to be set.
		 * HDMI_CTRL_ENABLE or HDMI_CTRL_HDMI
		 */
		reg_val &= ~HDMI_CTRL_ENABLE;
	}
	hdmi_write(HDMI_CTRL, reg_val);
	HDMI_DEBUG("HDMI Core: %s, HDMI_CTRL=0x%08x\n",
			en ? "Enable" : "Disable", reg_val);
}

static bool hdmi_panel_check_mode_hdmi(struct hdmi_panel_info *pinfo)
{
	if (pinfo->h_active == 640 &&
		pinfo->v_active == 480)
		return false;
	return true;
}

static int hdmi_panel_get_modes(struct hdmi_panel *hdmi_panel,
		struct drm_connector *connector)
{
	int rc = 0;

	if (!hdmi_panel || !connector) {
		HDMI_ERR("invalid input");
		return -EINVAL;
	}

	if (hdmi_panel->edid_ctrl && hdmi_panel->edid_ctrl->edid) {
		rc = _sde_edid_update_modes(connector,
					hdmi_panel->edid_ctrl);
	}

	return rc;

}

static void hdmi_panel_set_avi_infoframe(
		struct hdmi_panel_private *panel,
		const struct drm_display_mode *mode)
{
	struct drm_connector *connector;
	union hdmi_infoframe frame;
	struct drm_connector_state conn_state = {0};
	u8 buffer[HDMI_INFOFRAME_SIZE(AVI)] = {0};

	int ret;

	if (!mode || !panel) {
		HDMI_ERR("invalid input");
		return;
	}

	connector = panel->connector;
	conn_state.colorspace = connector->state->colorspace;

	ret = drm_hdmi_avi_infoframe_from_display_mode(&frame.avi,
						connector, mode);
	if (ret < 0) {
		HDMI_ERR("HDMI AVI Infoframe configuration failed: %i", ret);
		return;
	}

	hdmi_panel_avi_infoframe_bars(&frame.avi, &conn_state);
	hdmi_panel_avi_infoframe_colorimetry(&frame.avi, &conn_state);
	drm_hdmi_avi_infoframe_quant_range(&frame.avi, connector, mode,
				HDMI_QUANTIZATION_RANGE_LIMITED);

	ret = hdmi_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (ret < 0) {
		HDMI_ERR("failed to pack AVI infoframe: %i", ret);
		return;
	}

	print_hex_dump_debug("[AVI InfoFrame]: ", DUMP_PREFIX_NONE,
			16, 16, buffer, sizeof(buffer), false);

	_hdmi_panel_config_avi_iframe(panel, buffer);

	HDMI_DEBUG("OK");
}

static void hdmi_panel_set_vs_infoframe(
		struct hdmi_panel_private *panel,
		const struct drm_display_mode *mode)
{
	int ret;
	struct hdmi_vendor_infoframe frame;
	u8 buffer[HDMI_INFOFRAME_SIZE(VENDOR)] = {0};
	struct drm_connector *connector;

	if (!panel || !mode) {
		HDMI_ERR("invalid input");
		return;
	}

	connector =  panel->connector;

	ret = drm_hdmi_vendor_infoframe_from_display_mode(
			&frame, connector, mode);
	if (ret < 0) {
		HDMI_ERR("HDMI VENDOR Infoframe configuration failed: %i", ret);
		return;
	}

	ret = hdmi_vendor_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (ret < 0) {
		HDMI_ERR("failed to pack Vendor infoframe: %i", ret);
		return;
	}

	print_hex_dump_debug("[VENDOR InfoFrame]: ", DUMP_PREFIX_NONE,
			16, 16, buffer, sizeof(buffer), false);

	_hdmi_panel_config_vs_iframe(panel, &frame, buffer);

	HDMI_DEBUG("OK");
}

static void hdmi_panel_set_spd_infoframe(struct hdmi_panel_private *panel)
{
	u8 buffer[HDMI_INFOFRAME_SIZE(SPD)] = {0};
	struct hdmi_spd_infoframe frame;
	int ret;

	if (!panel) {
		HDMI_ERR("invalid input");
		return;
	}

	ret = hdmi_spd_infoframe_init(&frame,
			HDMI_DEFAULT_VENDOR_NAME,
			HDMI_DEFAULT_PRODUCT_NAME);
	if (ret) {
		HDMI_ERR("HDMI SPD Infoframe configuration failed: %i", ret);
		return;
	}

	ret = hdmi_spd_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (ret < 0) {
		HDMI_ERR("failed to pack SPD infoframe: %i", ret);
		return;
	}

	print_hex_dump_debug("[SPD InfoFrame]: ", DUMP_PREFIX_NONE,
			16, 16, buffer, sizeof(buffer), false);

	_hdmi_panel_config_spd_iframe(panel, buffer);

	HDMI_DEBUG("OK");
}

static int hdmi_panel_setup_hdr(struct hdmi_panel *hdmi_panel,
		struct drm_msm_ext_hdr_metadata *hdr_meta,
		bool dhdr_update, u64 core_clk_rate, bool flush)
{
	int rc = 0;
	struct hdmi_panel_private *panel;

	if (!hdmi_panel || !hdr_meta) {
		HDMI_ERR("invalid input");
		return -EINVAL;
	}

	panel = container_of(hdmi_panel, struct hdmi_panel_private, hdmi_panel);

	panel->hdr_state = hdr_meta->hdr_state;

	_hdmi_panel_config_hdr_iframe(panel, hdr_meta);

	if (flush)
		_hdmi_panel_flush_hdr_iframe(panel);

	return rc;
}

static void hdmi_panel_save_mode(struct hdmi_panel_info *pinfo,
		const struct drm_display_mode *mode)
{
	struct hdmi_panel *hdmi_panel;
	const u32 num_components = 24;

	hdmi_panel = container_of(pinfo, struct hdmi_panel, pinfo);
	/* TODO: Confirm whether mode copy is correct. */
	pinfo->htotal = mode->htotal;
	pinfo->vtotal = mode->vtotal;

	pinfo->h_active = mode->hdisplay;
	pinfo->h_skew = mode->hskew;

	pinfo->h_front_porch =
		mode->hsync_start - mode->hdisplay;
	pinfo->h_back_porch =
		mode->htotal - mode->hsync_end;

	pinfo->h_start = mode->htotal - mode->hsync_start;
	pinfo->h_end = mode->htotal - mode->hsync_start + mode->hdisplay;

	pinfo->h_sync_width = pinfo->htotal - (pinfo->h_active +
			pinfo->h_front_porch + pinfo->h_back_porch);

	pinfo->v_active = mode->vdisplay;

	pinfo->v_front_porch =
		mode->vsync_start - mode->vdisplay;
	pinfo->v_back_porch =
		mode->vtotal - mode->vsync_end;

	pinfo->v_start = mode->vtotal - mode->vsync_start - 1;
	pinfo->v_end = mode->vtotal - mode->vsync_start + mode->vdisplay - 1;

	pinfo->v_sync_width = pinfo->vtotal - (pinfo->v_active +
			pinfo->v_front_porch + pinfo->v_back_porch);

	pinfo->refresh_rate = drm_mode_vrefresh(mode);
	pinfo->pixel_clk_khz = mode->clock;

	pinfo->v_active_low =
		!!(mode->flags & DRM_MODE_FLAG_NVSYNC);

	pinfo->h_active_low =
		!!(mode->flags & DRM_MODE_FLAG_NHSYNC);

	pinfo->interlace =
		!!(mode->flags & DRM_MODE_FLAG_INTERLACE);

	pinfo->bpp =
		hdmi_panel->connector->display_info.bpc * num_components;
}

static void hdmi_panel_resolution_info(struct hdmi_panel_private *panel)
{
	struct hdmi_panel_info *pinfo = &panel->hdmi_panel.pinfo;

	/*
	 * print resolution info as this is a result
	 * of the user initiated action of cable connection.
	 */
	HDMI_INFO("HDMI RESOLUTION: active(back|front|width|low)");
	HDMI_INFO("%i(%i|%i|%i|%i)x%i(%i|%i|%i|%i)@%ifps %ibpp %iKHz",
		pinfo->h_active, pinfo->h_back_porch, pinfo->h_front_porch,
		pinfo->h_sync_width, pinfo->h_active_low,
		pinfo->v_active, pinfo->v_back_porch, pinfo->v_front_porch,
		pinfo->v_sync_width, pinfo->v_active_low,
		pinfo->refresh_rate, pinfo->bpp, pinfo->pixel_clk_khz);
}

static enum drm_mode_status hdmi_panel_validate_mode(
		struct hdmi_panel *hdmi_panel,
		const struct drm_display_mode *mode)
{
	/*
	 * TODO: Confirm the mode validation criteria
	 * 1. Reject all modes with TMDS greater than Max TMDS
	 * 2. Reject all Interlace modes
	 * 3. Allow any other modes.
	 */

	struct drm_display_info *info;
	enum drm_mode_status mode_status = MODE_BAD;

	if (!hdmi_panel || !mode) {
		HDMI_ERR("invalid input");
		return mode_status;
	}

	info = &hdmi_panel->connector->display_info;

	if ((mode->clock <= info->max_tmds_clock)
		&& !(mode->flags & DRM_MODE_FLAG_INTERLACE))
		mode_status = MODE_OK;

	HDMI_DEBUG("[%s] mode is %s", mode->name,
			(mode_status == MODE_OK) ?
			"valid" : "invalid");

	return mode_status;
}

static int hdmi_panel_set_mode(struct hdmi_panel *hdmi_panel,
		const struct drm_display_mode *mode)
{
	struct hdmi_panel_private *panel;
	struct hdmi_panel_info *pinfo;

	if (!hdmi_panel || !mode) {
		HDMI_ERR("invalid input");
		return -EINVAL;
	}

	pinfo = &hdmi_panel->pinfo;
	panel = container_of(hdmi_panel, struct hdmi_panel_private, hdmi_panel);

	hdmi_panel_save_mode(pinfo, mode);
	_hdmi_panel_config_mode(panel);

	panel->is_hdmi_mode = hdmi_panel_check_mode_hdmi(pinfo);
	if (panel->is_hdmi_mode) {
		hdmi_panel_set_avi_infoframe(panel, mode);
		hdmi_panel_set_vs_infoframe(panel, mode);
		hdmi_panel_set_spd_infoframe(panel);
	}

	return 0;
}

static int hdmi_panel_enable(struct hdmi_panel *hdmi_panel)
{
	struct hdmi_panel_private *panel;

	if (!hdmi_panel) {
		HDMI_ERR("invalid input");
		return -EINVAL;
	}

	/*
	 * TODO: Ensure all the reference timers
	 * are configured before enabling the HDMI
	 * TX Controlloer.
	 */
	panel = container_of(hdmi_panel, struct hdmi_panel_private, hdmi_panel);

	_hdmi_panel_manage_deep_color(panel, hdmi_panel->dc_enable);
	_hdmi_panel_manage_ctrl(panel, true);
	hdmi_panel_resolution_info(panel);

	/*
	 * TODO: Ensure all the reference timers
	 * are enabled after enabling the HDMI
	 * TX Controller.
	 */

	return 0;
}

static int hdmi_panel_disable(struct hdmi_panel *hdmi_panel)
{
	struct hdmi_panel_private *panel;

	if (!hdmi_panel) {
		HDMI_ERR("invalid input");
		return -EINVAL;
	}

	panel = container_of(hdmi_panel, struct hdmi_panel_private, hdmi_panel);

	_hdmi_panel_manage_ctrl(panel, false);

	/*
	 * TODO: Ensure all the reference timers are disabled
	 * after disabling the HDMI TX Controller.
	 */
	return 0;
}

struct hdmi_panel *hdmi_panel_get(struct device *dev,
		struct drm_connector *connector, struct hdmi_parser *parser)
{
	struct hdmi_panel_private *panel;
	struct hdmi_panel *hdmi_panel;
	struct sde_connector *sde_conn;

	if (!dev || !connector || !parser) {
		HDMI_ERR("invalid inputs");
		return ERR_PTR(-EINVAL);
	}

	panel = kzalloc(sizeof(*panel), GFP_KERNEL);
	if (!panel) {
		HDMI_ERR("Insufficient memory");
		return ERR_PTR(-ENOMEM);
	}

	panel->dev = dev;
	panel->connector = connector;
	hdmi_panel = &panel->hdmi_panel;
	hdmi_panel->connector = connector;

	sde_conn = to_sde_connector(connector);
	sde_conn->drv_panel = hdmi_panel;

	panel->io_data = parser->get_io(parser, "hdmi_ctrl");

	hdmi_panel->set_mode	= hdmi_panel_set_mode;
	hdmi_panel->get_modes	= hdmi_panel_get_modes;
	hdmi_panel->enable	= hdmi_panel_enable;
	hdmi_panel->disable	= hdmi_panel_disable;
	hdmi_panel->setup_hdr	= hdmi_panel_setup_hdr;
	hdmi_panel->update_pps	= hdmi_panel_update_pps;
	hdmi_panel->get_panel_on = hdmi_pabel_get_panel_on;
	hdmi_panel->validate_mode = hdmi_panel_validate_mode;
	hdmi_panel->set_colorspace = hdmi_panel_set_colorspace;
	hdmi_panel->pclk_factor = 1;

	return hdmi_panel;
}

void  hdmi_panel_put(struct hdmi_panel *hdmi_panel)
{
	struct hdmi_panel_private *panel;

	if (!hdmi_panel)
		return;

	panel = container_of(hdmi_panel, struct hdmi_panel_private, hdmi_panel);

	kfree(panel);
}
