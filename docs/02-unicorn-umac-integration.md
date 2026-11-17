# PLUS-EMU-0007 — Unicorn–uMac Floppy Integration

**Status:** Approved architectural direction, detailed peripheral/protocol design pending  
**Date:** 17 November 2026  
**Related:** [Introduction](00-introduction.md) · [Roadmap](01-development-roadmap.md)

## Goal and motivation

Debugging the STM32F411 in the eventual ATF1508-based physical floppy emulator is difficult. Our development machine should therefore run:

1. **The original MC68000 Macintosh ROM and `.Sony` disk code** under Musashi/uMac.
2. **The exact STM32F411 production firmware binary** under Unicorn (Cortex-M4 target).
3. **The exact SPI-NOR flash-device binary image** used by the physical disk cartridge/storage.

The emulator joins the Macintosh IWM registers to the STM32's expected **ATF1508-facing SPI interface** by any convenient host-side mechanism. The bridge is intentionally not a hardware simulator. The observable contract at **both ends** must be correct.

We want to discover track handling, buffering, GCR/sector interpretation, writes, erases, filesystem boot and persistence bugs before deploying firmware to the physical device. This is especially important for reproducing rare errors with CPU instruction tracing, memory watches and programmable fault injection.

## Component boundaries

```text
         ORIGINAL MACINTOSH PLUS SOFTWARE
         ┌───────────────────────────────┐
         │ MC68000 (Musashi)             │
         │ Unmodified Mac Plus ROM       │
         │ Original .Sony driver         │
         │ Macintosh IWM register model  │
         └──────────────┬────────────────┘
                        │ IWM reads/writes, control,
                        │ data and timing-visible results
               ┌────────▼────────┐
               │ DISPOSABLE HOST │
               │  IWM↔SPI BRIDGE │
               │                 │
               │ Any translation,│
               │ buffering or    │
               │ scheduling that │
               │ preserves each  │
               │ endpoint API    │
               └────────┬────────┘
                        │ SPI transactions and relevant
                        │ firmware-visible GPIO/events
         ┌──────────────▼────────────────┐
         │ STM32F411 (Unicorn Cortex-M4) │
         │ Production firmware ELF/BIN   │
         │ STM32 SPI / DMA / IRQ models   │
         │ Firmware track/sector logic   │
         │ NOR-flash SPI controller      │
         └──────────────┬────────────────┘
                        │ SPI NOR commands
                ┌───────▼────────┐
                │ SPI NOR model  │
                │ full raw image │
                │ e.g. >=16Mbit │
                └────────────────┘
```

The final hardware has an ATF1508 CPLD between the Mac's drive interface and STM32. **Do not emulate the CPLD's internal RTL, glue logic, signal propagation or electrical levels here.** The bridge supplies its expected *observable* SPI-level messages/events, potentially pre-decoded from Macintosh activity. The real CPLD is validated separately against timing and signal tests.

## Endpoint fidelity requirements

### Macintosh endpoint: strict

- Execute original 68K software and ROM startup, not an emulator-only replacement disk driver.
- Expose the original Macintosh IWM register addresses and required read/write semantics.
- Reproduce the control/status and data-stream observations the original `.Sony` driver requires to read/write/format an 800K disk.
- Start with the Macintosh Plus hardware profile (ROM overlay, interrupts, VIA, SCC, 1bpp video and RAM as applicable).
- Keep **patched uMac mode** as a separate baseline mode; no paravirtual `PV_SONY_ADDR` shortcuts in authentic integration tests.

**Important:** Although we do not emulate a complete spinning Sony drive, the unmodified ROM driver still depends on some observable controller status, synchronization and data timing. We may synthesize that behaviour in the IWM model/bridge at whatever abstraction is simplest, but we **must not** discard constraints the original driver actually checks. "Controller-level IWM" does not mean returning arbitrary register values.

### STM32 endpoint: strict

- Load the same firmware binary produced by the normal physical-build pipeline; do not substitute a separately compiled host implementation.
- Map internal STM32 flash and SRAM at the target addresses, initialize the Cortex-M vector table/stack/reset state, and execute the actual Thumb/ARM firmware.
- Model the **peripheral register semantics** used by firmware. Candidate set: RCC, GPIO, SPI, DMA, SysTick/timers, NVIC/SCB and internal flash control if used.
- Route interrupts and DMA completions in a manner consistent with the virtual peripheral state. Firmware must not receive "completion" without the corresponding bytes or state changes.
- Present the same external ATF1508-to-STM32 SPI transaction format (and any other firmware-visible control signals agreed in the hardware design).
- Allow debugger attachment, symbolic ELF inspection and tracing without requiring firmware-specific instrumentation or recompilation.

An ELF can supply symbols while a raw `.bin` supplies the bytes; verify that they correspond to **one production build**. Any emulator-only boot bypass or firmware patch invalidates a production-fidelity test.

### SPI-NOR endpoint: strict logical semantics

- Back the flash chip with a whole-device binary image, not merely an 800K logical floppy image.
- Respect key SPI NOR commands and state transitions: read, write-enable, page program, sector/block erase, write-in-progress/status, and NOR program/erase bit semantics as required by the chosen chip.
- Preserve image contents across simulated STM32 resets and full emulator restarts when requested.
- Provide deterministic snapshots/diffs and hashing for regression tests.
- Define the image layout in the physical STM32 firmware/media-format specification; **the emulator may not invent a second format**.

For a 16-Mbit flash, whole-device capacity is 2,097,152 bytes. Larger flash devices may be selected later; keep the programmer and emulator capacity-aware.

## The intentionally permissive bridge

**In scope:** observe IWM register reads/writes; track requested head/track/drive state; parse or construct the Macintosh-side serial data needed by the unmodified driver; translate those events into the firmware-defined SPI messages; queue/transfer bytes; synthesize necessary ready/status events; carry read responses back to IWM-visible state; collect diagnostic traces.

**Out of scope:** ATF1508 HDL emulation, clock dividers, physical SPI edges, CRT/video electrical signals, Sony motor mechanics, analog PLL/servo fidelity and faithful cable propagation delays.

A useful bridge can be implemented as a direct in-process callback, a memory queue, or a message pipe. These are implementation choices, not architectural commitments. The bridge may use high-level knowledge of 800K tracks or GCR to avoid simulating a physical transport, but **must not bypass the actual STM32 track/sector, buffer or NOR code that a test intends to exercise**.

The Macintosh's original floppy driver may operate on timeouts/handshakes, so use an explicit **virtual-time/event scheduler** for essential delays and ordering. Virtual time is *not* a commitment to bit-exact waveforms. Record any simplified timing assumptions and test them on hardware separately.

## SPI interfaces: keep the two roles distinct

The STM32 has logically separate interactions:

1. **Mac/ATF1508-facing SPI:** often an STM32 slave receiving CPLD-produced control/data transactions; exact clocking, framing, and DMA use are determined by the physical design and production firmware.
2. **External SPI NOR:** the STM32's flash-device transactions, through a separate model implementing its memory chip commands and status.

Even if the implementation eventually shares code or hardware resources, the test architecture must not confuse the interface that *feeds the STM32* with the one that *stores the disk image*.

## Host floppy-image programmer dependency

The desktop programmer (PLUS-SYS-0002) takes a **logical Macintosh 800K floppy image** and produces an entire SPI-flash device image in the exact representation the firmware expects. The chosen format is defined with the firmware, including:

- Track/head geometry and sector order, with 800K variable-speed zones.
- Raw encoded sector/header/GCR data and synchronization/gap handling according to the selected design.
- Track directory/offset/length metadata, if present.
- Chip capacity, erased space, optional header/checksum and flash erase/program constraints.

The current design discussion anticipates 703-byte GCR-encoded sector records, 12/11/10/9/8 sectors per track across zones, and sector-vs-full-track write discrimination using SYNC6 sequences. These are **design inputs requiring final firmware-format confirmation**, not a frozen on-flash file-format specification.

One programmer output must be loadable into the Unicorn flash model **without conversion** and programmable into the real flash chip. The emulator may keep sidecar test metadata outside the device-image bytes; the physical flash bytes themselves remain identical.

## Boot and I/O test sequence

1. Begin with a known-good original Macintosh 800K floppy image and a reproducible whole-device NOR image produced by PLUS-SYS-0002.
2. Boot the Macintosh Plus ROM in authentic mode; log IWM reads/writes.
3. Translate requests via the bridge to firmware-visible SPI and other required peripheral events.
4. Step Unicorn, deliver DMA/interrupt events and execute the production firmware's buffers/track-state logic.
5. Let the firmware read/program/erase the simulated SPI NOR as it would the actual device.
6. Return expected data/status to the IWM model so that the original ROM driver continues.
7. Verify ROM boot, filesystem reads, representative sector write/read-back, track/format operations, error paths and persistence after reset.
8. Capture the 68K trace, ARM trace, endpoint transactions, firmware build hash, raw-NOR starting/ending hashes and test result.

## Initial emulator acceptance matrix

| Scenario | Required observation |
|---|---|
| ARM reset/bring-up | Production firmware reaches operational state without emulator-only firmware changes |
| SPI receive | Original Macintosh-side activity reaches STM32 SPI registers/DMA as firmware expects |
| NOR read | Firmware receives chip-compatible data and status |
| NOR program/erase | Correct memory bytes and status changes, respecting chip semantics |
| Macintosh read | Original .Sony/IWM path receives correct disk bytes |
| Macintosh write | Firmware updates NOR, subsequent read returns the change |
| Full-track/format | Firmware's actual full-track detection/path executed |
| Reset and reopen | Disk data persists in same raw flash-image format |
| Error injection | Timeouts, malformed transfers and flash errors produce diagnosable outcomes |
| Original OS boot | Unmodified ROM boots a known original Macintosh System disk end-to-end |
| Tima/Plus boot | The same virtual mechanism boots a developer-generated Tima/Plus disk |

Tests explicitly classify the level proven: **endpoint software**, **emulated peripheral semantics**, or **physical hardware timing/electrical validation**. The third category can only be closed by hardware testing.

## Implementation notes and open questions

- Investigate existing STM32 peripheral emulation code already held by the project owner; reuse it if suitable for F411, license and coverage.
- Verify Unicorn's Cortex-M4F instruction/FPU/exceptions behavior required by the chosen firmware build. Write focused CPU/peripheral self-tests before attempting a disk boot.
- The hardware-facing SPI message format must be documented jointly with ATF1508 RTL and firmware. The bridge deliberately depends on that contract, not on RTL internals.
- Ensure IWM controller visibility to the original ROM is strong enough to satisfy boot and write paths even while mechanical simulation is absent.
- Keep CLI/headless regression tests possible; the SDL/GUI frontend must not be required for all automated tests.
- The real F411 flash, SPI and DMA configuration may evolve; track exact binary/SDK version used for each passing integration test.
- Full real hardware verification follows this virtual milestone, not the other way round.

## Not required for PLUS-EMU-0007

A full Mac floppy drive mechanical emulator; accurate ATF1508 logic simulation; an emulated SCSI disk; LCD/PSU support; modified Macintosh ROM; or a desktop-level reimplementation of the STM32 firmware.
