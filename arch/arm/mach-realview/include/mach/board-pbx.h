/*
 * arch/arm/mach-realview/include/mach/board-pbx.h
 *
 * Copyright (C) 2009 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef __ASM_ARCH_BOARD_PBX_H
#define __ASM_ARCH_BOARD_PBX_H

#include <mach/platform.h>

#define REALVIEW_PBX_UART0_BASE			0x10009000	
#define REALVIEW_PBX_UART1_BASE			0x1000A000	
#define REALVIEW_PBX_UART2_BASE			0x1000B000	
#define REALVIEW_PBX_UART3_BASE			0x1000C000	
#define REALVIEW_PBX_SSP_BASE			0x1000D000	
#define REALVIEW_PBX_WATCHDOG0_BASE		0x1000F000	
#define REALVIEW_PBX_WATCHDOG_BASE		0x10010000	
#define REALVIEW_PBX_TIMER0_1_BASE		0x10011000	
#define REALVIEW_PBX_TIMER2_3_BASE		0x10012000	
#define REALVIEW_PBX_GPIO0_BASE			0x10013000	
#define REALVIEW_PBX_RTC_BASE			0x10017000	
#define REALVIEW_PBX_TIMER4_5_BASE		0x10018000	
#define REALVIEW_PBX_TIMER6_7_BASE		0x10019000	
#define REALVIEW_PBX_SCTL_BASE			0x1001A000	
#define REALVIEW_PBX_CLCD_BASE			0x10020000	
#define REALVIEW_PBX_ONB_SRAM_BASE		0x10060000	
#define REALVIEW_PBX_DMC_BASE			0x100E0000	
#define REALVIEW_PBX_SMC_BASE			0x100E1000	
#define REALVIEW_PBX_CAN_BASE			0x100E2000	
#define REALVIEW_PBX_GIC_CPU_BASE		0x1E000000	
#define REALVIEW_PBX_FLASH0_BASE		0x40000000
#define REALVIEW_PBX_FLASH0_SIZE		SZ_64M
#define REALVIEW_PBX_FLASH1_BASE		0x44000000
#define REALVIEW_PBX_FLASH1_SIZE		SZ_64M
#define REALVIEW_PBX_ETH_BASE			0x4E000000	
#define REALVIEW_PBX_USB_BASE			0x4F000000	
#define REALVIEW_PBX_GIC_DIST_BASE		0x1E001000	
#define REALVIEW_PBX_LT_BASE			0xC0000000	
#define REALVIEW_PBX_SDRAM6_BASE		0x70000000	
#define REALVIEW_PBX_SDRAM7_BASE		0x80000000	

#define REALVIEW_PBX_TILE_SCU_BASE		0x1F000000      
#define REALVIEW_PBX_TILE_GIC_CPU_BASE		0x1F000100      
#define REALVIEW_PBX_TILE_TWD_BASE		0x1F000600
#define REALVIEW_PBX_TILE_TWD_PERCPU_BASE	0x1F000700
#define REALVIEW_PBX_TILE_TWD_SIZE		0x00000100
#define REALVIEW_PBX_TILE_GIC_DIST_BASE		0x1F001000      
#define REALVIEW_PBX_TILE_L220_BASE		0x1F002000      

#define REALVIEW_PBX_SYS_PLD_CTRL1		0x74

#define REALVIEW_PBX_PCI_BASE			0x90040000	
#define REALVIEW_PBX_PCI_IO_BASE		0x90050000	
#define REALVIEW_PBX_PCI_MEM_BASE		0xA0000000	

#define REALVIEW_PBX_PCI_BASE_SIZE		0x10000		
#define REALVIEW_PBX_PCI_IO_SIZE		0x1000		
#define REALVIEW_PBX_PCI_MEM_SIZE		0x20000000	

#define REALVIEW_PBX_PROC_MASK          0xFF000000
#define REALVIEW_PBX_PROC_ARM7TDMI      0x00000000
#define REALVIEW_PBX_PROC_ARM9          0x02000000
#define REALVIEW_PBX_PROC_ARM11         0x04000000
#define REALVIEW_PBX_PROC_ARM11MP       0x06000000
#define REALVIEW_PBX_PROC_A9MP          0x0C000000
#define REALVIEW_PBX_PROC_A8            0x0E000000

#define check_pbx_proc(proc_type)                                            \
	((readl(__io_address(REALVIEW_SYS_PROCID)) & REALVIEW_PBX_PROC_MASK) \
	== proc_type)

#ifdef CONFIG_MACH_REALVIEW_PBX
#define core_tile_pbx11mp()     check_pbx_proc(REALVIEW_PBX_PROC_ARM11MP)
#define core_tile_pbxa9mp()     check_pbx_proc(REALVIEW_PBX_PROC_A9MP)
#define core_tile_pbxa8()       check_pbx_proc(REALVIEW_PBX_PROC_A8)
#else
#define core_tile_pbx11mp()     0
#define core_tile_pbxa9mp()     0
#define core_tile_pbxa8()       0
#endif

#endif	
