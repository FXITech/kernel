/*
 * HDMI CEC Event module
 *
 * Copyright (c) 2012 NDS
 *
 * Abhijeet Dev <abhijeet@abhijeet-dev.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundiation. either version 2 of the License,
 * or (at your option) any later version
 */

#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/module.h>

#include <drm/drm_edid.h>

#include "cec.h"
#include "hdmi_cec_event.h"
#include "hdmi_edid.h"
#include "cec_priv.h"

#define CEC_STATUS_TX_RUNNING		(1<<0)
#define CEC_STATUS_TX_TRANSFERRING	(1<<1)
#define CEC_STATUS_TX_DONE		(1<<2)
#define CEC_STATUS_TX_ERROR		(1<<3)
#define CEC_STATUS_TX_BYTES		(0xFF<<8)
#define CEC_STATUS_RX_RUNNING		(1<<16)
#define CEC_STATUS_RX_RECEIVING		(1<<17)
#define CEC_STATUS_RX_DONE		(1<<18)
#define CEC_STATUS_RX_ERROR		(1<<19)
#define CEC_STATUS_RX_BCAST		(1<<20)
#define CEC_STATUS_RX_BYTES		(0xFF<<24)

#define CEC_RX_BUFF_SIZE		16
#define CEC_TX_BUFF_SIZE		16

static u8 OSD_NAME[] = {
	'F', 'X', 'I', ' ', 'c', 's', 't', 'i', 'c', 'k'
};

typedef struct {
	struct work_struct hdmi_cec_event_work;
	u8 *buffer;
	u32 size;
} hdmi_cec_event_work_t;

static const u32 cec_key_table[] = {
	/*0x00 - 0x03 */ KEY_SELECT, KEY_UP, KEY_DOWN, KEY_LEFT,
	/*0x04 - 0x07 */ KEY_RIGHT, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x08 - 0x0b */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x0c - 0x0f */ KEY_RESERVED, KEY_EXIT, KEY_RESERVED, KEY_RESERVED,
	/*0x10 - 0x13 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x14 - 0x17 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x18 - 0x1b */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x1c - 0x1f */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x20 - 0x23 */ KEY_0, KEY_1, KEY_2, KEY_3,
	/*0x24 - 0x27 */ KEY_4, KEY_5, KEY_6, KEY_7,
	/*0x28 - 0x2b */ KEY_8, KEY_9, KEY_DOT, KEY_ENTER,
	/*0x2c - 0x2f */ KEY_CLEAR, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x30 - 0x33 */ KEY_CHANNELUP, KEY_CHANNELDOWN, KEY_PREVIOUS, KEY_SOUND,
	/*0x34 - 0x37 */ KEY_RESERVED, KEY_INFO, KEY_HELP, KEY_PAGEUP,
	/*0x38 - 0x3b */ KEY_PAGEDOWN, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x3c - 0x3f */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x40 - 0x43 */ KEY_POWER, KEY_VOLUMEUP, KEY_VOLUMEDOWN, KEY_MUTE,
	/*0x44 - 0x47 */ KEY_PLAY, KEY_STOP, KEY_PAUSE, KEY_RECORD,
	/*0x48 - 0x4b */ KEY_REWIND, KEY_FASTFORWARD, KEY_EJECTCD, KEY_FORWARD,
	/*0x4c - 0x4f */ KEY_BACK, KEY_STOP, KEY_PLAYPAUSE, KEY_RESERVED,
	/*0x50 - 0x53 */ KEY_ANGLE, KEY_RESERVED, KEY_RESERVED, KEY_EPG,
	/*0x54 - 0x57 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x58 - 0x5b */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x5c - 0x5f */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x60 - 0x63 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x64 - 0x67 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x68 - 0x6b */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x6c - 0x6f */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x70 - 0x73 */ KEY_RESERVED, KEY_BLUE, KEY_RED, KEY_GREEN,
	/*0x74 - 0x77 */ KEY_YELLOW, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x78 - 0x7b */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x7c - 0x7f */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x80 - 0x83 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x84 - 0x87 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x88 - 0x8b */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x8c - 0x8f */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x90 - 0x93 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x94 - 0x97 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x98 - 0x9b */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0x9c - 0x9f */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xa0 - 0xa3 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xa4 - 0xa7 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xa8 - 0xab */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xac - 0xaf */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xb0 - 0xb3 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xb4 - 0xb7 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xb8 - 0xbb */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xbc - 0xbf */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xc0 - 0xc3 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xc4 - 0xc7 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xc8 - 0xcb */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xcc - 0xcf */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xd0 - 0xd3 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xd4 - 0xd7 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xd8 - 0xdb */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xdc - 0xdf */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xe0 - 0xe3 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xe4 - 0xe7 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xe8 - 0xeb */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xec - 0xef */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xf0 - 0xf3 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xf4 - 0xf7 */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xf8 - 0xfb */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	/*0xfc - 0xff */ KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
};

static struct {
	enum CECDeviceType devtype;
	unsigned char laddr;
} laddresses[] = {
	{ CEC_DEVICE_RECODER, 1},
	{ CEC_DEVICE_RECODER, 2},
	{ CEC_DEVICE_TUNER, 3},
	{ CEC_DEVICE_PLAYER, 4},
	{ CEC_DEVICE_AUDIO, 5},
	{ CEC_DEVICE_TUNER, 6},
	{ CEC_DEVICE_TUNER, 7},
	{ CEC_DEVICE_PLAYER, 8},
	{ CEC_DEVICE_RECODER, 9},
	{ CEC_DEVICE_TUNER, 10},
	{ CEC_DEVICE_PLAYER, 11},
};

/* ...  */
unsigned short keymap[ARRAY_SIZE(cec_key_table)];
static atomic_t hdmi_on = ATOMIC_INIT(0);
static DEFINE_MUTEX(cec_lock);
static struct input_dev *hdmi_cec_event_dev;
static struct workqueue_struct *hdmi_cec_event_wq;

static u32 mPaddr;
static u32 mLaddr;
static struct edid *gEdidData;
static bool connected;
static u8 connect_attempts = 0;
static u32 lastKey;
/* ...  */
extern struct clk *hdmi_cec_clk;
/* ...  */
static void hdmi_cec_queue_connect (void);
static void connect(void);
static void hdmi_cec_event_wq_function(struct work_struct *work);
static void hdmi_cec_init_wq_function(struct work_struct *work);
static bool cec_alloc_laddr(void);
static int cec_send_msg(u8 * buffer, int size);

static void cec_broadcast_physical_address(void);
static void cec_broadcast_active_source(void);
static void cec_broadcast_cec_version(void);

void hdmi_cec_start(void)
{
	mutex_lock(&cec_lock);
	clk_enable(hdmi_cec_clk);

	printk(KERN_INFO "hdmi_cec_event: CEC event handler started\n");

	if (atomic_read(&hdmi_on))
		goto no_multi_open;
	else
		atomic_inc(&hdmi_on);

	s5p_cec_reset();
	s5p_cec_set_divider();
	s5p_cec_threshold();
	s5p_cec_unmask_tx_interrupts();
	s5p_cec_set_rx_state(STATE_RX);
	s5p_cec_unmask_rx_interrupts();
	s5p_cec_enable_rx();

	/* Initialise CEC */
	hdmi_cec_queue_connect();

no_multi_open:
	mutex_unlock(&cec_lock);
}

void hdmi_cec_stop(void)
{
	atomic_dec(&hdmi_on);

	s5p_cec_mask_tx_interrupts();
	s5p_cec_mask_rx_interrupts();

	clk_disable(hdmi_cec_clk);
	clk_put(hdmi_cec_clk);

	connected = false;
	connect_attempts = 0;
	if (gEdidData) {
		kfree(gEdidData);
		gEdidData = NULL;
	}
	mLaddr = CEC_LADDR_UNREGISTERED;
	mPaddr = CEC_NOT_VALID_PHYSICAL_ADDRESS;

	printk(KERN_INFO "hdmi_cec_event: CEC event handler stopped\n");
}

void hdmi_cec_event_rx()
{
	unsigned long spin_flags = 0;
	hdmi_cec_event_work_t *work = NULL;

	spin_lock_irqsave(&cec_rx_struct.lock, spin_flags);
	if (!connected) {
		hdmi_cec_queue_connect();
		goto irq_restore;
	}

	/* Post message with cec_rx_struct.buffer, cec_rx_struct.size */
	work = (hdmi_cec_event_work_t *)
		kmalloc(sizeof(hdmi_cec_event_work_t), GFP_KERNEL);
	if (work) {
		work->buffer = (u8 *) kmalloc(cec_rx_struct.size, GFP_KERNEL);
		memcpy(work->buffer, cec_rx_struct.buffer, cec_rx_struct.size);
		work->size = cec_rx_struct.size;
		INIT_WORK((struct work_struct *) work,
			  hdmi_cec_event_wq_function);
		queue_work(hdmi_cec_event_wq, (struct work_struct *) work);
	} else {
		printk(KERN_ERR "hdmi_cec_event: kmalloc of "
				"hdmi_cec_event_work_t failed\n");
	}

	s5p_cec_set_rx_state(STATE_RX);

irq_restore:
	spin_unlock_irqrestore(&cec_rx_struct.lock, spin_flags);
}

static int cec_send_msg(u8 *buffer, int size)
{
	if (size > CEC_TX_BUFF_SIZE || size == 0)
		return -1;
	s5p_cec_copy_packet(buffer, size);
	if (wait_event_interruptible(cec_tx_struct.waitq,
			atomic_read(&cec_tx_struct.state) != STATE_TX)) {
		printk(KERN_ERR
			"hdmi_cec_event: Error waiting on tx waitqueue\n");
		return -ERESTARTSYS;
	}

	if (atomic_read(&cec_tx_struct.state) == STATE_ERROR) {
		printk(KERN_ERR "hdmi_cec_event: Err reading tx state\n");
		return -1;
	}

	return size;
}

static void hdmi_cec_queue_connect ()
{
	struct work_struct *work = NULL;

	connect_attempts++;
	if (connect_attempts < 6) {
		printk(KERN_ERR "hdmi_cec_event: Trying to connect. Attempt #%d\n", connect_attempts);
		work = kmalloc(sizeof(struct work_struct), GFP_KERNEL);
		if (work) {
			INIT_WORK(work, hdmi_cec_init_wq_function);
			queue_work(hdmi_cec_event_wq, work);
		}
	}
}

static void hdmi_cec_init_wq_function(struct work_struct *work)
{
	kfree(work);

	if (!connected)
		connect();
}

static void hdmi_cec_event_wq_function(struct work_struct *work)
{
	u8 lsrc, ldst, opcode;
	hdmi_cec_event_work_t *w = (hdmi_cec_event_work_t *) work;

	if ((w->size == 0) || (w->size == 1))
		goto free_buffer;

	lsrc = w->buffer[0] >> 4;
	/* ignore messages with src address == mLaddr */
	if (lsrc == mLaddr) {
		goto free_buffer;
	}
	ldst = lsrc;

	opcode = w->buffer[1];
	switch (opcode) {
	case CEC_OPCODE_GIVE_PHYSICAL_ADDRESS:
		printk(KERN_DEBUG "hdmi_cec_event: GIVE_PHYSICAL_ADDRESS\n");
		cec_broadcast_physical_address();
		break;
	case CEC_OPCODE_REQUEST_ACTIVE_SOURCE:
		printk(KERN_DEBUG "hdmi_cec_event: REQUEST_ACTIVE_SOURCE\n");
		cec_broadcast_active_source();
		break;
	case CEC_OPCODE_GET_CEC_VERSION:
		printk(KERN_DEBUG "hdmi_cec_event: GET_CEC_VERSION\n");
		cec_broadcast_cec_version();
		break;
	case CEC_OPCODE_GIVE_DEVICE_POWER_STATUS:
		{
			u8 buf[3];

			printk(KERN_DEBUG
				"hdmi_cec_event: GIVE_DEVICE_POWER_STATUS\n");
			buf[0] = (mLaddr << 4) | ldst;
			buf[1] = CEC_OPCODE_REPORT_POWER_STATUS;
			buf[2] = CEC_POWER_STATUS_ON;
			if (cec_send_msg(buf, 3) != 3) {
				printk(KERN_ERR
					"hdmi_cec_event: Err GIVE_DEVICE_POWER_STATUS\n");
			}
		}
		break;
	case CEC_OPCODE_SET_STREAM_PATH:
		printk(KERN_DEBUG "hdmi_cec_event: SET_STREAM_PATH\n");
		cec_broadcast_active_source();
		break;
	case CEC_OPCODE_GIVE_OSD_NAME:
		{
			int sz = ARRAY_SIZE(OSD_NAME);
			u8 buf[2 + sz];

			buf[0] = (mLaddr << 4) | ldst;
			buf[1] = CEC_OPCODE_SET_OSD_NAME;

			printk(KERN_DEBUG "hdmi_cec_event: GIVE_OSD_NAME\n");
			memcpy(&buf[2], OSD_NAME, sz);

			if (cec_send_msg(buf, 2 + sz) != (2 + sz)) {
				printk(KERN_ERR
					"hdmi_cec_event: Err GIVE_OSD_NAME\n");
			}
		}
		break;
	case CEC_OPCODE_MENU_REQUEST:
		{
			u8 buf[3];

			printk(KERN_DEBUG "hdmi_cec_event: MENU_REQUEST\n");
			buf[0] = (mLaddr << 4) | ldst;
			buf[1] = CEC_OPCODE_MENU_STATUS;
			buf[2] = 0;	/*menu_state */
			if (cec_send_msg(buf, 3) != 3) {
				printk(KERN_ERR
					"hdmi_cec_event: Err GET_CEC_VERSION\n");
			}
		}
		break;
	case CEC_OPCODE_USER_CONTROL_PRESSED:
		{
			u32 currKey = cec_key_table[w->buffer[2]];

			printk(KERN_DEBUG
				"hdmi_cec_event: USER_CONTROL_PRESSED 0x%x\n",
				currKey);
			if ((currKey > 0) && (currKey != lastKey)) {
				lastKey = currKey;
				input_event(hdmi_cec_event_dev,
					EV_KEY, currKey, 1);
				input_sync(hdmi_cec_event_dev);
			}
		}
		break;
	case CEC_OPCODE_USER_CONTROL_RELEASED:
		{
			printk(KERN_DEBUG
				"hdmi_cec_event: USER_CONTROL_RELEASED\n");
			if (lastKey > 0) {
				input_event(hdmi_cec_event_dev,
					EV_KEY, lastKey, 0);
				input_sync(hdmi_cec_event_dev);
			}
			lastKey = 0;
		}
		break;
	case CEC_OPCODE_ABORT:
	case CEC_OPCODE_FEATURE_ABORT:
	default:
		{
			u8 buf[4];

			printk(KERN_ERR
				"hdmi_cec_event: Unimplemented opcode %x\n",
				opcode);
			buf[0] = (mLaddr << 4) | ldst;
			buf[1] = CEC_OPCODE_FEATURE_ABORT;
			buf[2] = CEC_OPCODE_ABORT;
			buf[3] = 0x04;	// "refused"
			if (cec_send_msg(buf, 4) != 4) {
				printk(KERN_ERR
					"hdmi_cec_event: Err CEC_OPCODE_FEATURE_ABORT\n");
			}
		}
		break;
	}

free_buffer:
	if (w->buffer)
		kfree(w->buffer);
	kfree((void *) w);
	return;
}

static void connect(void)
{
	int i;
	u8 *cea = NULL;

	gEdidData = hdmi_get_edid();
	if (gEdidData == NULL) {
		printk(KERN_ERR "hdmi_cec_event: NULL EDID data\n");
		return;
	}

	cea = hdmi_edid_find_cea_extension(gEdidData);
	mPaddr = 0;

	for (i = 0; i < EDID_LENGTH - 4; i++) {
		if ((cea[i] == 0x03) && (cea[i + 1] == 0x0C) &&
			(cea[i + 2] == 0x00)) {
			mPaddr = cea[i + 3] << 8;
			mPaddr |= cea[i + 4];
		}
	}

	if (cec_alloc_laddr()) {
		/* Initial broadcast for auto-detect */
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(100));
		cec_broadcast_physical_address();
		cec_broadcast_active_source();
		cec_broadcast_cec_version();

		connected = true;
	}
}

static bool cec_alloc_laddr(void)
{
	int i = 0;

	if (mPaddr == CEC_NOT_VALID_PHYSICAL_ADDRESS) {
		printk(KERN_ERR
			"hdmi_cec_event: Invalid physical address!\n");
		return false;
	}

	for (i = 0; i < ARRAY_SIZE(laddresses); i++) {
		if (laddresses[i].devtype == CEC_DEVICE_PLAYER) {
			u8 _laddr = laddresses[i].laddr;
			u8 message = ((_laddr << 4) | _laddr);
			if (cec_send_msg(&message, 1) != 1) {
				mLaddr = _laddr;
				break;
			}
		}
	}

	if (mLaddr == CEC_LADDR_UNREGISTERED) {
		printk(KERN_ERR
			"hdmi_cec_event: All logical addresses in use!\n");
		return false;
	}

	/* Set logical address on device */
	s5p_cec_set_addr(mLaddr);
	return true;
}

static void cec_broadcast_physical_address(void)
{
	u8 buffer[5];

	buffer[0] = (mLaddr << 4) | CEC_MSG_BROADCAST;
	buffer[1] = CEC_OPCODE_REPORT_PHYSICAL_ADDRESS;
	buffer[2] = (mPaddr >> 8) & 0xFF;
	buffer[3] = mPaddr & 0xFF;
	buffer[4] = CEC_DEVICE_PLAYER;
	if (cec_send_msg(buffer, 5) != 5) {
		printk(KERN_ERR
			"hdmi_cec_event: Err REPORT_PHYSICAL_ADDRESS\n");
	}
}

static void cec_broadcast_active_source(void)
{
	u8 buf[4];

	buf[0] = (mLaddr << 4) | CEC_MSG_BROADCAST;
	buf[1] = CEC_OPCODE_ACTIVE_SOURCE;
	buf[2] = (mPaddr >> 8) & 0xFF;
	buf[3] = mPaddr & 0xFF;
	if (cec_send_msg(buf, 4) != 4) {
		printk(KERN_ERR "hdmi_cec_event: Err ACTIVE_SOURCE\n");
	}
}

static void cec_broadcast_cec_version(void)
{
	u8 buf[3];

	buf[0] = (mLaddr << 4) | CEC_MSG_BROADCAST;
	buf[1] = CEC_OPCODE_CEC_VERSION;
	/* 0x04 = v1.3a */
	buf[2] = 0x04;
	if (cec_send_msg(buf, 3) != 3) {
		printk(KERN_ERR "hdmi_cec_event: Err GET_CEC_VERSION\n");
	}
}

static char banner[] __initdata = "HDMI CEC Event module, (c) 2012 NDS\n";

static int __init hdmi_cec_init(void)
{
	int i, ret = 0;

	printk(banner);

	hdmi_cec_event_dev = input_allocate_device();
	if (!hdmi_cec_event_dev) {
		printk(KERN_ERR "hdmi_cec_event: Not enough memory\n");
		ret = -ENOMEM;
		goto err_return;
	}

	hdmi_cec_event_dev->name = "HDMI_CEC_event";
	hdmi_cec_event_dev->phys = "CEC_EVENT";
	hdmi_cec_event_dev->evbit[0] = BIT_MASK(EV_KEY);
	memcpy(keymap, cec_key_table, sizeof(keymap));
	hdmi_cec_event_dev->keycode = keymap;
	hdmi_cec_event_dev->keycodesize = sizeof(unsigned short);
	hdmi_cec_event_dev->keycodemax = ARRAY_SIZE(keymap);
	__set_bit(EV_KEY, hdmi_cec_event_dev->evbit);
	for (i = 0; i < ARRAY_SIZE(cec_key_table); i++)
		__set_bit(cec_key_table[i], hdmi_cec_event_dev->keybit);
	__clear_bit(KEY_RESERVED, hdmi_cec_event_dev->keybit);

	ret = input_register_device(hdmi_cec_event_dev);
	if (ret) {
		printk(KERN_ERR
			"hdmi_cec_event: device register failed %d\n", ret);
		ret = -1;
		goto err_free_dev;
	}

	connected = false;
	gEdidData = NULL;
	mLaddr = CEC_LADDR_UNREGISTERED;
	mPaddr = CEC_NOT_VALID_PHYSICAL_ADDRESS;
	hdmi_cec_event_wq = create_singlethread_workqueue("hdmi_cec_event_queue");

err_free_dev:
	input_free_device(hdmi_cec_event_dev);

err_return:
	return ret;
}

static void __exit hdmi_cec_exit(void)
{
	input_unregister_device(hdmi_cec_event_dev);
	if (hdmi_cec_event_wq) {
		flush_workqueue(hdmi_cec_event_wq);
		destroy_workqueue(hdmi_cec_event_wq);
	}
}

module_init(hdmi_cec_init);
module_exit(hdmi_cec_exit);

MODULE_AUTHOR("Abhijeet Dev");
MODULE_DESCRIPTION("HDMI CEC Event module");
MODULE_LICENSE("GPL");
