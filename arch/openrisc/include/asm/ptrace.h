/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __ASM_OPENRISC_PTRACE_H
#define __ASM_OPENRISC_PTRACE_H

#include <asm/spr_defs.h>

#ifndef __ASSEMBLY__
struct user_regs_struct {
	
	unsigned long gpr[32];
	unsigned long pc;
	unsigned long sr;
	unsigned long pad1;
	unsigned long pad2;
};
#endif

#ifdef __KERNEL__


#ifndef __ASSEMBLY__

struct pt_regs {
	union {
		struct {
			
			long  sr;	
			long  sp;	
		};
		struct {
			
			long offset[2];
			long gprs[30];
		};
		struct {
			
			long gpr[32];
		};
	};
	long  pc;
	long  orig_gpr11;	
	long dummy;		
	long dummy2;		
};

#define STACK_FRAME_OVERHEAD  128  

#define instruction_pointer(regs)	((regs)->pc)
#define user_mode(regs)			(((regs)->sr & SPR_SR_SM) == 0)
#define user_stack_pointer(regs)	((unsigned long)(regs)->sp)
#define profile_pc(regs)		instruction_pointer(regs)

static inline long regs_return_value(struct pt_regs *regs)
{
	return regs->gpr[11];
}

#endif 

#define PT_SR         0
#define PT_SP         4
#define PT_GPR2       8
#define PT_GPR3       12
#define PT_GPR4       16
#define PT_GPR5       20
#define PT_GPR6       24
#define PT_GPR7       28
#define PT_GPR8       32
#define PT_GPR9       36
#define PT_GPR10      40
#define PT_GPR11      44
#define PT_GPR12      48
#define PT_GPR13      52
#define PT_GPR14      56
#define PT_GPR15      60
#define PT_GPR16      64
#define PT_GPR17      68
#define PT_GPR18      72
#define PT_GPR19      76
#define PT_GPR20      80
#define PT_GPR21      84
#define PT_GPR22      88
#define PT_GPR23      92
#define PT_GPR24      96
#define PT_GPR25      100
#define PT_GPR26      104
#define PT_GPR27      108
#define PT_GPR28      112
#define PT_GPR29      116
#define PT_GPR30      120
#define PT_GPR31      124
#define PT_PC	      128
#define PT_ORIG_GPR11 132
#define PT_SYSCALLNO  136

#endif 

#endif 
