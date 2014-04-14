/*
 * Apple Motion Sensor driver (I2C variant)
 *
 * Copyright (C) 2005 Stelian Pop (stelian@popies.net)
 * Copyright (C) 2006 Michael Hanselmann (linux-kernel@hansmi.ch)
 *
 * Clean room implementation based on the reverse engineered Mac OS X driver by
 * Johannes Berg <johannes@sipsolutions.net>, documentation available at
 * http://johannes.sipsolutions.net/PowerBook/Apple_Motion_Sensor_Specification
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/delay.h>

#include "ams.h"

#define AMS_COMMAND	0x00	
#define AMS_STATUS	0x01	
#define AMS_CTRL1	0x02	
#define AMS_CTRL2	0x03	
#define AMS_CTRL3	0x04	
#define AMS_DATA1	0x05	
#define AMS_DATA2	0x06	
#define AMS_DATA3	0x07	
#define AMS_DATA4	0x08	
#define AMS_DATAX	0x20	
#define AMS_DATAY	0x21	
#define AMS_DATAZ	0x22	
#define AMS_FREEFALL	0x24	
#define AMS_SHOCK	0x25	
#define AMS_SENSLOW	0x26	
#define AMS_SENSHIGH	0x27	
#define AMS_CTRLX	0x28	
#define AMS_CTRLY	0x29	
#define AMS_CTRLZ	0x2A	
#define AMS_UNKNOWN1	0x2B	
#define AMS_UNKNOWN2	0x2C	
#define AMS_UNKNOWN3	0x2D	
#define AMS_VENDOR	0x2E	

enum ams_i2c_cmd {
	AMS_CMD_NOOP = 0,
	AMS_CMD_VERSION,
	AMS_CMD_READMEM,
	AMS_CMD_WRITEMEM,
	AMS_CMD_ERASEMEM,
	AMS_CMD_READEE,
	AMS_CMD_WRITEEE,
	AMS_CMD_RESET,
	AMS_CMD_START,
};

static int ams_i2c_probe(struct i2c_client *client,
			 const struct i2c_device_id *id);
static int ams_i2c_remove(struct i2c_client *client);

static const struct i2c_device_id ams_id[] = {
	{ "ams", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ams_id);

static struct i2c_driver ams_i2c_driver = {
	.driver = {
		.name   = "ams",
		.owner  = THIS_MODULE,
	},
	.probe          = ams_i2c_probe,
	.remove         = ams_i2c_remove,
	.id_table       = ams_id,
};

static s32 ams_i2c_read(u8 reg)
{
	return i2c_smbus_read_byte_data(ams_info.i2c_client, reg);
}

static int ams_i2c_write(u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(ams_info.i2c_client, reg, value);
}

static int ams_i2c_cmd(enum ams_i2c_cmd cmd)
{
	s32 result;
	int count = 3;

	ams_i2c_write(AMS_COMMAND, cmd);
	msleep(5);

	while (count--) {
		result = ams_i2c_read(AMS_COMMAND);
		if (result == 0 || result & 0x80)
			return 0;

		schedule_timeout_uninterruptible(HZ / 20);
	}

	return -1;
}

static void ams_i2c_set_irq(enum ams_irq reg, char enable)
{
	if (reg & AMS_IRQ_FREEFALL) {
		u8 val = ams_i2c_read(AMS_CTRLX);
		if (enable)
			val |= 0x80;
		else
			val &= ~0x80;
		ams_i2c_write(AMS_CTRLX, val);
	}

	if (reg & AMS_IRQ_SHOCK) {
		u8 val = ams_i2c_read(AMS_CTRLY);
		if (enable)
			val |= 0x80;
		else
			val &= ~0x80;
		ams_i2c_write(AMS_CTRLY, val);
	}

	if (reg & AMS_IRQ_GLOBAL) {
		u8 val = ams_i2c_read(AMS_CTRLZ);
		if (enable)
			val |= 0x80;
		else
			val &= ~0x80;
		ams_i2c_write(AMS_CTRLZ, val);
	}
}

static void ams_i2c_clear_irq(enum ams_irq reg)
{
	if (reg & AMS_IRQ_FREEFALL)
		ams_i2c_write(AMS_FREEFALL, 0);

	if (reg & AMS_IRQ_SHOCK)
		ams_i2c_write(AMS_SHOCK, 0);
}

static u8 ams_i2c_get_vendor(void)
{
	return ams_i2c_read(AMS_VENDOR);
}

static void ams_i2c_get_xyz(s8 *x, s8 *y, s8 *z)
{
	*x = ams_i2c_read(AMS_DATAX);
	*y = ams_i2c_read(AMS_DATAY);
	*z = ams_i2c_read(AMS_DATAZ);
}

static int ams_i2c_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int vmaj, vmin;
	int result;

	
	if (unlikely(ams_info.has_device))
		return -ENODEV;

	ams_info.i2c_client = client;

	if (ams_i2c_cmd(AMS_CMD_RESET)) {
		printk(KERN_INFO "ams: Failed to reset the device\n");
		return -ENODEV;
	}

	if (ams_i2c_cmd(AMS_CMD_START)) {
		printk(KERN_INFO "ams: Failed to start the device\n");
		return -ENODEV;
	}

	
	ams_i2c_write(AMS_CTRL1, 0x02);
	ams_i2c_write(AMS_CTRL2, 0x85);
	ams_i2c_write(AMS_CTRL3, 0x01);

	ams_i2c_cmd(AMS_CMD_READMEM);

	vmaj = ams_i2c_read(AMS_DATA1);
	vmin = ams_i2c_read(AMS_DATA2);
	if (vmaj != 1 || vmin != 52) {
		printk(KERN_INFO "ams: Incorrect device version (%d.%d)\n",
			vmaj, vmin);
		return -ENODEV;
	}

	ams_i2c_cmd(AMS_CMD_VERSION);

	vmaj = ams_i2c_read(AMS_DATA1);
	vmin = ams_i2c_read(AMS_DATA2);
	if (vmaj != 0 || vmin != 1) {
		printk(KERN_INFO "ams: Incorrect firmware version (%d.%d)\n",
			vmaj, vmin);
		return -ENODEV;
	}

	
	ams_i2c_set_irq(AMS_IRQ_ALL, 0);

	result = ams_sensor_attach();
	if (result < 0)
		return result;

	
	ams_i2c_write(AMS_SENSLOW, 0x15);
	ams_i2c_write(AMS_SENSHIGH, 0x60);
	ams_i2c_write(AMS_CTRLX, 0x08);
	ams_i2c_write(AMS_CTRLY, 0x0F);
	ams_i2c_write(AMS_CTRLZ, 0x4F);
	ams_i2c_write(AMS_UNKNOWN1, 0x14);

	
	ams_i2c_clear_irq(AMS_IRQ_ALL);

	ams_info.has_device = 1;

	
	ams_i2c_set_irq(AMS_IRQ_ALL, 1);

	printk(KERN_INFO "ams: Found I2C based motion sensor\n");

	return 0;
}

static int ams_i2c_remove(struct i2c_client *client)
{
	if (ams_info.has_device) {
		ams_sensor_detach();

		
		ams_i2c_set_irq(AMS_IRQ_ALL, 0);

		
		ams_i2c_clear_irq(AMS_IRQ_ALL);

		printk(KERN_INFO "ams: Unloading\n");

		ams_info.has_device = 0;
	}

	return 0;
}

static void ams_i2c_exit(void)
{
	i2c_del_driver(&ams_i2c_driver);
}

int __init ams_i2c_init(struct device_node *np)
{
	int result;

	
	ams_info.of_node = np;
	ams_info.exit = ams_i2c_exit;
	ams_info.get_vendor = ams_i2c_get_vendor;
	ams_info.get_xyz = ams_i2c_get_xyz;
	ams_info.clear_irq = ams_i2c_clear_irq;
	ams_info.bustype = BUS_I2C;

	result = i2c_add_driver(&ams_i2c_driver);

	return result;
}
