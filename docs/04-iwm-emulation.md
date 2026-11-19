
# Tima/Plus — IWM Emulation Architecture and Behaviour

**Status:** Two-phase architecture approved; detailed Macintosh Plus-specific implementation guide added for review; implementation pending
**Recorded:** 19 November 2026; read-only/write implementation split approved 19 November 2026
**Target:** Macintosh Plus M0001AP, original MC68000, original IWM and unmodified Macintosh Plus ROM
**Implementation tickets:** [PLUS-EMU-0003](tasks/db/PLUS-EMU-0003.md) (Phase 1: read-only) and [PLUS-EMU-0010](tasks/db/PLUS-EMU-0010.md) (Phase 2: writes)  
**Developer entry point:** [§12 — Mac Plus-specific IWM, Sony motor, head-step and drive-register implementation guide](#12-macintosh-plus-specific-implementation-guide-drive-control-and-head-movement)
**Companions:** [Introduction](00-introduction.md) · [Development Roadmap](01-development-roadmap.md) · [Unicorn–uMac Integration](02-unicorn-umac-integration.md)

## 1. Purpose and approved decisions

This document records the IWM discussions of 18–19 November 2026 and the subsequently approved **two-component, two-phase virtual floppy** plan. It supersedes earlier sequencing assumptions requiring STM32F411/Unicorn or a physical drive emulator before original-ROM virtual-floppy boot. The companion roadmap documents have not yet been reconciled with this updated sequencing.

1. **Two software components:** a register/timing-oriented **IWM behaviour model** (`iwm.c/.h`) and an independent **virtual disk/drive model** (`floppy_drive.c/.h`). Their names and API signatures are proposed, not mandatory pre-existing files.
2. **Phase 1 — `PLUS-EMU-0003`: read-only.** Disable ROM patching in the authentic execution path, execute the unmodified Macintosh Plus ROM and its own `.Sony` driver; implement the software-visible IWM read/control behaviour, drive control/sense, and a circular encoded track buffer populated from a standard 800K logical `.dsk` using 6-and-2 packing and GCR codeword mapping. **Stub the IWM/disk write path:** no track modification, no persistence and no claim of write support. Present the mounted medium as write-protected during this phase.
3. **Phase 2 — `PLUS-EMU-0010`: write-capable.** Implement real IWM write-handshake/buffering and overwrite the circular track stream at its current bit position. Mark changed tracks dirty. On stepping to a different track or changing head/side, decode and validate changes, reverse GCR and 6-and-2, and store modified logical sectors back into the `.dsk`. Also flush safely on eject, medium replacement and orderly shutdown; avoid writes when the track is clean.
4. **The IWM is an independent time-driven register device.** It owns Q6/Q7, mode, latch/rearm, register reads/writes and any hardware-equivalent handshakes; it does not decode disk sectors or access `.dsk` files.
5. **The disk is a bitstream-backed virtual device.** It owns tracks/sides, addressable drive control/sense, an encoded circular bitstream with explicit bit length/phase, and logical-sector image translation. It does not interpret MC68000 memory addresses or implement ROM shortcuts.
6. **Future production firmware is separate.** The initial backend models the logical encoded stream, **not** physical motor/head mechanics, analogue flux, or electrical WRDATA/RDDATA waveforms. The physical STM32-only versus STM32+ATF1508 choice remains deliberately open; the earlier 4× SPI waveform idea has been dropped. No ATF1508-to-STM32 SPI framing is selected by this document.

Preserve the existing paravirtual `.Sony` implementation only as an **explicitly selected legacy regression mode** if it remains useful. The authentic mode must never call `rom_patch()` or rely on `PV_SONY_ADDR`; the original ROM bytes must remain unchanged. Do not confuse the unmodified ROM's `.Sony` driver with the host-side replacement in `disc.c`.

## 2. Two independent software components

```text
             UNMODIFIED Macintosh Plus ROM
                Original .Sony driver
                         |
               Memory-mapped IWM
                         |
           +-------------v--------------+
           | iwm.c / iwm.h              |
           | Soft switches, Q6/Q7,      |
           | mode, read latch, clock,   |
           | sense and ROM-visible I/O  |
           | Phase 1: write STUB        |
           | Phase 2: write handshake   |
           +-------------+--------------+
                         |
          timed read/write encoded bits
          + drive controls / sense values
                         |
           +-------------v--------------+
           | floppy_drive.c / .h        |
           | Track/side, selection,     |
           | circular bit buffer,      |
           | bit phase, dirty flag      |
           | 6-and-2 + GCR mapping      |
           +-------------+--------------+
                         |
               800K logical .dsk
             (512-byte sector payloads)
               Phase 1: read-only
               Phase 2: dirty-track flush
```

**Design rule:** a CPU access does not request the “next byte from the disk”. The Macintosh clock advances an independent track bit position; the IWM reconstructs the bytes presented to ROM read-data polling. Each ordinary eight-bit GCR codeword takes 128 C8M ticks, but synchronization fields require bit-level representation and alignment.

**API direction (illustrative only):** the disk component can expose clock/position advancement, one-bit reads and writes, drive-control/sense operations and dirty-track flush; the IWM consumes these rather than fetching 512-byte sectors. The byte-oriented 6-and-2 and GCR code belongs only to the disk component.

**Phase-1 write stub:** switch/register accesses required during probing or initialization still produce coherent state, but a write request must not alter the bitstream or image. With the virtual medium advertised as write-protected, unexpected write attempts should be logged or rejected without falsely reporting successful durable writes. Full timed write acceptance, bitstream modification and underrun correctness are **Phase 2** responsibilities.

## 3. Evidence: what the unmodified ROM actually does

The principal reference is the disassembly of the original Macintosh Plus `.Sony` driver:

- [Mac Plus ROM `.Sony` driver disassembly](https://github.com/jonathanschilling/mac_rom/blob/main/plus/resources/res_drvr_sony.s) — particularly `Sony_RdAddr`, `Sony_RdData`, `Sony_WrData`, `Sony_Recal`, `Sony_FigTrkSpd`, `Sony_SetUpPoll`.
- Apple's *IWM Design Specification* and the IWM-compatible material in the *SWIM Chip User's Reference* are the authoritative descriptions for register/mode/handshake semantics; exact edition/page and register-timing provisions should be recorded in the implementation ticket.
- [MAME's IWM model](https://github.com/mamedev/mame/blob/master/src/devices/machine/iwm.cpp) is a **secondary engineering reference**, not a substitute for Apple hardware specifications and ROM observations.

### 3.1 Read operation

`Sony_RdData` reads the IWM's data register and tests its high bit:

```asm
SONY_RDDATA_LOOP:
    MOVE.B  (A4),D3
    BPL.B   SONY_RDDATA_LOOP
```

Here `A4` addresses the IWM read-data register (`Q6L` with Q7 low). In the selected latch/asynchronous mode, the driver polls until the IWM presents a complete valid encoded byte with its top bit set. **There is no per-byte VIA timer and no fixed-count delay loop** in this data-transfer path. The CPU performs the decoding *while the next byte is arriving*. Read-data, latch-clearing and synchronization behaviour therefore belong in the IWM.

The data-field routines process approximately **699 encoded data codewords plus four checksum codewords**, in addition to marks, sector number, synchronization/gaps and trailing marks; these are **not** a 703-byte FIFO transfer from the mechanism. The original driver decodes the payload progressively.

### 3.2 Write operation — ROM evidence for Phase 2

`Sony_WrData` changes IWM state from sense to write/load, then repeats:

```asm
SONY_WRDATA_NO:
    TST.B   (A4)
    BPL.B   SONY_WRDATA_NO
    MOVE.B  D1,(A3)
```

In this write state, `A4` selects the **write-handshake register** and `A3` the write-data register. Handshake bit 7 tells the ROM when a new encoded byte can be loaded. At the end, the ROM checks **bit 6** to detect an underrun and leaves write mode using the appropriate Q7 switch. This means writes require a timed output shifter/buffer and state-sensitive status; the emulator must **not** accept an unlimited instantaneous stream of writes.

### 3.3 VIA timers: real, but not per-byte clocks

The ROM `Sony_FigTrkSpd` routine uses **VIA Timer 2** to measure a sequence of tachometer pulses (and time out when no pulses are observed). Other routines use delays/counters/VBL mechanisms for drive operations and timeouts. Therefore VIA timing will matter to wider drive compatibility, but **Timer 2 does not pace the 703 codeword data-field read or write loops**.

The ROM's `Sony_SetUpPoll` disables interrupts for the critical transfer and checks SCC/serial work opportunistically. This helps explain how a real Macintosh services the time-sensitive IWM stream for an entire sector.

## 4. IWM register and soft-switch model

The Macintosh Plus IWM base is **`$DFE1FF`**, with 16 soft-switch positions at **`base + index * $200`**, index 0–15. **Reading or writing a switch address changes the associated control state**; the accessed address is not an ordinary independent byte register. Apply the switch side effect and then perform the state-selected read/write operation.

| Index | Switch | Effect |
|---:|---|---|
| 0 / 1 | PH0L / PH0H | Phase/control line 0 low/high |
| 2 / 3 | PH1L / PH1H | Phase/control line 1 low/high |
| 4 / 5 | PH2L / PH2H | Phase/control line 2 low/high |
| 6 / 7 | PH3L / PH3H | Phase/control line 3 low/high (including strobe use) |
| 8 / 9 | MTROFF / MTRON | Motor/drive enable control |
| 10 / 11 | DRVSEL low/high | Internal/external drive selection |
| 12 / 13 | Q6L / Q6H | Q6 low/high |
| 14 / 15 | Q7L / Q7H | Q7 low/high |

Effective register selection is by Q7 and Q6 state, with motor/enable determining write destination:

| Q7 | Q6 | Motor | Meaning |
|---:|---:|---|---|
| 0 | 0 | Off | Inactive data read (conventionally `$FF`) |
| 0 | 0 | On | Read data register |
| 0 | 1 | Either | Read status register |
| 1 | 0 | Either | Read write-handshake register |
| 1 | 1 | Off | Write mode register |
| 1 | 1 | On | Write encoded data |

The exact interaction of switch side effects with simultaneous data reads, mode transitions, motor-off delay and any unsupported modes is to be checked against the IWM specification. A *read* of a soft switch still changes the selected control line.

### 4.1 Mode $1F

The Macintosh Plus initializes the IWM with **mode `$1F`** (latch, asynchronous, motor-off timer disabled, fast, 8-MHz-clock selection). The ROM checks status and mode readback, including drive-enable status, and retries initialization if necessary. The initial implementation must pass this original ROM sequence, not just return a fixed `$1F` from a stub.

### 4.2 Read state

Keep separate:

- current incoming **serial bit position/shift register**;
- current **read-data latch** presented to the MC68000;
- whether a complete encoded byte has become visible;
- **acknowledgement/rearming/clearing** state, including specified internal delays;
- state changes when Q6/Q7, enable, head selection or mode changes.

In asynchronous latch mode a valid encoded codeword is recognized by its high bit, rather than a separately exposed FIFO-ready interrupt. The original ROM tests bit 7. The IWM has no full-sector FIFO; missed bytes must not be silently queued for later reads.

The often-cited nominal read-register clearing delay of **14 FCLK periods** is a specification item to verify exactly, including which clock is meant, the trigger and its ordering against a new byte. **Do not equate 14 FCLK periods with 14 C8M clocks without evidence.** Further distinctions for sync fields and rearming also require specification-based tests.

### 4.3 Write state

Keep separate:

- write-data holding register;
- serial output shift register and current output bit position;
- buffer empty/ready handshake (bit 7);
- underrun condition/status (bit 6; verify exact polarity/lifetime);
- entry into writing, ongoing transfer, final write splice and exit.

The ROM expects the **IWM** to determine when the next byte can be accepted. First-byte entry from sense mode and final-byte drain must be checked separately. The track backend captures the timed output at the **current stream position**, not necessarily the position of the last CPU read. This full write state machine belongs to **Phase 2**; Phase 1 implements only the safe, non-mutating write stub and register mode transitions.

## 5. Clocking: the key design decision

**Use Macintosh hardware clock ticks, not wall-clock sleeps or floating-point bytes/second, in the emulator's internal interfaces.**

For the standard fast-mode Macintosh Plus transfer discussed here:

| Property | Value |
|---|---:|
| Macintosh Plus master/MC68000 clock used for reference | 7,833,600 ticks/s |
| Nominal serial bit-cell rate | 489,600 bits/s |
| **C8M ticks per bit cell** | **16** |
| **C8M ticks per ordinary 8-bit encoded codeword** | **128** |

The ratio is exact: `7,833,600 / 489,600 = 16`. A normal eight-bit encoded disk byte takes `8 * 16 = 128` C8M ticks. The fixed nominal period is a **hardware stream cadence**, **not** a command to make the ROM execute exactly 128 nominal instructions/cycles between reads. The ROM polls the IWM. For self-sync patterns and unusual bit alignment, preserve a finer **bit-phase** representation rather than forcing every physical interval into an eight-bit byte boundary.

### 5.1 Cycle accounting and wait states

Use a **monotonically increasing 64-bit virtual C8M clock**. Derive track position from elapsed clocks independent of software polling. Late IWM register reads must not advance merely to “the next requested byte”; time may have passed and data may have been lost/overwritten.

**Important distinction:** C8M clock ticks elapsed are not always equal to nominal 68000 instruction cycles. Video and sound logic share **RAM access slots** with the processor and can delay CPU bus completion. The IWM does not compete for video RAM slots, but IWM/ROM/SCC/VIA bus timing may include separate phase-alignment or peripheral synchronization constraints.

Do not assert zero wait states without proving it from original hardware/PAL information. Potential additional bus waits must advance the **IWM stream clock**, even when the CPU is stalled. Conversely, bus contention must not slow the independent encoded stream.

References for follow-up comparison:
- Apple, *Guide to the Macintosh Family Hardware* (Mac Plus RAM arbitration and bus timing).
- [MacPlus MiSTer FPGA `cpu_wait.v`](https://github.com/MiSTer-devel/MacPlus_MiSTer/blob/master/rtl/cpu_wait.v) (phase-dependent wait-state implementation). Its precise PAL-phase values are **secondary-model evidence**, not yet independently verified original-hardware timing.

### 5.2 Musashi integration: what is and is not established

The repository's Musashi exposes:

```c
int m68k_execute(int num_cycles);
int m68k_cycles_run(void);
int m68k_cycles_remaining(void);
void m68k_modify_timeslice(int cycles);
void m68k_end_timeslice(void);
```

The implementation accounts for instruction costs and execution-slice cycles; it **does not guarantee the exact physical bus cycle of an IWM memory callback**. In particular, the base instruction cost is deducted after executing the instruction handler; additional cycle adjustments may occur within instruction handlers. The Macintosh-specific video/sound/PAL wait states are not currently modelled by uMac. Do not assume `m68k_cycles_run()` in the callback alone supplies precise intra-instruction bus timing.

Current uMac uses an approximately 5-ms frontend execution quantum and `m68k_execute(us * 8)` followed by `global_time_us += us`. That is a coarse 8-MHz approximation, **not** a suitable independent IWM clock. Replace or augment it with a clock-driven model using **actual consumed virtual cycles**, explicit bus-wait accounting where required and monotonic timestamps across timeslices; avoid cumulative rounding drift.

First test the access-phase accuracy experimentally with original ROM polling loops; only then decide whether a Musashi timing hook/core change is needed. Preserve the existing practical frontend batch size if accurate peripheral timestamps can be obtained without reducing execution to single-cycle steps.

### 5.3 Sketch of track progression (not final source API)

```c
#define MAC_PLUS_C8M_HZ         7833600ULL
#define IWM_CLOCKS_PER_BIT      16ULL
#define IWM_CLOCKS_PER_BYTE    128ULL

/* now_ticks is a monotonically increasing elapsed C8M clock. */
void track_advance_to(uint64_t now_ticks);
```

A circular encoded track has an independent angular/bit position. Between accesses, its position advances by elapsed ticks; reads can poll repeatedly with no new complete byte, and delayed reads cannot recover an unlimited backlog. Keep sub-byte phase and handle power/track transitions in the state machine.


## 6. Virtual 800K media and circular track buffer

### 6.1 Phase 1: logical .dsk to timed encoded track

Input is a standard **819,200-byte logical 800K `.dsk`**: 80 cylinder tracks, two sides, with 12/11/10/9/8 sectors per track in five 16-track zones. It contains 512-byte logical sectors, **not** a recording of their on-disk self-sync, prologues, checksums or timing.

For the selected track **and side**, the disk component:

1. Reads the corresponding 512-byte logical sectors from the image (honouring the correct zone and sector ordering).
2. Applies Macintosh Sony 6-and-2 packing and maps six-bit symbols to legal GCR codewords, producing the required sector data and tag representation; computes the correct checksums.
3. Constructs the proper address fields, sector-data fields, prologues/epilogues, **self-synchronization**, gaps and interleave. Self-sync includes bit lengths not necessarily divisible by eight; do not substitute a plain byte array without retaining valid bit count and phase.
4. Packs the resulting stream into an allocated **circular bit buffer** with explicit valid-bit length, track/side identity and continuously advancing rotational position.
5. Supplies bits at a nominal one bit cell per **16 elapsed C8M ticks** regardless of MC68000 polls; when read mode is active, the IWM assembles/latches codewords according to the original hardware/ROM contract.
6. On a head step or side change, selects/rebuilds the appropriate encoded buffer; in Phase 1 there is no dirty data to flush.

An implementation may cache multiple (track, side) buffers, but **must not** require a whole-disk encoded cache. Rebuilding on track change is acceptable so long as time advancement, side selection and the original ROM's expected drive status remain coherent. No host filesystem-level sector shortcut may satisfy authentic-ROM IWM reads.

The `.Sony` driver runs within the unmodified ROM and performs its original on-the-fly GCR decoding. The host-side disk emulator **encodes data for reading** because a logical `.dsk` does not already contain these encoded fields.

### 6.2 Phase 2: serial writes to circular bit positions

The original ROM converts its logical sector payload into encoded codewords and writes them into IWM registers. The IWM write shift register emits the corresponding timed **logical encoded bits** to the disk model. It is not necessary for this internal software boundary to turn those bits into physical pin-level NRZI WRDATA transitions and back again.

- Overwrite the **existing circular bitstream** at the position corresponding to elapsed time, respecting bit-level alignment, write-mode entry/exit, sync fields and wraparound; never append bytes or treat writes as 512-byte disk operations.
- Mark a track **dirty only if its encoded contents actually change**, and preserve the distinction between in-memory mutations and committed image data.
- **Primary flush trigger:** before committing a head-step change to a new track. Also handle changing side, drive selection or media, eject, motor-off where appropriate, and clean emulator shutdown. A same-track read/write should observe pending changes from the circular buffer without reloading stale `.dsk` contents.
- During flush, find complete valid sector records, validate address/data prologues and checksums, perform inverse GCR mapping and 6-and-2 decoding, and update the correct **512-byte logical sector payloads** in the `.dsk`. Avoid writing anything if the track is clean.
- Do **not** rewrite the `.dsk` merely because the rotational pointer advanced; do not silently discard a dirty track when decoding or saving fails. If a flush fails, preserve/retry the in-memory data when possible and report a clear error. Ensure clean shutdown/eject does not lose data.
- A standard logical `.dsk` cannot preserve arbitrary raw gaps, invalid codewords or nonstandard tag data. Define and document any nonrepresentable modification or formatting limitation; do not pretend those bytes were losslessly saved.
- Exercise sector writing and read-after-write first, then standard ROM-driven formatting using actual write streams, with explicit acceptance for persistence after reopening.

Read-only and write-capable modes should use the **same** circular stream representation and drive API. The Phase-1 stub is replaced/extended in Phase 2; do not introduce a second high-level `.Sony` replacement.

### 6.3 Encoding terminology

6-and-2 **packs** the Mac data/tag bytes into six-bit values; GCR **maps** those values into legal eight-bit disk codewords. Both operations are performed by the original ROM on a write and by our disk backend when synthesizing a track for a read; the inverse operations convert received encoded sectors back to 512-byte logical payloads at flush. The IWM only serializes/recovers the encoded bitstream and implements register-level handshakes. Exact bit layout, interleave, sync length, sector number and checksum algorithms must be unit-tested against documented Mac 800K examples and the unmodified ROM; do not freeze an assumed raw-SPI-flash layout here.


## 7. Future STM32F411 / Unicorn endpoint (not selected yet)

The initial deliverable is the **IWM + virtual disk + logical `.dsk`** reference. Later, the disk-side backend can be replaced by an interface driven by the actual STM32F411 production firmware running under Unicorn and a production-compatible SPI-NOR image, without rewriting the IWM behaviour visible to the Macintosh ROM.

**No physical drive interface has been selected.** Both STM32-only timed capture/generation and STM32+ATF1508 external logic remain options; we have discarded the proposed 4× SPI waveform-sampling approach. The physical IWM WRDATA pin toggles its level on each encoded logical `1`, while the IWM reads falling RDDATA pulses; those electrical representations are distinct from the internal logical encoded bits. The forthcoming hardware contract must account for pulse timing, bit recovery, control-register multiplexing and possible SPI transfer framing only after that design is investigated.

The nominal Macintosh bitstream cadence remains 16 C8M ticks per bit / 128 ticks per ordinary 8-bit codeword. No direct equality between a future SPI transaction's duration and that cadence has been specified. The physical circuit requires independent electrical and timing validation; this two-phase emulation work does **not** settle the STM32/ATF1508 question.


## 8. Two implementation tickets and acceptance gates

Both tickets are formally opened under `docs/tasks/db/`. **Phase 2 depends on Phase 1**, but both are documented now to keep the component interface stable.

| Phase / ticket | Scope | Required acceptance evidence |
|---|---|---|
| **Phase 1 — [PLUS-EMU-0003](tasks/db/PLUS-EMU-0003.md)** | Disable ROM patching for authentic mode; implement IWM soft switches, mode `$1F`, status, read latch/rearm, elapsed-clock progression; supply drive control/sense from a circular encoded track built from logical `.dsk`. **Write stub only**, medium reported write-protected. | Original **unmodified ROM** and original `.Sony` driver start, recognize the image, read across zones/sides and boot known-good 800K System Software. Read-only tests, byte/bit sync tests, reset/timing traces. The loaded ROM image is verified unchanged; no `PV_SONY_ADDR` calls in authentic mode. |
| **Phase 2 — [PLUS-EMU-0010](tasks/db/PLUS-EMU-0010.md)** | Replace write stub with IWM write buffer/handshake/underrun and bit-positioned circular writes; dirty-track flags; decode/checksum and persist changed logical sectors on step/side change and safe lifecycle events. | Original `.Sony` writes, read-after-write, dirty/clean flush, persistence after image reopen, failed-save recovery and standard formatting as specified. No sector-level bypass. |
| **Later — PLUS-EMU-0007** | Actual STM32 production firmware under Unicorn, separate media endpoint | Repeat ROM read/write/boot tests through firmware and production-compatible raw flash image. |
| **Later — physical drive prototype** | Macintosh IWM pin-level compatibility with whichever hardware architecture is selected | Real-Mac measurement and electrical compatibility; emulator-only evidence is insufficient. |

Keep the two phases independently reviewable. Synthetic bitstream and status tests are necessary but are **not** substitutes for the original-ROM boot (Phase 1) or disk-image write/persistence (Phase 2) milestones. Trace PC, IWM address/data, Q6/Q7/mode, global virtual tick, track/side/bit position, latch and handshake states, dirty state and outcomes. Record ROM identity, emulator revision, test fixtures and explicit pass/fail/not-tested outcomes.

The original ROM is the software acceptance authority. Existing patched/paravirtual uMac floppy support is a regression baseline only and **never counts as authentic IWM completion**.


## 9. Current repository observations (before the implementation)

At the time of this design decision, `devel/umac/src/main.c` still implements IWM access through a mostly stubbed 16-byte `iwm_regs` array. The IWM access window is decoded in `devel/umac/include/machw.h`.

The current `devel/umac/src/rom.c` has ROM patching that replaces the ROM `.Sony` driver; `devel/umac/src/disc.c` implements the current paravirtual floppy path; `devel/umac/src/unix_main.c` calls `rom_patch()`. **Phase 1 must make unmodified-ROM execution the authentic path and disable `rom_patch()` there completely**, not merely skip the replacement floppy entrypoint while leaving other ROM modifications in place. Preserve original ROM bytes; retain an explicit legacy regression option if useful.

SCSI `PLUS-EMU-0004` and `PLUS-EMU-0005` and RTC/PRAM `PLUS-EMU-0009` are already closed following project-owner verification/merge. **`PLUS-EMU-0009` is not available** for virtual floppy work; the write ticket is `PLUS-EMU-0010`.

This document and the issue tickets are a **design/documentation change only**. They do not claim new emulator builds, tests, read-only boot or write support have been executed.


## 10. Open verification questions — do not convert to assumptions

1. Follow the concrete IWM→Sony control/step/motor implementation and tests in §12. Confirm IWM read-latch, acknowledgement and rearm timing (including the specified 14-FCLK-period delay), and distinguish it from the virtual C8M tick counter.
2. Confirm the IWM mode `$1F` and every required PH/Q6/Q7/drive-sense interaction in the unmodified ROM. For Phase 1, implement enough write-register stub behaviour for safe read-only operation, **not** the future timed write state machine.
3. Measure or model when Musashi memory callbacks occur within CPU instructions; advance the independent track clock despite emulated bus stalls and avoid cumulative timing drift.
4. Validate exact zone sector ordering, sector headers, 6-and-2/GCR/checksum algorithms, ten-bit self-sync structure, interleave and circular track bit length for the selected `.dsk` fixtures.
5. Determine the least invasive way to disable all ROM patching in authentic mode while preserving the prior patched/paravirtual regression route.
6. For Phase 2, define how to handle incomplete/invalid rewritten records, nonrepresentable tags/gaps, format operations and failed track flushes without silent image corruption.
7. Defer hardware choices: physical clock recovery, motor-speed PWM relevance, STM32 timers versus ATF1508, SPI framing and pin-level timing remain independent investigations and are **not prerequisites** to these two software tickets.


## 11. Review and implementation handoff

The project owner has approved the **two-component design** and **two-ticket read-only-then-write sequence**. Proceed using [PLUS-EMU-0003](tasks/db/PLUS-EMU-0003.md) as the first bounded change; use [PLUS-EMU-0010](tasks/db/PLUS-EMU-0010.md) only after Phase 1 has a stable, tested bitstream/drive contract. Implementers must follow the [task methodology](tasks/bug-tracking-methodology.md): make **local `git commit`(s)** for implementation changes, **do not `git push`**, and report commit SHAs and test evidence for review. The owner controls subsequent publication and merge.

The development roadmap and companion documents can be reconciled in a separately reviewed documentation change. No emulator source code, physical board choice or task-completion state is changed by recording these tickets.

## 12. Macintosh Plus-specific implementation guide: drive control and head movement

**Implementation authority:** This section makes the two-component design actionable for [PLUS-EMU-0003](tasks/db/PLUS-EMU-0003.md) (read-only) and [PLUS-EMU-0010](tasks/db/PLUS-EMU-0010.md) (write). It describes **what we emulate to satisfy the Macintosh Plus ROM**, not a full transistor-level IWM, Sony motor controller or Apple II disk interface.

**Source priority:** (1) the [original Mac Plus ROM .Sony disassembly](https://github.com/jonathanschilling/mac_rom/blob/main/plus/resources/res_drvr_sony.s), especially `Sony_AdrDisk`, `Sony_Sense`, `Sony_Pulse`, `Sony_Seek`, `Sony_Recal`, `Sony_PowerUp`, `Sony_PowerDown`, `Sony_FigTrkSpd` and `Sony_RdAddrSetup`; (2) Apple IWM/SWIM and Macintosh hardware documentation; (3) independent engineering comparisons with [BMOW Floppy Emu CPLD](https://github.com/una1veritas/mac-floppy-emu/blob/master/floppy-emu-source-1.0L-F11/CPLD-Xilinx/floppyemu.v) and [ESFloppy's Sony interface](https://github.com/alexthecat123/ESFloppy/blob/main/ESFloppy/sonyInterface.cpp). Neither secondary implementation is a reason to skip original-ROM testing.

### 12.1 Two levels of control: IWM versus Sony 3.5-inch drive

A Macintosh 3.5-inch Sony drive does **not** directly receive four stepping-motor phases. The IWM PH lines address **drive registers**, and only an actual strobe executes a control command:

| Origin on Mac Plus | Signal at Sony interface | Emulator responsibility |
| --- | --- | --- |
| IWM PH0 | CA0 | Sony register address bit |
| IWM PH1 | CA1 | Sony register address bit |
| IWM PH2 | CA2 | Read-register address high bit **or** data value for write commands |
| IWM PH3 | LSTRB | Drive-control **write strobe**; normally pulse high then low |
| VIA Port A bit 5 | SEL / head-select | Extra Sony register address bit; selects read-data side 0 or 1 when addressing the data registers |
| IWM Drive-Sel | Internal/external position | Choose `/ENBL1` for internal or `/ENBL2` for external |
| IWM Motor-On latch | Drive-enable gate | Gate selected active-low `/ENBLx`; **not the drive's spindle-motor command** |
| IWM Q6/Q7 | IWM register selector | Select the IWM data/status/handshake/write registers; not a Sony register |
| Sony read output | RD / sense function | Provide selected drive's status bit, TACH or encoded read stream |
| IWM WRDATA and /WRREQ | Sony serial write interface | Phase 2; abstract to timed logical encoded bits in the virtual emulator |

The Plus original ROM sets **VIA PA5** through `Sony_AdrDisk`. The current `devel/umac/src/main.c::via_ra_changed()` already identifies PA5 as `hd.sel` in a comment, but does not yet deliver it to the virtual floppy. **Phase 1 must connect this path.** Do not lose the other VIA PA functions, including ROM overlay and SCC status.

The system has **two logically distinct motor/enables**:

1. **IWM Motor-On + Drive-Sel** chooses which /ENBLx is asserted; a selected and enabled drive is addressable.
2. **Sony MOTORON register** is a *separate* command sent using PH0–PH3 and PA5. Value `0` starts the spindle; value `1` stops it.

The original ROM's `Sony_PowerUp` selects/enables the drive, queries whether its motor is running, and **strobes the Sony MOTORON command** if not. `Sony_PowerDown` sends the Sony motor-off command and subsequently clears the IWM enable. Emulating only the IWM motor soft switch would be incorrect.

### 12.2 How the Sony drive registers are addressed and read

The drive sees **one enabled position** and four address/control bits. PH3 should be **low** for an ordinary read/sense operation:

```c
// PHx and SEL are one-bit current line values.
// This number is the Sony drive READ register, not an IWM MMIO index.
unsigned reg = (PH2 << 3) | (PH1 << 2) | (PH0 << 1) | VIA_PA5_SEL;
```

The ROM's `Sony_AdrAndSense` first selects the Sony register using `Sony_AdrDisk`; it then selects the **IWM status register** (Q6=1, Q7=0), reads its **bit 7** (SENSE), and returns to IWM read-data mode. No step or motor command happens merely from these address changes.

**Read-register map, physical index above (Mac Plus 800K subset):**

| Index | Function | Required sense/data behaviour |
| ---: | --- | --- |
| 0 | `DIRTN` | Direction latch; `0` inward (towards track 79), `1` outward (towards track 0) |
| 1 | `/CSTIN` | `0` when disk inserted; `1` when no medium |
| 2 | `/STEP` | `0` step busy; `1` step complete/ready |
| 3 | `/WRPROT` | `0` write protected; `1` writable; **Phase 1: mounted media are write protected** |
| 4 | `/MOTORON` | `0` Sony spindle on; `1` off |
| 5 | `/TK0` | `0` head at cylinder 0; `1` elsewhere |
| 6 | `EJECT` sense | Respond compatibly; **reading never initiates an eject** |
| 7 | `TACH` | A **time-dependent** tachometer level, 60 pulses per revolution, not a fixed or per-read toggle |
| 8 | `RDDATA0` | Encoded bitstream from head/side **0** when read is enabled and the spindle is running |
| 9 | `RDDATA1` | Encoded bitstream from head/side **1** (800K drive) |
| 10–11 | SuperDrive-specific/reserved | No 1.44MB/MFM support; confirm the conservative 800K response from ROM probes |
| 12 | `SIDES` | `1`: double-sided 800K drive |
| 13 | `/READY` | `0` ready; `1` not ready, spinning up or seeking |
| 14 | Drive installed/present | `0` drive installed; `1` no drive |
| 15 | Interface/handshake detection | New-interface 800K behaviour; polarity to match original ROM's probe, not guessed |

**Naming pitfall:** the *original ROM passes a different four-bit number* to `Sony_AdrDisk`: `D0[3:0] = {CA1, CA0, SEL, CA2}`. For example the ROM's parameter `TachAdr=$E` maps to physical **read index 7**, *not* physical index 14. Compare the actual pin bits, not just similarly named numeric register constants.

When index 8 or 9 is selected, the drive output carries **read-data information** and the IWM's serial reader reconstructs codewords. For other indices, the drive output provides a **status/tach/sense** value and the CPU observes it via IWM status bit 7. Do **not** give the IWM a new codeword just because a sense register was polled.

**Drive installed is different from disk inserted.** A mounted internal Sony 800K drive can show installed=0 and disk inserted=0. An installed drive with an empty slot shows installed=0 and disk inserted=1. A physically absent external position must not masquerade as an inserted disk or an HD20.

### 12.3 Command writes: PH3 controls *when* movement occurs

A drive-register command is **not** an IWM data-register write. The ROM sets PH0/PH1, VIA PA5 and PH2, and invokes `Sony_Pulse` to generate a **PH3 high→low strobe**, with two NOPs for the high pulse width:

```c
// Evaluate only on PH3/LSTRB's rising edge, for an enabled drive.
unsigned cmd_reg = (PH1 << 2) | (PH0 << 1) | VIA_PA5_SEL;
bool cmd_value = PH2;
```

| Write index | Name | Behaviour |
| ---: | --- | --- |
| 0 | `DIRTN` | Set direction latch: `0` step in (+1 track); `1` step out (−1 track). **Does not move the head** |
| 2 | `STEP` | Written value `0` requests **one** step in latched direction; `/STEP` becomes busy and later ready |
| 4 | `MOTORON` | `0` starts spindle; `1` stops spindle |
| 6 | `EJECT` | `1` requests eject (original ROM holds an extended strobe/command); maintain correct disk-in-place state, and in Phase 2 flush before removal |
| Other | Extensions/reserved | No movement and no invented success; add only if original ROM uses them |

**A change of PH0, PH1, PH2, VIA SEL or IWM Drive-Sel alone does not move the head**. One valid **rising** PH3 edge triggers at most one command; PH3 remaining high across multiple reads cannot produce multiple steps. PH3 falling ends the pulse. If no drive is enabled or present, strobing does not modify any other drive.

The ROM's `Sony_AdrDisk` raises CA0 and CA1 before changing PA5/SEL, then lowers whichever address bits are needed. Preserve individual soft-switch and VIA events in order; do not decode a command on every address-line change. Avoid false commands during initialization/reset, or when changing drives.

### 12.4 Required virtual-drive state and track-step algorithm

Each logical drive position requires **its own** state. Suggested minimum (illustrative C structure; not a frozen API):

```c
typedef struct {
    bool drive_installed;
    bool disk_inserted;
    bool write_protected;     // always true for Phase 1
    bool spindle_on;          // Sony MOTORON command, NOT IWM Motor-On
    bool step_out;            // 0: track++, 1: track--
    bool step_busy;
    uint64_t step_done_at;    // virtual C8M timestamp
    uint64_t ready_at;        // spin-up/head-settle deadline
    uint8_t cylinder;         // 0..79, shared by both heads
    uint8_t selected_side;    // 0/1 when RDDATA0/1 is selected
    // track bit count, circular position, track cache, TACH phase;
    // Phase 2 adds stream dirty/flush state.
} sony_drive_t;
```

`iwm.c` owns PH0–PH3, Q6/Q7, mode and IWM drive-enable selection; `floppy_drive.c` owns the above Sony controls, cylinder/side, track stream and image. **Do not duplicate an unrelated “enabled spindle” flag**: IWM /ENBL and Sony MOTORON are distinct, but related.

**One accepted STEP command** (command index 2, value 0, PH3 rising, selected drive enabled, not busy):

1. Read the direction previously set through command 0.
2. Set `step_busy=true`: drive read register 2 (`/STEP`) now returns **0**; /READY can report not-ready until settle.
3. Compute `target = clamp(cylinder + (step_out ? -1 : +1), 0, 79)`. **Never wrap around** at cylinder 0 or 79.
4. If target changes, switch to/rebuild the target's encoded circular track. In Phase 2, **flush the old track first if dirty**; do not discard it if decode/save fails. Both heads change cylinder together.
5. Using **virtual C8M elapsed time**, complete the step at a nonzero scheduled deadline. Return `/STEP=1`, and update `/TK0` from `cylinder==0`. /READY follows its own motor and settling conditions.

**Timing:** The original `Sony_Seek` checks /STEP ready, writes direction, issues a single STEP pulse and waits for completion per cylinder. `Sony_Recal` uses 6 ms per step for its “new-interface” drive path and 12 ms for older drives; the 800K path polls /STEP and /READY. Use a bounded millisecond-scale step-completion delay as an initial emulation policy, then tune against **unmodified-ROM traces**. Do not call a fixed delay an established electrical measurement or substitute host `sleep()`. If a step command arrives while busy, reject/ignore with trace rather than double-step.

**Recalibration is not a shortcut.** The original ROM loops: test /TK0 (read index 5), if not zero set DIRTN=1 (out), confirm /STEP ready, pulse STEP=0, wait, repeat until /TK0=0 (or ROM timeout). A virtual drive may initialize at cylinder 0 as a documented power-on policy, but it must support a genuine multi-step recalibration from any other valid cylinder in deterministic tests.

**Side selection is not track movement.** The 800K drive has 80 cylinders and two heads. A change between Sony read register 8 and 9 selects the other head **at the same cylinder**. PA5/SEL also selects many other *status registers*; changing it during status polling must not erroneously permanently switch heads or step cylinders. Retain the selected encoded-track identity as `(drive, cylinder, side)`.

### 12.5 Drive selection, motor, ready and tachometer

- **Drive select:** `IWM_DRIVE_SEL=0` means internal `/ENBL1`; `=1` means external `/ENBL2`. `IWM_MOTOR_ON=0` deasserts both. When enabled, only the selected position responds. The ROM probes internal/external positions independently; even if Phase 1 mounts one image, maintain a coherent absent external response.
- **Spindle:** start/stop only from Sony command index 4; a merely asserted /ENBL does not mean the spindle has already achieved speed. Model a spin-up/ready transition. A motor-off command stops delivering encoded bits and running TACH; it must not reset cylinder or silently erase the track.
- **READY:** Sony read index 13 is **active low**. It should reflect a present installed drive and its current motor/settling state, not a hardcoded permanent ready bit. The ROM's `Sony_WaitReady` checks it after initial wait and then polls.
- **TACH:** Sony index 7 is a **time-derived periodic level**, nominally 60 pulses per revolution; the ROM's `Sony_FigTrkSpd` measures 15 pulses with **VIA Timer 2**, including timeout. Do not toggle TACH merely on each read. Use a documented speed-zone table and verify the ROM-observed period; nominal published speeds are around 394, 429, 472, 525 and 590 RPM, with discrepancies for the innermost band across references. Treat those as calibration inputs, not exact proof.
- **VIA Timer 2 integration:** if the unmodified-ROM path uses `Sony_FigTrkSpd`, T2 count and IFR timeout must advance on the same virtual timeline. Avoid faking the ROM's return value or polling result. The ROM's other VBL/Time Manager motor/seek waits may also require existing VIA work.
- **PWM:** the separate Macintosh PWM signal controls older 400K spindle speed; it is **not the nominal 489,600-bit/s IWM stream clock**. The Plus 800K Sony drive has its own speed regulation; physical PWM waveform generation is outside this Phase-1 virtual-drive model.

### 12.6 Proposed software events and timing order

The IWM implementation must not be a byte-at-a-time “give ROM the next sector byte” service. It updates state on actual MMIO and VIA events; the disk bitstream rotates independently.

For **every** IWM access or relevant VIA PA write:

1. Advance both IWM and floppy virtual time to the **current virtual C8M timestamp**. Retire any step-complete, spin-up/ready or TACH deadlines, and advance the circular read position even if the CPU was doing unrelated work.
2. On an IWM soft-switch access, update exactly the targeted PH, drive-select, Motor-On, Q6 or Q7 latch. If PH3 makes a genuine low→high transition, decode the Sony command and strobe only the enabled drive.
3. On VIA Port A bit 5 change, update the selected Sony `SEL` line at that timestamp. Preserve the port's other functions. Its value participates immediately in read-register selection and subsequent PH3 command decode.
4. If Q7/Q6 select **IWM status**, return mode/enable and the selected drive's **one-bit SENSE**. If they select IWM **read-data**, return the asynchronous read latch filled from the continuous encoded stream only when Sony read function 8/9 is active and disk/spindle/enable conditions hold.
5. On a CPU write to the IWM encoded-data register, **Phase 1 stubs the write safely**, without changing track or file (and reports media write-protected). **Phase 2** must implement the actual timed holding register, serial write shift, handshake/underrun and bit-position overwrite.

**Illustrative API only, not prescribed exact prototypes:**

```c
void iwm_advance_to(uint64_t c8m_ticks);
uint8_t iwm_read_mmio(uint32_t address, uint64_t c8m_ticks);
void iwm_write_mmio(uint32_t address, uint8_t data, uint64_t c8m_ticks);
void iwm_on_via_pa5(bool sel, uint64_t c8m_ticks);

void floppy_drive_command(unsigned drive, unsigned cmd, bool value,
                          uint64_t c8m_ticks);   // PH3 rising only
bool floppy_drive_sense(unsigned drive, unsigned reg, uint64_t c8m_ticks);
int floppy_drive_get_bit(unsigned drive, unsigned side, uint64_t c8m_ticks);
// Phase 2: timed write_bit() and flush_dirty_track() endpoints.
```

These API boundaries mean that IWM handles the **software-visible parallel/serial controller**, and floppy_drive handles **Sony drive commands, stream and logical image**. Neither may intercept ROM logical-sector calls or mutate the ROM.

### 12.7 Required original-ROM execution trace

The developer shall capture events (real ROM PCs and virtual ticks) covering at least the following expected *kinds* of behaviour; the precise interleaving depends on the original ROM, and must not be imposed as a fake script:

```text
  ROM initializes IWM in mode $1F without any ROM patch.
  Drive-Sel=0; IWM Motor-On=1 -> /ENBL1 asserted.
  Sony MOTORON sense reads 1 when stopped.
  PH0/PH1/PA5 select MOTORON; PH2=0; PH3H/PH3L -> spindle on.
  Sony /READY transitions to ready on the virtual timeline.
  Sony /TK0 sense: 0 when cylinder=0, otherwise 1.
  Set DIRTN using one PH3 pulse; cylinder remains unchanged.
  Set STEP=0 using another PH3 pulse -> exactly one cylinder step.
  /STEP reports busy then complete; /TK0 updates at cylinder 0.
  Sony RDDATA0/RDDATA1 selection chooses side without moving cylinder.
  IWM read latch delivers timed GCR codewords; original .Sony decodes.
  Sony TACH sense produces timed edges; VIA T2 counts them if used.
  Sony MOTORON=1 then IWM Motor-On=0: spindle off, /ENBLx off.
```

Emit a compact trace of: **ROM PC, elapsed C8M clock, IWM address/soft-switch, Q6/Q7, PH0–PH3, VIA PA5, Drive-Sel, /ENBL1/2, Sony read/write register number and CA2 command value, direction, cylinder, side, spindle, step/ready deadlines, TACH, bit position and read-latch result**. Use this to diagnose unexpected commands and timeouts.

### 12.8 Phase-1 implementation and acceptance tests

| Test | Input | Expected |
| --- | --- | --- |
| Control-line independence | PH0–PH2 and VIA PA5 changes while PH3 low | No step, no direction/motor command; correctly selected sense register |
| PH3 edge | PH3H→PH3L once, PH3 held high, then repeated after low | **One** command per rising edge, never one per read or per PH value change |
| Enable logic | Internal/external select, Motor-On off/on | Exactly the appropriate /ENBLx active; absent external cannot respond as a present HD20 or disk |
| Direction | Strobe DIRTN=0/1 | Direction changes; cylinder does not |
| Single step | Strobe STEP=0, advance virtual time | One cylinder change, busy→ready and /TK0 correct |
| Track boundaries | Step out at 0 and inward at 79 | Cylinder clamped, never wraps, no wrong-side change |
| Recalibration | Start at nonzero cylinder, run original `Sony_Recal` | ROM genuinely steps outward to /TK0 without emulator shortcut |
| Head selection | Read RDDATA0 then RDDATA1 | Same cylinder, distinct side 0/1 encoded streams |
| Motor/ready | Enable, read motor, command motor on/off | Separate IWM enable and Sony spindle state; /READY and TACH reflect elapsed time |
| TACH & VIA T2 | Sample under independently advanced time | Correct periodicity and timer/IFR operation; no per-read toggle |
| Track geometry | Seek across zone transitions 15→16, 31→32, 47→48, 63→64 | Correct sector count, circular bit length, address fields and GCR payload for each zone |
| Phase-1 write stub | Attempt guest write to mounted image | Guest sees write protection; circular track and `.dsk` remain unchanged |
| Authentic boot | Identified unmodified ROM + known-good 800K boot image | Original .Sony reads/boots via IWM, no `rom_patch()`, no `PV_SONY_ADDR` path |

All checks must be deterministic under the **virtual clock**, not sleeps or instant “success” responses. Unit tests alone do not close Phase 1: an **original-ROM boot through the real .Sony driver** remains mandatory.

### 12.9 Explicit exclusions and unresolved refinements

Do **not** implement a complete Apple II IWM, 5.25-inch phase-stepper, high-density SWIM/MFM, analogue magnetic flux, physical WRDATA NRZI/RDDATA pulses, STM32 peripherals, ATF1508 RTL, the 400K PWM speed loop or an HD20 daisy-chain controller as part of the Phase-1 IWM. We need only the subset of **Mac Plus 800K IWM and Sony drive behaviour exercised by the original ROM**. Unused register values or modes should be logged and justified, not blindly hardcoded to “ready”.

The exact IWM read-latch 14-FCLK rearm condition, Musashi intra-instruction timestamping, spin-up and step completion delay, innermost-zone tach period, 800K interface/handshake sense polarity, VIA timer use and logical `.dsk` ordering require **ROM-trace/specification validation**. These are defined verification work, **not** permissions to bypass ROM checks. Phase 2 must preserve the same drive-control state machine and add timed write transmission, bit-positioned circular overwrites, dirty-track flush on a cylinder/side/lifecycle transition and sector decoding as detailed in §6.
