static int isis_studio_master_mute_get(snd_kcontrol_t *kcontrol,
                           snd_ctl_elem_value_t *ucontrol)
{
		isis_t *isis = snd_kcontrol_chip(kcontrol);
		if(isis->isis_gpio & ISIS_CONFIG_MUTEOUTPUT)
			ucontrol->value.integer.value[0] = 0;
		else
			ucontrol->value.integer.value[0] = 1;
        return 0;
}

static int isis_studio_master_mute_put(snd_kcontrol_t *kcontrol,
                           snd_ctl_elem_value_t *ucontrol)
{
    isis_t *isis = snd_kcontrol_chip(kcontrol);
 	u16 w;
 	outw(~ISIS_CONFIG_MUTEOUTPUT, isis->io_port + 0x64); // set mask
	w=(isis->isis_gpio & ISIS_CONFIG_MUTEOUTPUT ? 1 : 0);

	if(ucontrol->value.integer.value[0] != w) {
	 	w = inw(isis->io_port + 0x60);
		if (ucontrol->value.integer.value[0]) { // off
			isis->isis_gpio |= ISIS_CONFIG_MUTEOUTPUT;
			w |= ISIS_CONFIG_MUTEOUTPUT;
		}
		else { // on
			isis->isis_gpio &= ISIS_CONFIG_MUTEOUTPUT;
			w &= ~ISIS_CONFIG_MUTEOUTPUT;
		}
		outw(w,isis->io_port + 0x60);
		outw(0xFFF, isis->io_port + 0x64); // set mask
		return 1;
	}
	else return 0;

}

static int isis_studio_master_mute_info(snd_kcontrol_t *kcontrol,
                          snd_ctl_elem_info_t *uinfo)
{
          uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
          uinfo->count = 1;
          uinfo->value.integer.min = 0;
          uinfo->value.integer.max = 1;
          return 0;
}

static snd_kcontrol_new_t isis_studio_master_mute_ctl __devinitdata = {
          .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
          .name = "Master Playback Mute switch",
          .index = 0,
          .access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
          .private_value = 0xffff,
          .info = isis_studio_master_mute_info,
          .get = isis_studio_master_mute_get,
          .put = isis_studio_master_mute_put
};

/* monitoring controls */
static int isis_set_volume_monitor(isis_t *isis, u8 cmd, u8 chan, u8 left, u8 right) {
	sam9407_t *sam = isis->sam;
	u8 data[3];
	//snd_printk(" Setting volume: %02X, %02X to %02X, %02X\n",cmd,chan,left,right);
	data[0]=chan;
	data[1]=left;
	data[2]=right;

	return snd_sam9407_command(sam, NULL, cmd,
				      data, 3, NULL, 0, -1);

}
#if 0
static int isis_studio_monitor_get(snd_kcontrol_t *kcontrol,
                           snd_ctl_elem_value_t *ucontrol)
{
		isis_t *isis = snd_kcontrol_chip(kcontrol);
		/* private_value = (dest << 4) | (mute << 3) | (src) */
        u8 ch_src=kcontrol->private_value & 0x07;
		/* ch_dest = 0 if destination is CH1&2
		 * ch_dest = 1 if destination is CH3&4
		 * so 1+2*ch_dest gives correct 'left channel' index to table.
		 * so 2+2*ch_dest gives correct 'right channel' index to table.
		 * the table values are: monitor_volume[source][value]=(mute_state << 7) | (volume & 0x7F)
		 */
		u8 ch_dest=(kcontrol->private_value & 0x30)>>4;

		if(kcontrol->private_value & 0x08) { // it's the mute control
			ucontrol->value.integer.value[0] = (isis->monitor_volume[ch_src][1+2*ch_dest] & 0x80) >> 7;
			ucontrol->value.integer.value[1] = (isis->monitor_volume[ch_src][2+2*ch_dest] & 0x80) >> 7;
		}
		else { // it's the volume control
			ucontrol->value.integer.value[0] = (isis->monitor_volume[ch_src][1+2*ch_dest] & 0x7F);
			ucontrol->value.integer.value[1] = (isis->monitor_volume[ch_src][2+2*ch_dest] & 0x7F);
		}
        return 0;
}

static int isis_studio_monitor_put(snd_kcontrol_t *kcontrol,
                           snd_ctl_elem_value_t *ucontrol)
{
		isis_t *isis = snd_kcontrol_chip(kcontrol);
		/* private_value = (dest << 4) | (mute << 3) | (src) */
        u8 ch_src=kcontrol->private_value & 0x07;
		/* ch_dest = 0 if destination is CH1&2
		 * ch_dest = 1 if destination is CH3&4
		 * so 1+2*ch_dest gives correct 'left channel' index to table.
		 * so 2+2*ch_dest gives correct 'right channel' index to table.
		 * the table values are: monitor_volume[source][dest]=(mute_state << 7) | (volume & 0x7F)
		 */
		u8 ch_dest=(kcontrol->private_value & 0x30)>>4;

		if(kcontrol->private_value & 0x08) { // it's the mute control
			isis->monitor_volume[ch_src][1+2*ch_dest]=(ucontrol->value.integer.value[0] << 7) | (isis->monitor_volume[ch_src][1+2*ch_dest] & 0x7f);
			isis->monitor_volume[ch_src][2+2*ch_dest]=(ucontrol->value.integer.value[1] << 7) | (isis->monitor_volume[ch_src][2+2*ch_dest] & 0x7f);
		}
		else { // it's the volume control
			isis->monitor_volume[ch_src][1+2*ch_dest]=(ucontrol->value.integer.value[0]) | (isis->monitor_volume[ch_src][1+2*ch_dest] & 0x80);
			isis->monitor_volume[ch_src][2+2*ch_dest]=(ucontrol->value.integer.value[1]) | (isis->monitor_volume[ch_src][2+2*ch_dest] & 0x80);
		}
		/* This is the 'one-call-does-it-all' approach
		* the isis monitor volume commands are:
		* 0x34 for ch1/2, 0x35 for ch3/4 so 0x34+ch_dest is ok
		* The multiplication is to account for the MUTED state
		* You can change volume but muted = no volume
		*/
	//snd_printk(" Table values: %02X, %02X to \n",isis->monitor_volume[ch_src][1+2*ch_dest],isis->monitor_volume[ch_src][2+2*ch_dest]);

		isis_set_volume_monitor(isis,(0x34+ch_dest),ch_src,
				(isis->monitor_volume[ch_src][1+2*ch_dest] & 0x7F) * ((isis->monitor_volume[ch_src][1+2*ch_dest] & 0x80)>> 7),
				(isis->monitor_volume[ch_src][2+2*ch_dest] & 0x7F) * ((isis->monitor_volume[ch_src][2+2*ch_dest] & 0x80) >> 7));
        return 1; // always changed (is this allowed?)

}

static int isis_studio_monitor_info(snd_kcontrol_t *kcontrol,
                          snd_ctl_elem_info_t *uinfo)
{
		if(kcontrol->private_value & 0x08) { // it's the mute control
			uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
			uinfo->count = 2;
			uinfo->value.integer.min = 0;
			uinfo->value.integer.max = 1;
		}
		else {
			uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
			uinfo->count = 2;
			uinfo->value.integer.min = 0;
			uinfo->value.integer.max = 0x7F;
		}
        return 0;
}

#define ISIS_MONITOR_VOLUME(xname, dest, src) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), .info = isis_studio_monitor_info, \
  .get = isis_studio_monitor_get, .put = isis_studio_monitor_put, \
  .private_value = (dest << 4) | (0 << 3) | src }

#define ISIS_MONITOR_MUTE(xname, dest, src) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), .info = isis_studio_monitor_info, \
  .get = isis_studio_monitor_get, .put = isis_studio_monitor_put, \
  .private_value = (dest << 4) | (1 << 3) | src }

static const snd_kcontrol_new_t snd_isis_controls_monitor[32] = {
ISIS_MONITOR_MUTE("CH1 Monitor 1/2 Switch", 0, 0),
ISIS_MONITOR_VOLUME("CH1 Monitor 1/2 Volume", 0, 0),
ISIS_MONITOR_MUTE("CH2 Monitor 1/2 Switch", 0, 1),
ISIS_MONITOR_VOLUME("CH2 Monitor 1/2 Volume", 0, 1),
ISIS_MONITOR_MUTE("CH3 Monitor 1/2 Switch", 0, 2),
ISIS_MONITOR_VOLUME("CH3 Monitor 1/2 Volume", 0, 2),
ISIS_MONITOR_MUTE("CH4 Monitor 1/2 Switch", 0, 3),
ISIS_MONITOR_VOLUME("CH4 Monitor 1/2 Volume", 0, 3),
ISIS_MONITOR_MUTE("CH5 Monitor 1/2 Switch", 0, 4),
ISIS_MONITOR_VOLUME("CH5 Monitor 1/2 Volume", 0, 4),
ISIS_MONITOR_MUTE("CH6 Monitor 1/2 Switch", 0, 5),
ISIS_MONITOR_VOLUME("CH6 Monitor 1/2 Volume", 0, 5),
ISIS_MONITOR_MUTE("CH7 Monitor 1/2 Switch", 0, 6),
ISIS_MONITOR_VOLUME("CH7 Monitor 1/2 Volume", 0, 6),
ISIS_MONITOR_MUTE("CH8 Monitor 1/2 Switch", 0, 7),
ISIS_MONITOR_VOLUME("CH8 Monitor 1/2 Volume", 0, 7),

ISIS_MONITOR_MUTE("CH1 Monitor 3/4 Switch", 1, 0),
ISIS_MONITOR_VOLUME("CH1 Monitor 3/4 Volume", 1, 0),
ISIS_MONITOR_MUTE("CH2 Monitor 3/4 Switch", 1, 1),
ISIS_MONITOR_VOLUME("CH2 Monitor 3/4 Volume", 1, 1),
ISIS_MONITOR_MUTE("CH3 Monitor 3/4 Switch", 1, 2),
ISIS_MONITOR_VOLUME("CH3 Monitor 3/4 Volume", 1, 2),
ISIS_MONITOR_MUTE("CH4 Monitor 3/4 Switch", 1, 3),
ISIS_MONITOR_VOLUME("CH4 Monitor 3/4 Volume", 1, 3),
ISIS_MONITOR_MUTE("CH5 Monitor 3/4 Switch", 1, 4),
ISIS_MONITOR_VOLUME("CH5 Monitor 3/4 Volume", 1, 4),
ISIS_MONITOR_MUTE("CH6 Monitor 3/4 Switch", 1, 5),
ISIS_MONITOR_VOLUME("CH6 Monitor 3/4 Volume", 1, 5),
ISIS_MONITOR_MUTE("CH7 Monitor 3/4 Switch", 1, 6),
ISIS_MONITOR_VOLUME("CH7 Monitor 3/4 Volume", 1, 6),
ISIS_MONITOR_MUTE("CH8 Monitor 3/4 Switch", 1, 7),
ISIS_MONITOR_VOLUME("CH8 Monitor 3/4 Volume", 1, 7),

};

#endif

/* FIXME: clean up this mess! */
static int isis_studio_master_get(snd_kcontrol_t *kcontrol,
                           snd_ctl_elem_value_t *ucontrol)
{
	isis_t *isis = snd_kcontrol_chip(kcontrol);
	sam9407_t *sam = isis->sam;
	u8 ch_dest=(kcontrol->private_value & 0x30)>>4;

	if(kcontrol->private_value & 0x08) { // it's the mute control
		ucontrol->value.integer.value[0] = (sam->playback_volume[2+2*ch_dest] & 0x80) >> 7;
	}
	else { // it's the volume control
		ucontrol->value.integer.value[0] = (sam->playback_volume[1+2*ch_dest]);
	}
	return 0;
}

static int isis_studio_master_put(snd_kcontrol_t *kcontrol,
                           snd_ctl_elem_value_t *ucontrol)
{
	isis_t *isis = snd_kcontrol_chip(kcontrol);
	sam9407_t *sam = isis->sam;
	u8 data[1];

	u8 ch_dest=(kcontrol->private_value & 0x30)>>4;

	if(kcontrol->private_value & 0x08) { // it's the mute control
		sam->playback_volume[2+2*ch_dest]=(ucontrol->value.integer.value[0] << 7);
	}
	else { // it's the volume control
		sam->playback_volume[1+2*ch_dest]=(ucontrol->value.integer.value[0]);
	}


	data[0]=(sam->playback_volume[1+2*ch_dest]) * ((sam->playback_volume[2+2*ch_dest] & 0x80)>> 7);

	snd_sam9407_command(sam, NULL, 0x07, data, 1, NULL, 0, -1);
        return 1; // always changed (is this allowed?)

}

static int isis_studio_master_info(snd_kcontrol_t *kcontrol,
                          snd_ctl_elem_info_t *uinfo)
{

	if(kcontrol->private_value & 0x08) { // it's the mute control
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
		uinfo->count = 1;
		uinfo->value.integer.min = 0;
		uinfo->value.integer.max = 1;
	}
	else {
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
		uinfo->count = 1;
		uinfo->value.integer.min = 0;
		uinfo->value.integer.max = 0xFF;
	}
	return 0;
}
#define ISIS_PLAYBACK_VOLUME(xname, dest) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), .info = isis_studio_master_info, \
  .get = isis_studio_master_get, .put = isis_studio_master_put, \
  .private_value = (dest << 4) | (0 << 3) }

#define ISIS_PLAYBACK_MUTE(xname, dest) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), .info = isis_studio_master_info, \
  .get = isis_studio_master_get, .put = isis_studio_master_put, \
  .private_value = (dest << 4) | (1 << 3)}

static const snd_kcontrol_new_t snd_isis_controls_playback[2] = {
	ISIS_PLAYBACK_MUTE("SAM Master Switch", 0),
	ISIS_PLAYBACK_VOLUME("SAM Master Volume", 0)
};


