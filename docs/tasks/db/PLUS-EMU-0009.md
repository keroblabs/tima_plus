# PLUS-EMU-0009 — Implement Macintosh Plus serial RTC and battery-backed PRAM

**ID:** `PLUS-EMU-0009`  
**Status:** `IN_PROGRESS`  
**Title:** Implement Macintosh Plus serial RTC and battery-backed PRAM

**Description**

At discovery, uMac had no Macintosh Plus real-time clock (RTC) or battery-backed parameter RAM behind VIA Port B. Commit `1bcf2fe` implemented that hardware path, and the project owner confirmed that uMac displayed a clock with a time that differed from Linux. The ROM trace identified a one-bit shift in each response byte. The RTC/VIA timing correction has passed the recorded ROM and System Software verification. Subsequent engineering review found two outstanding **PRAM backing-file safety risks** (unvalidated/aliased destination and concurrent-instance collisions). The working serial RTC design and timing correction are acceptable; the ticket needs focused persistence rework before merge approval. [Original implementation](https://github.com/keroblabs/tima_plus/commit/1bcf2fed25218fdc85598d261d2de6686402a7de)

The M0001AP is a Macintosh Plus and has a real RTC. The goal is a **Macintosh Plus–accurate, VIA-connected serial RTC**, taking its initial time from the Linux host clock, with a 32-bit seconds counter and **256 bytes of persistent PRAM**. This must be observable by the Mac Plus ROM through normal 6522 register accesses; it must not be a paravirtual date/time API, direct low-memory patch, or replacement of guest RTC routines.

**Details**

- **Kind:** `BUG` — missing required hardware-emulation capability.
- **Category:** `PLUS-EMU`.
- **Milestone:** [PLUS-DEV-0002 — Establish the M0001AP hardware profile](../../01-development-roadmap.md#plus-dev-0002--establish-the-m0001ap-hardware-profile).
- **Related planned item:** `PLUS-EMU-0008` (overall VIA/SCC/RTC/keyboard/mouse correctness matrix; not an opened ticket).
- **Branch inspected:** `rtc-implementation`, at base commit `7f6ed0871ec798db5379b199ced83ba4f2fcd2d9` (identical to `main` at discovery).
- **Scope boundary:** RTC serial device, VIA pin connection, Linux-time abstraction, 256-byte PRAM persistence, and focused verification. Not a full rewrite of the 6522 VIA, an IWM implementation, a new guest OS time service, a host RTC driver, or restoration of every unrelated VIA timer.

### 1. Confirmed code evidence at discovery (19 November 2026)

1. `devel/umac/src/main.c`: `via_rb_changed(uint8_t val)` lists bits `[2:0] = RTC controls` but does nothing with them; `via_rb_in()` returns mouse quadrature/button information only, never RTC serial output. `umac_init()` installs these stubs in the `struct via_cb` callback table.
2. `devel/umac/src/via.c`: `via_write()` routes VIA register/Port B operations to `via_update_regb()`, while `via_read_regb()` merges the port output latch with input callback according to `VIA_DDRB`. There is no RTC peripheral state or protocol interpreter. `VIA_DDRB` changes are stored but do not notify a peripheral of pin-direction changes. `via_update_regb()` invokes the callback **before** storing the new output latch; integration must account for this ordering or make a narrowly scoped adjustment.
3. `devel/umac/include/via.h` has callbacks for Port B writes and input reads, but none specifically for a device's direction-sensitive resolved pin state. The change must avoid breaking existing consumers.
4. `devel/umac/include/umac.h` already exposes `umac_1hz_event()`, which invokes `via_caX_event(1)`. `devel/umac/src/unix_main.c` calls it on an approximately one-second `gettimeofday()` cadence. `devel/umac/src/via.c` maps this to VIA interrupt flag bit 0. **This is an existing 1 Hz interrupt approximation, not an emulated RTC counter or PRAM.**
5. `devel/umac/src/main.c` `umac_reset()` resets CPU/overlay/SCSI but has no RTC reset path; `devel/umac/CMakeLists.txt` has no RTC compilation unit or RTC test.
6. `docs/01-development-roadmap.md` already includes RTC correctness under `PLUS-DEV-0002`/`PLUS-EMU-0008`; the missing concrete capability warrants this separate implementation ticket, without altering roadmap sequencing.

At discovery, verification still needed an original ROM boot with RTC and PRAM commands, M0001AP electrical timing measurements, and broader VIA DDR/interrupt testing.

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
- Model command framing, chip-select, partial transactions, and read-data validity at the VIA pins. MAME's early Mac RTC model uses the **high-to-low clock transition**. The Plus V3 read trace and regression tests below establish the observable response timing in uMac.
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

### 6. Design and verification questions

1. What is the minimum VIA callback/API change that correctly detects ORB **and DDRB** transitions without destabilizing current input/output semantics?
2. What edge carries the first read bit across the DDRB turn-around? `EMU9-R1` verified the Plus V3 ROM sequence in uMac. Physical logic-analyzer timing remains **NOT TESTED**.
3. What persistence format best accommodates both 256 PRAM bytes and guest-adjusted RTC time without shared-file races or silent data loss? Document the guest clock adjustment policy before implementation.
4. Should the emulator expose a test-only/fixed-time option in addition to dependency-injected tests? Keep user-facing options minimal unless justified.
5. How should host wall-clock jumps while an emulator instance is running be reconciled with a deliberate guest-set offset? Specify consistent observable semantics.

### Verification evidence for the initial implementation (19 November 2026)

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

**NOT TESTED in the initial implementation run** were Plus ROM RTC transaction capture, physical edge timing, System Software clock and PRAM retention, and unpatched-ROM IWM boot.

### Verification evidence for the clock correction (19 November 2026)

| Check | Evidence | Result |
| --- | --- | --- |
| Isolated RTC and VIA timing | The updated tests sample PB0 after the falling edge. The VIA test reproduces the Plus V3 select, DDRB turn-around and low/high/read sequence. At fixed Unix time `1795114880`, the test expects `0xE724F800`. Both tests failed against `1bcf2fe` and passed with `df9bd44`. | PASS |
| Plus V3 ROM | With `rom.bin` and the supported patched startup, the ROM's RTC routine read `E7 24 F8 00` for commands `9D 99 95 91`. The same trace on `1bcf2fe` read `CF 48 F0 00`. | PASS |
| Guest clock with live Linux time | Finder reached System 7.0.1 from `System70_boot.dsk` with `HD10_512.hda` attached, then System 6.0.7 on a separate SCSI-only launch with a copy of the user's v1 PRAM. In each run, all three samples of guest `Time` (`$020C`) matched Linux to the second in GMT. The System 6 General Control Panel showed `19:16:38` on `11/19/26` while Linux read `19:16:38 GMT` on 19 November 2026. | PASS |
| Guest setting and PRAM retention | A Control Panel clock edit left a `+32 s` guest offset. A later volume edit changed PRAM byte `0x10` from `3` to `5`. After clean exit and restart, the guest clock still led Linux by 32 seconds and the Control Panel showed volume `5`. The runs used a disposable PRAM copy and opened both media images read-only. | PASS |
| SCSI regressions | `scsi_controller`, `scsi_disk`, and the optional `scsi_plus_rom` test passed. | PASS |

The Plus V3 ROM fixture `devel/umac/rom.bin` has SHA-256 `dd908e2b65772a6b1f0c859c24e9a0d3dcde17b1c6a24f4abd8955846d7895e7`. Guest `Time` is the low-memory value System Software loads from the RTC and uses for time queries. [Apple Inside Macintosh](https://leopard-adc.pepas.com/documentation/mac/OSUtilities/OSUtilities-105.html)

Physical logic-analyzer timing remains **NOT TESTED** and belongs to [the hardware profile](../../01-development-roadmap.md#plus-dev-0002--establish-the-m0001ap-hardware-profile). Unpatched-ROM IWM boot remains **NOT TESTED** under [the unmodified-ROM milestone](../../01-development-roadmap.md#plus-dev-0003--run-the-unmodified-macintosh-plus-rom). The ROM trace and System Software runs cover the software acceptance for this correction.

### Review Findings (19 November 2026)

| Finding ID | Classification | Evidence | Required action / acceptance evidence | State |
| --- | --- | --- | --- | --- |
| `EMU9-R1` | `CORRECTION_REQUIRED` | `rtc_begin_read()` in `1bcf2fe` puts bit 7 on PB0 early. The Plus V3 ROM changes DDRB to input, clocks PB1 low then high, and samples PB0 after the first falling edge has advanced to bit 6. The saved v1 PRAM has a zero guest adjustment. A fixed host timestamp gives `0xE724F800` in the RTC and `0xCF48F000` in the ROM's serial read. | Present bit 7 on the first response falling edge. Verify all eight bits using the ROM's VIA sequence and compare the guest clock with Linux. | `VERIFIED` in `df9bd44` by the regression tests, Plus V3 trace, and System 7.0.1 and 6.0.7 checks above. |

| `EMU9-R2` | `CORRECTION_REQUIRED — BLOCKING` | `unix_main.c` accepts arbitrary `-P` paths; `umac_rtc_load()` ignores all load failures; `rtc_pram_save()` unconditionally renames `<path>.tmp` over the destination on normal exit. For example, `-P rom.bin` can replace the actual ROM image with a 272-byte PRAM file. The same risk applies to RAM/floppy/SCSI images and an existing malformed PRAM file. This is a code-path hazard; the reviewer did **not** destructively reproduce it. | Distinguish missing file from existing invalid/inaccessible PRAM; reject unsafe destinations and aliases of ROM/RAM/floppy/SCSI, propagate actionable failures to the front end, and never overwrite unrelated or unvalidated existing data. Add non-destructive negative tests proving a ROM image and a malformed PRAM file remain byte-for-byte unchanged on rejection, alongside first-boot/normal-restart tests. | `OPEN` — merge-blocking. |
| `EMU9-R3` | `CORRECTION_REQUIRED — BLOCKING` | The default `pram.nv` lives in the working directory, with no inter-process coordination. `rtc_pram_save()` uses one fixed `<path>.tmp` filename and unconditional replacement. Two instances using the same directory/path can interfere, truncate the same temporary file, or silently lose one another's changes. This violates the per-instance/non-shared backing-file requirement; no destructive concurrent test was run. | Make concurrent use safe by explicit refusal/locking or per-instance isolation, and use a unique collision-resistant temporary file. Test a two-instance/conflicting-path scenario and confirm existing PRAM stays intact. Document whether save-on-exit alone is an accepted crash/power-loss durability limitation; implement `fsync`/directory synchronization if durability beyond normal exit is claimed. | `OPEN` — merge-blocking. |

**Notes**

- **2026-11-19 — Discovery:** Owner identified missing RTC after merging SCSI to `main` and created `rtc-implementation` from `main`; the branch was identical to `main` when inspected. Code review confirms the three RTC bits are noted but not implemented, and no PRAM backing or RTC serial counter exists. A synthetic 1 Hz VIA event already exists and must be preserved/integrated, not mistaken for the RTC.
- **2026-11-19 — Hardware investigation:** Apple hardware reference confirms the 6522 Port B PB0/PB1/PB2 interface, 4-byte 1904-epoch counter, 1 Hz VIA interrupt and **256-byte Plus PRAM**. MAME and Linux implementations provide useful protocol cross-checks; exact edge/command tolerance must be validated with Plus ROM. No emulator implementation or tests were performed as part of ticket creation.
- **2026-11-19 — Scope proposal:** One focused `BUG` ticket for RTC+PRAM hardware capability, proposed `PLUS-EMU-0009`, linked to the existing hardware-profile milestone. **OPEN for triage/design approval**, not authorization to implement.

- **2026-11-19 — Developer component reference and command clarification:** Added [343-0042-B RTC/PRAM reference](343-0042-B-RTC-PRAM-reference.md) with canonical regular and extended command tables and logical PRAM addressing. Clarified that `0xC1` reads PRAM offset `0x00`, whereas `0x81` reads seconds byte 0; distinguished the one-second VIA **IFR bit 0** from the code's helper-function CA naming. These corrections preserve the original discovery record. No emulator code or tests changed.

- **2026-11-19 — Decision (triage/design):** Proceed with the proposed scope: a platform-neutral serial RTC core in a dedicated `rtc.c`/`rtc.h`, a minimal VIA Port B callback extension to deliver resolved ORB **and** DDRB transitions, injectable host-time abstraction, and a versioned 256-byte PRAM + clock-offset backing file. Persistence policy chosen: a single backing file storing magic/version, a signed 64-bit guest clock offset (classic-epoch seconds minus host Unix seconds), and 256 PRAM bytes; written via temp-file + atomic `rename`. Guest clock writes update the offset anchor relative to host time and keep ticking; the host system clock is never modified. Edge convention: receive/advance on the serial clock **high-to-low** transition (per MAME), pending later ROM-trace confirmation.

- **2026-11-19 — Implementation:** Added `devel/umac/include/rtc.h` and `devel/umac/src/rtc.c` (serial state machine `DESELECTED→RX_CMD→[RX_CMD2]→RX_DATA/TX_DATA→COMPLETE`, seconds counter via injectable `rtc_time_provider`, legacy + extended PRAM with shared backing store/aliasing, write protection, and `rtc_pram_save`/`rtc_pram_load`). Extended `struct via_cb` with an `rb_pins(orb, ddrb)` callback invoked in `via_write()` **after** the register store for both `VIA_RB` and `VIA_DDRB` (so DDRB-only turn-around is delivered); existing `rb_change`, mouse/keyboard, sound, SR and IRQ paths unchanged. Wired the RTC from `umac_init()` (callback + `rtc_init`), `via_rb_in()` (PB0 = `rtc_data_out()`), and `umac_reset()` (transient-only `rtc_reset`, PRAM/clock preserved). Added `-P <pram path>` (default `pram.nv`), `umac_rtc_load`/`umac_rtc_save`, and load-on-start/save-on-exit in `unix_main.c`. Host time provided via `time(NULL)` behind the provider. Registered `src/rtc.c` and the two test targets in `CMakeLists.txt`.

- **2026-11-19 — Test:** Built with `-Wall -Wextra -pedantic` (no new warnings) and ran both new headless tests. See the verification matrix evidence under **Details → Verification evidence**. Isolated `umac_rtc_test` and integration `umac_via_rtc_test` both pass; SCSI controller/disk tests still pass when run directly. ROM-trace / System Software acceptance and logic-analyzer edge timing remain **NOT TESTED** (future verification items).

- **2026-11-19 — Owner verification and status change:** The project owner reported that the implementation makes a clock appear in uMac, but the displayed time differs from Linux. `REVIEW` → `IN_PROGRESS` to compare the host-derived RTC counter, the saved clock offset, and the bytes read by the ROM through VIA Port B.

- **2026-11-19 — Investigation and code review:** The launch's saved `pram.nv` contains the default offset of `2082844800` seconds. Its RAM dump holds guest `Time` (`$020C`) of `0xCF48F017`, corresponding to 14 March 2014 while Linux was on 19 November 2026. Reproduced with Plus V3 and System 7.0.1 using a disposable PRAM file and host `time()` fixed at `1795114880`. A trace at the RTC/VIA boundary showed the ROM writes DDRB at `$400268`, clocks PB1 low/high at `$400274`/`$400276`, then reads PB0 at `$400278`. Commands `9D 99 95 91` returned `CF 48 F0 00` for expected bytes `E7 24 F8 00`. Every response drops its first bit and repeats its last bit. The existing tests read PB0 before clocking and all four CTest entries passed on the affected build. Marked `1bcf2fe` `CHANGES_REQUESTED` for the response timing. [Plus ROM read loop](https://github.com/jonathanschilling/mac_rom/blob/main/plus/boot/vectors.s#L432-L445)

- **2026-11-19 — Rework:** `df9bd44` prepares the response at command completion and drives its most significant bit on the first subsequent falling edge, then drives the remaining bits on the next seven edges. Updated `tests/test_rtc.c` to sample after the falling edge and `tests/test_via_rtc.c` to reproduce the ROM's VIA sequence with the traced timestamp.

- **2026-11-19 — Automated test:** Configured an isolated Debug build with `cmake -S devel/umac -B "$rtc_build" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DUMAC_TEST_PLUS_ROM="$PWD/devel/umac/rom.bin"` and built with `cmake --build "$rtc_build" -j 4`. With the revised tests and original RTC code, `ctest --test-dir "$rtc_build" --output-on-failure` failed `rtc_core` and `via_rtc`. After the fix all five tests passed, including the optional Plus V3 SCSI test. `cmake --build build -j 4` and `ctest --test-dir build --output-on-failure` passed all four default tests. `make -C devel/umac -j4 MEMSIZE=4096` also linked the emulator in a temporary source copy. The clean builds reported existing Musashi unused-variable warnings.

- **2026-11-19 — ROM and System Software test:** Traced `rtc_via_update()` and `rtc_data_out()` in a diagnostic build with `time()` fixed to `1795114880`. The corrected ROM reads returned `E7 24 F8 00`. Booted the normal build offscreen in temporary directories with `SDL_VIDEODRIVER=offscreen SDL_RENDER_DRIVER=software TZ=Europe/London`, `umac -r rom.bin -d System70_boot.dsk -s HD10_512.hda -P <temporary-pram>`, and a separate launch with `-s HD10_512.hda` and a copy of the existing PRAM. Live guest time matched Linux in both runs. Opened the System 6 Control Panel and inspected the displayed date and time. With a disposable PRAM copy, set a guest clock adjustment, changed speaker volume, and verified both on restart as recorded above.

- **2026-11-19 — Status change:** `IN_PROGRESS` → `REVIEW` with `EMU9-R1` verified. Recorded correction commit `df9bd44` as `UNREVIEWED`. Resolution is pending explicit acceptance.

- **2026-11-19 — Project-owner review and manual verification:** Owner explicitly reported independently reviewing the code and being happy with it; the **System 6 Control Panel time matched the Linux host**. This owner-provided manual evidence supplements, but is distinct from, the recorded implementation-run automated and ROM tests.
- **2026-11-19 — Independent engineering code review:** Reviewed the RTC serial state machine, time conversion, legacy/extended PRAM mapping, VIA Port B ORB/DDRB integration, Unix lifecycle, test sources and `df9bd44` corrective diff. The serial RTC implementation and `df9bd44` response-bit timing correction are accepted (that commit marked `APPROVED`). **No build or executable tests were run by this reviewer.** Found two merge-blocking PRAM persistence risks `EMU9-R2` and `EMU9-R3` detailed above, so `1bcf2fe` remains `CHANGES_REQUESTED`. These are static code-path findings, not claims that data loss was reproduced. Requested targeted persistence rework, not changes to the verified RTC hardware protocol. `REVIEW` → `IN_PROGRESS`; full approval and resolution remain pending.

**Proposed Change**

Introduce a dedicated, independently testable Macintosh Plus 256-byte RTC/PRAM device connected to the 6522 VIA Port B serial pins with proper DDR handling, command state machine, seconds-since-1904 counter based on Linux time, guest-settable time, persisted battery-backed PRAM/clock anchor, and coherent 1 Hz VIA notifications. Integrate without direct-ROM or low-memory shortcuts, retain patched uMac mode and SCSI/floppy compatibility, and satisfy the acceptance matrix above.

For `EMU9-R1`, load the response byte at command completion and drive one bit on each subsequent falling edge of PB1, beginning with bit 7. Exercise the Plus ROM's DDRB turn-around and low/high/sample sequence in the VIA test. Compare the four serial bytes and the guest clock against Linux during boot.

**Review-requested rework (`EMU9-R2`, `EMU9-R3`):** Preserve the successful RTC core, VIA and timing implementation. Validate and isolate `-P` backing paths so no existing non-PRAM file or disk/ROM/RAM image can be silently replaced; distinguish initial creation from invalid/corrupt file load, and report errors. Prevent concurrent instances from sharing/colliding on a backing file or fixed temporary path. Document durability expectations and add negative tests for rejection, collision and restart. Re-run RTC, VIA and SCSI tests and a ROM/System 6 clock smoke test before resubmitting.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |
| `rtc-implementation` | [`1bcf2fe`](https://github.com/keroblabs/tima_plus/commit/1bcf2fed25218fdc85598d261d2de6686402a7de) | `CHANGES_REQUESTED` | Serial RTC + 256-byte PRAM core, VIA integration, Unix persistence and headless tests |
| `rtc-implementation` | [`df9bd44`](https://github.com/keroblabs/tima_plus/commit/df9bd44e1733b423ed38fb5f396b68549a2cedf6) | `APPROVED` | Correct RTC response bit timing and replay the Plus ROM's clock sequence in tests; reviewed 2026-11-19 |

**Resolution Description**

Pending.
