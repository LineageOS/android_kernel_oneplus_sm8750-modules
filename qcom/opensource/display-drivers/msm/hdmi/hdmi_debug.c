// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/debugfs.h>
#include <linux/slab.h>
#include "hdmi_debug.h"

#include "drm/drm_connector.h"
#include "sde_connector.h"
#include "hdmi_display.h"
#include "hdmi_pll.h"
#include "hdmi_parser.h"
#include "sde_hdcp.h"

#define DEBUG_NAME "drm_hdmi"
#define DEFAULT_CONNECT_NOTIFICATION_DELAY_MS 10
#define DEFAULT_DISCONNECT_DELAY_MS 10

struct hdmi_debug_private {
	struct dentry *root;

	bool hotplug;

	char exe_mode[SZ_32];
	char reg_dump[SZ_32];

	const char *name;

	enum sde_hdcp_state hdcp_status;
	struct hdmi_panel *panel;
	struct drm_connector *connector;
	struct device *dev;
	struct hdmi_debug hdmi_debug;
	struct hdmi_parser *parser;
	struct hdmi_pll *pll;
	struct hdmi_display *display;
	struct mutex lock;
};

static ssize_t hdmi_debug_write_edid(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct hdmi_debug_private *debug = file->private_data;
	u8 *buf = NULL, *buf_t = NULL, *edid = NULL;
	const int char_to_nib = 2;
	size_t edid_size = 0;
	size_t size = 0, edid_buf_index = 0;
	ssize_t rc = count;

	if (!debug)
		return -ENODEV;

	mutex_lock(&debug->lock);

	if (*ppos)
		goto bail;

	size = min_t(size_t, count, SZ_1K);

	buf = kzalloc(size, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(buf)) {
		rc = -ENOMEM;
		goto bail;
	}

	if (copy_from_user(buf, user_buff, size))
		goto bail;

	edid_size = size / char_to_nib;
	buf_t = buf;
	size = edid_size;

	edid = kzalloc(size, GFP_KERNEL);
	if (!edid)
		goto bail;

	while (size--) {
		char t[3];
		int d;

		memcpy(t, buf_t, sizeof(char) * char_to_nib);
		t[char_to_nib] = '\0';

		if (kstrtoint(t, 16, &d)) {
			HDMI_ERR("kstrtoint error\n");
			goto bail;
		}

		edid[edid_buf_index++] = d;
		buf_t += char_to_nib;
	}

bail:
	kfree(buf);
	kfree(edid);

	mutex_unlock(&debug->lock);
	return rc;
}

static ssize_t hdmi_debug_write_edid_modes(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct hdmi_debug_private *debug = file->private_data;
	struct hdmi_panel *panel;
	char buf[SZ_32];
	size_t len = 0;
	int hdisplay = 0, vdisplay = 0, vrefresh = 0, aspect_ratio;

	if (!debug)
		return -ENODEV;

	if (*ppos)
		goto end;

	panel = debug->panel;

	/* Leave room for termination char */
	len = min_t(size_t, count, SZ_32 - 1);
	if (copy_from_user(buf, user_buff, len))
		goto clear;

	buf[len] = '\0';

	if (sscanf(buf, "%d %d %d %d", &hdisplay, &vdisplay, &vrefresh,
				&aspect_ratio) < 3)
		goto clear;

	if (!hdisplay || !vdisplay || !vrefresh)
		goto clear;

	panel->mode_override = true;
	panel->hdisplay = hdisplay;
	panel->vdisplay = vdisplay;
	panel->vrefresh = vrefresh;
	panel->aspect_ratio = aspect_ratio;
	goto end;
clear:
	HDMI_DEBUG("clearing debug modes\n");
	panel->mode_override = false;
end:
	return len;
}

static ssize_t hdmi_debug_tpg_write(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct hdmi_debug_private *debug = file->private_data;
	char buf[SZ_8];
	size_t len = 0;
	u32 tpg_pattern = 0;

	if (!debug)
		return -ENODEV;

	if (*ppos)
		return 0;

	/* Leave room for termination char */
	len = min_t(size_t, count, SZ_8 - 1);
	if (copy_from_user(buf, user_buff, len))
		goto bail;

	buf[len] = '\0';

	if (kstrtoint(buf, 10, &tpg_pattern) != 0)
		goto bail;

	HDMI_DEBUG("tpg_pattern: %d\n", tpg_pattern);

	if (tpg_pattern == debug->hdmi_debug.tpg_pattern)
		goto bail;

	//if (debug->panel)
	//	debug->panel->tpg_config(debug->panel, tpg_pattern:;

	debug->hdmi_debug.tpg_pattern = tpg_pattern;
bail:
	return len;
}

static ssize_t hdmi_debug_read_connected(struct file *file,
		char __user *user_buff, size_t count, loff_t *ppos)
{
	struct hdmi_debug_private *debug = file->private_data;
	char buf[SZ_8];
	u32 len = 0;

	if (!debug)
		return -ENODEV;

	if (*ppos)
		return 0;

	len += scnprintf(buf, SZ_8, "%d\n", debug->display->connected);

	len = min_t(size_t, count, len);
	if (copy_to_user(user_buff, buf, len))
		return -EFAULT;

	*ppos += len;
	return len;
}

static ssize_t hdmi_debug_write_hdcp(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct hdmi_debug_private *debug = file->private_data;
	char buf[SZ_8];
	size_t len = 0;
	int hdcp = 0;

	if (!debug)
		return -ENODEV;

	if (*ppos)
		return 0;

	/* Leave room for termination char */
	len = min_t(size_t, count, SZ_8 - 1);
	if (copy_from_user(buf, user_buff, len))
		goto end;

	buf[len] = '\0';

	if (kstrtoint(buf, 10, &hdcp) != 0)
		goto end;

	debug->hdmi_debug.hdcp_disabled = !hdcp;
end:
	return len;
}

static ssize_t hdmi_debug_read_hdcp(struct file *file,
		char __user *user_buff, size_t count, loff_t *ppos)
{
	struct hdmi_debug_private *debug = file->private_data;
	u32 len = 0;

	if (!debug)
		return -ENODEV;

	if (*ppos)
		return 0;

	len = sizeof(debug->hdmi_debug.hdcp_status);

	len = min_t(size_t, count, len);
	if (copy_to_user(user_buff, debug->hdmi_debug.hdcp_status, len))
		return -EFAULT;

	*ppos += len;
	return len;
}

static int hdmi_debug_check_buffer_overflow(int rc, int *max_size, int *len)
{
	if (rc >= *max_size) {
		HDMI_ERR("buffer overflow\n");
		return -EINVAL;
	}
	*len += rc;
	*max_size = SZ_4K - *len;

	return 0;
}

static ssize_t hdmi_debug_read_edid_modes(struct file *file,
		char __user *user_buff, size_t count, loff_t *ppos)
{
	struct hdmi_debug_private *debug = file->private_data;
	char *buf;
	u32 len = 0, ret = 0, max_size = SZ_4K;
	int rc = 0;
	struct drm_connector *connector;
	struct drm_display_mode *mode;

	if (!debug) {
		HDMI_ERR("invalid data\n");
		rc = -ENODEV;
		goto error;
	}

	connector = debug->connector;

	if (!connector) {
		HDMI_ERR("connector is NULL\n");
		rc = -EINVAL;
		goto error;
	}

	if (*ppos)
		goto error;

	buf = kzalloc(SZ_4K, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(buf)) {
		rc = -ENOMEM;
		goto error;
	}

	mutex_lock(&connector->dev->mode_config.mutex);
	list_for_each_entry(mode, &connector->modes, head) {
		ret = scnprintf(buf + len, max_size,
		"%s %d %d %d %d %d %d %d %d %d %d %d 0x%x\n",
		mode->name, drm_mode_vrefresh(mode), mode->picture_aspect_ratio,
		mode->hdisplay, mode->hsync_start,
		mode->hsync_end, mode->htotal,
		mode->vdisplay, mode->vsync_start, mode->vsync_end,
		mode->vtotal, mode->clock, mode->flags);
		if (hdmi_debug_check_buffer_overflow(ret, &max_size, &len))
			break;
	}
	mutex_unlock(&connector->dev->mode_config.mutex);

	len = min_t(size_t, count, len);
	if (copy_to_user(user_buff, buf, len)) {
		kfree(buf);
		rc = -EFAULT;
		goto error;
	}

	*ppos += len;
	kfree(buf);

	return len;
error:
	return rc;
}

static ssize_t hdmi_debug_read_info(struct file *file, char __user *user_buff,
		size_t count, loff_t *ppos)
{
	struct hdmi_debug_private *debug = file->private_data;
	char *buf;
	u32 len = 0, rc = 0;
	u32 max_size = SZ_4K;

	if (!debug)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(SZ_4K, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(buf))
		return -ENOMEM;

	rc = scnprintf(buf + len, max_size, "\tresolution=%dx%d@%dHz\n",
		debug->panel->pinfo.h_active,
		debug->panel->pinfo.v_active,
		debug->panel->pinfo.refresh_rate);
	if (hdmi_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "\tpclock=%dKHz\n",
		debug->panel->pinfo.pixel_clk_khz);
	if (hdmi_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "\tbpp=%d\n",
		debug->panel->pinfo.bpp);
	if (hdmi_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size,
		"\tdisplay=%s\n", debug->display->name);
	if (hdmi_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	len = min_t(size_t, count, len);
	if (copy_to_user(user_buff, buf, len))
		goto error;

	*ppos += len;

	kfree(buf);
	return len;
error:
	kfree(buf);
	return -EINVAL;
}


static ssize_t hdmi_debug_tpg_read(struct file *file,
	char __user *user_buff, size_t count, loff_t *ppos)
{
	struct hdmi_debug_private *debug = file->private_data;
	char buf[SZ_8];
	u32 len = 0;

	if (!debug)
		return -ENODEV;

	if (*ppos)
		return 0;

	len += scnprintf(buf, SZ_8, "%d\n", debug->hdmi_debug.tpg_pattern);

	len = min_t(size_t, count, len);
	if (copy_to_user(user_buff, buf, len))
		return -EFAULT;

	*ppos += len;
	return len;
}

static ssize_t hdmi_debug_write_dump(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct hdmi_debug_private *debug = file->private_data;
	char buf[SZ_32];
	size_t len = 0;

	if (!debug)
		return -ENODEV;

	if (*ppos)
		return 0;

	/* Leave room for termination char */
	len = min_t(size_t, count, SZ_32 - 1);
	if (copy_from_user(buf, user_buff, len))
		goto end;

	buf[len] = '\0';

	if (sscanf(buf, "%31s", debug->reg_dump) != 1)
		goto end;

	/* qfprom register dump not supported */
	if (!strcmp(debug->reg_dump, "qfprom_physical"))
		strscpy(debug->reg_dump, "clear", sizeof(debug->reg_dump));
end:
	return len;
}

static ssize_t hdmi_debug_read_dump(struct file *file,
		char __user *user_buff, size_t count, loff_t *ppos)
{
	int rc = 0;
	struct hdmi_debug_private *debug = file->private_data;
	u8 *buf = NULL;
	u32 len = 0;
	char prefix[SZ_32];

	if (!debug)
		return -ENODEV;

	if (*ppos)
		return 0;

	if (rc)
		goto end;

	scnprintf(prefix, sizeof(prefix), "%s: ", debug->reg_dump);
	print_hex_dump_debug(prefix, DUMP_PREFIX_NONE,
		16, 4, buf, len, false);

	len = min_t(size_t, count, len);
	if (copy_to_user(user_buff, buf, len))
		return -EFAULT;

	*ppos += len;
end:
	return len;
}

static ssize_t hdmi_hdcp_state_read(struct file *file,
						char __user *buff,
						size_t count,
						loff_t *ppos)
{
	struct hdmi_debug_private *debug = file->private_data;
	char buf[SZ_128];
	u32 len = 0;

	if (!debug)
		return -ENODEV;

	HDMI_DEBUG("%s +", __func__);
	if (*ppos)
		return 0;

	len += scnprintf(buf, SZ_128 - len, "HDCP state : %s\n",
			sde_hdcp_state_name(debug->hdcp_status));

	if (copy_to_user(buff, buf, len))
		return -EFAULT;

	*ppos += len;
	HDMI_DEBUG("%s - ", __func__);
	return len;
}

static const struct file_operations hdmi_debug_fops = {
	.open = simple_open,
	.read = hdmi_debug_read_info,
};

static const struct file_operations edid_modes_fops = {
	.open = simple_open,
	.read = hdmi_debug_read_edid_modes,
	.write = hdmi_debug_write_edid_modes,
};

static const struct file_operations edid_fops = {
	.open = simple_open,
	.write = hdmi_debug_write_edid,
};

static const struct file_operations connected_fops = {
	.open = simple_open,
	.read = hdmi_debug_read_connected,
};

// #TODO topology read write need to be finalaized
static const struct file_operations tpg_fops = {
	.open = simple_open,
	.read = hdmi_debug_tpg_read,
	.write = hdmi_debug_tpg_write,
};

// #TODO need to be finalaized
static const struct file_operations dump_fops = {
	.open = simple_open,
	.write = hdmi_debug_write_dump,
	.read = hdmi_debug_read_dump,
};

static const struct file_operations hdcp_fops = {
	.open = simple_open,
	.write = hdmi_debug_write_hdcp,
	.read = hdmi_debug_read_hdcp,
};

static const struct file_operations hdmi_hdcp_state_fops = {
	.open = simple_open,
	.read = hdmi_hdcp_state_read,
};

static int hdmi_debug_init_sink_caps(struct hdmi_debug_private *debug,
		struct dentry *dir)
{
	int rc = 0;
	struct dentry *file;

	file = debugfs_create_file("edid_modes", 0644, dir,
					debug, &edid_modes_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		HDMI_ERR("[%s] debugfs create edid_modes failed, rc=%d\n",
		       debug->name, rc);
		return rc;
	}

	file = debugfs_create_file("edid", 0644, dir,
					debug, &edid_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		HDMI_ERR("[%s] debugfs edid failed, rc=%d\n",
			debug->name, rc);
		return rc;
	}

	return rc;
}

static int hdmi_debug_init_status(struct hdmi_debug_private *debug,
		struct dentry *dir)
{
	int rc = 0;
	struct dentry *file;

	file = debugfs_create_file("hdmi_debug", 0444, dir,
				debug, &hdmi_debug_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		HDMI_ERR("[%s] debugfs create file failed, rc=%d\n",
		       debug->name, rc);
		return rc;
	}

	file = debugfs_create_file("connected", 0444, dir,
					debug, &connected_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		HDMI_ERR("[%s] debugfs connected failed, rc=%d\n",
			debug->name, rc);
		return rc;
	}

	file = debugfs_create_file("hdcp", 0644, dir, debug, &hdcp_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		HDMI_ERR("[%s] debugfs hdcp failed, rc=%d\n",
			debug->name, rc);
		return rc;
	}

	file = debugfs_create_file("hdmi_hdcp_state", 0400, dir, debug,
			&hdmi_hdcp_state_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		HDMI_ERR("[%s] debugfs hdcp state failed, rc=%d\n",
			debug->name, rc);
		return rc;
	}

	return rc;
}

static int hdmi_debug_init_reg_dump(struct hdmi_debug_private *debug,
		struct dentry *dir)
{
	int rc = 0;
	struct dentry *file;

	file = debugfs_create_file("dump", 0644, dir,
			debug, &dump_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		HDMI_ERR("[%s] debugfs dump failed, rc=%d\n",
			debug->name, rc);
		return rc;
	}

	return rc;
}

static int hdmi_debug_init_configs(struct hdmi_debug_private *debug,
		struct dentry *dir)
{

	debugfs_create_ulong("connect_notification_delay_ms", 0644, dir,
			&debug->hdmi_debug.connect_notification_delay_ms);

	debug->hdmi_debug.connect_notification_delay_ms =
			DEFAULT_CONNECT_NOTIFICATION_DELAY_MS;

	debugfs_create_u32("disconnect_delay_ms", 0644, dir,
			&debug->hdmi_debug.disconnect_delay_ms);

	debug->hdmi_debug.disconnect_delay_ms = DEFAULT_DISCONNECT_DELAY_MS;

	return 0;

}

static int hdmi_debug_init(struct hdmi_debug *hdmi_debug)
{
	int rc = 0;
	struct hdmi_debug_private *debug = container_of(hdmi_debug,
		struct hdmi_debug_private, hdmi_debug);
	struct dentry *dir;

	if (!IS_ENABLED(CONFIG_DEBUG_FS)) {
		HDMI_WARN("Not creating debug root dir.");
		debug->root = NULL;
		return 0;
	}

	debug->name = of_get_property(debug->dev->of_node, "label", NULL);
	if (!debug->name)
		debug->name = DEBUG_NAME;

	dir = debugfs_create_dir(debug->name, NULL);
	if (IS_ERR_OR_NULL(dir)) {
		if (!dir)
			rc = -EINVAL;
		else
			rc = PTR_ERR(dir);
		HDMI_ERR("[%s] debugfs create dir failed, rc = %d\n",
		       debug->name, rc);
		goto error;
	}

	debug->root = dir;

	rc = hdmi_debug_init_status(debug, dir);
	if (rc)
		goto error_remove_dir;

	rc = hdmi_debug_init_sink_caps(debug, dir);
	if (rc)
		goto error_remove_dir;

	if (rc)
		goto error_remove_dir;

	rc = hdmi_debug_init_reg_dump(debug, dir);
	if (rc)
		goto error_remove_dir;

	rc = hdmi_debug_init_configs(debug, dir);
	if (rc)
		goto error_remove_dir;

	return 0;

error_remove_dir:
	debugfs_remove_recursive(dir);
error:
	return rc;
}

static void hdmi_debug_abort(struct hdmi_debug *hdmi_debug)
{
	struct hdmi_debug_private *debug;

	if (!hdmi_debug)
		return;

	debug = container_of(hdmi_debug, struct hdmi_debug_private, hdmi_debug);

	mutex_lock(&debug->lock);
	// disconnect has already been handled. so clear hotplug
	debug->hotplug = false;
	mutex_unlock(&debug->lock);
}

struct hdmi_debug *hdmi_debug_get(struct hdmi_debug_in *in)
{
	int rc = 0;
	struct hdmi_debug_private *debug;
	struct hdmi_debug *hdmi_debug;

	if (!in->dev || !in->panel || !in->pll) {
		HDMI_ERR("invalid input\n");
		rc = -EINVAL;
		goto error;
	}

	debug = devm_kzalloc(in->dev, sizeof(*debug), GFP_KERNEL);
	if (!debug) {
		rc = -ENOMEM;
		goto error;
	}

	debug->panel = in->panel;
	debug->dev = in->dev;
	debug->connector = in->connector;
	debug->parser = in->parser;
	debug->pll = in->pll;
	debug->display = in->display;

	hdmi_debug = &debug->hdmi_debug;

	mutex_init(&debug->lock);

	rc = hdmi_debug_init(hdmi_debug);
	if (rc) {
		kfree(debug);
		goto error;
	}

	hdmi_debug->abort = hdmi_debug_abort;
	hdmi_debug->max_pclk_khz = debug->parser->max_pclk_khz;

	return hdmi_debug;
error:
	return ERR_PTR(rc);
}

static int hdmi_debug_deinit(struct hdmi_debug *hdmi_debug)
{
	struct hdmi_debug_private *debug;

	if (!hdmi_debug)
		return -EINVAL;

	debug = container_of(hdmi_debug,
			struct hdmi_debug_private, hdmi_debug);

	debugfs_remove_recursive(debug->root);

	return 0;
}

void hdmi_debug_put(struct hdmi_debug *hdmi_debug)
{
	struct hdmi_debug_private *debug;

	if (!hdmi_debug)
		return;

	debug = container_of(hdmi_debug,
			struct hdmi_debug_private, hdmi_debug);

	hdmi_debug_deinit(hdmi_debug);

	mutex_destroy(&debug->lock);

	kfree(debug);
}
