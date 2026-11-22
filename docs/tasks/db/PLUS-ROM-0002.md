# PLUS-ROM-0002 — Print standalone ROM diagnostics over MC68681 UART-B

**ID:** `PLUS-ROM-0002`

**Status:** `OPEN`

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
- [Tima Boot](../../../devel/tima_rom/) builds a freestanding 68000 ROM. Its current toolchain default points to a sibling directory that does not exist in this checkout. The installed host tools are under `/home/marcio/Projects/languages/vbcc_pack/vport/build/host/bin`.
- Scope covers the register model and polled serial output in the emulator. Physical board wiring, serial bit timing and a DUART connection to the Macintosh interrupt controller are outside this task.

**Current Work**

| State | Work / acceptance evidence | Responsible | Next action / reference |
| --- | --- | --- | --- |
| DONE | Register the requested task and proposed verification | Reporter (Codex) | Master index and this record |
| PENDING | Port the peripheral and connect UART-B to host stdout | Assignee (Codex) | uMac raw ROM path |
| PENDING | Build and run the ROM with its own `printf` | Assignee (Codex) | Tima Boot smoke test |
| PENDING | Run regression tests and record local commits | Assignee (Codex) | Build and test logs |
| PENDING | Review the implementation and evidence | Reviewer (project owner) | Local handoff |

**Notes**

- **2026-11-22 — REPORTER (Codex) — Discovery:** Inspected the requested model, the uMac byte/word/long dispatch, and the freestanding ROM build and RAM-marker test. The UART-B route is missing and the configured compiler path does not resolve in this workspace.
- **2026-11-22 — REPORTER (Project owner) — Assignment:** Requested both the ticket and its implementation on `pre-release-rom-test`, with the `0xC00000–0xC0001F` debug window, project-local `printf`, the installed `vbcc_pack` tools, and retained test evidence.

**Proposed Change**

Port the MC68681 register model to `devel/umac/src`. Enable the extension for standalone `--raw-rom` sessions, decode the low byte lane in the exact 32-byte window, and flush bytes transmitted by UART-B to stdout. Use a freestanding ROM-local integer/string `printf`, with the ROM polling UART-B transmit-ready before sending each byte. Keep the toolchain override and set the default to the installed vport tools.

Keep the ROM's reset, `.data` and `.bss` checks. Replace the RAM mailbox with serial status lines and have the headless test verify the exact formatted success line while uMac runs and after clean shutdown. Keep the checksum and short-ROM rejection checks. Exercise the DUART window and reset behavior and run the uMac and vport regression suites.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |

**Resolution Description**

Pending.
