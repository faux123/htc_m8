/*
 *  arch/arm/mach-vt8500/include/mach/wm8505_irqs.h
 *
 *  Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#define IRQ_UHCI	0	
#define IRQ_EHCI	1	
#define IRQ_UDCDMA	2	
				
#define IRQ_PS2MOUSE	4	
#define IRQ_UDC		5	
#define IRQ_EXT0	6	
#define IRQ_EXT1	7	
#define IRQ_KEYPAD	8	
#define IRQ_DMA		9	
#define IRQ_ETHER	10	
				
				
#define IRQ_EXT2	13	
#define IRQ_EXT3	14	
#define IRQ_EXT4	15	
#define IRQ_APB		16	
#define IRQ_DMA0	17	
#define IRQ_I2C1	18	
#define IRQ_I2C0	19	
#define IRQ_SDMMC	20	
#define IRQ_SDMMC_DMA	21	
#define IRQ_PMC_WU	22	
#define IRQ_PS2KBD	23	
#define IRQ_SPI0	24	
#define IRQ_SPI1	25	
#define IRQ_SPI2	26	
#define IRQ_DMA1	27	
#define IRQ_NAND	28	
#define IRQ_NAND_DMA	29	
#define IRQ_UART5	30	
#define IRQ_UART4	31	
#define IRQ_UART0	32	
#define IRQ_UART1	33	
#define IRQ_DMA2	34	
#define IRQ_I2S		35	
#define IRQ_PMCOS0	36	
#define IRQ_PMCOS1	37	
#define IRQ_PMCOS2	38	
#define IRQ_PMCOS3	39	
#define IRQ_DMA3	40	
#define IRQ_DMA4	41	
#define IRQ_AC97	42	
				
#define IRQ_NOR		44	
#define IRQ_DMA5	45	
#define IRQ_DMA6	46	
#define IRQ_UART2	47	
#define IRQ_RTC		48	
#define IRQ_RTCSM	49	
#define IRQ_UART3	50	
#define IRQ_DMA7	51	
#define IRQ_EXT5	52	
#define IRQ_EXT6	53	
#define IRQ_EXT7	54	
#define IRQ_CIR		55	
#define IRQ_SIC0	56	
#define IRQ_SIC1	57	
#define IRQ_SIC2	58	
#define IRQ_SIC3	59	
#define IRQ_SIC4	60	
#define IRQ_SIC5	61	
#define IRQ_SIC6	62	
#define IRQ_SIC7	63	
				
#define IRQ_JPEGDEC	65	
#define IRQ_SAE		66	
				
#define IRQ_VPU		79	
#define IRQ_VPP		80	
#define IRQ_VID		81	
#define IRQ_SPU		82	
#define IRQ_PIP		83	
#define IRQ_GE		84	
#define IRQ_GOV		85	
#define IRQ_DVO		86	
				
#define IRQ_DMA8	92	
#define IRQ_DMA9	93	
#define IRQ_DMA10	94	
#define IRQ_DMA11	95	
#define IRQ_DMA12	96	
#define IRQ_DMA13	97	
#define IRQ_DMA14	98	
#define IRQ_DMA15	99	
				
#define IRQ_GOVW	111	
#define IRQ_GOVRSDSCD	112	
#define IRQ_GOVRSDMIF	113	
#define IRQ_GOVRHDSCD	114	
#define IRQ_GOVRHDMIF	115	

#define WM8505_NR_IRQS		116
