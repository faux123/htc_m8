/*
 * OMAP SoC specific OPP wrapper function
 *
 * Copyright (C) 2009-2010 Texas Instruments Incorporated - http://www.ti.com/
 *	Nishanth Menon
 *	Kevin Hilman
 * Copyright (C) 2010 Nokia Corporation.
 *      Eduardo Valentin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/module.h>
#include <linux/opp.h>

#include <plat/omap_device.h>

#include "omap_opp_data.h"

static u8 __initdata omap_table_init;

int __init omap_init_opp_table(struct omap_opp_def *opp_def,
		u32 opp_def_size)
{
	int i, r;

	if (!opp_def || !opp_def_size) {
		pr_err("%s: invalid params!\n", __func__);
		return -EINVAL;
	}

	if (omap_table_init)
		return -EEXIST;
	omap_table_init = 1;

	
	for (i = 0; i < opp_def_size; i++) {
		struct omap_hwmod *oh;
		struct device *dev;

		if (!opp_def->hwmod_name) {
			pr_err("%s: NULL name of omap_hwmod, failing [%d].\n",
				__func__, i);
			return -EINVAL;
		}
		oh = omap_hwmod_lookup(opp_def->hwmod_name);
		if (!oh || !oh->od) {
			pr_debug("%s: no hwmod or odev for %s, [%d] "
				"cannot add OPPs.\n", __func__,
				opp_def->hwmod_name, i);
			continue;
		}
		dev = &oh->od->pdev->dev;

		r = opp_add(dev, opp_def->freq, opp_def->u_volt);
		if (r) {
			dev_err(dev, "%s: add OPP %ld failed for %s [%d] "
				"result=%d\n",
			       __func__, opp_def->freq,
			       opp_def->hwmod_name, i, r);
		} else {
			if (!opp_def->default_available)
				r = opp_disable(dev, opp_def->freq);
			if (r)
				dev_err(dev, "%s: disable %ld failed for %s "
					"[%d] result=%d\n",
					__func__, opp_def->freq,
					opp_def->hwmod_name, i, r);
		}
		opp_def++;
	}

	return 0;
}
