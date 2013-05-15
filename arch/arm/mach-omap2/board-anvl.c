/*
 * Copyright (C) 2010 Network Improv
 *
 * Modified from board-omap3beagle.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/pwm.h>
#include <linux/leds_pwm.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/opp.h>
#include <linux/cpu.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand.h>
#include <linux/mmc/host.h>
#include <linux/usb/phy.h>
#include <linux/usb/nop-usb-xceiv.h>

#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/i2c/twl.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include "common.h"
#include "omap_device.h"
#include "gpmc.h"
#include "soc.h"
#include "mux.h"
#include "hsmmc.h"
#include "pm.h"
#include "board-flash.h"
#include "common-board-devices.h"
#include "sdram-micron-mt46h32m32lf-6.h"

#define	NAND_CS	0

static struct mtd_partition board_nand_partitions[] = {
	/* All the partition sizes are listed in terms of NAND block size */
	{
		.name		= "X-Loader",
		.offset		= 0,
		.size		= 4 * NAND_BLOCK_SIZE,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	{
		.name		= "U-Boot",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x80000 */
		.size		= 15 * NAND_BLOCK_SIZE,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	{
		.name		= "U-Boot Env",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x260000 */
		.size		= 1 * NAND_BLOCK_SIZE,
	},
	{
		.name		= "Kernel",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x280000 */
		.size		= 32 * NAND_BLOCK_SIZE,
	},
	{
		.name		= "File System",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x680000 */
		.size		= MTDPART_SIZ_FULL,
	},
};

static struct gpio_led board_gpio_leds[] = {
	{
		.name			= "anvl::cpu",
		.default_trigger	= "cpu0",
		.gpio			= 175,
	},
};

static struct gpio_led_platform_data board_gpio_led_data = {
	.leds		= board_gpio_leds,
	.num_leds	= ARRAY_SIZE(board_gpio_leds),
};

static struct platform_device board_leds_gpio = {
	.name	= "leds-gpio",
	.id	= 1,
	.dev	= {
		.platform_data	= &board_gpio_led_data,
	},
};

/* VMMC1 regulator on twl */
static struct regulator_consumer_supply board_vmmc1_supply[] = {
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.0"),
};

static struct twl4030_gpio_platform_data board_gpio_data = {
	.use_leds	= false,
};

/* VMMC1 for MMC1 pins CMD, CLK, DAT0..DAT3 (20 mA, plus card == max 220 mA) */
static struct regulator_init_data board_vmmc1 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(board_vmmc1_supply),
	.consumer_supplies	= board_vmmc1_supply,
};

static struct twl4030_platform_data board_twldata = {
	.gpio		= &board_gpio_data,
	.vmmc1		= &board_vmmc1,
};

static int __init board_i2c_init(void)
{
	omap3_pmic_get_config(&board_twldata,
			      TWL_COMMON_PDATA_USB |
			      TWL_COMMON_PDATA_MADC |
			      TWL_COMMON_PDATA_AUDIO, 0);

	omap3_pmic_init("twl4030", &board_twldata);

	return 0;
}

/* Fixed regulator 1v8_io */
static struct regulator_consumer_supply board_1v8_io_supply[] = {
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.1"),
};

static struct regulator_init_data board_1v8_io_init = {
	.num_consumer_supplies	= ARRAY_SIZE(board_1v8_io_supply),
	.consumer_supplies	= board_1v8_io_supply,
};

static struct fixed_voltage_config board_1v8_io_config = {
	.supply_name		= "1v8_io",
	.microvolts		= 1800000,
	.enabled_at_boot	= 1,
	.init_data		= &board_1v8_io_init,
};

static struct platform_device board_1v8_io_regulator = {
	.name		= "reg-fixed-voltage",
	.id		= 2,
	.dev = {
		.platform_data	= &board_1v8_io_config,
	},
};

/* Fixed regulator 3v3_sys */
static struct regulator_consumer_supply board_3v3_sys_supply[] = {
};

static struct regulator_init_data board_3v3_sys_init = {
	.num_consumer_supplies	= ARRAY_SIZE(board_3v3_sys_supply),
	.consumer_supplies	= board_3v3_sys_supply,
};

static struct fixed_voltage_config board_3v3_sys_config = {
	.supply_name		= "3v3_sys",
	.microvolts		= 3300000,
	.enabled_at_boot	= 1,
	.init_data		= &board_3v3_sys_init,
};

static struct platform_device board_3v3_sys_regulator = {
	.name		= "reg-fixed-voltage",
	.id		= 3,
	.dev = {
		.platform_data	= &board_3v3_sys_config,
	},
};

/*
 * Fixed regulator on mwifex at gpio155 powered by 3v3_sys
 * REVISIT: It seems that gpio155 does not control a regulator
 * on mwifex?
 * REVISIT: MMC3 will block deeper omap idle states if
 * mwifex_sdio is loaded.
 */
static struct regulator_consumer_supply board_wlan_supply[] = {
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.2"),
};

static struct regulator_init_data board_wlan_init = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(board_wlan_supply),
	.consumer_supplies	= board_wlan_supply,
};

static struct fixed_voltage_config board_wlan_config = {
	.supply_name		= "wlan",
	.microvolts		= 3300000,
	.gpio			= 155,
	.startup_delay		= 70000,	/* 70 msec */
	.enable_high		= 1,
	.enabled_at_boot	= 0,
	.init_data		= &board_wlan_init,
};

static struct platform_device board_wlan_regulator = {
	.name		= "reg-fixed-voltage",
	.id		= 4,
	.dev = {
		.platform_data	= &board_wlan_config,
	},
};

static struct platform_device *board_devices[] __initdata = {
	&board_leds_gpio,
	&board_1v8_io_regulator,
	&board_3v3_sys_regulator,
	&board_wlan_regulator,
};

static struct omap2_hsmmc_info mmc[] = {
	{
		.name		= "microsd",
		.mmc		= 1,
		.caps		= MMC_CAP_4_BIT_DATA,
		.gpio_wp	= -EINVAL,
		.gpio_cd	= -EINVAL,
	},
	{
		.name		= "emmc",
		.mmc		= 2,
		.caps		= MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
		.nonremovable	= true,
	},
	{
		.name		= "mwifi",
		.mmc		= 3,
		.caps		= MMC_CAP_4_BIT_DATA | MMC_CAP_POWER_OFF_CARD,
		.gpio_wp	= -EINVAL,
		.gpio_cd	= -EINVAL,
		.nonremovable	= true,
	},
	{}	/* Terminator */
};

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	/* UART1 connected to mwifiex */
	OMAP3_MUX(UART1_CTS, OMAP_PIN_INPUT_PULLDOWN | OMAP_MUX_MODE0),
	OMAP3_MUX(UART1_RTS, OMAP_PIN_OUTPUT | OMAP_MUX_MODE0),
	OMAP3_MUX(UART1_RX, OMAP_PIN_OFF_WAKEUPENABLE |
		  OMAP_PIN_INPUT | OMAP_MUX_MODE0),

	/* UART3 optional debug console */
	OMAP3_MUX(UART1_TX, OMAP_PIN_OUTPUT | OMAP_MUX_MODE0),
	OMAP3_MUX(UART3_RX_IRRX, OMAP_PIN_OFF_WAKEUPENABLE |
		  OMAP_PIN_INPUT | OMAP_MUX_MODE0),
	OMAP3_MUX(UART3_TX_IRTX, OMAP_PIN_OUTPUT | OMAP_MUX_MODE0),

	/* optional microSD slot */
	OMAP3_MUX(SDMMC1_CLK, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC1_CMD, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC1_DAT0, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC1_DAT1, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC1_DAT2, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC1_DAT3, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),

	/*
	 * internal eMMC, first pin is gpio75 and must be kept high for
	 * power with pull-up enabled too
	 * REVISIT: Check if pulls are needed for off-idle
	 */
	OMAP3_MUX(DSS_DATA5, OMAP_PIN_OFF_OUTPUT_HIGH |
		  OMAP_OFF_PULL_EN | OMAP_OFF_PULL_UP |
		  OMAP_PULL_ENA | OMAP_PULL_UP |
		  OMAP_PIN_OUTPUT | OMAP_MUX_MODE4),
	OMAP3_MUX(SDMMC2_CLK, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_CMD, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT0, OMAP_PIN_INPUT | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT1, OMAP_PIN_INPUT | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT2, OMAP_PIN_INPUT | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT3, OMAP_PIN_INPUT | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT4, OMAP_PIN_INPUT | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT5, OMAP_PIN_INPUT | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT6, OMAP_PIN_INPUT | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT7, OMAP_PIN_INPUT | OMAP_MUX_MODE0),

	/*
	 * mwifiex on sdio, first five pins are:
	 * PDN, SLEEP, WKUP, EEPROM_WP, HOST_WKUP
	 */
	OMAP3_MUX(MCBSP4_FSX, OMAP_PIN_INPUT | OMAP_MUX_MODE4),
	OMAP3_MUX(MCSPI1_CS3, OMAP_PIN_INPUT | OMAP_MUX_MODE4),
	OMAP3_MUX(MCBSP4_DR, OMAP_PIN_INPUT | OMAP_MUX_MODE4),
	OMAP3_MUX(MCSPI1_CS2, OMAP_PIN_INPUT | OMAP_MUX_MODE4),
	OMAP3_MUX(SYS_CLKOUT1, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE4),
	OMAP3_MUX(ETK_CLK, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE2),
	OMAP3_MUX(ETK_CTL, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE2),
	OMAP3_MUX(ETK_D3, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE2),
	OMAP3_MUX(ETK_D4, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE2),
	OMAP3_MUX(ETK_D5, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE2),
	OMAP3_MUX(ETK_D6, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE2),

	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#endif

static void __init board_init(void)
{
	omap3_mux_init(board_mux, OMAP_PACKAGE_CBP);
	omap_serial_init();

	/* Ensure SDRC pins are mux'd for self-refresh */
	omap_mux_init_signal("sdrc_cke0", OMAP_PIN_OUTPUT);
	omap_mux_init_signal("sdrc_cke1", OMAP_PIN_OUTPUT);
	omap_sdrc_init(mt46h32m32lf6_sdrc_params,
		       mt46h32m32lf6_sdrc_params);

	platform_add_devices(board_devices,
			     ARRAY_SIZE(board_devices));
	board_i2c_init();
	omap_hsmmc_init(mmc);
	usb_bind_phy("musb-hdrc.0.auto", 0, "twl4030_usb");
	usb_musb_init(NULL);
	board_nand_init(board_nand_partitions,
			ARRAY_SIZE(board_nand_partitions), NAND_CS,
			NAND_BUSWIDTH_16, NULL);

	/*
	 * Ensure msecure is mux'd to be able to set the RTC.
	 * REVISIT: Is this needed?
	 */
	omap_mux_init_signal("sys_drm_msecure", OMAP_PIN_OFF_OUTPUT_HIGH);
}

MACHINE_START(ANVL, "ANVL")
	.atag_offset	= 0x100,
	.reserve	= omap_reserve,
	.map_io		= omap3_map_io,
	.init_early	= omap3_init_early,
	.init_irq	= omap3_init_irq,
	.handle_irq	= omap3_intc_handle_irq,
	.init_machine	= board_init,
	.init_late	= omap3_init_late,
	.init_time	= omap3_sync32k_timer_init,
	.restart	= omap3xxx_restart,
MACHINE_END
