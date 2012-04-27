/*
 * fxifb.c
 *
 * Based heavily on videobuf2-fb.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/fb.h>

#include <linux/videodev2.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-fb.h>

/* for mali ump support */
#include <../drivers/gpu/arm/ump/include/ump_kernel_interface_ref_drv.h>
//#/drivers/gpu/arm/ump/include/ump_kernel_interface_ref_drv.h

#define SETRES
#define FULLHD
#define DOUBLE_BUFFERING
#define BPP32

#define FXIFB_BASE 0x48000000
#define FXIFB_SIZE (16 << 20)

static int num_allocated = 0;

static int debug = 0;
module_param(debug, int, 0644);

#define dprintk(level, fmt, arg...)				\
	do {							\
		if (debug >= level)				\
			printk(KERN_DEBUG "vb2: " fmt, ## arg);	\
	} while (0)

struct fxifb_data {
	struct video_device *vfd;
	struct vb2_queue *q;
	struct device *dev;
	struct v4l2_requestbuffers req;
	struct v4l2_buffer b[2];
	struct v4l2_plane p[2];
	void *videophys;
	struct resource *memres;
	void *vaddr;
	unsigned int size;
	int refcount;
	int blank;
	int streaming;

	struct file fake_file;
	struct dentry fake_dentry;
	struct inode fake_inode;

	// for mali ump support
	ump_dd_handle ump_wrapped_buffer;
};

static int fxifb_stop(struct fb_info *info);

struct fmt_desc {
	__u32			fourcc;
	__u32			bits_per_pixel;
	struct fb_bitfield	red;
	struct fb_bitfield	green;
	struct fb_bitfield	blue;
	struct fb_bitfield	transp;
};

static struct fmt_desc fmt_conv_table[] = {
	{
		.fourcc = V4L2_PIX_FMT_RGB565,
		.bits_per_pixel = 16,
		.red = {	.offset = 11,	.length = 5,	},
		.green = {	.offset = 5,	.length = 6,	},
		.blue = {	.offset = 0,	.length = 5,	},
	}, {
		.fourcc = V4L2_PIX_FMT_RGB555,
		.bits_per_pixel = 16,
		.red = {	.offset = 11,	.length = 5,	},
		.green = {	.offset = 5,	.length = 5,	},
		.blue = {	.offset = 0,	.length = 5,	},
	}, {
		.fourcc = V4L2_PIX_FMT_RGB444,
		.bits_per_pixel = 16,
		.red = {	.offset = 8,	.length = 4,	},
		.green = {	.offset = 4,	.length = 4,	},
		.blue = {	.offset = 0,	.length = 4,	},
		.transp = {	.offset = 12,	.length = 4,	},
	}, {
		.fourcc = V4L2_PIX_FMT_BGR32,
		.bits_per_pixel = 32,
		.red = {	.offset = 16,	.length = 8,	},
		.green = {	.offset = 8,	.length = 8,	},
		.blue = {	.offset = 0,	.length = 8,	},
		.transp = {	.offset = 24,	.length = 8,	},
	},
	/* TODO: add more format descriptors */
};

/**
 * vb2_drv_lock() - a shortcut to call driver specific lock()
 * @q:		videobuf2 queue
 */
static inline void vb2_drv_lock(struct vb2_queue *q)
{
	q->ops->wait_finish(q);
}

/**
 * vb2_drv_unlock() - a shortcut to call driver specific unlock()
 * @q:		videobuf2 queue
 */
static inline void vb2_drv_unlock(struct vb2_queue *q)
{
	q->ops->wait_prepare(q);
}

static int fxifb_start(struct fb_info *info);

/**
 * fxifb_activate() - activate framebuffer emulator
 * @info:	framebuffer vb2 emulator data
 * This function activates framebuffer emulator. The pixel format
 * is acquired from video node, memory is allocated and framebuffer
 * structures are filled with valid data.
 */
static int fxifb_activate(struct fb_info *info)
{
	struct fxifb_data *data = info->par;
	struct vb2_queue *q = data->q;
	struct fb_var_screeninfo *var;
	struct v4l2_format fmt;
	struct fmt_desc *conv = NULL;
	int width, height, fourcc, bpl, size;
	int i, ret = 0;
	int (*g_fmt)(struct file *file, void *fh, struct v4l2_format *f);

	/*
	 * Check if streaming api has not been already activated.
	 */
	if (q->streaming || q->num_buffers > 0) {
		printk (KERN_WARNING "%s already streaming\n", __func__);
		return -EBUSY;
	}

	/*
	 * Open video node.
	 */
	ret = data->vfd->fops->open(&data->fake_file);
	if (ret) {
		printk (KERN_WARNING "%s cant open video node\n", __func__);
		return ret;
	}

	/*
	 * Get format from the video node.
	 */
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = q->type;
	if (data->vfd->ioctl_ops->vidioc_g_fmt_vid_out) {
		g_fmt = data->vfd->ioctl_ops->vidioc_g_fmt_vid_out;
		ret = g_fmt(&data->fake_file, data->fake_file.private_data, &fmt);
		if (ret) {
			printk (KERN_WARNING "%s cant g_fmt\n", __func__);
			goto err;
		}
		width = fmt.fmt.pix.width;
		height = fmt.fmt.pix.height;
		fourcc = fmt.fmt.pix.pixelformat;
		bpl = fmt.fmt.pix.bytesperline;
		size = fmt.fmt.pix.sizeimage;
	} else if (data->vfd->ioctl_ops->vidioc_g_fmt_vid_out_mplane) {
		g_fmt = data->vfd->ioctl_ops->vidioc_g_fmt_vid_out_mplane;
		ret = g_fmt(&data->fake_file, data->fake_file.private_data, &fmt);
		if (ret) {
			printk (KERN_WARNING "%s cant g_fmt 2\n", __func__);
			goto err;
		}
		width = fmt.fmt.pix_mp.width;
		height = fmt.fmt.pix_mp.height;
		fourcc = fmt.fmt.pix_mp.pixelformat;
		bpl = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
		size = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
	} else {
		printk (KERN_WARNING "%s missing g_fmt\n", __func__);
		ret = -EINVAL;
		goto err;
	}

#ifdef SETRES
	/*
	 * Set source format
	 */

#ifdef FULLHD
	width = 1920;
	height = 1080;
#else
	width = 1280;
	height = 720;
#endif
#ifdef BPP32
	fourcc = V4L2_PIX_FMT_BGR32;
	bpl = width * 4;
#else
	fourcc = V4L2_PIX_FMT_RGB565;
	bpl = width * 2;
#endif

	{
		int (*s_fmt)(struct file *file, void *fh, struct v4l2_format *f);
		s_fmt = data->vfd->ioctl_ops->vidioc_s_fmt_vid_out_mplane;

		fmt.fmt.pix_mp.width = width;
		fmt.fmt.pix_mp.height = height;
		fmt.fmt.pix_mp.pixelformat = fourcc;
		fmt.fmt.pix_mp.plane_fmt[0].bytesperline = bpl;

		ret = s_fmt(&data->fake_file, data->fake_file.private_data, &fmt);
		if (ret) {
			printk (KERN_WARNING "%s cant s_fmt\n", __func__);
			goto err;
		}
	}
    
	/*
	 * Set DV preset
	 */

	{
		struct v4l2_dv_preset preset;

		int (*s_dv_preset)(struct file *file, void *fh,
				   struct v4l2_dv_preset *preset);
		s_dv_preset = data->vfd->ioctl_ops->vidioc_s_dv_preset;

#ifdef FULLHD
		preset.preset = V4L2_DV_1080P60;
#else
		preset.preset = V4L2_DV_720P59_94;
#endif

		ret = s_dv_preset(&data->fake_file, data->fake_file.private_data, &preset);

		if (ret) {
			printk (KERN_WARNING "%s cant set dv preset (%d)\n", __func__, ret);
		}
	}
#endif

	dprintk(3, "fb emu: width %d height %d fourcc %08x size %d bpl %d\n",
		width, height, fourcc, size, bpl);

	/*
	 * Find format mapping with fourcc returned by g_fmt().
	 */
	for (i = 0; i < ARRAY_SIZE(fmt_conv_table); i++) {
		if (fmt_conv_table[i].fourcc == fourcc) {
			conv = &fmt_conv_table[i];
			break;
		}
	}

	if (conv == NULL) {
		printk (KERN_WARNING "%s no conv for %d\n", __func__, fourcc);
		ret = -EBUSY;
		goto err;
	}

	/*
	 * Request buffers and use MMAP type to force driver
	 * to allocate buffers by itself.
	 */
	data->req.count = 2;
	data->req.memory = V4L2_MEMORY_USERPTR;
	data->req.type = q->type;
	ret = vb2_reqbufs(q, &data->req);
	if (ret) {
		printk (KERN_WARNING "%s cant reqbufs\n", __func__);
		goto err;
	}

	/*
	 * Check if plane_count is correct,
	 * multiplane buffers are not supported.
	 */
	if (q->bufs[0]->num_planes != 1) {
		printk (KERN_WARNING "%s num planes != 1 (%d)\n", __func__, q->bufs[0]->num_planes);
		data->req.count = 0;
		ret = -EBUSY;
		goto err;
	}

	data->size = size = vb2_plane_size(q->bufs[0], 0);

	/*
	 * Clear the buffer
	 */
	memset(data->vaddr, 0, size);

	/*
	 * Setup framebuffer parameters
	 */
	info->screen_base = data->vaddr;
	info->screen_size = size;
	info->fix.line_length = bpl;
	info->fix.ypanstep = 1;
	info->fix.ywrapstep = 1;
	info->fix.smem_len = info->fix.mmio_len = FXIFB_SIZE;
	info->fix.smem_start = (unsigned long)data->videophys;

	var = &info->var;
	var->xres = var->xres_virtual = var->width = width;
	var->yres = var->yres_virtual = var->height = height;
#ifdef DOUBLE_BUFFERING
	var->yres_virtual *= 2;
#endif
	var->bits_per_pixel = conv->bits_per_pixel;
	var->red = conv->red;
	var->green = conv->green;
	var->blue = conv->blue;
	var->transp = conv->transp;
	var->pixclock = 60000000000000LLU;

	return 0;

err:
	data->vfd->fops->release(&data->fake_file);
	return ret;
}

/**
 * fxifb_deactivate() - deactivate framebuffer emulator
 * @info:	framebuffer vb2 emulator data
 * Stop displaying video data and close framebuffer emulator.
 */
static int fxifb_deactivate(struct fb_info *info)
{
	struct fxifb_data *data = info->par;

	info->screen_base = NULL;
	info->screen_size = 0;
	data->blank = 1;
	data->streaming = 0;

	fxifb_stop(info);
	return data->vfd->fops->release(&data->fake_file);
}

/**
 * fxifb_start() - start displaying the video buffer
 * @info:	framebuffer vb2 emulator data
 * This function queues video buffer to the driver and starts streaming.
 */
static int fxifb_start(struct fb_info *info)
{
	struct fxifb_data *data = info->par;
	struct v4l2_buffer *b0 = &data->b[0];
	struct v4l2_buffer *b1 = &data->b[1];
	struct v4l2_plane *p0 = &data->p[0];
	struct v4l2_plane *p1 = &data->p[1];
	struct vb2_queue *q = data->q;
	int ret;

	if (data->streaming)
		return 0;

	/*
	 * Prepare the buffer and queue it.
	 */
	memset(b0, 0, sizeof(*b0));
	b0->type = q->type;
	b0->memory = q->memory;
	b0->index = 0;

	memset(b1, 0, sizeof(*b1));
	b1->type = q->type;
	b1->memory = q->memory;
	b1->index = 1;

	{ // set VM_IO flag
		// TODO: how to do this properly?
		struct vm_area_struct *vma;
		struct mm_struct *mm = current->mm;

		if (!mm) {
			printk (KERN_WARNING "cant find mm\n");
			return -EINVAL;
		}

		down_read(&mm->mmap_sem);
		vma = find_vma (mm, (unsigned long)data->vaddr);
		if (!vma) {
			printk (KERN_WARNING "cant find VMA\n");
			up_read(&mm->mmap_sem);
			return -EINVAL;
		}
		vma->vm_flags |= VM_IO;
		up_read(&mm->mmap_sem);
	}

	if (b0->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		b0->bytesused = data->size;
		b0->length = data->size;
		b0->m.userptr = (unsigned long)data->vaddr;

		b1->bytesused = data->size;
		b1->length = data->size;
		b1->m.userptr = (unsigned long)data->vaddr + data->size;
	} else if (b0->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		memset(p0, 0, sizeof(*p0));
		b0->m.planes = p0;
		b0->length = 1;
		p0->bytesused = data->size;
		p0->length = data->size;
		p0->m.userptr = (unsigned long)data->vaddr;

		memset(p1, 0, sizeof(*p1));
		b1->m.planes = p1;
		b1->length = 1;
		p1->bytesused = data->size;
		p1->length = data->size;
		p1->m.userptr = (unsigned long)data->vaddr + data->size;
	}
	ret = vb2_qbuf(q, b0);
#ifdef DOUBLE_BUFFERING
	ret += vb2_qbuf(q, b1);
#endif
	if (ret)
		return ret;

	/*
	 * Start streaming.
	 */
	ret = vb2_streamon(q, q->type);
	if (ret == 0) {
		data->streaming = 1;
		dprintk(3, "fb emu: enabled streaming\n");
	}
	return ret;
}

/**
 * fxifb_start() - stop displaying video buffer
 * @info:	framebuffer vb2 emulator data
 * This function stops streaming on the video driver.
 */
static int fxifb_stop(struct fb_info *info)
{
	struct fxifb_data *data = info->par;
	struct vb2_queue *q = data->q;
	int ret = 0;

	if (data->streaming) {
		ret = vb2_streamoff(q, q->type);
		data->streaming = 0;
		dprintk(3, "fb emu: disabled streaming\n");
	}

	return ret;
}

/**
 * fxifb_open() - open method for emulated framebuffer
 * @info:	framebuffer vb2 emulator data
 * @user:	client type (0 means kernel, 1 mean userspace)
 */
static int fxifb_open(struct fb_info *info, int user)
{
	struct fxifb_data *data = info->par;
	int ret = 0;
	dprintk(3, "fb emu: open()\n");

	/*
	 * Reject open() call from fb console.
	 */
	if (user == 0)
		return -ENODEV;

	vb2_drv_lock(data->q);

	/*
	 * Activate emulation on the first open.
	 */
	if (data->refcount == 0)
		ret = fxifb_activate(info);

	if (ret == 0)
		data->refcount++;

	vb2_drv_unlock(data->q);

	/*
	 * Start emulation
	 */
	if (data->blank) {
		ret = fxifb_start(info);
		if (ret == 0)
			data->blank = 0;
	}

	return ret;
}

/**
 * fxifb_release() - release method for emulated framebuffer
 * @info:	framebuffer vb2 emulator data
 * @user:	client type (0 means kernel, 1 mean userspace)
 */
static int fxifb_release(struct fb_info *info, int user)
{
	struct fxifb_data *data = info->par;
	int ret = 0;

	dprintk(3, "fb emu: release()\n");

	vb2_drv_lock(data->q);

	if (--data->refcount == 0)
		ret = fxifb_deactivate(info);

	vb2_drv_unlock(data->q);

	return ret;
}

/**
 * fxifb_blank() - blank method for emulated framebuffer
 * @blank_mode:	requested blank method
 * @info:	framebuffer vb2 emulator data
 */
static int fxifb_blank(int blank_mode, struct fb_info *info)
{
	struct fxifb_data *data = info->par;
	int ret = -EBUSY;

	dprintk(3, "fb emu: blank mode %d, blank %d, streaming %d\n",
		blank_mode, data->blank, data->streaming);

	/*
	 * If no blank mode change then return immediately
	 */
	if ((data->blank && blank_mode != FB_BLANK_UNBLANK) ||
	    (!data->blank && blank_mode == FB_BLANK_UNBLANK))
		return 0;

	/*
	 * Currently blank works only if device has been opened first.
	 */
	if (!data->refcount)
		return -EBUSY;

	vb2_drv_lock(data->q);

	/*
	 * Start emulation if user requested mode == FB_BLANK_UNBLANK.
	 */
	if (blank_mode == FB_BLANK_UNBLANK && data->blank) {
		ret = fxifb_start(info);
		if (ret == 0)
			data->blank = 0;
	}

	/*
	 * Stop emulation if user requested mode != FB_BLANK_UNBLANK.
	 */
	if (blank_mode != FB_BLANK_UNBLANK && !data->blank) {
		ret = fxifb_stop(info);
		if (ret == 0)
			data->blank = 1;
	}

	vb2_drv_unlock(data->q);

	return ret;
}

/**
 * fxifb_setcolreg() -
 * @regno:	
 * @red:	
 */
static int fxifb_setcolreg(unsigned regno, unsigned red, unsigned green, 
			   unsigned blue, unsigned transp, struct fb_info *fbi)
{
	dev_vdbg(fbi->dev, "%s\n", __func__);

	/* Nothing to do */
	return 0;
}

/**
 * fxifb_setcmap() -
 * @cmap:	
 * @fbi:	
 */
static int fxifb_setcmap(struct fb_cmap *cmap, struct fb_info *fbi)
{
	dev_vdbg(fbi->dev, "%s\n", __func__);

	/* Nothing to do */
	return 0;
}

static int fxifb_wait_for_vsync(struct fb_info *info, u32 crtc)
{
	unsigned long count;
	int ret;

	if (crtc != 0)
		return -ENODEV;

#if 0
	count = sfb->vsync_info.count;
	s3c_fb_enable_irq(sfb);
	ret = wait_event_interruptible_timeout(sfb->vsync_info.wait,
				       count != sfb->vsync_info.count,
				       msecs_to_jiffies(VSYNC_TIMEOUT_MSEC));
	if (ret == 0)
		return -ETIMEDOUT;
#endif
	return 0;
}

#define FXIFB_GET_FB_UMP_SECURE_ID 1074022094
#define TEST_310 _IOWR('m', 310, unsigned int)

/**
 * fxifb_ioctl() -
 * @fbi:	
 * @cmd:
 * @arg:
 */
static int fxifb_ioctl(struct fb_info *info, unsigned int cmd,
		       unsigned long arg)
{
	struct fxifb_data *data = info->par;
	u32 crtc;
	int ret = 0;
		
	dev_vdbg(info->dev, "%s\n", __func__);

	switch (cmd) {
	case FXIFB_GET_FB_UMP_SECURE_ID:
	{
		u32 __user *psecureid = (u32 __user *) arg;
		ump_secure_id secure_id;

		secure_id = ump_dd_secure_id_get(data->ump_wrapped_buffer);

		ret = put_user((unsigned int)secure_id, psecureid);
		break;
	}
  case FBIO_WAITFORVSYNC:
	{
    if (get_user(crtc, (u32 __user *)arg)) {
//      ret = -EFAULT;
      break;
    }

    ret = fxifb_wait_for_vsync(info, crtc);
    break;
	}
	default:
	{
		ret =  -EINVAL;
		break;
	}
	}

	/* Unsupported / Invalid ioctl op */
	return ret;
}

#ifdef DOUBLE_BUFFERING
static int fxifb_pan_display(struct fb_var_screeninfo *var,
            struct fb_info *info)
{
  unsigned long offset;
  struct fxifb_data *data = info->par;
  struct v4l2_buffer *b0 = &data->b[0];
  struct v4l2_buffer *b1 = &data->b[1];
  struct vb2_queue *q = data->q;

	offset = info->var.xres * (info->var.bits_per_pixel >> 3) * var->yoffset;

	if (offset != 0) {
		vb2_qbuf(q, b1);
		vb2_dqbuf(q, b0, 0);
	}
	else {
		vb2_qbuf(q, b0);
		vb2_dqbuf(q, b1, 0);
	}
  return 0;
}
#endif

static struct fb_ops fxifb_ops = {
	.owner		= THIS_MODULE,
	.fb_open	= fxifb_open,
	.fb_release	= fxifb_release,
	.fb_blank	= fxifb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_setcolreg	= fxifb_setcolreg,
	.fb_setcmap	= fxifb_setcmap,
	.fb_ioctl	= fxifb_ioctl,
#ifdef DOUBLE_BUFFERING
	.fb_pan_display = fxifb_pan_display,
#endif
};

/**
 * vb2_fb_reqister() - register framebuffer emulation
 * @q:		videobuf2 queue
 * @vfd:	video node
 * This function registers framebuffer emulation for specified
 * videobuf2 queue and video node. It returns a pointer to the registered
 * framebuffer device.
 */
void *vb2_fb_register(struct vb2_queue *q, struct video_device *vfd)
{
	struct fxifb_data *data;
	struct fb_info *info;
	int ret;

	BUG_ON(q->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	       q->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	BUG_ON(!q->mem_ops->vaddr);
	BUG_ON(!q->ops->wait_prepare || !q->ops->wait_finish);
	BUG_ON(!vfd->ioctl_ops || !vfd->fops);

	if (!try_module_get(vfd->fops->owner)) {
		return ERR_PTR(-ENODEV);
	}

	info = framebuffer_alloc(sizeof(struct fxifb_data), &vfd->dev);
	if (!info) {
		return ERR_PTR(-ENOMEM);
	}

	data = info->par;

	{ // get mem
		// FIXME: Should get memory from mach_fxi_c210.c, not
		// just from hard coded values
		data->videophys = (void*)FXIFB_BASE + FXIFB_SIZE *
			num_allocated++; // FIXME
		data->memres =
			request_mem_region((unsigned long)data->videophys,
					   FXIFB_SIZE, "fxifb");
		if (data->memres == NULL) {
			printk (KERN_ERR "fxifb: cannot request FXIFB mem\n");
			return ERR_PTR (-ENXIO);
		}

		data->vaddr =
			ioremap((unsigned long)data->videophys, FXIFB_SIZE);
		if (data->vaddr == NULL) {
			printk (KERN_ERR "fxifb: cannot map IO\n");
			return ERR_PTR (-ENXIO);
		}

		memset(data->vaddr, 0, FXIFB_SIZE);
	}  

	{ // mali ump
		ump_dd_physical_block ump_memory_description;

		ump_memory_description.addr = (unsigned long) data->videophys;
		ump_memory_description.size = FXIFB_SIZE;
		data->ump_wrapped_buffer = ump_dd_handle_create_from_phys_blocks(&ump_memory_description, 1);
	}

	info->fix.type	= FB_TYPE_PACKED_PIXELS;
	info->fix.accel	= FB_ACCEL_NONE;
	info->fix.visual = FB_VISUAL_TRUECOLOR,
		info->var.activate = FB_ACTIVATE_NOW;
	info->var.vmode	= FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP;
	info->fbops = &fxifb_ops;
	info->flags = FBINFO_FLAG_DEFAULT;
	info->screen_base = NULL;

	ret = register_framebuffer(info);
	if (ret) {
		iounmap (data->vaddr);
		release_resource(data->memres);
		return ERR_PTR(ret);
	}

	printk(KERN_INFO "fb%d: registered frame buffer emulation for /dev/%s\n",
	       info->node, dev_name(&vfd->dev));

	data->blank = 1;
	data->vfd = vfd;
	data->q = q;
	data->fake_file.f_path.dentry = &data->fake_dentry;
	data->fake_dentry.d_inode = &data->fake_inode;
	data->fake_inode.i_rdev = vfd->cdev->dev;

	return info;
}
EXPORT_SYMBOL_GPL(vb2_fb_register);

/**
 * vb2_fb_unreqister() - unregister framebuffer emulation
 * @fb_emu:	emulated framebuffer device
 */
int vb2_fb_unregister(void *fb_emu)
{
	struct fb_info *info = fb_emu;
	struct fxifb_data *data = info->par;
	struct module *owner = data->vfd->fops->owner;

	unregister_framebuffer(info);
	module_put(owner);
	iounmap (data->vaddr);
	release_resource(data->memres);
	return 0;
}
EXPORT_SYMBOL_GPL(vb2_fb_unregister);

MODULE_DESCRIPTION("fxifb");
MODULE_LICENSE("GPL");
