# PLUS-EMU-0004 — Implement Macintosh Plus NCR 5380 SCSI controller

**ID:** `PLUS-EMU-0004`  
**Status:** `OPEN`  
**Title:** Implement Macintosh Plus NCR 5380 SCSI controller

**Description**

The current uMac reference does **not implement the Macintosh Plus NCR 5380 SCSI controller**. The emulator therefore cannot expose the actual SCSI registers, controller state or bus handshakes to the original M0001AP ROM and SCSI Manager. This is a **confirmed missing hardware capability** for our Macintosh Plus model, not a regression in the upstream minimalist uMac emulator.

The goal is a testable NCR 5380 *host-controller* model with correct Macintosh Plus address decoding, register side effects, initiator bus phases and CPU-driven/pseudo-DMA transfers. The SCSI disk target, persistent media image and BlueSCSI-compatible boot are explicitly separate work.

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

### Required Macintosh Plus memory-mapped behaviour

The Macintosh Plus maps its NCR 5380 in the **`0x580000–0x5fffff`** region. Unlike a plain 16-bit register array, guest access is by **byte-oriented addresses**, conventionally documented as `$580drn`: `r` = NCR register 0–7; `n=0` for a read and `n=1` for a write; `d` controls normal chip-select versus DACK/pseudo-DMA-qualified access. Investigate and document valid mirrors/aliases and the exact Plus wiring before finalizing decoder masks. Example normal accesses to verify: `0x580000` (read R0), `0x580001` (write R0), `0x580040` (read R4); `0x580100` is a DACK-window access candidate. These examples are **reference-derived and require test confirmation**.

Introduce SCSI decoding **before** the existing `IS_DUMMY` fall-through. Preserve the other memory regions. Document what happens for unsupported or non-byte MMIO accesses, with explicit guest-visible behaviour rather than silently discarding required cycles.

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
- Support **programmed/polled I/O** and **DACK-qualified pseudo-DMA** in both directions, including REQ/ACK sequencing, DRQ, phase change and last-byte handling. The original Macintosh Plus has important **blind-transfer quirks**; in particular, do **not** assume later Macintosh hardware can stall the CPU until a byte is ready. Verify early-data behaviour from Apple documentation and targeted tests.
- Use **deterministic emulated time** (CPU cycles or a declared tick domain), not host `sleep()`, for selection/transfer timing and timeouts.
- Define distinct handling for power-on/MC68000 reset, NCR 5380 reset and guest SCSI bus reset, including what happens to in-flight target commands and status bits.
- **Do not invent a new CPU SCSI interrupt.** The Macintosh Plus SCSI Manager historically uses polling and the machine lacks the later Macintosh SCSI interrupt capability; internal 5380 IRQ status still needs correct semantics.

### Candidate module boundaries (proposal, not fixed API)

- `devel/umac/include/scsi5380.h`, `devel/umac/src/scsi5380.c`: per-instance register/chip state and `init/reset/read8/write8/tick` operations.
- Optional `scsi_bus.h/.c`: explicit target interface, signal ownership and phase progression if separation improves clarity.
- `devel/umac/include/machw.h`, `devel/umac/src/main.c`: Plus address decoding, MMIO dispatch, reset and emulated-time integration.
- `devel/umac/CMakeLists.txt`: new module and separate headless test target.
- `devel/umac/tests/`: scripted register and bus tests, plus a minimal synthetic target.

Avoid adding large unrelated functionality to `main.c` or coupling the target directly to SDL, paravirtual `.Sony` hooks or host filesystem. Document controller/backend ownership of handshakes to prevent double-advancing bus phases.

### Acceptance checks — must be demonstrated, not assumed

- [ ] A fresh CMake build succeeds with unchanged existing uMac warnings policy; patched-ROM virtual floppy, display and input smoke tests still work.
- [ ] Normal and DACK-qualified Mac Plus address accesses decode accurately; the `0x580000` window is no longer silently consumed by `IS_DUMMY`; other address regions remain unaffected.
- [ ] All eight registers are covered by read/write tests, including reset values, masks, side effects, status/latch clearing and read-vs-write differences.
- [ ] Bus free, arbitration, selection (present and absent ID), timeout, command, data in/out, status, message in and release are exercised by a synthetic target **only through the emulated register/bus interface**.
- [ ] Polled transfers and pseudo-DMA in both directions pass multi-byte, final-byte, phase-change and not-ready tests with observable REQ/ACK/DRQ behaviour.
- [ ] Guest-visible reset and emulated machine reset cleanly restore chip/bus state; no unexpected MC68000 SCSI IRQ is emitted.
- [ ] Error cases (no target, unsupported phase/command, missing byte, premature access, mid-transfer reset) are deterministic and not silently treated as successful disk operations.
- [ ] If legally sourced ROM/System media are available, an identified original Macintosh Plus ROM SCSI Manager smoke test (ROM version/hash and results documented) exercises the controller. If unavailable, mark this **NOT TESTED** and do not claim full original-ROM compatibility or resolve the ticket prematurely.
- [ ] Tests, build/toolchain revision, known gaps and controller-vs-disk integration boundary are documented; reviews/approvals and exact implementation commits are recorded here.

### Out of scope

No complete SCSI disk implementation, image format conversion, Apple partition-map boot, BlueSCSI image compatibility, Tima/Plus SCSI driver or filesystem, physical cables/termination, IWM rework, or faithful electrical-level NCR chip modelling. These require the related ticket(s), not a stealth extension of this controller ticket.

### References for code review

- [NCR 5380 SCSI Interface Chip Design Manual (1985)](https://ftpmirror.your.org/pub/misc/bitsavers/components/ncr/scsi/NCR_5380_SCSI_Interface_Chip_Design_Manual_May85.pdf) — register and handshake semantics.
- [Apple — Using the SCSI Manager](https://leopard-adc.pepas.com/documentation/mac/Devices/Devices-122.html) — polled/blind transfers and Plus-specific limitations.
- [Apple Technical Note DV14 — SCSI Bugs](https://leopard-adc.pepas.com/technotes/dv/dv_14.html) — historical boot, reset, phase and transfer pitfalls.
- [MAME Macintosh hardware model](https://github.com/mamedev/mame/blob/master/src/mame/apple/mac128.cpp) — independent mapping reference, **not source to copy** into this differently licensed emulator.
- [Tima/Plus development roadmap](../../01-development-roadmap.md).

**Notes**

- **2026-11-18 — Discovery:** User requested the first dedicated ticket for the missing uMac SCSI controller. Confirmed in `main` at `a3549f3`: no NCR 5380 module/dispatch and relevant address window marked dummy. No runtime test was claimed.
- **2026-11-18 — Classification:** Treat as a BUG for missing M0001AP capability, not an upstream regression. Reuse preallocated roadmap ID `PLUS-EMU-0004`.
- **2026-11-18 — Scope:** Controller/register model here; SCSI disk image/BlueSCSI boot stays in `PLUS-EMU-0005` and OS integration in `PLUS-SYS-0006`. Ticket opening is not implementation approval or a milestone reorder.
- **2026-11-18 — Research:** Consulted NCR documentation, Apple's SCSI Manager and MAME hardware mapping. Decoder aliases, actual ROM access patterns and pseudo-DMA edge behaviour remain to be established during implementation.

**Proposed Change**

**For triage and explicit approval, implement in these steps:**

1. Determine Macintosh Plus register mapping, DACK selection and supported access widths from references and a ROM trace; write headless address-decoding tests first.
2. Add an independent, testable NCR 5380 register/flag model with controlled chip and bus resets. Integrate byte-MMIO before `IS_DUMMY` without altering other memory regions.
3. Add bus signal ownership and a deterministic initiator-side phase state machine. Connect a minimal test target over a documented interface, **not** through a paravirtual disk shortcut.
4. Implement polled and pseudo-DMA data handshake paths, timeout and error handling, documenting which genuine Macintosh Plus quirks are simulated.
5. Add host-side functional/negative tests and preserve the existing patched uMac ROM/floppy boot regression.
6. Validate where possible against the **original** Mac Plus ROM/SCSI Manager, recording ROM revision/hash without shipping ROM or System Software. Review controller coverage before considering `PLUS-EMU-0005`.

**Alternative rejected as an initial approach:** patch SCSI Manager calls to invoke host disk reads directly. That would not model the NCR 5380, would mask original ROM compatibility errors, and would undermine eventual BlueSCSI testing.

**Unresolved until investigation:** precise 5380 address mirrors and DACK behaviour, non-byte access semantics, bus timing/early data semantics, and test target API. Do not silently encode assumptions as hardware facts.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |

**Resolution Description**

Pending.
