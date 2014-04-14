/*
 * arch/arm/mach-kirkwood/openrd-setup.c
 *
 * Marvell OpenRD (Base|Client|Ultimate) Board Setup
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/kirkwood.h>
#include <plat/mvsdio.h>
#include "common.h"
#include "mpp.h"

static struct mtd_partition openrd_nand_parts[] = {
	{
		.name		= "u-boot",
		.offset		= 0,
		.size		= SZ_1M,
		.mask_flags	= MTD_WRITEABLE
	}, {
		.name		= "uImage",
		.offset		= MTDPART_OFS_NXTBLK,
		.size		= SZ_4M
	}, {
		.name		= "root",
		.offset		= MTDPART_OFS_NXTBLK,
		.size		= MTDPART_SIZ_FULL
	},
};

static struct mv643xx_eth_platform_data openrd_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

static struct mv643xx_eth_platform_data openrd_ge01_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(24),
};

static struct mv_sata_platform_data openrd_sata_data = {
	.n_ports	= 2,
};

static struct mvsdio_platform_data openrd_mvsdio_data = {
	.gpio_card_detect = 29,	
};

static unsigned int openrd_mpp_config[] __initdata = {
	MPP12_SD_CLK,
	MPP13_SD_CMD,
	MPP14_SD_D0,
	MPP15_SD_D1,
	MPP16_SD_D2,
	MPP17_SD_D3,
	MPP28_GPIO,
	MPP29_GPIO,
	MPP34_GPIO,
	0
};

static unsigned int openrd_uart1_mpp_config[] __initdata = {
	MPP13_UART1_TXD,
	MPP14_UART1_RXD,
	0
};

static struct i2c_board_info i2c_board_info[] __initdata = {
	{
		I2C_BOARD_INFO("cs42l51", 0x4a),
	},
};

static struct platform_device openrd_client_audio_device = {
	.name		= "openrd-client-audio",
	.id		= -1,
};

static int __initdata uart1;

static int __init sd_uart_selection(char *str)
{
	uart1 = -EINVAL;

	
	if (!str)
		return 0;

	if (!strncmp(str, "232", 3)) {
		uart1 = 232;
	} else if (!strncmp(str, "485", 3)) {
		if (machine_is_openrd_base()) {
			uart1 = -ENODEV;
			return 1;
		}

		uart1 = 485;
	}
	return 1;
}
__setup("kw_openrd_init_uart1=", sd_uart_selection);

static int __init uart1_mpp_config(void)
{
	kirkwood_mpp_conf(openrd_uart1_mpp_config);

	if (gpio_request(34, "SD_UART1_SEL")) {
		printk(KERN_ERR "GPIO request failed for SD/UART1 selection"
				", gpio: 34\n");
		return -EIO;
	}

	if (gpio_request(28, "RS232_RS485_SEL")) {
		printk(KERN_ERR "GPIO request failed for RS232/RS485 selection"
				", gpio# 28\n");
		gpio_free(34);
		return -EIO;
	}

	gpio_direction_output(34, 0);

	if (uart1 == 232)
		gpio_direction_output(28, 0);
	else
		gpio_direction_output(28, 1);

	gpio_free(34);
	gpio_free(28);

	return 0;
}

static void __init openrd_init(void)
{
	kirkwood_init();
	kirkwood_mpp_conf(openrd_mpp_config);

	kirkwood_uart0_init();
	kirkwood_nand_init(ARRAY_AND_SIZE(openrd_nand_parts), 25);

	kirkwood_ehci_init();

	if (machine_is_openrd_ultimate()) {
		openrd_ge00_data.phy_addr = MV643XX_ETH_PHY_ADDR(0);
		openrd_ge01_data.phy_addr = MV643XX_ETH_PHY_ADDR(1);
	}

	kirkwood_ge00_init(&openrd_ge00_data);
	if (!machine_is_openrd_base())
		kirkwood_ge01_init(&openrd_ge01_data);

	kirkwood_sata_init(&openrd_sata_data);

	kirkwood_i2c_init();

	if (machine_is_openrd_client() || machine_is_openrd_ultimate()) {
		platform_device_register(&openrd_client_audio_device);
		i2c_register_board_info(0, i2c_board_info,
			ARRAY_SIZE(i2c_board_info));
		kirkwood_audio_init();
	}

	if (uart1 <= 0) {
		if (uart1 < 0)
			printk(KERN_ERR "Invalid kernel parameter to select "
				"UART1. Defaulting to SD. ERROR CODE: %d\n",
				uart1);

		if (gpio_request(34, "SD_UART1_SEL")) {
			printk(KERN_ERR "GPIO request failed for SD/UART1 "
					"selection, gpio: 34\n");
		} else {

			gpio_direction_output(34, 1);
			gpio_free(34);
			kirkwood_sdio_init(&openrd_mvsdio_data);
		}
	} else {
		if (!uart1_mpp_config())
			kirkwood_uart1_init();
	}
}

static int __init openrd_pci_init(void)
{
	if (machine_is_openrd_base() ||
	    machine_is_openrd_client() ||
	    machine_is_openrd_ultimate())
		kirkwood_pcie_init(KW_PCIE0);

	return 0;
}
subsys_initcall(openrd_pci_init);

#ifdef CONFIG_MACH_OPENRD_BASE
MACHINE_START(OPENRD_BASE, "Marvell OpenRD Base Board")
	
	.atag_offset	= 0x100,
	.init_machine	= openrd_init,
	.map_io		= kirkwood_map_io,
	.init_early	= kirkwood_init_early,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
	.restart	= kirkwood_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_OPENRD_CLIENT
MACHINE_START(OPENRD_CLIENT, "Marvell OpenRD Client Board")
	
	.atag_offset	= 0x100,
	.init_machine	= openrd_init,
	.map_io		= kirkwood_map_io,
	.init_early	= kirkwood_init_early,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
	.restart	= kirkwood_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_OPENRD_ULTIMATE
MACHINE_START(OPENRD_ULTIMATE, "Marvell OpenRD Ultimate Board")
	
	.atag_offset	= 0x100,
	.init_machine	= openrd_init,
	.map_io		= kirkwood_map_io,
	.init_early	= kirkwood_init_early,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
	.restart	= kirkwood_restart,
MACHINE_END
#endif
