
# Tima/Plus — IWM Emulation Architecture and Behaviour

**Status:** Two-phase architecture approved. Phase-1 read-only implementation [PLUS-EMU-0003](tasks/db/PLUS-EMU-0003.md) is **CLOSED on `main`** after PR #3. The Phase-2 **transactional temporary-track write architecture was approved 20 November 2026**, but implementation [PLUS-EMU-0010](tasks/db/PLUS-EMU-0010.md) remains **OPEN**; see the [authoritative detailed implementation plan](05-iwm-write-implementation.md).
**Recorded:** 19 November 2026; read-only/write implementation split approved 19 November 2026
**Target:** Macintosh Plus M0001AP, original MC68000, original IWM and unmodified Macintosh Plus ROM
**Implementation tickets:** [PLUS-EMU-0003](tasks/db/PLUS-EMU-0003.md) (Phase 1: closed) and [PLUS-EMU-0010](tasks/db/PLUS-EMU-0010.md) (Phase 2: open); [05 — IWM Write Plan](05-iwm-write-implementation.md)  
**Developer entry point:** [§12 — Mac Plus-specific IWM, Sony motor, head-step and drive-register implementation guide](#12-macintosh-plus-specific-implementation-guide-drive-control-and-head-movement)
**Companions:** [Introduction](00-introduction.md) · [Development Roadmap](01-development-roadmap.md) · [Unicorn–uMac Integration](02-unicorn-umac-integration.md)

## 1. Purpose and approved decisions

This document records the IWM discussions of 18–19 November 2026 and the subsequently approved **two-component, two-phase virtual floppy** plan. It supersedes earlier sequencing assumptions requiring STM32F411/Unicorn or a physical drive emulator before original-ROM virtual-floppy boot. The companion roadmap documents have not yet been reconciled with this updated sequencing.

1. **Two software components:** a register/timing-oriented **IWM behaviour model** (`iwm.c/.h`) and an independent **virtual disk/drive model** (`disk_img.c/.h`).
2. **Phase 1 — `PLUS-EMU-0003`: read-only.** Execute the unmodified Macintosh Plus ROM and its own `.Sony` driver. Implement IWM read/control behaviour, drive control and sense, and a circular track buffer populated from a raw or DiskCopy 4.2 Macintosh 800K image using 6-and-2 packing and GCR codewords. Stub writes and present the mounted medium as write-protected.
3. **Phase 2 — `PLUS-EMU-0010`: write-capable.** Implement original IWM write-handshake/buffering and emit timed bits into a **temporary snapshot of the active circular track** at the current angular bit position. On completed and valid `Q7L`, **swap the candidate into active RAM**, marking it dirty only if changed; on interrupted/invalid writes discard the candidate. On step/eviction, eject, medium replacement or orderly shutdown, independently decode/validate dirty active tracks and checkpoint the sector data. Tagged DiskCopy also stores the tags. Retain dirty data on checkpoint failure. [Image format policy](05-iwm-write-implementation.md#62-raw-versus-diskcopy-42-tags)
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
           | disk_img.c / .h            |
           | Track/side, selection,     |
           | circular bit buffer,      |
           | bit phase, dirty flag      |
           | 6-and-2 + GCR mapping      |
           +-------------+--------------+
                         |
               raw or DiskCopy 4.2 800K image
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

The ROM expects the **IWM** to determine when the next byte can be accepted. First-byte entry from sense mode and final-byte drain must be checked separately. The track backend captures the timed output at the **current stream position on a private circular-track candidate**, not necessarily the position of the last CPU read. The IWM's real holding/shifter timing is independent of that disk-side temporary buffer; **Q7L** ends the write and starts candidate validation/swap, not a forced serializer drain. This write state machine belongs to **Phase 2**; Phase 1 implements only the safe, non-mutating write stub and register mode transitions.

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

Before PLUS-EMU-0003, uMac advanced time in fixed 5-ms execution quanta. The IWM implementation uses consumed C8M cycles, modeled bus waits and access timestamps across slices. The ticket records the observed callback offsets and the timing approximations.

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

### 6.1 Phase 1: 800K image to timed encoded track

Both image formats contain 1,600 logical 512-byte sectors. The virtual drive has 80 cylinders with 12/11/10/9/8 sectors per side in five 16-cylinder zones.

- A raw image stores 819,200 sector-data bytes. The encoder supplies a zero-filled 12-byte tag for each sector.
- A Macintosh 800K DiskCopy 4.2 image has an 84-byte header, the same sector data, and an optional 19,200-byte tag area. A tagged image totals 838,484 bytes. `disk_img.c` validates the header, lengths and checksums before mounting. It preserves all stored tags, including the first tag in an image whose tag checksum starts at the second one. Tagless images use zero-filled tags. [Apple DiskCopy 4.2 format](https://nulib.com/library/FTN.e00005.htm)

For the selected track **and side**, the disk component:

1. Reads the corresponding logical sectors and tags in the image's zone and sector order.
2. Encodes the 12 tag bytes followed by the 512 sector bytes with Sony 6-and-2 packing and GCR codewords. The data-field checksum covers both.
3. Constructs the proper address fields, sector-data fields, prologues/epilogues, **self-synchronization**, gaps and interleave. Self-sync includes bit lengths not necessarily divisible by eight; do not substitute a plain byte array without retaining valid bit count and phase.
4. Packs the resulting stream into an allocated **circular bit buffer** with explicit valid-bit length, track/side identity and continuously advancing rotational position.
5. Supplies bits at a nominal one bit cell per **16 elapsed C8M ticks** regardless of MC68000 polls; when read mode is active, the IWM assembles/latches codewords according to the original hardware/ROM contract.
6. On a head step or side change, selects/rebuilds the appropriate encoded buffer; in Phase 1 there is no dirty data to flush.

An implementation may cache multiple (track, side) buffers, but **must not** require a whole-disk encoded cache. Rebuilding on track change is acceptable so long as time advancement, side selection and the original ROM's expected drive status remain coherent. No host filesystem-level sector shortcut may satisfy authentic-ROM IWM reads.

The host disk component synthesizes the encoded fields from the image. The `.Sony` driver in the unmodified ROM performs its original on-the-fly GCR decoding.

### 6.2 Phase 2: transactional serial writes to circular bit positions

**Owner-approved policy, 20 November 2026:** preserve the IWM's original holding/shifter/handshake timing and the position-derived encoded bitstream, but perform all mutations on a **temporary copy of the currently active circular track**, never directly on the live track during an in-flight write. The ROM continues to generate genuine serialized GCR bytes. No physical WRDATA pulse generator or decoded-sector ROM bypass is involved.

1. **Entry:** first `Q7H` write enters the IWM write state and supplies its first byte to the holding register. The disk backend snapshots the active side's encoded track, length and medium/cylinder/head identity. This candidate is separate from the IWM's limited byte buffer.
2. **Timed capture:** normal IWM bits are written at their current rotating angular positions **into the candidate**, respecting 16 C8M ticks/bit, self-sync not aligned to whole bytes, full-circle wrap and complete ROM-transmitted gaps.
3. **Validation and swap:** at normal `Q7L` exit, stop emitting at the actual tick and validate a newly transmitted complete sector record, or **all new sector headers and data** for a detected format. An ordinary sector write requires a decodable resulting track. A **structurally complete format attempt may be provisionally swapped** even if its final circular gap overwrote the earliest mark: the ROM must read this actual result to detect the shortage and retry. A provisional invalid format **cannot be saved to the host image** until a later complete, decodable format succeeds. Mark changed active bits dirty.
4. **Rollback:** on incomplete/invalid candidate, lost enable, reset, interrupted seek, stop or eject, discard only the in-flight candidate, preserving both active sides and their previous dirty state. This is an explicit *virtual-media consistency policy*, not physical magnetic-sector rollback behaviour.
5. **Separate persistence:** before cache eviction during track step, and at safe eject, replacement, idle/stop and clean exit boundaries, validate/decode **dirty active** tracks with reverse GCR/6-and-2, and checkpoint representable 512-byte sectors and 12-byte tags to the host image. A failed checkpoint must retain dirty active bits; clean tracks never cause a file write.

The original ROM's format routine transmits about **200 sync groups** initially, records every sector's headers and payload, then **measures gap timing from its own reads** and may format the track again. Capture the actual stream rather than constructing canonical gaps early. A 50+ sync-group pattern is useful as a detection hint but not proof of complete formatting; all sectors in a format must originate from the *current* transaction, never from the existing copy. **An interrupted incomplete format** is discarded; **a fully transmitted but gap-invalid format** is provisionally visible to the ROM for `RdAddr`/GapSync retry, but cannot be checkpointed until it decodes fully.

**Status caveat:** IWM D6 records transport underrun, sampled by the ROM before `Q7L`. Validation failures discovered after that cannot honestly be relabelled as underruns. Report a distinct host/media error and test whether the ROM subsequently detects a mismatch through read/verify. The detailed acceptance gates and storage/tag representability rules are in [Document 05](05-iwm-write-implementation.md).

Read-only and write-capable modes use the **same active circular-track representation**, and neither mode creates an alternative high-level `.Sony` interface.

### 6.3 Encoding terminology

6-and-2 **packs** the Mac data/tag bytes into six-bit values; GCR **maps** those values into legal eight-bit disk codewords. Both operations are performed by the original ROM on a write and by our disk backend when synthesizing a track for a read; the inverse operations convert received encoded sectors back to 512-byte logical payloads at flush. The IWM only serializes/recovers the encoded bitstream and implements register-level handshakes. Exact bit layout, interleave, sync length, sector number and checksum algorithms must be unit-tested against documented Mac 800K examples and the unmodified ROM; do not freeze an assumed raw-SPI-flash layout here.


## 7. Future STM32F411 / Unicorn endpoint (not selected yet)

The initial deliverable is the **IWM + virtual disk + logical `.dsk`** reference. Later, the disk-side backend can be replaced by an interface driven by the actual STM32F411 production firmware running under Unicorn and a production-compatible SPI-NOR image, without rewriting the IWM behaviour visible to the Macintosh ROM. **The approved temporary-track transaction is transferable to firmware:** capture real-time bit changes in RAM, validate the sector/format transaction, and only then schedule SPI flash programming. Flash writing and decoding must not stall real-time incoming bit capture; capacity, concurrency, power-loss recovery and possible ATF1508 assistance need future measurement.

**No physical drive interface has been selected.** Both STM32-only timed capture/generation and STM32+ATF1508 external logic remain options; we have discarded the proposed 4× SPI waveform-sampling approach. The physical IWM WRDATA pin toggles its level on each encoded logical `1`, while the IWM reads falling RDDATA pulses; those electrical representations are distinct from the internal logical encoded bits. The forthcoming hardware contract must account for pulse timing, bit recovery, control-register multiplexing and possible SPI transfer framing only after that design is investigated.

The nominal Macintosh bitstream cadence remains 16 C8M ticks per bit / 128 ticks per ordinary 8-bit codeword. No direct equality between a future SPI transaction's duration and that cadence has been specified. The physical circuit requires independent electrical and timing validation; this two-phase emulation work does **not** settle the STM32/ATF1508 question.


## 8. Two implementation tickets and acceptance gates

Both tickets are formally opened under `docs/tasks/db/`. **Phase 2 depends on Phase 1**, but both are documented now to keep the component interface stable.

| Phase / ticket | Scope | Required acceptance evidence |
|---|---|---|
| **Phase 1 — [PLUS-EMU-0003](tasks/db/PLUS-EMU-0003.md)** | Use the original ROM, implement IWM soft switches, mode `$1F`, status, latch/rearm and elapsed-clock progression. Provide drive control, sense and circular tracks from raw or DiskCopy 4.2 Macintosh 800K images. Stub writes and report write protection. | Original ROM and `.Sony` driver boot 800K System Software and match sector and tag contents across sides and zones. Test media protection, byte and bit sync, reset, and timing traces. |
| **Phase 2 — [PLUS-EMU-0010](tasks/db/PLUS-EMU-0010.md)** | Replace write stub with genuine IWM buffer/handshake/underrun and timed capture **to candidate circular track**, valid sector swap or provisional complete-format swap, two-side dirty active cache and separate valid decoded sector/tag checkpoint. | ROM write/readback, interrupted write rollback, failed-save recovery and authentic gap measurement/retry after a provisional gap-invalid full format. No sector-level bypass. |
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
| 6 | `EJECT` | `1` requests eject. The **800K new-interface** ROM uses a short PH3 strobe, then polls `/CSTIN` until the medium leaves; older 400K drives use a longer pulse. Distinguish request from actual removal; Phase 2 must flush dirty data before logical unmount |
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

`iwm.c` owns PH0–PH3, Q6/Q7, mode and IWM drive-enable selection. `disk_img.c` owns the Sony controls, cylinder, side, track stream and image. IWM /ENBL selects and enables the drive interface. Sony MOTORON controls the spindle.

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

- **Drive select:** `IWM_DRIVE_SEL=0` means internal `/ENBL1`; `=1` means external `/ENBL2`. A configured IWM motor-off delay may keep the selected effective `/ENBLx` asserted **after** the Motor-Off soft switch, until its timer expires; reasserting Motor-On before expiry cancels that pending disable. When enabled, only the selected position responds. The ROM probes internal/external positions independently; even if Phase 1 mounts one image, maintain a coherent absent external response.
- **Spindle:** Sony command index 4 starts/stops the motor; **actual loss of effective `/ENBLx` also powers down the Sony spindle**. A Motor-Off soft switch with a pending delayed-off timer is *not yet* loss of /ENBL. At genuine disable, stop TACH and RDDATA and invalidate stale readiness without ejecting the media, moving the head or clearing image data. Re-enable alone does **not** restart the spindle; the ROM must send Sony MOTORON=0 again. See §12.10 and `EMU3-R6`.
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

The trace records the ROM PC, C8M tick, IWM access, Q6/Q7, PH0–PH3, VIA PA5, Drive-Sel, active-low /ENBL1/2 levels, Motor-On latch, and off-delay deadline. Its Sony fields include register and command, cylinder, side, spindle, step and spin-up deadlines, /READY, TACH, media presence, bit position and the read latch. Drive state fields describe the internal drive even when the external slot is selected.

### 12.8 Phase-1 implementation and acceptance tests

| Test | Input | Expected |
| --- | --- | --- |
| Control-line independence | PH0–PH2 and VIA PA5 changes while PH3 low | No step, no direction/motor command; correctly selected sense register |
| PH3 edge | PH3H→PH3L once, PH3 held high, then repeated after low | **One** command per rising edge, never one per read or per PH value change |
| Enable logic | Internal/external select, Motor-On off/on, delayed timer expiry/cancel, reset | Correct effective /ENBLx; no premature spindle stop during pending timer; genuine disable powers down Sony spindle but retains cylinder/media; no absent-drive cross-talk |
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

### 12.10 Disk lifecycle: insertion, eject and drive power (software and future STM32)

**Purpose:** establish what the Macintosh sees when a virtual 800K disk is ejected or inserted, and preserve an implementation-independent lifecycle for the planned STM32 cartridge firmware. The motor enable correction in [`12.5](#125-drive-selection-motor-ready-and-tachometer) is required in Phase 1; **interactive insertion/eject hardware is not**.

#### 12.10.1 Three independent facts

- **Drive installed:** a Sony 800K-compatible drive or emulator is present in the selected internal/external slot. Sony read index **14** reports the installed drive (active-low). This does **not** mean there is a disk.
- **Media inserted:** a specific image/cartridge is accepted and present; Sony read index **1**, `/CSTIN`, is **0 when inserted**, **1 when empty**. Inserted media has an identity and geometry (e.g. 800K, its tags and currently encoded tracks).
- **Spindle running / ready:** distinct from presence. Sony read index **4**, `/MOTORON`, reports motor state; index **13**, `/READY`, is active-low and only reports ready when the disk is spinning/settled and the drive can supply data.

A drive may be empty but installed, or inserted but not spinning. No PH0–PH3 address change, IWM motor selection or periodic status read is a command to insert a disk.

#### 12.10.2 Required state and guest-visible effects

| Event | Guest-observable condition | Drive/media backend action |
| --- | --- | --- |
| Installed drive, empty | Drive-present still active; `/CSTIN=1`; `/READY=1`; no RDDATA | Retain the drive hardware model; no phantom medium |
| **External insertion** (real cartridge detected or host explicitly mounts image) | `/CSTIN` becomes **0 only after acceptance**; motor initially off, not yet ready | Validate disk format/metadata, establish **new medium identity**, invalidate previous read latch/track cache and prepare 800K GCR data/tags |
| Sony MOTORON=0 command | Motor sense on, initially `/READY=1` | Start spindle and a spin-up deadline; after ready serve read data/TACH for current cylinder and side |
| EJECT command **6**, value **1**, PH3 rising | **Eject requested**, not proof of medium absence | Trigger exactly one eject action. 800K ROM pulses PH3 briefly then polls `/CSTIN` to learn when removal completed |
| **Completed removal** (physical cartridge out or host explicitly unmounts) | `/CSTIN=1`, `/READY=1`, RDDATA/TACH inactive; *drive-present unchanged* | Stop spindle, invalidate old read latch and track state, detach old medium; never expose stale sectors |
| **Reinsertion** of same/different medium | `/CSTIN=0` only after a fresh validated insertion event | Create a fresh medium instance/track mapping; do not reuse old disk contents |

**Eject timing:** The [original Plus ROM Sony control routine](https://github.com/jonathanschilling/mac_rom/blob/main/plus/resources/res_drvr_sony.s) contains both `SONY_CTL_EJECT` and `SONY_NEWDRVEJECT`. For **new-interface 800K**, PH3 is pulsed briefly and the driver waits, polling `/CSTIN` for the medium to depart (with a timeout); for **old-interface 400K**, the eject pulse is much longer. Do not emulate 400K eject timing in the 800K replacement.

The **current Phase-1 disk backend immediately sets inserted=false when the eject command is strobed**. This is an acceptable simplified virtual-eject completion so long as sense/readiness/read data become consistent. It **must not** be interpreted as a specification that an actual mechanism removes a cartridge instantaneously.

#### 12.10.3 Effective /ENBL transition is NOT media eject

- The IWM Motor-On latch and Drive-Sel produce effective `/ENBL1` or `/ENBL2`; the IWM's off-delay mode can **keep enable asserted after a Motor-Off request**. Only an effective enable-falling edge powers down the selected Sony drive under this rule.
- **On enable loss:** stop the spindle, TACH and RDDATA and clear readiness. Preserve the inserted disk, tags, write protection, head, cylinder and rotational phase. In the virtual drive, apply any step already due at the disable tick and cancel a step whose deadline is still ahead.
- **On re-enable:** the drive interface becomes addressable, with Sony spindle **off**. Do not automatically replay a previous motor-on command or claim ready without a new start/spin-up.
- **On Drive-Sel change:** notify the old drive of its lost /ENBL and let it power down; the new drive has independent spindle/media state. Selection is not physical eject.
- **At motor-off timeout:** deliver a disable event at the actual **virtual C8M deadline**, not at the earlier Motor-Off soft-switch request. Re-enabling before expiry cancels the event and preserves the active spindle.
- **On reset:** when IWM enable drops, stop the spindle but **retain inserted medium and cylinder**; Phase-2 pending writes must never be discarded merely because the CPU resets.

This is the approved developer correction [PLUS-EMU-0003 / EMU3-R6](tasks/db/PLUS-EMU-0003.md), and is part of the minimum software-visible Sony 800K behaviour. It needs no simulation of head-motor windings or physical eject coils.

#### 12.10.4 Insertion comes from the media backend

**The Macintosh does not transmit a PH0–PH3 disk-insert command.** A real Sony drive detects media insertion; a virtual disk backend must instead receive an **external image mount** event. Future STM32 firmware should use whichever reliable cartridge-presence mechanism is selected (mechanical switch, detect contact, or other validated means) and confirm that the image/track metadata can be read before advertising `/CSTIN=0`.

- Treat detection and acceptance as distinct: debounce/settle the physical signal as necessary, reject unsupported/corrupt images, and never carry encoded bits or tags from a previous medium into a new one.
- Do **not** infer insertion from IWM /ENBL, Motor-On, RDDATA, the Mac's PWM line or a repeated SENSE poll.
- The inserted disk remains present while the Mac powers its drive down or switches drive selection.
- Phase 1 loads an image at startup. After a guest eject it may remain empty for the rest of that session. **Host hot-swapping or reinsertion without restarting uMac is not a new Phase-1 requirement**; the interface is documented to support later implementation.

#### 12.10.5 STM32 firmware and Phase-2 persistence

Use separate **drive control**, **media presence/identity** and **track stream** state; whether the physical design is STM32-only or STM32+ATF1508 remains an **open architecture decision**. An optional CPLD handles selected pin timing, not the logical decision that a cartridge is inserted or saved.

~~~c
// Conceptual interface only; NOT a prescribed Phase-1 API.
bool media_insert(drive_id drive, media_handle candidate, uint64_t time);
void media_request_eject(drive_id drive, uint64_t time);
void media_removed(drive_id drive, uint64_t time);
void drive_set_enabled(drive_id drive, bool enabled, uint64_t time);
~~~

- Guest **EJECT request** and **completed physical release** must be distinguishable. If no mechanical actuator exists, firmware may require safe user removal while honoring the ROM's bounded eject wait. Do not claim the cartridge is physically absent while still actively using it.
- **Phase 2:** finish/abort any in-flight temporary track transaction first, **then** before normal logical unmount checkpoint **only swapped, dirty active** circular tracks, decode/check sector data and 12-byte tags and persist to image/SPI flash. If decoding or saving fails, do not clear dirty state or claim durability. Unexpected physical removal can make persistence impossible: stop new accesses, retain a recoverable RAM buffer if possible and report an error rather than pretending the save completed.
- Reinsertion must validate medium identity, 800K geometry and storage metadata before exposing disk-present status. An empty or unreadable cartridge is not a writable Macintosh floppy.
- The host virtual model uses monotonically advancing C8M ticks. STM32 firmware additionally must respond to **real external media-presence events**; these cannot be inferred from the nominal 489,600 bit/s stream. Mac PWM is not a disk-presence or write bit-clock signal.
- Test guest eject with delayed physical removal, immediate virtual unmount, insertion of same and different media, empty drive, drive-enable loss without eject, removal while spindle runs, dirty-buffer save failure/power loss, and correct `/CSTIN`, `/READY`, motor/TACH, cylinder, sector/tag integrity and ROM remount detection.

**Boundary:** This document specifies future physical media lifecycle behaviour so STM32 firmware can be designed properly. **Only the enable-loss motor correction (EMU3-R6) is requested of the current Phase-1 developer**; do not enlarge the scope to user-interface hot swap, physical cartridge mechanics or write persistence.
