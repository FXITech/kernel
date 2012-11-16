#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>

#include <sound/soc.h>
#include <sound/pcm_params.h>

#include <asm/dma.h>
#include <mach/hardware.h>
#include <mach/dma.h>

#include "ccandy.h"

/* export the same functionality as the samsung dma driver */
static const struct snd_pcm_hardware pcm_dummy_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
	SNDRV_PCM_INFO_BLOCK_TRANSFER |
	SNDRV_PCM_INFO_MMAP |
	SNDRV_PCM_INFO_MMAP_VALID,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.channels_min		= 2,
	.channels_max		= 2,
	.buffer_bytes_max	= 128*1024,
	.period_bytes_min	= PAGE_SIZE,
	.period_bytes_max	= PAGE_SIZE*2,
	.periods_min		= 2,
	.periods_max		= 128,
	.fifo_size		= 32,
};

/* A helper used to retrieve our driver structure

   Since we need to have access to the capture stream from
   the playback device, we've stored our private data in the
   card instance (not the runtime->private_data as usual)

*/
struct ccandy_device* get_drvdata(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *runtime = substream->private_data;
	/* Get to card instance via the runtime->codec */
	struct snd_soc_card *card = runtime->codec->card;

	/* We've assigned our private data in card->drvdata */
	return snd_soc_card_get_drvdata(card);
}

static int ccandy_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params)
{
	pr_info("%s\n", __func__);
	return snd_pcm_lib_alloc_vmalloc_buffer(substream,
						params_buffer_bytes(params));
}

static int ccandy_hw_free(struct snd_pcm_substream *substream)
{
	pr_info("%s\n", __func__);
	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int ccandy_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ccandy_device *dev = get_drvdata(substream);
	dev->buf_pos = 0;
	dev->pcm_buffer_size = frames_to_bytes(runtime, runtime->buffer_size);
	dev->pcm_period_size = frames_to_bytes(runtime, runtime->period_size);

	dev->sample_width = snd_pcm_format_width(runtime->format) *
		runtime->channels;
	dev->sample_width /= 8;
	dev->rate = runtime->rate;
	dev->bps = dev->sample_width * runtime->rate;

	dev->last_jiffies = jiffies;
	dev->running = 0;

	snd_pcm_format_set_silence(runtime->format, runtime->dma_area,
				   runtime->buffer_size * runtime->channels);

	pr_info("%s, bps = %d, pcm_buffer_size = %d, pcm_period_size = %d\n",
		__func__, dev->bps, dev->pcm_buffer_size, dev->pcm_period_size);
	dev->capture_activated = 1;
	return 0;
}

static u32 jiffies_per_period(struct ccandy_device *dev)
{
	const u32 msecs_per_jiffie = jiffies_to_msecs(1);
	u32 period_size = bytes_to_frames(dev->substream->runtime,
					  dev->pcm_period_size);
	u32 rate = dev->rate;
	u32 msecs_pr_period = (period_size * 1000) / rate;

	return msecs_pr_period / msecs_per_jiffie;
}

static void ccandy_timer_start(struct ccandy_device *dev)
{
	const u32 jifs_per_period = jiffies_per_period(dev) + 1;
	s32 timeout = jifs_per_period - dev->timeout_adjust;

	dev->timer.expires = jiffies + timeout;
	pr_debug("%s (setting up timer to timeout @ %d jiffies from now)\n",
		 __func__, timeout);
	add_timer(&dev->timer);
}

static void ccandy_timer_stop(struct ccandy_device *dev)
{
	pr_info("%s\n", __func__);
	del_timer(&dev->timer);
}

static int ccandy_trigger(struct snd_pcm_substream *substream, int cmd)
{
	int ret = 0;
	struct ccandy_device *dev = get_drvdata(substream);

	pr_info("%s\n", __func__);

	switch (cmd)
	{
	case SNDRV_PCM_TRIGGER_START:
		if (!dev->running) {
			dev->last_jiffies = jiffies;
			ccandy_timer_start(dev);
		}
		dev->running |= (1 << substream->stream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		dev->running &= ~(1 << substream->stream);
		if (!dev->running) {
			dev->capture_activated = 0;
			ccandy_timer_stop(dev);
		}
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static long jiffies_elapsed(struct ccandy_device* dev)
{
	if (jiffies >= dev->last_jiffies)
		return jiffies - dev->last_jiffies;
	else  /* jiffies counter has wrapped around */
		return 0; /* ok for our use */
}

static unsigned long fill_buf(unsigned long pos, unsigned long end,
			      size_t size, void *dst, void *src)
{
	unsigned long left_to_end, left_after_end;

	/* message fits between start and end */
	if (pos + size <= end) {
		memcpy(dst + pos, src, size);
		if (pos + size == end)
			return 0;
		return pos + size;
	}
	/* don't fit.. copy to end and continue at start */
	left_to_end = end - pos;
	left_after_end = pos + size - end;
	memcpy(dst + pos, src, left_to_end);
	memcpy(dst, src + left_to_end, left_after_end);

	/* return new current position */
	return left_after_end;
}

static unsigned long clear_buf(unsigned long pos, unsigned long end,
			       size_t size, void *dst)
{
	unsigned long left_to_end, left_after_end;

	/* message fits between start and end */
	if (pos + size <= end) {
		memset(dst + pos, 0, size);
		if (pos + size == end)
			return 0;
		return pos + size;
	}
	/* don't fit.. copy to end and continue at start */
	left_to_end = end - pos;
	left_after_end = pos + size - end;
	memset(dst + pos, 0, left_to_end);
	memset(dst, 0, left_after_end);

	/* return new current position */
	return left_after_end;
}

static void fill_capture_buf(struct ccandy_device *dev, struct pcm_msg *msg)
{
	dev->buf_pos = fill_buf(dev->buf_pos,
				dev->pcm_buffer_size,
				msg->size,
				dev->substream->runtime->dma_area,
				msg->data);
	pr_debug("%s (buf_pos = %d, msg->size = %d)\n",
		 __func__, dev->buf_pos, msg->size);
}


static void clear_capture_buf(struct ccandy_device *dev)
{
	dev->buf_pos = clear_buf(dev->buf_pos,
				 dev->pcm_buffer_size,
				 dev->pcm_period_size,
				 dev->substream->runtime->dma_area);
	pr_debug("%s (buf_pos = %d, size = %d)\n",
		 __func__, dev->buf_pos, dev->pcm_period_size);
}

/* Calc the equivalent number of bytes that should pass
   through the pcm layer since last pcm commit

   Problem: this number has to be exact as-is and will
   directly control the pcm rate.. and is why I adjust
   it using dev->extra_bytes
*/
static long bytes_elapsed_since_last_commit(struct ccandy_device *dev)
{
	long jiffs_elapsed = jiffies_elapsed(dev);
	long msecs_elapsed = jiffies_to_msecs(jiffs_elapsed);
	long num_bytes = (dev->bps * msecs_elapsed) / 1000;

	pr_debug("%s (msecs elapsed = %lu, "
		"jiffies elapsed = %lu, bytes = %lu)\n",
		 __func__, msecs_elapsed, jiffs_elapsed, num_bytes);

	return num_bytes + dev->extra_bytes;
}

static long position_update(struct ccandy_device *dev)
{
	unsigned long flags;

	struct pcm_msg *msg;
	long num_bytes = bytes_elapsed_since_last_commit(dev);
	size_t qcount;
	struct list_head *dummy;

	/* We haven't waited long enough, so just return */
	if (num_bytes < dev->pcm_period_size)
		return num_bytes;

	qcount = 0;
	list_for_each(dummy, &dev->capture_q)
		qcount++;

	/* No data captured, generate silence */
	if (!qcount && !dev->previous_q_count) {
		clear_capture_buf(dev);
		dev->previous_q_count = qcount;
		return num_bytes;
	}

	pr_debug("%s (capture q count = %d, prev = %d)\n",
		 __func__, qcount, dev->previous_q_count);
	dev->previous_q_count = qcount;

	/* We've got no data, but still don't want to generate silence..
	   Most probably there will be in the next pcm period */
	if (!qcount) {
		/* Here we return period_size - 1 to minimize the
		   timeout time for next call */
		return dev->pcm_period_size - 1;
	}

	/* Get captured playback data */
	spin_lock_irqsave(&dev->lock, flags);
	{
		struct list_head *next;
		if (list_empty(&dev->capture_q)) {
			pr_err("Capture queue is empty. It shouldn't be.\n");
			msg = NULL;
			spin_unlock_irqrestore(&dev->lock, flags);
			return 0;
		}

		next = dev->capture_q.next;
		list_del_init(next);
		msg = list_entry(next, struct pcm_msg, qnode);
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	if (msg->size != dev->pcm_period_size) {
		pr_err("ALERT! Wrong period size setup");
		return 0;
	}

	fill_capture_buf(dev, msg);

	spin_lock_irqsave(&dev->lock, flags);
	list_del(&msg->qnode);
	kfree(msg->data);
	kfree(msg);
	spin_unlock_irqrestore(&dev->lock, flags);

	return num_bytes;
}

static snd_pcm_uframes_t ccandy_pointer(struct snd_pcm_substream *substream)
{
	struct ccandy_device *dev = get_drvdata(substream);
	snd_pcm_uframes_t position = bytes_to_frames(dev->substream->runtime,
						     dev->buf_pos);
	pr_debug("%s (frame position = %d)\n", __func__, (u32) position);
	return position;
}


static void ccandy_timer_callback(unsigned long data)
{
	struct ccandy_device *dev = (struct ccandy_device *) data;
	long num;

	if (!dev->running)
		return;

	if (!dev->capture_activated)
		return;

	/* If there's data in the queue, position_update will implicitly
	   copy the playback buffer to the capture buffer and move the
	   capture pointer. It returns the number of bytes that have been
	   processed. If this is larger than the capture pcm_period_size, we
	   should initiate a new capture pointer request by calling
	   snd_pcm_period_elasped. Due to our coarse calculation of number
	   of jiffies per pcm period, and the fact that we only have integer
	   jiffie resolution, we compensate for both the timer setup (via
	   timeout_adjust) and the bytes_elapsed estimate
	   (via dev->extra_bytes)'
	*/
	num = position_update(dev);
	if (num >= dev->pcm_period_size)
	{
		long extra_bytes = num - dev->pcm_period_size;
		long rate = dev->rate;
		long msecs = bytes_to_frames(dev->substream->runtime,
					     extra_bytes) * 1000 / rate;
		dev->extra_bytes = extra_bytes;
		dev->timeout_adjust = msecs_to_jiffies(msecs);
		dev->last_jiffies = jiffies;

		pr_debug("%s (timeout_adjust = %lu)\n",
			 __func__, dev->timeout_adjust);

		/* Inform the pcm subsystem and let it call
		   our pointer position callback */
		snd_pcm_period_elapsed(dev->substream);
	}

	ccandy_timer_start(dev);
}

static int ccandy_pcm_open(struct snd_pcm_substream *ss)
{
	struct ccandy_device *ccdev = get_drvdata(ss);
	snd_pcm_hw_constraint_integer(ss->runtime,
				      SNDRV_PCM_HW_PARAM_PERIODS);
	snd_soc_set_runtime_hwparams(ss, &pcm_dummy_hardware);

	ccdev->substream = ss;
	ccdev->capture_activated = 0;
	ccdev->running = 0;
	ccdev->buf_pos = 0;

	ccdev->previous_q_count = 0;
	ccdev->timeout_adjust = 0;
	ccdev->extra_bytes = 0;
	setup_timer(&ccdev->timer, ccandy_timer_callback,
		    (unsigned long) ccdev);
	pr_info("%s\n", __func__);
	return 0;
}

static int ccandy_pcm_close(struct snd_pcm_substream *ss)
{
	struct pcm_msg *msg, *tmp;
	unsigned long flags;
	struct ccandy_device *ccdev = get_drvdata(ss);

	/* Should be stopped already, but just in case .. */
	ccandy_timer_stop(ccdev);

	/* Disable capture and empty the queue */
	ccdev->capture_activated = 0;
	spin_lock_irqsave(&ccdev->lock, flags);
	list_for_each_entry_safe(msg, tmp, &ccdev->capture_q, qnode) {
		list_del(&msg->qnode);
		kfree(msg->data);
		kfree(msg);
	}
	spin_unlock_irqrestore(&ccdev->lock, flags);

	pr_info("%s\n", __func__);
	return 0;
}


static struct snd_pcm_ops ccandy_ops = {
	.open = ccandy_pcm_open,
	.close = ccandy_pcm_close,
	.hw_params = ccandy_hw_params,
	.hw_free = ccandy_hw_free,
	.ioctl = snd_pcm_lib_ioctl,
	.prepare = ccandy_prepare,
	.trigger = ccandy_trigger,
	.pointer = ccandy_pointer,
	.page = snd_pcm_lib_get_vmalloc_page,
	.mmap = snd_pcm_lib_mmap_vmalloc,
};

static struct snd_soc_platform_driver ccandy_platform = {
	.ops = &ccandy_ops,
};

static struct snd_soc_dai_driver ccandy_platform_dai[] = {
	{
		.name = "ccandy-cpu-dai",
		.id = 0,
		.capture = {
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_44100,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
	},
};

static int __devinit ccandy_platform_probe(struct platform_device *pdev)
{
	int ret = snd_soc_register_platform(&pdev->dev, &ccandy_platform);
	if (ret != 0) {
		pr_err("registering soc platform failed\n");
		return ret;
	}

	ret = snd_soc_register_dais(&pdev->dev,
				    ccandy_platform_dai,
				    ARRAY_SIZE(ccandy_platform_dai));
	if (ret) {
		pr_err("registering cpu dais failed\n");
		snd_soc_unregister_platform(&pdev->dev);
	}

	return ret;
}

static int __devexit ccandy_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

#define CCANDY_PLATFORM_DRIVER_NAME "ccandy-audio-plat"

static struct platform_driver ccandy_platform_driver = {
	.driver = {
		.name = CCANDY_PLATFORM_DRIVER_NAME,
		.owner = THIS_MODULE,
	},

	.probe = ccandy_platform_probe,
	.remove = __devexit_p(ccandy_platform_remove),
};

module_platform_driver(ccandy_platform_driver);

MODULE_AUTHOR("Frank Svendsboe");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" CCANDY_PLATFORM_DRIVER_NAME);
