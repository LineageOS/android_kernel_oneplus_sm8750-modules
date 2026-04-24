/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 */

#ifndef __HDMI_UTIL_H__
#define __HDMI_UTIL_H__

#include <linux/types.h>
#include <linux/spinlock.h>
#include <msm_ext_display.h>
#include "hdmi_regs.h"
#include "msm_drv.h"
#include "hdmi_drm.h"

#ifdef HDMI_UTIL_DEBUG_ENABLE
#define HDMI_UTIL_DEBUG(fmt, args...)   SDE_ERROR(fmt, ##args)
#else
#define HDMI_UTIL_DEBUG(fmt, args...)   SDE_DEBUG(fmt, ##args)
#endif

#ifndef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

#define HDMI_UTIL_ERROR(fmt, args...)   SDE_ERROR(fmt, ##args)

#define HDMI_KHZ_TO_HZ 1000
#define HDMI_MHZ_TO_HZ 1000000
#define HDMI_YUV420_24BPP_PCLK_TMDS_CH_RATE_RATIO 2
#define HDMI_RGB_24BPP_PCLK_TMDS_CH_RATE_RATIO 1

/*  Default hsyncs for 4k@60 for 200ms
 *  added resolve error
 */
#define HDMI_DEFAULT_TIMEOUT_HSYNC 28571

/*
 * Offsets in HDMI_DDC_INT_CTRL0 register
 *
 * The HDMI_DDC_INT_CTRL0 register is intended for HDCP 2.2 RxStatus
 * register manipulation. It reads like this:
 *
 * Bit 31: RXSTATUS_MESSAGE_SIZE_MASK (1 = generate interrupt when size > 0)
 * Bit 30: RXSTATUS_MESSAGE_SIZE_ACK  (1 = Acknowledge message size intr)
 * Bits 29-20: RXSTATUS_MESSAGE_SIZE  (Actual size of message available)
 * Bits 19-18: RXSTATUS_READY_MASK    (1 = generate interrupt when ready = 1
 *       2 = generate interrupt when ready = 0)
 * Bit 17: RXSTATUS_READY_ACK         (1 = Acknowledge ready bit interrupt)
 * Bit 16: RXSTATUS_READY      (1 = Rxstatus ready bit read is 1)
 * Bit 15: RXSTATUS_READY_NOT         (1 = Rxstatus ready bit read is 0)
 * Bit 14: RXSTATUS_REAUTH_REQ_MASK   (1 = generate interrupt when reauth is
 *   requested by sink)
 * Bit 13: RXSTATUS_REAUTH_REQ_ACK    (1 = Acknowledge Reauth req interrupt)
 * Bit 12: RXSTATUS_REAUTH_REQ        (1 = Rxstatus reauth req bit read is 1)
 * Bit 10: RXSTATUS_DDC_FAILED_MASK   (1 = generate interrupt when DDC
 *   tranasaction fails)
 * Bit 9:  RXSTATUS_DDC_FAILED_ACK    (1 = Acknowledge ddc failure interrupt)
 * Bit 8:  RXSTATUS_DDC_FAILED      (1 = DDC transaction failed)
 * Bit 6:  RXSTATUS_DDC_DONE_MASK     (1 = generate interrupt when DDC
 *   transaction completes)
 * Bit 5:  RXSTATUS_DDC_DONE_ACK      (1 = Acknowledge ddc done interrupt)
 * Bit 4:  RXSTATUS_DDC_DONE      (1 = DDC transaction is done)
 * Bit 2:  RXSTATUS_DDC_REQ_MASK      (1 = generate interrupt when DDC Read
 *   request for RXstatus is made)
 * Bit 1:  RXSTATUS_DDC_REQ_ACK       (1 = Acknowledge Rxstatus read interrupt)
 * Bit 0:  RXSTATUS_DDC_REQ           (1 = RXStatus DDC read request is made)
 *
 */

#define HDCP2P2_RXSTATUS_MESSAGE_SIZE_SHIFT         20
#define HDCP2P2_RXSTATUS_MESSAGE_SIZE_MASK          0x3ff00000
#define HDCP2P2_RXSTATUS_MESSAGE_SIZE_ACK_SHIFT     30
#define HDCP2P2_RXSTATUS_MESSAGE_SIZE_INTR_SHIFT    31

#define HDCP2P2_RXSTATUS_REAUTH_REQ_SHIFT           12
#define HDCP2P2_RXSTATUS_REAUTH_REQ_MASK             1
#define HDCP2P2_RXSTATUS_REAUTH_REQ_ACK_SHIFT    13
#define HDCP2P2_RXSTATUS_REAUTH_REQ_INTR_SHIFT    14

#define HDCP2P2_RXSTATUS_READY_SHIFT    16
#define HDCP2P2_RXSTATUS_READY_MASK                  1
#define HDCP2P2_RXSTATUS_READY_ACK_SHIFT            17
#define HDCP2P2_RXSTATUS_READY_INTR_SHIFT           18
#define HDCP2P2_RXSTATUS_READY_INTR_MASK            18

#define HDCP2P2_RXSTATUS_DDC_FAILED_SHIFT           8
#define HDCP2P2_RXSTATUS_DDC_FAILED_ACKSHIFT        9
#define HDCP2P2_RXSTATUS_DDC_FAILED_INTR_MASK       10
#define HDCP2P2_RXSTATUS_DDC_DONE                   6

/*
 * Bits 1:0 in HDMI_HW_DDC_CTRL that dictate how the HDCP 2.2 RxStatus will be
 * read by the hardware
 */
#define HDCP2P2_RXSTATUS_HW_DDC_DISABLE             0
#define HDCP2P2_RXSTATUS_HW_DDC_AUTOMATIC_LOOP      1
#define HDCP2P2_RXSTATUS_HW_DDC_FORCE_LOOP          2
#define HDCP2P2_RXSTATUS_HW_DDC_SW_TRIGGER          3

struct hdmi_ddc {
	struct device *dev;
	struct hdmi_io_data *io_data;
	bool use_hard_timeout;
	u32 timeout_count;
	struct i2c_adapter *i2c;

	void (*isr)(struct i2c_adapter *i2c);
	void (*scrambling_isr)(void *hdmi_ddc);
	spinlock_t reg_lock;
};

struct hdmi_hdcp_status {
	int hdcp_state;
	int hdcp_version;
};

struct hdmi_tx_ddc_data {
	char *what;
	u8 *data_buf;
	u32 data_len;
	u32 dev_addr;
	u32 offset;
	u32 request_len;
	u32 retry_align;
	u32 hard_timeout;
	u32 timeout_left;
	int retry;
};

enum hdmi_tx_hdcp2p2_rxstatus_intr_mask {
	RXSTATUS_MESSAGE_SIZE = BIT(31),
	RXSTATUS_READY = BIT(18),
	RXSTATUS_REAUTH_REQ = BIT(14),
};

struct hdmi_tx_hdcp2p2_ddc_data {
	enum hdmi_tx_hdcp2p2_rxstatus_intr_mask intr_mask;
	u32 timeout_ms;
	u32 timeout_hsync;
	u32 periodic_timer_hsync;
	u32 timeout_left;
	u32 read_method;
	u32 message_size;
	bool encryption_ready;
	bool ready;
	bool reauth_req;
	bool ddc_max_retries_fail;
	bool ddc_done;
	bool ddc_read_req;
	bool ddc_timeout;
	bool wait;
	int irq_wait_count;
	void (*link_cb)(void *data);
	void *link_data;
};

struct hdmi_tx_ddc_ctrl {
	struct completion rx_status_done;
	struct hdmi_tx_ddc_data ddc_data;
	struct hdmi_tx_hdcp2p2_ddc_data hdcp2p2_ddc_data;
};

enum hdmi_tx_io_type {
	HDMI_TX_CORE_IO,
	HDMI_TX_QFPROM_IO,
	HDMI_TX_HDCP_IO,
	HDMI_TX_MAX_IO
};

struct hdmi_i2c_adapter {
	struct i2c_adapter base;
	struct hdmi_ddc *ddc;
	bool sw_done;
	wait_queue_head_t ddc_event;
};

/*
 * DDC utility functions
 */
int hdmi_ddc_read(struct hdmi *hdmi, u16 addr, u8 offset,
				  u8 *data, u16 data_len, bool self_retry);
int hdmi_ddc_write(struct hdmi *hdmi, u16 addr, u8 offset,
				   u8 *data, u16 data_len, bool self_retry);

#if IS_ENABLED(CONFIG_DRM_MSM_HDMI)
int hdmi_ddc_write_helper(struct hdmi_ddc *ddc);
int hdmi_ddc_read_helper(struct hdmi_ddc *ddc);
#else
int hdmi_ddc_write_helper(struct hdmi_ddc *ddc)
{
	return 0;
}
int hdmi_ddc_read_helper(struct hdmi_ddc *ddc)
{
	return 0;
}
#endif
void sde_hdmi_ddc_scrambling_isr(void *hdmi_ddc);

void _sde_hdmi_scrambler_ddc_disable(void *hdmi_display);

void hdmi_hdcp2p2_ddc_disable(void *hdmi_display);

int hdmi_hdcp2p2_read_rxstatus(void *hdmi_display);
void hdmi_ddc_config(void *hdmi_display);
int hdmi_ddc_hdcp2p2_isr(void *hdmi_display);
void hdmi_ddc_isr(struct i2c_adapter *i2c);
struct hdmi_ddc *hdmi_ddc_get(struct device *dev, struct hdmi_parser *parser);
void hdmi_ddc_put(struct hdmi_ddc *ddc);
#endif /* #define __HDMI_UTIL_H__ */
