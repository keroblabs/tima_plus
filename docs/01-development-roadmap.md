# Tima/Plus — Development Roadmap

**Status:** Initial agreed plan · 17 November 2026  
**Companion documents:** [Introduction](00-introduction.md) · [Unicorn–uMac Integration](02-unicorn-umac-integration.md) · [Tima SDK Integration](03-tima-sdk-integration.md)

## Scheduling principles

The roadmap is **floppy-first**. Earlier discussion proposed implementing NCR 5380 and BlueSCSI before unmodified-ROM floppy support; this document **supersedes that order**. The end goal remains the same binary/system running on both the emulator and the original M0001AP. Ticket numbers are stable identifiers, **not** execution order.

Development takes place along two related tracks:
- **Virtual Macintosh + Tima/Plus:** uMac/Musashi, original ROM, IWM interface, Unicorn/STM32 integration, boot-image generation, common Tima SDK and desktop.
- **Physical peripherals:** ATF1508/STM32 floppy emulator, internal mechanism replacement and later external variant; BlueSCSI hardware/enclosure and eventual LCD/PSU. This track can proceed in parallel once the emulator contracts are established.

Do not confuse three media artifacts:
1. **Logical 800K floppy image:** Macintosh-visible sectors, boot blocks and eventual Tima/Plus content.
2. **Raw SPI-NOR device image:** Complete byte-for-byte contents of the physical cartridge/storage chip, including the firmware-defined encoding, track metadata and unused regions.
3. **SCSI hard-drive image:** A later BlueSCSI-compatible boot device, with its own partition/driver/bootstrap requirements.

## PLUS-DEV programme milestones

### PLUS-DEV-0001 — Establish the uMac reference baseline

**Scope:** Build upstream uMac on the development host with Musashi, an identified supported Macintosh Plus ROM, and a known-good historical Macintosh System disk. Preserve the original uMac behavior as the reference. Record reproducible build steps and ROM-image version/checks without distributing copyrighted ROM or System Software.

**Acceptance:** SDL frontend boots historical Macintosh System Software with display, keyboard, mouse and virtual floppy; test results and known limitations are documented.

**Implementation:** PLUS-EMU-0001, PLUS-EMU-0002.

### PLUS-DEV-0002 — Establish the M0001AP hardware profile

**Scope:** Evolve uMac into a dedicated Macintosh Plus model: MC68000, accurate 1–4 MB RAM configurations as applicable, original 512 × 342 1bpp screen, ROM overlay/address decoding, VIA, SCC, RTC/keyboard/mouse and IWM **register-level** control. Identify gaps in timers, interrupts, video page selection and resets. Describe SCSI controller presence/addressing, with full disk boot implementation deferred.

**Acceptance:** A documented hardware-profile test suite exercises ROM startup and peripheral accesses. Original System Software remains usable in the patched/virtual-media bring-up configuration. The model does not silently substitute virtual storage for accurate hardware tests.

**Implementation:** PLUS-EMU-0008, supported by PLUS-EMU-0006.

**Boundary:** This milestone establishes the **M0001AP machine model**, not yet an unmodified-ROM boot from a physical-equivalent drive.

### PLUS-DEV-0003 — Run the unmodified Macintosh Plus ROM

**Scope:** Remove the ROM patch requirements for the authentic mode and implement IWM control/status interactions required by the ROM and original `.Sony` driver. Retain patched uMac mode solely as a regression baseline.

**Acceptance:** Boot progresses through original ROM initialization with an **unmodified, identified ROM image**; authentic mode never relies on `PV_SONY_ADDR` or a replaced `.Sony` driver. Full media boot is accepted in the following milestone, together with firmware integration.

**Implementation:** PLUS-EMU-0003, PLUS-EMU-0008.

### PLUS-DEV-0004 — Complete the virtual 800K floppy subsystem

**Scope:** Run the **actual STM32F411 production firmware** under Unicorn, model only the required Cortex-M peripherals, connect original Macintosh IWM accesses to its expected ATF1508-facing SPI transaction interface using a deliberately simplified host bridge, and load the complete SPI-flash binary image.

**Acceptance:** With an unmodified ROM and original `.Sony` driver, the Macintosh can boot a known-good 800K System floppy, read/write files, and exercise representative media commands through the real STM32 firmware binary. Compare the virtual flash image before/after writes and verify persistence. Never count a ROM patch or emulator-only firmware branch as a pass.

**Implementation:** PLUS-EMU-0007, PLUS-EMU-0003, PLUS-EMU-0006.

**Boundary:** This proves end-to-end *software behavior*, not CPLD RTL, precise flux/bit timing or electrical margins.

### PLUS-DEV-0005 — Establish the floppy-image programmer

**Scope:** Create a development-host tool that transforms a Macintosh-visible 800K floppy image into the **exact raw SPI-NOR layout** expected by the production STM32 firmware, with format/version validation and read-back verification. It should support producing a file for the virtual flash model first and programming/verifying actual flash media later through a suitable programmer.

**Acceptance:** The tool deterministically emits a flash binary accepted unchanged by Unicorn/STM32 and by the selected physical SPI-flash programming path. Data extracted/read through the firmware matches the logical input image; malformed images and capacity limits are rejected.

**Implementation:** PLUS-SYS-0002. The NOR layout is not frozen until the firmware and media-format design are agreed.

### PLUS-DEV-0006 — Boot Tima/Plus from an 800K floppy

**Scope:** Implement the Macintosh-compatible bootstrap and integrate the **shared Tima 6.x SDK** as one pinned Git submodule. The initial boot may rely on original ROM services behind Tima/Plus-specific abstractions; common kernel/framework code must not be copied into this repo. Bring up a console or minimal monochrome UI, input and basic storage.

**Acceptance:** A developer-built raw SPI-flash image boots the **same Tima/Plus binary** under the virtual Macintosh and, when hardware is ready, the original M0001AP through its replacement drive. First emulator-only acceptance is permitted, explicitly marked as such. Boot stays independent of BlueSCSI.

**Implementation:** PLUS-SYS-0001, PLUS-SYS-0003, PLUS-SYS-0004.

**Parallel physical track:** ATF1508 + STM32 firmware bring-up and the internal drive replacement proceed independently after the emulator/firmware interface is testable.

### PLUS-DEV-0007 — Develop the monochrome Tima/Plus desktop

**Scope:** Build a responsive 512 × 342, 1bpp Tima Desktop target with appropriate memory and UI constraints. Use common Tima services and keep window-management implementation choices open; Retro WM may inform design but is not required. Transition from ROM-backed services to native drivers only where justified.

**Acceptance:** Boot, input, drawing, window/event processing and representative applications operate on the 1bpp display using the common Tima SDK; memory footprint is measured for supported RAM configurations.

**Implementation:** PLUS-SYS-0004, PLUS-SYS-0005, PLUS-SYS-0007.

### PLUS-DEV-0008 — Validate the physical floppy replacement

**Scope:** Assemble and validate the ATF1508/STM32F411/SPI-flash replacement for the Mac Plus **internal Sony drive mechanism**. Prove IWM-side timing, signals, GCR behavior and electrical compatibility in physical tests. Expand to the external floppy unit after internal success.

**Acceptance:** Original Macintosh System Software and Tima/Plus both load from and write to real replacement hardware; power cycling retains data. Emulator successes alone do not close this milestone.

**Schedule:** Runs partly **in parallel with PLUS-DEV-0006 and 0007**, not as a prerequisite for developing the OS in the emulator.

### PLUS-DEV-0009 — Migrate to SCSI/BlueSCSI

**Scope:** Finish NCR 5380 modelling with a compatible virtual SCSI disk target, prepare full BlueSCSI-compatible hard-drive images, and implement Tima/Plus SCSI bootstrap and storage services. Use a classic-looking externally powered BlueSCSI enclosure on the physical Mac after electrical and termination checks.

**Acceptance:** Tima/Plus boots and uses the **same hard-drive image** in the emulator and on the real M0001AP through BlueSCSI. Floppy boot remains supported. Original Macintosh System Software remains bootable from appropriate media.

**Implementation:** PLUS-EMU-0004, PLUS-EMU-0005, PLUS-SYS-0006.

### PLUS-DEV-0010 — Evaluate display and power modernization (deferred)

**Scope:** Mechanical measurements, FPGA direct digital-video capture, original monochrome output representation, panel selection, PSU architecture and electrical safety.

**Acceptance:** No hardware commitment until the display physically fits, the timing interface and power requirements are validated, and changes preserve the desired hardware compatibility.

## PLUS-EMU implementation tickets

These IDs preserve the suffixes of the earlier KL-EMU catalogue.

| Ticket | Description | Scheduled against |
|---|---|---|
| **PLUS-EMU-0001** | Build and run upstream uMac/Musashi on Fedora; record baseline | DEV-0001 |
| **PLUS-EMU-0002** | Boot original Macintosh System Software through existing uMac paravirtual disk | DEV-0001 |
| **PLUS-EMU-0003** | Authentic, unmodified-ROM execution with required IWM controller/control-status behavior; no full drive mechanics | DEV-0003, DEV-0004 |
| **PLUS-EMU-0004** | NCR 5380 SCSI controller model and bus-facing operations | DEV-0009 |
| **PLUS-EMU-0005** | Boot from the same SCSI hard-drive image format intended for BlueSCSI | DEV-0009 |
| **PLUS-EMU-0006** | Debugger: execution trace, breakpoints, register/memory views, peripheral transactions, deterministic test hooks | Begins DEV-0002, grows throughout |
| **PLUS-EMU-0007** | Unicorn STM32F411 actual-binary execution, peripheral models, IWM↔SPI software bridge, realistic NOR image and end-to-end tests | DEV-0004 |
| **PLUS-EMU-0008** | M0001AP hardware profile, ROM overlay, 1bpp framebuffer and VIA/SCC/RTC/keyboard/mouse correctness matrix | DEV-0002 |

### PLUS-EMU-0007 detailed acceptance conditions

- Load and execute the **production** Cortex-M4 firmware binary, with ELF debug symbols only as supplemental metadata.
- Model the STM32 peripherals required by the binary, including interrupts, timers, SPI and DMA where used.
- Emulate the SPI-facing contract that the physical ATF1508 would present; no implementation of the CPLD itself is required.
- Keep original Macintosh ROM and driver code unchanged in authentic mode.
- Read and write a byte-for-byte raw SPI-NOR image using appropriate NOR status/erase/program semantics.
- Capture both endpoint traces and verify reads, writes, formatting/full-track operations, resets and persistent flash contents.
- Retain a separate physical-validation checklist for electrical and timing correctness.

## PLUS-SYS implementation tickets

| Ticket | Description | Scheduled against |
|---|---|---|
| **PLUS-SYS-0001** | Macintosh Plus boot blocks and Tima/Plus stage-1/2 floppy loader; verify original ROM boot contract | DEV-0006 |
| **PLUS-SYS-0002** | Host-side floppy programmer: logical 800K image → exact raw SPI-NOR image, plus verify/extract and future physical-flash write adapter | DEV-0005 |
| **PLUS-SYS-0003** | Macintosh platform API: ROM-backed startup, display, input and storage services behind stable boundaries | DEV-0006 |
| **PLUS-SYS-0004** | Integrate one pinned `keroblabs/tima` SDK, selected Mac Plus BSP, common lifecycle and framework | DEV-0006/0007 |
| **PLUS-SYS-0005** | 512 × 342, 1bpp Tima/Plus desktop and application/event layer | DEV-0007 |
| **PLUS-SYS-0006** | Tima/Plus SCSI storage + boot migration, BlueSCSI disk-image tools | DEV-0009 |
| **PLUS-SYS-0007** | Gradual replacement of ROM-backed services with tested native implementations, without breaking API or floppy boot | DEV-0007 onward |

## Definition of done and evidence

Every ticket should eventually record: scope, dependencies, source revision and build/toolchain identity; a reproducible test; observable acceptance evidence; known hardware-vs-emulator gaps; and explicit approval to close. For `PLUS-EMU-0007`, record **firmware binary hash** and **initial and final flash-image hashes**.

The Tima framework repository is authoritative for generic kernel/framework/BSP improvements. This repo is authoritative for the Mac Plus target, programmer and Tima/Plus application integration. The uMac-derived emulator may have its own repository; this roadmap tracks its requirements without assuming it is a Git submodule here.

## Open decisions (do not guess)

- Exact M0001AP RAM configuration and fitted ROM revision.
- Which IWM timing/status details the original ROM requires before disk boot.
- STM32F411 firmware peripheral usage and any existing reusable STM32 emulation models.
- Final raw NOR layout, track metadata, GCR and sector encoding arrangements.
- Host programmer interface for the actual removable SPI-flash device.
- Minimum supported Tima/Plus RAM size and ROM/native driver transition.
- The specific SCSI driver, partition map and boot-image strategy for the later BlueSCSI migration.
