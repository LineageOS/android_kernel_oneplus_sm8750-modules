// SPDX-License-Identifier: GPL-2.0-or-later
/*
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
 * This file is the reference code of platform I2C bus module being used to
 * communicate with Synaptics TouchComm device over I2C.
 */

#include <linux/i2c.h>

#include "syna_tcm2.h"
#include "syna_tcm2_platform.h"

#define I2C_MODULE_NAME "synaptics_tcm_i2c"

#define XFER_ATTEMPTS 5

static struct syna_hw_interface *p_hw_i2c_if;



/*
 * Request and return the device pointer for managed resources
 *
 * param
 *     void.
 *
 * return
 *     a struct device pointer
 */
struct device *syna_request_managed_device(void)
{
	struct i2c_client *client;

	if (!p_hw_i2c_if)
		return NULL;

	client = p_hw_i2c_if->pdev;
	if (!client)
		return NULL;

	return &client->dev;
}


/*
 * Release the GPIO.
 * Be aware that the allocated devm-managed GPIO will be released when device is released.
 *
 * param
 *     [ in] gpio:   the target gpio
 *
 * return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_put_gpio(int gpio)
{
	/* release gpios */
	if (gpio <= 0) {
		LOGE("Invalid gpio pin\n");
		return -EINVAL;
	}

#ifndef DEV_MANAGED_API
	gpio_free(gpio);
#endif

	LOGD("GPIO-%d released\n", gpio);

	return 0;
}
/*
 * Request a gpio and perform the requested setup
 *
 * param
 *    [ in] gpio:   the target gpio
 *    [ in] dir:    default direction of gpio
 *    [ in] state:  default state of gpio
 *
 * return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_get_gpio(int gpio, int dir, int state, char *label)
{
	int retval;
#ifdef DEV_MANAGED_API
	struct device *dev = syna_request_managed_device();
#endif

	if (gpio < 0) {
		LOGE("Invalid gpio pin\n");
		return -EINVAL;
	}

	retval = scnprintf(label, 16, "tcm_gpio_%d\n", gpio);
	if (retval < 0) {
		LOGE("Fail to set GPIO label\n");
		return retval;
	}
#ifdef DEV_MANAGED_API
	if (!dev) {
		LOGE("Invalid device handle\n");
		return -EINVAL;
	}

	retval = devm_gpio_request(dev, gpio, label);
#else /* Legacy API */
	retval = gpio_request(gpio, label);
#endif
	if (retval < 0) {
		LOGE("Fail to request GPIO %d\n", gpio);
		return retval;
	}

	if (dir == 0)
		retval = gpio_direction_input(gpio);
	else
		retval = gpio_direction_output(gpio, state);

	if (retval < 0) {
		LOGE("Fail to set GPIO %d direction\n", gpio);
		return retval;
	}

	LOGD("GPIO-%d requested\n", gpio);

	return 0;
}
/*
 * Release the regulator.
 * Be aware that the allocated devm-managed regulator will be released when device is released.
 *
 * param
 *    [ in] reg_dev: regulator to release
 *
 * return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_put_regulator(struct regulator *reg_dev)
{
	if (!reg_dev) {
		LOGE("Invalid regulator device\n");
		return -EINVAL;
	}
#ifndef DEV_MANAGED_API
	regulator_put(reg_dev);
#endif

	return 0;
}
/*
 * Requested a regulator according to the name.
 *
 * param
 *    [ in] name: name of requested regulator
 *
 * return
 *    on success, return the pointer to the requested regulator; otherwise, on error.
 */
static struct regulator *syna_i2c_get_regulator(const char *name)
{
	struct regulator *reg_dev = NULL;
	struct device *dev = syna_request_managed_device();

	if (!dev) {
		LOGE("Invalid device handle\n");
		return (struct regulator *)PTR_ERR(NULL);
	}

	if (name != NULL && *name != 0) {
#ifdef DEV_MANAGED_API
		reg_dev = devm_regulator_get(dev, name);
#else /* Legacy API */
		reg_dev = regulator_get(dev, name);
#endif
		if (IS_ERR(reg_dev)) {
			LOGW("Regulator is not ready\n");
			return (struct regulator *)PTR_ERR(reg_dev);
		}
	}

	return reg_dev;
}
/*
 * Parse and obtain board specific data from the device tree source file.
 *
 * param
 *    [ in] void
 *
 * return
 *    0 in case of success, a negative value otherwise.
 */
#ifdef CONFIG_OF
static int syna_i2c_parse_dt(void)
{
	int retval;
	struct property *prop;
	struct device *dev = syna_request_managed_device();
	struct device_node *np;
	struct syna_hw_attn_data *attn;
	struct syna_hw_pwr_data *pwr;
	struct syna_hw_rst_data *rst;
	struct syna_hw_bus_data *bus;
	struct product_specific *product;
	int temp_value[5] = { 0 };

	if (!dev) {
		LOGE("Invalid device\n");
		return -EINVAL;
	}

	np = dev->of_node;

	if (!p_hw_i2c_if) {
		LOGE("Invalid hardware interface\n");
		return -EINVAL;
	}

	attn = &p_hw_i2c_if->bdata_attn;
	if (attn) {
		attn->irq_gpio = -1;
		prop = of_find_property(np, "synaptics,irq-gpio", NULL);
		if (prop && prop->length)
			attn->irq_gpio = of_get_named_gpio(np, "synaptics,irq-gpio", 0);

		attn->irq_flags = (IRQF_ONESHOT | IRQF_TRIGGER_LOW);
		prop = of_find_property(np, "synaptics,irq-flags", NULL);
		if (prop && prop->length) {
			of_property_read_u32(np, "synaptics,irq-flags",
				(unsigned int *)&temp_value[0]);
			attn->irq_flags = temp_value[0];
		}

		attn->irq_on_state = 0;
		prop = of_find_property(np, "synaptics,irq-on-state", NULL);
		if (prop && prop->length)
			of_property_read_u32(np, "synaptics,irq-on-state",
				&attn->irq_on_state);
	}

	pwr = &p_hw_i2c_if->bdata_pwr;
	if (pwr) {
		pwr->power_on_state = 1;
		prop = of_find_property(np, "synaptics,power-on-state", NULL);
		if (prop && prop->length)
			of_property_read_u32(np, "synaptics,power-on-state",
				&pwr->power_on_state);

		pwr->power_delay_ms = 0;
		prop = of_find_property(np, "synaptics,power-delay-ms", NULL);
		if (prop && prop->length)
			of_property_read_u32(np, "synaptics,power-delay-ms",
				&pwr->power_delay_ms);

		pwr->vdd.control = 0;
		prop = of_find_property(np, "synaptics,vdd-control", NULL);
		if (prop && prop->length)
			of_property_read_u32(np, "synaptics,vdd-control",
				&pwr->vdd.control);

		pwr->vdd.regulator_name = NULL;
		prop = of_find_property(np, "synaptics,vdd-name", NULL);
		if (prop && prop->length)
			of_property_read_string(np, "synaptics,vdd-name",
				&pwr->vdd.regulator_name);

		pwr->vdd.gpio = -1;
		prop = of_find_property(np, "synaptics,vdd-gpio", NULL);
		if (prop && prop->length)
			pwr->vdd.gpio = of_get_named_gpio(np, "synaptics,vdd-gpio", 0);

		pwr->vdd.power_on_delay_ms = 0;
		prop = of_find_property(np, "synaptics,vdd-power-on-delay-ms", NULL);
		if (prop && prop->length)
			of_property_read_u32(np, "synaptics,vdd-power-on-delay-ms",
				&pwr->vdd.power_on_delay_ms);

		pwr->vdd.power_off_delay_ms = 0;
		prop = of_find_property(np, "synaptics,vdd-power-off-delay-ms", NULL);
		if (prop && prop->length)
			of_property_read_u32(np, "synaptics,vdd-power-off-delay-ms",
				&pwr->vdd.power_off_delay_ms);

		pwr->vio.control = 0;
		prop = of_find_property(np, "synaptics,vio-control", NULL);
		if (prop && prop->length)
			of_property_read_u32(np, "synaptics,vio-control", &pwr->vio.control);

		pwr->vio.regulator_name = NULL;
		prop = of_find_property(np, "synaptics,vio-name", NULL);
		if (prop && prop->length)
			of_property_read_string(np, "synaptics,vio-name", &pwr->vio.regulator_name);

		pwr->vio.gpio = -1;
		prop = of_find_property(np, "synaptics,vio-gpio", NULL);
		if (prop && prop->length)
			pwr->vio.gpio = of_get_named_gpio(np, "synaptics,vio-gpio", 0);

		pwr->vio.power_on_delay_ms = 0;
		prop = of_find_property(np, "synaptics,vio-power-on-delay-ms", NULL);
		if (prop && prop->length)
			of_property_read_u32(np, "synaptics,vio-power-on-delay-ms",
				&pwr->vio.power_on_delay_ms);

		pwr->vio.power_off_delay_ms = 0;
		prop = of_find_property(np, "synaptics,vio-power-off-delay-ms", NULL);
		if (prop && prop->length)
			of_property_read_u32(np, "synaptics,vio-power-off-delay-ms",
				&pwr->vio.power_off_delay_ms);
	}

	rst = &p_hw_i2c_if->bdata_rst;
	if (rst) {
		rst->reset_on_state = 0;
		prop = of_find_property(np, "synaptics,reset-on-state", NULL);
		if (prop && prop->length)
			of_property_read_u32(np, "synaptics,reset-on-state",
				&rst->reset_on_state);

		rst->reset_gpio = -1;
		prop = of_find_property(np, "synaptics,reset-gpio", NULL);
		if (prop && prop->length)
			rst->reset_gpio = of_get_named_gpio(np, "synaptics,reset-gpio", 0);

		prop = of_find_property(np, "synaptics,reset-active-ms", NULL);
		if (prop && prop->length)
			of_property_read_u32(np, "synaptics,reset-active-ms",
				&rst->reset_active_ms);

		prop = of_find_property(np, "synaptics,reset-delay-ms", NULL);
		if (prop && prop->length)
			of_property_read_u32(np, "synaptics,reset-delay-ms",
				&rst->reset_delay_ms);
	}

	bus = &p_hw_i2c_if->bdata_io;
	if (bus) {
		bus->switch_gpio = -1;
		prop = of_find_property(np, "synaptics,io-switch-gpio", NULL);
		if (prop && prop->length)
			bus->switch_gpio = of_get_named_gpio(np, "synaptics,io-switch-gpio", 0);

		prop = of_find_property(np, "synaptics,io-switch-state", NULL);
		if (prop && prop->length)
			of_property_read_u32(np, "synaptics,io-switch-state", &bus->switch_state);
	}

	prop = of_find_property(np, "synaptics,chunks", NULL);
	if (prop && prop->length) {
		retval = of_property_read_u32_array(np, "synaptics,chunks", temp_value, 2);
		if (retval >= 0) {
			p_hw_i2c_if->hw_platform.rd_chunk_size = temp_value[0];
			p_hw_i2c_if->hw_platform.wr_chunk_size = temp_value[1];
		}
	}

	LOGI("Load from dt: chunk size(%d %d) reset (%d %d) vdd delay(%d %d) vio delay(%d %d)\n",
		p_hw_i2c_if->hw_platform.rd_chunk_size,
		p_hw_i2c_if->hw_platform.wr_chunk_size,
		p_hw_i2c_if->bdata_rst.reset_active_ms,
		p_hw_i2c_if->bdata_rst.reset_delay_ms,
		p_hw_i2c_if->bdata_pwr.vdd.power_on_delay_ms,
		p_hw_i2c_if->bdata_pwr.vdd.power_off_delay_ms,
		p_hw_i2c_if->bdata_pwr.vio.power_on_delay_ms,
		p_hw_i2c_if->bdata_pwr.vio.power_off_delay_ms);

	product = &p_hw_i2c_if->product;
	if (product) {
		prop = of_find_property(np, "synaptics,flash-access-delay-us", NULL);
		if (prop && prop->length) {
			retval = of_property_read_u32_array(np, "synaptics,flash-access-delay-usy",
					temp_value, 3);
			if (retval >= 0) {
				product->timings.flash_ops_delay_us[0] = temp_value[0];
				product->timings.flash_ops_delay_us[1] = temp_value[1];
				product->timings.flash_ops_delay_us[2] = temp_value[2];
			}
		}

		prop = of_find_property(np, "synaptics,command-timeout-ms", NULL);
		if (prop && prop->length)
			retval = of_property_read_u32(np, "synaptics,command-timeout-ms",
					&product->timings.cmd_timeout_ms);

		prop = of_find_property(np, "synaptics,command-polling-ms", NULL);
		if (prop && prop->length)
			retval = of_property_read_u32(np, "synaptics,command-polling-ms",
					&product->timings.cmd_polling_ms);

		prop = of_find_property(np, "synaptics,command-turnaround-us", NULL);
		if (prop && prop->length)
			retval = of_property_read_u32(np, "synaptics,command-turnaround-us",
					&product->timings.cmd_turnaround_us);

		prop = of_find_property(np, "synaptics,command-retry-ms", NULL);
		if (prop && prop->length)
			retval = of_property_read_u32(np, "synaptics,command-retry-ms",
					&product->timings.cmd_retry_ms);

		prop = of_find_property(np, "synaptics,fw-switch-delay-ms", NULL);
		if (prop && prop->length)
			retval = of_property_read_u32(np, "synaptics,fw-switch-delay-ms",
					&product->timings.fw_switch_delay_ms);

		LOGI("Load from dt: command timeout(%d) turnaround time(%d) retry time(%d)\n",
			product->timings.cmd_timeout_ms, product->timings.cmd_turnaround_us,
			product->timings.cmd_retry_ms);
		LOGI("Load from dt: fw switch(%d) flash erase(%d) flash write(%d) flash read(%d)\n",
			product->timings.fw_switch_delay_ms,
			product->timings.flash_ops_delay_us[0],
			product->timings.flash_ops_delay_us[1],
			product->timings.flash_ops_delay_us[2]);
	}

	return 0;
}
#endif

/*
 * Release the resources for the use of ATTN.
 *
 * param
 *    void
 *
 * return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_release_attn_resources(void)
{
	struct syna_hw_attn_data *attn;

	if (!p_hw_i2c_if)
		return -EINVAL;

	attn = &p_hw_i2c_if->bdata_attn;
	if (!attn)
		return -EINVAL;

	syna_pal_mutex_free(&attn->irq_en_mutex);

	if (attn->irq_gpio > 0)
		syna_i2c_put_gpio(attn->irq_gpio);

	return 0;
}
/*
 * Initialize the resources for the use of ATTN.
 *
 * param
 *    void
 *
 * return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_request_attn_resources(void)
{
	int retval;
	static char str_attn_gpio[32] = {0};
	struct syna_hw_attn_data *attn;

	if (!p_hw_i2c_if)
		return -EINVAL;

	attn = &p_hw_i2c_if->bdata_attn;
	if (!attn)
		return -EINVAL;

	syna_pal_mutex_alloc(&attn->irq_en_mutex);

	if (attn->irq_gpio > 0) {
		retval = syna_i2c_get_gpio(attn->irq_gpio, 0, 0, str_attn_gpio);
		if (retval < 0) {
			LOGE("Fail to request GPIO %d for attention\n",
				attn->irq_gpio);
			return retval;
		}
	}

	return 0;
}
/*
 * Release the resources for the use of hardware reset.
 *
 * param
 *    void
 *
 * return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_release_reset_resources(void)
{
	struct syna_hw_rst_data *rst;

	if (!p_hw_i2c_if)
		return -EINVAL;

	rst = &p_hw_i2c_if->bdata_rst;
	if (!rst)
		return -EINVAL;

	if (rst->reset_gpio > 0)
		syna_i2c_put_gpio(rst->reset_gpio);

	return 0;
}
/*
 * Initialize the resources for the use of hardware reset.
 *
 * param
 *    void
 *
 * return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_request_reset_resources(void)
{
	int retval;
	static char str_rst_gpio[32] = {0};
	struct syna_hw_rst_data *rst;

	if (!p_hw_i2c_if)
		return -EINVAL;

	rst = &p_hw_i2c_if->bdata_rst;
	if (!rst)
		return -EINVAL;

	if (rst->reset_gpio > 0) {
		retval = syna_i2c_get_gpio(rst->reset_gpio, 1, !rst->reset_on_state, str_rst_gpio);
		if (retval < 0) {
			LOGE("Fail to request GPIO %d for reset\n", rst->reset_gpio);
			return retval;
		}
	}

	return 0;
}
/*
 * Release the resources for the use of bus transferring.
 *
 * param
 *    void
 *
 * return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_release_bus_resources(void)
{
	struct syna_hw_bus_data *bus;

	if (!p_hw_i2c_if)
		return -EINVAL;

	bus = &p_hw_i2c_if->bdata_io;
	if (!bus)
		return -EINVAL;

	syna_pal_mutex_free(&bus->io_mutex);

	if (bus->switch_gpio > 0)
		syna_i2c_put_gpio(bus->switch_gpio);

	return 0;
}
/*
 * Initialize the resources for the use of bus transferring.
 *
 * param
 *    void
 *
 * return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_request_bus_resources(void)
{
	int retval;
	static char str_switch_gpio[32] = {0};
	struct syna_hw_bus_data *bus;

	if (!p_hw_i2c_if)
		return -EINVAL;

	bus = &p_hw_i2c_if->bdata_io;
	if (!bus)
		return -EINVAL;

	syna_pal_mutex_alloc(&bus->io_mutex);

	if (bus->switch_gpio > 0) {
		retval = syna_i2c_get_gpio(bus->switch_gpio, 1, bus->switch_state, str_switch_gpio);
		if (retval < 0) {
			LOGE("Fail to request GPIO %d for io switch\n", bus->switch_gpio);
			return retval;
		}
	}

	return 0;
}
/*
 * Release the resources for the use of power control.
 *
 * param
 *    void
 *
 * return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_release_power_resources(void)
{
	struct syna_hw_pwr_data *pwr;

	if (!p_hw_i2c_if)
		return -EINVAL;

	pwr = &p_hw_i2c_if->bdata_pwr;
	if (!pwr)
		return -EINVAL;

	/* release power resource for vio */
	if (pwr->vio.control == PSU_REGULATOR) {
		if (pwr->vio.regulator_dev)
			syna_i2c_put_regulator(pwr->vio.regulator_dev);
	} else if (pwr->vio.control > 0) {
		if (pwr->vio.gpio > 0)
			syna_i2c_put_gpio(pwr->vio.gpio);
	}
	/* release power resource for VDD */
	if (pwr->vdd.control == PSU_REGULATOR) {
		if (pwr->vdd.regulator_dev)
			syna_i2c_put_regulator(pwr->vdd.regulator_dev);
	} else if (pwr->vdd.control > 0) {
		if (pwr->vdd.gpio > 0)
			syna_i2c_put_gpio(pwr->vdd.gpio);
	}

	return 0;
}
/*
 * Initialize the resources for the use of power control.
 *
 * param
 *    void
 *
 * return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_request_power_resources(void)
{
	int retval;
	static char str_vdd_gpio[32] = {0};
	static char str_avdd_gpio[32] = {0};
	struct syna_hw_pwr_data *pwr;

	if (!p_hw_i2c_if)
		return -EINVAL;

	pwr = &p_hw_i2c_if->bdata_pwr;
	if (!pwr)
		return -EINVAL;

	if (pwr->vdd.control == 0) {
		if (pwr->vdd.regulator_name && (strlen(pwr->vdd.regulator_name) > 0))
			pwr->vdd.control = PSU_REGULATOR;
	}
	if (pwr->vio.control == 0) {
		if (pwr->vio.regulator_name && (strlen(pwr->vio.regulator_name) > 0))
			pwr->vio.control = PSU_REGULATOR;
	}

	/* request power resource for  VDD */
	if (pwr->vdd.control == PSU_REGULATOR) {
		if (!pwr->vdd.regulator_name || (strlen(pwr->vdd.regulator_name) <= 0)) {
			LOGE("Fail to get regulator for vdd, no given name of vdd\n");
			return -ENXIO;
		}
		pwr->vdd.regulator_dev = syna_i2c_get_regulator(pwr->vdd.regulator_name);
		if (IS_ERR((struct regulator *)pwr->vdd.regulator_dev)) {
			LOGE("Fail to request regulator for vdd\n");
			return -ENXIO;
		}
	} else if (pwr->vdd.control == PSU_GPIO) {
		if (pwr->vdd.gpio > 0) {
			retval = syna_i2c_get_gpio(pwr->vdd.gpio, 1,
				!pwr->power_on_state,
				str_avdd_gpio);
			if (retval < 0) {
				LOGE("Fail to request GPIO %d for vdd\n", pwr->vdd.gpio);
				return retval;
			}
		}
	}
	/* request power resource for VIO */
	if (pwr->vio.control == PSU_REGULATOR) {
		if (!pwr->vio.regulator_name || (strlen(pwr->vio.regulator_name) <= 0)) {
			LOGE("Fail to get regulator for vio, no given name of vio\n");
			return -ENXIO;
		}
		pwr->vio.regulator_dev = syna_i2c_get_regulator(pwr->vio.regulator_name);
		if (IS_ERR((struct regulator *)pwr->vio.regulator_dev)) {
			LOGE("Fail to configure regulator for vio\n");
			return -ENXIO;
		}
	} else if (pwr->vio.control == PSU_GPIO)  {
		if (pwr->vio.gpio > 0) {
			retval = syna_i2c_get_gpio(pwr->vio.gpio, 1,
				!pwr->power_on_state,
				str_vdd_gpio);
			if (retval < 0) {
				LOGE("Fail to request GPIO %d for vio\n", pwr->vio.gpio);
				return retval;
			}
		}
	}

	return 0;
}


/*
 * Enable or disable the kernel irq.
 *
 * param
 *    [ in] hw:    pointer to the hardware platform
 *    [ in] en:    '1' for enabling, and '0' for disabling
 *
 * return
 *    0 in case of nothing changed, positive value in case of success, a negative value otherwise.
 */
static int syna_i2c_enable_irq(struct tcm_hw_platform *hw, bool en)
{
	int retval = 0;
	struct syna_hw_interface *hw_if = (struct syna_hw_interface *)hw->device;
	struct syna_hw_attn_data *attn;

	if (!hw_if) {
		LOGE("Invalid handle of hw_if\n");
		return -ENXIO;
	}

	attn = &hw_if->bdata_attn;
	if (!attn || (attn->irq_id == 0))
		return -ENXIO;

	syna_pal_mutex_lock(&attn->irq_en_mutex);

	/* enable the handling of interrupt */
	if (en) {
		if (attn->irq_enabled) {
			LOGD("Interrupt already enabled\n");
			goto exit;
		}

		enable_irq(attn->irq_id);
		attn->irq_enabled = true;
		retval = 1;
		LOGD("Interrupt enabled\n");
	}
	/* disable the handling of interrupt */
	else {
		if (!attn->irq_enabled) {
			LOGD("Interrupt already disabled\n");
			goto exit;
		}

		disable_irq_nosync(attn->irq_id);
		attn->irq_enabled = false;
		retval = 1;
		LOGD("Interrupt disabled\n");
	}

exit:
	syna_pal_mutex_unlock(&attn->irq_en_mutex);

	return retval;
}
/*
 * Toggle the hardware gpio pin to perform the chip reset.
 *
 * param
 *    void
 *
 * return
 *     void.
 */
static void syna_i2c_hw_reset(void)
{
	struct syna_hw_rst_data *rst;

	if (!p_hw_i2c_if)
		return;

	rst = &p_hw_i2c_if->bdata_rst;
	if (!rst)
		return;

	if (rst->reset_gpio == 0)
		return;

	LOGD("Prepare to toggle reset, hold:%d delay:%d\n",
		rst->reset_active_ms, rst->reset_delay_ms);

	gpio_set_value(rst->reset_gpio, (rst->reset_on_state & 0x01));
	syna_pal_sleep_ms(rst->reset_active_ms);
	gpio_set_value(rst->reset_gpio, ((!rst->reset_on_state) & 0x01));
	syna_pal_sleep_ms(rst->reset_delay_ms);

	LOGD("Reset done\n");

}
/*
 * Set up a power source to the requested state
 *
 * param
 *    [ in] pwr:   specify a power source to set up
 *    [ in] on:    flag for powering on / off
 *    [ in] state: indicate the power state
 *
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_i2c_power_setup(struct power_setup *pwr, bool on, int state)
{
	int retval = 0;

	if (!pwr)
		return -EINVAL;

	if (pwr->control < 0) {
		LOGD("Invalid power source %d\n", pwr->control);
		return -EINVAL;
	}

	if (pwr->control == 0)
		return 0; /* unused power source */

	if (pwr->control == PSU_REGULATOR) {
		if (IS_ERR((struct regulator *)pwr->regulator_dev)) {
			LOGE("Invalid regulator (%s)\n", pwr->regulator_name);
			return -EINVAL;
		}
		if (on)
			retval = regulator_enable((struct regulator *)pwr->regulator_dev);
		else
			retval = regulator_disable((struct regulator *)pwr->regulator_dev);

		if (retval < 0) {
			LOGE("Failed to %s regulator (%s)\n",
				on ? "enable" : "disable", pwr->regulator_name);
			return retval;
		}
	} else {
		if (pwr->gpio > 0)
			gpio_set_value(pwr->gpio, on ? state : !state);
	}

	/* Delay after power operation */
	if (on && pwr->power_on_delay_ms > 0)
		syna_pal_sleep_ms(pwr->power_on_delay_ms);
	else if (!on && pwr->power_off_delay_ms > 0)
		syna_pal_sleep_ms(pwr->power_off_delay_ms);

	return 0;
}
/*
 * Power on touch controller through regulators or gpios.
 *
 * param
 *    [ in] on:    '1' for powering on, and '0' for powering off
 *
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_i2c_power_on(bool on)
{
	int retval = 0;
	struct syna_hw_pwr_data *pwr;

	if (!p_hw_i2c_if)
		return -EINVAL;

	pwr = &p_hw_i2c_if->bdata_pwr;
	if (!pwr)
		return -EINVAL;

	LOGD("Prepare to %s power ...\n", (on) ? "enable" : "disable");

	if (on) {
		retval = syna_i2c_power_setup(&pwr->vdd, true, pwr->power_on_state);
		if (retval < 0) {
			LOGE("Fail to power on VDD\n");
			goto exit;
		}

		retval = syna_i2c_power_setup(&pwr->vio, true, pwr->power_on_state);
		if (retval < 0) {
			LOGE("Fail to power on VIO\n");
			goto exit;
		}
	} else {
		retval = syna_i2c_power_setup(&pwr->vio, false, pwr->power_on_state);
		if (retval < 0) {
			LOGE("Fail to power off VDD\n");
			goto exit;
		}

		retval = syna_i2c_power_setup(&pwr->vdd, false, pwr->power_on_state);
		if (retval < 0) {
			LOGE("Fail to power off VIO\n");
			goto exit;
		}
	}

	LOGI("Device power %s\n", (on) ? "On" : "Off");

exit:
	return retval;
}
/*
 * Implement the I2C transaction to read out data over I2C bus.
 *
 * param
 *    [ in] hw:      pointer to the hardware platform
 *    [out] rd_data: buffer for storing data retrieved from device
 *    [ in] rd_len:  number of bytes retrieved from device
 *
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_i2c_read(struct tcm_hw_platform *hw, unsigned char *rd_data,
	unsigned int rd_len)
{
	int retval;
	unsigned int attempt;
	struct i2c_msg msg;
	struct i2c_client *i2c;
	struct syna_hw_bus_data *bus;

	if (!p_hw_i2c_if)
		return -EINVAL;

	i2c = p_hw_i2c_if->pdev;
	bus = &p_hw_i2c_if->bdata_io;
	if (!i2c || !bus) {
		LOGE("Invalid bus io device\n");
		return -ENXIO;
	}

	syna_pal_mutex_lock(&bus->io_mutex);

	if ((rd_len & 0xffff) == 0xffff) {
		LOGE("Invalid read length 0x%X\n", (rd_len & 0xffff));
		retval = -EINVAL;
		goto exit;
	}

	msg.addr = i2c->addr;
	msg.flags = I2C_M_RD;
	msg.len = rd_len;
	msg.buf = rd_data;

	for (attempt = 0; attempt < XFER_ATTEMPTS; attempt++) {
		retval = i2c_transfer(i2c->adapter, &msg, 1);
		if (retval == 1) {
			retval = rd_len;
			goto exit;
		}
		LOGE("Transfer attempt %d failed at addr 0x%02x\n",
			attempt + 1, i2c->addr);

		if (attempt + 1 == XFER_ATTEMPTS) {
			retval = -EIO;
			goto exit;
		}

		syna_pal_sleep_ms(20);
	}

#if defined(CONFIG_TOUCHSCREEN_SYNA_TCM2_DEBUG_MSG)
	struct syna_hw_interface *hw_if = (struct syna_hw_interface *)hw->device;

	if (hw_if->debug_trace) {
		int idx;
		unsigned char *dbg_str;
		int dbg_len = (rd_len > hw_if->debug_trace) ? hw_if->debug_trace : rd_len;

		dbg_str = syna_pal_mem_alloc(dbg_len * 3 + 3, sizeof(unsigned char));
		if (dbg_str) {
			for (idx = 0; idx < dbg_len; idx++) {
				unsigned char strbuff[6];

				syna_pal_mem_set(strbuff, 0x00, 6);
				snprintf(strbuff, 6, "%02X ", rd_data[idx]);
				strlcat(dbg_str, strbuff, dbg_len * 3 + 3);
			}
			if (rd_len > hw_if->debug_trace)
				strlcat(dbg_str, "...", dbg_len * 3 + 3);
			LOGD("RD size:%d [%s]\n", rd_len, dbg_str);
			syna_pal_mem_free(dbg_str);
		}
	}
#endif
exit:
	syna_pal_mutex_unlock(&bus->io_mutex);

	return retval;
}

/*
 * Implement the I2C transaction to write data over I2C bus.
 *
 * param
 *    [ in] hw:      pointer to the hardware platform
 *    [ in] wr_data: written data
 *    [ in] wr_len:  length of written data in bytes
 *
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_i2c_write(struct tcm_hw_platform *hw, unsigned char *wr_data,
	unsigned int wr_len)
{
	int retval;
	unsigned int attempt;
	struct i2c_msg msg;
	struct i2c_client *i2c;
	struct syna_hw_bus_data *bus;

	if (!p_hw_i2c_if)
		return -EINVAL;

	i2c = p_hw_i2c_if->pdev;
	bus = &p_hw_i2c_if->bdata_io;
	if (!i2c || !bus) {
		LOGE("Invalid bus io device\n");
		return -ENXIO;
	}

	syna_pal_mutex_lock(&bus->io_mutex);

	if ((wr_len & 0xffff) == 0xffff) {
		LOGE("Invalid write length 0x%X\n", (wr_len & 0xffff));
		retval = -EINVAL;
		goto exit;
	}

	msg.addr = i2c->addr;
	msg.flags = 0;
	msg.len = wr_len;
	msg.buf = wr_data;

	for (attempt = 0; attempt < XFER_ATTEMPTS; attempt++) {
		retval = i2c_transfer(i2c->adapter, &msg, 1);
		if (retval == 1) {
			retval = wr_len;
			goto exit;
		}
		LOGE("Transfer attempt %d failed at addr 0x%02x\n",
			attempt + 1, i2c->addr);

		if (attempt + 1 == XFER_ATTEMPTS) {
			retval = -EIO;
			goto exit;
		}

		syna_pal_sleep_ms(20);
	}

#if defined(CONFIG_TOUCHSCREEN_SYNA_TCM2_DEBUG_MSG)
	struct syna_hw_interface *hw_if = (struct syna_hw_interface *)hw->device;

	if (hw_if->debug_trace) {
		int idx;
		unsigned char *dbg_str;
		int dbg_len = (wr_len > hw_if->debug_trace) ? hw_if->debug_trace : wr_len;

		dbg_str = syna_pal_mem_alloc(dbg_len * 3 + 3, sizeof(unsigned char));
		if (dbg_str) {
			for (idx = 0; idx < dbg_len; idx++) {
				unsigned char strbuff[6];

				syna_pal_mem_set(strbuff, 0x00, 6);
				snprintf(strbuff, 6, "%02X ", wr_data[idx]);
				strlcat(dbg_str, strbuff, dbg_len * 3 + 3);
			}
			if (wr_len > hw_if->debug_trace)
				strlcat(dbg_str, "...", dbg_len * 3 + 3);
			LOGD("WR size:%d [%s]\n", wr_len, dbg_str);
			syna_pal_mem_free(dbg_str);
		}
	}
#endif
exit:
	syna_pal_mutex_unlock(&bus->io_mutex);

	return retval;
}


/*
 * Release the platform I2C device.
 *
 * param
 *    [ in] dev: pointer to device
 *
 * return
 *    none
 */
static void syna_i2c_release(struct device *dev)
{
	LOGI("I2C device removed\n");
}
/*
 * Probe and register the platform i2c device.
 *
 * param
 *    [ in] i2c:    i2c client device
 *    [ in] dev_id: i2c device id
 *
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
#if (KERNEL_VERSION(6, 4, 0) <= LINUX_VERSION_CODE)
static int syna_i2c_probe(struct i2c_client *i2c)
#else
static int syna_i2c_probe(struct i2c_client *i2c,
		const struct i2c_device_id *dev_id)
#endif
{
	int retval;

	if (!p_hw_i2c_if) {
		LOGE("Invalid i2c hardware interface\n");
		return -ENXIO;
	}

	/* initialize the hardware interface */
	p_hw_i2c_if->hw_platform.type = BUS_TYPE_I2C;
	p_hw_i2c_if->hw_platform.rd_chunk_size = RD_CHUNK_SIZE;
	p_hw_i2c_if->hw_platform.wr_chunk_size = WR_CHUNK_SIZE;
	p_hw_i2c_if->hw_platform.ops_read_data = syna_i2c_read;
	p_hw_i2c_if->hw_platform.ops_write_data = syna_i2c_write;
	p_hw_i2c_if->hw_platform.ops_enable_attn = syna_i2c_enable_irq;
	p_hw_i2c_if->hw_platform.support_attn = true;
#ifdef DATA_ALIGNMENT
	p_hw_i2c_if->hw_platform.alignment_base = ALIGNMENT_BASE;
	p_hw_i2c_if->hw_platform.alignment_boundary = ALIGNMENT_SIZE_BOUNDARY;
#endif
	p_hw_i2c_if->ops_power_on = syna_i2c_power_on;
	p_hw_i2c_if->ops_hw_reset = syna_i2c_hw_reset;

	i2c_set_clientdata(i2c, p_hw_i2c_if->platform_device);

	p_hw_i2c_if->pdev = i2c;
	p_hw_i2c_if->hw_platform.device = p_hw_i2c_if;

#ifdef CONFIG_OF
	syna_i2c_parse_dt();
#endif

	/* initialize resources for the use of power */
	retval = syna_i2c_request_power_resources();
	if (retval < 0) {
		LOGE("Fail to request power-related resources\n");
		return retval;
	}

	/* initialize resources for the use of bus transferring */
	retval = syna_i2c_request_bus_resources();
	if (retval < 0) {
		LOGE("Fail to request bus-related resources\n");
		return retval;
	}

	/* initialize resources for the use of reset */
	retval = syna_i2c_request_reset_resources();
	if (retval < 0) {
		LOGE("Fail to request reset-related resources\n");
		return retval;
	}

	/* initialize resources for the use of attn */
	retval = syna_i2c_request_attn_resources();
	if (retval < 0) {
		LOGE("Fail to request attn-related resources\n");
		return retval;
	}

	return 0;
}

/*
 * Unregister the platform i2c device.
 *
 * param
 *    [ in] i2c: i2c client device
 *
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
#if (KERNEL_VERSION(5, 18, 0) <= LINUX_VERSION_CODE)
static void syna_i2c_remove(struct i2c_client *i2c)
#else
static int syna_i2c_remove(struct i2c_client *i2c)
#endif
{
	/* release resources */
	syna_i2c_release_attn_resources();
	syna_i2c_release_reset_resources();
	syna_i2c_release_bus_resources();
	syna_i2c_release_power_resources();

#if (KERNEL_VERSION(5, 18, 0) <= LINUX_VERSION_CODE)
	return;
#else
	return 0;
#endif
}


/* Example of an i2c device driver */
static const struct i2c_device_id syna_i2c_id_table[] = {
	{"tcm-i2c", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, syna_i2c_id_table);

#ifdef CONFIG_OF
static const struct of_device_id syna_i2c_of_match_table[] = {
	{
		.compatible = "synaptics,tcm-i2c",
	},
	{},
};
MODULE_DEVICE_TABLE(of, syna_i2c_of_match_table);
#else
#define syna_i2c_of_match_table NULL
#endif

static struct i2c_driver syna_i2c_driver = {
	.driver = {
		.name = I2C_MODULE_NAME,
		.of_match_table = syna_i2c_of_match_table,
	},
	.probe = syna_i2c_probe,
	.remove = syna_i2c_remove,
	.id_table = syna_i2c_id_table,
};

/*
 * Register the hardware interface module.
 *
 * param
 *    void
 *
 * return
 *    0 or positive value in case of success, a negative value otherwise.
 */
int syna_hw_interface_bind(void)
{
	struct platform_device *p_device = NULL;

	/* allocate the hardware interface module */
	p_hw_i2c_if = kcalloc(1, sizeof(struct syna_hw_interface), GFP_KERNEL);
	if (!p_hw_i2c_if) {
		LOGE("Fail to allocate hardware interface module\n");
		return -ENOMEM;
	}

	/* register the platform device */
	p_device = platform_device_alloc(PLATFORM_DRIVER_NAME, -1);
	if (!p_device) {
		LOGE("Fail to allocate platform device\n");
		kfree(p_hw_i2c_if);
		p_hw_i2c_if = NULL;
		return -ENOMEM;
	}
	p_device->dev.release = syna_i2c_release;
	p_hw_i2c_if->platform_device = p_device;

	/* add the platform device */
	if (platform_device_add(p_device) < 0) {
		LOGE("Fail to add platform device\n");
		platform_device_put(p_device);
		p_device = NULL;
		kfree(p_hw_i2c_if);
		p_hw_i2c_if = NULL;
		return -ENOMEM;
	}

	p_device->dev.platform_data = p_hw_i2c_if;

	return i2c_add_driver(&syna_i2c_driver);
}

/*
 * Remove the hardware interface module.
 *
 * param
 *    void
 *
 * return
 *    void.
 */
void syna_hw_interface_unbind(void)
{
	/* unregister the platform device */
	if (p_hw_i2c_if->platform_device)
		platform_device_unregister(p_hw_i2c_if->platform_device);

	i2c_del_driver(&syna_i2c_driver);

	kfree(p_hw_i2c_if);
	p_hw_i2c_if = NULL;
}

MODULE_DESCRIPTION("Synaptics TouchComm I2C Bus Module");
MODULE_LICENSE("GPL");

