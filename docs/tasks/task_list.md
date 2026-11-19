# Tima/Plus — Master Task and Bug List

**Status:** SCSI integrated; RTC emulation ticket opened — 19 November 2026\
**Process:** [Bug-tracking methodology](bug-tracking-methodology.md)  
**Detailed records:** [db/](db/) — **one Markdown file per ticket**  
**Programme plan:** [Development roadmap](../01-development-roadmap.md)

This is the **single index for all formally opened Tima/Plus tasks, defects and improvements**. Each entry is grouped by category, permanently identified, and linked to its detailed record under `db/`. All technical scope, evidence, decisions, chronological notes, commit review and resolution history live in the individual record, not here.

**Current totals:** 3 registered / 1 OPEN / 0 IN_PROGRESS / 0 REVIEW / 0 RESOLVED / 2 CLOSED / 0 DEFERRED.

## PLUS-DEV — Programme milestones and cross-track work

No tickets formally opened under this category.

## PLUS-EMU — uMac, Macintosh hardware and development emulator

| ID | Kind | Status | Title | Milestone | Ticket |
| --- | --- | --- | --- | --- | --- |
| [`PLUS-EMU-0004`](db/PLUS-EMU-0004.md) | `BUG` | `CLOSED` | Implement Macintosh Plus NCR 5380 SCSI controller | [PLUS-DEV-0009](../01-development-roadmap.md#plus-dev-0009--migrate-to-scsibluescsi) | [Details and history](db/PLUS-EMU-0004.md) |
| [`PLUS-EMU-0005`](db/PLUS-EMU-0005.md) | `TASK` | `CLOSED` | Emulate a SCSI hard disk backed by a BlueSCSI-compatible image | [PLUS-DEV-0009](../01-development-roadmap.md#plus-dev-0009--migrate-to-scsibluescsi) | [Details and history](db/PLUS-EMU-0005.md) |
| [`PLUS-EMU-0009`](db/PLUS-EMU-0009.md) | `BUG` | `OPEN` | Implement Macintosh Plus serial RTC and battery-backed PRAM | [PLUS-DEV-0002](../01-development-roadmap.md#plus-dev-0002--establish-the-m0001ap-hardware-profile) | [Details and history](db/PLUS-EMU-0009.md) |

## PLUS-SYS — Tima/Plus boot, storage, OS and desktop

No tickets formally opened under this category.

## Relationship to the roadmap

Existing roadmap identifiers (`PLUS-DEV-0001`–`0010`, `PLUS-EMU-0001`–`0008`, `PLUS-SYS-0001`–`0007`) are **reserved programme/task identifiers**, not automatically opened tickets. Add each to this list with its own file only when formally raised. Do not create placeholder files.

`PLUS-EMU-0004` was the first opened record and covers the controller only. `PLUS-EMU-0005` covers a separate emulated SCSI hard-disk target and opaque BlueSCSI-compatible whole-device image. Both are **CLOSED** following approved engineering review, project-owner manual verification of a generated SCSI hard-drive image booting under uMac with floppy support, and the owner's mandatory final review and merge of [PR #1](https://github.com/keroblabs/tima_plus/pull/1) into `main` (`e568e6d`). `PLUS-SYS-0006` retains the Tima/Plus SCSI system driver, image preparation and boot work. Opening either ticket does **not** change the [floppy-first milestone order](../01-development-roadmap.md).

`PLUS-EMU-0009` records the missing serial RTC and 256-byte battery-backed PRAM hardware under the already-planned `PLUS-DEV-0002` profile; it does not imply approval to implement or change the milestone order.
