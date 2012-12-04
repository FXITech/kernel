#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/completion.h>

#include <linux/time.h>
#include "fxi_char_dev.h"

#define DEVNAME "fxichardev"

#define FXI_BLOCK_SIZE 512
#define FXI_MAX_BLOCKS 128

#define MAX_REQUESTS 128
#define MAX_BUF_SIZE 4096

enum PacketTypes {
	NODATA = 0,
	DATAPACKET,
	MARK,
	INIT,
	NACK,
	ACK = 0x80
};

enum CommandStates {
	COMMAND = 0,
	DATA
};

/* represents one request in the request queue */
struct FxiRequest {
	unsigned long type;
	unsigned long addr;
	signed long len;
	void *buf;
	void *writeBuf;
};

static struct device *fxidev;

static struct fasync_struct *fxiAsyncQueue;
static atomic_t fxichardev_available = ATOMIC_INIT(1);

static int commandState;

/* request queue (ringbuffer) */
static struct FxiRequest requestQueue[MAX_REQUESTS];
static int firstReq;
static int lastReq;

/* current request */
static struct FxiRequest *currentRequest;

/* current batch being served to USB host */
static unsigned char batch[FXI_MAX_BLOCKS][FXI_BLOCK_SIZE];
static int batchSize;
static int batchValid; /* batch is valid */
static int batchBlockPointer; /* current batch block pointer */

/* various flags */
static int preload; /* flag: preload mode active */
static int impAck; /* flag: ACK is done in driver (not user space) */
static volatile int daemonUp = false; /* flags: user space daemon has started */
static struct completion daemon_running;
static struct completion ready_for_new_requests;

static int polling;

/* other configuration vars */
static int inBlock; /* address of inblock */
static int outBlock1; /* address of outblock 1 */
static int outBlock2; /* address of outblock 2 */
static int curOutBlock; /* set to either outBlock1 or outBlock2 */

/* queue management */

static DEFINE_MUTEX(fxichardevmutex);

static int queueSize (void)
{
	if (lastReq < 0)
		return 0;
	else if (lastReq == firstReq)
		return MAX_REQUESTS;
	else if (lastReq > firstReq)
		return lastReq - firstReq;
	else
		return lastReq + (MAX_REQUESTS - firstReq);
}

static struct FxiRequest *queueInsert (void)
{
	struct FxiRequest *req;

	if (queueSize() >= MAX_REQUESTS)
		return NULL;

	if (lastReq < 0)
		lastReq = firstReq;

	dev_dbg(fxidev, "insert %d (%d)\n", lastReq, queueSize() + 1);
	req = &requestQueue[lastReq];
	lastReq = (lastReq + 1) % MAX_REQUESTS;
	return req;
}

void queueActivate (void)
{
	if (!polling) {
		dev_dbg(fxidev, "send SIGIO, start poll\n");
		polling = true;
		kill_fasync (&fxiAsyncQueue, SIGIO, POLL_IN);
	}
}

static struct FxiRequest *queueFront (void)
{
	struct FxiRequest *req;
	dev_dbg(fxidev, "getting %d\n", firstReq);
	req = &requestQueue[firstReq];
	return req;
}

static void queueRemove (void)
{
	mutex_lock (&fxichardevmutex);
	dev_dbg(fxidev, "remove %d\n", queueSize() - 1);
	firstReq = (firstReq + 1) % MAX_REQUESTS;
	if (firstReq == lastReq)
		lastReq = -1; /* queue empty */
	mutex_unlock (&fxichardevmutex);
}

static inline void *nextBlock (int blocks)
{
	void *ptr = batch[batchBlockPointer % batchSize];
	batchBlockPointer += blocks;
	return ptr;
}

static struct FxiRequest* get_request_wait(struct FxiRequest *req)
{
	for (;;) {
		mutex_lock(&fxichardevmutex);
		req = queueInsert();
		mutex_unlock(&fxichardevmutex);
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
	dev_dbg(fxidev, "fxirequest %ld %ld %d (queue: %d) - %p\n",
		type, addr, size, queueSize(), buf);

	/* wait if fusionx daemon is not up yet */
	if (!daemonUp) {
		wait_for_completion(&daemon_running);
		INIT_COMPLETION(daemon_running);
		dev_dbg(fxidev, "daemon connected\n");
	}

	if (type == FXIWRITE) {
		while (size) {
			struct FxiRequest *req;
			int bytes = size;
			unsigned char *block = buf;

			if (bytes > MAX_BUF_SIZE)
				bytes = MAX_BUF_SIZE;

			req = get_request_wait(req);

			 /* ACK, get new batch */
			if ((addr >= inBlock) && (block[0] & ACK))
				batchValid = false;

			 /* NACK, get a previous batch */
			if ((addr >= inBlock) && (block[0] == NACK))
				batchValid = false;

			mutex_lock(&fxichardevmutex);
			/* build request */
			req->addr = addr;
			req->type = type;
			req->len = bytes;
			req->buf = req->writeBuf;
			memcpy(req->writeBuf, buf, bytes);

			/* update variables for next request
			   (if this is longer than a single request) */
			size -= bytes;
			buf += bytes;
			addr += bytes / FXI_BLOCK_SIZE;

			queueActivate();
			mutex_unlock(&fxichardevmutex);
			wait_for_completion(&ready_for_new_requests);
			INIT_COMPLETION(ready_for_new_requests);
		}

	} else { /* FXIREAD */
		if ((addr >= outBlock1) && preload) {
			if (impAck && (((addr >= outBlock2) && (curOutBlock == outBlock1)) ||
				       ((addr < outBlock2) && (curOutBlock == outBlock2)))) {
				/* implicit ACK, get new batch */
				batchValid = false;
				curOutBlock = curOutBlock == outBlock1 ? outBlock2 : outBlock1;
			}

			if (!batchValid) {
				struct FxiRequest *req;
				req = get_request_wait(req);
				mutex_lock(&fxichardevmutex);
				req->addr = addr;
				req->type = type;
				req->buf = batch;
				req->len = FXI_BLOCK_SIZE * FXI_MAX_BLOCKS;
				batchBlockPointer = 0;
				queueActivate();
				mutex_unlock (&fxichardevmutex);
				wait_for_completion(&ready_for_new_requests);
				INIT_COMPLETION(ready_for_new_requests);
				batchValid = true;
			}

			memcpy(buf, nextBlock(size / FXI_BLOCK_SIZE), size);

			if (*(uint32_t*)buf == 0) {
				/* empty batch, need to fetch new batch next time */
				batchBlockPointer = 0;
				batchValid = false;
			}
		} else {
			struct FxiRequest *req;
			req = get_request_wait(req);
			mutex_lock (&fxichardevmutex);
			req->addr = addr;
			req->type = type;
			req->buf = buf;
			req->len = size;
			queueActivate();
			mutex_unlock (&fxichardevmutex);
			wait_for_completion(&ready_for_new_requests);
			INIT_COMPLETION(ready_for_new_requests);
		}
	}
}
EXPORT_SYMBOL_GPL(fxi_request);

/* fops */

static int fxichardev_open (struct inode *inode, struct file *filp)
{
	if (!atomic_dec_and_test (&fxichardev_available)) {
		/* already open, do not allow multiple opens */
		atomic_inc (&fxichardev_available);
		return -EBUSY;
	}
	return 0;
}

static int fxichardev_release (struct inode *inode, struct file *filp)
{
	atomic_inc (&fxichardev_available);
	return 0;
}

static ssize_t fxichardev_read (struct file *filp, char __user *buf,
				size_t count, loff_t *f_pos)
{
	switch (commandState) {
	case COMMAND:
		dev_dbg(fxidev, "read command %d\n", count);

		if (!currentRequest && queueSize()) {
			mutex_lock (&fxichardevmutex);
			currentRequest = queueFront();
			mutex_unlock (&fxichardevmutex);
		}

		if (count == 3 * sizeof (unsigned long)) {
			unsigned long req[3];

			if (!currentRequest) {
				req[0] = FXINONE;
				req[1] = 0;
				req[2] = 0;
			} else {
				req[0] = currentRequest->type;
				req[1] = currentRequest->addr;
				req[2] = currentRequest->len;
			}

			if (copy_to_user (buf, req, 3 * sizeof (unsigned long)) != 0)
				dev_err(fxidev, "copy_to_user failed in %s\n", __func__);

			if (currentRequest)
				commandState = DATA;
		} else {
			dev_err(fxidev, "Illegal read size: %d\n", count);
			return -EIO;
		}
		break;

	case DATA:
		dev_dbg(fxidev, "read data %d\n", count);

		if (count > currentRequest->len)
			count = currentRequest->len;

		if (copy_to_user (buf, currentRequest->buf, count) != 0)
			dev_err(fxidev, "copy_to_user failed in %s\n", __func__);

		currentRequest->buf += count;
		currentRequest->len -= count;
		break;
	}
	return count;
}

static ssize_t fxichardev_write (struct file *filp, const char __user *buf,
				 size_t count, loff_t *f_pos)
{
	dev_dbg(fxidev, "write %d\n", count);

	if (count > currentRequest->len)
		count = currentRequest->len;

	if (copy_from_user (currentRequest->buf, buf, count) != 0)
		dev_err(fxidev, "copy_from_user failed in %s\n", __func__);

	currentRequest->buf += count;
	currentRequest->len -= count;
	return count;
}

static long fxichardev_ioctl (struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	switch (cmd) {
	case FXI_CHAR_DEV_IOCTL_READY: {
		dev_dbg(fxidev, "waking up process\n");
		daemonUp = true;
		complete(&daemon_running);
		break;
	}

	case FXI_CHAR_DEV_IOCTL_BLOCK_DONE: {
		dev_dbg(fxidev, "BLOCK_DONE\n");
		queueRemove();
		complete_all(&ready_for_new_requests);
		mutex_lock (&fxichardevmutex);
		currentRequest = NULL;
		commandState = COMMAND;
		mutex_unlock (&fxichardevmutex);
		break;
	}

	case FXI_CHAR_DEV_IOCTL_IN:
		inBlock = arg;
		dev_dbg(fxidev, "inblock: %x\n", inBlock);
		break;

	case FXI_CHAR_DEV_IOCTL_OUT1:
		outBlock1 = arg;
		dev_dbg(fxidev, "outblock1: %x\n", outBlock1);
		break;

	case FXI_CHAR_DEV_IOCTL_OUT2:
		outBlock2 = arg;
		dev_dbg(fxidev, "outblock2: %x\n", outBlock2);
		break;

	case FXI_CHAR_DEV_IOCTL_PRELOAD:
		batchValid = false;
		preload = true;
		curOutBlock = outBlock1;
		dev_dbg(fxidev, "starting preload mode\n");
		break;

	case FXI_CHAR_DEV_IOCTL_BATCHSIZE:
		batchSize = arg;
		break;

	case FXI_CHAR_DEV_IOCTL_IMPACK:
		impAck = true;
		break;

	case FXI_CHAR_DEV_IOCTL_HASDATA:
		if (!currentRequest && queueSize()) {
			mutex_lock (&fxichardevmutex);
			currentRequest = queueFront();
			commandState = COMMAND;
			mutex_unlock (&fxichardevmutex);
		}

		if (currentRequest)
			return 1;
		else
			return 0;
		break;

	case FXI_CHAR_DEV_IOCTL_DISABLE_POLL: {
		mutex_lock (&fxichardevmutex);
		if (currentRequest || queueSize()) {
			dev_dbg(fxidev, "Inside DISABLE_POLL, send SIGIO, start poll\n");
			polling = true;
			kill_fasync (&fxiAsyncQueue, SIGIO, POLL_IN);
		} else {
			polling = false;
			dev_dbg(fxidev, "Inside DISABLE_POLL, stop poll\n");
		}
		mutex_unlock (&fxichardevmutex);
		break;
	}

	default:
		dev_err(fxidev, "invalid ioctl code %d\n", cmd);
		return -EIO;
	}
	return 0;
}

static int fxichardev_fasync (int fd, struct file *filp, int mode)
{
 	return fasync_helper (fd, filp, mode, &fxiAsyncQueue);
}

static struct file_operations fxichardev_fops = {
	.owner = THIS_MODULE,
	.open = fxichardev_open,
	.read = fxichardev_read,
	.write = fxichardev_write,
	.release = fxichardev_release,
	.fasync = fxichardev_fasync,
	.unlocked_ioctl = fxichardev_ioctl,
};

static struct miscdevice md = {
 	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVNAME,
	.fops = &fxichardev_fops,
};

static int __devinit fxichardev_probe(struct platform_device *dev)
{
	int i, retval;
	pr_warn(DEVNAME ": probe\n");
	fxidev = &dev->dev;
	impAck = false;
	currentRequest = NULL;
	fxiAsyncQueue = NULL;
	batchValid = false;
	preload = false;
	daemonUp = false;
	polling = false;
	firstReq = 0;
	lastReq = -1;

	init_completion(&daemon_running);
	init_completion(&ready_for_new_requests);

	for (i = 0; i < MAX_REQUESTS; i++) {
		requestQueue[i].writeBuf = kmalloc (MAX_BUF_SIZE, GFP_KERNEL);
		if (!requestQueue[i].writeBuf)
			goto fail_alloc;
	}

	retval = misc_register (&md);
	if (retval)
		goto fail_dev;
	else
		dev_err(fxidev, "misc minor: %d\n", md.minor);

	dev_dbg(fxidev, "init done");
	return 0;

fail_dev:
	pr_err(DEVNAME ": no misc device registration failed\n");
	return -ENODEV;

fail_alloc:
	/* TODO: free */
	pr_err(DEVNAME ":fail to allocate memory\n");
	return -ENODEV;
}

static int fxichardev_remove(struct platform_device *dev)
{
	misc_deregister (&md);
	dev_dbg(fxidev, "%s called\n", __func__);
	return 0;
}

static struct platform_driver fxichardev_driver = {
	.probe	= fxichardev_probe,
	.remove = fxichardev_remove,
	.driver = {
		.name = "fxichardev",
	},
};

static int __init fxichardev_init(void)
{
	return platform_driver_register(&fxichardev_driver);
}

module_init(fxichardev_init);

#ifdef MODULE
static void __exit fxichardev_exit(void)
{
	platform_driver_unregister(&fxichardev_driver);
}

module_exit(fxichardev_exit);

MODULE_LICENSE ("GPL");
#endif
