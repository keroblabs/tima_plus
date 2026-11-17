# Tima/Plus — Project Introduction

**Status:** Founding architecture / working project charter (17 November 2026)  
**Repository:** `keroblabs/tima_plus`  
**Physical target:** Original Apple Macintosh Plus, model M0001AP  
**Operating system:** Tima/Plus, a Macintosh Plus target of the shared Tima framework

## Purpose

Tima/Plus is a new, independently bootable operating environment for an **actual Macintosh Plus M0001AP**, not a reimplementation of the Macintosh hardware in a different computer. The Motorola MC68000, original logic board, Macintosh ROM and 512 × 342 monochrome (1bpp) display define the initial target.

We will develop against a dedicated uMac/Musashi-based Macintosh Plus emulator, then run the resulting boot media on the physical Mac. The emulator is the primary debug environment; the real machine is the compatibility authority. Original Macintosh System Software must remain usable on the hardware without requiring an irreversible modification.

The earliest Tima/Plus system will **boot from an 800K Macintosh-compatible floppy**. A developer-side programmer will build the complete raw SPI-NOR image used by our emulated floppy mechanism from a logical floppy-disk image. Once this works, we will extend the emulator and Tima/Plus to boot and run from SCSI/BlueSCSI. SCSI is a *later migration*, not a prerequisite for the first Tima/Plus boot.

## One Tima SDK, no Macintosh kernel fork

`keroblabs/tima` is the **single authoritative Tima 6.x SDK**. It contains the common kernel/lifecycle, framework, interfaces, common peripheral drivers, board support packages (BSPs) and build integration. Each application or target project consumes that repository as **one pinned Git submodule**.

This repository is the Macintosh-specific application/integration project. It may contain the Tima/Plus bootloader, application and desktop source, target configuration, build scripts, disk-image tools, and documentation. It must **not** maintain a copied or independently modified Tima kernel. Generic changes are made upstream in `keroblabs/tima`; Macintosh-specific implementations belong to the selected Macintosh BSP within that SDK. The project's own build chooses the BSP and components.

Tima 6.x framework modernization continues independently; Tima Desktop is the planned later shared desktop direction. We do not require Retro WM as a dependency. The first desktop is constrained by the original Mac's 512 × 342, 1bpp framebuffer and limited RAM, and can initially rely on Macintosh ROM services behind Tima/Plus APIs.

The historical `keroblabs/tima_linux` repository is a reference to an abandoned 5.x restructuring attempt, **not** this project's codebase. The existing Tima 4.2 snapshot remains a historical reference; 6.x is the current modernization path.

## Hardware and media

- **Macintosh:** Original M0001AP; MC68000; original ROM; original IWM and Sony floppy interface; original NCR 5380 SCSI hardware. RAM capacity is a deployment configuration, to be measured rather than assumed.
- **Primary development medium:** An emulated 800K floppy implemented through the **real STM32F411 firmware** under Unicorn, with a permissive software bridge to the Mac's IWM register interface.
- **Physical floppy replacement:** ATF1508 CPLD + STM32F411 + SPI NOR flash, replacing a Sony drive mechanism while preserving the Macintosh-facing IWM interface. The internal drive is the first target; an external drive is a later variant.
- **Later hard disk:** BlueSCSI in a classic-style externally powered enclosure with an approximately 60 cm DB25 SCSI cable. Correct termination, cabling, power and boot compatibility must be validated.
- **Much later:** An FPGA-captured digital Macintosh video signal, LCD and replacement PSU. LCD fit and safety-critical power details are deliberately deferred pending measurements.

## Two real endpoints, one deliberately unrealistic bridge

The Macintosh side runs original 68K ROM/driver code and sees its expected IWM register behaviour. The STM32 side runs the **same production Cortex-M4 firmware binary** as the eventual hardware and sees the SPI protocol its ATF1508 would provide. In between, the host emulator may translate, buffer, queue or synthesize data using any expedient mechanism: **we are not emulating the ATF1508 RTL, electrical waveforms or mechanical floppy drive**.

We *are* testing the observable software contracts on both sides. The emulated SPI NOR chip is loaded from a byte-for-byte image of the physical flash's intended contents; its erase/program/read semantics must be sufficiently accurate to exercise production firmware.

This approach makes the STM32 firmware debuggable *before* installing it in the physical floppy emulator. It does not eliminate the need for later ATF1508 timing and electrical validation on real hardware.

See [Unicorn–uMac Integration](02-unicorn-umac-integration.md).

## Development milestones (in order)

1. **M0001AP emulator baseline:** Bring up uMac/Musashi, then model the Macintosh Plus hardware profile, ROM startup, 1bpp display, VIA, SCC and relevant peripheral register behaviour. Existing ROM patches and paravirtual disk boot are permitted only as temporary bring-up aids.
2. **Unmodified-ROM floppy machine:** Remove ROM patches, provide the required IWM behaviour, connect IWM activity to Unicorn executing production STM32 firmware, and boot an original Macintosh floppy through the virtual firmware/flash path.
3. **Tima/Plus boots from floppy:** Produce bootable logical floppy media; use the host programmer to populate a full SPI-flash image; load the same image in simulation and later hardware. Bring up Tima/Plus using shared Tima components. Physical floppy-emulator development proceeds in parallel.
4. **Tima/Plus desktop:** Establish common services and an efficient monochrome desktop without forking the kernel.
5. **SCSI migration:** Implement/complete NCR 5380 and SCSI target emulation, generate BlueSCSI-compatible hard-disk images, and enable Tima/Plus to boot/run from SCSI while retaining floppy support.
6. **Optional physical upgrades:** External floppy variant, LCD interface and PSU when hardware measurements and validated designs justify them.

The milestones indicate goals, not a claim that the current emulator or firmware already meets them.

## Tracking and documentation conventions

- **`PLUS-DEV-xxxx`** — Programme phases, milestones and cross-project acceptance criteria.
- **`PLUS-EMU-xxxx`** — Macintosh Plus/uMac/Musashi, Unicorn, IWM/SPI bridge and SCSI emulation tasks.
- **`PLUS-SYS-xxxx`** — Tima/Plus boot, Tima integration, image programmer, system and desktop tasks.

Existing conceptual `KL-EMU-0001`–`0007` tasks are renumbered *without changing their numeric suffixes* to `PLUS-EMU-0001`–`0007`. Their implementation order is defined by dependencies and the roadmap, not by their numbers. See [Development Roadmap](01-development-roadmap.md).

## Project boundaries and working rules

1. Keep **one authoritative Tima kernel** in `keroblabs/tima`.
2. Keep uMac/Musashi and Unicorn as emulator/tooling concerns, separate from Tima/Plus runtime code.
3. Test production firmware as a binary, not a host rewrite. Keep the emulated flash image equal to the physical format.
4. Treat ROM-backed services as an initial implementation behind stable Tima/Plus APIs; replace with native drivers progressively.
5. Preserve a path for the physical Macintosh to run its original System Software.
6. Do not represent a successful emulator test as proof of electrical/timing correctness.
7. Record open technical questions instead of encoding unverified hardware facts as design constraints.

## Documents

- [Development Roadmap](01-development-roadmap.md) — stages, acceptance gates, ticket catalogue and sequencing.
- [Unicorn–uMac Integration](02-unicorn-umac-integration.md) — dual-CPU execution and the disposable IWM-to-SPI bridge.
- [Tima SDK Integration](03-tima-sdk-integration.md) — the single-submodule architecture and repository ownership boundaries.
