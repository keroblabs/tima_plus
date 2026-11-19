# Tima/Plus — IWM Emulation Architecture and Behaviour

**Status:** Engineering decision record / working design for project-owner review; not an implementation sign-off  
**Recorded:** 19 November 2026  
**Target:** Macintosh Plus M0001AP, original MC68000, original IWM and unmodified Macintosh Plus ROM  
**Primary work:** `PLUS-EMU-0003`; proposed follow-on virtual-media work `PLUS-EMU-0009` (identifier not yet formally opened)  
**Companions:** [Introduction](00-introduction.md) · [Development Roadmap](01-development-roadmap.md) · [Unicorn–uMac Integration](02-unicorn-umac-integration.md)

## 1. Purpose and decision summary

This document records the IWM engineering discussions of 18–19 November 2026. It clarifies the agreed direction before implementation tickets are raised. It supersedes any *sequencing or architectural assumptions* in the companion documents that imply the STM32F411/Unicorn firmware is required before a fully working, unmodified-ROM virtual floppy can be demonstrated. Companion documents have **not yet been edited** to reflect this decision.

**Agreed direction:**

1. First implement the Macintosh IWM's software-visible control, status, asynchronous read, write-handshake, and timing behaviour in uMac.
2. Connect the IWM to a deliberately simple **timed encoded-track source/sink**, backed initially by a normal logical 800K `.dsk` image. This is **not** an attempt to emulate the Sony MFD-51W mechanism or its electromechanical design.
3. Prove the **unmodified** Macintosh Plus ROM and its **original** `.Sony` driver can initialize the controller, recognize an inserted disk, read sectors and boot original System Software. Then validate writes, read-back, formatting and persistence.
4. Only **after the IWM and virtual-media reference implementation work**, substitute the downstream stream backend with the host bridge, Unicorn STM32F411 execution, production firmware and production-compatible SPI-NOR image (`PLUS-EMU-0007`). Preserve the IWM/ROM endpoint.
5. The firmware/ATF1508 interface and the eventual physical circuit are separate validation targets. Successful emulator operation does not establish electrical or physical timing compatibility.

**Naming convention:** call the new backend the **virtual floppy stream engine**, **encoded-track engine**, or **timed track-stream backend**. Do **not** call it a “Sony MFD-51W emulator”. In authentic mode the original ROM `.Sony` driver runs unchanged; the *host-side paravirtual Sony replacement* currently in `disc.c` is bypassed. We are not removing the original ROM driver.

## 2. Architectural boundaries

```text
                 Original Macintosh Plus ROM
                     Original .Sony driver
                               |
                   Memory-mapped IWM access
                               |
                      IWM model (iwm.c)
             soft switches, mode, data latch,
            write buffer/handshake, status, time
                               |
            drive control/sense + timed bitstream
                               |
            +------------------+---------------------+
            |                                        |
      INITIAL BACKEND                         FUTURE BACKEND
      Encoded-track engine                    Host IWM-to-SPI bridge
      800K logical .dsk                       Unicorn STM32F411
      Circular stream/position                Production ARM firmware
      Encode on load                          SPI-NOR device model
      Capture/decode writes                   Exact raw-flash image
```

The IWM remains a distinct, permanently reusable component. The virtual backend should not implement Q6/Q7 or manufacture IWM read/write register values directly; only the IWM decides what the MC68000 observes. Conversely, the IWM should not parse Macintosh logical block requests or operate directly on the filesystem.

The initial backend represents the **observable stream delivered to/from the IWM**, not an accurate motor, head servo, rotating platter, magnetic flux or physical cabling simulation. Model only those logical drive controls and sense responses that the ROM needs (drive selection, disk-in-place, write protection, head/track/side and related signals), without conflating those with the IWM register logic. Precise drive command/sense mappings must be checked against the documentation and ROM usage.

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

### 3.2 Write operation

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

The ROM expects the **IWM** to determine when the next byte can be accepted. First-byte entry from sense mode and final-byte drain must be checked separately. The track backend captures the timed output at the **current stream position**, not necessarily the position of the last CPU read.

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

## 6. Virtual media backend — subsequent milestone

After isolated IWM tests, read a normal **819,200-byte logical 800K `.dsk` image** (80 tracks, two sides, zone-dependent sectors-per-track: 12/11/10/9/8). Encode the selected track into the exact on-disk codeword, address-field, data-field, sync, checksum and gap sequence needed by the original ROM, and present it through the timed stream source. No filesystem-level shortcut is allowed in authentic mode.

Writes are captured into the circular stream **starting at the currently elapsed bit/byte position when writing begins**. Afterward the backend can decode and validate changed records and commit logical sector data to `.dsk`. For the duration of a run it may need to retain track-level features a logical `.dsk` cannot represent; do not claim a logical image can preserve arbitrary sync/gap or malformed-track modifications. Formatting must be exercised as an actual stream operation, not replaced by a high-level “format done” response.

**Terminology deferred:** 6-and-2 packing, GCR codeword mapping, tag bytes and checksums need a dedicated detailed encoding discussion before implementation of the track encoder. For orientation only, “6-and-2” describes the byte packing and six-bit values, while GCR maps such values to legal disk codewords; these are related stages, not two independent full-sector transfer protocols. This document does **not** freeze the on-flash 703-byte record layout or full sync/gap arrangement.

## 7. Future STM32F411 / Unicorn endpoint

Once the initial reference backend passes the unmodified ROM, substitute a backend that delivers IWM-level events through the **defined** ATF1508-facing SPI transaction contract to the exact production STM32F411 firmware under Unicorn, with the production-format SPI NOR image.

**The nominal IWM codeword cadence remains 128 C8M ticks.** This sets the external stream's event spacing, **not automatically the SPI peripheral clock rate or duration of each SPI transaction**. SPI end-of-transfer/completion must correspond to the real agreed framing and capacity of the bridge, while ensuring the IWM stream has advanced on its own independent clock. Do not declare the physical SPI transfer itself to take exactly 128 C8M ticks before that protocol is specified.

No ATF1508 RTL simulation is needed for software integration; physical circuit/timing validation remains separate. A reference IWM implementation tested with both backends is preferable to duplicating IWM logic in firmware-facing host code.

## 8. Implementation sequence and proposed acceptance gates

The identifiers below refer to *programme concepts* until individual records are formally opened under `docs/tasks/db/`; do not silently create/close unrelated tickets.

| Gate | Proposed work | Evidence |
|---|---|---|
| A — IWM register model (`PLUS-EMU-0003`) | Decode all 16 switches, mode `$1F`, Q6/Q7, motor/drive select, status/SENSE; authentic ROM option | Unmodified ROM gets through IWM initialization; no `PV_SONY_ADDR` calls |
| B — Timed IWM data and write-handshake (`PLUS-EMU-0003`) | Synthetic timed stream, read latch/rearm, write buffer and underrun; deterministic virtual timestamps | Headless tests of read/write polling, missed bytes, mode transitions, reset, sequential transfers |
| C — Virtual 800K media backend (proposed `PLUS-EMU-0009`) | Logical `.dsk` to circular encoded track; drive control/sense | Original ROM/`.Sony` reads sectors from a real disk image and boots historical System Software |
| D — Media mutation (same backend ticket) | Stream-positioned writes, read-back, format, persistence, cross-zone tests | Sector checksums, read-after-write, full track/format, disk image persists and reopens |
| E — Production bridge (`PLUS-EMU-0007`) | Unicorn STM32F411 binary and hardware-facing SPI/flash models | Unmodified ROM boots, reads, writes and formats via production firmware and exact raw-NOR image |
| F — Physical validation | Actual ATF1508 + STM32F411 drive replacement | Real M0001AP electrical/timing and media compatibility |

Tests should include varied polling intervals, CPU stall intervals, boundary conditions, alternative start-of-track phases, retries and deliberate write underruns. Trace at least PC, IWM address/read/write data, Q6/Q7/motor/mode, global virtual tick, stream bit/byte position, latch/handshake states and result; record original ROM identity and emulator commit.

**The original ROM is the software acceptance authority**. Patched uMac/Basilisk-derived paravirtual disk code remains a *separate regression baseline*, never a pass for authentic IWM/media behaviour.

## 9. Current repository observations (as of this decision record)

In `devel/umac/src/main.c`, IWM access still uses a 16-byte `iwm_regs` array; reads of switch 8 return `$FF` and switch 14 `$1F`, with most operations unimplemented. The IWM address window is decoded in `devel/umac/include/machw.h`.

`devel/umac/src/rom.c` patches the Mac Plus V3 ROM checksum path and replaces the ROM `.Sony` driver. `devel/umac/src/disc.c` handles the paravirtual disk path. `devel/umac/src/unix_main.c` currently calls `rom_patch()` during startup. **Authentic mode must skip all ROM patching**, preserving the untouched image bytes; the old patched mode stays independently runnable.

`devel/umac/src/via.c` currently stubs the timer tick; VIA Timer 2 and other timer functions required by ROM drive control remain follow-up scope. The working SCSI work (`PLUS-EMU-0004` and `PLUS-EMU-0005`) is the architectural precedent: controller + storage target, tested separately. Its branch/PR merge and the project owner's required review must not be presumed complete by this document.

## 10. Open verification questions — do not convert to assumptions

1. Confirm **exact** IWM asynchronous latch, acknowledgement, read-register rearming and the reported 14-FCLK delay against primary specification text; identify FCLK vs C8M precisely.
2. Confirm motor-off delay, write mode transition, write buffer-empty/underrun polarity and clearing rules, including different mode bits and error conditions actually exercised by the original ROM.
3. Determine cycle-level phase of memory-mapped accesses under Musashi and whether a lightweight memory-bus timing model is sufficient; quantify video/sound RAM contention and separate IWM/VIA/SCC bus acknowledgement timing.
4. Capture original ROM sequences for drive select, PH lines, VIA SEL, ready/sense, head movement, side and inserted/write-protect status; define a minimal but credible stream-backend signal contract.
5. Determine exact handling of self-sync bit patterns and stream alignment, then specify 6-and-2/GCR encoder and on-disk track layout in a separate review.
6. Design persistence semantics for modified logical sectors versus arbitrary track edits; define format and malformed-track test fixtures.
7. Specify the ATF1508-to-STM32 SPI framing and completion semantics **after** the direct IWM/reference backend is working, not before.

## 11. Next review/implementation action

Review this document, particularly §§4–5 and the open questions. Once agreed, open the detailed `PLUS-EMU-0003` IWM implementation ticket and separately formalize the virtual-media ticket (tentatively `PLUS-EMU-0009`) using the project bug/task methodology. Update the roadmap, introduction and Unicorn-integration document to reflect the **direct virtual floppy before STM32** sequencing. **No implementation code or task-status changes are authorized by this documentation-only commit.**
