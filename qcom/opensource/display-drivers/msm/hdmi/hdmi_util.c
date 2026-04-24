// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 */

#include "hdmi_util.h"
#include "sde_kms.h"
#include "sde_connector.h"
#include "hdmi_debug.h"

#define hdmi_read(x, off)  \
	readl_relaxed((x)->io_data->io.base + (off))

#define hdmi_write(x, off, value) \
	writel_relaxed(value, (x)->io_data->io.base + (off))

#define to_hdmi_i2c_adapter(x) container_of(x, struct hdmi_i2c_adapter, base)

#define HDMI_I2C_TRANSACTION_REG_RW__MASK			0x00000001
#define HDMI_I2C_TRANSACTION_REG_RW__SHIFT			0
#define HDMI_I2C_TRANSACTION_REG_STOP_ON_NACK			0x00000100
#define HDMI_I2C_TRANSACTION_REG_START				0x00001000
#define HDMI_I2C_TRANSACTION_REG_STOP				0x00002000
#define HDMI_I2C_TRANSACTION_REG_CNT__MASK			0x00ff0000
#define HDMI_I2C_TRANSACTION_REG_CNT__SHIFT			16

static inline uint32_t HDMI_DDC_CTRL_TRANSACTION_CNT(uint32_t val)
{
	return ((val) << HDMI_DDC_CTRL_TRANSACTION_CNT__SHIFT) &
		HDMI_DDC_CTRL_TRANSACTION_CNT__MASK;
}

static inline uint32_t HDMI_DDC_SPEED_THRESHOLD(uint32_t val)
{
	return ((val) << HDMI_DDC_SPEED_THRESHOLD__SHIFT) &
		HDMI_DDC_SPEED_THRESHOLD__MASK;
}

static inline uint32_t HDMI_DDC_SPEED_PRESCALE(uint32_t val)
{
	return ((val) << HDMI_DDC_SPEED_PRESCALE__SHIFT) &
		HDMI_DDC_SPEED_PRESCALE__MASK;
}


static inline uint32_t HDMI_DDC_SETUP_TIMEOUT(uint32_t val)
{
	return ((val) << HDMI_DDC_SETUP_TIMEOUT__SHIFT) &
		HDMI_DDC_SETUP_TIMEOUT__MASK;
}


static inline uint32_t HDMI_I2C_TRANSACTION_REG_RW(enum hdmi_ddc_read_write val)
{
	return ((val) << HDMI_I2C_TRANSACTION_REG_RW__SHIFT) &
		HDMI_I2C_TRANSACTION_REG_RW__MASK;
}

static inline uint32_t HDMI_I2C_TRANSACTION(uint32_t i0)
{
	return HDMI_DDC_TRANS0 + 0x4*i0;
}


static inline uint32_t HDMI_DDC_DATA_DATA_RW(enum hdmi_ddc_read_write val)
{
	return ((val) << HDMI_DDC_DATA_DATA_RW__SHIFT) &
		HDMI_DDC_DATA_DATA_RW__MASK;
}

static inline uint32_t HDMI_DDC_DATA_DATA(uint32_t val)
{
	return ((val) << HDMI_DDC_DATA_DATA__SHIFT) & HDMI_DDC_DATA_DATA__MASK;
}

static inline uint32_t HDMI_DDC_DATA_INDEX(uint32_t val)
{
	return ((val) << HDMI_DDC_DATA_INDEX__SHIFT) &
		HDMI_DDC_DATA_INDEX__MASK;
}

static inline uint32_t HDMI_DDC_REF_REFTIMER(uint32_t val)
{
	return ((val) << HDMI_DDC_REF_REFTIMER__SHIFT) &
		HDMI_DDC_REF_REFTIMER__MASK;
}

void sde_hdmi_ddc_scrambling_isr(void *hdmi_ddc)
{
	bool scrambler_timer_off = false;
	u32 intr2, intr5;

	struct hdmi_ddc *ddc = (struct hdmi_ddc *)hdmi_ddc;


	if (!ddc) {
		pr_err("invalid input\n");
		return;
	}

	intr2 = hdmi_read(ddc, HDMI_DDC_INT_CTRL2);
	intr5 = hdmi_read(ddc, HDMI_DDC_INT_CTRL5);

	HDMI_DEBUG("intr2: 0x%x, intr5: 0x%x\n", intr2, intr5);

	if (intr2 & BIT(12)) {
		pr_err("SCRAMBLER_STATUS_NOT\n");

		intr2 |= BIT(14);
		scrambler_timer_off = true;
	}

	if (intr2 & BIT(8)) {
		pr_err("SCRAMBLER_STATUS_DDC_FAILED\n");

		intr2 |= BIT(9);

		scrambler_timer_off = true;
	}
	hdmi_write(ddc, HDMI_DDC_INT_CTRL2, intr2);

	if (intr5 & BIT(8)) {
		pr_err("SCRAMBLER_STATUS_DDC_REQ_TIMEOUT\n");
		intr5 |= BIT(9);
		intr5 &= ~BIT(10);
		scrambler_timer_off = true;
	}
	hdmi_write(ddc, HDMI_DDC_INT_CTRL5, intr5);

	if (scrambler_timer_off)
		_sde_hdmi_scrambler_ddc_disable((void *)ddc);

}

static void _sde_hdmi_scrambler_ddc_reset(struct hdmi_ddc *ddc)
{
	u32 reg_val;

	/* clear ack and disable interrupts */
	reg_val = BIT(14) | BIT(9) | BIT(5) | BIT(1);
	hdmi_write(ddc, HDMI_DDC_INT_CTRL2, reg_val);

	/* Reset DDC timers */
	reg_val = BIT(0) | hdmi_read(ddc, HDMI_SCRAMBLER_STATUS_DDC_CTRL);
	hdmi_write(ddc, HDMI_SCRAMBLER_STATUS_DDC_CTRL, reg_val);

	reg_val = hdmi_read(ddc, HDMI_SCRAMBLER_STATUS_DDC_CTRL);
	reg_val &= ~BIT(0);
	hdmi_write(ddc, HDMI_SCRAMBLER_STATUS_DDC_CTRL, reg_val);
}


void _sde_hdmi_scrambler_ddc_disable(void *hdmi_ddc)
{
	u32 reg_val;

	struct hdmi_ddc *ddc = (struct hdmi_ddc *)hdmi_ddc;

	if (!ddc) {
		pr_err("Invalid parameters\n");
		return;
	}

	_sde_hdmi_scrambler_ddc_reset(ddc);
	/* Disable HW DDC access to RxStatus register */
	reg_val = hdmi_read(ddc, HDMI_HW_DDC_CTRL);
	reg_val &= ~(BIT(8) | BIT(9));
	hdmi_write(ddc, HDMI_HW_DDC_CTRL, reg_val);
}

static int ddc_clear_irq(struct hdmi_ddc *ddc)
{
	struct hdmi_i2c_adapter *hdmi_i2c = to_hdmi_i2c_adapter(ddc->i2c);
	uint32_t retry = 0xffff;
	uint32_t ddc_int_ctrl;

	do {
		--retry;

		hdmi_write(ddc, HDMI_DDC_INT_CTRL,
			HDMI_DDC_INT_CTRL_SW_DONE_ACK |
			HDMI_DDC_INT_CTRL_SW_DONE_MASK);

		ddc_int_ctrl = hdmi_read(ddc, HDMI_DDC_INT_CTRL);

	} while ((ddc_int_ctrl & HDMI_DDC_INT_CTRL_SW_DONE_INT) && retry);

	if (!retry) {
		HDMI_ERR("timeout waiting for DDC\n");
		return -ETIMEDOUT;
	}

	hdmi_i2c->sw_done = false;

	return 0;
}

static void hdmi_ddc_init(struct hdmi_ddc *ddc)
{

	uint32_t ddc_speed;

	hdmi_write(ddc, HDMI_DDC_CTRL,
			HDMI_DDC_CTRL_SW_STATUS_RESET);
	hdmi_write(ddc, HDMI_DDC_CTRL,
			HDMI_DDC_CTRL_SOFT_RESET);

	ddc_speed = hdmi_read(ddc, HDMI_DDC_SPEED);
	ddc_speed |= HDMI_DDC_SPEED_THRESHOLD(2);
	ddc_speed |= HDMI_DDC_SPEED_PRESCALE(12);

	hdmi_write(ddc, HDMI_DDC_SPEED,
			ddc_speed);

	hdmi_write(ddc, HDMI_DDC_SETUP,
			HDMI_DDC_SETUP_TIMEOUT(0xff));

	/* enable reference timer for 19us */
	hdmi_write(ddc, HDMI_DDC_REF,
			HDMI_DDC_REF_REFTIMER_ENABLE |
			HDMI_DDC_REF_REFTIMER(19));
}

#define MAX_TRANSACTIONS 4
#define SDE_DDC_TXN_CNT_MASK 0x07ff0000
#define SDE_DDC_TXN_CNT_SHIFT 16

static inline uint32_t hdmi_i2c_transaction_reg_cnt(uint32_t val)
{
	return ((val) << SDE_DDC_TXN_CNT_SHIFT) & SDE_DDC_TXN_CNT_MASK;
}

static bool sw_done(struct hdmi_i2c_adapter *hdmi_i2c)
{
	struct hdmi_ddc *ddc = hdmi_i2c->ddc;

	if (!hdmi_i2c->sw_done) {
		uint32_t ddc_int_ctrl;

		ddc_int_ctrl = hdmi_read(ddc, HDMI_DDC_INT_CTRL);

		if ((ddc_int_ctrl & HDMI_DDC_INT_CTRL_SW_DONE_MASK) &&
			(ddc_int_ctrl & HDMI_DDC_INT_CTRL_SW_DONE_INT)) {
			hdmi_i2c->sw_done = true;
			hdmi_write(ddc, HDMI_DDC_INT_CTRL,
					HDMI_DDC_INT_CTRL_SW_DONE_ACK);
		}
	}

	return hdmi_i2c->sw_done;
}

static int hdmi_i2c_xfer(struct i2c_adapter *i2c,
		struct i2c_msg *msgs, int num)
{
	struct hdmi_i2c_adapter *hdmi_i2c = to_hdmi_i2c_adapter(i2c);
	struct hdmi_ddc *ddc = hdmi_i2c->ddc;
	static const uint32_t nack[] = {
		HDMI_DDC_SW_STATUS_NACK0, HDMI_DDC_SW_STATUS_NACK1,
		HDMI_DDC_SW_STATUS_NACK2, HDMI_DDC_SW_STATUS_NACK3,
	};
	int indices[MAX_TRANSACTIONS];
	int ret, i, j, index = 0;
	uint32_t ddc_status, ddc_data, i2c_trans;

	num = min(num, MAX_TRANSACTIONS);

	WARN_ON(!(hdmi_read(ddc, HDMI_CTRL) & HDMI_CTRL_ENABLE));

	if (num == 0)
		return num;

	hdmi_ddc_init(ddc);

	ret = ddc_clear_irq(ddc);
	if (ret)
		return ret;

	for (i = 0; i < num; i++) {
		struct i2c_msg *p = &msgs[i];
		uint32_t raw_addr = p->addr << 1;

		if (p->flags & I2C_M_RD)
			raw_addr |= 1;

		ddc_data = HDMI_DDC_DATA_DATA(raw_addr) |
			HDMI_DDC_DATA_DATA_RW(DDC_WRITE);

		if (i == 0) {
			ddc_data |= HDMI_DDC_DATA_INDEX(0) |
				HDMI_DDC_DATA_INDEX_WRITE;
		}

		hdmi_write(ddc, HDMI_DDC_DATA, ddc_data);
		index++;

		indices[i] = index;

		if (p->flags & I2C_M_RD) {
			index += p->len;
		} else {
			for (j = 0; j < p->len; j++) {
				ddc_data = HDMI_DDC_DATA_DATA(p->buf[j]) |
					HDMI_DDC_DATA_DATA_RW(DDC_WRITE);
				hdmi_write(ddc, HDMI_DDC_DATA, ddc_data);
				index++;
			}
		}

		i2c_trans = hdmi_i2c_transaction_reg_cnt(p->len) |
			HDMI_I2C_TRANSACTION_REG_RW(
			(p->flags & I2C_M_RD) ? DDC_READ : DDC_WRITE) |
			HDMI_I2C_TRANSACTION_REG_START;

		if (i == (num - 1))
			i2c_trans |= HDMI_I2C_TRANSACTION_REG_STOP;

		hdmi_write(ddc, HDMI_I2C_TRANSACTION(i), i2c_trans);
	}

	/* trigger the transfer: */
	hdmi_write(ddc, HDMI_DDC_CTRL,
			HDMI_DDC_CTRL_TRANSACTION_CNT(num - 1) |
			HDMI_DDC_CTRL_GO);

	ret = wait_event_timeout(hdmi_i2c->ddc_event, sw_done(hdmi_i2c), HZ/4);
	if (ret <= 0) {
		if (ret == 0)
			ret = -ETIMEDOUT;
		HDMI_WARN("DDC timeout: %d\n", ret);
		HDMI_DEBUG("sw_status=%08x, hw_status=%08x, int_ctrl=%08x",
				hdmi_read(ddc, HDMI_DDC_SW_STATUS),
				hdmi_read(ddc, HDMI_DDC_HW_STATUS),
				hdmi_read(ddc, HDMI_DDC_INT_CTRL));
		if (ddc->use_hard_timeout) {
			ddc->use_hard_timeout = false;
			ddc->timeout_count = 0;
		}
		return ret;
	}

	ddc_status = hdmi_read(ddc, HDMI_DDC_SW_STATUS);

	/* read back results of any read transactions: */
	for (i = 0; i < num; i++) {
		struct i2c_msg *p = &msgs[i];

		if (!(p->flags & I2C_M_RD))
			continue;

		/* check for NACK: */
		if (ddc_status & nack[i]) {
			HDMI_DEBUG("ddc_status=%08x", ddc_status);
			break;
		}

		ddc_data = HDMI_DDC_DATA_DATA_RW(DDC_READ) |
			HDMI_DDC_DATA_INDEX(indices[i]) |
			HDMI_DDC_DATA_INDEX_WRITE;

		hdmi_write(ddc, HDMI_DDC_DATA, ddc_data);

		/* discard first byte: */
		hdmi_read(ddc, HDMI_DDC_DATA);

		for (j = 0; j < p->len; j++) {
			ddc_data = hdmi_read(ddc, HDMI_DDC_DATA);
			p->buf[j] = FIELD(ddc_data, HDMI_DDC_DATA_DATA);
		}
	}

	if (ddc->use_hard_timeout) {
		ddc->use_hard_timeout = false;
		ddc->timeout_count = jiffies_to_msecs(ret);
	}
	return i;
}

static u32 hdmi_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm hdmi_i2c_algorithm = {
	.master_xfer    = hdmi_i2c_xfer,
	.functionality  = hdmi_i2c_func,
};

void hdmi_ddc_isr(struct i2c_adapter *i2c)
{
	struct hdmi_i2c_adapter *hdmi_i2c = to_hdmi_i2c_adapter(i2c);

	if (sw_done(hdmi_i2c))
		wake_up_all(&hdmi_i2c->ddc_event);
}

static void hdmi_i2c_destroy(struct i2c_adapter *i2c)
{
	i2c_del_adapter(i2c);
}

static struct i2c_adapter *hdmi_i2c_init(struct hdmi_ddc *ddc)
{
	struct device *dev = ddc->dev;
	struct hdmi_i2c_adapter *hdmi_i2c;
	struct i2c_adapter *i2c = NULL;
	int ret;

	hdmi_i2c = devm_kzalloc(dev, sizeof(*hdmi_i2c), GFP_KERNEL);
	if (!hdmi_i2c) {
		ret = -ENOMEM;
		goto fail;
	}

	i2c = &hdmi_i2c->base;

	hdmi_i2c->ddc = ddc;
	init_waitqueue_head(&hdmi_i2c->ddc_event);


	i2c->owner = THIS_MODULE;
	snprintf(i2c->name, sizeof(i2c->name), "msm hdmi i2c");
	i2c->dev.parent = dev;
	i2c->algo = &hdmi_i2c_algorithm;

	ret = i2c_add_adapter(i2c);
	if (ret) {
		HDMI_ERR("failed to register hdmi i2c: %d\n", ret);
		goto fail;
	}

	return i2c;

fail:
	if (i2c)
		hdmi_i2c_destroy(i2c);
	return ERR_PTR(ret);
}


struct hdmi_ddc *hdmi_ddc_get(struct device *dev, struct hdmi_parser *parser)
{
	struct hdmi_ddc *ddc;

	if (!dev) {
		HDMI_ERR("invalid input\n");
		return ERR_PTR(-EINVAL);
	}

	ddc = devm_kzalloc(dev, sizeof(*ddc), GFP_KERNEL);
	if (!ddc)
		return ERR_PTR(-ENOMEM);

	ddc->dev = dev;
	ddc->io_data = parser->get_io(parser, "hdmi_ctrl");

	ddc->i2c = hdmi_i2c_init(ddc);
	ddc->isr = hdmi_ddc_isr;
	ddc->scrambling_isr = sde_hdmi_ddc_scrambling_isr;

	return ddc;
}

void hdmi_ddc_put(struct hdmi_ddc *ddc)
{
	if (ddc->i2c)
		hdmi_i2c_destroy(ddc->i2c);
}
