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
 * This file declares the platform-specific or hardware relevant data.
 */

#ifndef _SYNAPTICS_TCM2_PLATFORM_H_
#define _SYNAPTICS_TCM2_PLATFORM_H_

#include "tcm/synaptics_touchcom_platform.h"
#include "syna_tcm2_runtime.h"

/*
 * Capability of bus transferred
 *
 *    RD_CHUNK_SIZE: the max. transferred size for one 'read' operation
 *    WR_CHUNK_SIZE: the max. transferred size for one 'write' operation
 */
#if defined(TOUCHCOMM_VERSION_2)
#ifdef CONFIG_TOUCHSCREEN_SYNA_TCM2_SPI
#define RD_CHUNK_SIZE (4096)
#define WR_CHUNK_SIZE (2048)
#else
#define RD_CHUNK_SIZE (2048)
#define WR_CHUNK_SIZE (1024)
#endif
#else
#define RD_CHUNK_SIZE (2048)
#define WR_CHUNK_SIZE (1024)
#endif


/*
 * Definitions of hardware interface
 */

/* Hardware Data for bus transaction */
struct syna_hw_bus_data {
	/* bus clock rate */
	unsigned int frequency_hz;
	/* parameters for i2c */
	unsigned int i2c_addr;
	/* parameters for spi */
	unsigned int spi_mode;
	unsigned int spi_byte_delay_us;
	unsigned int spi_block_delay_us;
	/* mutex to protect the i/o */
	struct mutex io_mutex;
	/* option for io switch */
	int switch_gpio;
	int switch_state;
};

/* Hardware Data for ATTN or interrupt control */
struct syna_hw_attn_data {
	/* parameters */
	int irq_gpio;
	int irq_on_state;
	unsigned long irq_flags;
	int irq_id;
	bool irq_enabled;
	/* mutex to protect the irq control */
	struct mutex irq_en_mutex;
};

/* Hardware Data for reset pin */
struct syna_hw_rst_data {
	/* parameters */
	int reset_gpio;
	int reset_on_state;
	unsigned int reset_delay_ms;
	unsigned int reset_active_ms;
};

/* Hardware Data for power control */
enum power_supply {
	PSU_REGULATOR = 1,
	PSU_GPIO,
};

struct power_setup {
	int control;
	const char *regulator_name;
	void *regulator_dev;
	int gpio;
	int voltage;
	unsigned int power_on_delay_ms;
	unsigned int power_off_delay_ms;
};
struct syna_hw_pwr_data {
	/* VDD (VDD_3V3) */
	struct power_setup vdd;
	/* IO VDD (VDD_1V8) */
	struct power_setup vio;
	/* indicate the state of powering on */
	int power_on_state;
	/* the delay time after the completion of power sequence */
	unsigned int power_delay_ms;
};

/* Product specific data */
struct product_specific {
	struct tcm_timings timings;
};

struct syna_tvm {
	bool qts_en;
	struct mutex tui_transition_lock;
};

/* Abstractions of hardware-specific interface */
struct syna_hw_interface {
	/* pointers to the target platform */
	void *pdev;
	struct platform_device *platform_device;

	/* Hardware abstraction interface linked to tcm/ core lib. */
	struct tcm_hw_platform hw_platform;

	/* Hardware resources */
	struct syna_hw_bus_data bdata_io;
	struct syna_hw_attn_data bdata_attn;
	struct syna_hw_rst_data bdata_rst;
	struct syna_hw_pwr_data bdata_pwr;

	/* Product specific data */
	struct product_specific product;

	/* for trusted touch */
	struct syna_tvm bdata_tvm;

	/* Implementation of power on/off operation */
	int (*ops_power_on)(bool on);

	/* Implementation of hardware reset operation */
	void (*ops_hw_reset)(void);

#if defined(CONFIG_TOUCHSCREEN_SYNA_TCM2_DEBUG_MSG)
	int debug_trace;
#endif
};


/*
 * Hardware Interface Registration
 */

/*
 * Register the hardware interface module.
 *
 * param
 *    void
 *
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
int syna_hw_interface_bind(void);

/*
 * Remove the hardware interface module.
 *
 * param
 *    void
 *
 * return
 *    void.
 */
void syna_hw_interface_unbind(void);


#endif /* end of _SYNAPTICS_TCM2_PLATFORM_H_ */
