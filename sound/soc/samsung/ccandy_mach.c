/*
 * Copyright (C) 2012 FXI Technologies AS
 *
 * The playback components are copied from the smdk_i2s_stub.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */


#include <linux/clk.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "i2s.h"
#include "ccandy.h"

#define MAX_MSGS_IN_CAPTURE_QUEUE 20

static snd_pcm_uframes_t
ccandy_play_pointer(struct snd_pcm_substream *substream)
{
	snd_pcm_uframes_t position;
	struct snd_soc_pcm_runtime *rt = substream->private_data;
	struct snd_soc_codec *codec = rt->codec;
	struct snd_soc_card *card = codec->card;
	struct ccandy_device *ccdev = snd_soc_card_get_drvdata(card);
	unsigned long flags;

	if (!ccdev->play_pointer)
		return 0;

	position = ccdev->play_pointer(substream);

	if (ccdev->capture_activated) {
		u32 offset, steps;
		int i;

		/* Check if playback position has changed since
		   last call to pointer */
		if (ccdev->previous_playback_position == position)
			return position;

		offset = frames_to_bytes(substream->runtime,
					 ccdev->previous_playback_position);

		steps = frames_to_bytes(substream->runtime,
				       substream->runtime->period_size) /
			ccdev->pcm_period_size;

		pr_debug("%s (playback pcm size = %d, "
			"playback position = %d\n,"
			"prev position = %d\n,"
			"substeps = %d, sub msg size = %d)\n",
			__func__,
			frames_to_bytes(substream->runtime,
					substream->runtime->period_size),
			(u32) position,
			(u32) ccdev->previous_playback_position,
			steps, ccdev->pcm_period_size);

		/* Divide pcm frame into 'step' subpackets.. */
		for(i = 0; i < steps; i++) {
			struct pcm_msg *msg;
			struct list_head *dummy;
			u32 end, pos;
			size_t count = 0;

			msg = kmalloc(sizeof(struct pcm_msg), GFP_KERNEL);
			msg->size = ccdev->pcm_period_size;
			msg->data = kmalloc(msg->size, GFP_KERNEL);

			/* Check for wrap here */
			end = frames_to_bytes(substream->runtime,
					substream->runtime->buffer_size);
			pos = offset + ccdev->pcm_period_size*i;
			pos %= end;
			memcpy(msg->data,
				substream->runtime->dma_area + pos,
				msg->size);
			spin_lock_irqsave(&ccdev->lock, flags);
			list_for_each(dummy, &ccdev->capture_q)
				count++;
			if (count < MAX_MSGS_IN_CAPTURE_QUEUE)
				list_add_tail(&msg->qnode, &ccdev->capture_q);
			else
				dev_err(&ccdev->dev_ccandy_audio_plat->dev,
					"The capture queue is full\n"
					"Discarding pcm buffers\n");
			spin_unlock_irqrestore(&ccdev->lock, flags);
		}
	}
	ccdev->previous_playback_position = position;
	return position;
}

/* this is as-is from the smdk_i2s_stub.c
   i'll either export these, or extract them and share them with our
   driver
*/
static int set_epll_rate(unsigned long rate)
{
	struct clk *fout_epll;

	fout_epll = clk_get(NULL, "fout_epll");
	if (IS_ERR(fout_epll)) {
		pr_err("%s: failed to get fout_epll\n", __func__);
		return -ENOENT;
	}

	if (rate == clk_get_rate(fout_epll))
		goto out;

	clk_set_rate(fout_epll, rate);
out:
	clk_put(fout_epll);

	return 0;
}


/* this is also extracted from smdk_i2s_stub.c */
static int smdk_hw_params(struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int bfs, psr, rfs, ret;
	unsigned long rclk;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_U24:
	case SNDRV_PCM_FORMAT_S24:
		bfs = 48;
		break;
	case SNDRV_PCM_FORMAT_U16_LE:
	case SNDRV_PCM_FORMAT_S16_LE:
		bfs = 32;
		break;
	default:
		return -EINVAL;
	}

	switch (params_rate(params)) {
	case 16000:
	case 22050:
	case 24000:
	case 32000:
	case 44100:
	case 48000:
	case 88200:
	case 96000:
		rfs = (bfs == 48) ? 384 : 256;
		break;
	case 64000:
		rfs = 384;
		break;
	case 8000:
	case 11025:
	case 12000:
		rfs = (bfs == 48) ? 768 : 512;
		break;
	default:
		return -EINVAL;
	}

	rclk = params_rate(params) * rfs;

	switch (rclk) {
	case 4096000:
	case 5644800:
	case 6144000:
	case 8467200:
	case 9216000:
		psr = 8;
		break;
	case 8192000:
	case 11289600:
	case 12288000:
	case 16934400:
	case 18432000:
		psr = 4;
		break;
	case 22579200:
	case 24576000:
	case 33868800:
	case 36864000:
		psr = 2;
		break;
	case 67737600:
	case 73728000:
		psr = 1;
		break;
	default:
		pr_err("Not yet supported!\n");
		return -EINVAL;
	}

	set_epll_rate(rclk * psr);

	/* Set the AP DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
				  SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_CDCLK, rfs,
				     SND_SOC_CLOCK_OUT);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_clkdiv(cpu_dai, SAMSUNG_I2S_DIV_BCLK, bfs);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops smdk_ops = {
	.hw_params = smdk_hw_params,
};

static int smdk_init_paiftx(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_dapm_sync(dapm);

	return 0;
}

static struct snd_soc_dai_link cotton_dai[] = {
	[0] = {
		.name = "I2S",
		.stream_name = "Playback",
		.codec_name = "i2s-stub",
		.platform_name = "samsung-audio",
		.cpu_dai_name = "samsung-i2s.0",
		.codec_dai_name = "i2s-stub-hifi",
		.init = smdk_init_paiftx,
		.ops = &smdk_ops,
	},
	[1] = {
		.name = "Digital Loopback",
		.stream_name = "Capture",
		.platform_name = "ccandy-audio-plat",
		.cpu_dai_name = "ccandy-cpu-dai",
		.codec_name = "ccandy-audio-codec",
		.codec_dai_name = "ccandy-audio-capture",
	},
};

static struct snd_soc_card cottoncandy_card = {
	.name = "cottoncandy",
	.owner = THIS_MODULE,
	.dai_link = cotton_dai,
	.num_links = ARRAY_SIZE(cotton_dai),
};

static int __devinit cottoncandy_probe(struct platform_device *pdev)
{
	struct ccandy_device *ccdev;
	int ret = 0;

	ccdev = kzalloc(sizeof(struct ccandy_device), GFP_KERNEL);
	if (!ccdev)
		return -ENOMEM;

	ccdev->dev_i2s_stub = platform_device_alloc("i2s-stub", -1);
	if (!ccdev->dev_i2s_stub) {
		pr_err("Failed to alloc i2s-stub device\n");
		return -ENOMEM;
	}

	ret = platform_device_add(ccdev->dev_i2s_stub);
	if (ret != 0) {
		pr_err("Failed to add i2s_stub device\n");
		return -ENODEV;
	}

	ccdev->dev_ccandy_audio_codec =
		platform_device_alloc("ccandy-audio-codec", -1);
	if (!ccdev->dev_ccandy_audio_codec) {
		pr_err("Failed to alloc ccandy-audio-codec device\n");
		return -ENOMEM;
	}

	ret = platform_device_add(ccdev->dev_ccandy_audio_codec);
	if (ret != 0) {
		pr_err("Failed to add ccandy-audio-codec device\n");
		return -ENODEV;
	}

	ccdev->dev_ccandy_audio_plat =
		platform_device_alloc("ccandy-audio-plat", -1);
	if (!ccdev->dev_ccandy_audio_plat) {
		pr_err("Failed to alloc ccandy-audio-plat device\n");
		return -ENOMEM;
	}

	ret = platform_device_add(ccdev->dev_ccandy_audio_plat);
	if (ret != 0) {
		pr_err("Failed to add ccandy-audio-plat device\n");
		return -ENODEV;
	}

	cottoncandy_card.dev = &pdev->dev;

	ccdev->pcm_buffer_size = 0;
	ccdev->pcm_period_size = 0;
	ccdev->last_jiffies = jiffies;
	ccdev->buf_pos = 0;
	ccdev->running = 0;
	ccdev->capture_activated = 0;
	ccdev->previous_playback_position = 0;
	ccdev->previous_q_count = 0;
	ccdev->timeout_adjust = 0;
	ccdev->extra_bytes = 0;
	ccdev->play_pointer = NULL;

	spin_lock_init(&ccdev->lock);
	INIT_LIST_HEAD(&ccdev->capture_q);

	ret = snd_soc_register_card(&cottoncandy_card);
	if (ret != 0) {
		dev_err(&pdev->dev, "%s failed with %d\n", __func__, ret);
		return ret;
	}

	snd_soc_card_set_drvdata(&cottoncandy_card, ccdev);

	/* Copy the original dma_pointer function pointer,
	   set ours as the new callback, and use the stored
	   function pointer in ccandy_play_pointer */
	ccdev->play_pointer = cottoncandy_card.rtd[0].ops.pointer;
	cottoncandy_card.rtd[0].ops.pointer = ccandy_play_pointer;

	return 0;
}

static int __devexit cottoncandy_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	snd_soc_unregister_card(card);
	return 0;
}

#define COTTONCANDY_AUDIO_DRIVER "ccandy-audio"

static struct platform_driver ccandy_audio_driver = {
	.driver = {
		.name = COTTONCANDY_AUDIO_DRIVER,
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,

	},
	.probe = cottoncandy_probe,
	.remove = __devexit_p(cottoncandy_remove),
};

module_platform_driver(ccandy_audio_driver);

MODULE_AUTHOR("Frank Svendsboe");
MODULE_DESCRIPTION("CottonCandy I2S audio output and loopback capture driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:cottoncandy-audio");
