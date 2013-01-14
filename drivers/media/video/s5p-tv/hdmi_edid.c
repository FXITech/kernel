/*
 * Copy of drm_edid.c
 *
 * Copyright (c) 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 * Copyright 2010 Red Hat, Inc.
 *
 * EDID read implementation copied from Samsung's Android HAL
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/export.h>
#include <drm/drm_edid.h>

#define version_greater(edid, maj, min) \
	(((edid)->version > (maj)) || \
	 ((edid)->version == (maj) && (edid)->revision > (min)))

#define EDID_EST_TIMINGS 16
#define EDID_STD_TIMINGS 8
#define EDID_DETAILED_TIMINGS 4

/*
 * EDID blocks out in the wild have a variety of bugs, try to collect
 * them here (note that userspace may work around broken monitors first,
 * but fixes should make their way here so that the kernel "just works"
 * on as many displays as possible).
 */

/* First detailed mode wrong, use largest 60Hz mode */
#define EDID_QUIRK_PREFER_LARGE_60		(1 << 0)
/* Reported 135MHz pixel clock is too high, needs adjustment */
#define EDID_QUIRK_135_CLOCK_TOO_HIGH		(1 << 1)
/* Prefer the largest mode at 75 Hz */
#define EDID_QUIRK_PREFER_LARGE_75		(1 << 2)
/* Detail timing is in cm not mm */
#define EDID_QUIRK_DETAILED_IN_CM		(1 << 3)
/* Detailed timing descriptors have bogus size values, so just take the
 * maximum size and use that.
 */
#define EDID_QUIRK_DETAILED_USE_MAXIMUM_SIZE	(1 << 4)
/* Monitor forgot to set the first detailed is preferred bit. */
#define EDID_QUIRK_FIRST_DETAILED_PREFERRED	(1 << 5)
/* use +hsync +vsync for detailed mode */
#define EDID_QUIRK_DETAILED_SYNC_PP		(1 << 6)

#define LEVEL_DMT	0
#define LEVEL_GTF	1
#define LEVEL_GTF2	2
#define LEVEL_CVT	3

/*** DDC fetch and block validation ***/

static const u8 edid_header[] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
};

/*
 * Sanity check the header of the base EDID block.  Return 8 if the header
 * is perfect, down to 0 if it's totally wrong.
 */
int hdmi_edid_header_is_valid(const u8 *raw_edid)
{
	int i, score = 0;

	for (i = 0; i < sizeof(edid_header); i++)
		if (raw_edid[i] == edid_header[i])
			score++;

	return score;
}

/*
 * Sanity check the EDID block (base or extension).  Return 0 if the block
 * doesn't check out, or 1 if it's valid.
 */
bool hdmi_edid_block_valid(u8 *raw_edid)
{
	int i;
	u8 csum = 0;
	struct edid *edid = (struct edid *) raw_edid;

	if (raw_edid[0] == 0x00) {
		int score = hdmi_edid_header_is_valid(raw_edid);

		if (score != 8) {
			if (score >= 6) {
				printk(KERN_ERR
				       "Fixing EDID header, your hardware may be failing\n");
				memcpy(raw_edid, edid_header,
				       sizeof(edid_header));
			} else {
				goto bad;
			}
		}
	}

	for (i = 0; i < EDID_LENGTH; i++)
		csum += raw_edid[i];
	if (csum) {
		printk(KERN_ERR
		       "EDID checksum is invalid, remainder is %d\n",
		       csum);

		/* allow CEA to slide through, switches mangle this */
		if (raw_edid[0] != 0x02)
			goto bad;
	}

	/* per-block-type checks */
	switch (raw_edid[0]) {
	case 0:		/* base */
		if (edid->version != 1) {
			printk(KERN_ERR
			       "EDID has major version %d, instead of 1\n",
			       edid->version);
			goto bad;
		}

		if (edid->revision > 4)
			printk(KERN_DEBUG
			       "EDID minor > 4, assuming backward compatibility\n");
		break;

	default:
		break;
	}

	return 1;

bad:
	if (raw_edid) {
		printk(KERN_ERR "Raw EDID:\n");
		print_hex_dump(KERN_ERR, " \t", DUMP_PREFIX_NONE, 16, 1,
			       raw_edid, EDID_LENGTH, false);
	}
	return 0;
}

/**
 * hdmi_edid_is_valid - sanity check EDID data
 * @edid: EDID data
 *
 * Sanity-check an entire EDID record (including extensions)
 */
bool hdmi_edid_is_valid(struct edid *edid)
{
	int i;
	u8 *raw = (u8 *) edid;

	if (!edid)
		return false;

	for (i = 0; i <= edid->extensions; i++)
		if (!hdmi_edid_block_valid(raw + i * EDID_LENGTH))
			return false;

	return true;
}

#define DDC_SEGMENT_ADDR 0x30
/**
 * Get EDID information via I2C.
 *
 * \param buf     : EDID data buffer to be filled
 * \param len     : EDID data buffer length
 * \return 0 on success or -1 on failure.
 *
 * Try to fetch EDID information by calling i2c driver function.
 */
static int
hdmi_do_probe_ddc_edid(struct i2c_adapter *adapter, unsigned char *buf,
		       int block, int len)
{
	unsigned char start = block * EDID_LENGTH;
	unsigned char segment = block >> 1;
	int ret, retries = 5;

	/* The core i2c driver will automatically retry the transfer if the
	 * adapter reports EAGAIN. However, we find that bit-banging transfers
	 * are susceptible to errors under a heavily loaded machine and
	 * generate spurious NAKs and timeouts. Retrying the transfer
	 * of the individual block a few times seems to overcome this.
	 */
	do {
		struct i2c_msg msgs[] = {
			{	/*set segment pointer */
				.addr = DDC_SEGMENT_ADDR,
				.flags = segment ? 0 : I2C_M_IGNORE_NAK,
				.len = 1,
				.buf = &start,
			}, {	/*set offset */
				 .addr = DDC_ADDR,
				.flags = 0,
				.len = 1,
				.buf = &start,
			}, {	/*set data */
				.addr = DDC_ADDR,
				.flags = I2C_M_RD,
				.len = len,
				.buf = buf,
			}
		};

		ret = i2c_transfer(adapter, msgs, 3);

		if (ret == -ENXIO) {
			printk(KERN_ERR
				"drm: skipping non-existent adapter %s\n",
				adapter->name);
			break;
		}
	} while (ret != 3 && --retries);

	return ret == 3 ? 0 : -1;
}

static bool hdmi_edid_is_zero(u8 *in_edid, int length)
{
	int i;
	for (i = 0; i < length; i++)
		if (*(in_edid+i) != 0)
			return false;

	return true;
}

static u8 *hdmi_do_get_edid(struct i2c_adapter *adapter)
{
	int i, j = 0, valid_extensions = 0;
	u8 *block, *new;

	if ((block = kmalloc(EDID_LENGTH, GFP_KERNEL)) == NULL)
		return NULL;

	/* base block fetch */
	for (i = 0; i < 4; i++) {
		if (hdmi_do_probe_ddc_edid(adapter, block, 0, EDID_LENGTH))
			goto out;

		printk(KERN_DEBUG "EDID base block fetch successful %d\n", i);

		if (hdmi_edid_block_valid(block))
			break;

		printk(KERN_ERR "EDID base block invalid %d\n", i);
		if (i == 0 && hdmi_edid_is_zero(block, EDID_LENGTH)) {
			goto carp;
		}
	}
	if (i == 4)
		goto carp;

	/* if there's no extensions, we're done */
	if (block[0x7e] == 0)
		return block;

	new = krealloc(block, (block[0x7e] + 1) * EDID_LENGTH, GFP_KERNEL);
	if (!new)
		goto out;
	block = new;

	for (j = 1; j <= block[0x7e]; j++) {
		for (i = 0; i < 4; i++) {
			if (hdmi_do_probe_ddc_edid(adapter,
						   block +
						   (valid_extensions +
						    1) * EDID_LENGTH, j,
						   EDID_LENGTH))
				goto out;
			if (hdmi_edid_block_valid(block
						+ (valid_extensions + 1)
						* EDID_LENGTH)) {
				valid_extensions++;
				break;
			}
		}
		if (i == 4)
			printk(KERN_WARNING
			       "Ignoring invalid EDID block %d\n", j);
	}

	if (valid_extensions != block[0x7e]) {
		block[EDID_LENGTH - 1] += block[0x7e] - valid_extensions;
		block[0x7e] = valid_extensions;
		new =
		    krealloc(block, (valid_extensions + 1) * EDID_LENGTH,
			     GFP_KERNEL);
		if (!new)
			goto out;
		block = new;
	}

	return block;

carp:
	printk(KERN_WARNING "EDID block %d invalid.\n", j);

out:
	kfree(block);
	return NULL;
}

/**
 * Probe DDC presence.
 *
 * \return 1 on success
 */
static bool hdmi_probe_ddc(struct i2c_adapter *adapter)
{
	unsigned char out;

	printk(KERN_DEBUG "DDC Probe requested!\n");

	return (hdmi_do_probe_ddc_edid(adapter, &out, 0, 1) == 0);
}


/**
 * hdmi_get_edid - get EDID data, if available
 *
 * Poke the given i2c channel to grab EDID data if possible
 *
 * Return edid data or NULL if we couldn't find any.
 */
struct edid *hdmi_get_edid()
{
	struct edid *edid = NULL;

	struct i2c_adapter *adapter = i2c_get_adapter(1);
	if (adapter == NULL) {
		printk(KERN_ERR "No Adapter!\n");
		return NULL;
	}

	printk(KERN_DEBUG "Request to get EDID!\n");
	if (hdmi_probe_ddc(adapter))
		edid = (struct edid *) hdmi_do_get_edid(adapter);
	else
		printk(KERN_ERR "DDC Adapter missing!\n");
	printk(KERN_DEBUG "End request EDID!\n");
	return edid;
}

u8 *hdmi_edid_find_cea_extension(struct edid *edid)
{
	u8 *edid_ext = NULL;
	int i;

	/* No EDID or EDID extensions */
	if (edid == NULL || edid->extensions == 0)
		return NULL;

	/* Find CEA extension */
	for (i = 0; i < edid->extensions; i++) {
		edid_ext = (u8 *) edid + EDID_LENGTH * (i + 1);
		if (edid_ext[0] == CEA_EXT)
			break;
	}

	if (i == edid->extensions)
		return NULL;

	return edid_ext;
}
