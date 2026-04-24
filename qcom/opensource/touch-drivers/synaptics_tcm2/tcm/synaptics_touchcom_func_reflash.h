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
 * This file declares relevant functions and structures for Bootloader.
 */

#ifndef _SYNAPTICS_TOUCHCOM_REFLASH_FUNCS_H_
#define _SYNAPTICS_TOUCHCOM_REFLASH_FUNCS_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "synaptics_touchcom_core_dev.h"
#include "synaptics_touchcom_image_parsing.h"

#define BOOT_CONFIG_SLOT_SIZE 8
#define BOOT_CONFIG_SLOTS 16

/*
 * Requested Operations for Reflash
 */
enum update_operation {
	UPDATE_NONE = 0x0000,
	UPDATE_CONFIG = 0x0001,
	UPDATE_CUSTOMER_SERIALIZATION = 0x0002,
	UPDATE_LOCKDOWN = 0x0004,
	UPDATE_FIRMWARE = 0x0008,
	UPDATE_FIRMWARE_AND_CONFIG = 0x0009,
};

/*
 * Standard API Definitions
 */

/*
 *  The extended entry function to perform firmware update by using the firmware image file.
 *
 *  Please be noted there is no file parsing and ID comparison implemented in this function.
 *  Caller must provide the parsed image data before calling.
 *
 * param
 *    [ in] tcm_dev:                pointer to TouchComm device
 *    [ in] image:                  parsed data from image file
 *    [ in] op:                     requested operation
 *                                    use '0' in default, that updates both firmware and config
 *                                    otherwise, refer to the enumerated value of update_operation
 *    [ in] flash_erase_delay_ms:   set up the ms delay time to
 *                                  wait for the completion of flash access
 *                                    for polling,     set a positive value;
 *                                    for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 *    [ in] flash_write_delay_us:   set up the us delay time to
 *                                  wait for the completion of flash access
 *                                    for polling,     set a positive value;
 *                                    for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 *    [ in] fw_switch_delay_ms:     set up the ms delay time to
 *                                  wait for the completion of firmware switching
 *                                    for polling,     set a positive value;
 *                                    for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 *    [ in] use_opt:                perform the optimized flash write operation if supported
 *                                    set 'True' if no quite sure.
 *
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
int syna_tcm_do_fw_update_ex(struct tcm_dev *tcm_dev, struct image_info *image,
	unsigned int op, unsigned int flash_erase_delay_ms, unsigned int flash_write_delay_us,
	unsigned int fw_switch_delay_ms, bool use_opt);

/*
 *  The basic entry function to perform firmware update by using the firmware image file.
 *
 *  In this function, it intends to do file parsing and apply the default ID comparison method
 *  to determine the operation for reflash.
 *
 *  If requiring to update the specific partition, please use syna_tcm_do_fw_update_ex() instead.
 *
 * param
 *    [ in] tcm_dev:                pointer to TouchComm device
 *    [ in] image:                  binary data to write
 *    [ in] image_size:             size of data array
 *    [ in] flash_delay_settings:   set up the us delay time to
 *                                  wait for the completion of flash access for polling,
 *                                    set a value formatted with [erase ms | write us];
 *                                    for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 *    [ in] force_reflash:         '1' to do reflash anyway
 *                                 '0' to compare ID info before doing reflash.
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
int syna_tcm_do_fw_update(struct tcm_dev *tcm_dev, const unsigned char *image,
	unsigned int image_size, unsigned int delay_setting, bool force_reflash);


/*
 * API to access custom data
 */

 /*
  *  Request to read out data from the appointed address in the flash memory.
  *
  * param
  *    [ in] tcm_dev:       pointer to TouchComm device
  *    [ in] address:       specify the flash address to read
  *    [ in] length:        specify the size to read
  *    [out] rd_data:       buffer storing the retrieved data
  *    [ in] resp_reading:  method to read in the response
  *                          a positive value presents the us time delay for the processing
  *                          of each WORDs in the flash to read;
  *                          or, set '0' or 'RESP_IN_ATTN' for ATTN driven
  * return
  *    0 or positive value in case of success, a negative value otherwise.
  */
int syna_tcm_read_flash_address(struct tcm_dev *tcm_dev, unsigned int address,
	unsigned int length, struct tcm_buffer *rd_data, unsigned int resp_reading);

/*
 *  Request to read out data block from the flash memory.
 *
 * param
 *    [ in] tcm_dev:       pointer to TouchComm device
 *    [ in] area:          flash area to read
 *    [out] rd_data:       buffer storing the retrieved data
 *    [ in] resp_reading:  method to read in the response
 *                          a positive value presents the us time delay for the processing
 *                          of each WORDs in the flash to read;
 *                          or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
int syna_tcm_read_flash_area(struct tcm_dev *tcm_dev, enum flash_area area,
	struct tcm_buffer *rd_data, unsigned int resp_reading);

/*
 *   Request to update the lockdown config.
 *
 * param
 *    [ in] tcm_dev:               pointer to TouchComm device
 *    [ in] image:                 parsed data from image file
 *    [ in] flash_write_delay_us:  delay time to wait for the completion of flash write
 *                                   for polling,     set a positive value;
 *                                   for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 * return
 *    positive value in case of success,
 *    0 if device has been locked already,
 *    a negative value otherwise.
 */
int syna_tcm_update_lockdown_config(struct tcm_dev *tcm_dev, struct image_info *image,
	unsigned int flash_write_delay_us);
/*
 *   Request to write custom serialization area
 *
 * param
 *    [ in] tcm_dev:              pointer to TouchComm device
 *    [ in] cs_data:              custom serialization data to write
 *    [ in] cs_data_size:         size of requested data
 *    [ in] cs_offset:            the specified offset of address
 *                                set '0' in default
 *    [ in] flash_write_delay_us: delay time to wait for the completion of flash write
 *                                   for polling,     set a positive value;
 *                                   for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 * return
 *    positive value in case of success,
 *    0 if device has been locked already,
 *    a negative value otherwise.
 */
int syna_tcm_update_cs_config(struct tcm_dev *tcm_dev, unsigned char *cs_data,
	unsigned int cs_data_size, unsigned int cs_offset, unsigned int flash_write_delay_us);
/*
 *  Request to read custom serialization data
 *
 * param
 *    [ in] tcm_dev:               pointer to TouchComm device
 *    [out] cs_data:               buffer storing the custom serialization data
 *    [ in] cs_data_size:          size to read
 *    [ in] cs_offset:             the specified offset to read
 *                                 set '0' in default
 *    [ in] flash_read_delay_us:   delay time to wait for the completion of flash read
 *                                   for polling,     set a positive value;
 *                                   for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 *
 * return
 *    positive value in case of success,
 *    0 if device has been locked already,
 *    a negative value otherwise.
 */
int syna_tcm_read_cs_data(struct tcm_dev *tcm_dev, unsigned char *cs_data,
	unsigned int cs_data_size, unsigned int cs_offset, unsigned int flash_write_delay_us);
/*
 *  Request to write custom MTP data
 *
 * param
 *    [ in] tcm_dev:               pointer to TouchComm device
 *    [ in] mtp_data:              customer data to write
 *    [ in] mtp_data_size:         size of customer data
 *    [ in] mtp_offset:            the specified offset of address
 *                                 set '0' in default
 *    [ in] flash_write_delay_us:  delay time to wait for the completion of flash write
 *                                   for polling,     set a positive value;
 *                                   for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 *
 * return
 *    positive value in case of success,
 *    0 if device has been locked already,
 *    a negative value otherwise.
 */
int syna_tcm_update_mtp_data(struct tcm_dev *tcm_dev, unsigned char *mtp_data,
	unsigned int mtp_data_size, unsigned int mtp_offset, unsigned int flash_write_delay_us);
/*
 *  Request to read custom MTP data
 *
 * param
 *    [ in] tcm_dev:               pointer to TouchComm device
 *    [out] mtp_data:              buffer storing the customer data
 *    [ in] mtp_data_size:         size to read
 *    [ in] mtp_offset:            the specified offset to read
 *                                 set '0' in default
 *    [ in] flash_read_delay_us:   delay time to wait for the completion of flash read
 *                                   for polling,     set a positive value;
 *                                   for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 *
 * return
 *    positive value in case of success,
 *    0 if device has been locked already,
 *    a negative value otherwise.
 */
int syna_tcm_read_mtp_data(struct tcm_dev *tcm_dev, unsigned char *mtp_data,
	unsigned int mtp_data_size, unsigned int mtp_offset, unsigned int flash_read_delay_us);

/*
 *   Request to erase the MTP (Multi Time Programming) area
 *
 * param
 *    [ in] tcm_dev:               pointer to TouchComm device
 *    [ in] flash_erase_delay_us:  delay time to wait for the completion of flash erase
 *                                   for polling,     set a positive value;
 *                                   for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 * return
 *    positive value in case of success,
 *    0 if device has been locked already,
 *    a negative value otherwise.
 */
int syna_tcm_erase_mtp_data(struct tcm_dev *tcm_dev, unsigned int flash_erase_delay_us);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* end of _SYNAPTICS_TOUCHCOM_REFLASH_FUNCS_H_ */
