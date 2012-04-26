//--------------------------------------------------------------------------------------------------
//  HKC1XX Board : HKC1XX sysfs driver (charles.park)
//  2010.04.20
//   
//  FXI USB Dongle Sysfs Driver Update (icarus)
//  2011.07.06
//
//--------------------------------------------------------------------------------------------------
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/input.h>
#include <linux/gpio.h>

#include <mach/gpio.h>
#include <mach/regs-gpio.h>
#include <plat/gpio-cfg.h>


#define	DEBUG_PM_MSG
#define	SLEEP_DISABLE_FLAG

#if defined(SLEEP_DISABLE_FLAG)
	#ifdef CONFIG_HAS_WAKELOCK
		#include <linux/wakelock.h>
		static struct wake_lock 	sleep_wake_lock;
	#endif
#endif

// GPIO Index Define
enum	{
	// WIFI Control Port
	WIFI_RESET,
	WIFI_REG,
	WIFI_WAKEUP,
	WIFI_WAKEUP_HOST,
	
	// Bluetooth Control Port
	BLUETOOTH_RESET,
	BLUETOOTH_REG,
	BLUETOOTH_WAKEUP,
	BLUETOOTH_WAKEUP_HOST,
	BLUETOOTH_BUTTON,
	
	// 3G Modem Control Port
	MODEM_POWER,
	MODEM_RESET,
	MODEM_DISABLE1,
	MODEM_DISABLE2,
	
	// Status LED Display
	STATUS_LED_RED,
	STATUS_LED_GREEN,
	STATUS_LED_BLUE,
	STATUS_LED_NETRED,
	STATUS_LED_NETGREEN,
	
	// Power Control
//	SYSTEM_POWER_3V3,		// BUCK6 Enable Control
	SYSTEM_POWER_5V0,			// USB HOST Power
	SYSTEM_POWER_12V0,		// VLED Control (Backlight)
	
	GPIO_INDEX_END
};

static struct {
	int		gpio_index;	// Control Index
	int 	gpio;				// GPIO Number
	char	*name;			// GPIO Name == sysfs attr name (must)
	bool 	output;			// 1 = Output, 0 = Input
	int 	value;			// Default Value(only for output)
	int		pud;				// Pull up/down register setting : S3C_GPIO_PULL_DOWN, UP, NONE
} sControlGpios[] = {
	// High -> Reset Active
	{	WIFI_RESET,				EXYNOS4_GPK1(0), "wifi_reset",			1, 1, S3C_GPIO_PULL_UP	},
	// High -> REG Active
	{	WIFI_REG,				EXYNOS4_GPK1(1), "wifi_reg", 			1, 1, S3C_GPIO_PULL_UP	},
	// High -> Wakeup Active
	{	WIFI_WAKEUP,			EXYNOS4_GPK1(4), "wifi_wakeup", 		1, 1, S3C_GPIO_PULL_UP	},
	// Low -> High : Wifi wake up request to host
	{	WIFI_WAKEUP_HOST,		EXYNOS4_GPX0(7), "wifi_wakeup_host", 	0, 0, S3C_GPIO_PULL_UP	},

	// High -> Reset Active
	{	BLUETOOTH_RESET,		EXYNOS4_GPK1(2), "bt_reset", 			1, 1, S3C_GPIO_PULL_UP	},
	// High -> REG Active
	{	BLUETOOTH_REG,			EXYNOS4_GPK1(3), "bt_reg", 				1, 1, S3C_GPIO_PULL_UP	},
	// High -> Wakeup Active
	{	BLUETOOTH_WAKEUP,		EXYNOS4_GPK1(5), "bt_wakeup", 			1, 1, S3C_GPIO_PULL_UP	},
	// Low -> High : BT wake up request to host
	{	BLUETOOTH_WAKEUP_HOST,	EXYNOS4_GPX0(6), "bt_wakeup_host",		0, 0, S3C_GPIO_PULL_UP	},

	{	BLUETOOTH_BUTTON,			EXYNOS4_GPX3(5), "bt_button",		0, 0, S3C_GPIO_PULL_UP	},

	// High -> Power Enable
	{	MODEM_POWER,			EXYNOS4_GPX1(0), "modem_power", 		1, 1, S3C_GPIO_PULL_UP	},
	// Low -> Reset Active
	{	MODEM_RESET,			EXYNOS4_GPX1(1), "modem_reset", 		1, 0, S3C_GPIO_PULL_UP	},
	// High -> Disable1 Active
	{	MODEM_DISABLE1,			EXYNOS4_GPX1(2), "modem_disable1",		1, 0, S3C_GPIO_PULL_UP	},
	// High -> Disable2 Active
	{	MODEM_DISABLE2,			EXYNOS4_GPX1(3), "modem_disable2",		1, 0, S3C_GPIO_PULL_UP	},
	
	// STATUS LED : High -> LED ON
	{	STATUS_LED_RED,		EXYNOS4_GPC0(4), "led_red",			1, 0, S3C_GPIO_PULL_DOWN	},
	{	STATUS_LED_GREEN,			EXYNOS4_GPC0(3), "led_green",				1, 0, S3C_GPIO_PULL_DOWN	},

	{	STATUS_LED_BLUE,		EXYNOS4_GPC0(2), "led_blue",			1, 0, S3C_GPIO_PULL_DOWN	},

	{	STATUS_LED_NETRED,	EXYNOS4_GPC0(1), "led_netred",		1, 0, S3C_GPIO_PULL_DOWN	},
	{	STATUS_LED_NETGREEN,EXYNOS4_GPC1(0), "led_netgreen",	1, 0, S3C_GPIO_PULL_DOWN	},
	
	// SYSTEM POWER CONTROL
//	{	SYSTEM_POWER_3V3,		EXYNOS4_GPX3(5), "power_3v3",			1, 1, S3C_GPIO_PULL_DOWN	},
	{	SYSTEM_POWER_5V0,		EXYNOS4_GPC0(0), "power_5v0",			1, 1, S3C_GPIO_PULL_DOWN	},
	{	SYSTEM_POWER_12V0,	EXYNOS4_GPC0(3), "power_12v0",			1, 1, S3C_GPIO_PULL_DOWN	},

};


//   function prototype define
static 	int		fxi_sysfs_resume		(struct platform_device *dev);
static 	int		fxi_sysfs_suspend	(struct platform_device *dev, pm_message_t state);
static	int		fxi_sysfs_probe		(struct platform_device *pdev);
static	int		fxi_sysfs_remove		(struct platform_device *pdev);	

static 	int 	__init fxi_sysfs_init(void);
static 	void 	__exit fxi_sysfs_exit(void);

static struct platform_driver fxi_sysfs_driver = {
	.driver = {
		.name = "fxi-sysfs",
		.owner = THIS_MODULE,
	},
	.probe 		= fxi_sysfs_probe,
	.remove 	= fxi_sysfs_remove,
	.suspend	= fxi_sysfs_suspend,
	.resume		= fxi_sysfs_resume,
};


module_init(fxi_sysfs_init);
module_exit(fxi_sysfs_exit);

MODULE_DESCRIPTION("SYSFS driver for fxi-usb board");
MODULE_AUTHOR("ORG Hard-Kernel, Update By Seers");
MODULE_LICENSE("GPL");

extern	bool s5p_hpd_get_status(void);

//   sysfs function prototype define
static	ssize_t show_gpio	(struct device *dev, struct device_attribute *attr, char *buf);
static 	ssize_t set_gpio	(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static	ssize_t show_hdmi	(struct device *dev, struct device_attribute *attr, char *buf);

static	DEVICE_ATTR(wifi_reset, 		S_IRWXUGO, show_gpio, set_gpio);
static	DEVICE_ATTR(wifi_reg, 			S_IRWXUGO, show_gpio, set_gpio);
static	DEVICE_ATTR(wifi_wakeup, 		S_IRWXUGO, show_gpio, set_gpio);
static	DEVICE_ATTR(wifi_wakeup_host, 	S_IRWXUGO, show_gpio, set_gpio);

static	DEVICE_ATTR(bt_reset, 			S_IRWXUGO, show_gpio, set_gpio);
static	DEVICE_ATTR(bt_reg, 			S_IRWXUGO, show_gpio, set_gpio);
static	DEVICE_ATTR(bt_wakeup, 			S_IRWXUGO, show_gpio, set_gpio);
static	DEVICE_ATTR(bt_wakeup_host,		S_IRWXUGO, show_gpio, set_gpio);
static	DEVICE_ATTR(bt_button,		S_IRWXUGO, show_gpio, set_gpio);

static	DEVICE_ATTR(modem_power, 		S_IRWXUGO, show_gpio, set_gpio);
static	DEVICE_ATTR(modem_reset,		S_IRWXUGO, show_gpio, set_gpio);
static	DEVICE_ATTR(modem_disable1,		S_IRWXUGO, show_gpio, set_gpio);
static	DEVICE_ATTR(modem_disable2,		S_IRWXUGO, show_gpio, set_gpio);

static	DEVICE_ATTR(led_red, 			S_IRWXUGO, show_gpio, set_gpio);
static	DEVICE_ATTR(led_green,			S_IRWXUGO, show_gpio, set_gpio);
static	DEVICE_ATTR(led_blue,			S_IRWXUGO, show_gpio, set_gpio);
static	DEVICE_ATTR(led_netred,			S_IRWXUGO, show_gpio, set_gpio);
static	DEVICE_ATTR(led_netgreen,			S_IRWXUGO, show_gpio, set_gpio);

static	DEVICE_ATTR(power_5v0,			S_IRWXUGO, show_gpio, set_gpio);
static	DEVICE_ATTR(power_12v0,			S_IRWXUGO, show_gpio, set_gpio);

static	DEVICE_ATTR(hdmi_state,			S_IRWXUGO, show_hdmi, NULL);

static struct attribute *fxi_sysfs_entries[] = {
	&dev_attr_wifi_reset.attr,
	&dev_attr_wifi_reg.attr,
	&dev_attr_wifi_wakeup.attr,
	&dev_attr_wifi_wakeup_host.attr,

	&dev_attr_bt_reset.attr,
	&dev_attr_bt_reg.attr,
	&dev_attr_bt_wakeup.attr,
	&dev_attr_bt_wakeup_host.attr,
	&dev_attr_bt_button,

	&dev_attr_modem_power.attr,
	&dev_attr_modem_reset.attr,
	&dev_attr_modem_disable1.attr,
	&dev_attr_modem_disable2.attr,

	&dev_attr_led_red.attr,
	&dev_attr_led_green.attr,
	&dev_attr_led_blue.attr,
	&dev_attr_led_netred.attr,
	&dev_attr_led_netgreen.attr,

	&dev_attr_power_5v0.attr,
	&dev_attr_power_12v0.attr,
	&dev_attr_hdmi_state.attr,
	NULL
};

static struct attribute_group fxi_sysfs_attr_group = {
	.name   = NULL,
	.attrs  = fxi_sysfs_entries,
};

static 	ssize_t show_gpio(struct device *dev, struct device_attribute *attr, char *buf)
{
	int	i;

	for (i = 0; i < ARRAY_SIZE(sControlGpios); i++) {
		if(!strcmp(sControlGpios[i].name, attr->attr.name))
			return	sprintf(buf, "%d\n", (gpio_get_value(sControlGpios[i].gpio) ? 1 : 0));
	}
	
	return	sprintf(buf, "ERROR! : GPIO not found!\n");
}


static 	ssize_t set_gpio(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int	val, i;

    if(!(sscanf(buf, "%d\n", &val))) 	return	-EINVAL;

	for (i = 0; i < ARRAY_SIZE(sControlGpios); i++) {
		if(!strcmp(sControlGpios[i].name, attr->attr.name))	{
			if(sControlGpios[i].output) {
        printk (KERN_WARNING "Setting %x to %d\n", sControlGpios[i].gpio, ((val != 0) ? 1 : 0));
				gpio_set_value(sControlGpios[i].gpio, ((val != 0) ? 1 : 0));
      }
			else
				printk("This GPIO Configuration is INPUT!!\n");
      return count;
		}
	}

	printk("ERROR! : GPIO not found!\n");
    return 0;
}

static	ssize_t show_hdmi	(struct device *dev, struct device_attribute *attr, char *buf)
{
	int status = gpio_get_value(EXYNOS4_GPX3(7));    //s5p_hpd_get_status();
	
	if(status)	return	sprintf(buf, "%s\n", "on");
	else		return	sprintf(buf, "%s\n", "off");
}

void	STATUS_LED_CONTROL	(int led, int val)
{
	int	index;
	
	switch(led)	{
		case	0:	index = STATUS_LED_RED;		   break;
		case	1:	index = STATUS_LED_GREEN;	   break;
		case	2:	index = STATUS_LED_BLUE;	   break;
		case	3:	index = STATUS_LED_NETRED;   break;
		case	4:	index = STATUS_LED_NETGREEN; break;
		default	:								return;
	}
	
	gpio_set_value(sControlGpios[index].gpio, ((val != 0) ? 1 : 0));
}

EXPORT_SYMBOL(STATUS_LED_CONTROL);

void SYSTEM_POWER_CONTROL	(int power, int val)
{
	int	index;
	
	switch(power)	{
//		case	0:	index = SYSTEM_POWER_3V3;		break;
		case	1:	index = SYSTEM_POWER_5V0;		break;
		case	2:	index = SYSTEM_POWER_12V0;		break;
		default	:									return;
	}
	
	gpio_set_value(sControlGpios[index].gpio, ((val != 0) ? 1 : 0));
}

EXPORT_SYMBOL(SYSTEM_POWER_CONTROL);

static int fxi_sysfs_resume(struct platform_device *dev)
{
	#if	defined(DEBUG_PM_MSG)
		printk("%s\n", __FUNCTION__);
	#endif

    return  0;
}

static int fxi_sysfs_suspend(struct platform_device *dev, pm_message_t state)
{
	#if	defined(DEBUG_PM_MSG)
		printk("%s\n", __FUNCTION__);
	#endif
	
    return  0;
}

static	int	fxi_sysfs_probe(struct platform_device *pdev)	
{
	int	i;

	printk(KERN_INFO "FXI Sys FS Probe \n");

	// Control GPIO Init
	for (i = 0; i < ARRAY_SIZE(sControlGpios); i++) {
		if(gpio_request(sControlGpios[i].gpio, sControlGpios[i].name))	{
			printk("%s : %s gpio reqest err!\n", __FUNCTION__, sControlGpios[i].name);
		}
		else	{
			if(sControlGpios[i].output)		gpio_direction_output	(sControlGpios[i].gpio, sControlGpios[i].value);
			else							gpio_direction_input	(sControlGpios[i].gpio);

			s3c_gpio_setpull		(sControlGpios[i].gpio, sControlGpios[i].pud);
		}
	}

#if defined(SLEEP_DISABLE_FLAG)
	#ifdef CONFIG_HAS_WAKELOCK
		wake_lock(&sleep_wake_lock);
	#endif
#endif

	return	sysfs_create_group(&pdev->dev.kobj, &fxi_sysfs_attr_group);
}


static int fxi_sysfs_remove(struct platform_device *pdev)	
{
	int	i;
	
	for (i = 0; i < ARRAY_SIZE(sControlGpios); i++) 	gpio_free(sControlGpios[i].gpio);

#if defined(SLEEP_DISABLE_FLAG)
	#ifdef CONFIG_HAS_WAKELOCK
		wake_unlock(&sleep_wake_lock);
	#endif
#endif

    sysfs_remove_group(&pdev->dev.kobj, &fxi_sysfs_attr_group);
    
    return	0;
}

static int __init fxi_sysfs_init(void)
{

#if defined(SLEEP_DISABLE_FLAG)
	#ifdef CONFIG_HAS_WAKELOCK
		printk("\n%s(%d) : Sleep Disable Flag SET!!(Wake_lock_init)\n", __FUNCTION__, __LINE__);
	    wake_lock_init(&sleep_wake_lock, WAKE_LOCK_SUSPEND, "sleep_wake_lock");
	#endif
#endif

    return platform_driver_register(&fxi_sysfs_driver);
}

static void __exit fxi_sysfs_exit(void)
{
#if defined(SLEEP_DISABLE_FLAG)
	#ifdef CONFIG_HAS_WAKELOCK
	    wake_lock_destroy(&sleep_wake_lock);
	#endif
#endif
    platform_driver_unregister(&fxi_sysfs_driver);
}


