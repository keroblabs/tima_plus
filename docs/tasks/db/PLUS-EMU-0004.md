# PLUS-EMU-0004 — Implement Macintosh Plus NCR 5380 SCSI controller

**ID:** `PLUS-EMU-0004`  
**Status:** `OPEN`  
**Title:** Implement Macintosh Plus NCR 5380 SCSI controller

**Description**

The current uMac reference does **not implement the Macintosh Plus NCR 5380 SCSI controller**. The emulator therefore cannot expose the actual SCSI registers, controller state or bus handshakes to the original M0001AP ROM and SCSI Manager. This is a **confirmed missing hardware capability** for our Macintosh Plus model, not a regression in the upstream minimalist uMac emulator.

The goal is a **Macintosh Plus-specific, ROM-visible register-compatible initiator model**, not a full NCR 5380 or electrical SCSI bus simulator. Reproduce only the meaningful register operations, address decoding, arbitration/selection, protocol phases, CPU-polled I/O and pseudo-DMA that the genuine Plus ROM can observe. **There is no independent SCSI DMA controller or CPU-external SCSI IRQ/DRQ wiring on the M0001AP.** Both transfer paths are driven by MC68000 memory accesses. The SCSI disk target, persistent media image and BlueSCSI-compatible boot are explicitly separate work.

**Details**

### Ownership, dependencies and inspected baseline

- **Kind:** `BUG` — absent required hardware feature.
- **Category:** `PLUS-EMU`; **Roadmap ID:** `PLUS-EMU-0004`, under `PLUS-DEV-0009`; contributes to M0001AP hardware correctness (`PLUS-EMU-0008`).
- **Inspected revision:** `keroblabs/tima_plus`, `main` commit `a3549f3`, 18 November 2026. Findings below are from **static review**, not a new runtime test.
- **Related but excluded:** `PLUS-EMU-0005` — SCSI hard-drive target/image and BlueSCSI boot; `PLUS-SYS-0006` — Tima/Plus SCSI system support.
- **Schedule:** Creating this ticket now does not, by itself, override the floppy-first milestone order. Implementation priority/approval must be decided explicitly.

### Observed implementation gap — confirmed

1. `devel/umac/include/machw.h` has no SCSI decode. `IS_DUMMY(x)` treats the `0x500000–0x5fffff` region as dummy, which **overlaps the Macintosh Plus SCSI mapping**.
2. `devel/umac/src/main.c` `cpu_read_byte()` and `cpu_write_byte()` dispatch RAM, ROM, VIA, IWM and SCC, but **no NCR 5380**. The word/long paths do not model SCSI access either.
3. `umac_init()`, `umac_reset()` and `umac_loop()` have no SCSI initialization/reset/tick integration.
4. `devel/umac/CMakeLists.txt` has no SCSI component; `devel/umac/README.md` identifies SCSI as unimplemented.

### Required Macintosh Plus memory-mapped behaviour — address-selected, no PAL signal emulation

The Macintosh Plus maps its NCR 5380 in the **`0x580000–0x5fffff`** region. The **CAS PAL** (not BMU2) contains the relevant SCSI address decoder and drives the NCR 5380's active-low **/SCSI (chip select)** and **/DACK** pins. The reverse-engineered equations select the SCSI region using the upper address bits and distinguish the two paths with **CPU address bit A9 (mask `0x000200`)**:

- **A9 = 0:** normal NCR 5380 register access (/SCSI selected).
- **A9 = 1:** pseudo-DMA access (/DACK selected).
- **A4–A6:** NCR 5380 register index, 0–7, for normal-register accesses.
- **A0:** on the Plus, even-address reads versus odd-address writes are the documented software-facing access convention; verify unsupported combinations rather than silently treating every byte address identically.
- The PAL does not decode every lower address bit, so additional mirrors/aliases may exist. The *exact* mirror mask and unsupported/non-byte access semantics still require validation against schematics, original ROM traces and focused tests.

Reference addresses: `0x580000` (normal read R0), `0x580001` (normal write R0), `0x580040` (normal read R4), and **`0x580200` (A9-set pseudo-DMA read window; corresponding write-qualified address must obey A0 convention)**. **Correction to the original ticket:** `0x580100` does *not* select DACK merely by setting A8; it is **A9**, not A8, that selects pseudo-DMA. Older descriptions using `$580drn` can be misleading if `d` is interpreted as an arbitrary `0/1` hexadecimal digit.

**Implementation boundary:** Decode the address in uMac's existing CPU memory-mapped I/O dispatcher, before its `IS_DUMMY` fall-through. For normal accesses dispatch to `ncr5380_read_register()/write_register()`; for A9-selected accesses dispatch to `ncr5380_pseudo_dma_read()/write()`. The latter perform **one guest-requested controller data transfer**, subject to the 5380's internal mode/readiness/phase state; they do **not** move RAM directly. A module does **not** need a separately simulated CAS/BMU2 PAL, /SCSI or /DACK pin, DACK pulse scheduler, external DMA request line or DMA-controller state machine. This is an address-to-behaviour mapping, not physical PAL simulation.

Preserve all non-SCSI memory regions. Ensure CPU access width and byte-lane handling do not accidentally duplicate data-transfer side effects. Record the actual guest-visible behaviour for unsupported or non-byte MMIO accesses instead of silently dropping required cycles.

### Required NCR 5380 registers

The same register index has distinct read and write semantics:

| Reg | Read | Write |
| --- | --- | --- |
| 0 | Current SCSI data | Output data |
| 1 | Initiator command incl. arbitration status | Initiator command |
| 2 | Mode | Mode |
| 3 | Target command | Target command |
| 4 | Current bus status | Select enable |
| 5 | Bus and status (DRQ, IRQ, phase match, etc.) | Start DMA send |
| 6 | Input data | Start DMA target receive |
| 7 | Reset parity/interrupt latch | Start DMA initiator receive |

Implement register masks, latched vs live status, reset defaults, read/write asymmetry and side effects. Cover arbitration-in-progress and lost-arbitration flags; SEL/ATN/RST/ACK controls; phase match; select enable; parity where applicable; DRQ and start-DMA modes; IRQ-latch clearing. A passive `uint8_t regs[8]` is **not** sufficient.

### Firmware-visible sequence and small transaction model

The NCR 5380 **does not decode disk commands** such as READ(6): that belongs to the emulated target in [PLUS-EMU-0005](PLUS-EMU-0005.md). The original Mac ROM/SCSI Manager talks to the 5380 *only* through CPU-initiated MMIO register access. The required observed sequence is:

1. **Bus free / arbitration:** The firmware checks bus status, writes the initiator ID 7 bit, requests arbitration using the Mode register and polls arbitration-in-progress/lost-arbitration status. With a single initiator on an idle bus, arbitration can deterministically succeed. Incorrect/busy/reset states must not silently succeed.
2. **Select disk:** The firmware drives the initiator ID bit and target ID bit (e.g. ID 7 + ID 1 = `0x82`), uses the Initiator Command register to assert selection, and polls BSY. **There is no NCR register enumerating SCSI IDs.** The ROM discovers targets by attempting ID selection. Support one configured target ID (default 1, LUN 0); absent IDs do not respond, permitting a guest-visible selection timeout.
3. **COMMAND:** On successful selection, enter COMMAND phase and set the target's CDB byte position to **0**. Set the phase/REQ register bits correctly. The target receives the *first byte* because this is the first **completed REQ/ACK byte handshake** after entering COMMAND; not because the 5380 supplies a special first-byte marker. Merely writing the output register is **not** equivalent to completing a handshake. The target uses the opcode to determine CDB length (READ(6) is 6 bytes) and decodes only after all bytes are accepted.
4. **DATA IN / DATA OUT:** The target supplies or accepts one byte at a time. Ordinary programmed I/O uses guest-controlled REQ/ACK; A9-qualified pseudo-DMA uses the 5380 internal DMA mode and CPU DACK-window access to advance the byte handshake. **Only a valid completed transfer consumes one data byte.** Polling status or inspecting current bus data never advances the target buffer.
5. **STATUS → MESSAGE IN → BUS FREE:** The target supplies GOOD/CHECK CONDITION, then COMMAND COMPLETE, and releases BSY after the corresponding handshakes. Premature access, reset, abort or missing target remain visible through appropriate register/status behaviour.

**Illustrative firmware operation:** select ID 1; send `08 00 00 64 01 00` (READ(6), LBA 100, count 1) as six distinct command handshakes; read 512 data bytes through PIO/pseudo-DMA; accept status `0x00`, message `0x00` and bus-free transition. This is *not* claimed to be a literal ROM trace.

### What to emulate — and what not to emulate

- Use **one compact transaction state owner**: `BUS_FREE`, `ARBITRATION`, `SELECTION`, `COMMAND`, `DATA_IN`, `DATA_OUT`, `STATUS`, `MESSAGE_IN`; add MESSAGE OUT only when demonstrated necessary. Track one active transaction, REQ/ACK readiness, register latches, a small command/transfer counter and internal DMA status. These are logical states, **not** separately simulated electrical buses or independent signal drivers.
- Derive 5380 bus-status and phase bits from this state, including visible BSY, SEL, REQ, ACK, RST, ATN, MSG, C/D, I/O and relevant arbitration/parity/DRQ/IRQ latches. **Do not** replace meaningful register semantics with an eight-byte array or magic success values.
- Support a single initiator (Mac ID 7) and **one selected target at a time**; no multi-initiator contention, bus-device registry, target-mode firmware, disconnect/reselect machinery, synchronous transfer negotiation or generic SCSI wiring simulation in initial scope.
- CPU performs all data transfers. As already established, physical DRQ/IRQ/RDY are not connected to the Plus CPU. Internal status must still work, but no independent DMA controller or guest IRQ is required.
- **No implicit wait for DRQ:** the real Plus does not hold off premature/blind pseudo-DMA CPU reads. Match *observable* not-ready semantics via manual/ROM tests; never invent a helpful stall or silently advance an unavailable byte.
- Require deterministic emulated delays/timeouts **only** when they affect guest polling or selection. No nanosecond bus propagation engine or host `sleep()`.
- Distinguish machine reset, controller reset and SCSI bus reset; reset/abort cancels a partial CDB or transfer cleanly.

### Implementation shape — one lightweight subsystem, two logical responsibilities

The source layout is deliberately flexible. A **single SCSI subsystem** with internal register-compatible controller and target-facing callbacks is preferred. There is **no requirement** for standalone `scsi_bus.h/.c`, a pin simulator or a generic target framework.

- `devel/umac/include/scsi5380.h` / `devel/umac/src/scsi5380.c` (candidate): regular register read/write, A9 pseudo-DMA read/write, reset and transaction/phase progression. A small test target can supply/consume bytes without an image.
- `devel/umac/include/machw.h` / `devel/umac/src/main.c`: route A9-qualified and normal SCSI accesses from existing MMIO, without any separate CAS PAL/DACK signal component.
- Optional adjacent disk file or helper functions implementing `PLUS-EMU-0005` may share the same transaction engine. The 5380 register logic never knows SCSI opcodes, image files, LBAs or guest RAM addresses.
- `devel/umac/CMakeLists.txt` / `devel/umac/tests/`: headless register and synthetic-target tests, plus preserved patched-ROM floppy regression checks.

### Acceptance checks — firmware-visible compatibility

- [ ] Clean build and unchanged patched-ROM floppy, display and input regression.
- [ ] Macintosh Plus address tests cover regular A9=0 and pseudo-DMA A9=1, A4–A6 register index, A0 read/write convention, mirrors and access widths, including `0x580000`, `0x580001`, `0x580040`, `0x580200` and `0x580100` (A8-only negative check).
- [ ] Eight 5380 read/write register views return verified masks, reset/side effects, status, arbitration and start-DMA semantics rather than passive storage.
- [ ] Through guest-like register writes, one initiator acquires the free bus, selects attached target ID 1, and *fails/times out* selecting an absent ID; there is no invented SCSI ID enumeration register.
- [ ] On COMMAND phase entry the target CDB counter resets to zero. Six **completed** REQ/ACK handshakes deliver exactly six CDB bytes; repeated output writes, current-data reads and polling do not append duplicate bytes.
- [ ] PIO and A9-qualified pseudo-DMA support bidirectional bytes, correct phase/status/REQ/ACK/DRQ behaviour, last-byte advancement and not-ready errors with **exactly one** byte committed per valid completed transfer.
- [ ] STATUS, MESSAGE IN, BUS FREE, SCSI reset and incomplete/invalid access sequences are guest-visible and deterministic; neither controller nor disk directly manipulates guest RAM.
- [ ] No separate hardware DMA/IRQ, pin-level bus simulator, second initiator or mandatory general bus library is implemented.
- [ ] Validate against an identified original Plus ROM/SCSI Manager access trace as soon as available. Record ROM hash, actual register sequence and deviations; mark unavailable original-ROM tests **NOT TESTED**.
- [ ] Document tests, unsupported optional features, commits and explicit review before closing.

### Out of scope

No disk command/image implementation (separate `PLUS-EMU-0005`), disk format conversion, partition/HFS tooling, Tima/Plus SCSI driver, physical cabling, general electrical-level chip simulation, bus contention, target mode, multi-initiator support or disconnect/reselect initially. Add observed ROM-required behaviour through review rather than preemptive hardware completeness.

### References for code review

- [NCR 5380 SCSI Interface Chip Design Manual (1985)](https://ftpmirror.your.org/pub/misc/bitsavers/components/ncr/scsi/NCR_5380_SCSI_Interface_Chip_Design_Manual_May85.pdf) — register and handshake semantics.
- [Apple — Guide to the Macintosh Family Hardware, 2nd ed., Ch. 11](https://vintageapple.org/inside_o/pdf/Guide_to_Macintosh_Family_Hardware_2nd_Edition_1990.pdf) — Figure 11-4 (Plus DRQ/IRQ/RDY unconnected), pp. 392–393 (normal, pseudo-DMA, polled/blind transfers and absent Plus DRQ hold-off).
- [Macintosh Plus CAS PAL reverse-engineering](https://retro.co.za/ccc/mac/ReverseEngineering/PALs.html) — /SCSI and /DACK decoder equations with A9 selection; no need to emulate PAL pin signals.
- [Apple — Using the SCSI Manager](https://leopard-adc.pepas.com/documentation/mac/Devices/Devices-122.html) — polled/blind transfers and Plus-specific limitations.
- [Apple Technical Note DV14 — SCSI Bugs](https://leopard-adc.pepas.com/technotes/dv/dv_14.html) — historical boot, reset, phase and transfer pitfalls.
- [MAME Macintosh hardware model](https://github.com/mamedev/mame/blob/master/src/mame/apple/mac128.cpp) — independent mapping reference, **not source to copy** into this differently licensed emulator.
- [Tima/Plus development roadmap](../../01-development-roadmap.md).

**Notes**

- **2026-11-18 — Discovery:** User requested the first dedicated ticket for the missing uMac SCSI controller. Confirmed in `main` at `a3549f3`: no NCR 5380 module/dispatch and relevant address window marked dummy. No runtime test was claimed.
- **2026-11-18 — Classification:** Treat as a BUG for missing M0001AP capability, not an upstream regression. Reuse preallocated roadmap ID `PLUS-EMU-0004`.
- **2026-11-18 — Scope:** Controller/register model here; SCSI disk image/BlueSCSI boot stays in `PLUS-EMU-0005` and OS integration in `PLUS-SYS-0006`. Ticket opening is not implementation approval or a milestone reorder.
- **2026-11-18 — Research:** Consulted NCR documentation, Apple's SCSI Manager and MAME hardware mapping. Decoder aliases, actual ROM access patterns and pseudo-DMA edge behaviour remain to be established during implementation.
- **2026-11-18 — Hardware-design clarification (user discussion):** The Macintosh Plus has **no dedicated SCSI DMA controller**; 5380 DRQ/IRQ are **not wired** to CPU interrupt/DMA logic. The CAS PAL's A9-dependent /SCSI vs /DACK address decode can be represented directly as a branch in uMac's memory-mapped access handler. Preserve internal 5380 pseudo-DMA state, but **do not** model a physical DACK signal or a separate PAL/DMA peripheral. Corrected the provisional `0x580100` DACK example to A9-set `0x580200`; exact aliases, access widths and premature-transfer semantics still require testing. This clarifies scope only, **not** an implementation approval or change in milestone priority.

- **2026-11-18 — Related target ticket opened:** [PLUS-EMU-0005](PLUS-EMU-0005.md) specifies the independent direct-access SCSI hard-disk target, raw BlueSCSI-compatible whole-device image and end-to-end bus integration. This ticket retains the NCR 5380 and the bus/target attachment interface, including a synthetic target for isolated tests. Neither disk-image I/O nor image/boot tooling is to be embedded in the controller.

- **2026-11-18 — Simplified design (user decision):** Narrow the controller to Macintosh Plus **ROM-visible NCR 5380 initiator behaviour**. The firmware never reads a connected-device ID table; it probes IDs through selection. COMMAND phase begins at CDB position zero, and each byte is accepted only after an appropriate REQ/ACK handshake. A small shared transaction/phase engine can emulate the bus values; a separate electrical SCSI simulator, multi-initiator arbitration and independent PAL/DMA models are unnecessary. Preserve controller/target responsibility separation and accurate PIO/pseudo-DMA/error semantics. Revised requirements, proposed steps and acceptance accordingly; no code work or execution approval.


- **2026-11-18 — Scope reduction (user discussion):** Prior requirements were broader than needed for M0001AP. Reframed as firmware-visible NCR 5380 behaviour with a single logical transaction/phase owner rather than independent electrical signals or general SCSI bus machinery. SCSI IDs are selected/probed, not enumerated from a register; command byte 0 starts on entering COMMAND phase, and *only completed* REQ/ACK or pseudo-DMA transfer events advance CDB/data byte counters. Preserve accurate register status/reset/error semantics; target command decode and sector image I/O remain `PLUS-EMU-0005`. No implementation changes or approval.

**Proposed Change**

**For triage and explicit approval — compact Macintosh Plus initiator emulation:**

1. Verify normal vs A9-qualified SCSI MMIO decoding, register selection and real Plus register read/write semantics with headless tests.
2. Create one small logical SCSI phase/handshake owner with a minimal target byte interface; implement idle arbitration, valid ID selection/absent timeout, reset and visible status.
3. Prove a synthetic target receives the *first* command byte upon COMMAND-phase REQ/ACK, accepts each later byte only once, and advances data only on PIO/pseudo-DMA transfer completion.
4. Attach the simple disk command/512-byte-buffer target from [PLUS-EMU-0005](PLUS-EMU-0005.md); validate the READ(6) command / 512 data bytes / STATUS / COMMAND COMPLETE flow through guest MMIO.
5. Trace the original Mac Plus SCSI Manager and address only real compatibility gaps, including premature pseudo-DMA, phase and timing behaviour. Keep the existing patched floppy regression intact.
6. Record test results, hashes, unsupported hardware features and reviewed commits before closure.

**Alternative rejected:** patch Mac ROM SCSI calls or read/write guest RAM from host disk routines. This would not emulate the NCR 5380 register interface and would conceal compatibility defects.

**Remaining verification:** precise mirrors/byte lanes, ROM's actual register sequences, timing and premature pseudo-DMA behaviour. Do not infer success from synthetic transfers alone.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |

**Resolution Description**

Pending.
