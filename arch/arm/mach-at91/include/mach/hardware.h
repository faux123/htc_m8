/*
 * arch/arm/mach-at91/include/mach/hardware.h
 *
 *  Copyright (C) 2003 SAN People
 *  Copyright (C) 2003 ATMEL
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <asm/sizes.h>

#define AT91_BASE_DBGU0	0xfffff200
#define AT91_BASE_DBGU1	0xffffee00

#if defined(CONFIG_ARCH_AT91RM9200)
#include <mach/at91rm9200.h>
#elif defined(CONFIG_ARCH_AT91SAM9260) || defined(CONFIG_ARCH_AT91SAM9G20)
#include <mach/at91sam9260.h>
#elif defined(CONFIG_ARCH_AT91SAM9261) || defined(CONFIG_ARCH_AT91SAM9G10)
#include <mach/at91sam9261.h>
#elif defined(CONFIG_ARCH_AT91SAM9263)
#include <mach/at91sam9263.h>
#elif defined(CONFIG_ARCH_AT91SAM9RL)
#include <mach/at91sam9rl.h>
#elif defined(CONFIG_ARCH_AT91SAM9G45)
#include <mach/at91sam9g45.h>
#elif defined(CONFIG_ARCH_AT91SAM9X5)
#include <mach/at91sam9x5.h>
#elif defined(CONFIG_ARCH_AT91X40)
#include <mach/at91x40.h>
#else
#error "Unsupported AT91 processor"
#endif

#if !defined(CONFIG_ARCH_AT91X40)
#define AT91_BASE_SYS	0xffffc000
#endif

#define AT91_AIC	0xfffff000
#define AT91_PMC	0xfffffc00

#define AT91_ID_FIQ		0	
#define AT91_ID_SYS		1	

#ifdef CONFIG_MMU
#define AT91_IO_PHYS_BASE	0xFFF78000
#define AT91_IO_VIRT_BASE	(0xFF000000 - AT91_IO_SIZE)
#else
#define AT91_IO_PHYS_BASE	AT91_BASE_SYS
#define AT91_IO_VIRT_BASE	AT91_IO_PHYS_BASE
#endif

#define AT91_IO_SIZE		(0xFFFFFFFF - AT91_IO_PHYS_BASE + 1)

 
#define AT91_IO_P2V(x)		((x) - AT91_IO_PHYS_BASE + AT91_IO_VIRT_BASE)

#define AT91_VA_BASE_SYS	AT91_IO_P2V(AT91_BASE_SYS)
#define AT91_VA_BASE_EMAC	AT91_IO_P2V(AT91RM9200_BASE_EMAC)

 
#define AT91_SRAM_MAX		SZ_1M
#define AT91_VIRT_BASE		(AT91_IO_VIRT_BASE - AT91_SRAM_MAX)

#define ATMEL_MAX_UART		7		

#define AT91_CHIPSELECT_0	0x10000000
#define AT91_CHIPSELECT_1	0x20000000
#define AT91_CHIPSELECT_2	0x30000000
#define AT91_CHIPSELECT_3	0x40000000
#define AT91_CHIPSELECT_4	0x50000000
#define AT91_CHIPSELECT_5	0x60000000
#define AT91_CHIPSELECT_6	0x70000000
#define AT91_CHIPSELECT_7	0x80000000

#define AT91_SLOW_CLOCK		32768		


#endif
