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

#include <linux/time.h>

///////////////////////////////////////////////////////////////////////////////

#define DEVNAME "fxichardev"

#define FXI_BLOCK_SIZE 512
#define FXI_MAX_BLOCKS 128

#define MAX_REQUESTS 128
#define MAX_BUF_SIZE 4096

///////////////////////////////////////////////////////////////////////////////

enum PacketTypes {
	NODATA, DATAPACKET, MARK, INIT, NACK, ACK = 0x80
};

enum CommandStates {
	COMMAND,
	DATA
};

enum RequestTypes {
	FXINONE, FXIREAD, FXIWRITE
};

// represents one request in the request queue
struct FxiRequest {
	struct task_struct *task;
	unsigned long type;
	unsigned long addr;
	signed long len;
	void *buf;
	void *writeBuf;
};

///////////////////////////////////////////////////////////////////////////////
// IOCTLs

#define BLOCK_DONE 0xad1
#define IN 0xad2
#define OUT1 0xad3
#define OUT2 0xad4
#define PRELOAD 0xad5
#define BATCHSIZE 0xad6
#define DATACMD 0xad7
#define IMPACK 0xad8
#define READY 0xad9
#define HASDATA 0xada
#define DISABLE_POLL 0xadb

///////////////////////////////////////////////////////////////////////////////

static struct device *fxidev;

static struct fasync_struct *fxiAsyncQueue;
static atomic_t fxichardev_available = ATOMIC_INIT(1);

static struct task_struct *fxiSleepingTask; // task that is sleeping if !awake

static int commandState;

// request queue (ringbuffer)
static struct FxiRequest requestQueue[MAX_REQUESTS];
static int firstReq;
static int lastReq;

// current request
static struct FxiRequest *currentRequest;

// current batch being served to USB host
static unsigned char batch[FXI_MAX_BLOCKS][FXI_BLOCK_SIZE];
static int batchSize;
static int batchValid; // batch is valid
static int batchBlockPointer; // current batch block pointer

// various flags
static int preload; // flag: preload mode active
static int impAck; // flag: ACK is done in driver (not user space)
static volatile int awake; // flag: is awake and ready to accept reads/writes
static volatile int daemonUp = false; // flags: user space daemon has started
static int polling;

// other configuration vars
static int inBlock; // address of inblock
static int outBlock1; // address of outblock 1
static int outBlock2; // address of outblock 2
static int curOutBlock; // set to either outBlock1 or outBlock2

///////////////////////////////////////////////////////////////////////////////
// queue management

static DEFINE_MUTEX(fxichardevmutex);
static spinlock_t fxichardevlock;

static int queueSize (void) {
	if (lastReq < 0) return 0;
	else if (lastReq == firstReq) return MAX_REQUESTS;
	else if (lastReq > firstReq) return lastReq - firstReq;
	else return lastReq + (MAX_REQUESTS - firstReq);
}

static struct FxiRequest *queueInsert (void) {
	struct FxiRequest *req;

	if (queueSize() >= MAX_REQUESTS) {
		return NULL;
	}

	if (lastReq < 0) lastReq = firstReq;

	dev_dbg(fxidev, "insert %d (%d)\n", lastReq, queueSize() + 1);

	req = &requestQueue[lastReq];
	lastReq = (lastReq + 1) % MAX_REQUESTS;

	return req;
}

void queueActivate (void) {
	if (!polling) {
		dev_dbg(fxidev, "send SIGIO, start poll\n");
		polling = true;
		kill_fasync (&fxiAsyncQueue, SIGIO, POLL_IN);
	}
}

static struct FxiRequest *queueFront (void) {
	struct FxiRequest *req;

	dev_dbg(fxidev, "getting %d\n", firstReq);

	req = &requestQueue[firstReq];

	return req;
}

static void queueRemove (void) {
	unsigned long flags;

	// lock
	if (!in_irq()) mutex_lock (&fxichardevmutex);
	spin_lock_irqsave (&fxichardevlock, flags);

	dev_dbg(fxidev, "remove %d\n", queueSize() - 1);

	firstReq = (firstReq + 1) % MAX_REQUESTS;

	if (firstReq == lastReq) {
		// queue empty
		lastReq = -1;
	}

	// unlock
	spin_unlock_irqrestore (&fxichardevlock, flags);
	if (!in_irq()) mutex_unlock (&fxichardevmutex);
}

///////////////////////////////////////////////////////////////////////////////
// handling requests from USB subsystem

static void sleepThread (unsigned long flags)
{
	dev_dbg(fxidev, "Sleeping %p\n", current);
	while (!awake) {
		fxiSleepingTask = current;
		set_current_state (TASK_INTERRUPTIBLE);

		// unlock
		spin_unlock_irqrestore (&fxichardevlock, flags);
		if (!in_irq()) mutex_unlock (&fxichardevmutex);

		schedule();

		// lock
		if (!in_irq()) mutex_lock (&fxichardevmutex);
		spin_lock_irqsave (&fxichardevlock, flags);
	}
}

static void sleepReq (struct FxiRequest *req, unsigned long flags) {
	dev_dbg(fxidev, "Sleeping req %p\n", current);
	while (!awake) {
		req->task = current;
		set_current_state (TASK_INTERRUPTIBLE);

		// unlock
		spin_unlock_irqrestore (&fxichardevlock, flags);
		if (!in_irq()) mutex_unlock (&fxichardevmutex);

		schedule();

		// lock
		if (!in_irq()) mutex_lock (&fxichardevmutex);
		spin_lock_irqsave (&fxichardevlock, flags);
	}
}

static inline void *nextBlock (int blocks) {
	void *ptr = batch[batchBlockPointer % batchSize];
	batchBlockPointer += blocks;
	return ptr;
}

// called by gadget driver whenever there is a request (read of write)
// from the host
void fxi_request (unsigned long addr, void *buf, unsigned long type, int size) {

	dev_dbg(fxidev, "fxirequest %ld %ld %d (queue: %d) - %p\n",
		type, addr, size, queueSize(), buf);

	// wait if fusionx daemon is not up yet
	if (!daemonUp) {
		dev_dbg(fxidev, "Waiting for daemon ...\n");

		while (!daemonUp) {
			unsigned long flags;
			local_irq_save (flags);

			fxiSleepingTask = current;
			set_current_state (TASK_INTERRUPTIBLE);

			local_irq_restore (flags);

			schedule();
		}
		dev_dbg(fxidev, "daemon connected\n");
	}

	if (type == FXIWRITE) {
		while (size) {
			unsigned long flags;
			struct FxiRequest *req;
			int bytes = size;

			if (bytes > MAX_BUF_SIZE) bytes = MAX_BUF_SIZE;

			// lock
			if (!in_irq()) mutex_lock (&fxichardevmutex);
			spin_lock_irqsave (&fxichardevlock, flags);

			while (!(req = queueInsert())) {
				dev_dbg(fxidev, "Sleeping due to full queue\n");

				awake = 0;

				sleepThread (flags);
			}

			{
				unsigned char *block = buf;
				if ((addr >= inBlock) && (block[0] & ACK)) {
					// ACK, get new batch
					batchValid = false;
				}
				if ((addr >= inBlock) && (block[0] == NACK)) {
					// NACK, get a previous batch
					batchValid = false;
				}
			}

			// build request
			req->task = NULL;
			req->addr = addr;
			req->type = type;
			req->len = bytes;
			req->buf = req->writeBuf;

			memcpy (req->writeBuf, buf, bytes);

			// update variables for next request
			// (if this is longer than a single request)
			size -= bytes;
			buf += bytes;
			addr += bytes / FXI_BLOCK_SIZE;

			queueActivate();

			// unlock
			spin_unlock_irqrestore (&fxichardevlock, flags);
			if (!in_irq()) mutex_unlock (&fxichardevmutex);
		}

	} else { // FXIREAD

		if ((addr >= outBlock1) && preload) {

			if (impAck && (((addr >= outBlock2) && (curOutBlock == outBlock1)) ||
				       ((addr < outBlock2) && (curOutBlock == outBlock2)))) {
				// implicit ACK, get new batch
				batchValid = false;
				curOutBlock = curOutBlock == outBlock1 ? outBlock2 : outBlock1;
			}

			if (!batchValid) {
				unsigned long flags;
				struct FxiRequest *req;

				// lock
				if (!in_irq()) mutex_lock (&fxichardevmutex);
				spin_lock_irqsave (&fxichardevlock, flags);

				while (!(req = queueInsert())) {
					dev_dbg(fxidev, "Sleeping due to full queue\n");

					awake = 0;

					sleepThread (flags);
				}

				req->task = NULL;
				req->addr = addr;
				req->type = type;
				req->buf = batch;
				req->len = FXI_BLOCK_SIZE * FXI_MAX_BLOCKS;

				batchBlockPointer = 0;
				awake = 0;
				queueActivate();

				sleepReq (req, flags);

				// unlock
				spin_unlock_irqrestore (&fxichardevlock, flags);
				if (!in_irq()) mutex_unlock (&fxichardevmutex);

				batchValid = true;
			}

			memcpy (buf, nextBlock (size / FXI_BLOCK_SIZE), size);

			if (*(uint32_t*)buf == 0) {
				// empty batch, need to fetch new batch next time
				batchBlockPointer = 0;
				batchValid = false;
			}

		} else {
			unsigned long flags;
			struct FxiRequest *req;

			// lock
			if (!in_irq()) mutex_lock (&fxichardevmutex);
			spin_lock_irqsave (&fxichardevlock, flags);

			while (!(req = queueInsert())) {
				dev_dbg(fxidev, "Sleeping due to full queue\n");

				awake = 0;

				sleepThread (flags);
			}

			req->task = NULL;
			req->addr = addr;
			req->type = type;
			req->buf = buf;
			req->len = size;

			awake = 0;
			queueActivate();

			sleepReq (req, flags);

			// unlock
			spin_unlock_irqrestore (&fxichardevlock, flags);
			if (!in_irq()) mutex_unlock (&fxichardevmutex);
		}
	}
}
EXPORT_SYMBOL_GPL(fxi_request);

///////////////////////////////////////////////////////////////////////////////
// fops

static int fxichardev_open (struct inode *inode, struct file *filp) {
	if (!atomic_dec_and_test (&fxichardev_available)) {
		// already open, do not allow multiple opens
		atomic_inc (&fxichardev_available);
		return -EBUSY;
	}

	return 0;
}

static int fxichardev_release (struct inode *inode, struct file *filp) {
	atomic_inc (&fxichardev_available);

	return 0;
}

static int fxichardev_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long start = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	dma_addr_t handle;
	void *mem = dma_alloc_coherent (fxidev, size, &handle, GFP_KERNEL);
	unsigned long pos = (unsigned long)mem;

        dev_dbg(fxidev, "%s called, allocated: %p %x\n", __func__, mem, handle);

	if (!mem) {
		dev_err(fxidev, "Failed to allocate memory in %s\n", __func__);
		return -ENOMEM;
	}

	while (size > 0) {
		unsigned long page = vmalloc_to_pfn((void *)pos);
		if (remap_pfn_range(vma, start, page, PAGE_SIZE, PAGE_SHARED)) {
			return -EAGAIN;
		}
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}

	vma->vm_flags |= VM_RESERVED | VM_IO;

	return 0;
}

static ssize_t fxichardev_read (struct file *filp, char __user *buf,
				size_t count, loff_t *f_pos) {
	switch (commandState) {

	case COMMAND:
		dev_dbg(fxidev, "read command %d\n", count);

		if (!currentRequest && queueSize()) {
			unsigned long flags;

			// lock
			if (!in_irq()) mutex_lock (&fxichardevmutex);
			spin_lock_irqsave (&fxichardevlock, flags);

			// fetch
			currentRequest = queueFront();

			// unlock
			spin_unlock_irqrestore (&fxichardevlock, flags);
			if (!in_irq()) mutex_unlock (&fxichardevmutex);
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

			if (currentRequest) commandState = DATA;
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
				 size_t count, loff_t *f_pos) {

	dev_dbg(fxidev, "write %d\n", count);

	if (count > currentRequest->len) count = currentRequest->len;

	if (copy_from_user (currentRequest->buf, buf, count) != 0)
		dev_err(fxidev, "copy_from_user failed in %s\n", __func__);

	currentRequest->buf += count;
	currentRequest->len -= count;

	return count;
}

static long fxichardev_ioctl (struct file *file, unsigned int cmd,
			      unsigned long arg) {
	switch (cmd) {

	case READY: {
		unsigned long flags;

		// lock
		if (!in_irq()) mutex_lock (&fxichardevmutex);
		spin_lock_irqsave (&fxichardevlock, flags);

		dev_dbg(fxidev, "waking up process\n");
		daemonUp = true;
		if (fxiSleepingTask) {
			wake_up_process (fxiSleepingTask);
		}

		// unlock
		spin_unlock_irqrestore (&fxichardevlock, flags);
		if (!in_irq()) mutex_unlock (&fxichardevmutex);

		break;
	}

	case BLOCK_DONE: {
		unsigned long flags;

		dev_dbg(fxidev, "BLOCK_DONE\n");

		queueRemove();

		// lock
		if (!in_irq()) mutex_lock (&fxichardevmutex);
		spin_lock_irqsave (&fxichardevlock, flags);

		if (currentRequest->task) {
			awake = 1;
			dev_dbg(fxidev, "Inside BLOCK DONE, Waking task %p\n", currentRequest->task);
			wake_up_process (currentRequest->task);
		}

		currentRequest = NULL;

		if (fxiSleepingTask) {
			awake = 1;
			dev_dbg(fxidev, "Inside BLOCK DONE, Waking sleeping task %p\n", fxiSleepingTask);
			wake_up_process (fxiSleepingTask);
			fxiSleepingTask = NULL;
		}

		commandState = COMMAND;

		// unlock
		spin_unlock_irqrestore (&fxichardevlock, flags);
		if (!in_irq()) mutex_unlock (&fxichardevmutex);

		break;
	}

	case IN:
		inBlock = arg;
		dev_dbg(fxidev, "inblock: %x\n", inBlock);
		break;

	case OUT1:
		outBlock1 = arg;
		dev_dbg(fxidev, "outblock1: %x\n", outBlock1);
		break;

	case OUT2:
		outBlock2 = arg;
		dev_dbg(fxidev, "outblock2: %x\n", outBlock2);
		break;

	case PRELOAD:
		batchValid = false;
		preload = true;
		curOutBlock = outBlock1;
		dev_dbg(fxidev, "starting preload mode\n");
		break;

	case BATCHSIZE:
		batchSize = arg;
		break;

	case IMPACK:
		impAck = true;
		break;

	case HASDATA:
		if (!currentRequest && queueSize()) {
			unsigned long flags;

			// lock
			if (!in_irq()) mutex_lock (&fxichardevmutex);
			spin_lock_irqsave (&fxichardevlock, flags);

			// fetch
			currentRequest = queueFront();
			commandState = COMMAND;

			// unlock
			spin_unlock_irqrestore (&fxichardevlock, flags);
			if (!in_irq()) mutex_unlock (&fxichardevmutex);
		}

		if (currentRequest) return 1;
		else return 0;
		break;

	case DISABLE_POLL: {
		unsigned long flags;

		// lock
		if (!in_irq()) mutex_lock (&fxichardevmutex);
		spin_lock_irqsave (&fxichardevlock, flags);

		if (currentRequest || queueSize()) {
			dev_dbg(fxidev, "Inside DISABLE_POLL, send SIGIO, start poll\n");
			polling = true;
			kill_fasync (&fxiAsyncQueue, SIGIO, POLL_IN);
		} else {
			polling = false;
			dev_dbg(fxidev, "Inside DISABLE_POLL, stop poll\n");
		}

		// unlock
		spin_unlock_irqrestore (&fxichardevlock, flags);
		if (!in_irq()) mutex_unlock (&fxichardevmutex);

		break;
	}

	default:
		dev_err(fxidev, "invalid ioctl code %d\n", cmd);
		return -EIO;
	}

	return 0;
}

static int fxichardev_fasync (int fd, struct file *filp, int mode) {
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
	.mmap = fxichardev_mmap,
};

static struct miscdevice md = {
 	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVNAME,
	.fops = &fxichardev_fops,
};

///////////////////////////////////////////////////////////////////////////////
// module

static int __devinit fxichardev_probe(struct platform_device *dev)
{
	pr_warn(DEVNAME ": probe\n");

	fxidev = &dev->dev;

	impAck = false;
	currentRequest = NULL;
	fxiSleepingTask = NULL;
	fxiAsyncQueue = NULL;
	batchValid = false;
	preload = false;
	awake = 1;
	daemonUp = false;
	polling = false;
	firstReq = 0;
	lastReq = -1;

	{ // initialize request queue
		int i;

		for (i = 0; i < MAX_REQUESTS; i++) {
			requestQueue[i].writeBuf = kmalloc (MAX_BUF_SIZE, GFP_KERNEL);
			if (!requestQueue[i].writeBuf) goto fail_alloc;
		}
	}

	{ // init device
		int retval = misc_register (&md);
		if (retval)
			goto fail_dev;
		else
			dev_err(fxidev, "misc minor: %d\n", md.minor);
	}

	dev_dbg(fxidev, "init done");
	return 0;

fail_dev:
	pr_err(DEVNAME ": no misc device registration failed\n");
	return -ENODEV;

fail_alloc:
	// TODO: free
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
		.name	= "fxichardev",
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
