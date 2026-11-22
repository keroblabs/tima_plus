# PLUS-ROM-0002 — Print standalone ROM diagnostics over MC68681 UART-B

**ID:** `PLUS-ROM-0002`

**Status:** `REVIEW`

**Title:** Print standalone ROM diagnostics over MC68681 UART-B

**Reporter:** Codex, on behalf of the project owner

**Reviewer:** Project owner (Kerob Labs)

**Assignee:** Codex

**Description**

The standalone Tima Boot ROM reports its result by writing a marker to guest RAM. Reading the marker requires inspecting uMac's memory dump. Add an MC68681 debug device and report the ROM test through UART-B so the result is visible on the host console while the ROM runs.

**Details**

- Kind: `TASK`
- Category: `PLUS-ROM`
- Milestone: standalone ROM smoke test on the [PLUS-DEV-0002 hardware profile](../../01-development-roadmap.md#plus-dev-0002--establish-the-m0001ap-hardware-profile).
- Working branch: `pre-release-rom-test`, based on `5489ed7`. The preceding RAM-marker results are recorded in [PLUS-ROM-0001](PLUS-ROM-0001.md).
- The `0xC00000–0xC0001F` window is available for the debug device in uMac. The source model is in the local `m68k_emulator` checkout at `emulator/peripherals/m68681_emulator.c` and `.h` (`24ee077`). Its 16-bit bus layout uses odd byte addresses with a two-byte stride. Its host loop consumes UART-B output and flushes stdout.
- Tima Boot builds a freestanding 68000 ROM. The baseline assumed a sibling toolchain directory. The Makefile now defaults to the installed host tools at `/home/marcio/Projects/languages/vbcc_pack/vport/build/host/bin`. [Tima Boot](../../../devel/tima_rom/)
- Development uMac builds decode the DUART at `0xC00000–0xC0001F` with both ROM paths. Odd bytes select 8-bit registers. Even byte and wide reads return all ones, and their writes have no side effects. uMac drains both transmitters at CPU slice boundaries and flushes UART-B to the SDL frontend's stdout. Sink attachment is independent of decode and reset.
- `UMAC_DEVELOPMENT_DUART` defaults to CMake `ON` and Make `1`. CMake `OFF` or Make `0` excludes the model, address decode, reset hooks and host sink. That build uses the original M0001AP map with either ROM-loading policy. The standalone `devel/tima_rom` console and the boot-block probe are development fixtures.

### Reviewer comment — ROM-independent development DUART (22 November 2026)

**Review outcome: one change requested; ticket remains `REVIEW`, not accepted/closed.**

The MCU/ROM loading mode must **not control whether the MC68681 exists**.
The current implementation in `devel/umac/src/main.c` checks
`debug_uart_stream && IS_M68681(address)`, while
`devel/umac/src/unix_main.c` only attaches `umac_debug_uart(stdout)`
if `--raw-rom` is selected. This makes `0xC00000–0xC0001F` invisible
while booting the **unmodified Macintosh Plus V3 ROM** and therefore to
future disk-loaded `tima_boot` programs.

**Required change:** Separate the development-peripheral enable decision
from ROM selection. A *development-enabled* uMac must decode exactly the
same MC68681 8-bit/odd-lane register window and support its existing
UART-B stdout diagnostic sink regardless of whether the guest uses
`--raw-rom` or the **original Plus V3 ROM with `-d`**.
`--raw-rom` must retain its own original role: selecting an alternate
128 KiB reset ROM/checksum policy. Do not change its validity checks
or weaken normal ROM/Sony/IWM boot semantics. Do not expose the UART as
a Macintosh ROM service; it is an **emulator hardware extension**.

**Hardware boundary:** The actual M0001AP **has no MC68681**. This
device is for development and testing only. Provide an explicit
**build-time ability to disable/compile out** the extension for final
production/physical-target builds, and ensure that a disabled build
uses the original Macintosh address map, with no simulated MC68681
register responses at `0xC00000–0xC0001F`. Any guest-side console
code relying on this UART must be confined to development builds or
have a fallback; the physical Tima/Plus firmware must never require it.
The precise option name and defaults for development versus production
can be chosen by the implementer, but must be documented and exercised.

**Requested verification:** (a) on the development build, demonstrate
register decode and UART-B output with **both** raw reset ROM and
unmodified Plus V3 ROM + an 800K boot disk; (b) demonstrate that the
unmodified-ROM floppy boot still passes; (c) compile/test the disabled
configuration and show that the development address window is absent;
(d) retain the odd-byte, wide-access, reset, and existing stdout
diagnostic tests. Do not claim a positive guest-side UART test in the
original ROM path until code running under that ROM actually probes or
transmits to the new window.

**Out of scope:** No TCP/socket bridge, host-side bidirectional protocol,
additional DUART interrupts or new firmware features are requested in
this review. Socket transport can be implemented later under a separate
task.

Documentation updated directly by the reviewer in
[`devel/tima_rom/README.md`](../../../devel/tima_rom/README.md)
(commit `6cdd763`) and [`devel/umac/README.md`](../../../devel/umac/README.md)
(commit `c4eb7aa`). Both document **current behaviour versus requested
behaviour**, without implying the emulator change has already landed.

### Review Findings

| Finding ID | Classification | Evidence | Required action / acceptance evidence | State |
| --- | --- | --- | --- | --- |
| R1 | `CORRECTION_REQUIRED` | `9915021` ties DUART decode to the stdout sink and opens that sink only for `--raw-rom`. | Raw and disk-booted guest output verified in development. The production bus test and archive check verified the stock map and compiled-out model. | `VERIFIED` |

### Verification of the first implementation

The 131,072-byte ROM produced this UART-B output before uMac shutdown with SDL's dummy video driver. The test checked the same two lines after clean exit with status zero.

```text
Tima Boot: cookie=0x13579bdf bss=0
ROM_TEST_PASS
```

ROM SHA-256 `5fe2628119e1d97abcac3cf3146c774c898282f3f32e6cc489c560ab6ebdaca5`. The build used the default toolchain path. The ELF has entry `0x00400400`, `.data` in RAM at `0x00001000`, and four bytes of `NOBITS` `.bss` at `0x00001004`. The link resolves the ROM-local `printf` with `-nostdlib`.

| Check | Result | Local evidence under `devel/tima_rom/build/` |
| --- | --- | --- |
| `make -C devel/tima_rom test` | PASS. Standard mode rejected the custom image, raw mode rejected a short image, and the valid raw ROM emitted both exact lines during emulation. | `plus-rom-0002/rom-smoke.log`, `run-xhj8z4uo/umac.log` |
| uMac CTest with local Plus V3 ROM and 800K DiskCopy image | PASS, 17/17 on the baseline and 18/18 with this change. SCSI, RTC/PRAM, VIA, IWM checkpoint, original-ROM desktop boot, write and format all passed in both runs. | `plus-rom-0002/baseline-ctest.log`, `plus-rom-0002/updated-ctest.log` |
| `m68681_debug_uart` | PASS. Covers default map, window boundaries, odd/even and wide accesses, mode pointer, RX and masked IRQ state, binary bytes on B only, transmit-ready and reset. | `plus-rom-0002/updated/Testing/Temporary/LastTest.log` |
| Legacy uMac Make build and the same ROM smoke test | PASS. Ran `make prepare` and `make` in a build copy and booted the ROM using the resulting `main`. | `plus-rom-0002/legacy-make/build-and-smoke.log`, `plus-rom-0002/legacy-make/latest-smoke.log` |
| Previous PLUS-ROM-0001 image under updated uMac | PASS. Rebuilt the ROM and runner from `5489ed7` in a separate build copy and reproduced the previous acceptance result. | `plus-rom-0002/prior-rom/compatibility.log` |
| `python3 -B /home/marcio/Projects/languages/vbcc_pack/vport/tests/test_state1.py` | PASS before and after implementation. ELF/V68B matching, section placement, zero-fill and link rejection checks passed. | `plus-rom-0002/vport-baseline.log`, `plus-rom-0002/vport-after.log` |
| ROM fixtures and ELF inspection | PASS. Poisoned RAM initially contained `0xA5`; the ROM reported `.data` and `.bss` values correctly and the previous 16-byte mailbox at `0x400` still contained `0xA5`. A mixed-format ROM printed signed and unsigned 32-bit limits, pointers, characters, strings and literal percent, and checked the count and error returns. A changed cookie produced `ROM_TEST_FAIL` and an illegal instruction produced `ROM_TEST_TRAP`. Each caused the smoke runner to exit with status 1. | `plus-rom-0002/rom-layout-and-poisoned-ram.log`, `plus-rom-0002/format-boundaries.log`, `plus-rom-0002/fixtures/` |

Run the integration suite with user-supplied ROM and disk files from the repository root.

```sh
cmake -S devel/umac -B devel/tima_rom/build/plus-rom-0002/updated \
  -DUMAC_TEST_PLUS_ROM="$PWD/devel/umac/rom.bin" \
  -DUMAC_TEST_PLUS_FLOPPY="$PWD/devel/umac/plus-system.image"
cmake --build devel/tima_rom/build/plus-rom-0002/updated --parallel 2
ctest --test-dir devel/tima_rom/build/plus-rom-0002/updated --output-on-failure
```

The local Plus V3 ROM and DiskCopy image matched their starting SHA-256 values after the suite. The builds emitted the same unused-variable warnings from Musashi as the baseline. This verification ran under uMac/Musashi and SDL.

### Rework verification

The development emulator booted the supplied unmodified Plus V3 ROM (checksum `4d1f8172`) with `-d duart-probe.dsk`. The 819,200-byte, write-protected image contains a test-only 68000 boot block with header `4c 4b 60 00 00 86 44 18`. The ROM loads the boot blocks via `.Sony` and calls the disk entry at offset `0x02`. The guest checks the reset interrupt vector, reads the even lane and word response, writes and reads back the interrupt vector, then polls UART-B and transmits `MAC_PLUS_DUART_PASS`. The SDL test observed the complete line while uMac was running and verified clean shutdown. [Plus ROM `BootFromDisk` disassembly](https://github.com/jonathanschilling/mac_rom/blob/main/plus/boot/boot_load.s)

| Check | Result | Local evidence under `devel/tima_rom/build/` |
| --- | --- | --- |
| `m68681_plus_rom_disk` with development uMac | PASS. Actual disk guest code emitted `MAC_PLUS_DUART_PASS`, with standard Plus V3 checksum validation and `-d`. | `plus-rom-0002/rework/plus-rom-probe.log`, `plus-rom-0002/rework/development/m68681-disk-m40y8crg/umac.log` |
| Same guest probe against the reviewer baseline `0f14e2c` | Expected failure reproduced. The boot block ran and the read of `0xC00019` followed the unmapped path, so the UART line was absent. | `plus-rom-0002/rework/reviewer-baseline-probe.log` |
| `make -C devel/tima_rom test` | PASS. Both ROM validation rejections and the raw ROM UART-B output passed. ROM SHA-256 is `5fe2628119e1d97abcac3cf3146c774c898282f3f32e6cc489c560ab6ebdaca5`. | `plus-rom-0002/rework/raw-rom-smoke.log`, `run-_mm_gnk2/umac.log` |
| Full development and production CTest suites | PASS, 19/19 and 18/18 respectively. The existing Plus V3 System Tools disk reached Finder in both builds, and IWM boot, write and format passed in both. The development UART test covers window bounds, odd-byte and wide accesses, RX, reset, binary UART-B output, and sink attach/detach. | `plus-rom-0002/rework/development-ctest.log`, `plus-rom-0002/rework/production-ctest.log`, each build's `Testing/Temporary/LastTest.log` |
| Production address map and build contents | PASS. The bus test observed unmapped responses for all 32 bytes, ignored UART register writes and the original unmapped word diagnostic. `ar` and `nm` verified that the model object and UART symbols were absent. The previous PLUS-ROM-0001 reset image also booted in the production emulator via `--raw-rom`. | `plus-rom-0002/rework/production-map.log`, `plus-rom-0002/rework/production-symbols.log`, `plus-rom-0002/rework/production-raw-rom.log` |
| Legacy Make build and configuration switch | PASS. Default Make build emitted the disk guest UART line. Switching `1 → 0 → 1` in the same build tree removed the DUART symbols and restored successful guest output on re-enable. | `plus-rom-0002/rework/legacy-development.log`, `plus-rom-0002/rework/legacy-switches.log` |
| vport State 1 and HFS converter | PASS. ELF/V68B checks and all four converter tests passed. The original Plus V3 ROM and supplied System Tools image retained their starting hashes. | `plus-rom-0002/rework/vport-state1.log`, `plus-rom-0002/rework/hfs-converter.log`, `plus-rom-0002/rework/media-before.sha256` |

Reproduce both CTest configurations from the repository root using the local Plus V3 ROM, System Tools disk and installed assembler.

```sh
for duart in ON OFF; do
  build_dir="devel/tima_rom/build/plus-rom-0002/recheck-$duart"
  cmake -S devel/umac -B "$build_dir" -DUMAC_DEVELOPMENT_DUART="$duart" \
    -DUMAC_TEST_PLUS_ROM="$PWD/devel/umac/rom.bin" \
    -DUMAC_TEST_PLUS_FLOPPY="$PWD/devel/umac/plus-system.image" \
    -DUMAC_TEST_VASM=/home/marcio/Projects/languages/vbcc_pack/vport/build/host/bin/vasmm68k_std
  cmake --build "$build_dir" --parallel 2
  ctest --test-dir "$build_dir" --output-on-failure
done
```

**Current Work**

| State | Work / acceptance evidence | Responsible | Next action / reference |
| --- | --- | --- | --- |
| DONE | Register the requested task and proposed verification | Reporter (Codex) | Master index and this record |
| DONE | Port the peripheral and connect UART-B to host stdout | Assignee (Codex) | uMac raw ROM path and `m68681_debug_uart` |
| DONE | Build and run the ROM with its own `printf` | Assignee (Codex) | Tima Boot smoke test and fixture evidence |
| DONE | Run emulator and toolchain regression suites | Assignee (Codex) | Verification results above |
| DONE | Record the implementation commit | Assignee (Codex) | `9915021` in the Commits table |
| DONE | Decouple MC68681 from `--raw-rom` and verify the development and production configurations | Assignee (Codex) | R1 and rework verification above |
| DONE | Record the rework commit | Assignee (Codex) | `5eb2d3a` in the Commits table |
| PENDING | Final review and approval once rework is evidenced | Reviewer (project owner) | Do not resolve or close until confirmed |

**Notes**

- **2026-11-22 — REPORTER (Codex) — Discovery:** Inspected the requested model, the uMac byte/word/long dispatch, and the freestanding ROM build and RAM-marker test. The UART-B route is missing and the configured compiler path does not resolve in this workspace.
- **2026-11-22 — REPORTER (Project owner) — Assignment:** Requested both the ticket and its implementation on `pre-release-rom-test`, with the `0xC00000–0xC0001F` debug window, project-local `printf`, the installed `vbcc_pack` tools, and retained test evidence.
- **2026-11-22 — ASSIGNEE (Codex) — Status Change:** Began implementation on the requested branch after the reporter record was committed as `51c4341`. The SDL frontend will attach the console for `--raw-rom`, and each UART-B byte will be consumed after a CPU slice so polling guest firmware can continue without waiting for a display frame.
- **2026-11-22 — ASSIGNEE (Codex) — Baseline Test:** The uMac build from `5489ed7` passed all 17 CTests with the local Plus V3 ROM and DiskCopy boot disk. The installed vport tools passed their State 1 suite.
- **2026-11-22 — ASSIGNEE (Codex) — Implementation:** Ported the two-channel register model into `devel/umac/src` with direct byte decoding and reset. The SDL frontend attaches stdout for raw-ROM sessions. The ROM configures the UART-B registers for 9600 baud and 8N1, and its local formatter polls transmit-ready at `0xC00013` and writes the data at `0xC00017`. [Motorola MC68681 register formats, Table 4-5](https://bitsavers.trailing-edge.com/components/motorola/68000/MC68681_Dual_Asynchronous_Receiver_Transmitter_DUART_Sep85.pdf)
- **2026-11-22 — ASSIGNEE (Codex) — Rework:** An internal-linkage `boot_count` trial produced an unresolved local `.comm` symbol in the installed assembler/linker path. Restored the existing external boot-variable linkage and verified the `.bss` address, copy/clear behavior and clean freestanding link with the installed binaries.
- **2026-11-22 — ASSIGNEE (Codex) — Verification:** The ROM emits the formatted startup values and `ROM_TEST_PASS` over UART-B. Verified live capture, clean shutdown, validation rejections, the two guest failure messages, formatter boundaries and poisoned-RAM startup. Both uMac build paths passed the ROM smoke test. The previous ROM also passed its acceptance test with the updated uMac. All 18 uMac CTests and the vport State 1 suite passed. Commands and captures are listed above.
- **2026-11-22 — ASSIGNEE (Codex) — Status Change:** Moved the ticket to `REVIEW` for the project owner. Verification covers the polled emulator console and the ROM build and boot path.
- **2026-11-22 — ASSIGNEE (Codex) — Local Commit:** Recorded the implementation, tests and evidence as `9915021c246a7d6563d61c2f42fa3280f81dcd58` on `pre-release-rom-test`. The index and ticket match on status, title and kind. Project-owner review is pending.
- **2026-11-22 — REVIEWER (Project owner) — Change requested:** MC68681 must be available for **any guest ROM** in a development emulator, not only `--raw-rom`; compile it out/disable it in final production configurations because original M0001AP hardware lacks it. No socket implementation is requested. The reviewer directly updated both affected READMEs (`6cdd763`, `c4eb7aa`). Ticket stays `REVIEW` until implementation, tests, and owner approval.
- **2026-11-22 — ASSIGNEE (Codex) — Status Change:** Started R1 rework from review commit `0f14e2c` on `pre-release-rom-test`. Use one build-time setting in CMake and Make, with the DUART present by default in development and excluded in the production configuration. Verify actual guest MMIO with code booted by the original Plus V3 ROM from an 800K test floppy. Restore this ticket's row to the `PLUS-ROM` section and update its status to `IN_PROGRESS`.
- **2026-11-22 — ASSIGNEE (Codex) — Rework:** Enabled DUART decode and reset at build time and attached SDL's stdout in either ROM path. Made sink attachment independent of register state. The production option excludes the model and all core integration. The raw ROM smoke target selects the development build explicitly. Updated both READMEs with the options and guest test boundary.
- **2026-11-22 — ASSIGNEE (Codex) — Test Harness Correction:** The first development CTest run rejected the probe at assembly time. Used the existing project's vasm register syntax option (`-elfregs`) and an explicit boot-header pad, then confirmed the assembled eight-byte header before rerunning. The first result is retained in `plus-rom-0002/rework/development-ctest-first.log` under the test build directory.
- **2026-11-22 — ASSIGNEE (Codex) — Verification:** The same 800K disk fixture reached the UART register probe under the original Plus V3 ROM in both revisions. The review baseline produced the unmapped-byte diagnostic. The reworked emulator accepted the register accesses and printed `MAC_PLUS_DUART_PASS`. The alternate reset ROM printed `ROM_TEST_PASS`. Development passed all 19 CTests, production passed all 18, and the legacy Make switch passed. The production bus and symbol checks verified the compiled-out window. Captures and commands are in rework verification above.
- **2026-11-22 — ASSIGNEE (Codex) — Status Change:** Marked R1 verified and returned the ticket to `REVIEW` with the owner as reviewer.
- **2026-11-22 — ASSIGNEE (Codex) — Local Commit:** Recorded the rework, tests and verification as `5eb2d3a51dcf6d53df4fa3eed0be38afb13fa188` on `pre-release-rom-test`. Implementation review is pending.

**Proposed Change**

**Original implementation scope, now superseded on device availability by the reviewer comment above:** Port the MC68681 register model to `devel/umac/src`. Enable the extension for standalone `--raw-rom` sessions, decode the low byte lane in the exact 32-byte window, and flush bytes transmitted by UART-B to stdout. Use a freestanding ROM-local integer/string `printf`, with the ROM polling UART-B transmit-ready before sending each byte. Keep the toolchain override and set the default to the installed vport tools.

Keep the ROM's reset, `.data` and `.bss` checks. Replace the RAM mailbox with serial status lines and have the headless test verify the exact formatted success line while uMac runs and after clean shutdown. Keep the checksum and short-ROM rejection checks. Exercise the DUART window and reset behavior and run the uMac and vport regression suites.

**Reviewer-directed rework:** Make `UMAC_DEVELOPMENT_DUART` control device availability in both builds and have SDL attach UART-B with either ROM. Verify the map and output with a small guest probe loaded by an unmodified Plus V3 ROM from a generated 800K floppy. Test the original map in the production build independently of the raw-ROM selection policy.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |
| `pre-release-rom-test` | `9915021` | `CHANGES_REQUESTED` | Port MC68681, route UART-B to stdout, and verify standalone ROM output using the local vbcc tools. |
| `pre-release-rom-test` | `5eb2d3a` | `UNREVIEWED` | Enable the development DUART with either ROM, compile it out for production, and verify both configurations. |

**Resolution Description**

Pending.
