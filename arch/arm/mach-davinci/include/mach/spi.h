/*
 * Copyright 2009 Texas Instruments.
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ARCH_ARM_DAVINCI_SPI_H
#define __ARCH_ARM_DAVINCI_SPI_H

#include <mach/edma.h>

#define SPI_INTERN_CS	0xFF

enum {
	SPI_VERSION_1, 
	SPI_VERSION_2, 
};

struct davinci_spi_platform_data {
	u8			version;
	u8			num_chipselect;
	u8			intr_line;
	u8			*chip_sel;
	bool			cshold_bug;
	enum dma_event_q	dma_event_q;
};

struct davinci_spi_config {
	u8	wdelay;
	u8	odd_parity;
	u8	parity_enable;
#define SPI_IO_TYPE_INTR	0
#define SPI_IO_TYPE_POLL	1
#define SPI_IO_TYPE_DMA		2
	u8	io_type;
	u8	timer_disable;
	u8	c2tdelay;
	u8	t2cdelay;
	u8	t2edelay;
	u8	c2edelay;
};

#endif	
