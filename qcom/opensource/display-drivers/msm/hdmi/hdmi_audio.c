// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/of_platform.h>
#include <linux/version.h>
#include <msm_ext_display.h>
#include <linux/gcd.h>

#include "hdmi_audio.h"
#include "hdmi_panel.h"
#include "hdmi_debug.h"
#include "msm_drv.h"
#include "hdmi_display.h"
#include "sde_edid_parser.h"
#include "hdmi_regs.h"

#define HDMI_AUDIO_INFO_FRAME_PACKET_HEADER 0x84
#define HDMI_AUDIO_INFO_FRAME_PACKET_VERSION 0x1
#define HDMI_AUDIO_INFO_FRAME_PACKET_LENGTH 0x0A

#define HDMI_ACR_N_MULTIPLIER 128
#define DEFAULT_AUDIO_SAMPLE_RATE_HZ 48000

static inline uint32_t HDMI_AUDIO_CFG_FIFO_WATERMARK(uint32_t val)
{
	return ((val) << HDMI_AUDIO_CFG_FIFO_WATERMARK__SHIFT) &
		HDMI_AUDIO_CFG_FIFO_WATERMARK__MASK;
}

#define hdmi_read(x, off) \
	readl_relaxed((x)->io_data->io.base + (off))

#define hdmi_write(x, off, value)  \
	writel_relaxed(value, (x)->io_data->io.base + (off))

struct hdmi_audio_acr {
	u32 n;
	u32 cts;
};

/* Supported HDMI Audio channels */
enum hdmi_audio_channels {
	AUDIO_CHANNEL_2 = 2,
	AUDIO_CHANNEL_3,
	AUDIO_CHANNEL_4,
	AUDIO_CHANNEL_5,
	AUDIO_CHANNEL_6,
	AUDIO_CHANNEL_7,
	AUDIO_CHANNEL_8,
};

enum hdmi_audio_sample_rates {
	AUDIO_SAMPLE_RATE_32KHZ,
	AUDIO_SAMPLE_RATE_44_1KHZ,
	AUDIO_SAMPLE_RATE_48KHZ,
	AUDIO_SAMPLE_RATE_88_2KHZ,
	AUDIO_SAMPLE_RATE_96KHZ,
	AUDIO_SAMPLE_RATE_176_4KHZ,
	AUDIO_SAMPLE_RATE_192KHZ,
	AUDIO_SAMPLE_RATE_MAX
};

struct hdmi_audio_private {
	struct platform_device *ext_pdev;
	struct platform_device *pdev;
	struct msm_ext_disp_init_data ext_audio_data;
	struct hdmi_panel *panel;
	struct msm_ext_disp_audio_setup_params *params;
	struct hdmi_io_data *io_data;

	bool ack_enabled;
	atomic_t session_on;
	bool engine_on;

	u32 channels;
	u32 max_pclk_khz;

	struct completion hpd_comp;
	struct workqueue_struct *notify_workqueue;
	struct delayed_work notify_delayed_work;
	struct mutex ops_lock;

	struct hdmi_audio hdmi_audio;

	atomic_t acked;
};

static void _hdmi_audio_infoframe_setup(struct hdmi_audio_private *audio,
	bool enabled)
{
	u32 channels, channel_allocation, level_shift, down_mix, layout;
	u32 hdmi_debug_reg = 0, audio_info_0_reg = 0, audio_info_1_reg = 0;
	u32 audio_info_ctrl_reg, aud_pck_ctrl_2_reg;
	u32 check_sum, sample_present;

	audio_info_ctrl_reg = hdmi_read(audio, HDMI_INFOFRAME_CTRL0);
	audio_info_ctrl_reg &= ~0xF0;

	if (!enabled)
		goto end;

	channels           = audio->params->num_of_channels - 1;
	channel_allocation = audio->params->channel_allocation;
	level_shift        = audio->params->level_shift;
	down_mix           = audio->params->down_mix;
	sample_present     = audio->params->sample_present;

	layout = (audio->params->num_of_channels == AUDIO_CHANNEL_2) ? 0 : 1;
	aud_pck_ctrl_2_reg = BIT(0) | (layout << 1);
	hdmi_write(audio, HDMI_AUDIO_PKT_CTRL2, aud_pck_ctrl_2_reg);

	audio_info_1_reg |= channel_allocation & 0xFF;
	audio_info_1_reg |= ((level_shift & 0xF) << 11);
	audio_info_1_reg |= ((down_mix & 0x1) << 15);

	check_sum = 0;
	check_sum += HDMI_AUDIO_INFO_FRAME_PACKET_HEADER;
	check_sum += HDMI_AUDIO_INFO_FRAME_PACKET_VERSION;
	check_sum += HDMI_AUDIO_INFO_FRAME_PACKET_LENGTH;
	check_sum += channels;
	check_sum += channel_allocation;
	check_sum += (level_shift & 0xF) << 3 | (down_mix & 0x1) << 7;
	check_sum &= 0xFF;
	check_sum = (u8) (256 - check_sum);

	audio_info_0_reg |= check_sum & 0xFF;
	audio_info_0_reg |= ((channels & 0x7) << 8);

	/* Enable Audio InfoFrame Transmission */
	audio_info_ctrl_reg |= 0xF0;

	if (layout) {
		/* Set the Layout bit */
		hdmi_debug_reg |= BIT(4);

		/* Set the Sample Present bits */
		hdmi_debug_reg |= sample_present & 0xF;
	}
end:
	hdmi_write(audio, HDMI_DEBUG_REG, hdmi_debug_reg);
	hdmi_write(audio, HDMI_AUDIO_INFO0, audio_info_0_reg);
	hdmi_write(audio, HDMI_AUDIO_INFO1, audio_info_1_reg);
	hdmi_write(audio, HDMI_INFOFRAME_CTRL0, audio_info_ctrl_reg);
}

static void _hdmi_audio_get_acr_param(u32 pclk, u32 fs,
	struct hdmi_audio_acr *acr)
{
	u32 div, mul;

	if (!acr) {
		HDMI_ERR("invalid data\n");
		return;
	}

	/*
	 * as per HDMI specification, N/CTS = (128*fs)/pclk.
	 * get the ratio using this formula.
	 */
	acr->n = HDMI_ACR_N_MULTIPLIER * fs;
	acr->cts = pclk;

	/* get the greatest common divisor for the ratio */
	div = gcd(acr->n, acr->cts);

	/* get the n and cts values wrt N/CTS formula */
	acr->n /= div;
	acr->cts /= div;

	/*
	 * as per HDMI specification, 300 <= 128*fs/N <= 1500
	 * with a target of 128*fs/N = 1000. To get closest
	 * value without truncating fractional values, find
	 * the corresponding multiplier
	 */
	mul = ((HDMI_ACR_N_MULTIPLIER * fs / HDMI_KHZ_TO_HZ)
		+ (acr->n - 1)) / acr->n;

	acr->n *= mul;
	acr->cts *= mul;
}

static void _hdmi_audio_get_audio_sample_rate(u32 *sample_rate_hz)
{
	u32 rate = *sample_rate_hz;

	switch (rate) {
	case 32000:
		*sample_rate_hz = AUDIO_SAMPLE_RATE_32KHZ;
		break;
	case 44100:
		*sample_rate_hz = AUDIO_SAMPLE_RATE_44_1KHZ;
		break;
	case 48000:
		*sample_rate_hz = AUDIO_SAMPLE_RATE_48KHZ;
		break;
	case 88200:
		*sample_rate_hz = AUDIO_SAMPLE_RATE_88_2KHZ;
		break;
	case 96000:
		*sample_rate_hz = AUDIO_SAMPLE_RATE_96KHZ;
		break;
	case 176400:
		*sample_rate_hz = AUDIO_SAMPLE_RATE_176_4KHZ;
		break;
	case 192000:
		*sample_rate_hz = AUDIO_SAMPLE_RATE_192KHZ;
		break;
	default:
		HDMI_ERR("%d unchanged\n", rate);
		break;
	}
}

static void _hdmi_audio_acr_enable(struct hdmi_audio_private *audio)
{
	struct hdmi_audio_acr acr;
	struct msm_ext_disp_audio_setup_params *params;
	u32 pclk, layout, multiplier = 1, sample_rate;
	u32 acr_pkt_ctl, aud_pkt_ctl2, acr_reg_cts, acr_reg_n;

	params = audio->params;
	pclk = audio->hdmi_audio.pixclk;
	sample_rate = params->sample_rate_hz;

	_hdmi_audio_get_acr_param(pclk, sample_rate, &acr);
	_hdmi_audio_get_audio_sample_rate(&sample_rate);

	layout = (params->num_of_channels == AUDIO_CHANNEL_2) ? 0 : 1;

	HDMI_DEBUG("n=%u, cts=%u, layout=%u\n", acr.n, acr.cts, layout);

	/* AUDIO_PRIORITY | SOURCE */
	acr_pkt_ctl = BIT(31) | BIT(8);

	switch (sample_rate) {
	case AUDIO_SAMPLE_RATE_44_1KHZ:
		acr_pkt_ctl |= 0x2 << 4;
		acr.cts <<= 12;

		acr_reg_cts = HDMI_ACR_44_0;
		acr_reg_n = HDMI_ACR_44_1;
		break;
	case AUDIO_SAMPLE_RATE_48KHZ:
		acr_pkt_ctl |= 0x3 << 4;
		acr.cts <<= 12;

		acr_reg_cts = HDMI_ACR_48_0;
		acr_reg_n = HDMI_ACR_48_1;
		break;
	case AUDIO_SAMPLE_RATE_192KHZ:
		multiplier = 4;
		acr.n >>= 2;

		acr_pkt_ctl |= 0x3 << 4;
		acr.cts <<= 12;

		acr_reg_cts = HDMI_ACR_48_0;
		acr_reg_n = HDMI_ACR_48_1;
		break;
	case AUDIO_SAMPLE_RATE_176_4KHZ:
		multiplier = 4;
		acr.n >>= 2;

		acr_pkt_ctl |= 0x2 << 4;
		acr.cts <<= 12;

		acr_reg_cts = HDMI_ACR_44_0;
		acr_reg_n = HDMI_ACR_44_1;
		break;
	case AUDIO_SAMPLE_RATE_96KHZ:
		multiplier = 2;
		acr.n >>= 1;

		acr_pkt_ctl |= 0x3 << 4;
		acr.cts <<= 12;

		acr_reg_cts = HDMI_ACR_48_0;
		acr_reg_n = HDMI_ACR_48_1;
		break;
	case AUDIO_SAMPLE_RATE_88_2KHZ:
		multiplier = 2;
		acr.n >>= 1;

		acr_pkt_ctl |= 0x2 << 4;
		acr.cts <<= 12;

		acr_reg_cts = HDMI_ACR_44_0;
		acr_reg_n = HDMI_ACR_44_1;
		break;
	default:
		multiplier = 1;

		acr_pkt_ctl |= 0x1 << 4;
		acr.cts <<= 12;

		acr_reg_cts = HDMI_ACR_32_0;
		acr_reg_n = HDMI_ACR_32_1;
		break;
	}

	aud_pkt_ctl2 = BIT(0) | (layout << 1);

	/* N_MULTIPLE(multiplier) */
	acr_pkt_ctl &= ~(7 << 16);
	acr_pkt_ctl |= (multiplier & 0x7) << 16;

	/* SEND | CONT */
	acr_pkt_ctl |= BIT(0) | BIT(1);

	hdmi_write(audio, acr_reg_cts, acr.cts);
	hdmi_write(audio, acr_reg_n, acr.n);
	hdmi_write(audio, HDMI_ACR_PKT_CTRL, acr_pkt_ctl);
	hdmi_write(audio, HDMI_AUDIO_PKT_CTRL2, aud_pkt_ctl2);
}

static void _hdmi_audio_setup_acr(struct hdmi_audio_private *audio, bool on)
{
	if (on)
		_hdmi_audio_acr_enable(audio);
	else
		hdmi_write(audio, HDMI_ACR_PKT_CTRL, 0);
}


static void hdmi_audio_enable(struct hdmi_audio_private *audio, bool enable)
{
	uint32_t audio_config, aud_pkt_ctrl, vbi_pkt_ctrl, infofrm_ctrl,
		 hdmi_aud_init;

	vbi_pkt_ctrl = hdmi_read(audio, HDMI_VBI_PKT_CTRL);
	aud_pkt_ctrl = hdmi_read(audio, HDMI_AUDIO_PKT_CTRL1);
	infofrm_ctrl = hdmi_read(audio, HDMI_INFOFRAME_CTRL0);
	audio_config = hdmi_read(audio, HDMI_AUDIO_CFG);
	hdmi_aud_init = hdmi_read(audio, HDMI_AUD_INT);

	if (enable) {
		/**
		 * The GCP must also be programmed to be sent every frame
		 * through the VBI_PKT_CTRL.
		 */
		hdmi_write(audio, HDMI_GC, 0x0);
		vbi_pkt_ctrl |= HDMI_VBI_PKT_CTRL_GC_ENABLE;
		vbi_pkt_ctrl |= HDMI_VBI_PKT_CTRL_GC_EVERY_FRAME;
		vbi_pkt_ctrl |= HDMI_VBI_PKT_CTRL_ISRC_SEND;
		vbi_pkt_ctrl |= HDMI_VBI_PKT_CTRL_ISRC_CONTINUOUS;
		vbi_pkt_ctrl |= HDMI_VBI_PKT_CTRL_ACP_SEND;

		/**
		 * audio engine enable initiates the fetch of data from
		 * the LPASS module.
		 */
		aud_pkt_ctrl |= HDMI_AUDIO_PKT_CTRL1_AUDIO_SAMPLE_SEND;
		/**
		 * Audio InfoFrame Packet HPG
		 */
		infofrm_ctrl |= HDMI_INFOFRAME_CTRL0_AUDIO_INFO_SEND;
		infofrm_ctrl |= HDMI_INFOFRAME_CTRL0_AUDIO_INFO_CONT;
		infofrm_ctrl |= HDMI_INFOFRAME_CTRL0_AUDIO_INFO_SOURCE;
		infofrm_ctrl |= HDMI_INFOFRAME_CTRL0_AUDIO_INFO_UPDATE;

		/**
		 * All audio related settings must be configured prior
		 * to setting HDMI_AUDIO_CFG:ENABLE.
		 */
		audio_config &= ~HDMI_AUDIO_CFG_FIFO_WATERMARK__MASK;
		audio_config |= HDMI_AUDIO_CFG_FIFO_WATERMARK(4);
		audio_config |= HDMI_AUDIO_CFG_ENGINE_ENABLE;
	} else {
		vbi_pkt_ctrl &= ~HDMI_VBI_PKT_CTRL_GC_ENABLE;
		vbi_pkt_ctrl &= ~HDMI_VBI_PKT_CTRL_GC_EVERY_FRAME;
		vbi_pkt_ctrl &= ~HDMI_VBI_PKT_CTRL_ISRC_SEND;
		vbi_pkt_ctrl &= ~HDMI_VBI_PKT_CTRL_ISRC_CONTINUOUS;
		vbi_pkt_ctrl &= ~HDMI_VBI_PKT_CTRL_ACP_SEND;
		aud_pkt_ctrl &= ~HDMI_AUDIO_PKT_CTRL1_AUDIO_SAMPLE_SEND;
		infofrm_ctrl &= ~HDMI_INFOFRAME_CTRL0_AUDIO_INFO_SEND;
		infofrm_ctrl &= ~HDMI_INFOFRAME_CTRL0_AUDIO_INFO_CONT;
		infofrm_ctrl &= ~HDMI_INFOFRAME_CTRL0_AUDIO_INFO_SOURCE;
		infofrm_ctrl &= ~HDMI_INFOFRAME_CTRL0_AUDIO_INFO_UPDATE;
		audio_config &= ~HDMI_AUDIO_CFG_ENGINE_ENABLE;
	}

	hdmi_write(audio, HDMI_VBI_PKT_CTRL, vbi_pkt_ctrl);
	hdmi_write(audio, HDMI_AUDIO_PKT_CTRL1, aud_pkt_ctrl);
	hdmi_write(audio, HDMI_INFOFRAME_CTRL0, infofrm_ctrl);

	hdmi_write(audio, HDMI_AUD_INT,
			COND(enable, HDMI_AUD_INT_AUD_FIFO_URUN_INT) |
			COND(enable, HDMI_AUD_INT_AUD_SAM_DROP_INT));

	hdmi_write(audio, HDMI_AUDIO_CFG, audio_config);
	audio->engine_on = enable;

	if (!atomic_read(&audio->session_on))
		HDMI_WARN("session inactive. enable=%d\n", enable);
}

static struct hdmi_audio_private *hdmi_audio_get_data(
				struct platform_device *pdev)
{
	struct msm_ext_disp_data *ext_data;
	struct hdmi_audio *hdmi_audio;

	if (!pdev) {
		HDMI_ERR("invalid input\n");
		return ERR_PTR(-ENODEV);
	}

	ext_data = platform_get_drvdata(pdev);
	if (!ext_data) {
		HDMI_ERR("invalid ext disp data\n");
		return ERR_PTR(-EINVAL);
	}

	hdmi_audio = ext_data->intf_data;
	if (!hdmi_audio) {
		HDMI_ERR("invalid intf data\n");
		return ERR_PTR(-EINVAL);
	}

	return container_of(hdmi_audio, struct hdmi_audio_private, hdmi_audio);
}

static int hdmi_audio_info_setup(struct platform_device *pdev,
	struct msm_ext_disp_audio_setup_params *params)
{
	int rc = 0;
	struct hdmi_audio_private *audio;

	if (IS_ERR(pdev)) {
		rc = PTR_ERR(pdev);
		return rc;
	}

	if (IS_ERR(params)) {
		rc = PTR_ERR(params);
		return rc;
	}

	audio = hdmi_audio_get_data(pdev);
	if (IS_ERR(audio)) {
		rc = PTR_ERR(audio);
		return rc;
	}

	if (audio->hdmi_audio.tui_active) {
		HDMI_DEBUG("TUI session active\n");
		return rc;
	}

	mutex_lock(&audio->ops_lock);

	audio->channels = params->num_of_channels;
	audio->hdmi_audio.pixclk = audio->hdmi_audio.display->pixclk;
	audio->params = params;

	_hdmi_audio_setup_acr(audio, true);
	_hdmi_audio_infoframe_setup(audio, true);
	hdmi_audio_enable(audio, true);

	mutex_unlock(&audio->ops_lock);

	HDMI_DEBUG("audio stream configured\n");

	return rc;
}

static int hdmi_audio_get_edid_blk(struct platform_device *pdev,
		struct msm_ext_disp_audio_edid_blk *blk)
{
	int rc = 0;
	struct hdmi_audio_private *audio;
	struct sde_edid_ctrl *edid;

	if (!blk) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	audio = hdmi_audio_get_data(pdev);
	if (IS_ERR(audio)) {
		rc = PTR_ERR(audio);
		goto end;
	}

	if (!audio->panel || !audio->panel->edid_ctrl) {
		HDMI_ERR("invalid panel data\n");
		rc = -EINVAL;
		goto end;
	}

	edid = audio->panel->edid_ctrl;

	blk->audio_data_blk = edid->audio_data_block;
	blk->audio_data_blk_size = edid->adb_size;

	blk->spk_alloc_data_blk = edid->spkr_alloc_data_block;
	blk->spk_alloc_data_blk_size = edid->sadb_size;
end:
	return rc;
}

static int hdmi_audio_get_cable_status(struct platform_device *pdev, u32 vote)
{
	int rc = 0;
	struct hdmi_audio_private *audio;

	audio = hdmi_audio_get_data(pdev);
	if (IS_ERR(audio)) {
		rc = PTR_ERR(audio);
		goto end;
	}

	return atomic_read(&audio->session_on);
end:
	return rc;
}

static int hdmi_audio_get_intf_id(struct platform_device *pdev)
{
	int rc = 0;
	struct hdmi_audio_private *audio;

	audio = hdmi_audio_get_data(pdev);
	if (IS_ERR(audio)) {
		rc = PTR_ERR(audio);
		goto end;
	}

	return EXT_DISPLAY_TYPE_HDMI;
end:
	return rc;
}

static void hdmi_audio_teardown_done(struct platform_device *pdev)
{
	struct hdmi_audio_private *audio;

	audio = hdmi_audio_get_data(pdev);
	if (IS_ERR(audio))
		return;

	if (audio->hdmi_audio.tui_active) {
		HDMI_DEBUG("TUI session active\n");
		return;
	}

	mutex_lock(&audio->ops_lock);
	hdmi_audio_enable(audio, false);
	mutex_unlock(&audio->ops_lock);

	atomic_set(&audio->acked, 1);
	complete_all(&audio->hpd_comp);

	HDMI_DEBUG("audio engine disabled\n");
}

static int hdmi_audio_ack_done(struct platform_device *pdev, u32 ack)
{
	int rc = 0, ack_hpd;
	struct hdmi_audio_private *audio;

	audio = hdmi_audio_get_data(pdev);
	if (IS_ERR(audio)) {
		rc = PTR_ERR(audio);
		goto end;
	}

	if (ack & AUDIO_ACK_SET_ENABLE) {
		audio->ack_enabled = ack & AUDIO_ACK_ENABLE ?
			true : false;

		HDMI_DEBUG("audio ack feature %s\n",
			audio->ack_enabled ? "enabled" : "disabled");
		goto end;
	}

	if (!audio->ack_enabled)
		goto end;

	ack_hpd = ack & AUDIO_ACK_CONNECT;

	HDMI_DEBUG("acknowledging audio (%d)\n", ack_hpd);

	if (!audio->engine_on) {
		atomic_set(&audio->acked, 1);
		complete_all(&audio->hpd_comp);
	}
end:
	return rc;
}

static int hdmi_audio_codec_ready(struct platform_device *pdev)
{
	int rc = 0;
	struct hdmi_audio_private *audio;

	audio = hdmi_audio_get_data(pdev);
	if (IS_ERR(audio)) {
		HDMI_ERR("invalid input\n");
		rc = PTR_ERR(audio);
		goto end;
	}

	queue_delayed_work(audio->notify_workqueue,
			&audio->notify_delayed_work, HZ/4);
end:
	return rc;
}

static int hdmi_audio_register_ext_disp(struct hdmi_audio_private *audio)
{
	int rc = 0;
	struct device_node *pd = NULL;
	const char *phandle = "qcom,ext-disp";
	struct msm_ext_disp_init_data *ext;
	struct msm_ext_disp_audio_codec_ops *ops;

	ext = &audio->ext_audio_data;
	ops = &ext->codec_ops;

	ext->codec.type = EXT_DISPLAY_TYPE_HDMI;
	/* 0: DP and 1: HDMI */
	ext->codec.ctrl_id = 1;
	ext->codec.stream_id = 0; /* only stream 0 supported. */
	ext->pdev = audio->pdev;
	ext->intf_data = &audio->hdmi_audio;

	ops->audio_info_setup   = hdmi_audio_info_setup;
	ops->get_audio_edid_blk = hdmi_audio_get_edid_blk;
	ops->cable_status       = hdmi_audio_get_cable_status;
	ops->get_intf_id        = hdmi_audio_get_intf_id;
	ops->teardown_done      = hdmi_audio_teardown_done;
	ops->acknowledge        = hdmi_audio_ack_done;
	ops->ready              = hdmi_audio_codec_ready;

	if (!audio->pdev->dev.of_node) {
		HDMI_ERR("cannot find audio dev.of_node\n");
		rc = -ENODEV;
		goto end;
	}

	pd = of_parse_phandle(audio->pdev->dev.of_node, phandle, 0);
	if (!pd) {
		HDMI_ERR("cannot parse %s handle\n", phandle);
		rc = -ENODEV;
		goto end;
	}

	audio->ext_pdev = of_find_device_by_node(pd);
	if (!audio->ext_pdev) {
		HDMI_ERR("cannot find %s pdev\n", phandle);
		rc = -ENODEV;
		goto end;
	}
#if IS_ENABLED(CONFIG_MSM_EXT_DISPLAY)
	rc = msm_ext_disp_register_intf(audio->ext_pdev, ext);
	if (rc)
		HDMI_ERR("failed to register disp\n");
#endif
end:
	if (pd)
		of_node_put(pd);

	return rc;
}

static int hdmi_audio_deregister_ext_disp(struct hdmi_audio_private *audio)
{
	int rc = 0;
	struct device_node *pd = NULL;
	const char *phandle = "qcom,ext-disp";
	struct msm_ext_disp_init_data *ext;

	ext = &audio->ext_audio_data;

	if (!audio->pdev->dev.of_node) {
		HDMI_ERR("cannot find audio dev.of_node\n");
		rc = -ENODEV;
		goto end;
	}

	pd = of_parse_phandle(audio->pdev->dev.of_node, phandle, 0);
	if (!pd) {
		HDMI_ERR("cannot parse %s handle\n", phandle);
		rc = -ENODEV;
		goto end;
	}

	audio->ext_pdev = of_find_device_by_node(pd);
	if (!audio->ext_pdev) {
		HDMI_ERR("cannot find %s pdev\n", phandle);
		rc = -ENODEV;
		goto end;
	}

#if IS_ENABLED(CONFIG_MSM_EXT_DISPLAY)
	rc = msm_ext_disp_deregister_intf(audio->ext_pdev, ext);
	if (rc)
		HDMI_ERR("failed to deregister disp\n");
#endif

end:
	return rc;
}

static int hdmi_audio_notify(struct hdmi_audio_private *audio, u32 state)
{
	int rc = 0;
	struct msm_ext_disp_init_data *ext = &audio->ext_audio_data;

	atomic_set(&audio->acked, 0);

	if (!ext->intf_ops.audio_notify) {
		HDMI_ERR("audio notify not defined\n");
		goto end;
	}

	reinit_completion(&audio->hpd_comp);
	rc = ext->intf_ops.audio_notify(audio->ext_pdev,
			&ext->codec, state);
	if (rc)
		goto end;

	if (atomic_read(&audio->acked))
		goto end;

	if (state == EXT_DISPLAY_CABLE_DISCONNECT && !audio->engine_on)
		goto end;

	if (state == EXT_DISPLAY_CABLE_CONNECT)
		goto end;

	rc = wait_for_completion_timeout(&audio->hpd_comp, HZ * 4);
	if (!rc) {
		HDMI_ERR("timeout. state=%d err=%d\n", state, rc);
		rc = -ETIMEDOUT;
		goto end;
	}

	HDMI_DEBUG("success\n");
end:
	return rc;
}

static int hdmi_audio_config(struct hdmi_audio_private *audio, u32 state)
{
	int rc = 0;
	struct msm_ext_disp_init_data *ext = &audio->ext_audio_data;

	if (!ext || !ext->intf_ops.audio_config) {
		HDMI_ERR("audio_config not defined\n");
		goto end;
	}

	/*
	 * HDMI Audio sets default STREAM_0 only, other streams are
	 * set by audio driver based on the hardware/software support.
	 */
	rc = ext->intf_ops.audio_config(audio->ext_pdev, &ext->codec, state);
	if (rc)
		HDMI_ERR("failed to config audio, err=%d\n", rc);
end:
	return rc;
}

static int hdmi_audio_on(struct hdmi_audio *hdmi_audio)
{
	int rc;
	struct hdmi_audio_private *audio;
	struct msm_ext_disp_init_data *ext;

	if (!hdmi_audio) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	audio = container_of(hdmi_audio,
			struct hdmi_audio_private, hdmi_audio);
	if (IS_ERR_OR_NULL(audio)) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	rc = hdmi_audio_register_ext_disp(audio);
	if (rc)
		goto end;

	ext = &audio->ext_audio_data;

	atomic_set(&audio->session_on, 1);

	rc = hdmi_audio_config(audio, EXT_DISPLAY_CABLE_CONNECT);
	if (rc)
		goto end;

	rc = hdmi_audio_notify(audio, EXT_DISPLAY_CABLE_CONNECT);
	if (rc)
		goto end;

	HDMI_DEBUG("success\n");
end:
	return rc;
}

static int hdmi_audio_off(struct hdmi_audio *hdmi_audio, bool skip_wait)
{
	int rc = 0;
	struct hdmi_audio_private *audio;
	struct msm_ext_disp_init_data *ext;
	bool work_pending = false;

	if (!hdmi_audio) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	audio = container_of(hdmi_audio,
			struct hdmi_audio_private, hdmi_audio);

	if (!atomic_read(&audio->session_on)) {
		HDMI_DEBUG("audio already off\n");
		return rc;
	}

	ext = &audio->ext_audio_data;

	work_pending = cancel_delayed_work_sync(&audio->notify_delayed_work);
	if (work_pending)
		HDMI_DEBUG("pending notification work completed\n");

	if (!skip_wait) {
		rc = hdmi_audio_notify(audio, EXT_DISPLAY_CABLE_DISCONNECT);
		if (rc)
			goto end;
	}

	HDMI_DEBUG("success\n");
end:
	hdmi_audio_config(audio, EXT_DISPLAY_CABLE_DISCONNECT);

	atomic_set(&audio->session_on, 0);
	audio->engine_on  = false;
	_hdmi_audio_infoframe_setup(audio, false);
	_hdmi_audio_setup_acr(audio, false);

	hdmi_audio_deregister_ext_disp(audio);

	return rc;
}

static void hdmi_audio_notify_work_fn(struct work_struct *work)
{
	struct hdmi_audio_private *audio;
	struct delayed_work *dw = to_delayed_work(work);

	audio = container_of(dw, struct hdmi_audio_private,
				notify_delayed_work);

	hdmi_audio_notify(audio, EXT_DISPLAY_CABLE_CONNECT);
}

static int hdmi_audio_create_notify_workqueue(struct hdmi_audio_private *audio)
{
	audio->notify_workqueue = create_workqueue("sdm_hdmi_audio_notify");
	if (IS_ERR_OR_NULL(audio->notify_workqueue)) {
		HDMI_ERR("Error creating notify_workqueue\n");
		return -EPERM;
	}

	INIT_DELAYED_WORK(&audio->notify_delayed_work,
			hdmi_audio_notify_work_fn);

	return 0;
}

static void hdmi_audio_destroy_notify_workqueue(
			struct hdmi_audio_private *audio)
{
	if (audio->notify_workqueue)
		destroy_workqueue(audio->notify_workqueue);
}

struct hdmi_audio *hdmi_audio_get(struct platform_device *pdev,
		struct hdmi_panel *panel, struct hdmi_parser *parser)
{
	int rc;
	struct hdmi_audio_private *audio;
	struct hdmi_audio *hdmi_audio;

	if (!pdev || !panel || !parser) {
		HDMI_ERR("invalid input\n");
		rc = -EINVAL;
		goto error;
	}

	audio = devm_kzalloc(&pdev->dev, sizeof(*audio), GFP_KERNEL);
	if (!audio) {
		rc = -ENOMEM;
		goto error;
	}

	rc = hdmi_audio_create_notify_workqueue(audio);
	if (rc)
		goto error_notify_workqueue;

	init_completion(&audio->hpd_comp);

	audio->pdev = pdev;
	audio->panel = panel;
	audio->io_data = parser->get_io(parser, "hdmi_ctrl");
	audio->max_pclk_khz = parser->max_pclk_khz;

	atomic_set(&audio->acked, 0);

	hdmi_audio = &audio->hdmi_audio;

	mutex_init(&audio->ops_lock);

	hdmi_audio->on  = hdmi_audio_on;
	hdmi_audio->off = hdmi_audio_off;

	return hdmi_audio;

error_notify_workqueue:
error:
	return ERR_PTR(rc);
}

void hdmi_audio_put(struct hdmi_audio *hdmi_audio)
{
	struct hdmi_audio_private *audio;

	if (!hdmi_audio)
		return;

	audio = container_of(hdmi_audio,
			struct hdmi_audio_private, hdmi_audio);

	mutex_destroy(&audio->ops_lock);

	hdmi_audio_destroy_notify_workqueue(audio);

}

