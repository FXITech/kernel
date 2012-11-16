#ifndef _CCANDY_AUDIO_H
#define _CCANDY_AUDIO_H

#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/list.h>
#include <sound/pcm.h>

struct ccandy_device {
	struct snd_soc_card *soc_card;
	struct platform_device *dev_i2s_stub;
	struct platform_device *dev_ccandy_audio_codec;
	struct platform_device *dev_ccandy_audio_plat;
	struct snd_soc_pcm *pcm;
	struct timer_list timer;
	struct snd_pcm_substream *substream;

	u8 capture_activated;
	u8 running;
	u32 pcm_buffer_size;
	u32 pcm_period_size;
	u32 buf_pos;
	u32 sample_width;
	u32 bps;
	u32 rate;

	unsigned long last_jiffies;
	snd_pcm_uframes_t previous_playback_position;
	snd_pcm_uframes_t (*play_pointer)(struct snd_pcm_substream *substream);

	struct list_head capture_q;
	spinlock_t lock;

	u32 previous_q_count;

	long extra_bytes;
	long timeout_adjust;
};

struct pcm_msg {
	u16 size;
	struct list_head qnode;
	char *data;
};

#endif
