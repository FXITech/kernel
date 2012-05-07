/* linux/arch/arm/mach-exynos4/mach-fxi_c210.c
 *
 * Copyright (c) 2011 Insignal Co., Ltd.
 *		http://www.insignal.co.kr/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/serial_core.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/mmc/host.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/input.h>
#include <linux/pwm_backlight.h>
#include <linux/gpio_keys.h>
#include <linux/i2c.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/max8997.h>
#include <linux/lcd.h>
#include <linux/rfkill-gpio.h>
#include <linux/ath6kl.h>
#include <linux/delay.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>

#include <linux/dma-mapping.h>
#include <linux/memblock.h>
#include <linux/dma-contiguous.h>

#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>
#include <asm/mach-types.h>

#include <video/platform_lcd.h>

#include <plat/regs-serial.h>
#include <plat/regs-fb-v4.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/sdhci.h>
#include <plat/iic.h>
#include <plat/ehci.h>
#include <plat/clock.h>
#include <plat/gpio-cfg.h>
#include <plat/backlight.h>
#include <plat/pd.h>
#include <plat/fb.h>
#include <plat/mfc.h>
#include <plat/udc-hs.h>
#include <plat/hdmi.h>

#include <mach/ohci.h>
#include <mach/map.h>

#include "common.h"

#define MFC_RBASE 0x44000000
#define MFC_RSIZE (16 << 20)

#define MFC_LBASE 0x4b000000
#define MFC_LSIZE (16 << 20)

#define FXIFB_BASE 0x48000000
#define FXIFB_SIZE (16 << 20)
#define FXIFB_BUFS 2

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define FXI_C210_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define FXI_C210_ULCON_DEFAULT	S3C2410_LCON_CS8

#define FXI_C210_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG4 |	\
				 S5PV210_UFCON_RXTRIG4)

static struct s3c2410_uartcfg fxi_c210_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= FXI_C210_UCON_DEFAULT,
		.ulcon		= FXI_C210_ULCON_DEFAULT,
		.ufcon		= FXI_C210_UFCON_DEFAULT,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= FXI_C210_UCON_DEFAULT,
		.ulcon		= FXI_C210_ULCON_DEFAULT,
		.ufcon		= FXI_C210_UFCON_DEFAULT,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= FXI_C210_UCON_DEFAULT,
		.ulcon		= FXI_C210_ULCON_DEFAULT,
		.ufcon		= FXI_C210_UFCON_DEFAULT,
	},
	[3] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= FXI_C210_UCON_DEFAULT,
		.ulcon		= FXI_C210_ULCON_DEFAULT,
		.ufcon		= FXI_C210_UFCON_DEFAULT,
	},
};

static struct regulator_consumer_supply ldo1_consumer[] = {
  REGULATOR_SUPPLY("vdd", "s5p-adc"), /* ADC */
  REGULATOR_SUPPLY("vdd_osc", "exynos4-hdmi"),
};
static struct regulator_consumer_supply ldo3_consumer[] = {
  REGULATOR_SUPPLY("vusb_d", "s3c-hsotg"), /* USB */
  REGULATOR_SUPPLY("vusb_a", "s3c-hsotg"), /* USB */ // FIXME
  REGULATOR_SUPPLY("vdd", "exynos4-hdmi"), /* HDMI */
  REGULATOR_SUPPLY("hdmi-en", "exynos4-hdmi"), /* HDMI */
  REGULATOR_SUPPLY("vdd_pll", "exynos4-hdmi"), /* HDMI */
  REGULATOR_SUPPLY("vdd11", "s5p-mipi-csis.0"), /* MIPI */
  REGULATOR_SUPPLY("vmmc", NULL), /* sdhc-drivers want it, this is always on so we just hook it up */
};
static struct regulator_consumer_supply ldo4_consumer[] = {
  REGULATOR_SUPPLY("vdd18", "s5p-mipi-csis.0"), /* MIPI */
};

static struct regulator_consumer_supply buck1_consumer[] = {
  REGULATOR_SUPPLY("vdd_arm", NULL),
};

static struct regulator_consumer_supply buck2_consumer[] = {
  REGULATOR_SUPPLY("vdd_int", NULL),
};

static struct regulator_consumer_supply __initdata buck3_consumer[] = {
  REGULATOR_SUPPLY("vdd_g3d", NULL), /* G3D */
};

static struct regulator_init_data max8997_ldo1_data = {
  .constraints  = {
    .name   = "VDD_ADC_3.3V",
    .min_uV   = 3300000,
    .max_uV   = 3300000,
    .apply_uV = 1,
    .always_on  = 1,
    .valid_ops_mask = REGULATOR_CHANGE_STATUS,
    .state_mem  = {
      .uV   = 3300000,
      .enabled = 1,
    },
  },
  .num_consumer_supplies  = ARRAY_SIZE(ldo1_consumer),
  .consumer_supplies  = ldo1_consumer,
};

static struct regulator_init_data max8997_ldo2_data = {
  .constraints  = {
    .name   = "VALIVE_1.1V",
    .min_uV   = 1100000,
    .max_uV   = 1100000,
    .apply_uV = 1,
    .always_on  = 1,
    .valid_ops_mask = REGULATOR_CHANGE_STATUS,
    .state_mem  = {
      .uV   = 1100000,
      .enabled = 1,
    },
  },
};

static struct regulator_init_data max8997_ldo3_data = {
  .constraints  = {
    .name   = "VUOTG_D_1.1V_VUHOST_D_1.1V",
    .min_uV   = 1100000,
    .max_uV   = 1100000,
    .apply_uV = 1,
    .always_on  = 1,
    .valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
    .state_mem  = {
      .uV   = 1100000,
      .disabled = 1,
    },
  },
  .num_consumer_supplies  = ARRAY_SIZE(ldo3_consumer),
  .consumer_supplies  = ldo3_consumer,
};

static struct regulator_init_data max8997_ldo4_data = {
  .constraints  = {
    .name   = "V_MIPI_1.8V",
    .min_uV   = 1800000,
    .max_uV   = 1800000,
    .apply_uV = 1,
    .always_on  = 1,
    .valid_ops_mask = REGULATOR_CHANGE_STATUS,
    .state_mem  = {
      .uV   = 1800000,
      .disabled = 1,
    },
  },
  .num_consumer_supplies  = ARRAY_SIZE(ldo4_consumer),
  .consumer_supplies  = ldo4_consumer,
};

static struct regulator_init_data max8997_ldo5_data = {
  .constraints  = {
    .name   = "VDD_MIF_1.8V",
    .min_uV   = 1800000,
    .max_uV   = 1800000,
    .apply_uV = 1,
    .always_on  = 1,
    .valid_ops_mask = REGULATOR_CHANGE_STATUS,
    .state_mem  = {
      .uV   = 1800000,
      .disabled = 1,
    },
  },
};

static struct regulator_init_data max8997_ldo6_data = {
  .constraints  = {
    .name   = "VDD_CAM_1.8V",
    .min_uV   = 1800000,
    .max_uV   = 1800000,
    .apply_uV = 1,
    .always_on  = 1,
    .valid_ops_mask = REGULATOR_CHANGE_STATUS,
    .state_mem   = {
      .uV   = 1800000,
      .disabled = 1,
    },
  },
};

static struct regulator_init_data max8997_ldo7_data = {
  .constraints  = {
    .name   = "VDD_GPS_1.8V",
    .min_uV   = 1800000,
    .max_uV   = 1800000,
    .apply_uV = 1,
    .always_on  = 1,
    .valid_ops_mask = REGULATOR_CHANGE_STATUS,
    .state_mem  = {
      .uV   = 1800000,
      .disabled = 1,
    },
  },
};

static struct regulator_init_data max8997_ldo8_data = {
  .constraints  = {
    .name   = "VUOTG_A_3.3V_VUHOST_A_3.3V",
    .min_uV   = 3300000,
    .max_uV   = 3300000,
    .apply_uV = 1,
    .always_on  = 1,
    .valid_ops_mask = REGULATOR_CHANGE_STATUS,
    .state_mem  = {
      .uV   = 3300000,
      .enabled = 1,
    },
  },
};

static struct regulator_init_data max8997_ldo9_data = {
  .constraints  = {
    .name   = "VDD_LCD_2.8V",
    .min_uV   = 3000000,
    .max_uV   = 3000000,
    .apply_uV = 1,
    .always_on  = 1,
    .valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
    .state_mem  = {
      .uV   = 3000000,
      .enabled = 1,
    },
  },
};

static struct regulator_init_data max8997_ldo10_data = {
  .constraints  = {
    .name   = "VDD_PLL_1.1V",
    .min_uV   = 1100000,
    .max_uV   = 1100000,
    .apply_uV = 1,
    .always_on  = 1,
    .valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
    .state_mem  = {
      .uV   = 1100000,
      .enabled = 1,
    },
  },
};

static struct regulator_init_data max8997_ldo17_data = {
  .constraints  = {
    .name   = "VDD_WIFI_1.8V",
    .min_uV   = 1800000,
    .max_uV   = 1800000,
    .apply_uV = 1,
    .always_on  = 1,
    .valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
    .state_mem  = {
      .uV   = 1800000,
      .enabled = 1,
    },
  },
};

static struct regulator_init_data max8997_buck1_data = {
  .constraints  = {
    .name   = "vdd_arm",
    .min_uV   = 770000,
    .max_uV   = 1400000,
    .always_on  = 1,
    .boot_on  = 1,
    .valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
    .state_mem  = {
      .uV   = 1200000,
      .disabled = 1,
    },
  },
  .num_consumer_supplies  = ARRAY_SIZE(buck1_consumer),
  .consumer_supplies  = buck1_consumer,
};

static struct regulator_init_data max8997_buck2_data = {
  .constraints  = {
    .name   = "vdd_int",
    .min_uV   = 750000,
    .max_uV   = 1380000,
    .always_on  = 1,
    .boot_on  = 1,
    .valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
    .state_mem  = {
      .uV   = 1100000,
      .disabled = 1,
    },
  },
  .num_consumer_supplies  = ARRAY_SIZE(buck2_consumer),
  .consumer_supplies  = buck2_consumer,
};

static struct regulator_init_data max8997_buck3_data = {
  .constraints  = {
    .name   = "vdd_g3d",
    .min_uV   = 900000,
    .max_uV   = 1200000,
    .always_on  = 1,
    .boot_on  = 0,
    .valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
    REGULATOR_CHANGE_STATUS,
    .state_mem  = {
      .uV   = 1100000,
      .disabled = 1,
    },
  },
  .num_consumer_supplies  = ARRAY_SIZE(buck3_consumer),
  .consumer_supplies  = buck3_consumer,
};

static struct regulator_init_data max8997_buck5_data = {
  .constraints  = {
    .name   = "VDD_MEM",
    .min_uV   = 1200000,
    .max_uV   = 1200000,
    .apply_uV = 1,
    .always_on  = 1,
    .state_mem  = {
      .uV = 1200000,
      .mode = REGULATOR_MODE_NORMAL,
      .enabled = 1,
    },
  },
};

static struct regulator_init_data max8997_buck7_data = {
  .constraints  = {
    .name   = "VDD_IO_1.8V",
    .min_uV   = 1800000,
    .max_uV   = 1800000,
    .always_on  = 1,
    .apply_uV = 1,
    .valid_ops_mask = REGULATOR_CHANGE_STATUS,
    .state_mem  = {
      .uV = 1800000,
      .mode = REGULATOR_MODE_NORMAL,
      .enabled = 1,
    },
  },
};

static struct max8997_regulator_data __initdata fxi_c210_max8997_regulators[] = {
  { MAX8997_LDO1,   &max8997_ldo1_data },
  { MAX8997_LDO2,   &max8997_ldo2_data },
  { MAX8997_LDO3,   &max8997_ldo3_data },
  { MAX8997_LDO4,   &max8997_ldo4_data },
  { MAX8997_LDO5,   &max8997_ldo5_data },
  { MAX8997_LDO6,   &max8997_ldo6_data },
  { MAX8997_LDO7,   &max8997_ldo7_data },
  { MAX8997_LDO8,   &max8997_ldo8_data },
  { MAX8997_LDO9,   &max8997_ldo9_data },
  { MAX8997_LDO10,  &max8997_ldo10_data },
  { MAX8997_LDO10,  &max8997_ldo17_data },

  { MAX8997_BUCK1,  &max8997_buck1_data },
  { MAX8997_BUCK2,  &max8997_buck2_data },
  { MAX8997_BUCK3,  &max8997_buck3_data },
  { MAX8997_BUCK5,  &max8997_buck5_data },
  { MAX8997_BUCK7,  &max8997_buck7_data },
};

struct max8997_platform_data __initdata fxi_c210_max8997_pdata = {
  .num_regulators = ARRAY_SIZE(fxi_c210_max8997_regulators),
  .regulators = fxi_c210_max8997_regulators,

  .wakeup = true,
  .buck1_gpiodvs  = false,
  .buck2_gpiodvs  = false,
  .buck5_gpiodvs  = false,

  .ignore_gpiodvs_side_effect = true,
  .buck125_default_idx = 0x0,

  .buck125_gpios[0] = EXYNOS4_GPX0(0),
  .buck125_gpios[1] = EXYNOS4_GPX0(1),
  .buck125_gpios[2] = EXYNOS4_GPX0(2),

  .buck1_voltage[0]   = 1250000,
  .buck1_voltage[1]   = 1200000,
  .buck1_voltage[2]   = 1150000,
  .buck1_voltage[3]   = 1100000,
  .buck1_voltage[4]   = 1050000,
  .buck1_voltage[5]   = 1000000,
  .buck1_voltage[6]   = 950000,
  .buck1_voltage[7]   = 950000,

  .buck2_voltage[0]   = 1100000,
  .buck2_voltage[1]   = 1100000,
  .buck2_voltage[2]   = 1100000,
  .buck2_voltage[3]   = 1100000,
  .buck2_voltage[4]   = 1000000,
  .buck2_voltage[5]   = 1000000,
  .buck2_voltage[6]   = 1000000,
  .buck2_voltage[7]   = 1000000,

  .buck5_voltage[0]   = 1200000,
  .buck5_voltage[1]   = 1200000,
  .buck5_voltage[2]   = 1200000,
  .buck5_voltage[3]   = 1200000,
  .buck5_voltage[4]   = 1200000,
  .buck5_voltage[5]   = 1200000,
  .buck5_voltage[6]   = 1200000,
  .buck5_voltage[7]   = 1200000,
};

/* I2C0 */
static struct i2c_board_info i2c0_devs[] __initdata = {
	{
		I2C_BOARD_INFO("max8997", (0xCC >> 1)),
		.platform_data	= &fxi_c210_max8997_pdata,
		.irq		= IRQ_EINT(4),
	},
#ifdef CONFIG_TOUCHSCREEN_UNIDISPLAY_TS
	{
		I2C_BOARD_INFO("unidisplay_ts", 0x41),
		.irq = IRQ_TS,
	},
#endif
};

/* I2C1 */
static struct i2c_board_info i2c1_devs[] __initdata = {
	{
		I2C_BOARD_INFO("alc5625", 0x1E),
	},
};

static struct s3c_sdhci_platdata fxi_c210_hsmmc0_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_INTERNAL,
};

static struct s3c_sdhci_platdata fxi_c210_hsmmc2_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_INTERNAL,
};


/*
 * WLAN: SDIO Host will call this func at booting time
 */
static int fxi_c210_wifi_status_register(void (*notify_func)
		(struct platform_device *, int state));

/* WLAN: MMC3-SDIO */
static struct s3c_sdhci_platdata fxi_c210_hsmmc3_pdata __initdata = {
	.max_width		= 4,
	.host_caps		= MMC_CAP_4_BIT_DATA |
			MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED,
	.cd_type		= S3C_SDHCI_CD_EXTERNAL,
	.ext_cd_init		= fxi_c210_wifi_status_register,
};

/*
 * WLAN: Save SDIO Card detect func into this pointer
 */
static void (*wifi_status_cb)(struct platform_device *, int state);

static int fxi_c210_wifi_status_register(void (*notify_func)
		(struct platform_device *, int state))
{
	if (!notify_func)
		return -EAGAIN;
	else
		wifi_status_cb = notify_func;

	return 0;
}

#define FXI_C210_WLAN_WOW EXYNOS4_GPX2(3)
#define FXI_C210_WLAN_RESET EXYNOS4_GPX2(4)


static void fxi_c210_wlan_setup_power(bool val)
{
	int err;

	if (val) {
		err = gpio_request_one(FXI_C210_WLAN_RESET,
				GPIOF_OUT_INIT_LOW, "GPX2_4");
		if (err) {
			pr_warning("FXI_C210: Not obtain WIFI gpios\n");
			return;
		}
		s3c_gpio_cfgpin(FXI_C210_WLAN_RESET, S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(FXI_C210_WLAN_RESET,
						S3C_GPIO_PULL_NONE);
		/* VDD33,I/O Supply must be done */
		gpio_set_value(FXI_C210_WLAN_RESET, 0);
		udelay(30);	/*Tb */
		gpio_direction_output(FXI_C210_WLAN_RESET, 1);
	} else {
		gpio_direction_output(FXI_C210_WLAN_RESET, 0);
		gpio_free(FXI_C210_WLAN_RESET);
	}

	mdelay(100);

	return;
}

/*
 * This will be called at init time of WLAN driver
 */
static int fxi_c210_wifi_set_detect(bool val)
{
	if (!wifi_status_cb) {
		printk(KERN_WARNING "WLAN: Nobody to notify\n");
		return -EAGAIN;
	}
	if (true == val) {
		fxi_c210_wlan_setup_power(true);
		wifi_status_cb(&s3c_device_hsmmc3, 1);
	} else {
		fxi_c210_wlan_setup_power(false);
		wifi_status_cb(&s3c_device_hsmmc3, 0);
	}

	return 0;
}

struct ath6kl_platform_data fxi_c210_wlan_data  __initdata = {
	.setup_power = fxi_c210_wifi_set_detect,
};


/* USB EHCI */
static struct s5p_ehci_platdata fxi_c210_ehci_pdata;

static void __init fxi_c210_ehci_init(void)
{
	struct s5p_ehci_platdata *pdata = &fxi_c210_ehci_pdata;

	s5p_ehci_set_platdata(pdata);
}

/* USB OHCI */
static struct exynos4_ohci_platdata fxi_c210_ohci_pdata;

static void __init fxi_c210_ohci_init(void)
{
	struct exynos4_ohci_platdata *pdata = &fxi_c210_ohci_pdata;

	exynos4_ohci_set_platdata(pdata);
}

/* USB OTG */
static struct s3c_hsotg_plat fxi_c210_hsotg_pdata;

static struct gpio_led fxi_c210_gpio_leds[] = {
	{
		.name			= "fxi_c210::status1",
		.default_trigger	= "heartbeat",
		.gpio			= EXYNOS4_GPX1(3),
		.active_low		= 1,
	},
	{
		.name			= "fxi_c210::status2",
		.default_trigger	= "mmc0",
		.gpio			= EXYNOS4_GPX1(4),
		.active_low		= 1,
	},
};

static struct gpio_led_platform_data fxi_c210_gpio_led_info = {
	.leds		= fxi_c210_gpio_leds,
	.num_leds	= ARRAY_SIZE(fxi_c210_gpio_leds),
};

static struct platform_device fxi_c210_leds_gpio = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &fxi_c210_gpio_led_info,
	},
};

static struct gpio_keys_button fxi_c210_gpio_keys_table[] = {
	{
		.code			= KEY_MENU,
		.gpio			= EXYNOS4_GPX1(5),
		.desc			= "gpio-keys: KEY_MENU",
		.type			= EV_KEY,
		.active_low		= 1,
		.wakeup			= 1,
		.debounce_interval	= 1,
	}, {
		.code			= KEY_HOME,
		.gpio			= EXYNOS4_GPX1(6),
		.desc			= "gpio-keys: KEY_HOME",
		.type			= EV_KEY,
		.active_low		= 1,
		.wakeup			= 1,
		.debounce_interval	= 1,
	}, {
		.code			= KEY_BACK,
		.gpio			= EXYNOS4_GPX1(7),
		.desc			= "gpio-keys: KEY_BACK",
		.type			= EV_KEY,
		.active_low		= 1,
		.wakeup			= 1,
		.debounce_interval	= 1,
	}, {
		.code			= KEY_UP,
		.gpio			= EXYNOS4_GPX2(0),
		.desc			= "gpio-keys: KEY_UP",
		.type			= EV_KEY,
		.active_low		= 1,
		.wakeup			= 1,
		.debounce_interval	= 1,
	}, {
		.code			= KEY_DOWN,
		.gpio			= EXYNOS4_GPX2(1),
		.desc			= "gpio-keys: KEY_DOWN",
		.type			= EV_KEY,
		.active_low		= 1,
		.wakeup			= 1,
		.debounce_interval	= 1,
	},
};

static struct gpio_keys_platform_data fxi_c210_gpio_keys_data = {
	.buttons	= fxi_c210_gpio_keys_table,
	.nbuttons	= ARRAY_SIZE(fxi_c210_gpio_keys_table),
};

static struct platform_device fxi_c210_device_gpiokeys = {
	.name		= "gpio-keys",
	.dev		= {
		.platform_data	= &fxi_c210_gpio_keys_data,
	},
};
#if 0
static void lcd_hv070wsa_set_power(struct plat_lcd_data *pd, unsigned int power)
{
	int ret;
<<<<<<< HEAD

	if (power)
		ret = gpio_request_one(EXYNOS4_GPE3(4),
					GPIOF_OUT_INIT_HIGH, "GPE3_4");
	else
		ret = gpio_request_one(EXYNOS4_GPE3(4),
					GPIOF_OUT_INIT_LOW, "GPE3_4");

	gpio_free(EXYNOS4_GPE3(4));

	if (ret)
		pr_err("failed to request gpio for LCD power: %d\n", ret);
}

=======

	if (power)
		ret = gpio_request_one(EXYNOS4_GPE3(4),
					GPIOF_OUT_INIT_HIGH, "GPE3_4");
	else
		ret = gpio_request_one(EXYNOS4_GPE3(4),
					GPIOF_OUT_INIT_LOW, "GPE3_4");

	gpio_free(EXYNOS4_GPE3(4));

	if (ret)
		pr_err("failed to request gpio for LCD power: %d\n", ret);
}

>>>>>>> parent of a9b1745... Removes dead code from FXI C210 machine file.
static struct plat_lcd_data fxi_c210_lcd_hv070wsa_data = {
	.set_power = lcd_hv070wsa_set_power,
	.min_uV		= 3300000,
	.max_uV		= 3300000,
};

static struct platform_device fxi_c210_lcd_hv070wsa = {
	.name			= "platform-lcd",
	.dev.parent		= &s5p_device_fimd0.dev,
	.dev.platform_data	= &fxi_c210_lcd_hv070wsa_data,
};

static struct s3c_fb_pd_win fxi_c210_fb_win0 = {
	.win_mode = {
		.left_margin	= 64,
		.right_margin	= 16,
		.upper_margin	= 64,
		.lower_margin	= 16,
		.hsync_len	= 48,
		.vsync_len	= 3,
		.xres		= 1024,
		.yres		= 600,
	},
	.max_bpp		= 32,
	.default_bpp		= 24,
};

static struct s3c_fb_platdata fxi_c210_lcd_pdata __initdata = {
	.win[0]		= &fxi_c210_fb_win0,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
	.vidcon1	= VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC |
				VIDCON1_INV_VCLK,
	.setup_gpio	= exynos4_fimd0_gpio_setup_24bpp,
};
#endif
/* Bluetooth rfkill gpio platform data */
struct rfkill_gpio_platform_data fxi_c210_bt_pdata = {
	.reset_gpio	= EXYNOS4_GPX2(2),
	.shutdown_gpio	= -1,
	.type		= RFKILL_TYPE_BLUETOOTH,
	.name		= "fxi_c210-bt",
};

/* Bluetooth Platform device */
static struct platform_device fxi_c210_device_bluetooth = {
	.name		= "rfkill_gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &fxi_c210_bt_pdata,
	},
};


/* Bluetooth button */
static struct gpio_keys_button btbutton[] = {
  [0] = {
    .desc   = "Bluetooth button",
    .code   = KEY_RECORD,
    .type   = EV_KEY,
    .gpio   = EXYNOS4_GPX3(5),
    .wakeup = 0,
  },
};

static struct gpio_keys_platform_data bt_gpio_keys_data = {
  .buttons        = btbutton,
  .nbuttons       = 1,
};
 
static struct platform_device btbutton_device_gpiokeys = {
  .name      = "gpio-keys",
  .id      = -1,
  .dev = {
    .platform_data = &bt_gpio_keys_data,
  },
};

/* FXI FB */
static struct resource fxifb_resource[] = {
  [0] = {
    .start = FXIFB_BASE,
    .end   = FXIFB_BASE + (FXIFB_SIZE*FXIFB_BUFS) - 1,
    .flags = IORESOURCE_MEM,
  },
};

static u64 fxifb_dma_mask = DMA_BIT_MASK(32);

struct platform_device fxifb_device = {
  .name   = "fxifb",
  .id   = -1,
  .num_resources  = ARRAY_SIZE(fxifb_resource),
  .resource = fxifb_resource,
  .dev    = {
    .dma_mask   = &fxifb_dma_mask,
    .coherent_dma_mask  = DMA_BIT_MASK(32),
  },
};

/* FXI Sysfs */

static struct platform_device fxi_sysfs = {
  .name = "fxi-sysfs",
  .id = -1,
};

/* fxi-fxiid entry */
static struct platform_device fxi_fxiid = {
  .name = "fxi-fxiid",
  .id = -1,
 }; 

static struct platform_device fxi_mali = {
  .name = "mali_drm",
  .id = -1,
};

static struct platform_device *fxi_c210_devices[] __initdata = {
	&s3c_device_hsmmc2,
	&s3c_device_hsmmc0,
	&s3c_device_hsmmc3,
	&s3c_device_i2c0,
	&s3c_device_i2c1,

  &fxifb_device,

	&s3c_device_rtc,
	&s3c_device_wdt,
	&s3c_device_usb_hsotg,
	&s5p_device_ehci,
	&s5p_device_fimc0,
	&s5p_device_fimc1,
	&s5p_device_fimc2,
	&s5p_device_fimc3,
	&s5p_device_fimc_md,
	&s5p_device_fimd0,
	&s5p_device_g2d,
	&s5p_device_hdmi,
	&s5p_device_i2c_hdmiphy,
//	&s5p_device_jpeg,
	&s5p_device_mfc,
	&s5p_device_mfc_l,
	&s5p_device_mfc_r,
	&s5p_device_mixer,
	&samsung_asoc_dma,
	&exynos4_device_i2s0,
	&exynos4_device_ohci,
	&fxi_c210_device_gpiokeys,
	//&fxi_c210_lcd_hv070wsa,
	&fxi_c210_leds_gpio,
	&fxi_c210_device_bluetooth,
	&exynos4_device_tmu,
	&exynos4_device_pcm0,
	&exynos4_device_pcm1,
	&exynos4_device_pcm2,
	&exynos4_device_ac97,
	&exynos4_device_spdif,
  	&btbutton_device_gpiokeys,
  	&fxi_sysfs,
  	&fxi_fxiid,
    &fxi_mali,
};

/* LCD Backlight data */
static struct samsung_bl_gpio_info fxi_c210_bl_gpio_info = {
	.no		= EXYNOS4_GPD0(0),
	.func		= S3C_GPIO_SFN(2),
};

static struct platform_pwm_backlight_data fxi_c210_bl_data = {
	.pwm_id		= 0,
	.pwm_period_ns	= 1000,
};

/* LCD Backlight data */
static struct samsung_bl_gpio_info fxi_c210_bl_gpio_info = {
	.no		= EXYNOS4_GPD0(0),
	.func		= S3C_GPIO_SFN(2),
};

static struct platform_pwm_backlight_data fxi_c210_bl_data = {
	.pwm_id		= 0,
	.pwm_period_ns	= 1000,
};

static void __init fxi_c210_bt_setup(void)
{
	gpio_request(EXYNOS4_GPA0(0), "GPIO BT_UART");
	/* 4 UART Pins configuration */
	s3c_gpio_cfgrange_nopull(EXYNOS4_GPA0(0), 4, S3C_GPIO_SFN(2));
	/* Setup BT Reset, this gpio will be requesed by rfkill-gpio */
	s3c_gpio_cfgpin(EXYNOS4_GPX2(2), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPX2(2), S3C_GPIO_PULL_NONE);
}

/* I2C module and id for HDMIPHY */
static struct i2c_board_info hdmiphy_info = {
        I2C_BOARD_INFO("hdmiphy", 0x38),
};

static void s5p_tv_setup(void)
{
	/* Direct HPD to HDMI chip */
	gpio_request_one(EXYNOS4_GPX3(7), GPIOF_IN, "hpd-plug");
	s3c_gpio_cfgpin(EXYNOS4_GPX3(7), S3C_GPIO_SFN(0x3));
	s3c_gpio_setpull(EXYNOS4_GPX3(7), S3C_GPIO_PULL_NONE);
}

static void __init fxi_c210_map_io(void)
{
	exynos_init_io(NULL, 0);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(fxi_c210_uartcfgs, ARRAY_SIZE(fxi_c210_uartcfgs));
}

static void __init fxi_c210_power_init(void)
{
	gpio_request(EXYNOS4_GPX0(4), "PMIC_IRQ");
	s3c_gpio_cfgpin(EXYNOS4_GPX0(4), S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(EXYNOS4_GPX0(4), S3C_GPIO_PULL_NONE);
}

static void __init fxi_c210_reserve(void)
{
  s5p_mfc_reserve_mem(MFC_RBASE, MFC_RSIZE, MFC_LBASE, MFC_LSIZE);

  if (memblock_remove(FXIFB_BASE, (FXIFB_SIZE*FXIFB_BUFS))) {
    printk (KERN_ERR "Can't allocate FXI framebuffer memory\n");
  }
}

static void __init fxi_c210_machine_init(void)
{
	fxi_c210_power_init();

	s3c_i2c0_set_platdata(NULL);
	i2c_register_board_info(0, i2c0_devs, ARRAY_SIZE(i2c0_devs));

	s3c_i2c1_set_platdata(NULL);
	i2c_register_board_info(1, i2c1_devs, ARRAY_SIZE(i2c1_devs));

	/*
	 * Since sdhci instance 2 can contain a bootable media,
	 * sdhci instance 0 is registered after instance 2.
	 */
	s3c_sdhci2_set_platdata(&fxi_c210_hsmmc2_pdata);
	s3c_sdhci0_set_platdata(&fxi_c210_hsmmc0_pdata);
	s3c_sdhci3_set_platdata(&fxi_c210_hsmmc3_pdata);

	fxi_c210_ehci_init();
	fxi_c210_ohci_init();
	s3c_hsotg_set_platdata(&fxi_c210_hsotg_pdata);
	clk_xusbxti.rate = 24000000;

	s5p_tv_setup();
	s5p_i2c_hdmiphy_set_platdata(NULL);
	s5p_hdmi_set_platdata(&hdmiphy_info, NULL, 0);

//	s5p_fimd0_set_platdata(&fxi_c210_lcd_pdata);

	platform_add_devices(fxi_c210_devices, ARRAY_SIZE(fxi_c210_devices));


	samsung_bl_set(&fxi_c210_bl_gpio_info, &fxi_c210_bl_data);

//	fxi_c210_bt_setup();

//	ath6kl_set_platform_data(&fxi_c210_wlan_data);
}

MACHINE_START(FXI_C210, "FXI_C210")
	/* Maintainer: JeongHyeon Kim <jhkim@insignal.co.kr> */
	.atag_offset	= 0x100,
	.init_irq	= exynos4_init_irq,
	.map_io		= fxi_c210_map_io,
	.handle_irq	= gic_handle_irq,
	.init_machine	= fxi_c210_machine_init,
	.timer		= &exynos4_timer,
	.reserve	= &fxi_c210_reserve,
	.restart	= exynos4_restart,
MACHINE_END
