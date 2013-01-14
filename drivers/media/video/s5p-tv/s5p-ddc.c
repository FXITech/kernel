/* linux/drivers/media/video/samsung/tvout/hw_if/hdcp.c
 *
 * Copyright (c) 2009 Samsung Electronics
 *		http://www.samsung.com/
 *
 * DDC function for Samsung TVOUT driver
 *
 * This program is free software. you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/module.h>

#define AN_SZ			8
#define AKSV_SZ			5
#define BKSV_SZ			5
#define MAX_KEY_SZ		16

#define BKSV_RETRY_CNT		14
#define BKSV_DELAY		100

#define DDC_RETRY_CNT		400000
#define DDC_DELAY		25

#define KEY_LOAD_RETRY_CNT	1000
#define ENCRYPT_CHECK_CNT	10

#define KSV_FIFO_RETRY_CNT	50
#define KSV_FIFO_CHK_DELAY	100 /* ms */
#define KSV_LIST_RETRY_CNT	10000
#define SHA_1_RETRY_CNT		4

#define BCAPS_SIZE		1
#define BSTATUS_SIZE		2
#define SHA_1_HASH_SIZE		20

#define KSV_FIFO_READY			(0x1 << 5)

#define MAX_CASCADE_EXCEEDED_ERROR	(-2)
#define MAX_DEVS_EXCEEDED_ERROR		(-3)
#define REPEATER_ILLEGAL_DEVICE_ERROR	(-4)
#define REPEATER_TIMEOUT_ERROR		(-5)

#define MAX_CASCADE_EXCEEDED		(0x1 << 3)
#define MAX_DEVS_EXCEEDED		(0x1 << 7)


#define DDC_BUF_SIZE		32

struct i2c_client *ddc_port;

static bool sw_reset;
extern bool s5p_hdmi_ctrl_status(void);

/* start: external functions for HDMI */
extern void __iomem *hdmi_base;


/* end: external functions for HDMI */

/* ddc i2c */
static int s5p_ddc_read(u8 reg, int bytes, u8 *dest)
{
	struct i2c_client *i2c = ddc_port;
	u8 addr = reg;
	int ret, cnt = 0;

	struct i2c_msg msg[] = {
		[0] = {
			.addr = i2c->addr,
			.flags = 0,
			.len = 1,
			.buf = &addr
		},
		[1] = {
			.addr = i2c->addr,
			.flags = I2C_M_RD,
			.len = bytes,
			.buf = dest
		}
	};

	do {
		ret = i2c_transfer(i2c->adapter, msg, 2);

		if (ret < 0 || ret != 2)
			printk (KERN_DEBUG "ddc : can't read data, retry %d\n", cnt);
		else
			break;

		msleep(DDC_DELAY);
		cnt++;
	} while (cnt < DDC_RETRY_CNT);

	if (cnt == DDC_RETRY_CNT)
		goto ddc_read_err;

	printk(KERN_DEBUG "ddc : read data ok\n");

	return 0;
ddc_read_err:
	printk(KERN_ERR "ddc : can't read data, timeout\n");
	return -1;
}

static int s5p_ddc_write(u8 reg, int bytes, u8 *src)
{
	struct i2c_client *i2c = ddc_port;
	u8 msg[bytes + 1];
	int ret, cnt = 0;

	msg[0] = reg;
	memcpy(&msg[1], src, bytes);

	do {
		ret = i2c_master_send(i2c, msg, bytes + 1);

		if (ret < 0 || ret < bytes + 1)
			printk(KERN_DEBUG "ddc : can't write data, retry %d\n", cnt);
		else
			break;

		msleep(DDC_DELAY);
		cnt++;
	} while (cnt < DDC_RETRY_CNT);

	if (cnt == DDC_RETRY_CNT)
		goto ddc_write_err;

	printk(KERN_DEBUG "ddc : write data ok\n");
	return 0;
ddc_write_err:
	printk(KERN_ERR "ddc : can't write data, timeout\n");
	return -1;
}

static int __devinit s5p_ddc_probe(struct i2c_client *client,
			const struct i2c_device_id *dev_id)
{
	int ret = 0;

	ddc_port = client;

	dev_info(&client->adapter->dev, "attached s5p_ddc "
		"into i2c adapter successfully\n");

	return ret;
}

static int s5p_ddc_remove(struct i2c_client *client)
{
	dev_info(&client->adapter->dev, "detached s5p_ddc "
		"from i2c adapter successfully\n");

	return 0;
}

static int s5p_ddc_suspend(struct i2c_client *cl, pm_message_t mesg)
{
	return 0;
};

static int s5p_ddc_resume(struct i2c_client *cl)
{
	return 0;
};

static struct i2c_device_id ddc_idtable[] = {
	{"s5p_ddc", 0},
};

MODULE_DEVICE_TABLE(i2c, ddc_idtable);

static struct i2c_driver ddc_driver = {
	.driver = {
		.name = "s5p_ddc",
		.owner = THIS_MODULE,
	},
	.id_table	= ddc_idtable,
	.probe		= s5p_ddc_probe,
	.remove		= __devexit_p(s5p_ddc_remove),
	.suspend	= s5p_ddc_suspend,
	.resume		= s5p_ddc_resume,
};

static int __init s5p_ddc_init(void)
{
	return i2c_add_driver(&ddc_driver);
}

static void __exit s5p_ddc_exit(void)
{
	i2c_del_driver(&ddc_driver);
}


module_init(s5p_ddc_init);
module_exit(s5p_ddc_exit);

