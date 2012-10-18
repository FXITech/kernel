#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/sysfs.h>

#include "fxi-fxiid.h"

#include <w1.h>
#include <slaves/w1_ds2431.h>

#define MAX_EEPROM_SCAN_ATTEMPTS 20
#define EEPROM_FAMILY_ID 0x2D

static u8 uuid[16];
static u8 mac_wifi[6];
static u8 mac_bt[6];
static u8 revision[4];


static int export_fxi_wifi_mac(u8 *buf)
{
	 memcpy(buf, mac_wifi, 6);
	 return 0;
}

static int export_fxi_bt_mac(u8 *buf)
{
	memcpy(buf, mac_wifi, 6);
	return 0;
}

static ssize_t get_revision(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Type: %02x Version: %02x Major: %02x Minor: %02x\n",
		       revision[0], revision[1], revision[2], revision[3]);
}

static ssize_t get_uuid(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf,
		       "%02x%02x%02x%02x%02x%02x%02x%02x"
		       "%02x%02x%02x%02x%02x%02x%02x%02x\n",
		       uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5],
		       uuid[6], uuid[7], uuid[8], uuid[9], uuid[10], uuid[11],
		       uuid[12], uuid[13], uuid[14], uuid[15]);
}

static ssize_t get_mac_wifi(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x\n",
		       mac_wifi[0], mac_wifi[1], mac_wifi[2],
		       mac_wifi[3], mac_wifi[4], mac_wifi[5]);
}
static ssize_t get_mac_bt(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x\n",
		       mac_bt[0], mac_bt[1], mac_bt[2],
		       mac_bt[3], mac_bt[4], mac_bt[5]);
}

static DEVICE_ATTR(mac_wifi, S_IRUGO, get_mac_wifi, NULL);
static DEVICE_ATTR(mac_bt, S_IRUGO, get_mac_bt, NULL);
static DEVICE_ATTR(uuid, S_IRUGO, get_uuid, NULL);
static DEVICE_ATTR(revision, S_IRUGO, get_revision, NULL);

static struct attribute *fxi_fxiid_entries[] = {
	&dev_attr_mac_wifi.attr,
	&dev_attr_mac_bt.attr,
	&dev_attr_uuid.attr,
	&dev_attr_revision.attr,

	NULL
};

static struct attribute_group fxi_fxiid_attr_group = {
	.name = NULL,
	.attrs = fxi_fxiid_entries,
};

static void read_eeprom(struct w1_slave *slave)
{
	u8 page1[32];
	if (w1_f2d_read(slave, 0, 32, page1) < 32) {
		printk(KERN_ERR "fxiid: Failed to read eeprom\n");
		return;
	}
	memcpy(uuid, page1, 16);
	memcpy(mac_wifi, page1+16, 6);
	memcpy(mac_bt, page1+22, 6);
	memcpy(revision, page1+28, 4);
}

static int fxi_fxiid_probe(struct platform_device *pdev)
{
	struct w1_master *master = w1_search_master_id(1);
	struct w1_slave *slave;
	int eeprom_found = 0;
	int attempts =  MAX_EEPROM_SCAN_ATTEMPTS;

	if (!master) {
		printk(KERN_DEBUG "fxiid: Unable to find 1w master\n");
		return -EPROBE_DEFER;
	}

	do {
		struct w1_slave *sl;
		mutex_lock(&master->mutex);
		list_for_each_entry(sl, &master->slist, w1_slave_entry) {
			if (sl->reg_num.family == EEPROM_FAMILY_ID) {
				eeprom_found = 1;
				slave = sl;
				printk(KERN_DEBUG "fxiid: eeprom found\n");
			}
		}
		mutex_unlock(&master->mutex);
		if (!eeprom_found) {
			printk(KERN_DEBUG "fxiid: Did not find eeprom yet\n");
			msleep(50);
		}
	} while (!eeprom_found && attempts-- > 0);

	if (!eeprom_found) {
		printk(KERN_ERR "fxiid: Unable to find eeprom\n");
		return -ENODEV;
	}
	read_eeprom(slave);

	return sysfs_create_group(&pdev->dev.kobj, &fxi_fxiid_attr_group);
}

static int fxi_fxiid_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &fxi_fxiid_attr_group);
	return 0;
}


static struct platform_driver fxi_fxiid_driver = {
	.driver = {
		.name = "fxi-fxiid",
		.owner = THIS_MODULE,
	},
	.probe = fxi_fxiid_probe,
	.remove = fxi_fxiid_remove,
};

static int __init fxi_fxiid_init(void)
{
	return platform_driver_register(&fxi_fxiid_driver);
}

static void __exit fxi_fxiid_exit(void)
{
	platform_driver_unregister(&fxi_fxiid_driver);
}

#define FXI_MANUFACTURER "FXI Technologies AS"
#define FXI_PRODUCT "Cotton Candy"

void fxi_get_product_info(struct fxi_product_info *info)
{
	sprintf(info->iSerial,
		"%02x%02x%02x%02x%02x%02x%02x%02x"
		"%02x%02x%02x%02x%02x%02x%02x%02x"
		", 0x%02x%02x%02x%02x\n",
		uuid[0], uuid[1], uuid[2], uuid[3],
		uuid[4], uuid[5], uuid[6], uuid[7],
		uuid[8], uuid[9], uuid[10], uuid[11],
		uuid[12], uuid[13], uuid[14], uuid[15],
		revision[0], revision[1], revision[2], revision[3]);

	info->idVendor = FXI_VENDOR_ID;
	info->idProduct = *((u16*) &revision[0]);
	strcpy(info->iManufacturer, FXI_MANUFACTURER);
	strcpy(info->iProduct, FXI_PRODUCT);
}

EXPORT_SYMBOL(fxi_get_product_info);

module_init(fxi_fxiid_init);
module_exit(fxi_fxiid_exit);

EXPORT_SYMBOL_GPL(export_fxi_wifi_mac);
EXPORT_SYMBOL_GPL(export_fxi_bt_mac);

/* Maintainer - poul.jensen@fxitech.com */

MODULE_DESCRIPTION("FXI EEPROM driver");
MODULE_AUTHOR("FXI Technologies AS");
MODULE_LICENSE("GPL");
