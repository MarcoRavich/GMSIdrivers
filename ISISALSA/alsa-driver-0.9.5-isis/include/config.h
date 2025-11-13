/* include/config.h.  Generated automatically by configure.  */
/*
 *  Configuration header file for compilation of the ALSA driver
 */

#include "config1.h"

/* ALSA section */
#define CONFIG_SND_VERSION "0.9.5"
#define CONFIG_SND_ISA 1
#define CONFIG_SND_SEQUENCER 1
#define CONFIG_SND_OSSEMUL 1
#define CONFIG_SND_MIXER_OSS 1
#define CONFIG_SND_PCM_OSS 1
#define CONFIG_SND_SEQUENCER_OSS 1
#define CONFIG_SND_VERBOSE_PRINTK 1
/* #undef CONFIG_SND_DEBUG */
/* #undef CONFIG_SND_DEBUG_MEMORY */
/* #undef CONFIG_SND_DEBUG_FULL */
/* #undef CONFIG_SND_DEBUG_DETECT */
/* #undef CONFIG_SND_BIT32_EMUL_MODULE */
/* #undef CONFIG_SND_RTCTIMER_MODULE */
#define CONFIG_SND_SEQ_DUMMY_MODULE 1

/* build section */
#define CONFIG_SND_KERNELDIR "/lib/modules/2.4.20-LL/build"
#define CONFIG_ISAPNP_KERNEL 1
/* #undef CONFIG_PNP_KERNEL */
#define CONFIG_ISAPNP 1
#define CONFIG_PNP 1

/* 2.4 kernels */
#define CONFIG_HAVE_OLD_REQUEST_MODULE 1
/* #undef CONFIG_HAVE_STRLCPY */
#define CONFIG_HAVE_SNPRINTF 1
#define CONFIG_HAVE_VMALLOC_TO_PAGE 1

/* 2.2 kernels */
/* #undef CONFIG_OLD_KILL_FASYNC */
/* #undef CONFIG_HAVE_DMA_ADDR_T */
/* #undef CONFIG_HAVE_MUTEX_MACROS */
