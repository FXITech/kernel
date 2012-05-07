/*
 * Platform device data for FXI Framebuffer Device
 *
 * Copyright 2012 FXI Technologies A/S.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __FXIFB_H__
#define __FXIFB_H__

#include <linux/types.h>
#include <linux/fb.h>

/* ramebuffer driver platform data struct */
struct fxifb_platform_data {
	u32 rotate_screen;	/* Flag to rotate display 180 degrees */
	u32 screen_height_mm;	/* Physical dimensions of screen in mm */
	u32 screen_width_mm;
	u32 xres, yres;		/* resolution of screen in pixels */
	u32 virtual_x, virtual_y;	/* resolution of memory buffer */
    u32 max_bpp;
    struct fb_info *fbinfo;
	/* Physical address of framebuffer memory */
	u32 fb_phys;
};

#endif  /* __FXIFB_H__ */
