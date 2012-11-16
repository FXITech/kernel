#include <linux/module.h>
#include <linux/moduleparam.h>
#include <sound/soc.h>

#define DRIVER_NAME "ccandy-audio-codec"

static struct snd_soc_codec_driver ccandy_codec_driver;

static struct snd_soc_dai_driver ccandy_stub_dai_driver[] = {
	{
		.name = "ccandy-audio-capture",
		.id = 0,
		.capture = {
			.stream_name = "Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_44100,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
	},
};

static int ccandy_stub_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &ccandy_codec_driver,
				      ccandy_stub_dai_driver,
				      ARRAY_SIZE(ccandy_stub_dai_driver));
}

static int ccandy_stub_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver ccandy_stub_driver = {
	.probe = ccandy_stub_probe,
	.remove = ccandy_stub_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};

module_platform_driver(ccandy_stub_driver);

MODULE_AUTHOR("Frank Svendsboe");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
