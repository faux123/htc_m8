#ifndef __ASM_SH_CPU_SH5_IRQ_H
#define __ASM_SH_CPU_SH5_IRQ_H

/*
 * include/asm-sh/cpu-sh5/irq.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */



#define IRQ_IRL0	0
#define IRQ_IRL1	1
#define IRQ_IRL2	2
#define IRQ_IRL3	3

#define IRQ_INTA	4
#define IRQ_INTB	5
#define IRQ_INTC	6
#define IRQ_INTD	7

#define IRQ_SERR	12
#define IRQ_ERR		13
#define IRQ_PWR3	14
#define IRQ_PWR2	15
#define IRQ_PWR1	16
#define IRQ_PWR0	17

#define IRQ_DMTE0	18
#define IRQ_DMTE1	19
#define IRQ_DMTE2	20
#define IRQ_DMTE3	21
#define IRQ_DAERR	22

#define IRQ_TUNI0	32
#define IRQ_TUNI1	33
#define IRQ_TUNI2	34
#define IRQ_TICPI2	35

#define IRQ_ATI		36
#define IRQ_PRI		37
#define IRQ_CUI		38

#define IRQ_ERI		39
#define IRQ_RXI		40
#define IRQ_BRI		41
#define IRQ_TXI		42

#define IRQ_ITI		63

#define NR_INTC_IRQS	64

#ifdef CONFIG_SH_CAYMAN
#define NR_EXT_IRQS     32
#define START_EXT_IRQS  64

#define IRQ_P2INTA      (START_EXT_IRQS + (3*8) + 0)
#define IRQ_P2INTB      (START_EXT_IRQS + (3*8) + 1)
#define IRQ_P2INTC      (START_EXT_IRQS + (3*8) + 2)
#define IRQ_P2INTD      (START_EXT_IRQS + (3*8) + 3)

#define I8042_KBD_IRQ	(START_EXT_IRQS + 2)
#define I8042_AUX_IRQ	(START_EXT_IRQS + 6)

#define IRQ_CFCARD	(START_EXT_IRQS + 7)
#define IRQ_PCMCIA	(0)

#else
#define NR_EXT_IRQS	0
#endif

#define TIMER_IRQ	IRQ_TUNI0
#define RTC_IRQ		IRQ_CUI

#define	NO_PRIORITY	0	
#define TIMER_PRIORITY	2
#define RTC_PRIORITY	TIMER_PRIORITY
#define SCIF_PRIORITY	3
#define INTD_PRIORITY	3
#define	IRL3_PRIORITY	4
#define INTC_PRIORITY	6
#define	IRL2_PRIORITY	7
#define INTB_PRIORITY	9
#define	IRL1_PRIORITY	10
#define INTA_PRIORITY	12
#define	IRL0_PRIORITY	13
#define TOP_PRIORITY	15

extern int intc_evt_to_irq[(0xE20/0x20)+1];
extern int platform_int_priority[NR_INTC_IRQS];

#endif 
