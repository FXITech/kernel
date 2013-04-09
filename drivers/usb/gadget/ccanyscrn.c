#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/signal.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/completion.h>
#include "ccanyscrn.h"

#define DEVNAME "ccanyscrn"
#define MAX_REQUESTS 128
#define MAX_BUF_SIZE 4096

enum packet_types {
	NODATA = 0,
	DATAPACKET,
	MARK,
	INIT,
	NACK,
	ACK = 0x80
};

enum message_part {
	HEADER = 0,
	DATA
};

/* represents one request in the request queue */
struct request {
	unsigned long type;
	unsigned long addr;
	signed long len;
	void *buf;
	void *write_buf;
};

struct anyscreen {
	struct device *dev;
	struct miscdevice miscdev;
	struct mutex lock;
	int disable_async_notification;
	enum message_part message_part;
	struct fasync_struct *async_queue;

};

/* needed to access our private driver instance from the
   exported fxi_request function */
static struct anyscreen *anyscreen_global;

static atomic_t anyscreen_available = ATOMIC_INIT(1);

/* request queue (ringbuffer) */
static struct request request_queue[MAX_REQUESTS];
static int first_req;
static int last_req;

/* current request */
static struct request *current_request;

/* current batch being served to USB host */
static unsigned char batch[CC_USB_MAX_BLOCKS][CC_USB_BLOCK_SIZE];
static int batch_size;
static int batch_valid; /* batch is valid */
static int batch_block_pointer; /* current batch block pointer */

/* various flags */
static int preload; /* flag: preload mode active */
static int imp_ack; /* flag: ACK is done in driver (not user space) */
static volatile int daemon_up = false; /* flags: user space daemon has started */
static struct completion daemon_running;
static struct completion ready_for_new_requests;


/* other configuration vars */
static int in_block; /* address of inblock */
static int out_block1; /* address of outblock 1 */
static int out_block2; /* address of outblock 2 */
static int cur_out_block; /* set to either outBlock1 or outBlock2 */

/* queue management */
static int queue_size(struct anyscreen *p)
{
	if (last_req < 0)
		return 0;
	else if (last_req == first_req)
		return MAX_REQUESTS;
	else if (last_req > first_req)
		return last_req - first_req;
	else
		return last_req + (MAX_REQUESTS - first_req);
}

static struct request *queue_insert(struct anyscreen *p)
{
	struct request *req;

	if (queue_size(p) >= MAX_REQUESTS)
		return NULL;

	if (last_req < 0)
		last_req = first_req;

	dev_dbg(p->dev, "insert %d (%d)\n", last_req, queue_size(p) + 1);
	req = &request_queue[last_req];
	last_req = (last_req + 1) % MAX_REQUESTS;
	return req;
}

void queue_activate(struct anyscreen *p)
{
	if (!p->disable_async_notification) {
		dev_dbg(p->dev, "send SIGIO, start poll\n");
		p->disable_async_notification = true;
		kill_fasync(&p->async_queue, SIGIO, POLL_IN);
	}
}

static struct request *queue_front(struct anyscreen *p)
{
	struct request *req;
	dev_dbg(p->dev, "getting %d\n", first_req);
	req = &request_queue[first_req];
	return req;
}

static void queue_remove(struct anyscreen *p)
{
	mutex_lock(&p->lock);
	dev_dbg(p->dev, "remove %d\n", queue_size(p) - 1);
	first_req = (first_req + 1) % MAX_REQUESTS;
	if (first_req == last_req)
		last_req = -1; /* queue empty */
	mutex_unlock(&p->lock);
}

static inline void *next_block (int blocks)
{
	void *ptr = batch[batch_block_pointer % batch_size];
	batch_block_pointer += blocks;
	return ptr;
}

static struct request* get_request_wait(struct anyscreen *p,
					struct request *req)
{
	for (;;) {
		mutex_lock(&p->lock);
		req = queue_insert(p);
		mutex_unlock(&p->lock);
		if (req)
			return req;
		wait_for_completion(&ready_for_new_requests);
		INIT_COMPLETION(ready_for_new_requests);
	}
}

/* called by gadget driver whenever there is a request (read of write)
   from the host */
void fxi_request (unsigned long addr, void *buf, unsigned long type, int size)
{
	struct anyscreen *priv = anyscreen_global;
	dev_dbg(priv->dev, "fxirequest %ld %ld %d (queue: %d) - %p\n",
		type, addr, size, queue_size(priv), buf);

	/* wait if fusionx daemon is not up yet */
	if (!daemon_up) {
		wait_for_completion(&daemon_running);
		INIT_COMPLETION(daemon_running);
		dev_dbg(priv->dev, "daemon connected\n");
	}

	if (type == CC_REQ_WRITE) {
		while (size) {
			struct request *req;
			int bytes = size;
			unsigned char *block = buf;

			if (bytes > MAX_BUF_SIZE)
				bytes = MAX_BUF_SIZE;

			req = get_request_wait(priv, req);

			/* ACK, get new batch */
			if ((addr >= in_block) && (block[0] & ACK))
				batch_valid = false;

			/* NACK, get a previous batch */
			if ((addr >= in_block) && (block[0] == NACK))
				batch_valid = false;

			mutex_lock(&priv->lock);
			/* build request */
			req->addr = addr;
			req->type = type;
			req->len = bytes;
			req->buf = req->write_buf;
			memcpy(req->write_buf, buf, bytes);

			/* update variables for next request
			   (if this is longer than a single request) */
			size -= bytes;
			buf += bytes;
			addr += bytes / CC_USB_BLOCK_SIZE;

			queue_activate(priv);
			mutex_unlock(&priv->lock);
			wait_for_completion(&ready_for_new_requests);
			INIT_COMPLETION(ready_for_new_requests);
		}

	} else { /* CC_REQ_READ */
		if ((addr >= out_block1) && preload) {
			if (imp_ack && (((addr >= out_block2) && (cur_out_block == out_block1)) ||
					((addr < out_block2) && (cur_out_block == out_block2)))) {
				/* implicit ACK, get new batch */
				batch_valid = false;
				cur_out_block = cur_out_block == out_block1 ? out_block2 : out_block1;
			}

			if (!batch_valid) {
				struct request *req;
				req = get_request_wait(priv, req);
				mutex_lock(&priv->lock);
				req->addr = addr;
				req->type = type;
				req->buf = batch;
				req->len = CC_USB_BLOCK_SIZE * CC_USB_MAX_BLOCKS;
				batch_block_pointer = 0;
				queue_activate(priv);
				mutex_unlock (&priv->lock);
				wait_for_completion(&ready_for_new_requests);
				INIT_COMPLETION(ready_for_new_requests);
				batch_valid = true;
			}

			memcpy(buf, next_block(size / CC_USB_BLOCK_SIZE), size);

			if (*(uint32_t*)buf == 0) {
				/* empty batch, need to fetch new batch next time */
				batch_block_pointer = 0;
				batch_valid = false;
			}
		} else {
			struct request *req;
			req = get_request_wait(priv, req);
			mutex_lock (&priv->lock);
			req->addr = addr;
			req->type = type;
			req->buf = buf;
			req->len = size;
			queue_activate(priv);
			mutex_unlock (&priv->lock);
			wait_for_completion(&ready_for_new_requests);
			INIT_COMPLETION(ready_for_new_requests);
		}
	}
}
EXPORT_SYMBOL_GPL(fxi_request);

/* fops */

static int anyscreen_open(struct inode *inode, struct file *filp)
{
	/* A miscdevice is a character device driver, but has some
	   strange differences. During open the filp->private_data is
	   used to store the miscdevice instance itself rather that
	   the usual private data. This is done since there's no way
	   to derive the miscdevice instance from the inode.  We want
	   the actual private data (the struct anyscreen pointer), so
	   we overwrite private_data with the containing anyscreen
	   structure */
	struct anyscreen *priv;
	priv = container_of(filp->private_data, struct anyscreen, miscdev);
	filp->private_data = priv;

	if (!atomic_dec_and_test(&anyscreen_available)) {
		/* already open, do not allow multiple opens */
		atomic_inc(&anyscreen_available);
		return -EBUSY;
	}
	return 0;
}

static int anyscreen_release(struct inode *inode, struct file *filp)
{
	atomic_inc(&anyscreen_available);
	return 0;
}

static ssize_t anyscreen_read(struct file *filp, char __user *buf,
			       size_t count, loff_t *f_pos)
{
	struct anyscreen *priv = filp->private_data;
	switch (priv->message_part) {
	case HEADER:
		dev_dbg(priv->dev, "read header %d\n", count);

		if (!current_request && queue_size(priv)) {
			mutex_lock(&priv->lock);
			current_request = queue_front(priv);
			mutex_unlock(&priv->lock);
		}

		if (count == 3 * sizeof (unsigned long)) {
			unsigned long req[3];

			if (!current_request) {
				req[0] = CC_REQ_NONE;
				req[1] = 0;
				req[2] = 0;
			} else {
				req[0] = current_request->type;
				req[1] = current_request->addr;
				req[2] = current_request->len;
			}

			if (copy_to_user(buf, req, 3 * sizeof (unsigned long)) != 0)
				dev_err(priv->dev, "copy_to_user failed in %s\n", __func__);

			if (current_request)
				priv->message_part = DATA;
		} else {
			dev_err(priv->dev, "Illegal read size: %d\n", count);
			return -EIO;
		}
		break;

	case DATA:
		dev_dbg(priv->dev, "read data %d\n", count);

		if (count > current_request->len)
			count = current_request->len;

		if (copy_to_user(buf, current_request->buf, count) != 0)
			dev_err(priv->dev, "copy_to_user failed in %s\n", __func__);

		current_request->buf += count;
		current_request->len -= count;
		break;
	}
	return count;
}

static ssize_t anyscreen_write(struct file *filp, const char __user *buf,
				 size_t count, loff_t *f_pos)
{

	struct anyscreen *priv = filp->private_data;
	dev_dbg(priv->dev, "write %d\n", count);

	if (count > current_request->len)
		count = current_request->len;

	if (copy_from_user(current_request->buf, buf, count) != 0)
		dev_err(priv->dev, "copy_from_user failed in %s\n", __func__);

	current_request->buf += count;
	current_request->len -= count;
	return count;
}

static long anyscreen_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	struct anyscreen *priv = file->private_data;
	switch (cmd) {
	case CC_ANYSCREEN_IOCTL_READY: {
		dev_dbg(priv->dev, "waking up process\n");
		daemon_up = true;
		complete(&daemon_running);
		break;
	}

	case CC_ANYSCREEN_IOCTL_BLOCK_DONE: {
		dev_dbg(priv->dev, "BLOCK_DONE\n");
		queue_remove(priv);
		complete_all(&ready_for_new_requests);
		mutex_lock(&priv->lock);
		current_request = NULL;
		priv->message_part = HEADER;
		mutex_unlock(&priv->lock);
		break;
	}

	case CC_ANYSCREEN_IOCTL_IN:
		in_block = arg;
		dev_dbg(priv->dev, "inblock: %x\n", in_block);
		break;

	case CC_ANYSCREEN_IOCTL_OUT1:
		out_block1 = arg;
		dev_dbg(priv->dev, "outblock1: %x\n", out_block1);
		break;

	case CC_ANYSCREEN_IOCTL_OUT2:
		out_block2 = arg;
		dev_dbg(priv->dev, "outblock2: %x\n", out_block2);
		break;

	case CC_ANYSCREEN_IOCTL_PRELOAD:
		batch_valid = false;
		preload = true;
		cur_out_block = out_block1;
		dev_dbg(priv->dev, "starting preload mode\n");
		break;

	case CC_ANYSCREEN_IOCTL_BATCHSIZE:
		batch_size = arg;
		break;

	case CC_ANYSCREEN_IOCTL_IMPACK:
		imp_ack = true;
		break;

	case CC_ANYSCREEN_IOCTL_HASDATA:
		if (!current_request && queue_size(priv)) {
			mutex_lock(&priv->lock);
			current_request = queue_front(priv);
			priv->message_part = HEADER;
			mutex_unlock(&priv->lock);
		}

		if (current_request)
			return 1;
		else
			return 0;
		break;

	case CC_ANYSCREEN_IOCTL_DISABLE_POLL: {
		mutex_lock(&priv->lock);
		if (current_request || queue_size(priv)) {
			dev_dbg(priv->dev, "Inside DISABLE_POLL, send SIGIO, start poll\n");
			priv->disable_async_notification = true;
			kill_fasync(&priv->async_queue, SIGIO, POLL_IN);
		} else {
			priv->disable_async_notification = false;
			dev_dbg(priv->dev, "Inside DISABLE_POLL, stop poll\n");
		}
		mutex_unlock(&priv->lock);
		break;
	}

	default:
		dev_err(priv->dev, "invalid ioctl code %d\n", cmd);
		return -EIO;
	}
	return 0;
}

static int anyscreen_fasync (int fd, struct file *filp, int mode)
{
	struct anyscreen *priv = filp->private_data;
	return fasync_helper(fd, filp, mode, &priv->async_queue);
}

static struct file_operations anyscreen_fops = {
	.owner = THIS_MODULE,
	.open = anyscreen_open,
	.read = anyscreen_read,
	.write = anyscreen_write,
	.release = anyscreen_release,
	.fasync = anyscreen_fasync,
	.unlocked_ioctl = anyscreen_ioctl,
};

static int __devinit anyscreen_probe(struct platform_device *dev)
{
	int i, retval;
	struct anyscreen *priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&dev->dev, "Failed to alloc anyscreen\n");
		return -ENOMEM;
	}

	priv->miscdev.minor = MISC_DYNAMIC_MINOR;
	priv->miscdev.name = DEVNAME;
	priv->miscdev.fops = &anyscreen_fops;
	dev_set_drvdata(&dev->dev, priv);
	priv->dev = &dev->dev;
	anyscreen_global = priv;
	mutex_init(&priv->lock);

	pr_warn(DEVNAME ": probe\n");

	imp_ack = false;
	current_request = NULL;
	priv->async_queue = NULL;
	batch_valid = false;
	preload = false;
	daemon_up = false;
	priv->disable_async_notification = false;
	first_req = 0;
	last_req = -1;

	init_completion(&daemon_running);
	init_completion(&ready_for_new_requests);

	for (i = 0; i < MAX_REQUESTS; i++) {
		request_queue[i].write_buf = kmalloc (MAX_BUF_SIZE, GFP_KERNEL);
		if (!request_queue[i].write_buf)
			goto fail_alloc;
	}

	retval = misc_register(&priv->miscdev);
	if (retval)
		goto fail_dev;
	else
		dev_info(priv->dev, "misc minor: %d\n", priv->miscdev.minor);

	dev_info(priv->dev, "init done");
	return 0;

fail_dev:
	pr_err(DEVNAME ": no misc device registration failed\n");
	return -ENODEV;

fail_alloc:
	/* TODO: free */
	pr_err(DEVNAME ":fail to allocate memory\n");
	return -ENODEV;
}

static int anyscreen_remove(struct platform_device *dev)
{
	struct anyscreen *priv = dev_get_drvdata(&dev->dev);
	misc_deregister(&priv->miscdev);
	return 0;
}

static struct platform_driver anyscreen_driver = {
	.probe  = anyscreen_probe,
	.remove = anyscreen_remove,
	.driver = {
		.name = DEVNAME,
		.owner = THIS_MODULE,
	},
};

module_platform_driver(anyscreen_driver);
MODULE_LICENSE ("GPL");
