#
# Makefile for ALSA
# Copyright (c) 2001 by Uros Bizjak <uros@kss-loka.si>
#   Modified by Pieter Palmers

TOPDIR = ../..

include $(TOPDIR)/toplevel.config
include $(TOPDIR)/Makefile.conf

TOPDIR = $(MAINSRCDIR)

#
#
#

#O_TARGET     := _sam9407.o

list-multi   := snd-isis.o

export-objs  := sam9407.o

#snd-sam9407-lib-objs := sam9407_lib.o sam_io.o sam_mem.o sam_ctl.o sam_proc.o sam_ucode.o

snd-isis-objs := isis.o 
snd-sam9407-objs := sam_pcm.o sam9407.o sam_io.o sam_proc.o sam_mem.o sam_ctl.o sam_ucode.o sam_mpu401.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_ISIS) += snd-isis.o
obj-$(CONFIG_SND_ISIS) += snd-sam9407.o


include $(TOPDIR)/Rules.make
all: snd-isis.o snd-sam9407.o

modules: snd-isis.o snd-sam9407.o

snd-sam9407.o: $(snd-sam9407-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-sam9407-objs)

snd-isis.o: $(snd-isis-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-isis-objs)

sambootload:
	gcc -I../../include/ -l asound -o sambootload sam_bootloader.c
install:
	rm -f /lib/modules/`uname -r`/kernel/sound/pci/snd-isis.o
	rm -f /lib/modules/`uname -r`/kernel/sound/pci/snd-sam9407.o

	cp -f snd-isis.o /lib/modules/`uname -r`/kernel/sound/pci/
	cp -f snd-sam9407.o /lib/modules/`uname -r`/kernel/sound/pci/

	/sbin/depmod -a -e

modules_install:
	cp -f snd-isis.o /lib/modules/`uname -r`/kernel/sound/pci/
	cp -f snd-sam9407.o /lib/modules/`uname -r`/kernel/sound/pci/
