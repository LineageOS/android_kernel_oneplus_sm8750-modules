/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Synaptics TouchComm C library
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
 * This file declares the essential hardware utility functions in abstraction.
 *
 * Platform-specific implementations must be provided and implemented separately in the shell layer.
 */

#ifndef _SYNAPTICS_TOUCHCOM_PLATFORM_H_
#define _SYNAPTICS_TOUCHCOM_PLATFORM_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "./../syna_tcm2_runtime.h"


/* Types for Lower-Level Bus */
enum bus_connection {
	BUS_TYPE_NONE,
	BUS_TYPE_I2C,
	BUS_TYPE_SPI,
	BUS_TYPE_I3C,
};


/* Structure of Timing Configuration  */
struct tcm_timings {
	/* timeout time of command processing */
	int cmd_timeout_ms;
	/* time interval to process command by polling */
	int cmd_polling_ms;
	/* bus turnaround time */
	int cmd_turnaround_us;
	/* command retry delay */
	int cmd_retry_ms;
	/* timings for flash operations (0: erase / 1: write / 2: read) */
	int flash_ops_delay_us[3];
	/* time delay for firmware mode switching */
	int fw_switch_delay_ms;
	/* time delay after issuing a reset */
	int reset_delay_ms;
};

/*
 * Hardware Platform Abstraction Layer
 *
 * The structure contains the hardware-specific implementations on the target platform.
 */
struct tcm_hw_platform {
	/* pointer to the target platform */
	void *device;
	/* indicate the bus interface enumerated as the bus_connection */
	unsigned char type;
	/* capability of I/O chunk size */
	unsigned int rd_chunk_size;
	unsigned int wr_chunk_size;

	/* enable the support of interrupt or attention (ATTN)
	 * set to 'true' if the platform supports ATTN notifications;
	 * otherwise, set to 'false' by default.
	 */
	bool support_attn;

	/* indicates the extra data alignment on the target platform */
	bool alignment_enabled;
	unsigned int alignment_base;
	unsigned int alignment_boundary;

	/* abstraction to implement the data read transaction
	 *
	 * param
	 *    [ in] hw:       pointer to the hardware platform
	 *    [out] rd_data:  buffer for storing data retrieved from device
	 *    [ in] rd_len:   length of reading data in bytes
	 *
	 * return
	 *    0 or positive value in case of success, a negative value otherwise.
	 */
	int (*ops_read_data)(struct tcm_hw_platform *hw,
		unsigned char *rd_data, unsigned int rd_len);

	/* abstraction to implement the data write transaction
	 *
	 * param
	 *    [ in] hw:       pointer to the hardware platform
	 *    [ in] wr_data:  data to write
	 *    [ in] wr_len:   length of written data in bytes
	 *
	 * return
	 *    0 or positive value in case of success, a negative value otherwise.
	 */
	int (*ops_write_data)(struct tcm_hw_platform *hw,
		unsigned char *wr_data, unsigned int wr_len);

#ifdef TOUCHCOMM_VERSION_2
	/* abstraction to write and then retrieve data in a single transaction
	 *
	 * param
	 *    [ in] hw:       pointer to the hardware platform
	 *    [ in] wr_data:  data to write
	 *    [ in] wr_len:   length of written data in bytes
	 *    [out] rd_data:  buffer for storing data retrieved from device
	 *    [ in] rd_len:   number of bytes retrieved from device
	 *    [ in] turnaround_bytes:  number of bytes for bus turnaround
	 *
	 * return
	 *    0 or positive value in case of success, a negative value otherwise.
	 */
	int (*ops_write_then_read_data)(struct tcm_hw_platform *hw,
		unsigned char *wr_data, unsigned int wr_len, unsigned char *rd_data,
		unsigned int rd_len, unsigned int turnaround_bytes);
#endif
	/* abstraction to wait for the ATTN assertion
	 *
	 * param
	 *    [ in] hw:       pointer to the hardware platform
	 *    [ in] timeout:  timeout time waiting for the assertion
	 *
	 * return
	 *    0 in case of timeout,
	 *    positive value in case of ATTN asserted,
	 *    a negative value otherwise.
	 */
	int (*ops_wait_for_attn)(struct tcm_hw_platform *hw, int timeout);

	/* abstraction to enable/disable the ATTN
	 *
	 * param
	 *    [ in] hw:  pointer to the hardware platform
	 *    [ in] en: '1' for enabling, and '0' for disabling
	 *
	 * return
	 *    0 if nothing to do, positive value in case of success, a negative value otherwise.
	 */
	int (*ops_enable_attn)(struct tcm_hw_platform *hw, bool en);

	/* abstraction to return the current status of ATTN signal
	 *
	 * param
	 *    [ in] hw:  pointer to the hardware platform
	 *
	 * return
	 *    0 in case of the low level of ATTN pin, 1 otherwise.
	 */
	unsigned int (*ops_get_attn_level)(struct tcm_hw_platform *hw);

	/* abstraction to trigger a hardware reset
	 *
	 * param
	 *    [ in] hw:  pointer to the hardware platform
	 *
	 * return
	 *    0 or positive value in case of success, a negative value otherwise.
	 */
	int (*ops_hw_reset)(struct tcm_hw_platform *hw);

};
/* end of structure tcm_hw_platform */


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* end of _SYNAPTICS_TOUCHCOM_PLATFORM_H_ */
