# Tima/Plus — Project Introduction

**Status:** Founding charter (17 November 2026), updated 20 November 2026: **Phase-1 working uMac emulator frozen on `pre-release-umac` pending owner manual IWM write acceptance and merge**  
**Repository:** `keroblabs/tima_plus`  
**Physical target:** Original Apple Macintosh Plus, model M0001AP  
**Operating system:** Tima/Plus, a Macintosh Plus target of the shared Tima framework  
**Current development boundary:** [Phase-1 uMac release checkpoint and next STM32 milestone](01-development-roadmap.md#phase-1-release-checkpoint--working-umac--original-macintosh-plus)

## Purpose

Tima/Plus is a new, independently bootable operating environment for an **actual Macintosh Plus M0001AP**, not a reimplementation of the Macintosh hardware in a different computer. The Motorola MC68000, original logic board, Macintosh ROM and 512 × 342 monochrome (1bpp) display define the initial target.

We will develop against a dedicated uMac/Musashi-based Macintosh Plus emulator, then run the resulting boot media on the physical Mac. The emulator is the primary debug environment; the real machine is the compatibility authority. Original Macintosh System Software must remain usable on the hardware without requiring an irreversible modification.

The earliest Tima/Plus system will **boot from an 800K Macintosh-compatible floppy**. The Phase-1 uMac emulator **already boots original Macintosh System Software with the unmodified ROM and provides software-backed Sony/IWM floppy read/write/format plus virtual NCR 5380 SCSI hard-disk support**. The project owner's final manual write test and merge remain pending on `pre-release-umac`. The **next** work, after the pause, is to implement STM32F411 production floppy firmware and integrate its actual Cortex-M4 binary with uMac under Unicorn, then generate its raw SPI-NOR cartridge images. **Tima/Plus OS** SCSI/BlueSCSI migration is still later; the virtual SCSI disk is already working for original Mac System Software.

## One Tima SDK, no Macintosh kernel fork

`keroblabs/tima` is the **single authoritative Tima 6.x SDK**. It contains the common kernel/lifecycle, framework, interfaces, common peripheral drivers, board support packages (BSPs) and build integration. Each application or target project consumes that repository as **one pinned Git submodule**.

This repository is the Macintosh-specific application/integration project. It may contain the Tima/Plus bootloader, application and desktop source, target configuration, build scripts, disk-image tools, and documentation. It must **not** maintain a copied or independently modified Tima kernel. Generic changes are made upstream in `keroblabs/tima`; Macintosh-specific implementations belong to the selected Macintosh BSP within that SDK. The project's own build chooses the BSP and components.

Tima 6.x framework modernization continues independently; Tima Desktop is the planned later shared desktop direction. We do not require Retro WM as a dependency. The first desktop is constrained by the original Mac's 512 × 342, 1bpp framebuffer and limited RAM, and can initially rely on Macintosh ROM services behind Tima/Plus APIs.

The historical `keroblabs/tima_linux` repository is a reference to an abandoned 5.x restructuring attempt, **not** this project's codebase. The existing Tima 4.2 snapshot remains a historical reference; 6.x is the current modernization path.

## Hardware and media

- **Macintosh:** Original M0001AP; MC68000; original ROM; original IWM and Sony floppy interface; original NCR 5380 SCSI hardware. RAM capacity is a deployment configuration, to be measured rather than assumed.
- **Current development medium (Phase 1):** a fully software-backed, time-driven **virtual Sony 800K drive** in `devel/umac/`, behind the actual IWM register model and original ROM `.Sony` driver; writable floppy images are opt-in, with host checkpoint and original-ROM formatting supported.
- **Next firmware and physical floppy replacement:** production **STM32F411 firmware**, SPI NOR cartridge and **potential ATF1508 assistance** to replace a Sony drive mechanism while preserving the Macintosh-facing IWM interface. The internal drive is first; the external unit follows. MCU/CPLD partition, transport framing and electrical level shifting remain unapproved physical details.
- **SCSI:** the Macintosh Plus has an NCR 5380 controller and the **emulator already includes a working virtual SCSI hard disk**. The physical M0001AP has no SCSI disk installed in the owner's current setup. A later BlueSCSI enclosure, cabling, power, termination and boot path still require hardware validation.
- **Much later:** An FPGA-captured digital Macintosh video signal, LCD and replacement PSU. LCD fit and safety-critical power details are deliberately deferred pending measurements.

## Next milestone: two real endpoints, one deliberately simplified bridge

**This architecture is future Phase 2, not part of the finished virtual-drive Phase 1.** The Macintosh side runs original 68K ROM/driver code and sees its expected IWM register behaviour, as already implemented. The next endpoint will execute the **same STM32F411 production Cortex-M4 firmware binary** as eventual hardware and receive the agreed real-firmware-facing stream/command interface. The host bridge may translate, buffer, queue or synthesize data if it preserves both software contracts; **it will not emulate CPLD RTL, electrical waveforms or mechanical drive details**. ATF1508 involvement and SPI framing remain future engineering decisions.

We *are* testing the observable software contracts on both sides. The emulated SPI NOR chip is loaded from a byte-for-byte image of the physical flash's intended contents; its erase/program/read semantics must be sufficiently accurate to exercise production firmware.

This approach makes the STM32 firmware debuggable *before* installing it in the physical floppy emulator. It does not eliminate the need for later ATF1508 timing and electrical validation on real hardware.

See [Unicorn–uMac Integration](02-unicorn-umac-integration.md).

## Development milestones (current sequence)

1. **Phase 1 — functional M0001AP/uMac reference (pre-release):** original ROM, Sony/IWM 800K read/write/format, RTC/PRAM, display/input/VIA and virtual SCSI with original System Software. Engineering implementation/code review complete; manual owner write test and PR/merge still pending. **Park at `pre-release-umac`**.
2. **Phase 2 — STM32F411 production firmware (next, when resumed):** implement real capture/transaction/NOR firmware; run its unmodified binary under Unicorn and connect it to the existing uMac IWM via a contract-preserving software bridge. This is [PLUS-DEV-0011](01-development-roadmap.md#plus-dev-0011--stm32f411-production-firmware-and-unicorn-integration-next).
3. **Firmware media programmer and physical emulator:** define raw NOR layout and image-programmer tools, validate the internal replacement drive and optional CPLD on original M0001AP, then the external variant. Physical tests are not closed by Unicorn success.
4. **Tima/Plus boot and desktop:** use shared Tima 6.x services and floppy boot to develop the original Mac's 512 × 342 monochrome interface, with a later move to SCSI/physical BlueSCSI.
5. **Optional display/power upgrades:** defer until measurements, practical need and validated designs.

These are **sequencing/parking statements, not new tickets or renumbered roadmap IDs**. See the [development roadmap](01-development-roadmap.md) for stable IDs, acceptance criteria and the separation between completed emulator SCSI and later Tima/Plus SCSI hardware.

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
- [Unicorn–uMac Integration](02-unicorn-umac-integration.md) — **future** dual-CPU execution and the disposable IWM-to-firmware bridge.
- [Tima SDK Integration](03-tima-sdk-integration.md) — the single-submodule architecture and repository ownership boundaries.
- [IWM Architecture](04-iwm-emulation.md) and [IWM Write Plan](05-iwm-write-implementation.md) — implemented Phase-1 emulator contracts and verification.
