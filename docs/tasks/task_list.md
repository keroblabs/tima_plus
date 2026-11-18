# Tima/Plus — Master Task and Bug List

**Status:** Initial register — 18 November 2026  
**Process:** [Bug-tracking methodology](bug-tracking-methodology.md)  
**Detailed records:** [db/](db/) — **one Markdown file per ticket**  
**Programme plan:** [Development roadmap](../01-development-roadmap.md)

This is the **single index for all formally opened Tima/Plus tasks, defects and improvements**. Each entry is grouped by category, permanently identified, and linked to its detailed record under `db/`. All technical scope, evidence, decisions, chronological notes, commit review and resolution history live in the individual record, not here.

**Current totals:** 1 registered / 1 OPEN / 0 IN_PROGRESS / 0 REVIEW / 0 RESOLVED / 0 CLOSED / 0 DEFERRED.

## PLUS-DEV — Programme milestones and cross-track work

No tickets formally opened under this category.

## PLUS-EMU — uMac, Macintosh hardware and development emulator

| ID | Kind | Status | Title | Milestone | Ticket |
| --- | --- | --- | --- | --- | --- |
| [`PLUS-EMU-0004`](db/PLUS-EMU-0004.md) | `BUG` | `OPEN` | Implement Macintosh Plus NCR 5380 SCSI controller | [PLUS-DEV-0009](../01-development-roadmap.md#plus-dev-0009--migrate-to-scsibluescsi) | [Details and history](db/PLUS-EMU-0004.md) |

## PLUS-SYS — Tima/Plus boot, storage, OS and desktop

No tickets formally opened under this category.

## Relationship to the roadmap

Existing roadmap identifiers (`PLUS-DEV-0001`–`0010`, `PLUS-EMU-0001`–`0008`, `PLUS-SYS-0001`–`0007`) are **reserved programme/task identifiers**, not automatically opened tickets. Add each to this list with its own file only when formally raised. Do not create placeholder files.

`PLUS-EMU-0004` is the first formally opened record at the user's request. It covers the controller only. The separately planned `PLUS-EMU-0005` concerns the virtual disk/BlueSCSI image and boot, and `PLUS-SYS-0006` the Tima/Plus SCSI system integration. Opening `PLUS-EMU-0004` does **not** itself change the [floppy-first milestone order](../01-development-roadmap.md).
