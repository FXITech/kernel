#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#include <mach/gpio.h>
#include <mach/regs-gpio.h>
#include <plat/gpio-cfg.h>
#include <asm/gpio.h>

#define PIN EXYNOS4_GPK3(1)
#define READ_MEMORY 0xF0
#define PAGESIZE 32
#define SLICE_SIZE 8
#define WRITE_SCRATCH 0x0F
#define COPY_SCRATCH 0x55
#define READ_SCRATCH 0xAA
#define READ_ROM 0x33
#define SKIP_ROM 0xCC


static 	int		fxi_fxiid_resume		(struct platform_device *dev);
static 	int		fxi_fxiid_suspend	(struct platform_device *dev, pm_message_t state);
static	int		fxi_fxiid_probe		(struct platform_device *pdev);
static	int		fxi_fxiid_remove		(struct platform_device *pdev);	


static ssize_t	get_romid(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t	get_uuid(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t	get_mac_wifi(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t	get_mac_bt(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t	get_revision(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t	get_eeprom(struct device *dev, struct device_attribute *attr, char *buf);

static int 	  	w1_rst_presence(void);
static int    	read_factory(void);
static void   	fxi_w1_write_bit(u32 pin, u8 bit);
static u8 		fxi_w1_read_bit(u32 pin);
static void 	fxi_w1_write_byte(u32 pin, u8 data);
static u8 		fxi_w1_read_byte(u32 pin);
static int 		write_8(u8 *buf, u8 off);
static ssize_t read_protect1(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t write_protect1(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static ssize_t 		write_page1(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static ssize_t 		read_page1(struct device *dev, struct device_attribute *attr, char *buf);

static 	int 	__init fxi_fxiid_init(void);
static 	void 	__exit fxi_fxiid_exit(void);


static	DEVICE_ATTR(mac_wifi,			S_IRUGO,		get_mac_wifi,	NULL);
static	DEVICE_ATTR(mac_bt,				S_IRUGO,		get_mac_bt,		NULL);
static	DEVICE_ATTR(uuid,				S_IRUGO,		get_uuid,		NULL);
static	DEVICE_ATTR(romid,				S_IRUGO,		get_romid,		NULL);
static	DEVICE_ATTR(revision,			S_IRUGO,		get_revision,	NULL);
static	DEVICE_ATTR(eeprom,				S_IRUGO,		get_eeprom,		NULL);
static  DEVICE_ATTR(page1,				S_IRWXUGO,		read_page1,	  write_page1);
static	DEVICE_ATTR(wp1,				S_IRWXUGO,		read_protect1,		write_protect1);

static u8 uuid[16];
static u8 mac_wifi[6];
static u8 mac_bt[6];
static u8 romid[8];
static u8 revision[4];
static u8 eeprom[128];
static u8 page1[32];

#if 0
/*
test content:
de:ad:c0:de:c0:fe:ba:be:ad:be:24:67:88:ba:ee:e6
00:21:29:A5:84:55
00:12:34:56:78:99 */

static u8 testcontent[128]={ 0x00, 0x01, 0x02, 0x03, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
							 0x00, 0x01, 0x02, 0x03, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
							 0x00, 0x01, 0x02, 0x03, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
							 0x00, 0x01, 0x02, 0x03, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
							 0x00, 0x01, 0x02, 0x03, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
							 0x00, 0x01, 0x02, 0x03, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
							 0x00, 0x01, 0x02, 0x03, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
							 0x00, 0x01, 0x02, 0x03, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
#endif

static struct platform_driver fxi_fxiid_driver = {
	.driver = {
		.name = "fxi-fxiid",
		.owner	 = THIS_MODULE,
	},
	.probe 		= fxi_fxiid_probe,
	.remove 	= fxi_fxiid_remove,
	.suspend	= fxi_fxiid_suspend,
	.resume		= fxi_fxiid_resume,
};

static struct attribute *fxi_fxiid_entries[] = {
	&dev_attr_mac_wifi.attr,
	&dev_attr_mac_bt.attr,
	&dev_attr_uuid.attr,
	&dev_attr_romid.attr,
	&dev_attr_revision.attr,
	&dev_attr_eeprom.attr,
	&dev_attr_page1.attr,
	&dev_attr_wp1.attr,

	NULL
};

static struct attribute_group fxi_fxiid_attr_group = {
	.name   = NULL,
	.attrs  = fxi_fxiid_entries,
};


static void fxi_w1_write_bit(u32 pin, u8 bit)
{

	/* Delays are from Application Note 126 Timing Calculation Worksheet. */

	if (bit)
	{
		/* Write '1' bit */
		gpio_direction_output(pin, 0);
		udelay(6); 
		gpio_direction_input(pin);
		udelay(64);
	} else {
		/* Write '0' bit */
		gpio_direction_output(pin, 0);
		udelay(60);
		gpio_direction_input(pin);
		udelay(10);
	}
}

static u8 fxi_w1_read_bit(u32 pin)
{
	u8 result;

	gpio_direction_output(pin, 0);
	udelay(6);
	gpio_direction_input(pin);
	udelay(9);
	result = gpio_get_value(pin) & 0x1;
	udelay(55);

	return result;
}

static void fxi_w1_write_byte(u32 pin, u8 data)
{
	int loop;
	for (loop = 0; loop < 8; loop++)
	{
		fxi_w1_write_bit(pin, data & 0x1);
		data >>= 1;
	}
}

static u8 fxi_w1_read_byte(u32 pin)
{
	int loop;
	u8 result;
	
	result = 0;
	for (loop = 0; loop < 8; loop++)
	{
		result >>= 1;
		if (fxi_w1_read_bit(pin))
			result |= 0x80;
	}

	return result;
}


static ssize_t read_protect1(struct device *dev, struct device_attribute *attr, char *buf){
	u8 temp[8];
	u8 i;

	if(w1_rst_presence()){
		printk(KERN_DEBUG "fxiid: rst failed in read_protect1");
		return -1;
	}

	fxi_w1_write_byte(PIN, READ_MEMORY);
	fxi_w1_write_byte(PIN, 0x80);
	fxi_w1_write_byte(PIN, 0x00);
	for (i = 0; i < 8; i++)
		temp[i] = fxi_w1_read_byte(PIN);
	temp[0] = 0x55;


	return sprintf(buf, "%02x\n", temp[0]);
}

static ssize_t write_protect1(struct device *dev, struct device_attribute *attr, const char *buf, size_t count){
	u8 temp[8];
	u8 scratch[8];
	u8 status;
	u8 i;
	u8 check_pre[2];
	u8 es;
	u8 ta1;
	u8 ta2;
	if(w1_rst_presence()){
		printk(KERN_DEBUG "fxiid: rst failed in write_protect1");
		return -1;
	}

	fxi_w1_write_byte(PIN, READ_MEMORY);
	fxi_w1_write_byte(PIN, 0x80);
	fxi_w1_write_byte(PIN, 0x00);
	for (i = 0; i < 8; i++)
		temp[i] = fxi_w1_read_byte(PIN);
	temp[0] = 0x55;

	for(i=0;i<8;i++)
		printk(KERN_DEBUG "temp = %x\n", temp[i]);

	if(w1_rst_presence()){
		printk(KERN_DEBUG "fxiid: write_block rst failed\n");
		return -1;
	}

	fxi_w1_write_byte(PIN, SKIP_ROM);
	
	fxi_w1_write_byte(PIN, WRITE_SCRATCH);
	fxi_w1_write_byte(PIN, 0x80);
	fxi_w1_write_byte(PIN, 0x00);

	for(i=0; i<8;i++){
		fxi_w1_write_byte(PIN, temp[i]);
		}

	check_pre[0] = fxi_w1_read_byte(PIN);
	check_pre[1] = fxi_w1_read_byte(PIN);

	printk(KERN_DEBUG "fxiid: pre: crc bytes: %x and %x\n", check_pre[0], check_pre[1]);
	

	if(w1_rst_presence()){
		printk(KERN_DEBUG "fxiid: write_block rst failed 2\n");
		return -1;
	}


	fxi_w1_write_byte(PIN, SKIP_ROM);
	fxi_w1_write_byte(PIN, READ_SCRATCH);

	ta1 = fxi_w1_read_byte(PIN);
	ta2 = fxi_w1_read_byte(PIN);
	es = fxi_w1_read_byte(PIN);
	printk(KERN_DEBUG "fxiid: ta1 : %x ta2 : %x es : %x", ta1, ta2, es);

	for(i=0; i<10;i++){
		scratch[i] = fxi_w1_read_byte(PIN);
	}
	
	if(w1_rst_presence()){
		printk(KERN_DEBUG "fxiid: write_block rst failed 3\n");
		return -1;
	}


	fxi_w1_write_byte(PIN, SKIP_ROM);
	fxi_w1_write_byte(PIN, COPY_SCRATCH); 
	fxi_w1_write_byte(PIN, ta1); 
	fxi_w1_write_byte(PIN, ta2); 
	fxi_w1_write_byte(PIN, es);

	msleep(11);

	status = fxi_w1_read_byte(PIN);
	printk(KERN_DEBUG "fxiid: Status %x", status);
	if(status!= 0xAA){
		printk(KERN_DEBUG "fxiid: write_protect1 failed (success != 0xAA)");
		return -1;
		}

	if(w1_rst_presence()){
		printk(KERN_DEBUG "fxiid: write_block rst failed 4\n");
		return -1;
	}
	return 0;
}


static int w1_rst_presence()
{
	u8 firstbyte;

	udelay(0);
	gpio_direction_output(PIN, 0);
	udelay(480);
	gpio_direction_input(PIN);
	udelay(70);
	firstbyte = gpio_get_value(PIN) & 0x1;
	udelay(410);

	if (firstbyte){
		printk(KERN_DEBUG "fxiid: reset-presence failed");
		return -1;
	}

	return 0;
}

static ssize_t read_page1(struct device *dev, struct device_attribute *attr, char *buf){

	memcpy(buf, page1, 32);
	return 32;
}

static ssize_t write_page1(struct device *dev, struct device_attribute *attr, const char *buf, size_t count){
	unsigned int i;
	u8 alignment=0;
	char temp[32];
	int retries;

	printk(KERN_DEBUG "Writing page\n");
	retries =0;
	memcpy(temp, buf, 32);
	for(i=0; i<4;i++){
retry:
		printk(KERN_DEBUG "fxiid: alignment: %x", alignment);
		if(write_8(&temp[alignment], alignment)){
			if(retries<10){
				msleep(1);
				retries++;
				goto retry;
			}
			printk("Unable to write bytes.");
			return -1;
		}
		alignment+=8;
	}
	read_factory(); // Re-read to refresh globals.
	return count;
}

#if 0
static int write_testcontent(u8 *buf){
	int i;
	u8 alignment=0;
	char temp[128];


	memcpy(temp, buf, 128);

	printk(KERN_DEBUG "Writing testcontent...");

	for(i=0; i<16; i++){
		if(write_8(&temp[alignment], alignment)){
			printk(KERN_DEBUG "Unable to write bytes.");
			return -1;
		}
		alignment+=8;
	printk(KERN_DEBUG "Wrote 8 bytes starting at testcontent[%d]", alignment);
	}
	printk(KERN_DEBUG "Wrote testcontent...");
	return 0;
}
#endif

/*
 Write a block of 8 bytes. 

 off = start offset 0x00<off>
*/


static int write_8(u8 *buf, u8 off)
{
	int i;
	u8 scratch[10];
	u8 status;
	u8 check_pre[2];
	u8 es;
	u8 ta1;
	u8 ta2;
	int tries;
	tries = 1;
retry:

	if(w1_rst_presence()){
		printk(KERN_DEBUG "fxiid: write_block rst failed\n");
		return -1;
	}

	fxi_w1_write_byte(PIN, SKIP_ROM);
	
	fxi_w1_write_byte(PIN, WRITE_SCRATCH);
	fxi_w1_write_byte(PIN, off);
	fxi_w1_write_byte(PIN, 0x00);

	for(i=0; i<8;i++){
		fxi_w1_write_byte(PIN, buf[i]);
		}

	check_pre[0] = fxi_w1_read_byte(PIN);
	check_pre[1] = fxi_w1_read_byte(PIN);

	printk(KERN_DEBUG "fxiid: pre: crc bytes: %x and %x\n", check_pre[0], check_pre[1]);
	

	if(w1_rst_presence()){
		printk(KERN_DEBUG "fxiid: write_block rst failed 2\n");
		return -1;
	}


	fxi_w1_write_byte(PIN, SKIP_ROM);
	fxi_w1_write_byte(PIN, READ_SCRATCH);

	ta1 = fxi_w1_read_byte(PIN);
	ta2 = fxi_w1_read_byte(PIN);
	es = fxi_w1_read_byte(PIN);
	printk(KERN_DEBUG "fxiid: ta1 : %x ta2 : %x es : %x", ta1, ta2, es);

	for(i=0; i<10;i++){
		scratch[i] = fxi_w1_read_byte(PIN);
	}
	
	if(w1_rst_presence()){
		printk(KERN_DEBUG "fxiid: write_block rst failed 3\n");
		return -1;
	}


	fxi_w1_write_byte(PIN, SKIP_ROM);
	fxi_w1_write_byte(PIN, COPY_SCRATCH); 
	fxi_w1_write_byte(PIN, ta1); 
	fxi_w1_write_byte(PIN, ta2); 
	fxi_w1_write_byte(PIN, es);

	msleep(11);

	status = fxi_w1_read_byte(PIN);
	printk(KERN_DEBUG "fxiid: Status %x", status);
	if(status!= 0xAA){
		if(tries < 20){
			tries++;
			msleep(100);
			goto retry;
		}
		printk(KERN_DEBUG "fxiid: Copy scratchpad failed.\n");
		return -1;
	}

	if(w1_rst_presence()){
		printk(KERN_DEBUG "fxiid: write_block rst failed 4\n");
		return -1;
	}

printk(KERN_DEBUG "fxiid:Successfully wrote block!\n");
return w1_rst_presence();
}


static int read_factory()
{

		u8 i;

		printk(KERN_DEBUG "fxiid: in read_factory");



	if(w1_rst_presence()){
		printk(KERN_DEBUG "fxiid: reset-presence in EEPROM failed. Setting mem to 0xFF");
		memset(eeprom, 0xFF, 32);
		goto exit_eeread;
	}

	fxi_w1_write_byte(PIN, READ_ROM);

	for (i = 0; i < 8; i++){
		romid[i] = fxi_w1_read_byte(PIN);
	}


	fxi_w1_write_byte(PIN, READ_MEMORY);
	fxi_w1_write_byte(PIN, 0x00);
	fxi_w1_write_byte(PIN, 0x00);
	for (i = 0; i < 128; i++)
		eeprom[i] = fxi_w1_read_byte(PIN);



exit_eeread:

	memcpy(page1, eeprom, 32);
	memcpy(uuid, page1, 16);
	memcpy(mac_wifi, page1+16, 6);
	memcpy(mac_bt, page1+22, 6);
	memcpy(revision, page1+28, 4);
#if 1
/* Temporary, this code should be removed after moving from gingerbread */
int eeprom_empty = 1;
for (i=0; i < 6; i++) {
	if (mac_wifi[i] != 0) {
		eeprom_empty = 0;
		break;
	}
}

if (eeprom_empty) {
	mac_wifi[0] = 0x00;
	mac_wifi[1] = 0x11;
	mac_wifi[2] = 0x22;
	mac_wifi[3] = 0x33;
	mac_wifi[4] = 0x44;
	mac_wifi[5] = 0x55;
}

/* Temporary, this code should be removed after moving from gingerbread */
eeprom_empty = 1;
for (i=0; i < 6; i++) {
	if (mac_bt[i] != 0) {
		eeprom_empty = 0;
		break;
	}
}

if (eeprom_empty) {
	mac_bt[0] = 0x55;
	mac_bt[1] = 0x44;
	mac_bt[2] = 0x33;
	mac_bt[3] = 0x22;
	mac_bt[4] = 0x11;
	mac_bt[5] = 0x00;
}
#endif
	return 0;
}


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

static ssize_t get_romid(struct device *dev, struct device_attribute *attr, char *buf)
{

		return sprintf(buf, "%02x%02x%02x%02x%02x%02x%02x\n", 
		romid[0], romid[1], romid[2], romid[3], romid[4], romid[5], romid[6]); // last byte is the crc byte. Dont show this.
}

static ssize_t get_revision(struct device *dev, struct device_attribute *attr, char *buf)
{

		return sprintf(buf, "Type: %02x Version: %02x Major: %02x Minor: %02x\n", 
		revision[0], revision[1], revision[2], revision[3]); 
}

static ssize_t get_eeprom(struct device *dev, struct device_attribute *attr, char *buf)
{

		memcpy(buf, eeprom, 128);
		return(128);
}

static ssize_t get_uuid(struct device *dev, struct device_attribute *attr, char *buf)
{

		return sprintf(buf, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n", 
		uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
		uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
}

static ssize_t get_mac_wifi(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x\n",
		mac_wifi[0], mac_wifi[1], mac_wifi[2], mac_wifi[3], mac_wifi[4], mac_wifi[5]);
}
static ssize_t get_mac_bt(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x\n",
		mac_bt[0], mac_bt[1], mac_bt[2], mac_bt[3], mac_bt[4], mac_bt[5]);
}

static int fxi_fxiid_resume(struct platform_device *dev)
{
    return  0;
}

static int fxi_fxiid_suspend(struct platform_device *dev, pm_message_t state)
{
	
    return  0;
}

static	int	fxi_fxiid_probe(struct platform_device *pdev)	
{

	return	sysfs_create_group(&pdev->dev.kobj, &fxi_fxiid_attr_group);
}


static int fxi_fxiid_remove(struct platform_device *pdev)	
{

    sysfs_remove_group(&pdev->dev.kobj, &fxi_fxiid_attr_group);
    
    return	0;
}

static int __init fxi_fxiid_init(void)
{	
	int err;
		gpio_request(PIN, "fxiid");
	s3c_gpio_setpull(PIN, S3C_GPIO_PULL_UP);

	//write_testcontent(&testcontent[0]);
	read_factory();
    err = platform_driver_register(&fxi_fxiid_driver);
    return err;
}

static void __exit fxi_fxiid_exit(void)
{

    platform_driver_unregister(&fxi_fxiid_driver);
}

module_init(fxi_fxiid_init);
module_exit(fxi_fxiid_exit);

EXPORT_SYMBOL_GPL(export_fxi_wifi_mac);
EXPORT_SYMBOL_GPL(export_fxi_bt_mac);

/* Maintainer - poul.jensen@fxitech.com */

MODULE_DESCRIPTION("FXI EEPROM driver");
MODULE_AUTHOR("FXI Technologies AS");
MODULE_LICENSE("GPL");