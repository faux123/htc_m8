/*
 *  On-Chip devices setup code for the AT91SAM9G45 family
 *
 *  Copyright (C) 2009 Atmel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/i2c-gpio.h>
#include <linux/atmel-mci.h>

#include <linux/fb.h>
#include <video/atmel_lcdc.h>

#include <mach/board.h>
#include <mach/at91sam9g45.h>
#include <mach/at91sam9g45_matrix.h>
#include <mach/at91_matrix.h>
#include <mach/at91sam9_smc.h>
#include <mach/at_hdmac.h>
#include <mach/atmel-mci.h>

#include <media/atmel-isi.h>

#include "generic.h"
#include "clock.h"



#if defined(CONFIG_AT_HDMAC) || defined(CONFIG_AT_HDMAC_MODULE)
static u64 hdmac_dmamask = DMA_BIT_MASK(32);

static struct resource hdmac_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_DMA,
		.end	= AT91SAM9G45_BASE_DMA + SZ_512 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_DMA,
		.end	= AT91SAM9G45_ID_DMA,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at_hdmac_device = {
	.name		= "at91sam9g45_dma",
	.id		= -1,
	.dev		= {
				.dma_mask		= &hdmac_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= hdmac_resources,
	.num_resources	= ARRAY_SIZE(hdmac_resources),
};

void __init at91_add_device_hdmac(void)
{
#if defined(CONFIG_OF)
	struct device_node *of_node =
		of_find_node_by_name(NULL, "dma-controller");

	if (of_node)
		of_node_put(of_node);
	else
#endif
		platform_device_register(&at_hdmac_device);
}
#else
void __init at91_add_device_hdmac(void) {}
#endif



#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static u64 ohci_dmamask = DMA_BIT_MASK(32);
static struct at91_usbh_data usbh_ohci_data;

static struct resource usbh_ohci_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_OHCI_BASE,
		.end	= AT91SAM9G45_OHCI_BASE + SZ_1M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_UHPHS,
		.end	= AT91SAM9G45_ID_UHPHS,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91_usbh_ohci_device = {
	.name		= "at91_ohci",
	.id		= -1,
	.dev		= {
				.dma_mask		= &ohci_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &usbh_ohci_data,
	},
	.resource	= usbh_ohci_resources,
	.num_resources	= ARRAY_SIZE(usbh_ohci_resources),
};

void __init at91_add_device_usbh_ohci(struct at91_usbh_data *data)
{
	int i;

	if (!data)
		return;

	
	for (i = 0; i < data->ports; i++) {
		if (gpio_is_valid(data->vbus_pin[i]))
			at91_set_gpio_output(data->vbus_pin[i],
					     data->vbus_pin_active_low[i]);
	}

	
	for (i = 0; i < data->ports; i++) {
		if (gpio_is_valid(data->overcurrent_pin[i]))
			at91_set_gpio_input(data->overcurrent_pin[i], 1);
	}

	usbh_ohci_data = *data;
	platform_device_register(&at91_usbh_ohci_device);
}
#else
void __init at91_add_device_usbh_ohci(struct at91_usbh_data *data) {}
#endif



#if defined(CONFIG_USB_EHCI_HCD) || defined(CONFIG_USB_EHCI_HCD_MODULE)
static u64 ehci_dmamask = DMA_BIT_MASK(32);
static struct at91_usbh_data usbh_ehci_data;

static struct resource usbh_ehci_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_EHCI_BASE,
		.end	= AT91SAM9G45_EHCI_BASE + SZ_1M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_UHPHS,
		.end	= AT91SAM9G45_ID_UHPHS,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91_usbh_ehci_device = {
	.name		= "atmel-ehci",
	.id		= -1,
	.dev		= {
				.dma_mask		= &ehci_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &usbh_ehci_data,
	},
	.resource	= usbh_ehci_resources,
	.num_resources	= ARRAY_SIZE(usbh_ehci_resources),
};

void __init at91_add_device_usbh_ehci(struct at91_usbh_data *data)
{
	int i;

	if (!data)
		return;

	
	for (i = 0; i < data->ports; i++) {
		if (gpio_is_valid(data->vbus_pin[i]))
			at91_set_gpio_output(data->vbus_pin[i],
					     data->vbus_pin_active_low[i]);
	}

	usbh_ehci_data = *data;
	platform_device_register(&at91_usbh_ehci_device);
}
#else
void __init at91_add_device_usbh_ehci(struct at91_usbh_data *data) {}
#endif



#if defined(CONFIG_USB_ATMEL_USBA) || defined(CONFIG_USB_ATMEL_USBA_MODULE)
static struct resource usba_udc_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_UDPHS_FIFO,
		.end	= AT91SAM9G45_UDPHS_FIFO + SZ_512K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_BASE_UDPHS,
		.end	= AT91SAM9G45_BASE_UDPHS + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= AT91SAM9G45_ID_UDPHS,
		.end	= AT91SAM9G45_ID_UDPHS,
		.flags	= IORESOURCE_IRQ,
	},
};

#define EP(nam, idx, maxpkt, maxbk, dma, isoc)			\
	[idx] = {						\
		.name		= nam,				\
		.index		= idx,				\
		.fifo_size	= maxpkt,			\
		.nr_banks	= maxbk,			\
		.can_dma	= dma,				\
		.can_isoc	= isoc,				\
	}

static struct usba_ep_data usba_udc_ep[] __initdata = {
	EP("ep0", 0, 64, 1, 0, 0),
	EP("ep1", 1, 1024, 2, 1, 1),
	EP("ep2", 2, 1024, 2, 1, 1),
	EP("ep3", 3, 1024, 3, 1, 0),
	EP("ep4", 4, 1024, 3, 1, 0),
	EP("ep5", 5, 1024, 3, 1, 1),
	EP("ep6", 6, 1024, 3, 1, 1),
};

#undef EP

static struct {
	struct usba_platform_data pdata;
	struct usba_ep_data ep[7];
} usba_udc_data;

static struct platform_device at91_usba_udc_device = {
	.name		= "atmel_usba_udc",
	.id		= -1,
	.dev		= {
				.platform_data	= &usba_udc_data.pdata,
	},
	.resource	= usba_udc_resources,
	.num_resources	= ARRAY_SIZE(usba_udc_resources),
};

void __init at91_add_device_usba(struct usba_platform_data *data)
{
	usba_udc_data.pdata.vbus_pin = -EINVAL;
	usba_udc_data.pdata.num_ep = ARRAY_SIZE(usba_udc_ep);
	memcpy(usba_udc_data.ep, usba_udc_ep, sizeof(usba_udc_ep));

	if (data && gpio_is_valid(data->vbus_pin)) {
		at91_set_gpio_input(data->vbus_pin, 0);
		at91_set_deglitch(data->vbus_pin, 1);
		usba_udc_data.pdata.vbus_pin = data->vbus_pin;
	}

	

	platform_device_register(&at91_usba_udc_device);
}
#else
void __init at91_add_device_usba(struct usba_platform_data *data) {}
#endif



#if defined(CONFIG_MACB) || defined(CONFIG_MACB_MODULE)
static u64 eth_dmamask = DMA_BIT_MASK(32);
static struct macb_platform_data eth_data;

static struct resource eth_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_EMAC,
		.end	= AT91SAM9G45_BASE_EMAC + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_EMAC,
		.end	= AT91SAM9G45_ID_EMAC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9g45_eth_device = {
	.name		= "macb",
	.id		= -1,
	.dev		= {
				.dma_mask		= &eth_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &eth_data,
	},
	.resource	= eth_resources,
	.num_resources	= ARRAY_SIZE(eth_resources),
};

void __init at91_add_device_eth(struct macb_platform_data *data)
{
	if (!data)
		return;

	if (gpio_is_valid(data->phy_irq_pin)) {
		at91_set_gpio_input(data->phy_irq_pin, 0);
		at91_set_deglitch(data->phy_irq_pin, 1);
	}

	
	at91_set_A_periph(AT91_PIN_PA17, 0);	
	at91_set_A_periph(AT91_PIN_PA15, 0);	
	at91_set_A_periph(AT91_PIN_PA12, 0);	
	at91_set_A_periph(AT91_PIN_PA13, 0);	
	at91_set_A_periph(AT91_PIN_PA16, 0);	
	at91_set_A_periph(AT91_PIN_PA14, 0);	
	at91_set_A_periph(AT91_PIN_PA10, 0);	
	at91_set_A_periph(AT91_PIN_PA11, 0);	
	at91_set_A_periph(AT91_PIN_PA19, 0);	
	at91_set_A_periph(AT91_PIN_PA18, 0);	

	if (!data->is_rmii) {
		at91_set_B_periph(AT91_PIN_PA29, 0);	
		at91_set_B_periph(AT91_PIN_PA30, 0);	
		at91_set_B_periph(AT91_PIN_PA8,  0);	
		at91_set_B_periph(AT91_PIN_PA9,  0);	
		at91_set_B_periph(AT91_PIN_PA28, 0);	
		at91_set_B_periph(AT91_PIN_PA6,  0);	
		at91_set_B_periph(AT91_PIN_PA7,  0);	
		at91_set_B_periph(AT91_PIN_PA27, 0);	
	}

	eth_data = *data;
	platform_device_register(&at91sam9g45_eth_device);
}
#else
void __init at91_add_device_eth(struct macb_platform_data *data) {}
#endif



#if defined(CONFIG_MMC_ATMELMCI) || defined(CONFIG_MMC_ATMELMCI_MODULE)
static u64 mmc_dmamask = DMA_BIT_MASK(32);
static struct mci_platform_data mmc0_data, mmc1_data;

static struct resource mmc0_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_MCI0,
		.end	= AT91SAM9G45_BASE_MCI0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_MCI0,
		.end	= AT91SAM9G45_ID_MCI0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9g45_mmc0_device = {
	.name		= "atmel_mci",
	.id		= 0,
	.dev		= {
				.dma_mask		= &mmc_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &mmc0_data,
	},
	.resource	= mmc0_resources,
	.num_resources	= ARRAY_SIZE(mmc0_resources),
};

static struct resource mmc1_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_MCI1,
		.end	= AT91SAM9G45_BASE_MCI1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_MCI1,
		.end	= AT91SAM9G45_ID_MCI1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9g45_mmc1_device = {
	.name		= "atmel_mci",
	.id		= 1,
	.dev		= {
				.dma_mask		= &mmc_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &mmc1_data,
	},
	.resource	= mmc1_resources,
	.num_resources	= ARRAY_SIZE(mmc1_resources),
};

void __init at91_add_device_mci(short mmc_id, struct mci_platform_data *data)
{

	if (!data)
		return;

	
	if (!data->slot[0].bus_width)
		return;

#if defined(CONFIG_AT_HDMAC) || defined(CONFIG_AT_HDMAC_MODULE)
	{
	struct at_dma_slave	*atslave;
	struct mci_dma_data	*alt_atslave;

	alt_atslave = kzalloc(sizeof(struct mci_dma_data), GFP_KERNEL);
	atslave = &alt_atslave->sdata;

	
	atslave->dma_dev = &at_hdmac_device.dev;
	atslave->cfg = ATC_FIFOCFG_HALFFIFO
			| ATC_SRC_H2SEL_HW | ATC_DST_H2SEL_HW;
	atslave->ctrla = ATC_SCSIZE_16 | ATC_DCSIZE_16;
	if (mmc_id == 0)	
		atslave->cfg |= ATC_SRC_PER(AT_DMA_ID_MCI0)
			      | ATC_DST_PER(AT_DMA_ID_MCI0);

	else			
		atslave->cfg |= ATC_SRC_PER(AT_DMA_ID_MCI1)
			      | ATC_DST_PER(AT_DMA_ID_MCI1);

	data->dma_slave = alt_atslave;
	}
#endif


	
	if (gpio_is_valid(data->slot[0].detect_pin)) {
		at91_set_gpio_input(data->slot[0].detect_pin, 1);
		at91_set_deglitch(data->slot[0].detect_pin, 1);
	}
	if (gpio_is_valid(data->slot[0].wp_pin))
		at91_set_gpio_input(data->slot[0].wp_pin, 1);

	if (mmc_id == 0) {		

		
		at91_set_A_periph(AT91_PIN_PA0, 0);

		
		at91_set_A_periph(AT91_PIN_PA1, 1);

		
		at91_set_A_periph(AT91_PIN_PA2, 1);
		if (data->slot[0].bus_width == 4) {
			at91_set_A_periph(AT91_PIN_PA3, 1);
			at91_set_A_periph(AT91_PIN_PA4, 1);
			at91_set_A_periph(AT91_PIN_PA5, 1);
			if (data->slot[0].bus_width == 8) {
				at91_set_A_periph(AT91_PIN_PA6, 1);
				at91_set_A_periph(AT91_PIN_PA7, 1);
				at91_set_A_periph(AT91_PIN_PA8, 1);
				at91_set_A_periph(AT91_PIN_PA9, 1);
			}
		}

		mmc0_data = *data;
		platform_device_register(&at91sam9g45_mmc0_device);

	} else {			

		
		at91_set_A_periph(AT91_PIN_PA31, 0);

		
		at91_set_A_periph(AT91_PIN_PA22, 1);

		
		at91_set_A_periph(AT91_PIN_PA23, 1);
		if (data->slot[0].bus_width == 4) {
			at91_set_A_periph(AT91_PIN_PA24, 1);
			at91_set_A_periph(AT91_PIN_PA25, 1);
			at91_set_A_periph(AT91_PIN_PA26, 1);
			if (data->slot[0].bus_width == 8) {
				at91_set_A_periph(AT91_PIN_PA27, 1);
				at91_set_A_periph(AT91_PIN_PA28, 1);
				at91_set_A_periph(AT91_PIN_PA29, 1);
				at91_set_A_periph(AT91_PIN_PA30, 1);
			}
		}

		mmc1_data = *data;
		platform_device_register(&at91sam9g45_mmc1_device);

	}
}
#else
void __init at91_add_device_mci(short mmc_id, struct mci_platform_data *data) {}
#endif



#if defined(CONFIG_MTD_NAND_ATMEL) || defined(CONFIG_MTD_NAND_ATMEL_MODULE)
static struct atmel_nand_data nand_data;

#define NAND_BASE	AT91_CHIPSELECT_3

static struct resource nand_resources[] = {
	[0] = {
		.start	= NAND_BASE,
		.end	= NAND_BASE + SZ_256M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_BASE_ECC,
		.end	= AT91SAM9G45_BASE_ECC + SZ_512 - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device at91sam9g45_nand_device = {
	.name		= "atmel_nand",
	.id		= -1,
	.dev		= {
				.platform_data	= &nand_data,
	},
	.resource	= nand_resources,
	.num_resources	= ARRAY_SIZE(nand_resources),
};

void __init at91_add_device_nand(struct atmel_nand_data *data)
{
	unsigned long csa;

	if (!data)
		return;

	csa = at91_matrix_read(AT91_MATRIX_EBICSA);
	at91_matrix_write(AT91_MATRIX_EBICSA, csa | AT91_MATRIX_EBI_CS3A_SMC_SMARTMEDIA);

	
	if (gpio_is_valid(data->enable_pin))
		at91_set_gpio_output(data->enable_pin, 1);

	
	if (gpio_is_valid(data->rdy_pin))
		at91_set_gpio_input(data->rdy_pin, 1);

	
	if (gpio_is_valid(data->det_pin))
		at91_set_gpio_input(data->det_pin, 1);

	nand_data = *data;
	platform_device_register(&at91sam9g45_nand_device);
}
#else
void __init at91_add_device_nand(struct atmel_nand_data *data) {}
#endif



#if defined(CONFIG_I2C_GPIO) || defined(CONFIG_I2C_GPIO_MODULE)
static struct i2c_gpio_platform_data pdata_i2c0 = {
	.sda_pin		= AT91_PIN_PA20,
	.sda_is_open_drain	= 1,
	.scl_pin		= AT91_PIN_PA21,
	.scl_is_open_drain	= 1,
	.udelay			= 5,		
};

static struct platform_device at91sam9g45_twi0_device = {
	.name			= "i2c-gpio",
	.id			= 0,
	.dev.platform_data	= &pdata_i2c0,
};

static struct i2c_gpio_platform_data pdata_i2c1 = {
	.sda_pin		= AT91_PIN_PB10,
	.sda_is_open_drain	= 1,
	.scl_pin		= AT91_PIN_PB11,
	.scl_is_open_drain	= 1,
	.udelay			= 5,		
};

static struct platform_device at91sam9g45_twi1_device = {
	.name			= "i2c-gpio",
	.id			= 1,
	.dev.platform_data	= &pdata_i2c1,
};

void __init at91_add_device_i2c(short i2c_id, struct i2c_board_info *devices, int nr_devices)
{
	i2c_register_board_info(i2c_id, devices, nr_devices);

	if (i2c_id == 0) {
		at91_set_GPIO_periph(AT91_PIN_PA20, 1);		
		at91_set_multi_drive(AT91_PIN_PA20, 1);

		at91_set_GPIO_periph(AT91_PIN_PA21, 1);		
		at91_set_multi_drive(AT91_PIN_PA21, 1);

		platform_device_register(&at91sam9g45_twi0_device);
	} else {
		at91_set_GPIO_periph(AT91_PIN_PB10, 1);		
		at91_set_multi_drive(AT91_PIN_PB10, 1);

		at91_set_GPIO_periph(AT91_PIN_PB11, 1);		
		at91_set_multi_drive(AT91_PIN_PB11, 1);

		platform_device_register(&at91sam9g45_twi1_device);
	}
}

#elif defined(CONFIG_I2C_AT91) || defined(CONFIG_I2C_AT91_MODULE)
static struct resource twi0_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_TWI0,
		.end	= AT91SAM9G45_BASE_TWI0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_TWI0,
		.end	= AT91SAM9G45_ID_TWI0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9g45_twi0_device = {
	.name		= "at91_i2c",
	.id		= 0,
	.resource	= twi0_resources,
	.num_resources	= ARRAY_SIZE(twi0_resources),
};

static struct resource twi1_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_TWI1,
		.end	= AT91SAM9G45_BASE_TWI1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_TWI1,
		.end	= AT91SAM9G45_ID_TWI1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9g45_twi1_device = {
	.name		= "at91_i2c",
	.id		= 1,
	.resource	= twi1_resources,
	.num_resources	= ARRAY_SIZE(twi1_resources),
};

void __init at91_add_device_i2c(short i2c_id, struct i2c_board_info *devices, int nr_devices)
{
	i2c_register_board_info(i2c_id, devices, nr_devices);

	
	if (i2c_id == 0) {
		at91_set_A_periph(AT91_PIN_PA20, 0);		
		at91_set_multi_drive(AT91_PIN_PA20, 1);

		at91_set_A_periph(AT91_PIN_PA21, 0);		
		at91_set_multi_drive(AT91_PIN_PA21, 1);

		platform_device_register(&at91sam9g45_twi0_device);
	} else {
		at91_set_A_periph(AT91_PIN_PB10, 0);		
		at91_set_multi_drive(AT91_PIN_PB10, 1);

		at91_set_A_periph(AT91_PIN_PB11, 0);		
		at91_set_multi_drive(AT91_PIN_PB11, 1);

		platform_device_register(&at91sam9g45_twi1_device);
	}
}
#else
void __init at91_add_device_i2c(short i2c_id, struct i2c_board_info *devices, int nr_devices) {}
#endif



#if defined(CONFIG_SPI_ATMEL) || defined(CONFIG_SPI_ATMEL_MODULE)
static u64 spi_dmamask = DMA_BIT_MASK(32);

static struct resource spi0_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_SPI0,
		.end	= AT91SAM9G45_BASE_SPI0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_SPI0,
		.end	= AT91SAM9G45_ID_SPI0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9g45_spi0_device = {
	.name		= "atmel_spi",
	.id		= 0,
	.dev		= {
				.dma_mask		= &spi_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= spi0_resources,
	.num_resources	= ARRAY_SIZE(spi0_resources),
};

static const unsigned spi0_standard_cs[4] = { AT91_PIN_PB3, AT91_PIN_PB18, AT91_PIN_PB19, AT91_PIN_PD27 };

static struct resource spi1_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_SPI1,
		.end	= AT91SAM9G45_BASE_SPI1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_SPI1,
		.end	= AT91SAM9G45_ID_SPI1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9g45_spi1_device = {
	.name		= "atmel_spi",
	.id		= 1,
	.dev		= {
				.dma_mask		= &spi_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= spi1_resources,
	.num_resources	= ARRAY_SIZE(spi1_resources),
};

static const unsigned spi1_standard_cs[4] = { AT91_PIN_PB17, AT91_PIN_PD28, AT91_PIN_PD18, AT91_PIN_PD19 };

void __init at91_add_device_spi(struct spi_board_info *devices, int nr_devices)
{
	int i;
	unsigned long cs_pin;
	short enable_spi0 = 0;
	short enable_spi1 = 0;

	
	for (i = 0; i < nr_devices; i++) {
		if (devices[i].controller_data)
			cs_pin = (unsigned long) devices[i].controller_data;
		else if (devices[i].bus_num == 0)
			cs_pin = spi0_standard_cs[devices[i].chip_select];
		else
			cs_pin = spi1_standard_cs[devices[i].chip_select];

		if (!gpio_is_valid(cs_pin))
			continue;

		if (devices[i].bus_num == 0)
			enable_spi0 = 1;
		else
			enable_spi1 = 1;

		
		at91_set_gpio_output(cs_pin, 1);

		
		devices[i].controller_data = (void *) cs_pin;
	}

	spi_register_board_info(devices, nr_devices);

	
	if (enable_spi0) {
		at91_set_A_periph(AT91_PIN_PB0, 0);	
		at91_set_A_periph(AT91_PIN_PB1, 0);	
		at91_set_A_periph(AT91_PIN_PB2, 0);	

		platform_device_register(&at91sam9g45_spi0_device);
	}
	if (enable_spi1) {
		at91_set_A_periph(AT91_PIN_PB14, 0);	
		at91_set_A_periph(AT91_PIN_PB15, 0);	
		at91_set_A_periph(AT91_PIN_PB16, 0);	

		platform_device_register(&at91sam9g45_spi1_device);
	}
}
#else
void __init at91_add_device_spi(struct spi_board_info *devices, int nr_devices) {}
#endif



#if defined(CONFIG_SND_ATMEL_AC97C) || defined(CONFIG_SND_ATMEL_AC97C_MODULE)
static u64 ac97_dmamask = DMA_BIT_MASK(32);
static struct ac97c_platform_data ac97_data;

static struct resource ac97_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_AC97C,
		.end	= AT91SAM9G45_BASE_AC97C + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_AC97C,
		.end	= AT91SAM9G45_ID_AC97C,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9g45_ac97_device = {
	.name		= "atmel_ac97c",
	.id		= 0,
	.dev		= {
				.dma_mask		= &ac97_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &ac97_data,
	},
	.resource	= ac97_resources,
	.num_resources	= ARRAY_SIZE(ac97_resources),
};

void __init at91_add_device_ac97(struct ac97c_platform_data *data)
{
	if (!data)
		return;

	at91_set_A_periph(AT91_PIN_PD8, 0);	
	at91_set_A_periph(AT91_PIN_PD9, 0);	
	at91_set_A_periph(AT91_PIN_PD7, 0);	
	at91_set_A_periph(AT91_PIN_PD6, 0);	

	
	if (gpio_is_valid(data->reset_pin))
		at91_set_gpio_output(data->reset_pin, 0);

	ac97_data = *data;
	platform_device_register(&at91sam9g45_ac97_device);
}
#else
void __init at91_add_device_ac97(struct ac97c_platform_data *data) {}
#endif

#if defined(CONFIG_VIDEO_ATMEL_ISI) || defined(CONFIG_VIDEO_ATMEL_ISI_MODULE)
static u64 isi_dmamask = DMA_BIT_MASK(32);
static struct isi_platform_data isi_data;

struct resource isi_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_ISI,
		.end	= AT91SAM9G45_BASE_ISI + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_ISI,
		.end	= AT91SAM9G45_ID_ISI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9g45_isi_device = {
	.name		= "atmel_isi",
	.id		= 0,
	.dev		= {
			.dma_mask		= &isi_dmamask,
			.coherent_dma_mask	= DMA_BIT_MASK(32),
			.platform_data		= &isi_data,
	},
	.resource	= isi_resources,
	.num_resources	= ARRAY_SIZE(isi_resources),
};

static struct clk_lookup isi_mck_lookups[] = {
	CLKDEV_CON_DEV_ID("isi_mck", "atmel_isi.0", NULL),
};

void __init at91_add_device_isi(struct isi_platform_data *data,
		bool use_pck_as_mck)
{
	struct clk *pck;
	struct clk *parent;

	if (!data)
		return;
	isi_data = *data;

	at91_set_A_periph(AT91_PIN_PB20, 0);	
	at91_set_A_periph(AT91_PIN_PB21, 0);	
	at91_set_A_periph(AT91_PIN_PB22, 0);	
	at91_set_A_periph(AT91_PIN_PB23, 0);	
	at91_set_A_periph(AT91_PIN_PB24, 0);	
	at91_set_A_periph(AT91_PIN_PB25, 0);	
	at91_set_A_periph(AT91_PIN_PB26, 0);	
	at91_set_A_periph(AT91_PIN_PB27, 0);	
	at91_set_A_periph(AT91_PIN_PB28, 0);	
	at91_set_A_periph(AT91_PIN_PB30, 0);	
	at91_set_A_periph(AT91_PIN_PB29, 0);	
	at91_set_B_periph(AT91_PIN_PB8, 0);	
	at91_set_B_periph(AT91_PIN_PB9, 0);	
	at91_set_B_periph(AT91_PIN_PB10, 0);	
	at91_set_B_periph(AT91_PIN_PB11, 0);	

	platform_device_register(&at91sam9g45_isi_device);

	if (use_pck_as_mck) {
		at91_set_B_periph(AT91_PIN_PB31, 0);	

		pck = clk_get(NULL, "pck1");
		parent = clk_get(NULL, "plla");

		BUG_ON(IS_ERR(pck) || IS_ERR(parent));

		if (clk_set_parent(pck, parent)) {
			pr_err("Failed to set PCK's parent\n");
		} else {
			
			isi_mck_lookups[0].clk = pck;
			clkdev_add_table(isi_mck_lookups,
					ARRAY_SIZE(isi_mck_lookups));
		}

		clk_put(pck);
		clk_put(parent);
	}
}
#else
void __init at91_add_device_isi(struct isi_platform_data *data,
		bool use_pck_as_mck) {}
#endif



#if defined(CONFIG_FB_ATMEL) || defined(CONFIG_FB_ATMEL_MODULE)
static u64 lcdc_dmamask = DMA_BIT_MASK(32);
static struct atmel_lcdfb_info lcdc_data;

static struct resource lcdc_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_LCDC_BASE,
		.end	= AT91SAM9G45_LCDC_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_LCDC,
		.end	= AT91SAM9G45_ID_LCDC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91_lcdc_device = {
	.name		= "atmel_lcdfb",
	.id		= 0,
	.dev		= {
				.dma_mask		= &lcdc_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &lcdc_data,
	},
	.resource	= lcdc_resources,
	.num_resources	= ARRAY_SIZE(lcdc_resources),
};

void __init at91_add_device_lcdc(struct atmel_lcdfb_info *data)
{
	if (!data)
		return;

	at91_set_A_periph(AT91_PIN_PE0, 0);	

	at91_set_A_periph(AT91_PIN_PE2, 0);	
	at91_set_A_periph(AT91_PIN_PE3, 0);	
	at91_set_A_periph(AT91_PIN_PE4, 0);	
	at91_set_A_periph(AT91_PIN_PE5, 0);	
	at91_set_A_periph(AT91_PIN_PE6, 0);	
	at91_set_A_periph(AT91_PIN_PE7, 0);	
	at91_set_A_periph(AT91_PIN_PE8, 0);	
	at91_set_A_periph(AT91_PIN_PE9, 0);	
	at91_set_A_periph(AT91_PIN_PE10, 0);	
	at91_set_A_periph(AT91_PIN_PE11, 0);	
	at91_set_A_periph(AT91_PIN_PE12, 0);	
	at91_set_A_periph(AT91_PIN_PE13, 0);	
	at91_set_A_periph(AT91_PIN_PE14, 0);	
	at91_set_A_periph(AT91_PIN_PE15, 0);	
	at91_set_A_periph(AT91_PIN_PE16, 0);	
	at91_set_A_periph(AT91_PIN_PE17, 0);	
	at91_set_A_periph(AT91_PIN_PE18, 0);	
	at91_set_A_periph(AT91_PIN_PE19, 0);	
	at91_set_A_periph(AT91_PIN_PE20, 0);	
	at91_set_A_periph(AT91_PIN_PE21, 0);	
	at91_set_A_periph(AT91_PIN_PE22, 0);	
	at91_set_A_periph(AT91_PIN_PE23, 0);	
	at91_set_A_periph(AT91_PIN_PE24, 0);	
	at91_set_A_periph(AT91_PIN_PE25, 0);	
	at91_set_A_periph(AT91_PIN_PE26, 0);	
	at91_set_A_periph(AT91_PIN_PE27, 0);	
	at91_set_A_periph(AT91_PIN_PE28, 0);	
	at91_set_A_periph(AT91_PIN_PE29, 0);	
	at91_set_A_periph(AT91_PIN_PE30, 0);	

	lcdc_data = *data;
	platform_device_register(&at91_lcdc_device);
}
#else
void __init at91_add_device_lcdc(struct atmel_lcdfb_info *data) {}
#endif



#ifdef CONFIG_ATMEL_TCLIB
static struct resource tcb0_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_TCB0,
		.end	= AT91SAM9G45_BASE_TCB0 + SZ_256 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_TCB,
		.end	= AT91SAM9G45_ID_TCB,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9g45_tcb0_device = {
	.name		= "atmel_tcb",
	.id		= 0,
	.resource	= tcb0_resources,
	.num_resources	= ARRAY_SIZE(tcb0_resources),
};

static struct resource tcb1_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_TCB1,
		.end	= AT91SAM9G45_BASE_TCB1 + SZ_256 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_TCB,
		.end	= AT91SAM9G45_ID_TCB,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9g45_tcb1_device = {
	.name		= "atmel_tcb",
	.id		= 1,
	.resource	= tcb1_resources,
	.num_resources	= ARRAY_SIZE(tcb1_resources),
};

#if defined(CONFIG_OF)
static struct of_device_id tcb_ids[] = {
	{ .compatible = "atmel,at91rm9200-tcb" },
	{  }
};
#endif

static void __init at91_add_device_tc(void)
{
#if defined(CONFIG_OF)
	struct device_node *np;

	np = of_find_matching_node(NULL, tcb_ids);
	if (np) {
		of_node_put(np);
		return;
	}
#endif

	platform_device_register(&at91sam9g45_tcb0_device);
	platform_device_register(&at91sam9g45_tcb1_device);
}
#else
static void __init at91_add_device_tc(void) { }
#endif



#if defined(CONFIG_RTC_DRV_AT91RM9200) || defined(CONFIG_RTC_DRV_AT91RM9200_MODULE)
static struct resource rtc_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_RTC,
		.end	= AT91SAM9G45_BASE_RTC + SZ_256 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91_ID_SYS,
		.end	= AT91_ID_SYS,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9g45_rtc_device = {
	.name		= "at91_rtc",
	.id		= -1,
	.resource	= rtc_resources,
	.num_resources	= ARRAY_SIZE(rtc_resources),
};

static void __init at91_add_device_rtc(void)
{
	platform_device_register(&at91sam9g45_rtc_device);
}
#else
static void __init at91_add_device_rtc(void) {}
#endif



#if defined(CONFIG_TOUCHSCREEN_ATMEL_TSADCC) || defined(CONFIG_TOUCHSCREEN_ATMEL_TSADCC_MODULE)
static u64 tsadcc_dmamask = DMA_BIT_MASK(32);
static struct at91_tsadcc_data tsadcc_data;

static struct resource tsadcc_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_TSC,
		.end	= AT91SAM9G45_BASE_TSC + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_TSC,
		.end	= AT91SAM9G45_ID_TSC,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device at91sam9g45_tsadcc_device = {
	.name		= "atmel_tsadcc",
	.id		= -1,
	.dev		= {
				.dma_mask		= &tsadcc_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &tsadcc_data,
	},
	.resource	= tsadcc_resources,
	.num_resources	= ARRAY_SIZE(tsadcc_resources),
};

void __init at91_add_device_tsadcc(struct at91_tsadcc_data *data)
{
	if (!data)
		return;

	at91_set_gpio_input(AT91_PIN_PD20, 0);	
	at91_set_gpio_input(AT91_PIN_PD21, 0);	
	at91_set_gpio_input(AT91_PIN_PD22, 0);	
	at91_set_gpio_input(AT91_PIN_PD23, 0);	

	tsadcc_data = *data;
	platform_device_register(&at91sam9g45_tsadcc_device);
}
#else
void __init at91_add_device_tsadcc(struct at91_tsadcc_data *data) {}
#endif



static struct resource rtt_resources[] = {
	{
		.start	= AT91SAM9G45_BASE_RTT,
		.end	= AT91SAM9G45_BASE_RTT + SZ_16 - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device at91sam9g45_rtt_device = {
	.name		= "at91_rtt",
	.id		= 0,
	.resource	= rtt_resources,
};

#if IS_ENABLED(CONFIG_RTC_DRV_AT91SAM9)
static void __init at91_add_device_rtt_rtc(void)
{
	at91sam9g45_rtt_device.name = "rtc-at91sam9";
	at91sam9g45_rtt_device.num_resources = 2;
	rtt_resources[1].start = AT91SAM9G45_BASE_GPBR +
				 4 * CONFIG_RTC_DRV_AT91SAM9_GPBR;
	rtt_resources[1].end = rtt_resources[1].start + 3;
}
#else
static void __init at91_add_device_rtt_rtc(void)
{
	
	at91sam9g45_rtt_device.num_resources = 1;
}
#endif

static void __init at91_add_device_rtt(void)
{
	at91_add_device_rtt_rtc();
	platform_device_register(&at91sam9g45_rtt_device);
}



#if defined(CONFIG_HW_RANDOM_ATMEL) || defined(CONFIG_HW_RANDOM_ATMEL_MODULE)
static struct resource trng_resources[] = {
	{
		.start	= AT91SAM9G45_BASE_TRNG,
		.end	= AT91SAM9G45_BASE_TRNG + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device at91sam9g45_trng_device = {
	.name		= "atmel-trng",
	.id		= -1,
	.resource	= trng_resources,
	.num_resources	= ARRAY_SIZE(trng_resources),
};

static void __init at91_add_device_trng(void)
{
	platform_device_register(&at91sam9g45_trng_device);
}
#else
static void __init at91_add_device_trng(void) {}
#endif


#if defined(CONFIG_AT91SAM9X_WATCHDOG) || defined(CONFIG_AT91SAM9X_WATCHDOG_MODULE)
static struct resource wdt_resources[] = {
	{
		.start	= AT91SAM9G45_BASE_WDT,
		.end	= AT91SAM9G45_BASE_WDT + SZ_16 - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device at91sam9g45_wdt_device = {
	.name		= "at91_wdt",
	.id		= -1,
	.resource	= wdt_resources,
	.num_resources	= ARRAY_SIZE(wdt_resources),
};

static void __init at91_add_device_watchdog(void)
{
	platform_device_register(&at91sam9g45_wdt_device);
}
#else
static void __init at91_add_device_watchdog(void) {}
#endif



#if defined(CONFIG_ATMEL_PWM) || defined(CONFIG_ATMEL_PWM_MODULE)
static u32 pwm_mask;

static struct resource pwm_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_PWMC,
		.end	= AT91SAM9G45_BASE_PWMC + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_PWMC,
		.end	= AT91SAM9G45_ID_PWMC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9g45_pwm0_device = {
	.name	= "atmel_pwm",
	.id	= -1,
	.dev	= {
		.platform_data		= &pwm_mask,
	},
	.resource	= pwm_resources,
	.num_resources	= ARRAY_SIZE(pwm_resources),
};

void __init at91_add_device_pwm(u32 mask)
{
	if (mask & (1 << AT91_PWM0))
		at91_set_B_periph(AT91_PIN_PD24, 1);	

	if (mask & (1 << AT91_PWM1))
		at91_set_B_periph(AT91_PIN_PD31, 1);	

	if (mask & (1 << AT91_PWM2))
		at91_set_B_periph(AT91_PIN_PD26, 1);	

	if (mask & (1 << AT91_PWM3))
		at91_set_B_periph(AT91_PIN_PD0, 1);	

	pwm_mask = mask;

	platform_device_register(&at91sam9g45_pwm0_device);
}
#else
void __init at91_add_device_pwm(u32 mask) {}
#endif



#if defined(CONFIG_ATMEL_SSC) || defined(CONFIG_ATMEL_SSC_MODULE)
static u64 ssc0_dmamask = DMA_BIT_MASK(32);

static struct resource ssc0_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_SSC0,
		.end	= AT91SAM9G45_BASE_SSC0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_SSC0,
		.end	= AT91SAM9G45_ID_SSC0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9g45_ssc0_device = {
	.name	= "ssc",
	.id	= 0,
	.dev	= {
		.dma_mask		= &ssc0_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= ssc0_resources,
	.num_resources	= ARRAY_SIZE(ssc0_resources),
};

static inline void configure_ssc0_pins(unsigned pins)
{
	if (pins & ATMEL_SSC_TF)
		at91_set_A_periph(AT91_PIN_PD1, 1);
	if (pins & ATMEL_SSC_TK)
		at91_set_A_periph(AT91_PIN_PD0, 1);
	if (pins & ATMEL_SSC_TD)
		at91_set_A_periph(AT91_PIN_PD2, 1);
	if (pins & ATMEL_SSC_RD)
		at91_set_A_periph(AT91_PIN_PD3, 1);
	if (pins & ATMEL_SSC_RK)
		at91_set_A_periph(AT91_PIN_PD4, 1);
	if (pins & ATMEL_SSC_RF)
		at91_set_A_periph(AT91_PIN_PD5, 1);
}

static u64 ssc1_dmamask = DMA_BIT_MASK(32);

static struct resource ssc1_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_SSC1,
		.end	= AT91SAM9G45_BASE_SSC1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_SSC1,
		.end	= AT91SAM9G45_ID_SSC1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9g45_ssc1_device = {
	.name	= "ssc",
	.id	= 1,
	.dev	= {
		.dma_mask		= &ssc1_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= ssc1_resources,
	.num_resources	= ARRAY_SIZE(ssc1_resources),
};

static inline void configure_ssc1_pins(unsigned pins)
{
	if (pins & ATMEL_SSC_TF)
		at91_set_A_periph(AT91_PIN_PD14, 1);
	if (pins & ATMEL_SSC_TK)
		at91_set_A_periph(AT91_PIN_PD12, 1);
	if (pins & ATMEL_SSC_TD)
		at91_set_A_periph(AT91_PIN_PD10, 1);
	if (pins & ATMEL_SSC_RD)
		at91_set_A_periph(AT91_PIN_PD11, 1);
	if (pins & ATMEL_SSC_RK)
		at91_set_A_periph(AT91_PIN_PD13, 1);
	if (pins & ATMEL_SSC_RF)
		at91_set_A_periph(AT91_PIN_PD15, 1);
}

void __init at91_add_device_ssc(unsigned id, unsigned pins)
{
	struct platform_device *pdev;

	switch (id) {
	case AT91SAM9G45_ID_SSC0:
		pdev = &at91sam9g45_ssc0_device;
		configure_ssc0_pins(pins);
		break;
	case AT91SAM9G45_ID_SSC1:
		pdev = &at91sam9g45_ssc1_device;
		configure_ssc1_pins(pins);
		break;
	default:
		return;
	}

	platform_device_register(pdev);
}

#else
void __init at91_add_device_ssc(unsigned id, unsigned pins) {}
#endif



#if defined(CONFIG_SERIAL_ATMEL)
static struct resource dbgu_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_DBGU,
		.end	= AT91SAM9G45_BASE_DBGU + SZ_512 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91_ID_SYS,
		.end	= AT91_ID_SYS,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data dbgu_data = {
	.use_dma_tx	= 0,
	.use_dma_rx	= 0,
};

static u64 dbgu_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9g45_dbgu_device = {
	.name		= "atmel_usart",
	.id		= 0,
	.dev		= {
				.dma_mask		= &dbgu_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &dbgu_data,
	},
	.resource	= dbgu_resources,
	.num_resources	= ARRAY_SIZE(dbgu_resources),
};

static inline void configure_dbgu_pins(void)
{
	at91_set_A_periph(AT91_PIN_PB12, 0);		
	at91_set_A_periph(AT91_PIN_PB13, 1);		
}

static struct resource uart0_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_US0,
		.end	= AT91SAM9G45_BASE_US0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_US0,
		.end	= AT91SAM9G45_ID_US0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart0_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart0_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9g45_uart0_device = {
	.name		= "atmel_usart",
	.id		= 1,
	.dev		= {
				.dma_mask		= &uart0_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &uart0_data,
	},
	.resource	= uart0_resources,
	.num_resources	= ARRAY_SIZE(uart0_resources),
};

static inline void configure_usart0_pins(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PB19, 1);		
	at91_set_A_periph(AT91_PIN_PB18, 0);		

	if (pins & ATMEL_UART_RTS)
		at91_set_B_periph(AT91_PIN_PB17, 0);	
	if (pins & ATMEL_UART_CTS)
		at91_set_B_periph(AT91_PIN_PB15, 0);	
}

static struct resource uart1_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_US1,
		.end	= AT91SAM9G45_BASE_US1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_US1,
		.end	= AT91SAM9G45_ID_US1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart1_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart1_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9g45_uart1_device = {
	.name		= "atmel_usart",
	.id		= 2,
	.dev		= {
				.dma_mask		= &uart1_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &uart1_data,
	},
	.resource	= uart1_resources,
	.num_resources	= ARRAY_SIZE(uart1_resources),
};

static inline void configure_usart1_pins(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PB4, 1);		
	at91_set_A_periph(AT91_PIN_PB5, 0);		

	if (pins & ATMEL_UART_RTS)
		at91_set_A_periph(AT91_PIN_PD16, 0);	
	if (pins & ATMEL_UART_CTS)
		at91_set_A_periph(AT91_PIN_PD17, 0);	
}

static struct resource uart2_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_US2,
		.end	= AT91SAM9G45_BASE_US2 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_US2,
		.end	= AT91SAM9G45_ID_US2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart2_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart2_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9g45_uart2_device = {
	.name		= "atmel_usart",
	.id		= 3,
	.dev		= {
				.dma_mask		= &uart2_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &uart2_data,
	},
	.resource	= uart2_resources,
	.num_resources	= ARRAY_SIZE(uart2_resources),
};

static inline void configure_usart2_pins(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PB6, 1);		
	at91_set_A_periph(AT91_PIN_PB7, 0);		

	if (pins & ATMEL_UART_RTS)
		at91_set_B_periph(AT91_PIN_PC9, 0);	
	if (pins & ATMEL_UART_CTS)
		at91_set_B_periph(AT91_PIN_PC11, 0);	
}

static struct resource uart3_resources[] = {
	[0] = {
		.start	= AT91SAM9G45_BASE_US3,
		.end	= AT91SAM9G45_BASE_US3 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9G45_ID_US3,
		.end	= AT91SAM9G45_ID_US3,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart3_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart3_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9g45_uart3_device = {
	.name		= "atmel_usart",
	.id		= 4,
	.dev		= {
				.dma_mask		= &uart3_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &uart3_data,
	},
	.resource	= uart3_resources,
	.num_resources	= ARRAY_SIZE(uart3_resources),
};

static inline void configure_usart3_pins(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PB8, 1);		
	at91_set_A_periph(AT91_PIN_PB9, 0);		

	if (pins & ATMEL_UART_RTS)
		at91_set_B_periph(AT91_PIN_PA23, 0);	
	if (pins & ATMEL_UART_CTS)
		at91_set_B_periph(AT91_PIN_PA24, 0);	
}

static struct platform_device *__initdata at91_uarts[ATMEL_MAX_UART];	

void __init at91_register_uart(unsigned id, unsigned portnr, unsigned pins)
{
	struct platform_device *pdev;
	struct atmel_uart_data *pdata;

	switch (id) {
		case 0:		
			pdev = &at91sam9g45_dbgu_device;
			configure_dbgu_pins();
			break;
		case AT91SAM9G45_ID_US0:
			pdev = &at91sam9g45_uart0_device;
			configure_usart0_pins(pins);
			break;
		case AT91SAM9G45_ID_US1:
			pdev = &at91sam9g45_uart1_device;
			configure_usart1_pins(pins);
			break;
		case AT91SAM9G45_ID_US2:
			pdev = &at91sam9g45_uart2_device;
			configure_usart2_pins(pins);
			break;
		case AT91SAM9G45_ID_US3:
			pdev = &at91sam9g45_uart3_device;
			configure_usart3_pins(pins);
			break;
		default:
			return;
	}
	pdata = pdev->dev.platform_data;
	pdata->num = portnr;		

	if (portnr < ATMEL_MAX_UART)
		at91_uarts[portnr] = pdev;
}

void __init at91_set_serial_console(unsigned portnr)
{
	if (portnr < ATMEL_MAX_UART) {
		atmel_default_console_device = at91_uarts[portnr];
		at91sam9g45_set_console_clock(at91_uarts[portnr]->id);
	}
}

void __init at91_add_device_serial(void)
{
	int i;

	for (i = 0; i < ATMEL_MAX_UART; i++) {
		if (at91_uarts[i])
			platform_device_register(at91_uarts[i]);
	}

	if (!atmel_default_console_device)
		printk(KERN_INFO "AT91: No default serial console defined.\n");
}
#else
void __init at91_register_uart(unsigned id, unsigned portnr, unsigned pins) {}
void __init at91_set_serial_console(unsigned portnr) {}
void __init at91_add_device_serial(void) {}
#endif


static int __init at91_add_standard_devices(void)
{
	at91_add_device_hdmac();
	at91_add_device_rtc();
	at91_add_device_rtt();
	at91_add_device_trng();
	at91_add_device_watchdog();
	at91_add_device_tc();
	return 0;
}

arch_initcall(at91_add_standard_devices);
