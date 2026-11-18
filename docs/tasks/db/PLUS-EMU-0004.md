# PLUS-EMU-0004 — Implement Macintosh Plus NCR 5380 SCSI controller

**ID:** `PLUS-EMU-0004`  
**Status:** `OPEN`  
**Title:** Implement Macintosh Plus NCR 5380 SCSI controller

**Description**

The current uMac reference does **not implement the Macintosh Plus NCR 5380 SCSI controller**. The emulator therefore cannot expose the actual SCSI registers, controller state or bus handshakes to the original M0001AP ROM and SCSI Manager. This is a **confirmed missing hardware capability** for our Macintosh Plus model, not a regression in the upstream minimalist uMac emulator.

The goal is a testable NCR 5380 *host-controller* model with correct Macintosh Plus address decoding, register side effects, initiator bus phases, CPU-polled I/O and address-selected pseudo-DMA transfers. **There is no independent SCSI DMA controller or CPU-external SCSI IRQ/DRQ wiring on the M0001AP.** Both transfer paths are driven by MC68000 memory accesses. The SCSI disk target, persistent media image and BlueSCSI-compatible boot are explicitly separate work.

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

### SCSI bus interface and operational model

- Explicitly represent the SCSI data bus/parity and `BSY`, `SEL`, `REQ`, `ACK`, `ATN`, `RST`, `MSG`, `C/D`, `I/O` signal state. The controller reports coherent status derived from the bus, rather than invented fixed values.
- Support **bus free → arbitration → selection → command → optional data in/out → status → message in → bus free**. Include absence of a target, selection timeout, reset, short transfer and unexpected phase handling. Keep the possibility of other phases/target IDs in the architecture without pretending to support unimplemented operations.
- Separate **initiator controller** from **target device**. Introduce a small bus/backend interface that can attach a **synthetic test target** supplying bytes and responses. This tests controller handshakes without implementing a real hard disk in this ticket.
- Support **programmed/polled I/O** and **A9-address-selected pseudo-DMA** in both directions. The MC68000 performs every controller register/data read and write; 5380 internal DMA-mode logic handles the SCSI REQ/ACK handshake, DRQ *status bit*, phase changes and final-byte state. **DRQ, IRQ and RDY pins are not connected on the Macintosh Plus**, but internal DRQ/IRQ status and latch semantics remain observable through registers. Do **not** invent SCSI interrupt delivery, external DMA handshaking, direct controller-to-RAM transfers or CPU-cycle stalling on external DRQ. The Plus has **no hardware hold-off** when software makes an early/blind pseudo-DMA access; verify/document guest-visible early-data behaviour with focused tests and original-ROM evidence.
- Use **deterministic emulated time** (CPU cycles or a declared tick domain), not host `sleep()`, for selection/transfer timing and timeouts.
- Define distinct handling for power-on/MC68000 reset, NCR 5380 reset and guest SCSI bus reset, including what happens to in-flight target commands and status bits.
- **No CPU SCSI interrupt:** The Plus leaves the NCR 5380's IRQ output unconnected, as it does DRQ. Polling is software-driven; the 5380's internal IRQ status and interrupt-latch reset remain part of the register model, but must not feed the MC68000 interrupt controller.

### Candidate module boundaries (proposal, not fixed API)

- `devel/umac/include/scsi5380.h`, `devel/umac/src/scsi5380.c`: per-instance register/chip state and `init/reset/read8/write8/tick` operations.
- Optional `scsi_bus.h/.c`: explicit target interface, signal ownership and phase progression if separation improves clarity.
- `devel/umac/include/machw.h`, `devel/umac/src/main.c`: Plus address decoding (including A9 selecting normal vs pseudo-DMA), MMIO dispatch, reset and emulated-time integration. No standalone CAS PAL/DACK module.
- `devel/umac/CMakeLists.txt`: new module and separate headless test target.
- `devel/umac/tests/`: scripted register and bus tests, plus a minimal synthetic target.

Avoid adding large unrelated functionality to `main.c` or coupling the target directly to SDL, paravirtual `.Sony` hooks or host filesystem. Document controller/backend ownership of handshakes to prevent double-advancing bus phases.

### Acceptance checks — must be demonstrated, not assumed

- [ ] A fresh CMake build succeeds with unchanged existing uMac warnings policy; patched-ROM virtual floppy, display and input smoke tests still work.
- [ ] Normal (A9=0) and pseudo-DMA (A9=1) Mac Plus accesses decode accurately, including `0x580000`, `0x580001`, `0x580040`, `0x580200`, and the A8-only `0x580100` negative check. Confirm A4–A6 register selection, A0 read/write convention, justified mirrors and byte lanes; the `0x580000` window is no longer consumed by `IS_DUMMY`; other regions remain unaffected.
- [ ] All eight registers are covered by read/write tests, including reset values, masks, side effects, status/latch clearing and read-vs-write differences.
- [ ] Bus free, arbitration, selection (present and absent ID), timeout, command, data in/out, status, message in and release are exercised by a synthetic target **only through the emulated register/bus interface**.
- [ ] Polled transfers and address-selected pseudo-DMA in both directions pass multi-byte, final-byte, phase-change and not-ready tests with observable REQ/ACK and internal DRQ status; a CPU access initiates at most the relevant 5380 transfer action, **never** a direct RAM copy.
- [ ] Guest-visible reset and emulated machine reset cleanly restore chip/bus state; no MC68000 SCSI IRQ, DMA controller, external DRQ/IRQ dependency or unintended CPU stall is introduced.
- [ ] Error cases (no target, unsupported phase/command, missing byte, premature access, mid-transfer reset) are deterministic and not silently treated as successful disk operations.
- [ ] If legally sourced ROM/System media are available, an identified original Macintosh Plus ROM SCSI Manager smoke test (ROM version/hash and results documented) exercises the controller. If unavailable, mark this **NOT TESTED** and do not claim full original-ROM compatibility or resolve the ticket prematurely.
- [ ] Tests, build/toolchain revision, known gaps and controller-vs-disk integration boundary are documented; reviews/approvals and exact implementation commits are recorded here.

### Out of scope

No complete SCSI disk implementation, image format conversion, Apple partition-map boot, BlueSCSI image compatibility, Tima/Plus SCSI driver or filesystem, physical cables/termination, IWM rework, or faithful electrical-level NCR chip modelling. These require the related ticket(s), not a stealth extension of this controller ticket.

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

**Proposed Change**

**For triage and explicit approval, implement in these steps:**

1. Base Macintosh Plus SCSI dispatch on the **CAS PAL's known A9 decode**: `0x580000` region, A9=0 register route, A9=1 pseudo-DMA route; confirm register (A4–A6), read/write (A0), mirrors and widths with original-ROM traces and focused headless address-decoding tests.
2. Add an independent, testable NCR 5380 register/flag model with controlled chip and bus resets. Connect its normal and pseudo-DMA operations directly to CPU byte-MMIO before `IS_DUMMY`, without implementing CAS/DACK pin simulation or changing other memory regions.
3. Add bus signal ownership and a deterministic initiator-side phase state machine. Connect a minimal test target over a documented interface, **not** through a paravirtual disk shortcut.
4. Implement CPU-driven polled and address-selected pseudo-DMA data handshake paths, timeout and error handling, ensuring no direct RAM copying, no interrupt/DRQ wiring, and no later-Mac DRQ hold-off behaviour. Document which genuine Macintosh Plus premature/blind-transfer quirks are simulated.
5. Add host-side functional/negative tests and preserve the existing patched uMac ROM/floppy boot regression.
6. Validate where possible against the **original** Mac Plus ROM/SCSI Manager, recording ROM revision/hash without shipping ROM or System Software. Review controller coverage before considering `PLUS-EMU-0005`.

**Alternative rejected as an initial approach:** patch SCSI Manager calls to invoke host disk reads directly. That would not model the NCR 5380, would mask original ROM compatibility errors, and would undermine eventual BlueSCSI testing.

**Unresolved until investigation:** precise 5380 address mirrors and unsupported/non-byte access semantics, 5380 internal not-ready/premature pseudo-DMA behaviour, bus timing and test target API. **A9-based /SCSI vs /DACK selection is established**, and physical DACK signal simulation is explicitly unnecessary. Do not silently encode remaining assumptions as hardware facts.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |

**Resolution Description**

Pending.
