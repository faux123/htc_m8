/* Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/debugfs.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/clk.h>
#include <linux/mfd/pmic8058.h>
#include <asm/mach-types.h>
#include <asm/uaccess.h>
#include <mach/mdm2.h>
#include <mach/restart.h>
#include <mach/subsystem_notif.h>
#include <mach/subsystem_restart.h>
#include <linux/msm_charm.h>
#include "msm_watchdog.h"
#include "devices.h"
#include "clock.h"
#include "mdm_private.h"
#define MDM_PBLRDY_CNT		20

static int mdm_debug_mask;

static void mdm_peripheral_connect(struct mdm_modem_drv *mdm_drv)
{
	if (!mdm_drv->pdata->peripheral_platform_device)
		return;

	mutex_lock(&mdm_drv->peripheral_status_lock);
	if (mdm_drv->peripheral_status)
		goto out;
	platform_device_add(mdm_drv->pdata->peripheral_platform_device);
	mdm_drv->peripheral_status = 1;
out:
	mutex_unlock(&mdm_drv->peripheral_status_lock);
}

static void mdm_peripheral_disconnect(struct mdm_modem_drv *mdm_drv)
{
	if (!mdm_drv->pdata->peripheral_platform_device)
		return;

	mutex_lock(&mdm_drv->peripheral_status_lock);
	if (!mdm_drv->peripheral_status)
		goto out;
	platform_device_del(mdm_drv->pdata->peripheral_platform_device);
	mdm_drv->peripheral_status = 0;
out:
	mutex_unlock(&mdm_drv->peripheral_status_lock);
}

static void mdm_toggle_soft_reset(struct mdm_modem_drv *mdm_drv)
{
	int soft_reset_direction_assert = 0,
	    soft_reset_direction_de_assert = 1;

	if (mdm_drv->pdata->soft_reset_inverted) {
		soft_reset_direction_assert = 1;
		soft_reset_direction_de_assert = 0;
	}
	gpio_direction_output(mdm_drv->ap2mdm_soft_reset_gpio,
			soft_reset_direction_assert);
	mdelay(10);
	gpio_direction_output(mdm_drv->ap2mdm_soft_reset_gpio,
			soft_reset_direction_de_assert);
}

static void mdm_atomic_soft_reset(struct mdm_modem_drv *mdm_drv)
{
	mdm_toggle_soft_reset(mdm_drv);
}

static void mdm_power_down_common(struct mdm_modem_drv *mdm_drv)
{
	int i;
	int soft_reset_direction =
		mdm_drv->pdata->soft_reset_inverted ? 1 : 0;

	mdm_peripheral_disconnect(mdm_drv);

	
	for (i = 20; i > 0; i--) {
		if (gpio_get_value(mdm_drv->mdm2ap_status_gpio) == 0) {
			if (mdm_debug_mask & MDM_DEBUG_MASK_SHDN_LOG)
				pr_debug("%s:id %d: mdm2ap_statuswent low, i=%d\n",
					__func__, mdm_drv->device_id, i);
			break;
		}
		msleep(100);
	}

	
	gpio_direction_output(mdm_drv->ap2mdm_soft_reset_gpio,
					soft_reset_direction);
	if (i == 0) {
		pr_debug("%s:id %d: MDM2AP_STATUS never went low. Doing a hard reset\n",
			   __func__, mdm_drv->device_id);
		gpio_direction_output(mdm_drv->ap2mdm_soft_reset_gpio,
					soft_reset_direction);
		msleep(4000);
	}
}

static void mdm_do_first_power_on(struct mdm_modem_drv *mdm_drv)
{
	int i;
	int pblrdy;
	if (mdm_drv->power_on_count != 1) {
		pr_debug("%s:id %d: Calling fn when power_on_count != 1\n",
			   __func__, mdm_drv->device_id);
		return;
	}

	pr_debug("%s:id %d: Powering on modem for the first time\n",
		   __func__, mdm_drv->device_id);
	mdm_peripheral_disconnect(mdm_drv);

	mdm_toggle_soft_reset(mdm_drv);
	
	if (GPIO_IS_VALID(mdm_drv->ap2mdm_kpdpwr_n_gpio)) {
		pr_debug("%s:id %d: Pulling AP2MDM_KPDPWR gpio high\n",
				 __func__, mdm_drv->device_id);
		gpio_direction_output(mdm_drv->ap2mdm_kpdpwr_n_gpio, 1);
		gpio_direction_output(mdm_drv->ap2mdm_status_gpio, 1);
		msleep(1000);
		gpio_direction_output(mdm_drv->ap2mdm_kpdpwr_n_gpio, 0);
	} else {
		gpio_direction_output(mdm_drv->ap2mdm_status_gpio, 1);
	}

	if (!GPIO_IS_VALID(mdm_drv->mdm2ap_pblrdy))
		goto start_mdm_peripheral;

	for (i = 0; i  < MDM_PBLRDY_CNT; i++) {
		pblrdy = gpio_get_value(mdm_drv->mdm2ap_pblrdy);
		if (pblrdy)
			break;
		usleep_range(5000, 5000);
	}
	pr_debug("%s: id %d: pblrdy i:%d\n", __func__,
			 mdm_drv->device_id, i);

start_mdm_peripheral:
	mdm_peripheral_connect(mdm_drv);
	msleep(200);
}

static void mdm_do_soft_power_on(struct mdm_modem_drv *mdm_drv)
{
	int i;
	int pblrdy;

	pr_debug("%s: id %d:  soft resetting mdm modem\n",
		   __func__, mdm_drv->device_id);
	mdm_peripheral_disconnect(mdm_drv);
	mdm_toggle_soft_reset(mdm_drv);

	if (!GPIO_IS_VALID(mdm_drv->mdm2ap_pblrdy))
		goto start_mdm_peripheral;

	for (i = 0; i  < MDM_PBLRDY_CNT; i++) {
		pblrdy = gpio_get_value(mdm_drv->mdm2ap_pblrdy);
		if (pblrdy)
			break;
		usleep_range(5000, 5000);
	}

	pr_debug("%s: id %d: pblrdy i:%d\n", __func__,
			 mdm_drv->device_id, i);

start_mdm_peripheral:
	mdm_peripheral_connect(mdm_drv);
	msleep(200);
}

static void mdm_power_on_common(struct mdm_modem_drv *mdm_drv)
{
	mdm_drv->power_on_count++;

	if (GPIO_IS_VALID(mdm_drv->ap2mdm_wakeup_gpio))
		gpio_direction_output(mdm_drv->ap2mdm_wakeup_gpio, 0);

	if (mdm_drv->pdata->early_power_on &&
			(mdm_drv->power_on_count == 2))
		return;

	if (mdm_drv->power_on_count == 1)
		mdm_do_first_power_on(mdm_drv);
	else
		mdm_do_soft_power_on(mdm_drv);
}

static void debug_state_changed(int value)
{
	mdm_debug_mask = value;
}

static void mdm_status_changed(struct mdm_modem_drv *mdm_drv, int value)
{
	pr_debug("%s: id %d: value:%d\n", __func__,
			 value, mdm_drv->device_id);

	if (value) {
		mdm_peripheral_disconnect(mdm_drv);
		mdm_peripheral_connect(mdm_drv);
		if (GPIO_IS_VALID(mdm_drv->ap2mdm_wakeup_gpio))
			gpio_direction_output(mdm_drv->ap2mdm_wakeup_gpio, 1);
	}
}

static void mdm_image_upgrade(struct mdm_modem_drv *mdm_drv, int type)
{
	switch (type) {
	case APQ_CONTROLLED_UPGRADE:
		pr_debug("%s: id %d: APQ controlled modem image upgrade\n",
				 __func__, mdm_drv->device_id);
		atomic_set(&mdm_drv->mdm_ready, 0);
		mdm_toggle_soft_reset(mdm_drv);
		break;
	case MDM_CONTROLLED_UPGRADE:
		pr_debug("%s: id %d: MDM controlled modem image upgrade\n",
				 __func__, mdm_drv->device_id);
		atomic_set(&mdm_drv->mdm_ready, 0);
		mdm_drv->disable_status_check = 1;
		if (GPIO_IS_VALID(mdm_drv->usb_switch_gpio)) {
			pr_debug("%s: id %d: Switching usb control to MDM\n",
					__func__, mdm_drv->device_id);
			gpio_direction_output(mdm_drv->usb_switch_gpio, 1);
		} else
			pr_err("%s: id %d: usb switch gpio unavailable\n",
				   __func__, mdm_drv->device_id);
		break;
	default:
		pr_err("%s: id %d: invalid upgrade type\n",
			   __func__, mdm_drv->device_id);
	}
}

static struct mdm_ops mdm_cb = {
	.power_on_mdm_cb = mdm_power_on_common,
	.reset_mdm_cb = mdm_power_on_common,
	.atomic_reset_mdm_cb = mdm_atomic_soft_reset,
	.power_down_mdm_cb = mdm_power_down_common,
	.debug_state_changed_cb = debug_state_changed,
	.status_cb = mdm_status_changed,
	.image_upgrade_cb = mdm_image_upgrade,
};

int mdm_get_ops(struct mdm_ops **mdm_ops)
{
	*mdm_ops = &mdm_cb;
	return 0;
}


