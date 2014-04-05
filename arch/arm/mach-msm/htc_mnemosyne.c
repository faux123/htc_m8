/* arch/arm/mach-msm/mnemosyne.c
 *
 * Copyright (C) 2013 HTC Corporation.
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
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/of.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <mach/msm_iomap.h>
#include <mach/htc_mnemosyne.h>

#define MNEMOSYNE_MODULE_NAME	"mnemosyne"

struct mnemosyne_data *mnemosyne_phys;
EXPORT_SYMBOL(mnemosyne_phys);
struct mnemosyne_data *mnemosyne_base;
EXPORT_SYMBOL(mnemosyne_base);

static atomic_t mnemosync_is_init = ATOMIC_INIT(0);

struct mnemosyne_meta {
	char *name;
	int num;
};

#undef DECLARE_MNEMOSYNE_START
#undef DECLARE_MNEMOSYNE_END
#undef DECLARE_MNEMOSYNE
#undef DECLARE_MNEMOSYNE_ARRAY

#define DECLARE_MNEMOSYNE_START()	struct mnemosyne_meta mnemosyne_meta_data[] = {

#define DECLARE_MNEMOSYNE_END()	};

#define DECLARE_MNEMOSYNE_ARRAY(meta_name, meta_num)	{ \
		.name = #meta_name,			\
		.num = meta_num,			\
	},

#define DECLARE_MNEMOSYNE(meta_name)	DECLARE_MNEMOSYNE_ARRAY(meta_name, 1)

#include <mach/htc_mnemosyne_footprint.inc>

struct mnemosyne_data *mnemosyne_get_base(void)
{
	return mnemosyne_base;
}
EXPORT_SYMBOL(mnemosyne_get_base);

static int mnemosyne_setup(unsigned int phys, unsigned base)
{
	if (atomic_read(&mnemosync_is_init)) {
		WARN(1, "%s: init again with phys=0x%08x, bas=0x%08x!\n", MNEMOSYNE_MODULE_NAME, phys, base);
		return -EIO;
	}

	mnemosyne_phys = (struct mnemosyne_data *) phys;
	mnemosyne_base = (struct mnemosyne_data *) base;

	pr_info("%s: phys: 0x%p\n", MNEMOSYNE_MODULE_NAME, mnemosyne_phys);
	pr_info("%s: base: 0x%p\n", MNEMOSYNE_MODULE_NAME, mnemosyne_base);
	pr_info("%s: init success.\n", MNEMOSYNE_MODULE_NAME);

	atomic_inc(&mnemosync_is_init);

	return 0;
}

#define OF_PROP_PHYS_NAME	"htc,phys"
#define OF_PROP_BASE_NAME	"htc,base"
static int mnemosyne_parse_dt(struct device* dev)
{
	struct device_node *node= NULL;
	u32 phys, base;
	int ret;

	pr_info("%s: init from device tree.\n", MNEMOSYNE_MODULE_NAME);

	node = dev->of_node;
	if (node == NULL) {
		pr_err("%s, Can't find device_node", MNEMOSYNE_MODULE_NAME);
		ret = -ENODEV;
		goto PARSE_DT_ERR_OUT;
	}

	ret = of_property_read_u32(node, OF_PROP_PHYS_NAME, &phys);
	if (ret) {
		pr_err("%s: could not get %s for %s\n", MNEMOSYNE_MODULE_NAME, OF_PROP_PHYS_NAME, node->full_name);
		goto PARSE_DT_ERR_OUT;
	}

	ret = of_property_read_u32(node, OF_PROP_BASE_NAME, &base);
	if (ret) {
		pr_err("%s: could not get %s for %s\n", MNEMOSYNE_MODULE_NAME, OF_PROP_BASE_NAME, node->full_name);
		goto PARSE_DT_ERR_OUT;
	}

	mnemosyne_setup(phys, base);

PARSE_DT_ERR_OUT:
	return ret;
}

static int mnemosyne_parse_pdata(struct device* dev)
{
	int ret = 0;
	struct mnemosyne_platform_data *pdata = (struct mnemosyne_platform_data *)dev->platform_data;

	pr_info("%s: init from platform data.\n", MNEMOSYNE_MODULE_NAME);

	if (pdata == NULL) {
		pr_err("%s: No pdata\n", MNEMOSYNE_MODULE_NAME);
		ret = -ENODEV;
		goto PARSE_PDATA_ERR_OUT;
	}

	mnemosyne_setup(pdata->phys, pdata->base);

PARSE_PDATA_ERR_OUT:
	return ret;
}

#ifdef CONFIG_SYSFS
#define MNEMOSYNE_ATTR_RO(_name)	static struct kobj_attribute _name##_attr = __ATTR_RO(_name)
#define MNEMOSYNE_ATTR(_name)		static struct kobj_attribute _name##_attr = __ATTR(_name, 0644, _name##_show, _name##_store)

static ssize_t rawdata_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct mnemosyne_data *data = mnemosyne_base;
	MNEMOSYNE_ELEMENT_TYPE *rawdata = (MNEMOSYNE_ELEMENT_TYPE *)data;
	int size = 0;
	int i, j;

	
	if (atomic_read(&mnemosync_is_init) == 0) {
		pr_warn("%s: not init!\n", MNEMOSYNE_MODULE_NAME);
		return 0;
	}

	
	for (i=0; i<sizeof(mnemosyne_meta_data)/sizeof(struct mnemosyne_meta); i++) {
		for (j=0; j<mnemosyne_meta_data[i].num; j++) {
			size += sprintf(buf + size, "%s", mnemosyne_meta_data[i].name);
			if (mnemosyne_meta_data[i].num > 1)
				size += sprintf(buf + size, "[%d]", j);
			size += sprintf(buf + size, ": 0x%08x = %d\n", rawdata[j], rawdata[j]);
		}
		rawdata += mnemosyne_meta_data[i].num;
	}

	return size;
}

MNEMOSYNE_ATTR_RO(rawdata);

static ssize_t is_init_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int size = 0;
	size = sprintf(buf, "%d\n", atomic_read(&mnemosync_is_init));
	return size;
}

MNEMOSYNE_ATTR_RO(is_init);

static struct attribute *mnemosyne_attrs[] = {
	&rawdata_attr.attr,
	&is_init_attr.attr,
	NULL,
};

static struct attribute_group mnemosyne_attr_group = {
	.attrs = mnemosyne_attrs,
	.name = MNEMOSYNE_MODULE_NAME,
};

static int __devinit mnemosyne_sysfs_setup(void)
{
	int ret = 0;

	ret = sysfs_create_group(kernel_kobj, &mnemosyne_attr_group);
	if (ret) {
		pr_err("%s: register sysfs failed\n", MNEMOSYNE_MODULE_NAME);
	}

	return ret;
}
#endif

static int __devinit mnemosyne_probe(struct platform_device *pdev)
{
	if (pdev->dev.of_node) {
		if (mnemosyne_parse_dt(&pdev->dev)) {
			pr_err("%s: parse device tree fail.\n", MNEMOSYNE_MODULE_NAME);
			return -ENODEV;
		}
	} else {
		if (mnemosyne_parse_pdata(&pdev->dev)) {
			pr_err("%s: parse pdata fail.\n", MNEMOSYNE_MODULE_NAME);
			return -ENODEV;
		}
	}

	return 0;
}

static int __devexit mnemosyne_remove(struct platform_device *pdev)
{
	return 0;
}

static struct of_device_id mnemosyne_match[] = {
	{       .compatible = "htc,mnemosyne",},
	{},
};

static struct platform_driver mnemosyne_driver = {
	.probe          = mnemosyne_probe,
	.remove         = __devexit_p(mnemosyne_remove),
	.driver         = {
		.name = MNEMOSYNE_MODULE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = mnemosyne_match,
	},
};

static int __init mnemosyne_init(void)
{
#ifdef CONFIG_SYSFS
	mnemosyne_sysfs_setup();
#endif

	return platform_driver_register(&mnemosyne_driver);
}

int mnemosyne_early_init(unsigned int phys, unsigned base)
{
	pr_info("%s: init from early init.\n", MNEMOSYNE_MODULE_NAME);
	return mnemosyne_setup(phys, base);
}

arch_initcall(mnemosyne_init);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jimmy Chen <jimmy.cm_chen@htc.com>");
MODULE_DESCRIPTION("HTC Footprint driver");
MODULE_VERSION("1.1");
MODULE_ALIAS("platform:mnemosyne");

