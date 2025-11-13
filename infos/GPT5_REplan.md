# Reverse Engineering Plan

Focused Win9x plan delivered. Steps, tools, checkpoints and workarounds tuned to VxD / 16-bit drivers and the limits of Windows 9x debugging.

### 1. Containment and lab specifics (Win9x constraints)
One host for analysis. Two VMs minimum:
   - [Windows 98 SE](https://archive.org/details/windows-98-second-edition_20240720) VM (target).
   - Linux analysis VM for tooling ([Ghidra](https://github.com/NationalSecurityAgency/ghidra#readme), [rizin](https://rizin.re/), scripts).

Prefer emulators ([Bochs](https://bochs.sourceforge.io/) or [QEMU](https://www.qemu.org/download/)) for dynamic tracing because they can intercept port I/O and MMIO. Host-only networking. No outbound uploads without review. Keep original package read-only and hashed.

### 2. Artifact acquisition and catalog
Collect every file in the driver package: EXE, CAB, INF, SYS, VxD, VXD, DLL, COM, installers, README, firmware blobs. Compute [sha256](https://emn178.github.io/online-tools/sha256_checksum.html) for each. Extract installers with [7z](https://7-zip.org/download.html) / [cabextract](https://www.cabextract.org.uk/) / [UniExtract](https://github.com/Bioruebe/UniExtract2#readme). Save extracted tree.

### 3. Identify format and driver model (critical)
   Determine whether the driver is:
   - VxD / 16-bit NE (Win9x legacy virtual device).
   - Win32 .sys (Windows NT family backports are rare for Win9x).
   Use file, binwalk, rizin (rabin2), and Ghidra to detect NE vs PE vs raw blob. Record device names and INF-provided resources (IRQ, I/O ranges, DMA).

### 4. Static triage focused on VxD/NE specifics
   - Extract strings across all binaries (strings -a -n 8). Look for port addresses, registry keys, device IDs.
   - Use Ghidra and rizin/[radare2](https://rada.re/n/radare2.html) for NE/VxD support; configure segmentation model for 16-bit segmented x86. Load the binary as NE/16-bit and check entry segment:offset.
   - Search for inb/outb patterns, INT instructions, far calls/interrupt vectors, and DOS/BIOS hooks. Note interrupt vectors used by driver (e.g., INT 0x2F or custom).
   - Extract resources and version info from VxD or installer.

### 5. Disassembly / decompilation for 16-bit code
   - Use at least two engines: Ghidra (with 16-bit project settings) plus rizin/[retdec](https://github.com/avast/retdec) for cross-checks. IDA (if available) handles VxD well but license may restrict use.
   - Reconstruct segment:offset usage. Annotate far pointers, interrupt handlers, and descriptor tables.
   - Identify I/O and MMIO accesses, ISR entrypoints, and any BIOS interactions. Create a register-access matrix (port -> code locations).

### 6. Reconstruct initialization and resource negotiation
   - From INF and disassembly, map how the driver acquires I/O ports, IRQ, DMA. Win9x resource allocation uses CONFIGMG and BIOS/Plug-and-Play primitives; identify driver calls to these services.
   - Document VxD entry points: init, device attach, ISR, cleanup.

### 7. Firmware and blob handling
   - Identify upload routines. For VxD drivers look for segmented memory writes to ports or DMA setup sequences. Extract firmware blobs and map offsets used by the upload code.
   - Use binwalk to detect compression or signatures. If upload code applies transforms (XOR, checksum), recover the algorithm from disassembly.

### 8. Prepare emulator-based dynamic tracing (preferred for Win9x)
   Rationale: native Win9x kernel debuggers are scarce and unreliable. Emulators let you trap every port/MMIO access and snapshot memory.
   - Bochs: strong for port trapping and step-by-step CPU tracing. Use its logging and disk snapshot features to capture I/O sequences.
   - QEMU: use it for full-system emulation and event logging. Use its monitor and trace facilities to capture port I/O and memory.
   - Configure the VM with the same resource assignments the INF expects (I/O ranges, IRQ). Boot Win9x in the emulator and install the driver from snapshot 1.
   - During driver init, log all I/O and memory writes to device addresses. Correlate those traces with disassembly addresses.

### 9. Alternatives if emulator-only is insufficient
   - [SoftICE](https://en.wikipedia.org/wiki/SoftICE) for Win9x historically allowed kernel breakpoints. It is legacy, may be hard to source, and legally/ethically sensitive. Treat SoftICE as a last resort and verify licensing.
   - Use instrumented, "shim" replacement VxD: build a minimal VxD that intercepts the same INF-named device and logs calls from user-mode to the original VxD file. Useful to capture API usage without kernel debugger.
   - Use memory dumps and offline analysis if live kernel debug is impossible. Take full VM memory snapshot and analyze with Ghidra/rizin.

### 10. Dynamic tests to run in emulator
   - Installer run trace: capture file/registry operations (RegShot or legacy FileMon/RegMon on Win9x).
   - Driver load trace: use emulator I/O logging to capture all writes to device I/O ports and DMA descriptors during DriverEntry and device start.
   - Interrupt trace: capture which I/O or register changes lead to interrupts and how driver acknowledges them.
   - Firmware upload trace: capture exact write sequence and timing for firmware upload. Preserve captured data.

### 11. Reconstruct data structures and IOCTLs
   - In VxD context, identify user<->kernel interfaces: DeviceIoControl mappings may be implemented by helper DLLs (VxD accesses via DeviceIoControl emulation). Extract corresponding IOCTL codes and argument layouts.
   - Rebuild DMA descriptor formats and ring buffer layouts by matching memory writes and the DMA engine behavior observed. Produce annotated structures.

### 12. Build local test harnesses and simulators
   - Create a hardware simulator that emulates the I/O behavior discovered. Let your driver under test run against the simulator in the emulator. This allows safe, repeatable validation without original hardware.
   - Build user-mode test apps to exercise IOCTLs and APIs discovered, run them against the simulator and the original driver to compare behavior.

### 13. Clean-room reimplementation approach (if distribution needed)
   - Produce a specification document: registers, init sequence, DMA formats, interrupts, IOCTLs, and firmware handshake.
   - Implement fresh code that follows the spec. Validate against the original driver with your test suite. Keep original disassembly separate from clean-room team if legal constraints require.

### 14. Documentation and deliverables (Win9x-tailored)
   - Annotated Ghidra/rizin projects with segmented addressing and comments.
   - I/O trace logs from emulator, correlated to code offsets.
   - Register map spreadsheet with bitfields, masks, and access direction.
   - Firmware blob analysis and recovered upload algorithm.
   - Test harness and hardware simulator.
   - Final report with reproduction checklist and remaining unknowns.

### 15. Practical limitations and fallback guidance
   - Expect missing pieces: encrypted firmware, proprietary tables, or timing-sensitive init sequences that require the original hardware for final validation. Document precisely what cannot be reproduced and why.
   - If 100% reproduction requires undistributable firmware, provide an adapter that uses original firmware under user’s control rather than redistributing it.

### 16. Validation exit criteria for Win9x
   You can claim full reverse-engineer when:
   - All VxD entrypoints, ISR paths, and device init/teardown sequences are documented and reproducible.
   - I/O traces reproduce identical register write/read sequences and timing-critical flows.
   - Firmware upload sequence is fully understood and can be reproduced (or a safe legal alternative is provided).
   - The test harness demonstrates equivalent behavior to the original driver for all advertised features on the Win9x target.

### 17. Recommended short checklist to start immediately
   - Hash originals, extract artifacts, and identify NE vs PE.
   - Load VxD into Ghidra with 16-bit settings and create initial signatures.
   - Configure QEMU/Bochs VM, take snapshot, install driver, and start I/O logging during install.
   - Capture firmware upload and ISR sequences.
   - Correlate traces to disassembly and build register map.

### Tools summary (Win9x-focused)
- Static: Ghidra (16-bit support), rizin/radare2, RetDec, strings, binwalk, 7z, cabextract.
- Dynamic/Emulation: Bochs, QEMU (full-system), VM snapshots.
- Legacy monitoring: FileMon/RegMon for Win9x installers. SoftICE only as last-resort and with legal check.
- Development: Python for parsers/harness, git, hex editors.
