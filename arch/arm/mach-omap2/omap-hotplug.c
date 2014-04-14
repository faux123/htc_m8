/*
 * OMAP4 SMP cpu-hotplug support
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Author:
 *      Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * Platform file needed for the OMAP4 SMP. This file is based on arm
 * realview smp platform.
 * Copyright (c) 2002 ARM Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/smp.h>

#include <asm/cacheflush.h>

#include "common.h"

#include "powerdomain.h"

int platform_cpu_kill(unsigned int cpu)
{
	return 1;
}

void __ref platform_cpu_die(unsigned int cpu)
{
	unsigned int this_cpu;

	flush_cache_all();
	dsb();

	if (omap_modify_auxcoreboot0(0x0, 0x200) != 0x0)
		pr_err("Secure clear status failed\n");

	for (;;) {
		omap4_hotplug_cpu(cpu, PWRDM_POWER_OFF);
		this_cpu = smp_processor_id();
		if (omap_read_auxcoreboot0() == this_cpu) {
			break;
		}
		pr_debug("CPU%u: spurious wakeup call\n", cpu);
	}
}

int platform_cpu_disable(unsigned int cpu)
{
	return cpu == 0 ? -EPERM : 0;
}
