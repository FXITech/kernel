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
#include "spdif.h"
#include "../../../sound/soc/samsung/i2s.h"

static int set_audio_clock_heirachy(struct platform_device *pdev)
{
    struct clk *fout_epll, *mout_epll, *sclk_audio0, *sclk_spdif;
    int ret = 0;

    fout_epll = clk_get(NULL, "fout_epll");
    if (IS_ERR(fout_epll)) {
        printk(KERN_WARNING "%s: Cannot find fout_epll.\n",
                __func__);
        return -EINVAL;
    }

    mout_epll = clk_get(NULL, "mout_epll");
    if (IS_ERR(mout_epll)) {
        printk(KERN_WARNING "%s: Cannot find mout_epll.\n",
                __func__);
        ret = -EINVAL;
        goto out1;
    }

    sclk_audio0 = clk_get(&pdev->dev, "sclk_audio");
    if (IS_ERR(sclk_audio0)) {
        printk(KERN_WARNING "%s: Cannot find sclk_audio.\n",
                __func__);
        ret = -EINVAL;
        goto out2;
    }

    sclk_spdif = clk_get(NULL, "sclk_spdif");
    if (IS_ERR(sclk_spdif)) {
        printk(KERN_WARNING "%s: Cannot find sclk_spdif.\n",
                __func__);
        ret = -EINVAL;
        goto out3;
    }

    /* Set audio clock hierarchy for S/PDIF */
    clk_set_parent(mout_epll, fout_epll);
    clk_set_parent(sclk_audio0, mout_epll);
    clk_set_parent(sclk_spdif, sclk_audio0);

    clk_put(sclk_spdif);
out3:
    clk_put(sclk_audio0);
out2:
    clk_put(mout_epll);
out1:
    clk_put(fout_epll);

    return ret;
}

/* We should haved to set clock directly on this part because of clock
 * scheme of Samsudng SoCs did not support to set rates from abstrct
 * clock of it's hierarchy.
 */
static int set_audio_clock_rate(unsigned long epll_rate,
                unsigned long audio_rate)
{
    struct clk *fout_epll, *sclk_spdif;

    fout_epll = clk_get(NULL, "fout_epll");
    if (IS_ERR(fout_epll)) {
        printk(KERN_ERR "%s: failed to get fout_epll\n", __func__);
        return -ENOENT;
    }

    clk_set_rate(fout_epll, epll_rate);
    clk_put(fout_epll);

    sclk_spdif = clk_get(NULL, "sclk_spdif");
    if (IS_ERR(sclk_spdif)) {
        printk(KERN_ERR "%s: failed to get sclk_spdif\n", __func__);
        return -ENOENT;
    }

    clk_set_rate(sclk_spdif, audio_rate);
    clk_put(sclk_spdif);

    return 0;
}

static int fxi_hw_params(struct snd_pcm_substream *substream,
        struct snd_pcm_hw_params *params)
{
    struct snd_soc_pcm_runtime *rtd = substream->private_data;
    struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
    unsigned long pll_out, rclk_rate;
    int ret, ratio;

    switch (params_rate(params)) {
    case 44100:
        pll_out = 45158400;
        break;
    case 32000:
    case 48000:
    case 96000:
        pll_out = 49152000;
        break;
    default:
        return -EINVAL;
    }

    /* Setting ratio to 512fs helps to use S/PDIF with HDMI without
     * modify S/PDIF ASoC machine driver.
     */
    ratio = 512;
    rclk_rate = params_rate(params) * ratio;

    /* Set audio source clock rates */
    ret = set_audio_clock_rate(pll_out, rclk_rate);
    if (ret < 0)
        return ret;

    /* Set S/PDIF uses internal source clock */
    printk(KERN_DEBUG "%s - rclk_rate = %d\n", __func__, rclk_rate);
    ret = snd_soc_dai_set_sysclk(cpu_dai, SND_SOC_SPDIF_INT_MCLK,
                    rclk_rate, SND_SOC_CLOCK_IN);
    if (ret < 0)
        return ret;

    return ret;
}

static struct snd_soc_ops fxi_ops = {
    .hw_params = fxi_hw_params,
};

static struct snd_soc_dai_link fxi_dai[] = {
    {
        .name = "FXI Sound",
        .stream_name = "FXI Playback",
        .platform_name = "samsung-audio",
        .cpu_dai_name = "samsung-spdif",
        .codec_dai_name = "dit-hifi",
        .codec_name = "spdif-dit",
        .ops = &fxi_ops,
    },
};
/*

*/
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

    /* Set audio clock hierarchy manually */
    ret = set_audio_clock_heirachy(fxi_snd_device);
    if (ret)
        goto err1;

    return ret;
err1:
    platform_device_del(fxi_snd_device);
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
