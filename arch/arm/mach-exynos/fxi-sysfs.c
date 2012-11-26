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

#define DEBUG_PM_MSG
#define SLEEP_DISABLE_FLAG

#if defined(SLEEP_DISABLE_FLAG)
#ifdef CONFIG_HAS_WAKELOCK
#include <linux/wakelock.h>
static struct wake_lock sleep_wake_lock;
#endif
#endif

// GPIO Index Define
enum {
	// Bluetooth
	BLUETOOTH_BUTTON,

	// Power Control
	SYSTEM_POWER_5V0,     // USB HOST, HDMI Power

	GPIO_INDEX_END
};

static struct {
	int gpio_index; // Control Index
	int gpio;       // GPIO Number
	char *name;     // GPIO Name == sysfs attr name (must)
	bool output;    // 1 = Output, 0 = Input
	int value;      // Default Value(only for output)
	int pud;        // Pull up/down register setting : S3C_GPIO_PULL_DOWN, UP, NONE
} sControlGpios[] = {
	{ BLUETOOTH_BUTTON, EXYNOS4_GPX3(5), "bt_button", 0, 0, S3C_GPIO_PULL_UP},


	// SYSTEM POWER CONTROL
	{ SYSTEM_POWER_5V0, EXYNOS4_GPC0(0), "power_5v0", 1, 1, S3C_GPIO_PULL_DOWN},
};

static ssize_t show_gpio(struct device *dev, struct device_attribute *attr, char *buf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sControlGpios); i++) {
		if (!strcmp(sControlGpios[i].name, attr->attr.name))
			return sprintf(buf, "%d\n", (gpio_get_value(sControlGpios[i].gpio) ? 1 : 0));
	}

	return sprintf(buf, "ERROR! : GPIO not found!\n");
}


static ssize_t set_gpio(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int val, i;

	if (!(sscanf(buf, "%d\n", &val)))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(sControlGpios); i++) {
		if (!strcmp(sControlGpios[i].name, attr->attr.name)) {
			if (sControlGpios[i].output) {
				printk(KERN_WARNING "Setting %x to %d\n",
					sControlGpios[i].gpio, !!val);
				gpio_set_value(sControlGpios[i].gpio, !!val);
			} else
				printk(KERN_ERR "This GPIO Configuration is INPUT!!\n");
			return count;
		}
	}

	printk(KERN_ERR "ERROR! : GPIO not found!\n");
	return 0;
}

static ssize_t show_hdmi(struct device *dev, struct device_attribute *attr, char *buf)
{
	int status = gpio_get_value(EXYNOS4_GPX3(7));

	if (status)
		return sprintf(buf, "on\n");
	else
		return sprintf(buf, "off\n");
}

static DEVICE_ATTR(bt_button, S_IRUGO | S_IWUSR, show_gpio, set_gpio);

static DEVICE_ATTR(power_5v0, S_IRUGO | S_IWUSR, show_gpio, set_gpio);

static DEVICE_ATTR(hdmi_state, S_IRUGO | S_IWUSR, show_hdmi, NULL);

static struct attribute *fxi_sysfs_entries[] = {
	&dev_attr_bt_button.attr,

	&dev_attr_power_5v0.attr,
	&dev_attr_hdmi_state.attr,
	NULL
};

static struct attribute_group fxi_sysfs_attr_group = {
	.name   = NULL,
	.attrs  = fxi_sysfs_entries,
};

void SYSTEM_POWER_CONTROL(int power, int val)
{
	int	index;

	switch (power) {
		case 1:
			index = SYSTEM_POWER_5V0;
			break;
		default:
			return;
	}

	gpio_set_value(sControlGpios[index].gpio, !!val);
}
EXPORT_SYMBOL(SYSTEM_POWER_CONTROL);

static int fxi_sysfs_resume(struct platform_device *dev)
{
#if defined(DEBUG_PM_MSG)
	printk("%s\n", __FUNCTION__);
#endif

	return  0;
}

static int fxi_sysfs_suspend(struct platform_device *dev, pm_message_t state)
{
#if defined(DEBUG_PM_MSG)
	printk("%s\n", __FUNCTION__);
#endif

	return  0;
}

static int fxi_sysfs_probe(struct platform_device *pdev)
{
	int i;

	printk(KERN_INFO "FXI Sys FS Probe \n");

	// Control GPIO Init
	for (i = 0; i < ARRAY_SIZE(sControlGpios); i++) {
		if (gpio_request(sControlGpios[i].gpio, sControlGpios[i].name)) {
			printk("%s : %s gpio reqest err!\n", __FUNCTION__, sControlGpios[i].name);
		} else {
			if (sControlGpios[i].output)
				gpio_direction_output(sControlGpios[i].gpio, sControlGpios[i].value);
			else
				gpio_direction_input(sControlGpios[i].gpio);

			s3c_gpio_setpull(sControlGpios[i].gpio, sControlGpios[i].pud);
		}
	}

#if defined(SLEEP_DISABLE_FLAG)
#ifdef CONFIG_HAS_WAKELOCK
	wake_lock(&sleep_wake_lock);
#endif
#endif

	return sysfs_create_group(&pdev->dev.kobj, &fxi_sysfs_attr_group);
}


static int fxi_sysfs_remove(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sControlGpios); i++)
		gpio_free(sControlGpios[i].gpio);

#if defined(SLEEP_DISABLE_FLAG)
#ifdef CONFIG_HAS_WAKELOCK
	wake_unlock(&sleep_wake_lock);
#endif
#endif

	sysfs_remove_group(&pdev->dev.kobj, &fxi_sysfs_attr_group);

	return 0;
}

static struct platform_driver fxi_sysfs_driver = {
	.driver = {
		.name = "fxi-sysfs",
		.owner = THIS_MODULE,
	},
	.probe = fxi_sysfs_probe,
	.remove = fxi_sysfs_remove,
	.suspend = fxi_sysfs_suspend,
	.resume = fxi_sysfs_resume,
};

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

module_init(fxi_sysfs_init);
module_exit(fxi_sysfs_exit);

MODULE_DESCRIPTION("SYSFS driver for fxi-usb board");
MODULE_AUTHOR("ORG Hard-Kernel, Update By Seers");
MODULE_LICENSE("GPL");
