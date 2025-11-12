# A compact, driver-oriented extract of what is known and where to get the missing details.

## ESS ES1978MS (Maestro-2EM / Maestro-2E, PCI)
- Bus and IDs: PCI multimedia audio, Vendor 0x125d, Device 0x1978. Seen and supported in BSD/Linux IDs. ([bsd-hardware.info][1])
- Architecture (from Linux driver comments): 64 APUs, WaveCache DMA engine, on-chip ASSP DSP, AC’97 codec link(s). PCM goes PCI→WaveCache→APUs→AC’97; capture uses input APU + rate-converter APU then DMA to RAM. WaveCache can only address 28-bit physical addresses; allocations must fit in a single 4 MiB window. ([codebrowser.dev][2])
- Legacy/aux blocks exposed by config registers: gameport, FM, SB-compat, MPU-401 UART, GPIO, serial IRQ; enable bits and “legacy audio control” flags are defined in driver. ([codebrowser.dev][2])
- Power management: device has quirky PM; mainstream driver keeps an allowlist and can disable PM on unsupported subsystems. ([codebrowser.dev][2])
- IRQ enable and MIDI: host IRQ bits include DSIE, HW volume, optional MPU-401; MPU UART is usually at io_base + ESM_MPU401_PORT. ([codebrowser.dev][2])
- Codec interface: almost always standard AC’97 codecs; mixer and variable-rate via AC’97 registers. Use the AC’97 2.2/2.3 spec for AC-link framing, slot enables, cold/warm reset, VRA, power states. ([codebrowser.dev][2])
- Open drivers to study/port:
   - Linux ALSA: snd-es1968 (sound/pci/es1968.c). Contains register maps, init sequence, DMA setup, APU programming, PM, MPU/gameport glue. This is the primary, working, modern reference. ([GitHub][3])
   - NetBSD esm(4) and OpenBSD maestro(4) manpages for device scope and OS-level behaviors. ([Manuale NetBSD][4])
   - Vendor drivers (for reverse-engineering firmware tables and mixer defaults): Windows 9x/NT/XP packages exist. ([The Retro Web][5])

## What you can be implemented safely from public sources
- PCI probe and BAR/iobase mapping; IRQ handler; WaveCache/PCM engine init (see snd_es1968_chip_init, playback/capture setup); APU register programming; AC’97 reset, read/write, VRA, power; MPU-401 UART subdevice; optional gameport. ([codebrowser.dev][6])
- Known constraints: 28-bit DMA addressing and 4 MiB window constraint; allocate DMA buffers accordingly or use IOMMU/bounce buffers. ([codebrowser.dev][2])

## Key documents/resources
• Linux kernel source snd-es1968 and Kconfig help for device IDs and options. ([GitHub][3])
• AC’97 spec rev 2.2/2.3 for codec programming and AC-link timing. ([Alsa Project][7])
• BSD manpages for alternative initialization details. ([Manuale NetBSD][4])

ESS ES1918 (AC’97 codec)
• Type: stand-alone AC’97 audio codec and mixer intended to pair with Maestro controllers. Stereo 16-bit DAC/ADC, seven inputs, two outputs, TDM support; standard AC-link to controller. ([The Retro Web][8])
• Driver surface: treat it as a standard AC’97 codec on the controller’s AC-link. Implement AC’97 cold/warm/reset, read/write of 0x00–0x7E registers, mixer routes, power states, variable rate audio if advertised. Use AC’97 2.2/2.3 for register map and slot control. ([Alsa Project][7])
• Notes: product briefs exist; a full ES1918 programming guide is scarce publicly, but interoperability follows AC’97. Start with generic AC’97 driver logic and enable any ES1918-specific extended registers only if documented. ([The Retro Web][8])

Key documents/resources
• ES1918 AudioDrive AC’97 codec brief. ([The Retro Web][8])
• AC’97 spec rev 2.2/2.3 for all timings, slots, and register semantics. ([Alsa Project][7])
• Example AC’97 controller docs (e.g., Intel ICHx) show DMA engines, buffer descriptors, and AC-link resets that generic drivers implement; useful for validation. ([Intel][9])

Dream S.A.S SAM9707 (Integrated Sound Studio)
• Type: dual-core audio SoC: a synthesis/DSP RISC plus a 16-bit control CISC. Firmware-downloadable at boot. Supports wavetable synthesis, MPU-401 UART, AdLib-compatible I/O, serial MIDI I/O, effects (reverb/chorus), 64-voice engine, external DRAM up to tens of MiB. Typically requires an external DAC/codec; can directly drive ISA bus signals. ([sewoon.com][10])
• Host interfaces (from datasheet):
– Parallel 16-bit host bus with timing to directly attach to ISA; 24 mA drivers. ([digchip.com][11])
– MIDI: serial MIDI in/out and an MPU-401-UART-compatible parallel interface for game-port style connections. ([digchip.com][11])
– “Game-compatible synthesis with AdLib interface” for OPL-compatible I/O mapping. ([digchip.com][11])
• Driver model options:
– If wired as MPU-401 UART: you can expose it using a standard MPU-401 UART driver; bank management and effect control then need vendor firmware commands, not public. Consult the datasheet’s command protocol and Dream’s tooling. ([digchip.com][11])
– If wired as memory-mapped ISA device: implement a native driver that downloads firmware at init, configures DRAM, uploads soundbanks, and handles voice/effect commands. Use the SAM9707 register/pin timing and firmware load sequences from the datasheet. Public Linux support for its cousin SAM9407 exists only in incomplete reverse-engineered form; ISIS boards with SAM9707 were Windows-only for advanced features. Plan for reverse-engineering if advanced functions are required. ([SourceForge][12])

Key documents/resources
• SAM9707 full datasheet (Atmel/Dream doc1711). Contains block diagrams, host bus timing, register sets, firmware boot method, DRAM map, and MIDI/AdLib interfaces. Primary reference. ([sewoon.com][10])
• Dream official tools page (SamVS, ProgSam). Useful to understand firmware formats and device programming workflows. ([docs.dream.fr][13])
• Historical context and board docs: Maxi Studio ISIS pages confirm SAM9707 deployment and constraints; helpful for hw topology. ([The Retro Web][14])
• MPU-401 technical manuals for UART/intelligent mode protocol details if your board exposes MPU-401. ([Archivio Internet][15])

Pragmatic implementation plan (Linux/Unix-like)

1. ES1978MS
   • Base a driver on ALSA snd-es1968. Keep the 28-bit DMA and 4 MiB window constraint. Implement AC’97 codec attach and VRA. Expose a rawmidi subdevice by instantiating an MPU-401 UART at io_base + ESM_MPU401_PORT when present. Wire up PM only on allowlisted subsystems. ([codebrowser.dev][6])
2. ES1918
   • Use your AC’97 core to reset and enumerate the codec, then program mixer routes and power states as per 2.2/2.3. Treat ES1918 as spec-compliant unless you discover vendor extensions. ([Alsa Project][7])
3. SAM9707
   • Determine wiring: MPU-401 UART vs native parallel host bus.
   • For MPU-401 UART wiring: bind to a standard MPU-401 UART driver for MIDI I/O; optional vendor IOCTLs can manage bank select if the board exposes such hooks. ([ManualsLib][16])
   • For native host bus: implement device probe, firmware download at boot, DRAM init, and a command API for synthesis/effects. Use doc1711 for register timings and boot sequence; expect reverse-engineering for undocumented opcodes and bank formats. ([sewoon.com][10])

Useful secondary references
• Performance characterization of Maestro-2E DACs for sanity-checking sample-rate behavior. ([baudline.com][17])
• OEM driver packages to extract mixer defaults, GPIO setups, and firmware blobs where legally permitted. ([helpdrivers.com][18])

If you need, I can turn this into a concrete Linux driver work plan with module layout, init order, register programming sequences, and a bring-up checklist per chip using only the cited sources.

[1]: https://bsd-hardware.info/?id=pci%3A125d-1978&utm_source=chatgpt.com "Device 'ESS Technology ES1978 Maestro 2E' - bsd-hardware.info"
[2]: https://codebrowser.dev/linux/linux/sound/pci/es1968.c.html "es1968.c source code [linux/sound/pci/es1968.c] - Codebrowser "
[3]: https://github.com/torvalds/linux/blob/master/sound/pci/es1968.c?utm_source=chatgpt.com "linux/sound/pci/es1968.c at master · torvalds/linux · GitHub"
[4]: https://man.netbsd.org/NetBSD-8.1/esm.4?utm_source=chatgpt.com "esm (4) - NetBSD Manual Pages"
[5]: https://theretroweb.com/drivers/491?utm_source=chatgpt.com "ESS ES1978 (Maestro-2E) Driver - The Retro Web"
[6]: https://codebrowser.dev/linux/linux/sound/pci/es1968.c.html?utm_source=chatgpt.com "es1968.c source code [linux/sound/pci/es1968.c] - Codebrowser"
[7]: https://www.alsa-project.org/files/pub/datasheets/intel/ac97r22.pdf?utm_source=chatgpt.com "Audio Codec ‘97 A - AlsaProject"
[8]: https://theretroweb.com/chip/documentation/es1918b-660fd6969a053245639871.pdf?utm_source=chatgpt.com "ES1918 Audio Drive Audio CODEC Product Brief - The Retro Web"
[9]: https://www.intel.com/Assets/PDF/manual/252751.pdf?utm_source=chatgpt.com "Intel® 82801EB (ICH5) I/O 82801ER (ICH5R), and 82801DB (ICH4 ..."
[10]: https://www.sewoon.com/icmaster/Semi/atmel/pdf/doc1711.pdf?utm_source=chatgpt.com "SAM9707, Integrated Sound Studio - sewoon.com"
[11]: https://www.digchip.com/datasheets/parts/datasheet/054/SAM9707.php?utm_source=chatgpt.com "SAM9707 datasheet - DigChip"
[12]: https://sourceforge.net/projects/isisalsa/?utm_source=chatgpt.com "Guillemot Maxisound ISIS ALSA driver - SourceForge.net"
[13]: https://www.docs.dream.fr/downloads.html?utm_source=chatgpt.com "Dream Sound Synthesis & Audio Applications"
[14]: https://theretroweb.com/expansioncards/s/guillemot-maxi-studio-isis?utm_source=chatgpt.com "Guillemot Maxi Studio ISIS - theretroweb.com"
[15]: https://archive.org/details/mpu401technicalreferencemanual?utm_source=chatgpt.com "MPU-401 Technical Reference Manual : Roland Corporation : Free Download ..."
[16]: https://www.manualslib.com/manual/3563183/Roland-Mpu-401.html?utm_source=chatgpt.com "ROLAND MPU-401 TECHNICAL REFERENCE MANUAL Pdf Download"
[17]: https://www.baudline.com/solutions/full_duplex/maestro2e/index.html?utm_source=chatgpt.com "baudline solution - ESS Maestro 2E"
[18]: https://www.helpdrivers.com/sound/ESS/ES1978_Maestro-2E_PCI_AudioDrive/?utm_source=chatgpt.com "ESS ES1978 Maestro-2E PCI AudioDrive Sound Card ... - HelpDrivers"
