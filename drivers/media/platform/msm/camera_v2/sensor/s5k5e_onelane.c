/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
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
#include "msm_sensor.h"
#define S5K5E_ONELANE_SENSOR_NAME "s5k5e_onelane"
DEFINE_MSM_MUTEX(s5k5e_onelane_mut);

static struct msm_sensor_ctrl_t s5k5e_onelane_s_ctrl;

static struct msm_sensor_power_setting s5k5e_onelane_power_setting[] = {

	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_RESET,
		.config_val = GPIO_OUT_LOW,
		.delay = 20,
	},
	{
		.seq_type = SENSOR_VREG_NCP6924,
		.seq_val = NCP6924_VANA,
		.config_val = 1,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_VREG_NCP6924,
		.seq_val = NCP6924_VIO,
		.config_val = 1,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_VREG_NCP6924,
		.seq_val = NCP6924_VDIG,
		.config_val = 1,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_CLK,
		.seq_val = SENSOR_CAM_MCLK,
		.config_val = 0,
		.delay = 5,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_RESET,
		.config_val = GPIO_OUT_HIGH,
		.delay = 80,
	},
	{
		.seq_type = SENSOR_I2C_MUX,
		.seq_val = 0,
		.config_val = 0,
		.delay = 0,
	},
	
};

static struct v4l2_subdev_info s5k5e_onelane_subdev_info[] = {
	{
		.code   = V4L2_MBUS_FMT_SBGGR10_1X10,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.fmt    = 1,
		.order    = 0,
	},
};

static const struct i2c_device_id s5k5e_onelane_i2c_id[] = {
	{S5K5E_ONELANE_SENSOR_NAME, (kernel_ulong_t)&s5k5e_onelane_s_ctrl},
	{ }
};

static int32_t msm_s5k5e_onelane_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	return msm_sensor_i2c_probe(client, id, &s5k5e_onelane_s_ctrl);
}

static struct i2c_driver s5k5e_onelane_i2c_driver = {
	.id_table = s5k5e_onelane_i2c_id,
	.probe  = msm_s5k5e_onelane_i2c_probe,
	.driver = {
		.name = S5K5E_ONELANE_SENSOR_NAME,
	},
};

static struct msm_camera_i2c_client s5k5e_onelane_sensor_i2c_client = {
	.addr_type = MSM_CAMERA_I2C_WORD_ADDR,
};

static const struct of_device_id s5k5e_onelane_dt_match[] = {
	{.compatible = "htc,s5k5e_onelane", .data = &s5k5e_onelane_s_ctrl},
	{}
};

MODULE_DEVICE_TABLE(of, s5k5e_onelane_dt_match);

static struct platform_driver s5k5e_onelane_platform_driver = {
	.driver = {
		.name = "htc,s5k5e_onelane",
		.owner = THIS_MODULE,
		.of_match_table = s5k5e_onelane_dt_match,
	},
};

static const char *s5k5e_onelaneVendor = "Samsung";
static const char *s5k5e_onelaneNAME = "s5k5e_onelane";
static const char *s5k5e_onelaneSize = "5.0M";

static ssize_t sensor_vendor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	sprintf(buf, "%s %s %s\n", s5k5e_onelaneVendor, s5k5e_onelaneNAME, s5k5e_onelaneSize);
	ret = strlen(buf) + 1;

	return ret;
}

static DEVICE_ATTR(sensor, 0444, sensor_vendor_show, NULL);

static struct kobject *android_s5k5e_onelane;

static int s5k5e_onelane_sysfs_init(void)
{
	int ret ;
	pr_info("s5k5e_onelane:kobject creat and add\n");
	android_s5k5e_onelane = kobject_create_and_add("android_camera2", NULL);
	if (android_s5k5e_onelane == NULL) {
		pr_info("s5k5e_onelane_sysfs_init: subsystem_register " \
		"failed\n");
		ret = -ENOMEM;
		return ret ;
	}
	pr_info("s5k5e_onelane:sysfs_create_file\n");
	ret = sysfs_create_file(android_s5k5e_onelane, &dev_attr_sensor.attr);
	if (ret) {
		pr_info("s5k5e_onelane_sysfs_init: sysfs_create_file " \
		"failed\n");
		kobject_del(android_s5k5e_onelane);
	}

	return 0 ;
}

static int32_t s5k5e_onelane_platform_probe(struct platform_device *pdev)
{
	int32_t rc = 0;
	const struct of_device_id *match;
	match = of_match_device(s5k5e_onelane_dt_match, &pdev->dev);
	rc = msm_sensor_platform_probe(pdev, match->data);
	return rc;
}

static int __init s5k5e_onelane_init_module(void)
{
	int32_t rc = 0;
	pr_info("%s:%d\n", __func__, __LINE__);
	pr_info("s5k5e_onelane_init_module");
	rc = platform_driver_probe(&s5k5e_onelane_platform_driver,
		s5k5e_onelane_platform_probe);
	if (!rc) {
		s5k5e_onelane_sysfs_init();
		return rc;
	}
	pr_err("%s:%d rc %d\n", __func__, __LINE__, rc);
	return i2c_add_driver(&s5k5e_onelane_i2c_driver);
}

static void __exit s5k5e_onelane_exit_module(void)
{
	pr_info("%s:%d\n", __func__, __LINE__);
	if (s5k5e_onelane_s_ctrl.pdev) {
		msm_sensor_free_sensor_data(&s5k5e_onelane_s_ctrl);
		platform_driver_unregister(&s5k5e_onelane_platform_driver);
	} else
		i2c_del_driver(&s5k5e_onelane_i2c_driver);
	return;
}

static struct msm_sensor_ctrl_t s5k5e_onelane_s_ctrl = {
	.sensor_i2c_client = &s5k5e_onelane_sensor_i2c_client,
	.power_setting_array.power_setting = s5k5e_onelane_power_setting,
	.power_setting_array.size = ARRAY_SIZE(s5k5e_onelane_power_setting),
	.msm_sensor_mutex = &s5k5e_onelane_mut,
	.sensor_v4l2_subdev_info = s5k5e_onelane_subdev_info,
	.sensor_v4l2_subdev_info_size = ARRAY_SIZE(s5k5e_onelane_subdev_info),
};

module_init(s5k5e_onelane_init_module);
module_exit(s5k5e_onelane_exit_module);
MODULE_DESCRIPTION("s5k5e_onelane");
MODULE_LICENSE("GPL v2");
