/*
 *  Copyright (c) by Pieter Palmers <pieterpalmers@sourceforge.net>
 *  MIDI/Synth routines for SAM9407 chip
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
#include <sound/mpu401.h>

#include <sam9407.h>

/* The SAM9407 MIDI functions are MPU401 compatible
 * This means we can use the generic MPU-401 driver for these functions
 *
 * What we need to do is modify the low level io stuff to use ISIS i/o
 * and redirect the SAM midi IRQ to the generic handler
 *
 * We build two MPU devices: the first device is the synth device, the second is the
 * MIDI port on the SAM. Mind that there is also an MPU device on the ESS maestro, so the
 * rawmidi device map becomes: 0=ess mpu; 1=sam synth; 2=sam midi
 *
 */
#define SAM_CMD_EN_MIDIOUT	0x3D /* enable midi out */
#define SAM_CMD_MIDI_PORT0	0xB0 /* select port 0 (channels 0 to 15) */
#define SAM_CMD_MIDI_PORT1	0xB1 /* select port 1 (channels 16 to 31) */
#define SAM_CMD_GM_VOL		0x38 /* GM Midi Volume */
#define SAM_CMD_GM_PAN		0x39 /* GM Midi PAN */


/* lowlevel io */
static void snd_sam9407_mpu401_write_port(mpu401_t *mpu, unsigned char data, unsigned long addr)
{
	sam9407_t *sam = snd_magic_cast(sam9407_t, mpu->private_data, return);

	switch (addr)
	{ // determine the port
		case 0: // data port

			/* The MIDI-OUT port is PORT0
			 * We select the right port on every byte sent, just to make sure
			 */
			snd_sam9407_command(sam, NULL, SAM_CMD_MIDI_PORT0, NULL, 0, NULL, 0, -1);
			/* the enable control is sent before every midi byte. The SAM specs indicate
			 * that midi out gets disabled each time a control command is sent. We don't
			 * know what controls are sent by other parts of the driver (e.g. volume),
			 * hence we have to send this enable control each time we send a MIDI byte.
			 */
			snd_sam9407_command(sam, NULL, SAM_CMD_EN_MIDIOUT, &data, 1, NULL, 0, -1);
			break;
		case 1: // control port
			/* according to the MPU-401 specs, the only bytes that can be written to the
			 * control port when in UART mode, are the bytes 0xFF (=reset to standalone)
			 * and 0x3F (switch to UART mode).
			 * Both commands are handled by the other driver code, and we don't want to
			 * interfere. Commands are therefore ignored
			 * SAM specific controls are not handled by the generic MPU driver.
			 */
			break;
		default: //ignore other ports
			break;
	}
}

static unsigned char snd_sam9407_mpu401_read_port(mpu401_t *mpu, unsigned long addr)
{
	sam9407_t *sam = snd_magic_cast(sam9407_t, mpu->private_data, return 0);
	switch (addr)
	{ // determine the port
		case 0: // data port
			return sam->readData8(sam);
		case 1: // control port
			return (sam->readCtrl(sam) & 0xC0); // we mask the SAM specific bits
		default: //ignore other ports
			return 0;
	}
}

static void snd_sam9407_synth_write_port(mpu401_t *mpu, unsigned char data, unsigned long addr)
{
	sam9407_t *sam = snd_magic_cast(sam9407_t, mpu->private_data, return);

	switch (addr)
	{ // determine the port
		case 0: // data port

			/* The SYNTH port is PORT1
			 * We select the right port on every byte sent, just to make sure
			 */
			snd_sam9407_command(sam, NULL, SAM_CMD_MIDI_PORT1, NULL, 0, NULL, 0, -1);
			/* the enable control is sent before every midi byte. The SAM specs indicate
			 * that midi out gets disabled each time a control command is sent. We don't
			 * know what controls are sent by other parts of the driver (e.g. volume),
			 * hence we have to send this enable control each time we send a MIDI byte.
			 */
			snd_sam9407_command(sam, NULL, SAM_CMD_EN_MIDIOUT, &data, 1, NULL, 0, -1);
			break;
		case 1: // control port
			/* according to the MPU-401 specs, the only bytes that can be written to the
			 * control port when in UART mode, are the bytes 0xFF (=reset to standalone)
			 * and 0x3F (switch to UART mode).
			 * Both commands are handled by the other driver code, and we don't want to
			 * interfere. Commands are therefore ignored
			 * SAM specific controls are not handled by the generic MPU driver.
			 */
			break;
		default: //ignore other ports
			break;
	}

}

static unsigned char snd_sam9407_synth_read_port(mpu401_t *mpu, unsigned long addr)
{
	sam9407_t *sam = snd_magic_cast(sam9407_t, mpu->private_data, return 0);
	switch (addr)
	{ // determine the port
		case 0: // data port
			return sam->readData8(sam);
		case 1: // control port
			/* One cannot record from the synth, so we make sure the TE bit is always set
			 */
			return ((sam->readCtrl(sam) & 0xC0) | SAM_STATUS_TE); // we mask the SAM specific bits
		default: //ignore other ports
			return 0;
	}
}


/* mixer controls */

static int snd_sam9407_midi_vol_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0xFF;
	return 0;
}

static int snd_sam9407_midi_vol_get(snd_kcontrol_t * kcontrol,
			        snd_ctl_elem_value_t * ucontrol)
{
	u8 left,right;
	sam9407_t *sam = _snd_kcontrol_chip(kcontrol);
	snd_assert(sam != 0, return -EINVAL);


	/* R=(pan/pan_center - 1)*max_vol/2+volume
	 * L=2*volume-R
	 */

	right=(u8)(((float)sam->midi_pan)/0x40 - 1)*0x7F + sam->midi_volume;
	left=2*sam->midi_volume - right;

	ucontrol->value.integer.value[0] = left;
	ucontrol->value.integer.value[1] = right;

	return 0;
}

static int snd_sam9407_midi_vol_put(snd_kcontrol_t * kcontrol,
			        snd_ctl_elem_value_t * ucontrol)
{
	sam9407_t *sam = _snd_kcontrol_chip(kcontrol);

	unsigned short val1, val2;
	int err, change1, change2;

	u8 pan;
	u8 volume;
	u8 tmp;

	snd_assert(sam != 0, return -EINVAL);

	val1 = ucontrol->value.integer.value[0];
	val2 = ucontrol->value.integer.value[1];

	/* GM_PAN = ((right - left)/max_vol + 1) * pan_center */
	pan = (u8)((((float)(val2-val1))/((float)0xFF)+1)*0x40);
	volume=(val1+val2)/2;

	change1 = (pan != sam->midi_pan);
	if (change1) {
		err=snd_sam9407_command(sam, NULL, SAM_CMD_GM_PAN, &pan, 1, NULL, 0, -1);
		if (err < 0) {
			change1 = 0;
		} else {
			sam->midi_pan=pan;
		}
	}
	change2 = (volume != sam->midi_volume);
	if (change2) {
		tmp = volume * sam->midi_unmuted;
		err=snd_sam9407_command(sam, NULL, SAM_CMD_GM_VOL, &tmp, 1, NULL, 0, -1);
		if (err < 0) {
			change2 = 0;
		} else {
			 sam->midi_volume=volume;
		}
	}

	return (change1 || change2);
}

static int snd_sam9407_midi_vol_mute_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_sam9407_midi_vol_mute_get(snd_kcontrol_t * kcontrol,
			        snd_ctl_elem_value_t * ucontrol)
{
	sam9407_t *sam = _snd_kcontrol_chip(kcontrol);
	snd_assert(sam != 0, return -EINVAL);

	ucontrol->value.integer.value[0] = sam->midi_unmuted;

	return 0;
}

static int snd_sam9407_midi_vol_mute_put(snd_kcontrol_t * kcontrol,
			        snd_ctl_elem_value_t * ucontrol)
{
	sam9407_t *sam = _snd_kcontrol_chip(kcontrol);
	unsigned short val1;
	int err, change1;

	u8 tmp;

	snd_assert(sam != 0, return -EINVAL);

	val1 = ucontrol->value.integer.value[0];

	change1 = (val1 != sam->midi_unmuted);
	if (change1) {
		tmp = sam->midi_volume * val1;
		err=snd_sam9407_command(sam, NULL, SAM_CMD_GM_VOL, &tmp, 1, NULL, 0, -1);
		if (err < 0) {
			change1 = 0;
		} else {
			 sam->midi_unmuted=val1;
		}
	}

	return (change1);
}

static snd_kcontrol_new_t snd_sam9407_midi_vol_control =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "SAM MIDI Playback Volume",
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info =		snd_sam9407_midi_vol_info,
	.get =		snd_sam9407_midi_vol_get,
	.put =		snd_sam9407_midi_vol_put,
	.private_value=0
};

static snd_kcontrol_new_t snd_sam9407_midi_vol_mute_control =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "SAM MIDI Playback Switch",
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info =		snd_sam9407_midi_vol_mute_info,
	.get =		snd_sam9407_midi_vol_mute_get,
	.put =		snd_sam9407_midi_vol_mute_put,
	.private_value=0
};

static int snd_sam9407_midi_mixer(sam9407_t *sam) {
	int err;
	snd_card_t *card;
	
	snd_assert(sam != 0, return -EINVAL);

	card=sam->card;
	snd_assert(card != 0, return -EINVAL);

	/* init the controls */
	sam->midi_volume=0;
	sam->midi_pan=0x40;
	sam->midi_unmuted=0;

	snd_printk("creating MIDI controls\n");
	if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_sam9407_midi_vol_control, sam))) < 0) {
		return err;
	}
	if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_sam9407_midi_vol_mute_control, sam))) < 0) {
		return err;
	}

	return 0;
}


/* constructor */
static int snd_sam9407_midi(sam9407_t *sam) {
	int err;

	mpu401_t *synth;
	mpu401_t *midi;

	snd_card_t *card=sam->card;

	snd_assert(sam != 0, return -EINVAL);
	snd_assert(card != 0, return -EINVAL);

	/* create synth device */
	if ((err = snd_mpu401_uart_new(card, 1, MPU401_HW_MPU401,
				       0, 1,
				       0, 0, &sam->rmidi_synth)) < 0) {
		printk(KERN_WARNING "isis: skipping SAM MPU-401 synth support..\n");
	} else {
		sprintf(sam->rmidi_synth->name, "Maxi Studio ISIS SAM Synth");

		synth=snd_magic_cast(mpu401_t, sam->rmidi_synth->private_data, return -EINVAL);

		/* assign correct private data */
		synth->private_data=sam;

		/* set correct I/O functions */
		synth->write=snd_sam9407_synth_write_port;
		synth->read=snd_sam9407_synth_read_port;

	}

	/* create midi port device */
	if ((err = snd_mpu401_uart_new(card, 2, MPU401_HW_MPU401,
				       0, 1,
				       0, 0, &sam->rmidi_midi)) < 0) {
		printk(KERN_WARNING "isis: skipping SAM MPU-401 MIDI support..\n");
	} else {
		sprintf(sam->rmidi_midi->name, "Maxi Studio ISIS SAM MPU-401");

		midi=snd_magic_cast(mpu401_t, sam->rmidi_midi->private_data, return -EINVAL);

		/* assign correct private data */
		midi->private_data=sam;

		/* set correct I/O functions */
		midi->write=snd_sam9407_mpu401_write_port;
		midi->read=snd_sam9407_mpu401_read_port;
	}


	if ((err = snd_sam9407_midi_mixer(sam))<0) {
		snd_printk("failed to create SAM MIDI mixer controls\n");
		return err;
	}


	return 0;
}

