# Tima/Plus — Master Task and Bug List

**Status:** SCSI, RTC and read-only IWM merged and closed; write-phase PLUS-EMU-0010 remains open, 20 November 2026.

**Process:** [Bug-tracking methodology](bug-tracking-methodology.md)  
**Detailed records:** [db/](db/) — **one Markdown file per ticket**  
**Programme plan:** [Development roadmap](../01-development-roadmap.md)

This is the **single index for all formally opened Tima/Plus tasks, defects and improvements**. Each entry is grouped by category, permanently identified, and linked to its detailed record under `db/`. All technical scope, evidence, decisions, chronological notes, commit review and resolution history live in the individual record, not here.

**Current totals:** 5 registered / 1 OPEN / 0 IN_PROGRESS / 0 REVIEW / 0 RESOLVED / 4 CLOSED / 0 DEFERRED.

## PLUS-DEV — Programme milestones and cross-track work

No tickets formally opened under this category.

## PLUS-EMU — uMac, Macintosh hardware and development emulator

| ID | Kind | Status | Title | Milestone | Ticket |
| --- | --- | --- | --- | --- | --- |
| [`PLUS-EMU-0003`](db/PLUS-EMU-0003.md) | `TASK` | `CLOSED` | Implement unmodified-ROM read-only IWM and virtual floppy | [PLUS-DEV-0003](../01-development-roadmap.md#plus-dev-0003--run-the-unmodified-macintosh-plus-rom) | [Details and history](db/PLUS-EMU-0003.md) |
| [`PLUS-EMU-0004`](db/PLUS-EMU-0004.md) | `BUG` | `CLOSED` | Implement Macintosh Plus NCR 5380 SCSI controller | [PLUS-DEV-0009](../01-development-roadmap.md#plus-dev-0009--migrate-to-scsibluescsi) | [Details and history](db/PLUS-EMU-0004.md) |
| [`PLUS-EMU-0005`](db/PLUS-EMU-0005.md) | `TASK` | `CLOSED` | Emulate a SCSI hard disk backed by a BlueSCSI-compatible image | [PLUS-DEV-0009](../01-development-roadmap.md#plus-dev-0009--migrate-to-scsibluescsi) | [Details and history](db/PLUS-EMU-0005.md) |
| [`PLUS-EMU-0009`](db/PLUS-EMU-0009.md) | `BUG` | `CLOSED` | Implement Macintosh Plus serial RTC and battery-backed PRAM | [PLUS-DEV-0002](../01-development-roadmap.md#plus-dev-0002--establish-the-m0001ap-hardware-profile) | [Details and history](db/PLUS-EMU-0009.md) |
| [`PLUS-EMU-0010`](db/PLUS-EMU-0010.md) | `TASK` | `OPEN` | Add IWM write handling and persistent virtual floppy writes | [PLUS-DEV-0004](../01-development-roadmap.md#plus-dev-0004--complete-the-virtual-800k-floppy-subsystem) | [Details and history](db/PLUS-EMU-0010.md) |

## PLUS-SYS — Tima/Plus boot, storage, OS and desktop

No tickets formally opened under this category.

## Relationship to the roadmap

Existing roadmap identifiers (`PLUS-DEV-0001`–`0010`, `PLUS-EMU-0001`–`0008`, `PLUS-SYS-0001`–`0007`) are **reserved programme/task identifiers**, not automatically opened tickets. Add each to this list with its own file only when formally raised. Do not create placeholder files.

`PLUS-EMU-0004` was the first opened record and covers the controller only. `PLUS-EMU-0005` covers a separate emulated SCSI hard-disk target and opaque BlueSCSI-compatible whole-device image. Both are **CLOSED** following approved engineering review, project-owner manual verification of a generated SCSI hard-drive image booting under uMac with floppy support, and the owner's mandatory final review and merge of [PR #1](https://github.com/keroblabs/tima_plus/pull/1) into `main` (`e568e6d`). `PLUS-SYS-0006` retains the Tima/Plus SCSI system driver, image preparation and boot work. Opening either ticket does **not** change the [floppy-first milestone order](../01-development-roadmap.md).

`PLUS-EMU-0009` covers the serial RTC and 256-byte battery-backed PRAM hardware under `PLUS-DEV-0002`; its implementation and review findings are accepted, and it is **CLOSED** after the owner's mandatory post-merge verification of [PR #2](https://github.com/keroblabs/tima_plus/pull/2) on `main` ([merge `a772bea`](https://github.com/keroblabs/tima_plus/commit/a772bea95d08c9e14cdd5a40fa6afb087bff97bf)).

[PR #3](https://github.com/keroblabs/tima_plus/pull/3), merged at [`b6ca242`](https://github.com/keroblabs/tima_plus/commit/b6ca242ae8efc5bc552b2491df7ad8ec0375f8a7), completed the read-only original-ROM IWM work: `PLUS-EMU-0003` is now **CLOSED** following the project-owner review, with Sony motor correction and expected Finder eject/restart behaviour confirmed. This does **not** close the separate write-phase ticket.

**Dedicated Phase-2 developer plan:** [05-iwm-write-implementation.md](../05-iwm-write-implementation.md) provides proposed IWM write handshake, circular bit update, inverse GCR/sector tags, DC42 persistence, ROM format and testing decisions. [PLUS-EMU-0010](db/PLUS-EMU-0010.md) is still `OPEN`, not completed by documentation.

**IWM/floppy phased work:** [PLUS-EMU-0003](db/PLUS-EMU-0003.md) establishes the unmodified-ROM, read-only IWM and encoded circular disk buffer **with write stubbed**; [PLUS-EMU-0010](db/PLUS-EMU-0010.md) later adds timed writes, dirty-track decode/flush and `.dsk` persistence. Both follow [04-iwm-emulation.md](../04-iwm-emulation.md). Implementation changes require **local `git commit`, not developer `git push`**, under the updated methodology.
