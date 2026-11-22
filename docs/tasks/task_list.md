# Tima/Plus — Master Task and Bug List

**Status:** SCSI, RTC and read-only IWM are closed. Writable IWM and the standalone ROM tickets are in review as of 22 November 2026.

**Process:** [Bug-tracking methodology](bug-tracking-methodology.md)  
**Detailed records:** [db/](db/) — **one Markdown file per ticket**  
**Programme plan:** [Development roadmap](../01-development-roadmap.md)

This is the **single index for all formally opened Tima/Plus tasks, defects and improvements**. Each entry is grouped by category, permanently identified, and linked to its detailed record under `db/`. All technical scope, evidence, decisions, chronological notes, commit review and resolution history live in the individual record, not here.

**Current Tima/Plus totals:** 7 registered / 0 OPEN / 0 IN_PROGRESS / 3 REVIEW / 0 RESOLVED / 4 CLOSED / 0 DEFERRED.

## PLUS-DEV — Programme milestones and cross-track work

No tickets formally opened under this category.

## PLUS-EMU — uMac, Macintosh hardware and development emulator

| ID | Kind | Status | Title | Milestone | Ticket |
| --- | --- | --- | --- | --- | --- |
| [`PLUS-EMU-0003`](db/PLUS-EMU-0003.md) | `TASK` | `CLOSED` | Implement unmodified-ROM read-only IWM and virtual floppy | [PLUS-DEV-0003](../01-development-roadmap.md#plus-dev-0003--run-the-unmodified-macintosh-plus-rom) | [Details and history](db/PLUS-EMU-0003.md) |
| [`PLUS-EMU-0004`](db/PLUS-EMU-0004.md) | `BUG` | `CLOSED` | Implement Macintosh Plus NCR 5380 SCSI controller | [PLUS-DEV-0009](../01-development-roadmap.md#plus-dev-0009--migrate-to-scsibluescsi) | [Details and history](db/PLUS-EMU-0004.md) |
| [`PLUS-EMU-0005`](db/PLUS-EMU-0005.md) | `TASK` | `CLOSED` | Emulate a SCSI hard disk backed by a BlueSCSI-compatible image | [PLUS-DEV-0009](../01-development-roadmap.md#plus-dev-0009--migrate-to-scsibluescsi) | [Details and history](db/PLUS-EMU-0005.md) |
| [`PLUS-EMU-0009`](db/PLUS-EMU-0009.md) | `BUG` | `CLOSED` | Implement Macintosh Plus serial RTC and battery-backed PRAM | [PLUS-DEV-0002](../01-development-roadmap.md#plus-dev-0002--establish-the-m0001ap-hardware-profile) | [Details and history](db/PLUS-EMU-0009.md) |
| [`PLUS-EMU-0010`](db/PLUS-EMU-0010.md) | `TASK` | `REVIEW` | Add IWM write handling and persistent virtual floppy writes | [PLUS-DEV-0004](../01-development-roadmap.md#plus-dev-0004--complete-the-virtual-800k-floppy-subsystem) | [Details and history](db/PLUS-EMU-0010.md) |

## PLUS-ROM — Standalone firmware tests on the M0001AP hardware model

| ID | Kind | Status | Title | Milestone | Ticket |
| --- | --- | --- | --- | --- | --- |
| [`PLUS-ROM-0001`](db/PLUS-ROM-0001.md) | `TASK` | `REVIEW` | Boot a standalone test ROM and record its RAM marker | [PLUS-DEV-0002 hardware profile](../01-development-roadmap.md#plus-dev-0002--establish-the-m0001ap-hardware-profile) | [Details and history](db/PLUS-ROM-0001.md) |
| [`PLUS-ROM-0002`](db/PLUS-ROM-0002.md) | `TASK` | `REVIEW` | Print standalone ROM diagnostics over MC68681 UART-B | [PLUS-DEV-0002 hardware profile](../01-development-roadmap.md#plus-dev-0002--establish-the-m0001ap-hardware-profile) | [Details and history](db/PLUS-ROM-0002.md) |

## CPLD-DOC — Documentation and engineering methodology

| ID | Kind | Status | Title | Milestone | Ticket |
| --- | --- | --- | --- | --- | --- |
| `CPLD-DOC-0001` | IMPROVEMENT | RESOLVED | Add stakeholder attribution and current-work summary to tickets | Engineering workflow | [Details](db/CPLD-DOC-0001.md) |

## PLUS-SYS — Tima/Plus boot, storage, OS and desktop

No tickets formally opened under this category.

## Relationship to the roadmap

Existing roadmap identifiers (`PLUS-DEV-0001`–`0011`, `PLUS-EMU-0001`–`0008`, `PLUS-SYS-0001`–`0007`) are **reserved programme/task identifiers**, not automatically opened tickets. `PLUS-DEV-0011` was added on 20 November 2026 as the **next STM32 production firmware / Unicorn integration milestone**, separate from the now implemented virtual-IWM `PLUS-DEV-0004`. Add each to this list with its own file only when formally raised. Do not create placeholder files.

`PLUS-EMU-0004` was the first opened record and covers the controller only. `PLUS-EMU-0005` covers a separate emulated SCSI hard-disk target and opaque BlueSCSI-compatible whole-device image. Both are **CLOSED** following approved engineering review, project-owner manual verification of a generated SCSI hard-drive image booting under uMac with floppy support, and the owner's mandatory final review and merge of [PR #1](https://github.com/keroblabs/tima_plus/pull/1) into `main` (`e568e6d`). `PLUS-SYS-0006` retains the Tima/Plus SCSI system driver, image preparation and boot work. Opening either ticket does **not** change the [floppy-first milestone order](../01-development-roadmap.md).

`PLUS-EMU-0009` covers the serial RTC and 256-byte battery-backed PRAM hardware under `PLUS-DEV-0002`; its implementation and review findings are accepted, and it is **CLOSED** after the owner's mandatory post-merge verification of [PR #2](https://github.com/keroblabs/tima_plus/pull/2) on `main` ([merge `a772bea`](https://github.com/keroblabs/tima_plus/commit/a772bea95d08c9e14cdd5a40fa6afb087bff97bf)).

[PR #3](https://github.com/keroblabs/tima_plus/pull/3), merged at [`b6ca242`](https://github.com/keroblabs/tima_plus/commit/b6ca242ae8efc5bc552b2491df7ad8ec0375f8a7), completed the read-only original-ROM IWM work: `PLUS-EMU-0003` is now **CLOSED** following the project-owner review, with Sony motor correction and expected Finder eject/restart behaviour confirmed. This does **not** close the separate write-phase ticket.

**Phase-1 uMac release boundary:** [Development Roadmap](../01-development-roadmap.md#phase-1-release-checkpoint--working-umac--original-macintosh-plus) records the implemented original-ROM Macintosh Plus reference on `pre-release-umac`; **PLUS-EMU-0010 remains `REVIEW`** pending owner manual IWM write acceptance and owner-controlled PR/merge, so this index does not mark it closed. The next planned work, when resumed, is `PLUS-DEV-0011`/reserved `PLUS-EMU-0007` STM32 firmware execution under Unicorn.

**Dedicated IWM write developer plan:** [05-iwm-write-implementation.md](../05-iwm-write-implementation.md) provides the approved IWM write architecture and acceptance tests. [PLUS-EMU-0010](db/PLUS-EMU-0010.md) records implementation progress and evidence.

**IWM/floppy phased work:** [PLUS-EMU-0003](db/PLUS-EMU-0003.md) establishes the unmodified-ROM, read-only IWM and encoded circular disk buffer **with write stubbed**; [PLUS-EMU-0010](db/PLUS-EMU-0010.md) later adds timed writes, dirty-track decode/flush and `.dsk` persistence. Both follow [04-iwm-emulation.md](../04-iwm-emulation.md). Implementation changes require **local `git commit`, not developer `git push`**, under the updated methodology.
