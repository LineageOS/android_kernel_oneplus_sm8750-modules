// SPDX-License-Identifier: GPL-2.0-or-later
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
 * This file implements the fw reflash relevant functions.
 */

#include "synaptics_touchcom_core_dev.h"
#include "synaptics_touchcom_func_base.h"
#include "synaptics_touchcom_func_reflash.h"

/*
 * Parameters for the bootloader firmware
 */

/* Main structure for reflash */
struct tcm_reflash_data_blob {
	unsigned int op;
	/* pointer to binary data of the image file */
	const unsigned char *image;
	unsigned int image_size;
	/* pointer to parsed data of the image file */
	struct image_info *image_info;
	/* pointer to boot info */
	struct tcm_boot_info *boot_info;
	/* standard information for flash access */
	unsigned int page_size;
	unsigned int write_block_size;
	unsigned int max_write_payload_size;
	unsigned int boot_config_blocks;
	unsigned int boot_config_start_addr;
	unsigned int mtp_config_start_addr;
	/* temporary buffer during the reflash */
	struct tcm_buffer out;
	/* flag to indicate the optimized write support */
	bool support_optimized_write;
	/* number of bytes to update in total  */
	unsigned int total_bytes_to_update;
	unsigned int bytes_updated;
};

/*
 *  Compare the ID information between device and the image file,
 *  and then determine the operation to work.
 *
 * param
 *    [ in] tcm_dev:     pointer to TouchComm device
 *    [ in] image_info:  parsed data from image file
 *
 * return
 *    one of the following enumerated values being used to indicate the target to update
 *    in case of success
 *
 *       - UPDATE_NONE                 no needs to update
 *       - UPDATE_FIRMWARE_AND_CONFIG  update the both firmware code area and the config area
 *       - UPDATE_CONFIG               update the firmware config area only
 *
 *    otherwise, a negative value.
 */
static int syna_tcm_compare_image_id_info(struct tcm_dev *tcm_dev,
	struct image_info *image_info)
{
	int op = (int)UPDATE_NONE;
	unsigned int idx;
	unsigned int image_fw_id;
	unsigned int device_fw_id;
	unsigned char *image_config_id;
	unsigned char *device_config_id;
	struct app_config_header *header;
	struct app_config_header_v2 *header_v2;
	const unsigned char *app_config_data;
	struct area_block *app_config;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return UPDATE_NONE;
	}

	if (!image_info) {
		LOGE("Invalid parsed image data\n");
		return UPDATE_NONE;
	}

	app_config = &image_info->data[AREA_APP_CONFIG];

	if (app_config->size <
		MIN(sizeof(struct app_config_header),
			sizeof(struct app_config_header_v2))) {
		LOGE("Invalid application config in image file\n");
		return UPDATE_NONE;
	}

	app_config_data = app_config->data;
	if (tcm_dev->protocol == TOUCHCOMM_V2) {
		header_v2 = (struct app_config_header_v2 *)app_config_data;
		image_fw_id = syna_pal_le4_to_uint(header_v2->build_id);
		image_config_id = header_v2->customer_config_id;
	} else {
		header = (struct app_config_header *)app_config_data;
		image_fw_id = syna_pal_le4_to_uint(header->build_id);
		image_config_id = header->customer_config_id;
	}

	device_fw_id = tcm_dev->packrat_number;
	device_config_id = tcm_dev->app_info.customer_config_id;

	LOGN("Device firmware ID: %d, image build id: %d\n", device_fw_id, image_fw_id);

	if (image_fw_id != device_fw_id) {
		LOGN("Image build ID and device fw ID mismatched\n");
		op = (int)UPDATE_FIRMWARE_AND_CONFIG;
		goto exit;
	}

	for (idx = 0; idx < MAX_SIZE_CONFIG_ID; idx++) {
		if (image_config_id[idx] != device_config_id[idx]) {
			LOGN("Different Config ID\n");
			op = (int)UPDATE_CONFIG;
			goto exit;
		}
	}
	op = (int)UPDATE_NONE;

exit:
	switch ((enum update_operation)op) {
	case UPDATE_FIRMWARE_AND_CONFIG:
		LOGN("Update firmware and config\n");
		break;
	case UPDATE_CONFIG:
		LOGN("Update config only\n");
		break;
	case UPDATE_NONE:
	default:
		LOGN("No need to do reflash\n");
		break;
	}

	return (int)op;
}

/*
 *  Prepare and run the bootloader firmware.
 *
 * param
 *    [ in] tcm_dev:         pointer to TouchComm device
 *    [out] reflash_data:    data blob for reflash
 *    [ in] resp_reading:    method to read in the response
 *                            a positive value presents the ms time delay for polling;
 *                            or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 *    [ in] fw_switch_delay: method to switch the fw mode
 *                            a positive value presents the ms time delay for polling;
 *                            or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_set_up_flash_access(struct tcm_dev *tcm_dev,
	struct tcm_reflash_data_blob *reflash_data, unsigned int resp_reading,
	unsigned int fw_switch_delay)
{
	int retval;
	unsigned int temp;
	struct tcm_identification_info id_info;
	unsigned int wr_chunk;
	int idx;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	LOGI("Set up flash access\n");

	retval = syna_tcm_identify(tcm_dev, &id_info, resp_reading);
	if (retval < 0) {
		LOGE("Fail to do identification\n");
		return retval;
	}

	/* switch to bootloader mode */
	if (IS_APP_FW_MODE(id_info.mode)) {
		LOGI("Prepare to enter bootloader mode\n");

		retval = syna_tcm_switch_fw_mode(tcm_dev,
				MODE_BOOTLOADER,
				fw_switch_delay);
		if (retval < 0) {
			LOGE("Fail to enter bootloader mode\n");
			return retval;
		}
	}

	if (!IS_BOOTLOADER_MODE(tcm_dev->dev_mode)) {
		LOGE("Fail to enter bootloader mode (current: 0x%x)\n",
			tcm_dev->dev_mode);
		return retval;
	}

	for (idx = 0; idx < (int)sizeof(tcm_dev->id_info.part_number); idx++) {
		if (tcm_dev->id_info.part_number[idx] == 0x3A) {
			if ((tcm_dev->id_info.part_number[idx + 3] & 0x20) == 0x20)
				reflash_data->support_optimized_write = true;
		}
	}

	reflash_data->boot_info = &tcm_dev->boot_info;

	/* get boot info to set up the flash access */
	retval = syna_tcm_get_boot_info(tcm_dev, reflash_data->boot_info, resp_reading);
	if (retval < 0) {
		LOGE("Fail to get boot info");
		return retval;
	}

	LOGI("Boot info version: %d\n", reflash_data->boot_info->version);

	wr_chunk = tcm_dev->max_wr_size;

	temp = reflash_data->boot_info->write_block_size_words;
	reflash_data->write_block_size = temp * 2;

	LOGI("Write block size: %d (words size: %d)\n",
		reflash_data->write_block_size, temp);

	temp = syna_pal_le2_to_uint(reflash_data->boot_info->erase_page_size_words);
	reflash_data->page_size = temp * 2;

	LOGI("Erase page size: %d (words size: %d)\n",
		reflash_data->page_size, temp);

	temp = syna_pal_le2_to_uint(reflash_data->boot_info->max_write_payload_size);
	reflash_data->max_write_payload_size = temp;

	LOGI("Max write flash data size: %d\n",
		reflash_data->max_write_payload_size);

	if ((wr_chunk != 0) && (reflash_data->write_block_size > wr_chunk))
		reflash_data->write_block_size = wr_chunk;

	if (reflash_data->write_block_size == 0) {
		LOGE("Invalid write block size %d\n",
			reflash_data->write_block_size);
		return -ERR_INVAL;
	}

	if (reflash_data->page_size == 0) {
		LOGE("Invalid erase page size %d\n", reflash_data->page_size);
		return -ERR_INVAL;
	}

	if (reflash_data->boot_info->version == 3) {
		reflash_data->mtp_config_start_addr =
			reflash_data->boot_info->v3.mtp_config_start_page * reflash_data->page_size;
		LOGI("MTP config start address :%d (%X)\n",
			reflash_data->mtp_config_start_addr, reflash_data->mtp_config_start_addr);
		LOGI("MTP config start page: %d, num of pages: %d\n",
			reflash_data->boot_info->v3.mtp_config_start_page,
			reflash_data->boot_info->v3.mtp_config_num_pages);
	}

	return 0;
}

/*
 *  Process the bootloader command message.
 *
 * param
 *    [ in] tcm_dev:        pointer to TouchComm device
 *    [ in] command:        given command code
 *    [ in] payload:        payload data, if any
 *    [ in] payload_len:    length of payload data
 *    [ in] resp_reading:   delay time to get the response of command
 *
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_reflash_send_command(struct tcm_dev *tcm_dev,
	unsigned char command, unsigned char *payload,
	unsigned int payload_len, unsigned int resp_reading)
{
	int retval = 0;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!IS_BOOTLOADER_MODE(tcm_dev->dev_mode)) {
		LOGE("Device is not in BL mode, 0x%x\n", tcm_dev->dev_mode);
		retval = -ERR_INVAL;
	}

	if (resp_reading == CMD_RESPONSE_IN_ATTN) {
		if (!tcm_dev->hw->support_attn) {
			resp_reading = tcm_dev->msg_data.command_polling_time;
			LOGN("No support of IRQ control, use polling mode instead, interval:%d\n",
				resp_reading);
		}
	}

	LOGD("Command 0x%x, length:%d\n", command, payload_len);

	retval = tcm_dev->write_message(tcm_dev,
			command,
			payload,
			payload_len,
			NULL,
			resp_reading);
	if (retval < 0) {
		LOGE("Fail to send command 0x%02x\n", command);
		goto exit;
	}

exit:
	return retval;
}

/*
 *  Check whether the TOOL BOOT CONFIG area is valid.
 *
 * param
 *    [ in] boot_config:   block data of boot_config from image file
 *    [ in] reflash_data:  data blob for reflash
 *    [ in] block_size:    max size of write block
 *
 * return
 *    '0' represent no need to do reflash;
 *    the positive value means a valid partition to update;
 *    otherwise, negative value on error.
 */
static int syna_tcm_check_flash_tool_boot_config(struct area_block *config,
	struct tcm_reflash_data_blob *reflash_data, unsigned int block_size)
{
	unsigned int start_block = 0;
	unsigned int image_addr;
	unsigned int device_addr;
	struct tcm_boot_info *boot_info;

	if (!config) {
		LOGE("Invalid boot_config block data\n");
		return -ERR_INVAL;
	}

	if (config->size < BOOT_CONFIG_SLOT_SIZE) {
		LOGE("No valid BOOT_CONFIG size, %d, in image file\n",
			config->size);
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid boot_info\n");
		return -ERR_INVAL;
	}

	boot_info = reflash_data->boot_info;

	if ((boot_info->version != 1) && (boot_info->version != 3)) {
		LOGE("Invalid boot info version %d\n", boot_info->version);
		return -ERR_INVAL;
	}

	image_addr = config->flash_addr;

	LOGD("Boot Config address in image file: 0x%x\n", image_addr);

	if (boot_info->version == 1)
		start_block = syna_pal_le2_to_uint(boot_info->v1.boot_or_otp_config_start_block);
	else if (boot_info->version == 3)
		start_block = syna_pal_le2_to_uint(boot_info->v3.boot_or_otp_config_start_block);

	device_addr = start_block * block_size;

	LOGD("Boot Config address in device: 0x%x\n", device_addr);

	if (image_addr != device_addr) {
		LOGE("Boot Config address mismatched, device: 0x%x, image file: 0x%x\n",
			device_addr, image_addr);
		return -ERR_INVAL;
	}

	return config->size;
}
/*
 *  Check whether the APP CONFIG area is valid.
 *
 * param
 *    [ in] app_config:      block data of app_config from image file
 *    [ in] app_info:        data of application info
 *    [ in] block_size:      max size of write block
 *
 * return
 *    '0' represent no need to do reflash;
 *    the positive value means a valid partition to update;
 *    otherwise, negative value on error.
 */
static int syna_tcm_check_flash_app_config(struct area_block *app_config,
	struct tcm_application_info *app_info, unsigned int block_size)
{
	unsigned int temp;
	unsigned int image_addr;
	unsigned int image_size;
	unsigned int device_addr;
	unsigned int device_size;

	if (!app_config) {
		LOGE("Invalid app_config block data\n");
		return -ERR_INVAL;
	}

	if (!app_info) {
		LOGE("Invalid app_info\n");
		return -ERR_INVAL;
	}

	if (app_config->size == 0) {
		LOGD("No APP_CONFIG in image file\n");
		return 0;
	}

	image_addr = app_config->flash_addr;
	image_size = app_config->size;

	LOGD("App Config address in image file: 0x%x, size: %d\n",
		image_addr, image_size);

	temp = syna_pal_le2_to_uint(app_info->app_config_start_write_block);
	device_addr = temp * block_size;
	device_size = syna_pal_le2_to_uint(app_info->app_config_size);

	LOGD("App Config address in device: 0x%x, size: %d\n",
		device_addr, device_size);

	if (device_addr == 0 && device_size == 0)
		return image_size;

	if (image_addr != device_addr) {
		LOGW("App Config address mismatch, image:0x%x, dev:0x%x\n",
			image_addr, device_addr);
		return -ERR_INVAL;
	}

	if (image_size != device_size)
		LOGW("App Config address size mismatch, image:%d, dev:%d\n",
			image_size, device_size);

	return image_size;
}
/*
 *  Check whether the APP CODE area is valid.
 *
 * param
 *    [ in] app_code:      block data of app_code from image file
 *
 * return
 *    '0' represent no need to do reflash;
 *    the positive value means a valid partition to update;
 *    otherwise, negative value on error.
 */
static int syna_tcm_check_flash_app_code(struct area_block *app_code)
{
	if (!app_code) {
		LOGE("Invalid app_code block data\n");
		return -ERR_INVAL;
	}

	if (app_code->size == 0) {
		LOGD("No %s in image file\n", FLASH_PARTITION_ID_STR(app_code->id));
		return -ERR_INVAL;
	}

	return app_code->size;
}
/*
 *  Check whether the APP PROD_TEST area is valid.
 *
 * param
 *    [ in] prod_test:  block data of app_prod_test from image file
 *
 * return
 *    '0' represent no need to do reflash;
 *    the positive value means a valid partition to update;
 *    otherwise, negative value on error.
 */
static int syna_tcm_check_flash_app_prod_test(struct area_block *prod_test)
{
	if (!prod_test) {
		LOGE("Invalid app_prod_test block data\n");
		return -ERR_INVAL;
	}

	if (prod_test->size == 0) {
		LOGD("No %s in image file\n", FLASH_PARTITION_ID_STR(prod_test->id));
		return 0;
	}

	return prod_test->size;
}
/*
 *  Helper to ensure a valid data partition.
 *
 * param
 *    [ in] tcm_dev:      pointer to TouchComm device
 *    [ in] reflash_data: data blob for reflash
 *    [ in] block:        target block area to check
 *
 * return
 *    '0' represent no need to do reflash;
 *    the positive value means a valid partition to update;
 *    otherwise, negative value on error.
 */
static int syna_tcm_check_flash_block(struct tcm_dev *tcm_dev,
	struct tcm_reflash_data_blob *reflash_data, struct area_block *block)
{
	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	if (!block) {
		LOGE("Invalid block data\n");
		return -ERR_INVAL;
	}

	switch (block->id) {
	case AREA_APP_CODE:
		return syna_tcm_check_flash_app_code(block);
	case AREA_APP_CONFIG:
		return syna_tcm_check_flash_app_config(block,
			&tcm_dev->app_info, reflash_data->write_block_size);
	case AREA_TOOL_BOOT_CONFIG:
		return syna_tcm_check_flash_tool_boot_config(block,
			reflash_data, reflash_data->write_block_size);
	case AREA_PROD_TEST:
		return syna_tcm_check_flash_app_prod_test(block);
	default:
		return 0;
	}

	return 0;
}
/*
 *  Process bootloader command to read data from the flash.
 *
 *  Reads to the protected bootloader code or application code areas will
 *  read as 0.
 *
 * param
 *    [ in] tcm_dev:      pointer to TouchComm device
 *    [ in] address:      the address in flash memory to read
 *    [out] rd_data:      data retrieved
 *    [ in] rd_len:       length of data to be read
 *    [ in] rd_delay_us:  a short delay after the command executed
 *                        set 'DEFAULT_FLASH_READ_DELAY' to use default,
 *                        which is 10 us;
 *                        set '0' or 'RESP_IN_ATTN' to select ATTN-driven.
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_read_flash(struct tcm_dev *tcm_dev, unsigned int address,
	unsigned char *rd_data, unsigned int rd_len, unsigned int rd_delay_us)
{
	int retval = 0;
	unsigned int length_words, length_bytes;
	unsigned int flash_addr_words;
	unsigned char out[6] = { 0 };
	unsigned int resp_delay;
	unsigned int offset;
	unsigned int iterations, idx;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!rd_data) {
		LOGE("Invalid rd_data buffer\n");
		return -ERR_INVAL;
	}

	if (address == 0 || rd_len == 0) {
		LOGE("Invalid flash address and length\n");
		retval = -ERR_INVAL;
		goto exit;
	}

	length_bytes = rd_len;
	if ((tcm_dev->protocol == TOUCHCOMM_V2) && (length_bytes > tcm_dev->max_rd_size))
		length_bytes = tcm_dev->max_rd_size - (MESSAGE_HEADER_SIZE + TCM_MSG_CRC_LENGTH);

	iterations = syna_pal_int_division(rd_len, length_bytes, true);
	iterations = iterations == 0 ? 1 : iterations;

	offset = 0;
	for (idx = 0; idx < iterations; idx++) {
		address += offset;

		if (rd_len - offset < length_bytes)
			length_bytes = rd_len - offset;

		length_words = length_bytes / 2;
		flash_addr_words = address / 2;

		LOGD("Flash address: 0x%x (words: 0x%x), size: %d (words: %d)\n",
			address, flash_addr_words, length_bytes, length_words);

		if (rd_delay_us == CMD_RESPONSE_IN_ATTN) {
			LOGD("xfer: %d, delay: ATTN-driven\n", length_words);
			resp_delay = CMD_RESPONSE_IN_ATTN;
		} else {
			resp_delay = (rd_delay_us * length_words) / 1000;
			LOGD("xfer: %d, delay: %d ms\n", length_words, resp_delay);
		}

		out[0] = (unsigned char)flash_addr_words;
		out[1] = (unsigned char)(flash_addr_words >> 8);
		out[2] = (unsigned char)(flash_addr_words >> 16);
		out[3] = (unsigned char)(flash_addr_words >> 24);
		out[4] = (unsigned char)length_words;
		out[5] = (unsigned char)(length_words >> 8);

		retval = syna_tcm_reflash_send_command(tcm_dev,
			CMD_READ_FLASH,
			out,
			sizeof(out),
			resp_delay);
		if (retval < 0) {
			LOGE("Fail to read flash data from addr 0x%x, size %d\n",
				address, length_bytes);
			goto exit;
		}
#ifdef HAS_PROGRESS_FEEDBACK
		if (tcm_dev->cb_progress_callback)
			tcm_dev->cb_progress_callback(tcm_dev->resp_buf.data_length, rd_len);
#endif

		if (tcm_dev->resp_buf.data_length != length_bytes) {
			LOGE("Fail to read requested length %d (length read in: %d)\n",
				length_bytes, tcm_dev->resp_buf.data_length);
			retval = -ERR_INVAL;
			goto exit;
		}

		retval = syna_pal_mem_cpy(&rd_data[offset],
			rd_len,
			tcm_dev->resp_buf.buf,
			tcm_dev->resp_buf.buf_size,
			tcm_dev->resp_buf.data_length);
		if (retval < 0) {
			LOGE("Fail to copy read data, size %d\n", tcm_dev->resp_buf.data_length);
			goto exit;
		}

		offset += tcm_dev->resp_buf.data_length;
	}

exit:
	return retval;
}

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
	unsigned int length, struct tcm_buffer *rd_data, unsigned int resp_reading)
{
	int retval = 0;
	struct tcm_reflash_data_blob reflash_data;
	unsigned char original_mode;
	unsigned int fw_switch_time;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!rd_data) {
		LOGE("Invalid data buffer\n");
		return -ERR_INVAL;
	}

	if (resp_reading == CMD_RESPONSE_IN_ATTN)
		fw_switch_time = resp_reading;
	else
		fw_switch_time = tcm_dev->fw_mode_switching_time;

	original_mode = tcm_dev->dev_mode;

	if (original_mode != MODE_BOOTLOADER) {
		retval = syna_tcm_set_up_flash_access(tcm_dev, &reflash_data,
			resp_reading, fw_switch_time);
		if (retval < 0) {
			LOGE("Fail to set up flash access\n");
			return retval;
		}
	}

	syna_tcm_buf_init(&reflash_data.out);

	/* read data from the specific flash address */
	if (length == 0)
		goto exit;

	retval = syna_tcm_buf_alloc(rd_data, length);
	if (retval < 0) {
		LOGE("Fail to allocate memory for rd_data buffer\n");
		goto exit;
	}

	retval = syna_tcm_read_flash(tcm_dev, address, rd_data->buf,
		length, resp_reading);
	if (retval < 0) {
		LOGE("Fail to read %d bytes data from address 0x%X\n", length, address);
		goto exit;
	}
	rd_data->data_length = length;

	retval = 0;

exit:
	if (original_mode == MODE_APPLICATION_FIRMWARE)
		syna_tcm_switch_fw_mode(tcm_dev, MODE_APPLICATION_FIRMWARE, resp_reading);

	syna_tcm_buf_release(&reflash_data.out);

	return retval;
}
/*
 *  Retrieve the BOOT CONFIG data area.
 *
 * param
 *    [ in] tcm_dev:      pointer to TouchComm device
 *    [ in] reflash_data: data blob for reflash
 *    [out] rd_data:      buffer used for storing the retrieved data
 *    [ in] resp_reading:  method to read in the response
 *                          a positive value presents the us time delay for the processing
 *                          of each WORDs in the flash to read;
 *                          or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_read_flash_boot_config(struct tcm_dev *tcm_dev,
	struct tcm_reflash_data_blob *reflash_data, struct tcm_buffer *rd_data,
	unsigned int resp_reading)
{
	int retval;
	unsigned int temp_addr = 0, tmp_blocks = 0;
	unsigned int addr = 0;
	unsigned int length = 0;
	struct tcm_boot_info *boot_info;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	if (!rd_data) {
		LOGE("Invalid read data buffer\n");
		return -ERR_INVAL;
	}

	if (IS_APP_FW_MODE(tcm_dev->dev_mode)) {
		LOGE("BOOT_CONFIG not available in app fw mode %d\n",
			tcm_dev->dev_mode);
		return -ERR_INVAL;
	}

	boot_info = reflash_data->boot_info;

	if ((boot_info->version != 1) && (boot_info->version != 3)) {
		LOGE("Invalid boot info version %d\n", boot_info->version);
		return -ERR_INVAL;
	}

	if (boot_info->version == 1) {
		temp_addr = syna_pal_le2_to_uint(boot_info->v1.boot_or_otp_config_start_block);
		tmp_blocks = syna_pal_le2_to_uint(boot_info->v1.boot_or_otp_config_size_blocks);
	} else if (boot_info->version == 3) {
		temp_addr = syna_pal_le2_to_uint(boot_info->v3.boot_or_otp_config_start_block);
		tmp_blocks = syna_pal_le2_to_uint(boot_info->v3.boot_or_otp_config_size_blocks);
	}

	addr = temp_addr * reflash_data->write_block_size;
	length = tmp_blocks * reflash_data->write_block_size;

	if (addr == 0 || length == 0) {
		LOGE("BOOT_CONFIG data area unavailable\n");
		retval = -ERR_INVAL;
		goto exit;
	}

	if (reflash_data->boot_config_start_addr != addr)
		reflash_data->boot_config_start_addr = addr;

	if (reflash_data->boot_config_blocks != tmp_blocks)
		reflash_data->boot_config_blocks = tmp_blocks;

	LOGI("Boot Config start addr: 0x%X (%d), blocks: %d, length: %d\n",
		reflash_data->boot_config_start_addr, reflash_data->boot_config_start_addr,
		reflash_data->boot_config_blocks, length);

	retval = syna_tcm_buf_alloc(rd_data, length);
	if (retval < 0) {
		LOGE("Fail to allocate memory for rd_data buffer\n");
		goto exit;
	}

	retval = syna_tcm_read_flash(tcm_dev, addr, rd_data->buf,
		length, resp_reading);
	if (retval < 0) {
		LOGE("Fail to read BOOT_CONFIG area (addr: 0x%x, length: %d)\n",
			addr, length);
		goto exit;
	}

	rd_data->data_length = length;

exit:
	return retval;
}
/*
 *  Retrieve the lockdown config area.
 *
 * param
 *    [ in] tcm_dev:      pointer to TouchComm device
 *    [ in] reflash_data: data blob for reflash
 *    [out] rd_data:      buffer used for storing the retrieved data
 *    [ in] resp_reading:  method to read in the response
 *                          a positive value presents the us time delay for the processing
 *                          of each WORDs in the flash to read;
 *                          or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_read_flash_boot_lockdown_config(struct tcm_dev *tcm_dev,
	struct tcm_reflash_data_blob *reflash_data, struct tcm_buffer *rd_data,
	unsigned int resp_reading)
{
	int retval;
	struct tcm_buffer boot_config;
	int offset = 0;
	int i;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	if (!rd_data) {
		LOGE("Invalid read data buffer\n");
		return -ERR_INVAL;
	}

	if (IS_APP_FW_MODE(tcm_dev->dev_mode)) {
		LOGE("Lockdown config not available in app fw mode %d\n",
			tcm_dev->dev_mode);
		return -ERR_INVAL;
	}

	syna_tcm_buf_init(&boot_config);

	retval = syna_tcm_read_flash_boot_config(tcm_dev, reflash_data,
		&boot_config, resp_reading);
	if (retval < 0) {
		LOGE("Fail to read boot config\n");
		goto exit;
	}

	retval = syna_tcm_buf_alloc(rd_data, BOOT_CONFIG_SLOT_SIZE);
	if (retval < 0) {
		LOGE("Fail to allocate memory for rd_data buffer\n");
		goto exit;
	}

	offset = boot_config.data_length - BOOT_CONFIG_SLOT_SIZE;
	for (i = 0; i < BOOT_CONFIG_SLOTS; i++) {
		if ((boot_config.buf[offset + 1] >> 7) == 1)
			break;

		offset -= BOOT_CONFIG_SLOT_SIZE;
	}

	retval = syna_pal_mem_cpy(rd_data->buf, rd_data->buf_size,
		&boot_config.buf[offset], BOOT_CONFIG_SLOT_SIZE, BOOT_CONFIG_SLOT_SIZE);
	if (retval < 0) {
		LOGE("Fail to copy data to rd_data buffer\n");
		goto exit;
	}

	rd_data->data_length = BOOT_CONFIG_SLOT_SIZE;

	offset = boot_config.data_length - BOOT_CONFIG_SLOT_SIZE;
	tcm_dev->is_locked = (boot_config.buf[offset + 1] & 0x80) ? true : false;

exit:
	syna_tcm_buf_release(&boot_config);

	return retval;
}
/*
 *  Retrieve the customer serialization data area.
 *
 * param
 *    [ in] tcm_dev:      pointer to TouchComm device
 *    [ in] reflash_data: data blob for reflash
 *    [out] rd_data:      buffer used for storing the retrieved data
 *    [ in] rd_size:      size to read, set '0' to retrieve the entire cs config data
 *    [ in] offset:       offset to read
 *    [ in] resp_reading:  method to read in the response
 *                          a positive value presents the us time delay for the processing
 *                          of each WORDs in the flash to read;
 *                          or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_read_flash_boot_cs_config(struct tcm_dev *tcm_dev,
	struct tcm_reflash_data_blob *reflash_data, struct tcm_buffer *rd_data,
	unsigned int rd_size, unsigned int cs_offset, unsigned int resp_reading)
{
	int retval;
	struct tcm_buffer cs_config;
	struct tcm_boot_info *boot_info;
	unsigned int temp;
	unsigned int addr = 0;
	int offset = 0;
	int i;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	if (!rd_data) {
		LOGE("Invalid read data buffer\n");
		return -ERR_INVAL;
	}

	if (IS_APP_FW_MODE(tcm_dev->dev_mode)) {
		LOGE("CS config not available in app fw mode %d\n",
			tcm_dev->dev_mode);
		return -ERR_INVAL;
	}

	boot_info = reflash_data->boot_info;

	if ((boot_info->version != 1) && (boot_info->version != 3)) {
		LOGE("Invalid boot info version %d\n", boot_info->version);
		return -ERR_INVAL;
	}

	syna_tcm_buf_init(&cs_config);

	if (rd_size == 0) {
		retval = syna_tcm_read_flash_boot_config(tcm_dev, reflash_data,
			&cs_config, resp_reading);
		if (retval < 0) {
			LOGE("Fail to read boot config\n");
			goto exit;
		}

		retval = syna_tcm_buf_alloc(rd_data, cs_config.data_length);
		if (retval < 0) {
			LOGE("Fail to allocate memory for rd_data buffer\n");
			goto exit;
		}

		/* skip the lockdown config */
		offset = cs_config.data_length - BOOT_CONFIG_SLOT_SIZE;
		for (i = 0; i < BOOT_CONFIG_SLOTS; i++) {
			if ((cs_config.buf[offset + 1] >> 7) == 1) {
				if ((cs_config.buf[offset - BOOT_CONFIG_SLOT_SIZE + 1] >> 7) == 0)
					break;
			}

			offset -= BOOT_CONFIG_SLOT_SIZE;
		}

		rd_size = offset;
	} else {
		if (boot_info->version == 1) {
			temp = syna_pal_le2_to_uint(boot_info->v1.boot_or_otp_config_start_block);
			addr = temp * reflash_data->write_block_size;
		}
		if (boot_info->version == 3) {
			temp = syna_pal_le2_to_uint(boot_info->v3.boot_or_otp_config_start_block);
			addr = temp * reflash_data->write_block_size;
		}

		if (cs_offset != 0)
			addr += cs_offset;

		retval = syna_tcm_buf_alloc(&cs_config, rd_size);
		if (retval < 0) {
			LOGE("Fail to allocate memory for cs_config buffer\n");
			goto exit;
		}

		retval = syna_tcm_buf_alloc(rd_data, rd_size);
		if (retval < 0) {
			LOGE("Fail to allocate memory for rd_data buffer\n");
			goto exit;
		}

		retval = syna_tcm_read_flash(tcm_dev, addr,
			&cs_config.buf[0], rd_size, resp_reading);
		if (retval < 0) {
			LOGE("Fail to read cs from the address %X\n", addr);
			goto exit;
		}
	}

	retval = syna_pal_mem_cpy(rd_data->buf, rd_data->buf_size,
		&cs_config.buf[0], cs_config.buf_size, rd_size);
	if (retval < 0) {
		LOGE("Fail to copy data to rd_data buffer\n");
		goto exit;
	}

	rd_data->data_length = rd_size;

exit:
	syna_tcm_buf_release(&cs_config);

	return retval;
}
/*
 *  Retrieve the MTP (Multi Time Programming) data area.
 *
 * param
 *    [ in] tcm_dev:      pointer to TouchComm device
 *    [ in] reflash_data: data blob for reflash
 *    [out] rd_data:      buffer used for storing the retrieved data
 *    [ in] rd_size:      size to read, set '0' to retrieve the entire mtp config data
 *    [ in] offset:       offset to read
 *    [ in] resp_reading:  method to read in the response
 *                          a positive value presents the us time delay for the processing
 *                          of each WORDs in the flash to read;
 *                          or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_read_flash_mtp_config(struct tcm_dev *tcm_dev,
	struct tcm_reflash_data_blob *reflash_data, struct tcm_buffer *rd_data,
	unsigned int rd_size, unsigned int offset, unsigned int resp_reading)
{
	int retval;
	struct tcm_boot_info *boot_info;
	unsigned int addr = 0;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	if (!rd_data) {
		LOGE("Invalid read data buffer\n");
		return -ERR_INVAL;
	}

	if (IS_APP_FW_MODE(tcm_dev->dev_mode)) {
		LOGE("CS config not available in app fw mode %d\n",
			tcm_dev->dev_mode);
		return -ERR_INVAL;
	}

	if (rd_size == 0)
		rd_size = reflash_data->page_size;

	boot_info = reflash_data->boot_info;

	if (boot_info->version != 3) {
		LOGE("Invalid boot info version %d\n", boot_info->version);
		return -ERR_INVAL;
	}

	addr = reflash_data->mtp_config_start_addr;
	if (offset != 0)
		addr += offset;

	retval = syna_tcm_buf_alloc(rd_data, rd_size);
	if (retval < 0) {
		LOGE("Fail to allocate memory for rd_data buffer\n");
		goto exit;
	}

	retval = syna_tcm_read_flash(tcm_dev, addr, rd_data->buf, rd_size, resp_reading);
	if (retval < 0) {
		LOGE("Fail to read cs from the address %X\n", addr);
		goto exit;
	}

	rd_data->data_length = rd_size;

exit:
	return retval;
}
/*
 *  Retrieve the APP CONFIG data area.
 *
 * param
 *    [ in] tcm_dev:      pointer to TouchComm device
 *    [ in] reflash_data: data blob for reflash
 *    [out] rd_data:      buffer used for storing the retrieved data
 *    [ in] resp_reading:  method to read in the response
 *                          a positive value presents the us time delay for the processing
 *                          of each WORDs in the flash to read;
 *                          or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_read_flash_app_config(struct tcm_dev *tcm_dev,
	struct tcm_reflash_data_blob *reflash_data, struct tcm_buffer *rd_data,
	unsigned int resp_reading)
{
	int retval;
	unsigned int temp;
	unsigned int addr = 0;
	unsigned int length = 0;
	struct tcm_application_info *app_info;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	if (!rd_data) {
		LOGE("Invalid read data buffer\n");
		return -ERR_INVAL;
	}

	app_info = &tcm_dev->app_info;

	if (IS_APP_FW_MODE(tcm_dev->dev_mode)) {
		LOGE("APP_CONFIG not available in app fw mode %d\n",
			tcm_dev->dev_mode);
		return -ERR_INVAL;
	}

	temp = syna_pal_le2_to_uint(app_info->app_config_start_write_block);
	addr = temp * reflash_data->write_block_size;
	length = syna_pal_le2_to_uint(app_info->app_config_size);

	if (addr == 0 || length == 0) {
		LOGE("APP_CONFIG data area unavailable\n");
		retval = -ERR_INVAL;
		goto exit;
	}

	LOGD("APP_CONFIG address: 0x%x, length: %d\n", addr, length);

	retval = syna_tcm_buf_alloc(rd_data, length);
	if (retval < 0) {
		LOGE("Fail to allocate memory for rd_data buffer\n");
		goto exit;
	}

	retval = syna_tcm_read_flash(tcm_dev, addr, rd_data->buf,
		length, resp_reading);
	if (retval < 0) {
		LOGE("Fail to read APP_CONFIG area (addr: 0x%x, length: %d)\n",
			addr, length);
		goto exit;
	}

	rd_data->data_length = length;

exit:
	return retval;
}
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
	struct tcm_buffer *rd_data, unsigned int resp_reading)
{
	int retval = 0;
	struct tcm_reflash_data_blob reflash_data;
	unsigned char original_mode;
	unsigned int fw_switch_time;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!rd_data) {
		LOGE("Invalid data buffer\n");
		return -ERR_INVAL;
	}

	if (resp_reading == CMD_RESPONSE_IN_ATTN)
		fw_switch_time = resp_reading;
	else
		fw_switch_time = tcm_dev->fw_mode_switching_time;

	original_mode = tcm_dev->dev_mode;

	if (original_mode != MODE_BOOTLOADER) {
		retval = syna_tcm_set_up_flash_access(tcm_dev, &reflash_data,
				resp_reading, fw_switch_time);
		if (retval < 0) {
			LOGE("Fail to set up flash access\n");
			return retval;
		}
	}

	syna_tcm_buf_init(&reflash_data.out);

	switch (area) {
	case AREA_BOOT_CONFIG:
		retval = syna_tcm_read_flash_boot_config(tcm_dev, &reflash_data,
				rd_data, resp_reading);
		if (retval < 0) {
			LOGE("Fail to get boot config data\n");
			goto exit;
		}
		break;
	case AREA_APP_CONFIG:
		retval = syna_tcm_read_flash_app_config(tcm_dev, &reflash_data,
				rd_data, resp_reading);
		if (retval < 0) {
			LOGE("Fail to get app config data\n");
			goto exit;
		}
		break;
	case AREA_TOOL_CUSTOM_CS:
		retval = syna_tcm_read_flash_boot_cs_config(tcm_dev, &reflash_data,
			rd_data, 0, 0, resp_reading);
		if (retval < 0) {
			LOGE("Fail to get lockdown config\n");
			goto exit;
		}
		break;
	case AREA_TOOL_CUSTOM_LOCKDOWN:
		retval = syna_tcm_read_flash_boot_lockdown_config(tcm_dev, &reflash_data,
			rd_data, resp_reading);
		if (retval < 0) {
			LOGE("Fail to get lockdown config\n");
			goto exit;
		}
		break;
	case AREA_TOOL_CUSTOM_MTP:
		retval = syna_tcm_read_flash_mtp_config(tcm_dev, &reflash_data,
			rd_data, 0, 0, resp_reading);
		if (retval < 0) {
			LOGE("Fail to get lockdown config\n");
			goto exit;
		}
		break;

	default:
		LOGE("Invalid data area\n");
		retval = -ERR_INVAL;
		goto exit;
	}

	LOGI("%s read\n", FLASH_PARTITION_ID_STR(area));
	retval = 0;

exit:
	if (original_mode == MODE_APPLICATION_FIRMWARE)
		syna_tcm_switch_fw_mode(tcm_dev, MODE_APPLICATION_FIRMWARE, resp_reading);

	syna_tcm_buf_release(&reflash_data.out);

	return retval;
}
/*
 *  Process bootloader command to write data to the flash memory.
 *
 *  If the length of the data to write is not an integer multiple of words,
 *  the trailing byte will be discarded.  If the length of the data to write
 *  is not an integer number of write blocks, it will be zero-padded to the
 *  next write block.
 *
 * param
 *    [ in] tcm_dev:      pointer to TouchComm device
 *    [ in] reflash_data: data blob for reflash
 *    [ in] address:      the address in flash memory to write
 *    [ in] wr_data:      data to write
 *    [ in] wr_len:       length of data to write
 *    [ in] wr_delay_us:  a short delay after the command executed
 *                         set 'DEFAULT_FLASH_WRITE_DELAY' to use default,
 *                         set '0' or 'RESP_IN_ATTN' to run in ATTN-driven.
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_write_flash(struct tcm_dev *tcm_dev,
	struct tcm_reflash_data_blob *reflash_data, unsigned int address,
	const unsigned char *wr_data, unsigned int wr_len, unsigned int wr_delay_us)
{
	int retval;
	unsigned int offset;
	unsigned int w_length;
	unsigned int xfer_length;
	unsigned int remaining_length;
	unsigned int flash_address;
	unsigned int block_address;
	unsigned int num_blocks;
	unsigned int resp_delay;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	/* ensure that the length to write is the multiple of max_write_payload_size */
	w_length = reflash_data->max_write_payload_size;
	w_length -= (w_length % reflash_data->write_block_size);

	offset = 0;

	remaining_length = wr_len;

	syna_tcm_buf_lock(&reflash_data->out);

	while (remaining_length) {
		if (remaining_length > w_length)
			xfer_length = w_length;
		else
			xfer_length = remaining_length;

		retval = syna_tcm_buf_alloc(&reflash_data->out, xfer_length + 2);
		if (retval < 0) {
			LOGE("Fail to allocate memory for buf.out\n");
			syna_tcm_buf_unlock(&reflash_data->out);
			return retval;
		}

		flash_address = address + offset;
		block_address = flash_address / reflash_data->write_block_size;
		reflash_data->out.buf[0] = (unsigned char)block_address;
		reflash_data->out.buf[1] = (unsigned char)(block_address >> 8);

		num_blocks = syna_pal_int_division(xfer_length,
			reflash_data->write_block_size, true);

		if (wr_delay_us == CMD_RESPONSE_IN_ATTN) {
			LOGD("xfer: %d (blocks: %d), delay: ATTN-driven\n",
				xfer_length, num_blocks);
			resp_delay = CMD_RESPONSE_IN_ATTN;
		} else {
			resp_delay = (wr_delay_us * num_blocks) / 1000;
			if (resp_delay == 0)
				resp_delay = 1;
			LOGD("xfer: %d (blocks: %d), delay: %d ms\n",
				xfer_length, num_blocks, resp_delay);
		}

		retval = syna_pal_mem_cpy(&reflash_data->out.buf[2],
				reflash_data->out.buf_size - 2,
				&wr_data[offset],
				wr_len - offset,
				xfer_length);
		if (retval < 0) {
			LOGE("Fail to copy write data ,size: %d\n",
				xfer_length);
			syna_tcm_buf_unlock(&reflash_data->out);
			return retval;
		}

		retval = syna_tcm_reflash_send_command(tcm_dev,
				CMD_WRITE_FLASH,
				reflash_data->out.buf,
				xfer_length + 2,
				resp_delay);
		if (retval < 0) {
			LOGE("Fail to write data to flash addr 0x%x, size %d\n",
				flash_address, xfer_length + 2);
			syna_tcm_buf_unlock(&reflash_data->out);
			return retval;
		}

		offset += xfer_length;
		remaining_length -= xfer_length;
		reflash_data->bytes_updated += xfer_length;
#ifdef HAS_PROGRESS_FEEDBACK
		if (tcm_dev->cb_progress_callback)
			tcm_dev->cb_progress_callback(reflash_data->bytes_updated,
				reflash_data->total_bytes_to_update);
#endif
	}

	syna_tcm_buf_unlock(&reflash_data->out);

	return 0;
}
#ifdef TOUCHCOMM_VERSION_1
/*
 *  Process bootloader command to write data to the flash memory through optimized operation.
 *
 *  If the length of the data to write is not an integer multiple of words,
 *  the trailing byte will be discarded.  If the length of the data to write
 *  is not an integer number of write blocks, it will be zero-padded to the
 *  next write block.
 *
 * param
 *    [ in] tcm_dev:      pointer to TouchComm device
 *    [ in] reflash_data: data blob for reflash
 *    [ in] address:      the address in flash memory to write
 *    [ in] wr_data:      data to write
 *    [ in] wr_len:       length of data to write
 *    [ in] wr_delay_us:  a short delay after the command executed
 *                         set 'DEFAULT_FLASH_WRITE_DELAY' to use default,
 *                         set '0' or 'RESP_IN_ATTN' to run in ATTN-driven.
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_write_flash_opt(struct tcm_dev *tcm_dev,
	struct tcm_reflash_data_blob *reflash_data, unsigned int address,
	const unsigned char *wr_data, unsigned int wr_len, unsigned int wr_delay_us)
{
	int retval;
	unsigned int offset;
	unsigned int w_length;
	unsigned int xfer_length;
	unsigned int remaining_length;
	unsigned int flash_address;
	unsigned int start_address;
	unsigned int end_address;
	unsigned int num_blocks;
	unsigned int resp_delay;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data->support_optimized_write) {
		LOGE("Optimized write operation not supported\n");
		return -ERR_INVAL;
	}

	/* ensure that the length to write is the multiple of max_write_payload_size */
	w_length = reflash_data->max_write_payload_size;
	w_length -= (w_length % reflash_data->write_block_size);

	start_address = address / reflash_data->write_block_size;
	end_address = start_address + syna_pal_int_division(wr_len,
					reflash_data->write_block_size, true);

	offset = 0;

	remaining_length = wr_len;

	syna_tcm_buf_lock(&reflash_data->out);

	while (remaining_length) {
		if (remaining_length > w_length)
			xfer_length = w_length;
		else
			xfer_length = remaining_length;

		retval = syna_tcm_buf_alloc(&reflash_data->out, xfer_length + 6);
		if (retval < 0) {
			LOGE("Fail to allocate memory for buf.out\n");
			syna_tcm_buf_unlock(&reflash_data->out);
			return retval;
		}

		flash_address = address + offset;
		start_address = flash_address / reflash_data->write_block_size;

		if (remaining_length == wr_len)
			reflash_data->out.buf[0] = 0x01;
		else
			reflash_data->out.buf[0] = 0x00;

		reflash_data->out.buf[2] = (unsigned char)start_address;
		reflash_data->out.buf[3] = (unsigned char)(start_address >> 8);

		reflash_data->out.buf[4] = (unsigned char)end_address;
		reflash_data->out.buf[5] = (unsigned char)(end_address >> 8);

		num_blocks = syna_pal_int_division(xfer_length,
						reflash_data->write_block_size, true);

		if (wr_delay_us == CMD_RESPONSE_IN_ATTN) {
			LOGD("xfer: %d (blocks: %d), delay: ATTN-driven\n",
				xfer_length, num_blocks);
			resp_delay = CMD_RESPONSE_IN_ATTN;
		} else {
			resp_delay = (wr_delay_us * num_blocks) / 1000;
			LOGD("xfer: %d (blocks: %d), delay: %d ms\n",
				xfer_length, num_blocks, resp_delay);
		}

		retval = syna_pal_mem_cpy(&reflash_data->out.buf[6],
				reflash_data->out.buf_size - 6,
				&wr_data[offset],
				wr_len - offset,
				xfer_length);
		if (retval < 0) {
			LOGE("Fail to copy write data ,size: %d\n",
				xfer_length);
			syna_tcm_buf_unlock(&reflash_data->out);
			return retval;
		}

		retval = syna_tcm_reflash_send_command(tcm_dev,
				CMD_OPTIMIZED_WRITE_FLASH,
				reflash_data->out.buf,
				xfer_length + 6,
				resp_delay);
		if (retval < 0) {
			LOGE("Fail to write data to flash addr 0x%x, size %d\n",
				flash_address, xfer_length + 6);
			syna_tcm_buf_unlock(&reflash_data->out);
			return retval;
		}

		offset += xfer_length;
		remaining_length -= xfer_length;
		reflash_data->bytes_updated += xfer_length;
#ifdef HAS_PROGRESS_FEEDBACK
		if (tcm_dev->cb_progress_callback)
			tcm_dev->cb_progress_callback(reflash_data->bytes_updated,
				reflash_data->total_bytes_to_update);
#endif
	}

	syna_tcm_buf_unlock(&reflash_data->out);

	return 0;
}
#endif
/*
 *  Write data to the target block data area in the flash memory.
 *
 * param
 *    [ in] tcm_dev:       pointer to TouchComm device
 *    [ in] reflash_data:  data blob for reflash
 *    [ in] area:          target block area to write
 *    [ in] resp_reading:  method to read in the response
 *                          a positive value presents the us time delay for the processing
 *                          of each BLOCKs in the flash to write;
 *                          or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 *    [ in] opt_write:     to indicate the optimized write
 *
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_write_flash_block(struct tcm_dev *tcm_dev,
	struct tcm_reflash_data_blob *reflash_data, struct area_block *block,
	unsigned int resp_reading, bool opt_write)
{
	int retval;
	unsigned int size;
	unsigned int flash_addr;
	const unsigned char *data;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	if (!block) {
		LOGE("Invalid block data\n");
		return -ERR_INVAL;
	}

	data = block->data;
	size = block->size;
	flash_addr = block->flash_addr;

	LOGD("Write data to %s - address: 0x%x, size: %d\n",
		FLASH_PARTITION_ID_STR(block->id), flash_addr, size);

	if (size == 0) {
		LOGI("No need to update, size = %d\n", size);
		goto exit;
	}

	if (flash_addr % reflash_data->write_block_size != 0) {
		LOGE("Flash writes (address:0x%x) not starting on block boundary\n",
			flash_addr);
		return -ERR_INVAL;
	}
#ifdef TOUCHCOMM_VERSION_1
	if (opt_write) {
		retval = syna_tcm_write_flash_opt(tcm_dev, reflash_data,
			flash_addr, data, size, resp_reading);
	} else {
		retval = syna_tcm_write_flash(tcm_dev, reflash_data,
			flash_addr, data, size, resp_reading);
	}
#else
	retval = syna_tcm_write_flash(tcm_dev, reflash_data,
		flash_addr, data, size, resp_reading);
#endif

	if (retval < 0) {
		LOGE("Fail to write %s to flash (addr: 0x%x, size: %d)\n",
			FLASH_PARTITION_ID_STR(block->id), flash_addr, size);
		return retval;
	}

exit:
	LOGN("%s area written\n", FLASH_PARTITION_ID_STR(block->id));

	return 0;
}

/*
 *  Process the command to erase the requested pages of the flash memory.
 *  During the command processing, the device may be unresponsive.
 *
 * param
 *    [ in] tcm_dev:        pointer to TouchComm device
 *    [ in] reflash_data:   data blob for reflash
 *    [ in] address:        the address in flash memory to erase
 *    [ in] size:           size to erase
 *    [ in] erase_delay_ms: a short delay after the command executed
 *                          set a positive value or 'DEFAULT_FLASH_ERASE_DELAY' to use default;
 *                          set '0' or 'RESP_IN_ATTN' to select ATTN-driven.
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_erase_flash(struct tcm_dev *tcm_dev,
	struct tcm_reflash_data_blob *reflash_data, unsigned int address,
	unsigned int size, unsigned int erase_delay_ms)
{
	int retval;
	unsigned int page_start = 0;
	unsigned int page_count = 0;
	unsigned char out_buf[4] = { 0 };
	int size_erase_cmd;
	unsigned int resp_delay;

	page_start = address / reflash_data->page_size;

	page_count = syna_pal_int_division(size, reflash_data->page_size, true);

	if (erase_delay_ms == CMD_RESPONSE_IN_ATTN) {
		resp_delay = CMD_RESPONSE_IN_ATTN;
		LOGD("Page start = %d (0x%04x), Page count = %d (0x%04x), delay: ATTN-driven\n",
			page_start, page_start, page_count, page_count);
	} else {
		resp_delay = erase_delay_ms * page_count;
		LOGD("Page start = %d (0x%04x), Page count = %d (0x%04x), delay: %d ms\n",
			page_start, page_start, page_count, page_count, resp_delay);
	}

	if ((page_start > 0xff) || (page_count > 0xff)) {
		size_erase_cmd = 4;

		out_buf[0] = (unsigned char)(page_start & 0xff);
		out_buf[1] = (unsigned char)((page_start >> 8) & 0xff);
		out_buf[2] = (unsigned char)(page_count & 0xff);
		out_buf[3] = (unsigned char)((page_count >> 8) & 0xff);
	} else {
		size_erase_cmd = 2;

		out_buf[0] = (unsigned char)(page_start & 0xff);
		out_buf[1] = (unsigned char)(page_count & 0xff);
	}

	retval = syna_tcm_reflash_send_command(tcm_dev,
			CMD_ERASE_FLASH,
			out_buf,
			size_erase_cmd,
			resp_delay);
	if (retval < 0) {
		LOGE("Fail to erase data at flash page 0x%x, count %d\n",
			page_start, page_count);
		return retval;
	}

	return 0;
}

/*
 *  Erase the target block data area in the flash memory.
 *
 * param
 *    [ in] tcm_dev:      pointer to TouchComm device
 *    [ in] reflash_data: data blob for reflash
 *    [ in] block:        target block area to erase
 *    [ in] resp_reading:  method to read in the response
 *                          a positive value presents the us time delay for the processing
 *                          of each PAGEs in the flash to erase;
 *                          or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 *    [ in] opt_write:     to indicate the optimized write
 *
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_erase_flash_block(struct tcm_dev *tcm_dev,
	struct tcm_reflash_data_blob *reflash_data, struct area_block *block,
	unsigned int resp_reading, bool opt_write)
{
	int retval;
	unsigned int size;
	unsigned int flash_addr;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	if (!block) {
		LOGE("Invalid block data\n");
		return -ERR_INVAL;
	}

	if (opt_write) {
		LOGN("Bypass %s area due to the optimized write\n",
			FLASH_PARTITION_ID_STR(block->id));
		return 0;
	}

	flash_addr = block->flash_addr;
	size = block->size;

	LOGD("Erase %s block - address: 0x%x, size: %d\n",
		FLASH_PARTITION_ID_STR(block->id), flash_addr, size);

	if (size == 0) {
		LOGI("No need to erase, size = %d\n", size);
		goto exit;
	}

	if (flash_addr % reflash_data->page_size != 0) {
		LOGE("Flash erases (address:0x%x) not starting on page boundary\n",
			flash_addr);
		return -ERR_INVAL;
	}

	retval = syna_tcm_erase_flash(tcm_dev, reflash_data,
			flash_addr, size, resp_reading);
	if (retval < 0) {
		LOGE("Fail to erase %s data (addr: 0x%x, size: %d)\n",
			FLASH_PARTITION_ID_STR(block->id), flash_addr, size);
		return retval;
	}

exit:
	LOGN("%s area erased\n", FLASH_PARTITION_ID_STR(block->id));

	return 0;
}

/*
 *  Update the target area in the flash memory.
 *
 * param
 *    [ in] tcm_dev:               pointer to TouchComm device
 *    [ in] reflash_data:          data blob for reflash
 *    [ in] block:                 target block area to update
 *    [ in] flash_erase_delay_ms:  delay time to wait for the completion of flash erase
 *                                   for polling,     set a positive value;
 *                                   for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 *    [ in] flash_write_delay_us:  delay time to wait for the completion of flash write
 *                                   for polling,     set a positive value;
 *                                   for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 *    [ in] opt_write:             to indicate the optimized write
 *
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_update_flash_block(struct tcm_dev *tcm_dev,
	struct tcm_reflash_data_blob *reflash_data, struct area_block *block,
	unsigned int flash_erase_delay_ms, unsigned int flash_write_delay_us,
	bool opt_write)
{
	int retval;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	if (!block) {
		LOGE("Invalid block data\n");
		return -ERR_INVAL;
	}

	/* reflash is not needed for the partition */
	retval = syna_tcm_check_flash_block(tcm_dev, reflash_data, block);
	if (retval < 0) {
		LOGE("Invalid %s area\n", FLASH_PARTITION_ID_STR(block->id));
		return retval;
	}

	if (retval == 0)
		return 0;

	LOGN("Prepare to erase %s area\n", FLASH_PARTITION_ID_STR(block->id));

	retval = syna_tcm_erase_flash_block(tcm_dev,
			reflash_data,
			block,
			flash_erase_delay_ms,
			opt_write);
	if (retval < 0) {
		LOGE("Fail to erase %s area\n", FLASH_PARTITION_ID_STR(block->id));
		return retval;
	}

	LOGN("Prepare to update %s area\n", FLASH_PARTITION_ID_STR(block->id));

	retval = syna_tcm_write_flash_block(tcm_dev,
			reflash_data,
			block,
			flash_write_delay_us,
			opt_write);
	if (retval < 0) {
		LOGE("Fail to write %s area\n", FLASH_PARTITION_ID_STR(block->id));
		return retval;
	}

	return 0;
}

/*
 *  The extended entry function to perform firmware update by using the firmware image file.
 *
 *  Please be noted there is no such file parsing and ID comparison implemented in this function.
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
 *
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
int syna_tcm_do_fw_update_ex(struct tcm_dev *tcm_dev, struct image_info *image,
	unsigned int op, unsigned int flash_erase_delay_ms, unsigned int flash_write_delay_us,
	unsigned int fw_switch_delay_ms, bool use_opt)
{
	int retval;
	struct tcm_reflash_data_blob reflash_data;
	unsigned int resp_handling;
	int app_status;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if ((!image) || (image->size == 0)) {
		LOGE("Invalid image data\n");
		return -ERR_INVAL;
	}

	syna_pal_mem_set(&reflash_data, 0x00, sizeof(struct tcm_reflash_data_blob));

	reflash_data.op = op;
	reflash_data.support_optimized_write = false;

	if (fw_switch_delay_ms == CMD_RESPONSE_IN_ATTN)
		resp_handling = CMD_RESPONSE_IN_ATTN;
	else
		resp_handling = tcm_dev->msg_data.command_polling_time;

	syna_tcm_buf_init(&reflash_data.out);

	reflash_data.image_size = image->size;
	reflash_data.image_info = image;

	LOGN("Start of reflash\n");

	ATOMIC_SET(tcm_dev->firmware_flashing, 1);

	app_status = syna_pal_le2_to_uint(tcm_dev->app_info.status);

	/* forcedly update the firmware and config when the following cases
	 *   - device stays in bootloader
	 *   - app firmware doesn't run properly
	 */
	if (IS_BOOTLOADER_MODE(tcm_dev->dev_mode))
		reflash_data.op |= UPDATE_FIRMWARE_AND_CONFIG;
	if (IS_APP_FW_MODE(tcm_dev->dev_mode) && (app_status != APP_STATUS_OK))
		reflash_data.op |= UPDATE_FIRMWARE_AND_CONFIG;

	if ((reflash_data.op & UPDATE_FIRMWARE) == UPDATE_FIRMWARE)
		reflash_data.total_bytes_to_update +=
			reflash_data.image_info->data[AREA_APP_CODE].size;
	if ((reflash_data.op & UPDATE_CONFIG) == UPDATE_CONFIG)
		reflash_data.total_bytes_to_update +=
			reflash_data.image_info->data[AREA_APP_CONFIG].size;

	/* set up flash access, and enter the bootloader mode */
	retval = syna_tcm_set_up_flash_access(tcm_dev, &reflash_data,
		resp_handling, fw_switch_delay_ms);
	if (retval < 0) {
		LOGE("Fail to set up flash access\n");
		goto exit;
	}

	/* perform the fw update */
	if (tcm_dev->dev_mode != MODE_BOOTLOADER) {
		LOGE("Incorrect device mode, 0x%02x, before reflash\n", tcm_dev->dev_mode);
		goto reset;
	}

	if ((reflash_data.op & UPDATE_FIRMWARE) == UPDATE_FIRMWARE) {
		retval = syna_tcm_update_flash_block(tcm_dev,
			&reflash_data,
			&reflash_data.image_info->data[AREA_APP_CODE],
			flash_erase_delay_ms,
			flash_write_delay_us,
			(reflash_data.support_optimized_write && use_opt));
		if (retval < 0) {
			LOGE("Fail to update APP_CODE\n");
			goto exit;
		}
	}

	if ((reflash_data.op & UPDATE_CONFIG) == UPDATE_CONFIG) {
		retval = syna_tcm_update_flash_block(tcm_dev,
			&reflash_data,
			&reflash_data.image_info->data[AREA_APP_CONFIG],
			flash_erase_delay_ms,
			flash_write_delay_us,
			false);
		if (retval < 0) {
			LOGE("Fail to update APP_CONFIG\n");
			goto exit;
		}
	}

	LOGN("End of reflash\n");
	retval = 0;
reset:
	retval = syna_tcm_reset(tcm_dev, fw_switch_delay_ms);
	if (retval < 0) {
		LOGE("Fail to do reset\n");
		goto exit;
	}

exit:
	ATOMIC_SET(tcm_dev->firmware_flashing, 0);

	syna_tcm_buf_release(&reflash_data.out);

	return retval;
}

/*
 *  The legacy entry function to perform firmware update by using the firmware image file.
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
 *                                  wait for the completion of flash access
 *                                    for polling,     set a value formatted
 *                                    with [erase ms | write us];
 *                                    for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 *    [ in] force_reflash:         '1' to do reflash anyway
 *                                 '0' to compare ID info before doing reflash.
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
int syna_tcm_do_fw_update(struct tcm_dev *tcm_dev, const unsigned char *image,
	unsigned int image_size, unsigned int flash_delay_settings, bool force_reflash)
{
	int retval;
	struct image_info image_info;
	int op = (int)UPDATE_NONE;
	unsigned int flash_erase_delay_ms;
	unsigned int flash_write_delay_us;
	unsigned int fw_switch_time;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if ((!image) || (image_size == 0)) {
		LOGE("Invalid image data\n");
		return -ERR_INVAL;
	}

	if (flash_delay_settings == CMD_RESPONSE_IN_ATTN) {
		flash_erase_delay_ms = flash_delay_settings;
		flash_write_delay_us = flash_delay_settings;
		fw_switch_time = flash_delay_settings;
	} else {
		flash_erase_delay_ms = (flash_delay_settings >> 16) & 0xFFFF;
		flash_write_delay_us = flash_delay_settings & 0xFFFF;
		fw_switch_time = tcm_dev->fw_mode_switching_time;
	}

	syna_pal_mem_set(&image_info, 0x00, sizeof(struct image_info));

	LOGI("Prepare to do reflash\n");

	retval = syna_tcm_parse_fw_image(image, image_size, &image_info);
	if (retval < 0) {
		LOGE("Fail to parse firmware image\n");
		return retval;
	}

	if (force_reflash) {
		op = UPDATE_FIRMWARE_AND_CONFIG;
		goto reflash;
	}

	/* determine the partitions to update  */
	op = syna_tcm_compare_image_id_info(tcm_dev, &image_info);
	if (op == (int)UPDATE_NONE)
		return 0;

reflash:
	return syna_tcm_do_fw_update_ex(tcm_dev, &image_info, op,
		flash_erase_delay_ms, flash_write_delay_us, fw_switch_time, true);
}

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
	unsigned int flash_write_delay_us)
{
	int retval;
	struct tcm_reflash_data_blob reflash_data;
	struct tcm_buffer boot_config;
	struct area_block *block;
	unsigned int start_addr, offset;
	const unsigned char *data;
	unsigned int resp_handling;
	unsigned int fw_switch_time;
	unsigned int total_bytes_to_update = 0;
	unsigned int i;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if ((!image) || (image->size == 0)) {
		LOGE("Invalid image data\n");
		return -ERR_INVAL;
	}

	block = &image->data[AREA_TOOL_BOOT_CONFIG];

	if ((!block->described) || (!block->data)) {
		LOGE("Invalid partition data of %s area\n", FLASH_PARTITION_ID_STR(block->id));
		return -ERR_INVAL;
	}

	if (block->size < BOOT_CONFIG_SLOT_SIZE) {
		LOGE("Invalid size of given data, %d, expected %d\n",
			block->size, BOOT_CONFIG_SLOT_SIZE);
		return -ERR_INVAL;
	}

	syna_pal_mem_set(&reflash_data, 0x00, sizeof(struct tcm_reflash_data_blob));

	total_bytes_to_update = BOOT_CONFIG_SLOT_SIZE;
	reflash_data.total_bytes_to_update = total_bytes_to_update;

	if (flash_write_delay_us == CMD_RESPONSE_IN_ATTN) {
		resp_handling = CMD_RESPONSE_IN_ATTN;
		fw_switch_time = CMD_RESPONSE_IN_ATTN;
	} else {
		resp_handling = tcm_dev->msg_data.command_polling_time;
		fw_switch_time = tcm_dev->fw_mode_switching_time;
	}

	syna_tcm_buf_init(&reflash_data.out);

	syna_tcm_buf_init(&boot_config);

	ATOMIC_SET(tcm_dev->firmware_flashing, 1);

	/* set up flash access, and enter the bootloader mode */
	retval = syna_tcm_set_up_flash_access(tcm_dev, &reflash_data,
		resp_handling, fw_switch_time);
	if (retval < 0) {
		LOGE("Fail to set up flash access\n");
		goto exit;
	}

	retval = syna_tcm_read_flash_boot_config(tcm_dev, &reflash_data,
		&boot_config, resp_handling);
	if (retval < 0) {
		LOGE("Fail to read boot config\n");
		goto exit;
	}

	/* confirm the unlocked device */
	offset = boot_config.data_length - BOOT_CONFIG_SLOT_SIZE;
	if ((boot_config.buf[offset + 1] & 0x80) > 0) {
		LOGN("Device has been locked already\n");
		retval = 0;
		goto exit;
	}

	retval = syna_tcm_check_flash_block(tcm_dev, &reflash_data, block);
	if (retval <= 0) {
		LOGE("Invalid %s area\n", FLASH_PARTITION_ID_STR(block->id));
		goto exit;
	}

	/* update the lockdown config */
	offset = block->size - BOOT_CONFIG_SLOT_SIZE;
	data = &block->data[offset];
	start_addr = reflash_data.boot_config_start_addr + offset;

	for (i = 0; i < (total_bytes_to_update / BOOT_CONFIG_SLOT_SIZE); i++) {
		LOGD("Config start addr: 0x%X (%d), offset:%d\n",
			start_addr, start_addr, offset);

		retval = syna_tcm_write_flash(tcm_dev, &reflash_data,
			start_addr + (i * BOOT_CONFIG_SLOT_SIZE),
			&data[i * BOOT_CONFIG_SLOT_SIZE],
			BOOT_CONFIG_SLOT_SIZE,
		flash_write_delay_us);
		if (retval < 0)
			goto exit;
	}

	retval = total_bytes_to_update;

exit:
	if (IS_BOOTLOADER_MODE(tcm_dev->dev_mode))
		syna_tcm_switch_fw_mode(tcm_dev, MODE_APPLICATION_FIRMWARE, fw_switch_time);

	ATOMIC_SET(tcm_dev->firmware_flashing, 0);

	syna_tcm_buf_release(&boot_config);

	syna_tcm_buf_release(&reflash_data.out);

	return retval;
}

/*
 *   Request to write custom serialization area
 *
 * param
 *    [ in] tcm_dev:              pointer to TouchComm device
 *    [ in] cs_data:              custom serialization data to write
 *    [ in] cs_data_size:         size of given data
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
	unsigned int cs_data_size, unsigned int cs_offset, unsigned int flash_write_delay_us)
{
	int retval;
	struct tcm_reflash_data_blob reflash_data;
	struct tcm_buffer boot_config;
	unsigned int addr;
	unsigned int end;
	unsigned int offset = cs_offset;
	unsigned int resp_handling;
	unsigned int fw_switch_time;
	bool is_empty = false;
	unsigned int i, m;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if ((!cs_data) || (cs_data_size == 0)) {
		LOGE("Invalid cs data\n");
		return -ERR_INVAL;
	}

	syna_pal_mem_set(&reflash_data, 0x00, sizeof(struct tcm_reflash_data_blob));

	if (flash_write_delay_us == CMD_RESPONSE_IN_ATTN) {
		resp_handling = CMD_RESPONSE_IN_ATTN;
		fw_switch_time = CMD_RESPONSE_IN_ATTN;
	} else {
		resp_handling = tcm_dev->msg_data.command_polling_time;
		fw_switch_time = tcm_dev->fw_mode_switching_time;
	}

	syna_tcm_buf_init(&reflash_data.out);

	syna_tcm_buf_init(&boot_config);

	ATOMIC_SET(tcm_dev->firmware_flashing, 1);

	/* set up flash access, and enter the bootloader mode */
	retval = syna_tcm_set_up_flash_access(tcm_dev, &reflash_data,
		resp_handling, fw_switch_time);
	if (retval < 0) {
		LOGE("Fail to set up flash access\n");
		goto exit;
	}

	retval = syna_tcm_read_flash_boot_config(tcm_dev, &reflash_data,
		&boot_config, resp_handling);
	if (retval < 0) {
		LOGE("Fail to read boot config\n");
		goto exit;
	}

	/* skip the lockdown config */
	end = boot_config.data_length - BOOT_CONFIG_SLOT_SIZE;
	for (i = 0; i < BOOT_CONFIG_SLOTS; i++) {
		if ((boot_config.buf[end + 1] >> 7) == 1) {
			if ((boot_config.buf[end - BOOT_CONFIG_SLOT_SIZE + 1] >> 7) == 0)
				break;
		}

		end -= BOOT_CONFIG_SLOT_SIZE;
	}

	if (cs_data_size > end) {
		LOGE("Invalid write block size, lockdown config may be overwritten\n");
		goto exit;
	}
	if (reflash_data.write_block_size == 0) {
		LOGE("Invalid write block size\n");
		goto exit;
	}

	if (offset != 0) {
		m = reflash_data.write_block_size;
		offset = syna_pal_int_division(offset, m, true) * m;
	}
	if (offset != cs_offset)
		LOGD("Apply offset %d,because giving %d,is not %d-byte-aligned\n",
			offset, cs_offset, m);

	/* skip the written area */
	do {
		is_empty = false;
		if (boot_config.buf[offset] == 0) {
			for (i = 0; i < reflash_data.write_block_size; i++) {
				if (boot_config.buf[offset + i] != 0)
					break;
			}
			is_empty = (bool)(i >= reflash_data.write_block_size);
		}

		if (is_empty)
			break;

		offset += reflash_data.write_block_size;

	} while (offset < end);

	if (offset > end) {
		LOGE("Invalid address to write\n");
		goto exit;
	}

	m = reflash_data.write_block_size;
	reflash_data.total_bytes_to_update = syna_pal_int_division(cs_data_size, m, true) * m;

	/* write the given data to buffer */
	for (i = 0; i < cs_data_size; i++)
		boot_config.buf[offset + i] = cs_data[i];

	addr = reflash_data.boot_config_start_addr + offset;
	LOGD("Config start addr: 0x%X (%d), offset:%d\n",
		reflash_data.boot_config_start_addr, reflash_data.boot_config_start_addr, offset);
	LOGD("Target addr: 0x%X (%d), write length:%d, total bytes to update:%d\n",
		addr, addr, cs_data_size, reflash_data.total_bytes_to_update);

	/* write to flash */
	retval = syna_tcm_write_flash(tcm_dev, &reflash_data, addr, &boot_config.buf[offset],
			reflash_data.total_bytes_to_update, flash_write_delay_us);
	if (retval < 0)
		goto exit;

	retval = reflash_data.total_bytes_to_update;

exit:
	if (IS_BOOTLOADER_MODE(tcm_dev->dev_mode))
		syna_tcm_switch_fw_mode(tcm_dev, MODE_APPLICATION_FIRMWARE, fw_switch_time);

	ATOMIC_SET(tcm_dev->firmware_flashing, 0);

	syna_tcm_buf_release(&boot_config);

	syna_tcm_buf_release(&reflash_data.out);

	return retval;
}

/*
 *   Request to read custom serialization data
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
	unsigned int cs_data_size, unsigned int cs_offset, unsigned int flash_read_delay_us)
{
	int retval;
	struct tcm_reflash_data_blob reflash_data;
	struct tcm_buffer cs;
	unsigned int size;
	unsigned int resp_handling;
	unsigned int fw_switch_time;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if ((!cs_data) || (cs_data_size == 0)) {
		LOGE("Invalid cs data\n");
		return -ERR_INVAL;
	}

	syna_pal_mem_set(&reflash_data, 0x00, sizeof(struct tcm_reflash_data_blob));

	if (flash_read_delay_us == CMD_RESPONSE_IN_ATTN) {
		resp_handling = CMD_RESPONSE_IN_ATTN;
		fw_switch_time = CMD_RESPONSE_IN_ATTN;
	} else {
		resp_handling = tcm_dev->msg_data.command_polling_time;
		fw_switch_time = tcm_dev->fw_mode_switching_time;
	}

	syna_tcm_buf_init(&reflash_data.out);

	syna_tcm_buf_init(&cs);

	/* set up flash access, and enter the bootloader mode */
	retval = syna_tcm_set_up_flash_access(tcm_dev, &reflash_data,
		resp_handling, fw_switch_time);
	if (retval < 0) {
		LOGE("Fail to set up flash access\n");
		goto exit;
	}

	LOGD("Request to read %d bytes\n", cs_data_size);
	size = syna_pal_int_division(cs_data_size, reflash_data.write_block_size, true);
	size *= reflash_data.write_block_size;
	if (size != cs_data_size)
		LOGD("Apply max size to read %d,because giving %d, is not %d-byte-aligned\n",
			size, cs_data_size, reflash_data.write_block_size);

	retval = syna_tcm_read_flash_boot_cs_config(tcm_dev,
		&reflash_data, &cs,
		size, cs_offset, flash_read_delay_us);
	if (retval < 0) {
		LOGE("Fail to read cs config data\n");
		goto exit;
	}

	retval = syna_pal_mem_cpy(cs_data, cs_data_size, cs.buf, cs.buf_size, cs_data_size);
	if (retval < 0) {
		LOGE("Fail to copy data to caller buffer\n");
		goto exit;
	}

exit:
	if (IS_BOOTLOADER_MODE(tcm_dev->dev_mode))
		syna_tcm_switch_fw_mode(tcm_dev, MODE_APPLICATION_FIRMWARE, fw_switch_time);

	syna_tcm_buf_release(&cs);

	syna_tcm_buf_release(&reflash_data.out);

	return retval;
}

/*
 *   Request to write custom MTP data
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
	unsigned int mtp_data_size, unsigned int mtp_offset, unsigned int flash_write_delay_us)
{
	int retval;
	struct tcm_reflash_data_blob reflash_data;
	unsigned char *data;
	unsigned int addr;
	unsigned int offset = mtp_offset;
	unsigned int resp_handling;
	unsigned int fw_switch_time;
	unsigned int m;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if ((!mtp_data) || (mtp_data_size == 0)) {
		LOGE("Invalid mtp data\n");
		return -ERR_INVAL;
	}

	syna_pal_mem_set(&reflash_data, 0x00, sizeof(struct tcm_reflash_data_blob));

	if (flash_write_delay_us == CMD_RESPONSE_IN_ATTN) {
		resp_handling = CMD_RESPONSE_IN_ATTN;
		fw_switch_time = CMD_RESPONSE_IN_ATTN;
	} else {
		resp_handling = tcm_dev->msg_data.command_polling_time;
		fw_switch_time = tcm_dev->fw_mode_switching_time;
	}

	syna_tcm_buf_init(&reflash_data.out);

	ATOMIC_SET(tcm_dev->firmware_flashing, 1);

	/* set up flash access, and enter the bootloader mode */
	retval = syna_tcm_set_up_flash_access(tcm_dev, &reflash_data,
		resp_handling, fw_switch_time);
	if (retval < 0) {
		LOGE("Fail to set up flash access\n");
		goto exit;
	}

	if (reflash_data.boot_info->version < 3) {
		LOGE("MTP area may not be supported. Boot info version: %d\n",
			reflash_data.boot_info->version);
		goto exit;
	}

	if (reflash_data.write_block_size == 0) {
		LOGE("Invalid write block size\n");
		goto exit;
	}

	if (offset != 0) {
		m = reflash_data.write_block_size;
		offset = syna_pal_int_division(offset, m, true) * m;
	}
	if (offset != mtp_offset)
		LOGD("Apply offset, %d, because giving, %d, is not %d-byte-aligned\n",
			offset, mtp_offset, m);

	m = reflash_data.write_block_size;
	reflash_data.total_bytes_to_update = syna_pal_int_division(mtp_data_size, m, true) * m;

	/* write the given data to buffer */
	data = syna_pal_mem_alloc(reflash_data.total_bytes_to_update, sizeof(unsigned char));
	if (!data) {
		LOGE("Fail to set up flash access\n");
		retval = -ERR_NOMEM;
		goto exit;
	}
	syna_pal_mem_cpy(data, reflash_data.total_bytes_to_update,
		mtp_data, mtp_data_size, mtp_data_size);

	addr = reflash_data.mtp_config_start_addr + offset;
	LOGD("Config start addr: 0x%X (%d), offset:%d\n",
		reflash_data.mtp_config_start_addr, reflash_data.mtp_config_start_addr, offset);
	LOGD("Target addr: 0x%X (%d), write length:%d, total bytes to update:%d\n",
		addr, addr, mtp_data_size, reflash_data.total_bytes_to_update);

	/* write to flash */
	retval = syna_tcm_write_flash(tcm_dev, &reflash_data, addr, data,
		reflash_data.total_bytes_to_update, flash_write_delay_us);
	if (retval < 0)
		goto exit;

	retval = reflash_data.total_bytes_to_update;

exit:
	if (IS_BOOTLOADER_MODE(tcm_dev->dev_mode))
		syna_tcm_switch_fw_mode(tcm_dev, MODE_APPLICATION_FIRMWARE, fw_switch_time);

	ATOMIC_SET(tcm_dev->firmware_flashing, 0);

	syna_tcm_buf_release(&reflash_data.out);

	return retval;
}

/*
 *   Request to read custom MTP data
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
	unsigned int mtp_data_size, unsigned int mtp_offset, unsigned int flash_read_delay_us)
{
	int retval;
	struct tcm_reflash_data_blob reflash_data;
	struct tcm_buffer mtp;
	unsigned int size;
	unsigned int resp_handling;
	unsigned int fw_switch_time;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if ((!mtp_data) || (mtp_data_size == 0)) {
		LOGE("Invalid mtp data\n");
		return -ERR_INVAL;
	}

	syna_pal_mem_set(&reflash_data, 0x00, sizeof(struct tcm_reflash_data_blob));

	if (flash_read_delay_us == CMD_RESPONSE_IN_ATTN) {
		resp_handling = CMD_RESPONSE_IN_ATTN;
		fw_switch_time = CMD_RESPONSE_IN_ATTN;
	} else {
		resp_handling = tcm_dev->msg_data.command_polling_time;
		fw_switch_time = tcm_dev->fw_mode_switching_time;
	}

	syna_tcm_buf_init(&reflash_data.out);

	syna_tcm_buf_init(&mtp);

	/* set up flash access, and enter the bootloader mode */
	retval = syna_tcm_set_up_flash_access(tcm_dev, &reflash_data,
		resp_handling, fw_switch_time);
	if (retval < 0) {
		LOGE("Fail to set up flash access\n");
		goto exit;
	}

	if (reflash_data.boot_info->version < 3) {
		LOGE("MTP area may not be supported. Boot info version: %d\n",
			reflash_data.boot_info->version);
		goto exit;
	}

	LOGD("Request to read %d bytes\n", mtp_data_size);
	size = syna_pal_int_division(mtp_data_size, reflash_data.write_block_size, true);
	size *= reflash_data.write_block_size;
	if (size != mtp_data_size)
		LOGD("Apply max size, %d, giving value, %d, is not %d-byte-aligned\n",
			size, mtp_data_size, reflash_data.write_block_size);

	retval = syna_tcm_read_flash_mtp_config(tcm_dev,
		&reflash_data, &mtp,
		size, mtp_offset,
		flash_read_delay_us);
	if (retval < 0) {
		LOGE("Fail to read mtp config data\n");
		goto exit;
	}

	retval = syna_pal_mem_cpy(mtp_data, mtp_data_size, mtp.buf, mtp.buf_size, mtp_data_size);
	if (retval < 0) {
		LOGE("Fail to copy data to caller buffer\n");
		goto exit;
	}

exit:
	if (IS_BOOTLOADER_MODE(tcm_dev->dev_mode))
		syna_tcm_switch_fw_mode(tcm_dev, MODE_APPLICATION_FIRMWARE, fw_switch_time);

	syna_tcm_buf_release(&mtp);

	syna_tcm_buf_release(&reflash_data.out);

	return retval;
}

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
int syna_tcm_erase_mtp_data(struct tcm_dev *tcm_dev, unsigned int flash_erase_delay_us)
{
	int retval;
	struct tcm_reflash_data_blob reflash_data;
	unsigned int resp_handling;
	unsigned int fw_switch_time;
	unsigned int size;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	syna_pal_mem_set(&reflash_data, 0x00, sizeof(struct tcm_reflash_data_blob));

	if (flash_erase_delay_us == CMD_RESPONSE_IN_ATTN) {
		resp_handling = CMD_RESPONSE_IN_ATTN;
		fw_switch_time = CMD_RESPONSE_IN_ATTN;
	} else {
		resp_handling = tcm_dev->msg_data.command_polling_time;
		fw_switch_time = tcm_dev->fw_mode_switching_time;
	}

	syna_tcm_buf_init(&reflash_data.out);

	/* set up flash access, and enter the bootloader mode */
	retval = syna_tcm_set_up_flash_access(tcm_dev, &reflash_data,
		resp_handling, fw_switch_time);
	if (retval < 0) {
		LOGE("Fail to set up flash access\n");
		goto exit;
	}

	if (reflash_data.boot_info->version < 3) {
		LOGE("MTP area may not be supported. Boot info version: %d\n",
			reflash_data.boot_info->version);
		goto exit;
	}

	size = reflash_data.boot_info->v3.mtp_config_num_pages * reflash_data.page_size;

	retval = syna_tcm_erase_flash(tcm_dev, &reflash_data,
		reflash_data.mtp_config_start_addr, size, resp_handling);
	if (retval < 0) {
		LOGE("Fail to erase mtp area\n");
		goto exit;
	}

exit:
	if (IS_BOOTLOADER_MODE(tcm_dev->dev_mode))
		syna_tcm_switch_fw_mode(tcm_dev, MODE_APPLICATION_FIRMWARE, fw_switch_time);

	syna_tcm_buf_release(&reflash_data.out);

	return retval;
}

