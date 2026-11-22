# PLUS-ROM-0002 — Print standalone ROM diagnostics over MC68681 UART-B

**ID:** `PLUS-ROM-0002`

**Status:** `IN_PROGRESS`

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
- Scope covers the register model and polled serial output in uMac. The standalone mode decodes the DUART at `0xC00000–0xC0001F`. Odd byte accesses select registers. Even byte and wide reads return all ones, and their writes have no side effects. The standard Plus V3 mode uses the original M0001AP map. uMac drains both transmitters at CPU slice boundaries and flushes UART-B to stdout.

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
| R1 | `CORRECTION_REQUIRED` | `9915021` ties DUART decode to the stdout sink and opens that sink only for `--raw-rom`. | Select the development DUART at build time. Prove guest UART-B output in both ROM modes and the original address map in a disabled build. | IN PROGRESS |

### Verification

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

**Current Work**

| State | Work / acceptance evidence | Responsible | Next action / reference |
| --- | --- | --- | --- |
| DONE | Register the requested task and proposed verification | Reporter (Codex) | Master index and this record |
| DONE | Port the peripheral and connect UART-B to host stdout | Assignee (Codex) | uMac raw ROM path and `m68681_debug_uart` |
| DONE | Build and run the ROM with its own `printf` | Assignee (Codex) | Tima Boot smoke test and fixture evidence |
| DONE | Run emulator and toolchain regression suites | Assignee (Codex) | Verification results above |
| DONE | Record the implementation commit | Assignee (Codex) | `9915021` in the Commits table |
| PENDING | Decouple MC68681 from `--raw-rom` and verify the development and production configurations | Assignee (Codex) | R1 and a guest probe booted from an 800K floppy |
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

**Proposed Change**

**Original implementation scope, now superseded on device availability by the reviewer comment above:** Port the MC68681 register model to `devel/umac/src`. Enable the extension for standalone `--raw-rom` sessions, decode the low byte lane in the exact 32-byte window, and flush bytes transmitted by UART-B to stdout. Use a freestanding ROM-local integer/string `printf`, with the ROM polling UART-B transmit-ready before sending each byte. Keep the toolchain override and set the default to the installed vport tools.

Keep the ROM's reset, `.data` and `.bss` checks. Replace the RAM mailbox with serial status lines and have the headless test verify the exact formatted success line while uMac runs and after clean shutdown. Keep the checksum and short-ROM rejection checks. Exercise the DUART window and reset behavior and run the uMac and vport regression suites.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |
| `pre-release-rom-test` | `9915021` | `CHANGES_REQUESTED` | Port MC68681, route UART-B to stdout, and verify standalone ROM output using the local vbcc tools. |

**Resolution Description**

Pending.
