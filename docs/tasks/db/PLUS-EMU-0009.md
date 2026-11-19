# PLUS-EMU-0009 — Implement Macintosh Plus serial RTC and battery-backed PRAM

**ID:** `PLUS-EMU-0009`  
**Status:** `REVIEW`  
**Title:** Implement Macintosh Plus serial RTC and battery-backed PRAM

**Description**

The `rtc-implementation` branch (created from `main` after the approved SCSI controller/disk merge) has **no emulation of the Macintosh Plus real-time clock (RTC) device**. uMac exposes a VIA Port B interface but does not connect its RTC control/data pins to any serial device or maintain an RTC seconds counter and battery-backed parameter RAM. As a result, ROM/System Software cannot read or set the machine clock through the authentic hardware interface or persist its control-panel settings through power cycles. This is a **confirmed missing capability in the upstream-minimalist uMac-derived emulator**, not a regression introduced by the recent SCSI work.

The M0001AP is a Macintosh Plus and has a real RTC. The goal is a **Macintosh Plus–accurate, VIA-connected serial RTC**, taking its initial time from the Linux host clock, with a 32-bit seconds counter and **256 bytes of persistent PRAM**. This must be observable by the Mac Plus ROM through normal 6522 register accesses; it must not be a paravirtual date/time API, direct low-memory patch, or replacement of guest RTC routines.

**Details**

- **Kind:** `BUG` — missing required hardware-emulation capability.
- **Category:** `PLUS-EMU`.
- **Milestone:** [PLUS-DEV-0002 — Establish the M0001AP hardware profile](../../01-development-roadmap.md#plus-dev-0002--establish-the-m0001ap-hardware-profile).
- **Related planned item:** `PLUS-EMU-0008` (overall VIA/SCC/RTC/keyboard/mouse correctness matrix; not an opened ticket).
- **Branch inspected:** `rtc-implementation`, at base commit `7f6ed0871ec798db5379b199ced83ba4f2fcd2d9` (identical to `main` at discovery).
- **Scope boundary:** RTC serial device, VIA pin connection, Linux-time abstraction, 256-byte PRAM persistence, and focused verification. Not a full rewrite of the 6522 VIA, an IWM implementation, a new guest OS time service, a host RTC driver, or restoration of every unrelated VIA timer.

### 1. Confirmed code evidence (19 November 2026)

1. `devel/umac/src/main.c`: `via_rb_changed(uint8_t val)` lists bits `[2:0] = RTC controls` but does nothing with them; `via_rb_in()` returns mouse quadrature/button information only, never RTC serial output. `umac_init()` installs these stubs in the `struct via_cb` callback table.
2. `devel/umac/src/via.c`: `via_write()` routes VIA register/Port B operations to `via_update_regb()`, while `via_read_regb()` merges the port output latch with input callback according to `VIA_DDRB`. There is no RTC peripheral state or protocol interpreter. `VIA_DDRB` changes are stored but do not notify a peripheral of pin-direction changes. `via_update_regb()` invokes the callback **before** storing the new output latch; integration must account for this ordering or make a narrowly scoped adjustment.
3. `devel/umac/include/via.h` has callbacks for Port B writes and input reads, but none specifically for a device's direction-sensitive resolved pin state. The change must avoid breaking existing consumers.
4. `devel/umac/include/umac.h` already exposes `umac_1hz_event()`, which invokes `via_caX_event(1)`. `devel/umac/src/unix_main.c` calls it on an approximately one-second `gettimeofday()` cadence. `devel/umac/src/via.c` maps this to VIA interrupt flag bit 0. **This is an existing 1 Hz interrupt approximation, not an emulated RTC counter or PRAM.**
5. `devel/umac/src/main.c` `umac_reset()` resets CPU/overlay/SCSI but has no RTC reset path; `devel/umac/CMakeLists.txt` has no RTC compilation unit or RTC test.
6. `docs/01-development-roadmap.md` already includes RTC correctness under `PLUS-DEV-0002`/`PLUS-EMU-0008`; the missing concrete capability warrants this separate implementation ticket, without altering roadmap sequencing.

**Not yet verified:** a boot/session test exercising real Macintosh ROM RTC reads and PRAM commands; precise serial edge timing against an M0001AP logic-analyzer trace; complete current VIA DDR/interrupt behaviour outside the RTC path. These are acceptance evidence to gather, not claimed results.

### 2. Macintosh Plus hardware contract

**Developer component reference:** [Apple 343-0042-B RTC — serial command/register and PRAM map](343-0042-B-RTC-PRAM-reference.md). This self-contained reference identifies the RTC family, VIA register addresses and pins, canonical command bytes, legacy/extended PRAM alias mapping, write protection, time-counter byte order, and known emulator-source discrepancies. Use it for implementation; refer to external manuals only for further corroboration.

The standard Macintosh Plus has a separate Apple serial clock/PRAM IC in the 256-byte family (commonly identified as **343-0042-B**), interfaced through its **single Rockwell/VII 6522 VIA**. Earlier 128K/512K machines used the 20-byte predecessor; do **not** accidentally emulate that smaller model for the Plus.

| VIA Port B | Direction | Meaning |
| --- | --- | --- |
| PB2 / bit 2 | Mac output | `rtcEnb`: **active low**; low selects RTC transaction, high deselects/aborts transaction |
| PB1 / bit 1 | Mac output | `rtcClk`: host-driven serial clock |
| PB0 / bit 0 | Bidirectional | `rtcData`: host serial transmit or RTC serial response, depending on VIA DDRB and transaction state |

- These are **pins of VIA Port B**, not memory-mapped RTC registers. Software uses the existing VIA address decoding/register mapping. RTC has no separate CPU address range.
- The transfer is serial, **MSB first**: one command byte, then one data byte for ordinary commands; an extended PRAM command uses two command/address bytes and one data byte. Read/write direction and turn-around are controlled by the command and `VIA_DDRB` bit 0.
- Model command framing, serial clock transitions, sampling/output timing, chip-select assertion/deassertion, partial transactions, and read-data validity at the software-visible pin level. MAME's early-Mac RTC model samples/advances on the **high-to-low clock transition**; confirm observable start/finish/read alignment using the Mac Plus ROM before fixing test vectors. Do not implement a byte-level shortcut that ignores VIA bit transitions.
- Map legacy seconds registers 0–3 (and documented aliases), the legacy-accessible 20 PRAM bytes, write-protect register, and the **extended address protocol for all 256 PRAM bytes**. Correctly implement the overlap/alias of legacy and extended PRAM addressing, not two disconnected stores. Decode direction, legal commands, and write-protect effects; avoid speculative undocumented chip commands. Test-mode register handling can be limited to explicitly documented software-observable behaviour, with any unsupported diagnostic-clock acceleration called out.
- The clock is a **32-bit seconds-since-1904-01-01 00:00:00** counter, not Unix seconds, BCD fields, or 1970 epoch. Linux UTC epoch -> classic Mac epoch offset is **2,082,844,800 seconds**. Use 64-bit internal arithmetic and explicitly encode the 32-bit hardware wrap (the classic 2040 limitation); never truncate the Linux `time_t` before conversion.
- The real RTC generates a one-second signal visible at VIA **IFR bit 0** (not to be confused with the 6522 pin naming in our helper functions). Preserve the existing interrupt wiring, enable/mask semantics and unrelated CA2/VBL/keyboard events. Coordinate this periodic event with the chosen clock source and avoid double-generating an RTC 1 Hz interrupt.
- Do not infer that host time zone equals an RTC wire-format field. Clock is elapsed seconds; System Software/PRAM governs display preferences.

**Reference evidence:** Apple, *Guide to the Macintosh Family Hardware*, 2nd ed., Chapter 2 (RTC/PRAM), Chapter 3 pp. 144–146 (RTC pins, 32-bit count, 1 Hz interrupt), Chapter 4 Table 4-11 (classic Mac VIA PB0–PB2), Appendix A (Plus 256-byte PRAM):  
https://manuals.plus/m/a72b5d07eb3d77011312fccd35e350ed46fbddcc0a2847b97ff421c7396cd758  
Independent behaviour references to cross-check, **not copy mechanically**: MAME `macrtc.cpp` (343-0040 vs 343-0042-B, serial edge state machine and extended PRAM), https://github.com/mamedev/mame/blob/master/src/mame/apple/macrtc.cpp; Linux m68k VIA-RTC functions, https://cos.googlesource.com/third_party/kernel/+/refs/heads/main-R125-cos-6.12/arch/m68k/mac/misc.c; Macintosh Plus ROM RTC initialization/disassembly, https://github.com/jonathanschilling/mac_rom/blob/main/plus/boot/vectors.s. Confirm protocol details against the actual Plus ROM. Reference implementations differ in tolerated command low-bit patterns; the guest-ROM trace is decisive.

### 3. Clock source, guest writes, and persistence

- **Default clock source:** Linux `CLOCK_REALTIME` / current host wall-clock seconds. Provide an injectable time-provider abstraction so automated tests can use fixed/advanced timestamps without sleeping or depending on local time zone.
- **Runtime progression:** the RTC advances continuously by elapsed seconds whether a guest reads it or not. Keep guest-visible time coherent with 1 Hz notifications, without simulating the internal 32.768 kHz crystal cycle by cycle. Use a monotonic host clock for timer cadence where appropriate; specify how a host wall-clock adjustment is reflected.
- **Guest clock writes:** provide functional serial writes to each seconds register, including write protection. A coherent, completed guest setting should update a guest-clock offset/anchor relative to host time and then continue ticking (not be overwritten by the next wall-clock poll). Document and test how partial four-byte writes behave.
- **Restart contract:** on first boot without saved RTC state, initialize the seconds counter from host Linux time. On later boots, preserve settings and any deliberate guest-set clock offset while continuing time through emulator downtime using the host wall clock. Never change the Linux system clock when the guest sets the RTC. The precise persisted clock anchor/offset format is an implementation decision to document.
- **PRAM storage:** a dedicated per-emulator-instance nonvolatile backing file with **all 256 bytes**, independent of RAM, floppy image, and SCSI disk. Define versioned/validated file format (or a justified simple raw format plus separate clock metadata), safe create/load/save, missing-file defaults, corrupted/truncated file handling, and a documented path/CLI override. Follow sensible crash-safety rules (dirty state, atomic replace or equivalent, reliable flush); do not lose updates merely because a normal `umac_reset()` occurs.
- **Power/reset separation:** a CPU reset or `umac_reset()` must not erase battery-backed PRAM or reset the running seconds counter. Reset the transient serial state machine when required; define cold start versus warm reset clearly. RTC data survives closing/restarting the emulator through its backing file.
- No shared global `pram.bin` across simultaneous emulator instances without explicit protection. Prevent accidental aliasing with a disk/ROM/RAM image or quietly overwriting an unrelated file.
- Document the expected initial PRAM contents/checksum policy; do not invent already-valid System Software settings. Let the original ROM/System Software initialize genuinely blank PRAM unless verified fixture behaviour requires otherwise.

### 4. Integration boundary

Suggested implementation (names may change after design review):

1. New `devel/umac/include/rtc.h` and `devel/umac/src/rtc.c`: instance RTC data, 256-byte PRAM, decoded command state, serial pin state, clock source/offset, reset and persistence API. Keep Linux file operations and wall-clock access behind a narrow adapter where practical; the serial core should remain testable without SDL.
2. Extend VIA Port B plumbing only as much as required to pass **resolved pin/latch/DDR** changes to the device, including `VIA_DDRB` changes even when ORB is unchanged. Preserve the semantics for PB3–PB7, quadrature/button, sound, interrupts and keyboard shift-register machinery.
3. Wire the RTC from `umac_init()`, `via_rb_changed()`/equivalent, `via_rb_in()`, reset and periodic clock update. Avoid reentrant callbacks, writing a new output to VIA while reading inputs, or modifying the three RTC pins behind the CPU's back.
4. Integrate CLI/configuration and lifecycle in `devel/umac/src/unix_main.c`, and register the module/tests in `devel/umac/CMakeLists.txt`.
5. Preserve compatibility of existing SCSI tests, SCSI/floppy boot modes, ROM-patched baseline, and later authentic/unpatched-ROM work. No ROM patch may supply RTC values.

### 5. Proposed acceptance and verification matrix

| Area | Required evidence |
| --- | --- |
| Pin wiring/DDR | Tests write ORB/DDRB, assert/deassert active-low PB2, clock PB1 and read PB0. Input reads return RTC bit only in input direction; output reads preserve VIA latch semantics; PB3–PB7 and mouse/keyboard behaviour unaffected. |
| Serial framing | MSB-first command + data read/write, clock edge/turnaround, aborted/incomplete transaction, selection reset and repeated transfers, including the ROM-observed `0xC1` (**read PRAM byte 0x00**, not a seconds-register read) and `0x81` (**read seconds byte 0**). |
| Clock read | Fixed injected Linux UTC timestamp yields correct four Mac-epoch bytes, stable repeated reads, progression across seconds, no BCD/localtime conversion, 32-bit boundary/wrap test. |
| Clock write | Guest serial writes update readback and continue counting, including bytewise writes and write protection; host clock remains unchanged. |
| PRAM commands | Legacy 20-byte aliases and extended 256-byte addressing, boundary addresses `0x00`, `0x13`, `0x1F`, `0x20`, `0xFF`, independent values and read/write protect. |
| PRAM persistence | Missing-file/cold-start, normal exit/restart, warm reset, multiple clean launches, corrupt/truncated backing, read-only/error paths, and separate emulator instances. |
| IRQ/time | Existing 1 Hz / IFR bit 0 / IER bit 0 behaviour verified with controlled time advancement. No duplicate pulses; VBL and keyboard interrupts remain operational. |
| ROM integration | Capture genuine Plus ROM RTC pin transactions and verify decoded commands/data. Inspect guest Date & Time / Control Panel and PRAM setting retention using available System Software; distinguish **manual** verification from automated. |
| Regressions | Build with warnings, add headless RTC test target, pass existing SCSI controller/disk tests, and repeat SCSI+floppy boot smoke test with RTC enabled. Do not claim an unpatched-ROM IWM boot until that separate milestone actually works. |

Record exact commands, test fixtures, host time source, expected/observed RTC bytes, test limitations and any screenshot/trace artefacts in **Notes** before requesting review.

### 6. Open design/verification questions

1. What is the minimum VIA callback/API change that correctly detects ORB **and DDRB** transitions without destabilizing current input/output semantics?
2. What exact edge/bit timing does the M0001AP ROM use on the first read bit, especially across the DDRB turn-around? Confirm with ROM trace, and later a hardware logic analyzer if ambiguity remains.
3. What persistence format best accommodates both 256 PRAM bytes and guest-adjusted RTC time without shared-file races or silent data loss? Document the guest clock adjustment policy before implementation.
4. Should the emulator expose a test-only/fixed-time option in addition to dependency-injected tests? Keep user-facing options minimal unless justified.
5. How should host wall-clock jumps while an emulator instance is running be reconciled with a deliberate guest-set offset? Specify consistent observable semantics.

**Notes**

- **2026-11-19 — Discovery:** Owner identified missing RTC after merging SCSI to `main` and created `rtc-implementation` from `main`; the branch was identical to `main` when inspected. Code review confirms the three RTC bits are noted but not implemented, and no PRAM backing or RTC serial counter exists. A synthetic 1 Hz VIA event already exists and must be preserved/integrated, not mistaken for the RTC.
- **2026-11-19 — Hardware investigation:** Apple hardware reference confirms the 6522 Port B PB0/PB1/PB2 interface, 4-byte 1904-epoch counter, 1 Hz VIA interrupt and **256-byte Plus PRAM**. MAME and Linux implementations provide useful protocol cross-checks; exact edge/command tolerance must be validated with Plus ROM. No emulator implementation or tests were performed as part of ticket creation.
- **2026-11-19 — Scope proposal:** One focused `BUG` ticket for RTC+PRAM hardware capability, proposed `PLUS-EMU-0009`, linked to the existing hardware-profile milestone. **OPEN for triage/design approval**, not authorization to implement.

- **2026-11-19 — Developer component reference and command clarification:** Added [343-0042-B RTC/PRAM reference](343-0042-B-RTC-PRAM-reference.md) with canonical regular and extended command tables and logical PRAM addressing. Clarified that `0xC1` reads PRAM offset `0x00`, whereas `0x81` reads seconds byte 0; distinguished the one-second VIA **IFR bit 0** from the code's helper-function CA naming. These corrections preserve the original discovery record. No emulator code or tests changed.

- **2026-11-19 — Decision (triage/design):** Proceed with the proposed scope: a platform-neutral serial RTC core in a dedicated `rtc.c`/`rtc.h`, a minimal VIA Port B callback extension to deliver resolved ORB **and** DDRB transitions, injectable host-time abstraction, and a versioned 256-byte PRAM + clock-offset backing file. Persistence policy chosen: a single backing file storing magic/version, a signed 64-bit guest clock offset (classic-epoch seconds minus host Unix seconds), and 256 PRAM bytes; written via temp-file + atomic `rename`. Guest clock writes update the offset anchor relative to host time and keep ticking; the host system clock is never modified. Edge convention: receive/advance on the serial clock **high-to-low** transition (per MAME), pending later ROM-trace confirmation.

- **2026-11-19 — Implementation:** Added `devel/umac/include/rtc.h` and `devel/umac/src/rtc.c` (serial state machine `DESELECTED→RX_CMD→[RX_CMD2]→RX_DATA/TX_DATA→COMPLETE`, seconds counter via injectable `rtc_time_provider`, legacy + extended PRAM with shared backing store/aliasing, write protection, and `rtc_pram_save`/`rtc_pram_load`). Extended `struct via_cb` with an `rb_pins(orb, ddrb)` callback invoked in `via_write()` **after** the register store for both `VIA_RB` and `VIA_DDRB` (so DDRB-only turn-around is delivered); existing `rb_change`, mouse/keyboard, sound, SR and IRQ paths unchanged. Wired the RTC from `umac_init()` (callback + `rtc_init`), `via_rb_in()` (PB0 = `rtc_data_out()`), and `umac_reset()` (transient-only `rtc_reset`, PRAM/clock preserved). Added `-P <pram path>` (default `pram.nv`), `umac_rtc_load`/`umac_rtc_save`, and load-on-start/save-on-exit in `unix_main.c`. Host time provided via `time(NULL)` behind the provider. Registered `src/rtc.c` and the two test targets in `CMakeLists.txt`.

- **2026-11-19 — Test:** Built with `-Wall -Wextra -pedantic` (no new warnings) and ran both new headless tests. See the verification matrix evidence under **Details → Verification evidence**. Isolated `umac_rtc_test` and integration `umac_via_rtc_test` both pass; SCSI controller/disk tests still pass when run directly. ROM-trace / System Software acceptance and logic-analyzer edge timing remain **NOT TESTED** (future verification items).

### Verification evidence (2026-11-19)

**Commands (from `build/`):**

```
cmake . && cmake --build . --target umac_rtc_test umac_via_rtc_test
./umac_rtc_test
./umac_via_rtc_test
./umac_scsi_test controller && ./umac_scsi_test disk
```

**Isolated RTC core — `tests/test_rtc.c` → `umac_rtc_test` (PASS):**

```
RTC core tests:
  test_clock_read OK
  test_clock_write OK
  test_wrap OK
  test_pram_aliasing OK
  test_pram_extended OK
  test_write_protect OK
  test_abort OK
  test_persistence OK
All RTC core tests passed.
```

| Area | Evidence (test fixture) | Result |
| --- | --- | --- |
| Clock read | Host Unix `0`, zero guest offset → `0x7C25B080`; bytes `0x81/0x85/0x89/0x8D` = `80 B0 25 7C`; aliases `0x91/0x95/0x99/0x9D` identical; +100 s progression. | PASS |
| Clock write | Guest writes `0x11223344`; host unchanged (`1000`); continues ticking (+5 → `0x11223349`). | PASS |
| 32-bit wrap | Set `0xFFFFFFFE`, +3 s host → `0x00000001`. | PASS |
| PRAM aliasing | `0x41`/`0xC1`/ext `0x00` one byte; `0x21`/`0xA1`/ext `0x10` one byte; ext write ↔ legacy read. | PASS |
| Extended PRAM | Addresses `0x00,0x13,0x1F,0x20,0xFF` independent read/write. | PASS |
| Write protect | `0x35 0x80` blocks PRAM + seconds writes, reads OK; `0x35 0x00` re-enables. | PASS |
| Aborted transfer | Deselect after 4 of 8 data bits → no completed write. | PASS |
| Persistence | Save/reload restores 256 PRAM + guest clock offset; warm reset preserves; missing file and corrupt/truncated file both rejected. | PASS |

**VIA + RTC integration — `tests/test_via_rtc.c` → `umac_via_rtc_test` (PASS):**

```
VIA + RTC integration tests:
  test_clock_through_via OK
  test_pram_through_via OK
  test_ddrb_turnaround OK
  test_one_second_irq OK
All VIA + RTC integration tests passed.
```

Drives the real `via.c` at canonical bus addresses (ORB `0xEFE1FE`, DDRB `0xEFE5FE`, IFR `0xEFFBFE`, IER `0xEFFDFE`) with the exact `main.c` wiring (`rb_pins`→`rtc_via_update`, `rb_in`→`rtc_data_out`):

| Area | Evidence | Result |
| --- | --- | --- |
| Clock through VIA | Serial `0x81/0x85/0x89/0x8D` via ORB/DDRB reconstruct `0x7C25B080`. | PASS |
| PRAM through VIA | Legacy `0x41` write, legacy `0xC1` + extended `0xB8 01` read agree; extended `0xFF` write/read. | PASS |
| DDRB turn-around | After driving PB0 output latch = 1, flipping DDRB bit 0 to input makes PB0 reads track the RTC-driven byte (`0x80`); with PB0 output the ORB read returns the latch. | PASS |
| One-second IRQ | `IER 0x81` enables IFR bit 0; `via_caX_event(1)` asserts IRQ + IFR bit 0; ack clears; VBL (IFR bit 1) independent. | PASS |

**Regressions:** `umac_scsi_test controller` and `umac_scsi_test disk` pass when invoked directly; core builds clean with `-Wall -Wextra -pedantic`. (Note: `ctest` reports the pre-existing SCSI rows as *Not Run* due to a multi-config executable-path quirk in this build tree, unrelated to this change; the binaries themselves pass.)

**NOT TESTED (future acceptance / other milestones):** genuine Macintosh Plus ROM RTC pin-transaction capture and decoded-command verification; exact first-read-bit edge alignment against a logic-analyzer trace; Date & Time / Control Panel retention under System Software; unpatched-ROM IWM boot smoke test. These remain open verification items, not claimed results.

**Proposed Change**

Introduce a dedicated, independently testable Macintosh Plus 256-byte RTC/PRAM device connected to the 6522 VIA Port B serial pins with proper DDR handling, command state machine, seconds-since-1904 counter based on Linux time, guest-settable time, persisted battery-backed PRAM/clock anchor, and coherent 1 Hz VIA notifications. Integrate without direct-ROM or low-memory shortcuts, retain patched uMac mode and SCSI/floppy compatibility, and satisfy the acceptance matrix above.

**This is a proposal only.** Confirm pin timing, persistence/guest-time policy, and narrow VIA integration during ticket triage before writing device code.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |
| `rtc-implementation` | `1bcf2fe` | `UNREVIEWED` | Serial RTC + 256-byte PRAM core, VIA Port B `rb_pins` plumbing, umac/unix integration, isolated and VIA+RTC tests |

**Resolution Description**

Pending review. Implemented on `rtc-implementation` (commit `1bcf2fe`): a platform-neutral serial RTC/PRAM core (`devel/umac/src/rtc.c`, `devel/umac/include/rtc.h`) connected to the 6522 VIA Port B pins through a new `via_cb.rb_pins` resolved-pin callback, with injectable host time, guest-settable 32-bit 1904-epoch counter, legacy + extended 256-byte PRAM with shared/aliased storage and write protection, and a versioned atomically-replaced backing file persisted across restarts (`-P`, default `pram.nv`). Integrated into `umac_init`/`umac_reset` and the Unix front-end without ROM or low-memory shortcuts; existing VIA/SCSI/keyboard/mouse behaviour preserved. Verified by `umac_rtc_test` (8 isolated cases) and `umac_via_rtc_test` (4 bus-level integration cases), all passing; SCSI tests pass directly. Remaining acceptance items (real Plus ROM pin-transaction capture, edge-timing against hardware trace, System Software retention, unpatched-ROM boot) are explicitly **NOT TESTED** and tracked as future verification. Resolution remains `Pending` until reviewed and accepted.
