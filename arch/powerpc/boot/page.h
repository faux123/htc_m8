#ifndef _PPC_BOOT_PAGE_H
#define _PPC_BOOT_PAGE_H
/*
 * Copyright (C) 2001 PPC64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifdef __ASSEMBLY__
#define ASM_CONST(x) x
#else
#define __ASM_CONST(x) x##UL
#define ASM_CONST(x) __ASM_CONST(x)
#endif

#define PAGE_SHIFT	12
#define PAGE_SIZE	(ASM_CONST(1) << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#define _ALIGN_UP(addr,size)	(((addr)+((size)-1))&(~((size)-1)))
#define _ALIGN_DOWN(addr,size)	((addr)&(~((size)-1)))

#define _ALIGN(addr,size)     _ALIGN_UP(addr,size)

#define PAGE_ALIGN(addr)	_ALIGN(addr, PAGE_SIZE)

#endif				
