/*
 * videobuf2-fb.c - FrameBuffer API emulator on top of Videobuf2 framework
 *
 * Copyright (C) 2011 Samsung Electronics
 *
 * Author: Marek Szyprowski <m.szyprowski@samsung.com>
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
#include <media/videobuf2-dma-contig.h>
#include <linux/fxifb.h>
#include <linux/dma-mapping.h>

#include <../drivers/gpu/arm/ump/include/ump_kernel_interface_ref_drv.h>

#include "s5p-tv/mixer.h"

#define BYTES_PER_PIXEL 4
#define MAX_BUFFER_NUM 3
#define FXIFB_GET_FB_UMP_SECURE_ID_0      _IOWR('m', 310, unsigned int)
#define FXIFB_GET_FB_UMP_SECURE_ID_1      _IOWR('m', 311, unsigned int)
#define FXIFB_GET_FB_UMP_SECURE_ID_2      _IOWR('m', 312, unsigned int)
static ump_dd_handle       ump_wrapped_buffer[MAX_BUFFER_NUM];

static int debug = 1;
module_param(debug, int, 0644);

#define dprintk(level, fmt, arg...)					\
	do {								\
		if (debug >= level)					\
			printk(KERN_DEBUG "vb2: " fmt, ## arg);		\
	} while (0)

struct vb2_fb_data {
	struct video_device *vfd;
	struct vb2_queue *q;
	struct device *dev;
	struct v4l2_requestbuffers req;
	struct v4l2_buffer b;
	struct v4l2_plane p;
	void *vaddr;
	unsigned int size;
	int refcount;
	int blank;
	int streaming;

	struct file fake_file;
	struct dentry fake_dentry;
	struct inode fake_inode;
};

static int vb2_fb_stop(struct fb_info *info);

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

static struct fxifb_platform_data fxi_fb_default_pdata = {
	.xres = 1920,
	.yres = 1080,
	.virtual_x = 1920,
	.virtual_y = 1080,
	.max_bpp = 32,
};

static int fxifb_unmap_video_memory(struct device *dev, struct fb_info *fbi)
{
	struct fb_fix_screeninfo *fix = &fbi->fix;

	if (fix->smem_start) {

		dma_free_coherent(dev, fix->smem_len, fbi->screen_base, fix->smem_start);

		fix->smem_start = 0;
		fix->smem_len = 0;
		printk(KERN_DEBUG "fxifb: video memory released\n");
	}
	return 0;
}

static int fxifb_ump_wrapper(struct fb_info *info, int id)
{
	ump_dd_physical_block ump_memory_description;
	unsigned int buffer_size;
	buffer_size = info->fix.smem_len;
	ump_memory_description.addr = info->fix.smem_start;
	ump_memory_description.size = buffer_size;
	ump_wrapped_buffer[id] = ump_dd_handle_create_from_phys_blocks(&ump_memory_description, 1);
	return 0;
}

static int fxi_fb_alloc_memory(struct device *dev, struct fxifb_platform_data *fxiplat, struct fb_info *fbi)
{
#if 0
	unsigned int size;
	dma_addr_t map_dma;
	struct vb2_fb_data *data;

	data = fbi->par;

	printk(KERN_DEBUG "fxifb: allocating memory for display for dev\n");

	size = fxiplat->virtual_y * fxiplat->virtual_x * BYTES_PER_PIXEL;
	printk(KERN_DEBUG "fxifb: size = %x\n", size);

	fbi->fix.smem_len = size;
	printk(KERN_DEBUG "fxifb: want %u bytes for window, actually getting: %u\n",
	       fbi->fix.smem_len, PAGE_ALIGN(fbi->fix.smem_len));
	dev->coherent_dma_mask = ~0L;
	data->vaddr = dma_alloc_writecombine(dev,
						 PAGE_ALIGN(fbi->fix.smem_len),
						 &map_dma, GFP_KERNEL);



	printk(KERN_DEBUG "fxifb: Screen_base %x", (unsigned int)data->vaddr);
	if (!data->vaddr)
		return -ENOMEM;
	else
		fbi->fix.smem_start = map_dma;
		printk(KERN_DEBUG "[fxifb] dma: 0x%08x, cpu: 0x%08x, size: 0x%08x\n",
			 (unsigned int)fbi->fix.smem_start,
			 (unsigned int)data->vaddr, fbi->fix.smem_len);

	memset(data->vaddr, 0, fbi->fix.smem_len);

	/* Setup UMP for Mali */
	if (fxifb_ump_wrapper(fbi, fbi->node)) {
		printk(KERN_DEBUG "[fxib] : Wrapped UMP memory : %x\n", (unsigned int)ump_wrapped_buffer);
		fxifb_unmap_video_memory(dev, fbi);
		return 0;
	}
#endif
	return 0;
}

static int fxifb_wait_for_vsync(struct fb_info *info, u32 crtc)
{
	struct vb2_fb_data *data = info->par;
	struct vb2_queue *q = data->q;
	struct mxr_layer *layer = vb2_get_drv_priv(q);
	struct mxr_device *mdev = layer->mdev;

	if (crtc != 0)
		return -ENODEV;

	if (mxr_reg_wait4vsync(mdev) < 0)
		return -ETIMEDOUT;

	return 0;
}
static int fxifb_ioctl(struct fb_info *info, unsigned int cmd,
		       unsigned long arg)
{
	u32 crtc;
	int ret = 0;

	switch (cmd) {
#if 0
	case FXIFB_GET_FB_UMP_SECURE_ID_0: {
		u32 __user *psecureid = (u32 __user *) arg;
		ump_secure_id secure_id;

		printk(KERN_DEBUG "fxifb: ump_dd_secure_id_get_0\n");
		secure_id = ump_dd_secure_id_get(ump_wrapped_buffer[0]);
		printk(KERN_DEBUG "fxifb: Saving secure id 0x%x in userptr %p\n", (unsigned int)secure_id, psecureid);
		printk(KERN_DEBUG "fxifb: Saving secure id 0x%x in userptr %p\n", (unsigned int)secure_id, psecureid);
		return put_user((unsigned int)secure_id, psecureid);
	}

	case FXIFB_GET_FB_UMP_SECURE_ID_1: {
		u32 __user *psecureid = (u32 __user *) arg;
		ump_secure_id secure_id = 0;

		printk(KERN_DEBUG "fxifb: ump_dd_secure_id_get_1\n");
		secure_id = ump_dd_secure_id_get(ump_wrapped_buffer[1]);
		printk(KERN_DEBUG "fxifb: Saving secure id 0x%x in userptr %p\n", (unsigned int)secure_id, psecureid);
		printk(KERN_DEBUG "fxifb: Saving secure id 0x%x in userptr %p\n", (unsigned int)secure_id, psecureid);
		return put_user((unsigned int)secure_id, psecureid);
	}

	case FXIFB_GET_FB_UMP_SECURE_ID_2: {
		u32 __user *psecureid = (u32 __user *) arg;
		ump_secure_id secure_id = 0;

		printk(KERN_DEBUG "fxifb: ump_dd_secure_id_get_2\n");
		secure_id = ump_dd_secure_id_get(ump_wrapped_buffer[2]);
		printk(KERN_DEBUG "fxifb: Saving secure id 0x%x in userptr %p\n", (unsigned int)secure_id, psecureid);
		printk(KERN_DEBUG "fxifb: Saving secure id 0x%x in userptr %p\n", (unsigned int)secure_id, psecureid);
		return put_user((unsigned int)secure_id, psecureid);
	}
#endif

	case FBIO_WAITFORVSYNC:
		if (get_user(crtc, (u32 __user *)arg)) {
			ret = -EFAULT;
			break;
		}

		ret = fxifb_wait_for_vsync(info, crtc);
		break;

	default:
		/* Unsupported / Invalid ioctl op */
		printk(KERN_ERR "fxifb ioctl command: %x\n", cmd);
		ret =  -EINVAL;
		break;
	}

	return ret;
}

static int fxifb_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	struct vb2_fb_data *data = info->par;
	struct vb2_queue *q = data->q;
	struct mxr_layer *layer = vb2_get_drv_priv(q);
	struct mxr_device *mdev = layer->mdev;

	dma_addr_t addr = vb2_dma_contig_plane_dma_addr(q->bufs[0], 0);

	if (var->yoffset)
		addr += var->yoffset * info->fix.line_length;

	// TODO(havardk): This should only take effect after vsync,
	// but we still get tearing?  It would be cleaner if we could
	// queue two different buffers.
	mxr_reg_set_graph_base(mdev, 0, addr);

	return 0;
}

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

/**
 * vb2_fb_activate() - activate framebuffer emulator
 * @info:	framebuffer vb2 emulator data
 * This function activates framebuffer emulator. The pixel format
 * is acquired from video node, memory is allocated and framebuffer
 * structures are filled with valid data.
 */
static int vb2_fb_activate(struct fb_info *info)
{
	struct vb2_fb_data *data = info->par;
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
	if (q->streaming || q->num_buffers > 0)
		return -EBUSY;

	dprintk(3, "setting up framebuffer\n");

	/*
	 * Open video node.
	 */
	ret = data->vfd->fops->open(&data->fake_file);
	if (ret)
		return ret;

	/*
	 * Get format from the video node.
	 */
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = q->type;
	if (data->vfd->ioctl_ops->vidioc_g_fmt_vid_out) {
		g_fmt = data->vfd->ioctl_ops->vidioc_g_fmt_vid_out;
		ret = g_fmt(&data->fake_file, data->fake_file.private_data, &fmt);
		if (ret)
			goto err;
		width = fmt.fmt.pix.width;
		height = fmt.fmt.pix.height;
		fourcc = fmt.fmt.pix.pixelformat;
		bpl = fmt.fmt.pix.bytesperline;
		size = fmt.fmt.pix.sizeimage;
	} else if (data->vfd->ioctl_ops->vidioc_g_fmt_vid_out_mplane) {
		g_fmt = data->vfd->ioctl_ops->vidioc_g_fmt_vid_out_mplane;
		ret = g_fmt(&data->fake_file, data->fake_file.private_data, &fmt);
		if (ret)
			goto err;
		width = fmt.fmt.pix_mp.width;
		height = fmt.fmt.pix_mp.height;
		fourcc = fmt.fmt.pix_mp.pixelformat;
		bpl = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
		size = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
	} else {
		ret = -EINVAL;
		goto err;
	}

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
		ret = -EBUSY;
		goto err;
	}

	/*
	 * Request buffers and use MMAP type to force driver
	 * to allocate buffers by itself.
	 */
	data->req.count = 1;
	data->req.memory = V4L2_MEMORY_MMAP;
	data->req.type = q->type;
	ret = vb2_reqbufs(q, &data->req);
	if (ret)
		goto err;

	/*
	 * Check if plane_count is correct,
	 * multiplane buffers are not supported.
	 */
	if (q->bufs[0]->num_planes != 1) {
		data->req.count = 0;
		ret = -EBUSY;
		goto err;
	}

	/*
	 * Get kernel address of the buffer.
	 */
	data->vaddr = vb2_plane_vaddr(q->bufs[0], 0);
	if (data->vaddr == NULL) {
		ret = -EINVAL;
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
	info->fix.smem_start = vb2_dma_contig_plane_dma_addr(q->bufs[0], 0);
	info->fix.smem_len = info->fix.mmio_len = size;

	var = &info->var;
	var->xres = var->xres_virtual = var->width = width;
	var->yres = var->height = height;
	var->yres_virtual = var->yres * 2;
	var->bits_per_pixel = conv->bits_per_pixel;
	var->red = conv->red;
	var->green = conv->green;
	var->blue = conv->blue;
	var->transp = conv->transp;

	printk(KERN_DEBUG "Activating framebuffer res: %u:%u; bps: %u; size: %u\n",
	       var->xres, var->yres, var->bits_per_pixel, size);

	return 0;

err:
	data->vfd->fops->release(&data->fake_file);
	return ret;
}

/**
 * vb2_fb_deactivate() - deactivate framebuffer emulator
 * @info:	framebuffer vb2 emulator data
 * Stop displaying video data and close framebuffer emulator.
 */
static int vb2_fb_deactivate(struct fb_info *info)
{
	struct vb2_fb_data *data = info->par;

	info->screen_base = NULL;
	info->screen_size = 0;
	data->blank = 1;
	data->streaming = 0;

	vb2_fb_stop(info);
	return data->vfd->fops->release(&data->fake_file);
}

/**
 * vb2_fb_start() - start displaying the video buffer
 * @info:	framebuffer vb2 emulator data
 * This function queues video buffer to the driver and starts streaming.
 */
static int vb2_fb_start(struct fb_info *info)
{
	struct vb2_fb_data *data = info->par;
	struct v4l2_buffer *b = &data->b;
	struct v4l2_plane *p = &data->p;
	struct vb2_queue *q = data->q;
	int ret;

	if (data->streaming)
		return 0;

	/*
	 * Prepare the buffer and queue it.
	 */
	memset(b, 0, sizeof(*b));
	b->type = q->type;
	b->memory = q->memory;
	b->index = 0;

	if (b->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		b->bytesused = data->size;
		b->length = data->size;
	} else if (b->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		memset(p, 0, sizeof(*p));
		b->m.planes = p;
		b->length = 1;
		p->bytesused = data->size;
		p->length = data->size;
	}
	ret = vb2_qbuf(q, b);
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
 * vb2_fb_start() - stop displaying video buffer
 * @info:	framebuffer vb2 emulator data
 * This function stops streaming on the video driver.
 */
static int vb2_fb_stop(struct fb_info *info)
{
	struct vb2_fb_data *data = info->par;
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
 * vb2_fb_open() - open method for emulated framebuffer
 * @info:	framebuffer vb2 emulator data
 * @user:	client type (0 means kernel, 1 mean userspace)
 */
static int vb2_fb_open(struct fb_info *info, int user)
{
	struct vb2_fb_data *data = info->par;
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
		ret = vb2_fb_activate(info);

	if (ret == 0)
		data->refcount++;

	vb2_drv_unlock(data->q);

	/*
         * Start emulation
         */
	if (data->blank) {
		ret = vb2_fb_start(info);
		if (ret == 0)
			data->blank = 0;
	}

	return ret;
}

/**
 * vb2_fb_release() - release method for emulated framebuffer
 * @info:	framebuffer vb2 emulator data
 * @user:	client type (0 means kernel, 1 mean userspace)
 */
static int vb2_fb_release(struct fb_info *info, int user)
{
	struct vb2_fb_data *data = info->par;
	int ret = 0;

	dprintk(3, "fb emu: release()\n");

	vb2_drv_lock(data->q);

	if (--data->refcount == 0)
		ret = vb2_fb_deactivate(info);

	vb2_drv_unlock(data->q);

	return ret;
}

/**
 * vb2_fb_mmap() - mmap method for emulated framebuffer
 * @info:	framebuffer vb2 emulator data
 * @vma:	memory area to map
 */
static int vb2_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct vb2_fb_data *data = info->par;
	int ret = 0;

	dprintk(3, "fb emu: mmap offset %ld\n", vma->vm_pgoff);

	/*
	 * Add flags required by v4l2/vb2
	 */
	vma->vm_flags |= VM_SHARED;

	/*
	 * Only the most common case (mapping the whole framebuffer) is
	 * supported for now.
	 */
	if (vma->vm_pgoff != 0 || (vma->vm_end - vma->vm_start) < data->size)
		return -EINVAL;

	vb2_drv_lock(data->q);
	ret = vb2_mmap(data->q, vma);
	vb2_drv_unlock(data->q);

	return ret;
}

/**
 * vb2_fb_blank() - blank method for emulated framebuffer
 * @blank_mode:	requested blank method
 * @info:	framebuffer vb2 emulator data
 */
static int vb2_fb_blank(int blank_mode, struct fb_info *info)
{
	struct vb2_fb_data *data = info->par;
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
		ret = vb2_fb_start(info);
		if (ret == 0)
			data->blank = 0;
	}

	/*
	 * Stop emulation if user requested mode != FB_BLANK_UNBLANK.
	 */
	if (blank_mode != FB_BLANK_UNBLANK && !data->blank) {
		ret = vb2_fb_stop(info);
		if (ret == 0)
			data->blank = 1;
	}

	vb2_drv_unlock(data->q);

	return ret;
}

static struct fb_ops vb2_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_open	= vb2_fb_open,
	.fb_release	= vb2_fb_release,
	.fb_mmap	= vb2_fb_mmap,
	.fb_blank	= vb2_fb_blank,
	.fb_pan_display	= fxifb_pan_display,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_ioctl 	= fxifb_ioctl,
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
	struct vb2_fb_data *data;
	struct fb_info *info;
	int ret;

	BUG_ON(q->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	     q->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	BUG_ON(!q->mem_ops->vaddr);
	BUG_ON(!q->ops->wait_prepare || !q->ops->wait_finish);
	BUG_ON(!vfd->ioctl_ops || !vfd->fops);

	if (!try_module_get(vfd->fops->owner))
		return ERR_PTR(-ENODEV);

	info = framebuffer_alloc(sizeof(struct vb2_fb_data), &vfd->dev);
	if (!info)
		return ERR_PTR(-ENOMEM);

	data = info->par;

	info->fix.type	= FB_TYPE_PACKED_PIXELS;
	info->fix.accel	= FB_ACCEL_NONE;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.ypanstep = 1;
	info->var.activate = FB_ACTIVATE_NOW;
	info->var.vmode	= FB_VMODE_NONINTERLACED;
	info->fbops = &vb2_fb_ops;
	info->flags = FBINFO_FLAG_DEFAULT;
	info->screen_base = NULL;

	printk(KERN_INFO "fb%d: calling: fxi_fb_alloc_memory\n",
	       info->node);

	fxi_fb_alloc_memory(&vfd->dev, &fxi_fb_default_pdata, info);

	ret = register_framebuffer(info);
	if (ret)
		return ERR_PTR(ret);

	printk(KERN_INFO "fb%d: registered frame buffer emulation for /dev/%s\n",
	       info->node, dev_name(&vfd->dev));

	data->blank = 1;
	data->vfd = vfd;
	data->q = q;
	data->fake_file.f_path.dentry = &data->fake_dentry;
	data->fake_dentry.d_inode = &data->fake_inode;
	data->fake_inode.i_rdev = vfd->cdev->dev;

	printk(KERN_INFO "fb driver init: x: %d; y: %d; bpp: %d\n",
	       info->var.xres, info->var.yres, info->var.bits_per_pixel);


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
	struct vb2_fb_data *data = info->par;
	struct module *owner = data->vfd->fops->owner;

	unregister_framebuffer(info);
	module_put(owner);
	return 0;
}
EXPORT_SYMBOL_GPL(vb2_fb_unregister);

MODULE_DESCRIPTION("FrameBuffer emulator for Videobuf2 and Video for Linux 2");
MODULE_AUTHOR("Marek Szyprowski");
MODULE_LICENSE("GPL");
