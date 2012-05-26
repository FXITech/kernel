/*
 * Copyright (C) 2011 Insignal Co., Ltd.
 *
 * Author: Pan <pan@insginal.co.kr>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/module.h>

#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "../../../sound/soc/samsung/i2s.h"

static int set_epll_rate(unsigned long rate)
{
    struct clk *fout_epll;

    fout_epll = clk_get(NULL, "fout_epll");
    if (IS_ERR(fout_epll)) {
        printk(KERN_ERR "%s: failed to get fout_epll\n", __func__);
        return -ENOENT;
    }

    if (rate == clk_get_rate(fout_epll))
        goto out;

    clk_set_rate(fout_epll, rate);
out:
    clk_put(fout_epll);

    return 0;
}

static int fxi_hw_params(struct snd_pcm_substream *substream,
                struct snd_pcm_hw_params *params)
{
    struct snd_soc_pcm_runtime *rtd = substream->private_data;
    struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
    struct snd_soc_dai *codec_dai = rtd->codec_dai;
    int bfs, psr, rfs, ret;
    unsigned long rclk;
    printk(KERN_DEBUG "Entered %s", __func__);

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
        printk(KERN_DEBUG "%s Invalid param format.", __func__ );
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
    printk(KERN_DEBUG "%s Invalid param rate.", __func__ );
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
        printk(KERN_ERR "%s Not yet supported!\n", __func__);
        return -EINVAL;
    }

    set_epll_rate(rclk * psr);

    return 0;
}

static struct snd_soc_ops fxi_ops = {
    .hw_params = fxi_hw_params,
};

static struct snd_soc_dai_link fxi_dai[] = {
    {
        .name = "FXI Sound",
        .stream_name = "FXI Playback",
        .platform_name = "samsung-audio",
        .cpu_dai_name = "samsung-i2s.0",
        .codec_dai_name = "samsung-spdif",
        .codec_name = "spdif-dit",
        .ops = &fxi_ops,
    },
};

static struct snd_soc_card fxisnd = {
    .name = "FXI-SND",
    .owner = THIS_MODULE,
    .dai_link = fxi_dai,
    .num_links = ARRAY_SIZE(fxi_dai),
};

static struct platform_device *fxi_snd_device;
//static struct platform_device *fxi_snd_spdif_dit_device;


static int __init fxi_audio_init(void)
{
    int ret;

    fxi_snd_device = platform_device_alloc("soc-audio", -1);
    if (!fxi_snd_device){
        printk(KERN_DEBUG "%s couldnt alloc soc-audio\n", __func__);
        return -ENOMEM;
    }

    platform_set_drvdata(fxi_snd_device, &fxisnd);

    ret = platform_device_add(fxi_snd_device);
    if (ret)
        goto err;

    return ret;
err:
    platform_device_put(fxi_snd_device);
    return ret;
}
module_init(fxi_audio_init);

static void __exit fxi_audio_exit(void)
{
    platform_device_unregister(fxi_snd_device);
}
module_exit(fxi_audio_exit);

MODULE_AUTHOR("Pan, <pan@insignal.co.kr>");
MODULE_DESCRIPTION("ALSA SoC ORIGEN+ALC5625");
MODULE_LICENSE("GPL");
