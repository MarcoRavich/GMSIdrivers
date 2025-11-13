/*
 *  Copyright (c) by Uros Bizjak <uros@kss-loka.si>
 *  PCM routines for SAM9407 chip
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#define __NO_VERSION__
#include <sound/driver.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sam9407.h>

//#include "testwave2.h"

#define chip_t sam9407_t
static int snd_sam_pcm_mixer_free(sam9407_t *sam, sam9407_voice_t *voice, snd_pcm_substream_t *substream);
static int snd_sam9407_pcm_mixer_build(sam9407_t *sam, sam9407_voice_t *voice, snd_pcm_substream_t *substream);
static void snd_sam9407_async_pcm_open(sam9407_t *sam, sam9407_voice_t * voice);

/*
 *  send control data to SAM9407
 *  and update control table
 * -> ctl_table_rwlock should be writelocked
 */
static int snd_sam9407_pcm_ctl_put(sam9407_t * sam, int chan, u8 ctl, u16 val)
{
	u8 data[3];
	int err;

	data[0] = chan;
	data[1] = val;
	data[2] = val >> 8;

	err = snd_sam9407_command(sam, NULL, ctl, &data, 3, NULL, 0, -1);
	if (err < 0) {
		snd_printk("sam9407: change PCM control failed, ctl = 0x%x\n", ctl);
		return -EIO;
	}
	return 0;
}

static int snd_sam9407_playback_hw_params(snd_pcm_substream_t * substream,
				  snd_pcm_hw_params_t * hw_params)
{
	sam9407_t *sam = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	sam9407_voice_t *voice = (sam9407_voice_t *) runtime->private_data;
	u16 id;

	int err;
	int words;
	id = SAM_MMT_ID_PCM | (voice->number << 8);

	printk(KERN_INFO " sam9407_pcm: built on " __TIME__ " " __DATE__ "\n");
	snd_printk("TEST (hw_params) voice %i\n", voice->number);

	if(test_bit(SAM_VOICE_PLAYING,&voice->status)) {
		snd_printk("voice %i already playing\n", voice->number);
		return -EAGAIN;
	}
	/* paranoia: release sam9407 buffer */
	if (voice->buf_addr) {
		snd_sam9407_mem_free(sam, id);
		voice->buf_addr=0;
	}

	/* the period size should be the rate at which interrupts are generated, so the SAM memory size should equal the
	period size.
	*/
	words=(params_period_bytes(hw_params)) + SAM_PLAY_OVERHEAD;

	snd_printk("allocating %d words of SAM memory to id %03X, periodsize = %d\n",words,id,params_period_bytes(hw_params));

 	if ((err = snd_sam9407_mem_alloc(sam, id, &voice->buf_addr, words, SAM_ALIGN_EVEN, SAM_BOUNDARY_MASK_64K))<0) {
		snd_printk("could not allocate SAM memory: %03X\n",id);
		return err;
	}

	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0) {
		snd_printk("could not allocate memory!\n");
		return err;
	}

	return 0;
}

static int snd_sam9407_playback_hw_free(snd_pcm_substream_t * substream)
{
	sam9407_t *sam = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	sam9407_voice_t *voice = (sam9407_voice_t *) runtime->private_data;

	signed long end_time;
	u16 id = SAM_MMT_ID_PCM | (voice->number << 8);

	snd_printk("TEST (hw_free) voice %i %p\n", voice->number, voice);

	/* wait here max 500ms for chip to finish playback */
	end_time = jiffies + (HZ );
	do {
		if(test_bit(SAM_VOICE_PLAYING, &voice->status)) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1);
		} else {
			goto __released;
		}
	} while (end_time - (signed long) jiffies >= 0);
	snd_printk("sam9407: voice %d close ACK timeout\n", voice->number);

 __released:

	snd_pcm_lib_free_pages(substream);

	/* release sam9407 buffer */
	if (voice->buf_addr) {
		snd_sam9407_mem_free(sam, id);
		voice->buf_addr=0;
	}

	snd_printk("TEST (hw_free leave) voice %i\n", voice->number);
	return 0;
}

static int snd_sam9407_playback_prepare(snd_pcm_substream_t * substream)
{
	sam9407_t *sam = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	sam9407_voice_t *voice = (sam9407_voice_t *) runtime->private_data;

	u8 format;
	u8 bytes[4];
	unsigned int rate=runtime->rate;

	// FIXME: workaround for the speed problem
	// I should the correct method to change the clock speed of the SAM
	// 0.6667 = 11.2896MHz/16.9334MHz
	// not that much of a problem since speeds up to 48000Hz can be played

	rate = (unsigned int)(((float)rate) * 0.6667);

	format = runtime->channels > 1 ? SAM_PCM_STEREO : SAM_PCM_MONO;
	format |= snd_pcm_format_width(runtime->format) > 8 ? SAM_PCM_16BIT : SAM_PCM_8BIT;

	snd_sam9407_async_pcm_open(sam, voice);

	bytes[0] = voice->number;
	bytes[1] = format;
	bytes[2] = (u8) rate;
	bytes[3] = (u8) (rate >> 8);

	snd_printk("sam9407_pcm: opening channel %d with parameters %02X %02X %02X\n",bytes[0],bytes[1],bytes[2],bytes[3]);
	/* send command but ignore ACK */
	snd_sam9407_command(sam, NULL, SAM_CMD_W_OPEN, bytes, 4, NULL, 0, -1);

	return 0;
}


static int snd_sam9407_trigger(snd_pcm_substream_t *substream, int cmd)
{
	//snd_printk("entering snd_sam9407_trigger\n");

	sam9407_t *sam = snd_pcm_substream_chip(substream);
	sam9407_voice_t *voice = (sam9407_voice_t *) substream->runtime->private_data;

	u8 channel = voice->number;
	u8 command;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_printk("Start TRIGGER\n");
		if(test_bit(SAM_VOICE_STOP_BUSY, &voice->status)) {
			return -EBUSY;
		}
		set_bit(SAM_VOICE_PLAYING, &voice->status);
		set_bit(SAM_VOICE_START_BUSY, &voice->status);
		command=SAM_CMD_W_START;
		snd_sam9407_command(sam, NULL, command,
			&channel, 1, NULL, 0, -1);


		break;
	case SNDRV_PCM_TRIGGER_STOP:
		snd_printk("Stop TRIGGER\n");
		if(test_bit(SAM_VOICE_START_BUSY, &voice->status)) {
			return -EBUSY;
		}
		set_bit(SAM_VOICE_STOP_BUSY, &voice->status);
		command = SAM_CMD_W_CLOSE;
		snd_sam9407_command(sam, NULL, command,
			&channel, 1, NULL, 0, -1);
		break;
	default:
		return -EINVAL;
	}

	/* HACK: sam9407 has no STOP trigger command. We send SAM_W_CLOSE
	   and wait for voice close ACK in irq handler */
	//snd_sam9407_command(sam, NULL, command, &channel, 1, NULL, 0, -1);

	//snd_printk("exiting snd_sam9407_trigger\n");
	return 0;
}

static snd_pcm_uframes_t snd_sam9407_pointer(snd_pcm_substream_t * substream)
{
	unsigned int ptr;
	sam9407_voice_t *voice = (sam9407_voice_t *) substream->runtime->private_data;

	//snd_printk("*> position request (%d) at %d for voice %p\n",voice->hwptr, jiffies,voice);
	//snd_printk("*> hw_ptr_base=%d,hw_ptr_interrupt =%d\n",substream->runtime->hw_ptr_base,substream->runtime->hw_ptr_interrupt);

	ptr=frames_to_bytes(substream->runtime,voice->hwptr);

	return bytes_to_frames(substream->runtime, ptr % substream->runtime->dma_bytes);

}

static snd_pcm_hardware_t snd_sam9407_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE  /* | SNDRV_PCM_FMTBIT_U8 */,
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		4000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	8*SAM_HW_BUFF_SIZE_BYTES,
	.period_bytes_min =	16,
	.period_bytes_max =	SAM_HW_BUFF_SIZE_BYTES,
	.periods_min =		2,
	.periods_max =		1024,
	.fifo_size =		0,
};


static snd_pcm_hardware_t snd_sam9407_capture =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE  /* | SNDRV_PCM_FMTBIT_U8 */,
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		4000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	8*SAM_HW_BUFF_SIZE_BYTES,
	.period_bytes_min =	SAM_HW_BUFF_SIZE_BYTES,
	.period_bytes_max =	SAM_HW_BUFF_SIZE_BYTES,
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};


static int __snd_sam9407_playback_open(snd_pcm_substream_t * substream, int voicenr)
{
	sam9407_t *sam = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	sam9407_voice_t *voice;

	/* Only voicenr 0 and 1 are playback channels */
	if (voicenr == 0 || voicenr == 1 ) {
		voice = &sam->voices[voicenr];
		voice->number=voicenr;
		clear_bit(SAM_VOICE_PLAYING, &voice->status);
		set_bit(SAM_VOICE_IN_USE, &voice->status);
		voice->pcm = 1;
		voice->hwptr = 0;

	} else {
		return -EAGAIN;
	}

	//snd_sam_pcm_mixer_build(sam, voice, substream);
	voice->substream = substream;
	runtime->private_data = voice;
	runtime->hw = snd_sam9407_playback;

#if 0
	/* FIXME: check this */
	snd_pcm_set_sync(substream);
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 0, 64*1024);
#endif

	return 0;
}

static int __snd_sam9407_playback_close(snd_pcm_substream_t * substream, int voicenr)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	sam9407_voice_t *voice = (sam9407_voice_t *) runtime->private_data;

	/* release PCM mixer element */
	//snd_sam9407_pcm_mixer_free(sam, voice, substream);


	if (voicenr == 0 || voicenr == 1) {
		voice->pcm = 0;
		clear_bit(SAM_VOICE_IN_USE, &voice->status);
	} else {
		return -EAGAIN;
	}

	return 0;
}

static int snd_sam9407_playback_open_12(snd_pcm_substream_t * substream)
{
	return __snd_sam9407_playback_open(substream, 0);
}

static int snd_sam9407_playback_close_12(snd_pcm_substream_t * substream)
{
	return __snd_sam9407_playback_close(substream, 0);
}

static int snd_sam9407_playback_open_34(snd_pcm_substream_t * substream)
{
	return __snd_sam9407_playback_open(substream, 1);
}

static int snd_sam9407_playback_close_34(snd_pcm_substream_t * substream)
{
	return __snd_sam9407_playback_close(substream, 1);
}

static snd_pcm_ops_t snd_sam9407_playback_ops_12 = {
	.open =			snd_sam9407_playback_open_12,
	.close =		snd_sam9407_playback_close_12,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_sam9407_playback_hw_params,
	.hw_free =		snd_sam9407_playback_hw_free,
	.prepare =		snd_sam9407_playback_prepare,
	.trigger =		snd_sam9407_trigger,
	.pointer =		snd_sam9407_pointer

};

static snd_pcm_ops_t snd_sam9407_playback_ops_34 = {
	.open =			snd_sam9407_playback_open_34,
	.close =		snd_sam9407_playback_close_34,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_sam9407_playback_hw_params,
	.hw_free =		snd_sam9407_playback_hw_free,
	.prepare =		snd_sam9407_playback_prepare,
	.trigger =		snd_sam9407_trigger,
	.pointer =		snd_sam9407_pointer

};

#if 0
static snd_pcm_ops_t snd_sam9407_capture_ops_12 = {
	.open =			snd_sam9407_capture_open_12,
	.close =		snd_sam9407_capture_close_12,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_sam9407_capture_hw_params,
	.hw_free =		snd_sam9407_capture_hw_free,
	.prepare =		snd_sam9407_capture_prepare,
	.trigger =		snd_sam9407_trigger,
	.pointer =		snd_sam9407_pointer
};

static snd_pcm_ops_t snd_sam9407_capture_ops_34 = {
	.open =			snd_sam9407_capture_open_34,
	.close =		snd_sam9407_capture_close_34,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =	snd_sam9407_capture_hw_params,
	.hw_free =	snd_sam9407_capture_hw_free,
	.prepare =		snd_sam9407_capture_prepare,
	.trigger =		snd_sam9407_trigger,
	.pointer =		snd_sam9407_pointer
};

static snd_pcm_ops_t snd_sam9407_capture_ops_56 = {
	.open =			snd_sam9407_capture_open_56,
	.close =		snd_sam9407_capture_close_56,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =	snd_sam9407_capture_hw_params,
	.hw_free =	snd_sam9407_capture_hw_free,
	.prepare =		snd_sam9407_capture_prepare,
	.trigger =		snd_sam9407_trigger,
	.pointer =		snd_sam9407_pointer
};

static snd_pcm_ops_t snd_sam9407_capture_ops_78 = {
	.open =			snd_sam9407_capture_open_78,
	.close =		snd_sam9407_capture_close_78,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =	snd_sam9407_capture_hw_params,
	.hw_free =	snd_sam9407_capture_hw_free,
	.prepare =		snd_sam9407_capture_prepare,
	.trigger =		snd_sam9407_trigger,
	.pointer =		snd_sam9407_pointer
};
#endif
/* ========== */

static void snd_sam9407_async_pcm_open(sam9407_t *sam, sam9407_voice_t * voice)
{
#if 0
	/* FIXME: program PCM mixer here ? */
	snd_sam9407_pcm_mixer_t *mix = &sam->pcm_mixer[substream->number];
#endif
	if (!(test_bit(SAM_VOICE_IN_USE, &voice->status)))
		snd_printk("sam9407: wrong PCM channel open ACK\n");

	return;
}

static void snd_sam9407_async_pcm_close(sam9407_t *sam, sam9407_voice_t * voice)
{
	snd_printk("Closing voice %d %p\n",voice->number, voice);
	clear_bit(SAM_VOICE_PLAYING, &voice->status);
	voice->hwptr=0;
}

static void snd_sam9407_async_pcm_update(sam9407_t *sam, sam9407_voice_t *voice)
{

	snd_pcm_substream_t *substream;
	snd_pcm_runtime_t *runtime;

	u16 *hwbuf;
	u8 stat;

	snd_pcm_uframes_t hwoff;
	unsigned int wcount;

	//snd_printk("Updating voice %p\n",voice);

	if (voice == NULL) {
		snd_printk("pcm update: Illegal voice\n");
		return;
	}

	substream = voice->substream;

	if (substream == NULL) {
		snd_printk("pcm update: Illegal substream\n");
		return;
	}

	runtime = substream->runtime;

	if (runtime == NULL) {
		snd_printk("pcm update: Illegal runtime\n");
		return;
	}


	/* the ISIS outputs a few more bytes... */
	while(((stat=sam->readCtrl(sam)) & (1 << 7)) == 0) {
  		snd_printk("  read DATA8 (CTRL=%02X): %02X\n ", stat, sam->readData8(sam));
 	}

	if (test_bit(SAM_VOICE_START_BUSY, &voice->status)) {
		/* set volumes */
		snd_sam9407_pcm_ctl_put(sam, voice->number, 0x45, (voice->volume_left * voice->left_unmuted)<<8);
		snd_sam9407_pcm_ctl_put(sam, voice->number, 0x46, (voice->volume_right * voice->right_unmuted)<<8);
		snd_sam9407_pcm_ctl_put(sam, voice->number, 0x47, (voice->volume_aux_left * voice->aux_left_unmuted)<<8);
		snd_sam9407_pcm_ctl_put(sam, voice->number, 0x49, (voice->volume_aux_right * voice->aux_right_unmuted)<<8);
	}

	/* FIXME: Is this OK? */
	hwoff = runtime->hw_ptr_interrupt;
	//snd_printk(" Current HW offset: %d frames = %d bytes\n", hwoff, frames_to_bytes(runtime, hwoff));

	hwbuf = (u16 *) runtime->dma_area;
       	hwbuf += ((frames_to_bytes(runtime, hwoff) % runtime->dma_bytes ) >> 1);

	//wcount = SAM_HW_BUFF_SIZE_BYTES;
	//wcount >>= 1;
	wcount = snd_pcm_lib_period_bytes(substream)>>1;

	//snd_printk(" transfer 1 period: %d bytes = %d words\n", snd_pcm_lib_period_bytes(substream), wcount);

	/* transfer to/from sam9407 buffers */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		sam->writeData16Burst(sam, hwbuf, wcount);
	} else {
		sam->readData16Burst(sam, hwbuf, wcount);
	}

	//snd_printk(" Burstwrite done. Sending EOT.\n");

	snd_sam9407_command(sam, NULL, SAM_END_XFER,
			&voice->number, 1, NULL, 0, -1);

	/* update hardware pointer mirror */
	voice->hwptr = runtime->hw_ptr_interrupt + bytes_to_frames(runtime, snd_pcm_lib_period_bytes(substream));

	snd_pcm_period_elapsed(substream);

}

/* async audio interrupt handler */
void snd_sam9407_async_audio_irq(sam9407_t *sam, u8 data)
{
	sam9407_voice_t *voice;
	int channel;

	enum { OPEN, CLOSE, UPDATE } what = UPDATE;

	//snd_printk("sam9407: async audio IRQ received, data 0x%x\n", data);
	
	/* ignore PCM channel open acknowledges - we use proper channel accounting */
	if (data == SAM_ACK)
		return;
	if (data == SAM_NAK) {
		snd_printk("sam9407: PCM channel open NAK received\n");
		return;
	}

	if ((data & 0xF0)==SAM_WAVE_ACK_CLOSE) {
		channel = data & 0x0F;
		what = CLOSE;
	} else {
		channel = data;
	}

	if (channel >= SAM_MAX_PCM_CHANNELS) {
		snd_printk("sam9407: wrong PCM channel requested\n");
		return;
	}

	voice = &sam->voices[channel];
	if (voice == NULL)
		return;

	switch (what) {
	case OPEN:
		// does not occur with the ISIS firmware
		snd_printk("sam9407: open interrupt\n");
		break;
	case CLOSE:
		snd_printk("sam9407: close interrupt\n");
		snd_sam9407_async_pcm_close(sam, voice);
		clear_bit(SAM_VOICE_STOP_BUSY, &voice->status);
		break;
	default:
		//snd_printk("sam9407: update interrupt for voice %p, channel %d\n", voice, channel);
		snd_sam9407_async_pcm_update(sam, voice);
		// i assume that after first update the start was successfull.
		clear_bit(SAM_VOICE_START_BUSY, &voice->status);
	}
	//snd_printk("sam9407: end of SAM interrupt handler\n");
}



/* ========== */

static int snd_sam9407_pcm_vol_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0xFF;
	return 0;
}

static int snd_sam9407_pcm_vol_get(snd_kcontrol_t * kcontrol,
			     snd_ctl_elem_value_t * ucontrol)
{
	sam9407_t *sam = _snd_kcontrol_chip(kcontrol);
	sam9407_voice_t *voice;

	snd_assert(sam != 0, return -EINVAL);
	voice = &sam->voices[0];

	snd_assert(voice != 0, return -EINVAL);

	ucontrol->value.integer.value[0] = voice->volume_left;
	ucontrol->value.integer.value[1] = voice->volume_right;
	//snd_printk("get pcm 1/2 volume: %d %d\n",ucontrol->value.integer.value[0],ucontrol->value.integer.value[1]);
	return 0;
}

static int snd_sam9407_pcm_vol_put(snd_kcontrol_t * kcontrol,
			     snd_ctl_elem_value_t * ucontrol)
{

	sam9407_t *sam = _snd_kcontrol_chip(kcontrol);
	sam9407_voice_t *voice;

	unsigned short val1, val2;
	int err, change1, change2;

	snd_assert(sam != 0, return -EINVAL);
	voice = &sam->voices[0];
	snd_assert(voice != 0, return -EINVAL);


	val1 = ucontrol->value.integer.value[0];
	val2 = ucontrol->value.integer.value[1];

	change1 = (val1 != voice->volume_left);
	if (change1) {
		err = snd_sam9407_pcm_ctl_put(sam, 0, 0x45, (val1 * voice->left_unmuted)<<8);
		if (err < 0) {
			change1 = 0;
		} else {
			voice->volume_left=val1;
		}
	}
	change2 = (val2 != voice->volume_right);
	if (change2) {
		err = snd_sam9407_pcm_ctl_put(sam, 0, 0x46, (val2 * voice->right_unmuted)<<8);
		if (err < 0) {
			change2 = 0;
		} else {
			voice->volume_right=val2;
		}
	}
	// always keep voice 0 muted on the 3/4 output
	snd_sam9407_pcm_ctl_put(sam, 0, 0x47, 0);
	voice->volume_aux_left=0;

	snd_sam9407_pcm_ctl_put(sam, 0, 0x49, 0);
	voice->volume_aux_right=0;

	//snd_printk("put pcm 1/2 volume: %d %d\n",voice->volume_left,voice->volume_right);

	return (change1 || change2);

}
static int snd_sam9407_pcm_vol_mute_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_sam9407_pcm_vol_mute_get(snd_kcontrol_t * kcontrol,
			        snd_ctl_elem_value_t * ucontrol)
{
	sam9407_t *sam = _snd_kcontrol_chip(kcontrol);
	sam9407_voice_t *voice;

	snd_assert(sam != 0, return -EINVAL);
	voice = &sam->voices[0];
	snd_assert(voice != 0, return -EINVAL);

	ucontrol->value.integer.value[0] = voice->left_unmuted;
	ucontrol->value.integer.value[1] = voice->right_unmuted;

	return 0;
}

static int snd_sam9407_pcm_vol_mute_put(snd_kcontrol_t * kcontrol,
			        snd_ctl_elem_value_t * ucontrol)
{
	sam9407_t *sam = _snd_kcontrol_chip(kcontrol);
	sam9407_voice_t *voice;
	unsigned short val1, val2;
	int err, change1, change2;

	snd_assert(sam != 0, return -EINVAL);
	voice = &sam->voices[0];
	snd_assert(voice != 0, return -EINVAL);


	val1 = ucontrol->value.integer.value[0];
	val2 = ucontrol->value.integer.value[1];

	change1 = (val1 != voice->left_unmuted);
	if (change1) {
		err = snd_sam9407_pcm_ctl_put(sam, 0, 0x45, (val1 * voice->volume_left)<<8);
		if (err < 0) {
			change1 = 0;
		} else {
			voice->left_unmuted=val1;
		}
	}
	change2 = (val2 != voice->right_unmuted);
	if (change2) {
		err = snd_sam9407_pcm_ctl_put(sam, 0, 0x46, (val2 * voice->volume_right)<<8);
		if (err < 0) {
			change2 = 0;
		} else {
			voice->right_unmuted=val2;
		}
	}
	// always keep voice 1 muted on the 3/4 output
	snd_sam9407_pcm_ctl_put(sam, 0, 0x47, 0);
	voice->volume_aux_left=0;

	snd_sam9407_pcm_ctl_put(sam, 0, 0x49, 0);
	voice->volume_aux_right=0;

	return (change1 || change2);
}

static snd_kcontrol_new_t snd_sam9407_pcm_vol_control =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "SAM PCM 1/2 Playback Volume",
	.index=		0,
	.private_value=0,
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info =		snd_sam9407_pcm_vol_info,
	.get =		snd_sam9407_pcm_vol_get,
	.put =		snd_sam9407_pcm_vol_put,

};

static snd_kcontrol_new_t snd_sam9407_pcm_vol_mute_control =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "SAM PCM 1/2 Playback Switch",
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info =		snd_sam9407_pcm_vol_mute_info,
	.get =		snd_sam9407_pcm_vol_mute_get,
	.put =		snd_sam9407_pcm_vol_mute_put,
	.private_value=0
};

static int snd_sam9407_pcm_auxvol_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0xFF;
	return 0;
}

static int snd_sam9407_pcm_auxvol_get(snd_kcontrol_t * kcontrol,
			        snd_ctl_elem_value_t * ucontrol)
{
	sam9407_t *sam = _snd_kcontrol_chip(kcontrol);
	sam9407_voice_t *voice;

	snd_assert(sam != 0, return -EINVAL);
	voice = &sam->voices[1];
	snd_assert(voice != 0, return -EINVAL);

	ucontrol->value.integer.value[0] = voice->volume_aux_left;
	ucontrol->value.integer.value[1] = voice->volume_aux_right;

	return 0;
}

static int snd_sam9407_pcm_auxvol_put(snd_kcontrol_t * kcontrol,
			        snd_ctl_elem_value_t * ucontrol)
{
	sam9407_t *sam = _snd_kcontrol_chip(kcontrol);

	sam9407_voice_t *voice;

	unsigned short val1, val2;
	int err, change1, change2;

	snd_assert(sam != 0, return -EINVAL);
	voice = &sam->voices[1];
	snd_assert(voice != 0, return -EINVAL);



	val1 = ucontrol->value.integer.value[0];
	val2 = ucontrol->value.integer.value[1];

	change1 = (val1 != voice->volume_aux_left);
	if (change1) {
		err = snd_sam9407_pcm_ctl_put(sam, 1, 0x47, (val1 * voice->aux_left_unmuted)<<8);
		if (err < 0) {
			change1 = 0;
		} else {
			voice->volume_aux_left=val1;
		}
	}
	change2 = (val2 != voice->volume_aux_right);
	if (change2) {
		err = snd_sam9407_pcm_ctl_put(sam, 1, 0x49, (val2 * voice->aux_right_unmuted)<<8);
		if (err < 0) {
			change2 = 0;
		} else {
			voice->volume_aux_right=val2;
		}
	}
	// always keep voice 1 muted on the 1/2 output
	snd_sam9407_pcm_ctl_put(sam, 1, 0x45, 0);
	voice->volume_left=0;

	snd_sam9407_pcm_ctl_put(sam, 1, 0x46, 0);
	voice->volume_right=0;

	return (change1 || change2);
}

static int snd_sam9407_pcm_auxvol_mute_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_sam9407_pcm_auxvol_mute_get(snd_kcontrol_t * kcontrol,
			        snd_ctl_elem_value_t * ucontrol)
{
	sam9407_t *sam = _snd_kcontrol_chip(kcontrol);
	sam9407_voice_t *voice;

	snd_assert(sam != 0, return -EINVAL);
	voice = &sam->voices[1];
	snd_assert(voice != 0, return -EINVAL);

	ucontrol->value.integer.value[0] = voice->aux_left_unmuted;
	ucontrol->value.integer.value[1] = voice->aux_right_unmuted;

	return 0;
}

static int snd_sam9407_pcm_auxvol_mute_put(snd_kcontrol_t * kcontrol,
			        snd_ctl_elem_value_t * ucontrol)
{
	sam9407_t *sam = _snd_kcontrol_chip(kcontrol);
	sam9407_voice_t *voice;

	unsigned short val1, val2;
	int err, change1, change2;

	snd_assert(sam != 0, return -EINVAL);
	voice = &sam->voices[1];
	snd_assert(voice != 0, return -EINVAL);



	val1 = ucontrol->value.integer.value[0];
	val2 = ucontrol->value.integer.value[1];

	change1 = (val1 != voice->aux_left_unmuted);
	if (change1) {
		err = snd_sam9407_pcm_ctl_put(sam, 1, 0x47, (val1 * voice->volume_aux_left)<<8);
		if (err < 0) {
			change1 = 0;
		} else {
			voice->aux_left_unmuted=val1;
		}
	}
	change2 = (val2 != voice->aux_right_unmuted);
	if (change2) {
		err = snd_sam9407_pcm_ctl_put(sam, 1, 0x49, (val2 * voice->volume_aux_right)<<8);
		if (err < 0) {
			change2 = 0;
		} else {
			voice->aux_right_unmuted=val2;
		}
	}
	// always keep voice 1 muted on the 1/2 output
	snd_sam9407_pcm_ctl_put(sam, 1, 0x45, 0);
	voice->volume_left=0;

	snd_sam9407_pcm_ctl_put(sam, 1, 0x46, 0);
	voice->volume_right=0;

	return (change1 || change2);
}

static snd_kcontrol_new_t snd_sam9407_pcm_auxvol_control =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "SAM PCM 3/4 Playback Volume",
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info =		snd_sam9407_pcm_auxvol_info,
	.get =		snd_sam9407_pcm_auxvol_get,
	.put =		snd_sam9407_pcm_auxvol_put,
	.private_value=0
};

static snd_kcontrol_new_t snd_sam9407_pcm_auxvol_mute_control =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "SAM PCM 3/4 Playback Switch",
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info =		snd_sam9407_pcm_auxvol_mute_info,
	.get =		snd_sam9407_pcm_auxvol_mute_get,
	.put =		snd_sam9407_pcm_auxvol_mute_put,
	.private_value=0
};

/* ========== */

static int snd_sam9407_pcm(sam9407_t *sam, int device, int sam_target , snd_pcm_t ** rpcm)
{
	snd_card_t * card = sam->card;
	snd_kcontrol_t *kctl;

	snd_pcm_t *pcm;
	int err,i;




	if (rpcm)
		*rpcm = NULL;
	// FIXME: no capture substream yet!!

	switch (sam_target) {
	default:
	case SAM_TARGET_12: // this is the first playback/record channel (1/2)
		/* build pcm mixer controls */

		i=0;
		sam->voices[i].volume_left=0;
		sam->voices[i].volume_right=0;
		sam->voices[i].volume_aux_left=0;
		sam->voices[i].volume_aux_right=0;
		sam->voices[i].left_unmuted=1;
		sam->voices[i].right_unmuted=1;
		sam->voices[i].aux_left_unmuted=1;
		sam->voices[i].aux_right_unmuted=1;

		snd_printk("creating 1/2 controls\n");
		// the 1/2 playback control
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_sam9407_pcm_vol_control, sam))) < 0) {
			return err;
		}
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_sam9407_pcm_vol_mute_control, sam))) < 0) {
			return err;
		}

		if ((err = snd_pcm_new(sam->card, "SAM9407 PCM 1/2", device, 1, 0, &pcm)) < 0) {
			return err;
		}
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_sam9407_playback_ops_12);
		//snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_sam9407_capture_ops_12);
		pcm->private_data = sam;
		pcm->info_flags = 0;
		strcpy(pcm->name, "SAM9407 PCM 1/2");
		//sam->pcm = pcm;

		if ((err=snd_pcm_lib_preallocate_pages_for_all(pcm,8*SAM_MAX_BUFFER_SIZE, 8*SAM_MAX_BUFFER_SIZE, GFP_KERNEL))) {
			snd_printk("Could not preallocate memory\n");
			return err;
		}
		break;

	case SAM_TARGET_34: // this is the second playback/record channel (3/4)
		/* build pcm mixer controls */

		i=1;
		sam->voices[i].volume_left=0;
		sam->voices[i].volume_right=0;
		sam->voices[i].volume_aux_left=0;
		sam->voices[i].volume_aux_right=0;
		sam->voices[i].left_unmuted=1;
		sam->voices[i].right_unmuted=1;
		sam->voices[i].aux_left_unmuted=1;
		sam->voices[i].aux_right_unmuted=1;

		snd_printk("creating 3/4 controls\n");
		// the 3/4 playback control
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_sam9407_pcm_auxvol_control, sam))) < 0) {
			return err;
		}
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_sam9407_pcm_auxvol_mute_control, sam))) < 0) {
			return err;
		}

		// the PCM device
		if ((err = snd_pcm_new(sam->card, "SAM9407 PCM 3/4", device, 1, 0, &pcm)) < 0) {
			return err;
		}

		// the pcm operations
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_sam9407_playback_ops_34);
		//snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_sam9407_capture_ops_34);
		pcm->private_data = sam;
		pcm->info_flags = 0;
		strcpy(pcm->name, "SAM9407 PCM 3/4");
		//sam->pcm = pcm;

		if ((err=snd_pcm_lib_preallocate_pages_for_all(pcm,8*SAM_MAX_BUFFER_SIZE, 8*SAM_MAX_BUFFER_SIZE, GFP_KERNEL))) {
			snd_printk("Could not preallocate memory\n");
			return err;
		}
		break;

	case SAM_TARGET_56: // this is the third record channel (5/6)
		if ((err = snd_pcm_new(sam->card, "SAM9407 PCM 5/6", device, 0, 0, &pcm)) < 0) {
			return err;
		}
		//snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_sam9407_capture_ops_56);
		pcm->private_data = sam;
		pcm->info_flags = 0;
		strcpy(pcm->name, "SAM9407 PCM 5/6");
		//sam->pcm = pcm;

		if ((err=snd_pcm_lib_preallocate_pages_for_all(pcm,8*SAM_MAX_BUFFER_SIZE, 8*SAM_MAX_BUFFER_SIZE, GFP_KERNEL))) {
			snd_printk("Could not preallocate memory\n");
			return err;
		}
		break;

	case SAM_TARGET_78: // this is the fourth record channel (7/8)
		if ((err = snd_pcm_new(sam->card, "SAM9407 PCM 7/8", device, 0, 0, &pcm)) < 0) {
			return err;
		}
		//snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_sam9407_capture_ops_78);
		pcm->private_data = sam;
		pcm->info_flags = 0;
		strcpy(pcm->name, "SAM9407 PCM 7/8");
		//sam->pcm = pcm;

		if ((err=snd_pcm_lib_preallocate_pages_for_all(pcm,8*SAM_MAX_BUFFER_SIZE, 8*SAM_MAX_BUFFER_SIZE, GFP_KERNEL))) {
			snd_printk("Could not preallocate memory\n");
			return err;
		}
		break;
	}

	if (rpcm)
		*rpcm = pcm;
	return 0;
}
