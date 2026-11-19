# Tima/Plus — Task and Bug Tracking Methodology

**Document status:** Working process, initial issue-register version (18 November 2026)
**Applies to:** All Tima/Plus implementation tasks, defects, improvements, reviews, and resolutions
**Location:** `docs/tasks/bug-tracking-methodology.md`
**Source:** Copied and adapted from [Tima bug-tracking-methodology.md — phase-1-improvements](https://github.com/keroblabs/tima/blob/phase-1-improvements/docs/bugs/bug-tracking-methodology.md), source blob `7d68621b656774aec4e54d97d196cd85178332db`.

## 1. Purpose and guiding principles

This document defines how we identify, describe, discuss, fix, review, and close defects and engineering improvements in Tima/Plus. The tracker is part of the repository and is the durable record of decisions, not merely a to-do list.

- **Review before change.** A finding is recorded before implementation, and its proposed remedy is not automatically approved.
- **Preserve history.** Historical snapshots and reference branches remain unchanged; fixes and improvements belong on an explicitly designated working branch. Never rewrite a preserved baseline as part of issue resolution.
- **One issue, one identifiable concern.** Related issues may be linked, but distinct causes or acceptance criteria should not be hidden in a single ticket.
- **Keep records consistent.** Every ticket has the same required fields and is indexed in one master list.
- **One ticket per file.** Every ticket, of any length, has a dedicated `docs/tasks/db/<ID>.md` file; no category files or numbered 16-ticket volumes.
- **One master list.** `docs/tasks/task_list.md` is the single index, grouped by category. Its rows link to individual ticket files.
- **Record decisions and rework.** Notes form a chronological, append-only engineering history, including discarded proposals, review feedback, and reopened issues.
- **Distinguish evidence from hypotheses.** A suspected defect is permissible, but must be labelled as unverified until confirmed by analysis or tests.
- **Track implementation commits.** Each issue retains a commit list with branch, short hash, review status, and concise change description.
- **No silent closure.** A resolved issue must say what changed, how it was verified, and who approved the conclusion.

## 2. Files and ticket ownership

```text
docs/tasks/
├── bug-tracking-methodology.md     # Method, lifecycle, review rules
├── task_list.md                    # One master list: category, ID, kind, status, title
└── db/
    ├── PLUS-EMU-0004.md            # Exactly one complete record
    └── PLUS-<AREA>-NNNN.md         # One record per later opened ticket
```

**`task_list.md` is the only summary/index.** Group by category and sort by permanent ID. Each row must identify `ID`, `Kind`, `Status`, `Title`, `Milestone`, and link directly to its canonical ticket file. The index must never duplicate full technical descriptions.

**`db/<ID>.md` is the authoritative record.** Every file contains exactly one ticket, its detailed design/evidence, chronological Notes, Commits and Resolution Description. No 16-ticket limits, no split category volumes, and no second index files. Very detailed tickets are expected.

**Existing ID allocation:** The Tima/Plus [roadmap](../01-development-roadmap.md) defines `PLUS-DEV` (end-to-end programme), `PLUS-EMU` (uMac/Musashi, Macintosh hardware, Unicorn and debugging), and `PLUS-SYS` (OS, bootstrap, programmer, SDK and desktop). Preserve the IDs already assigned there. A roadmap mention is a **planned item**, not an opened ticket; create its individual record and master-index row simultaneously when formally raised.

**Kind:** `BUG` (confirmed or suspected defect, including a missing mandatory hardware capability), `TASK` (bounded planned implementation), or `IMPROVEMENT` (optional refinement). Label unverified claims; absence of a feature is not automatically an upstream regression.

Each ticket belongs to one primary category. Related concerns should link to each other, not be duplicated. Ticket creation does not authorize implementation or change the programme schedule. Preserve historical source branches and implementation review trails.

## 3. Ticket identification

IDs have the form `PLUS-<AREA>-NNNN` (e.g. `PLUS-EMU-0004`, `PLUS-DEV-0001`, `PLUS-SYS-0006`).

- Numbers and existing roadmap IDs are permanent; never reuse, rename, recycle or renumber them. New numbers increase within each category without reset.
- Each newly opened ticket gets exactly one canonical `docs/tasks/db/<ID>.md` file, and exactly one row under its category in `task_list.md`.
- Changing a title/status/kind/category does not change the ID or the canonical filename. Update the master row in the same commit, recording any reclassification in Notes.
- Commit messages, reviews, testing evidence and cross-ticket references should use the permanent ID.
- The roadmap is the place for programme sequencing; the tracker is the place for active issue decisions and verified dispositions.

## 4. Status model

| Status | Meaning |
| --- | --- |
| `OPEN` | Recorded for assessment. The cause or proposed fix may still be unverified. |
| `IN_PROGRESS` | The agreed investigation or implementation is actively being carried out. |
| `REVIEW` | A proposed implementation or conclusion is ready for examination and testing evidence is recorded. |
| `RESOLVED` | The outcome has been reviewed and accepted, with the resolution recorded; integration or final housekeeping may remain. |
| `CLOSED` | The accepted outcome is integrated into the designated working branch, or a documented no-change disposition has been completed. |
| `DEFERRED` | Deliberately postponed, with a reason and a condition for reconsideration. |

Normal flow: `OPEN → IN_PROGRESS → REVIEW → RESOLVED → CLOSED`.

Investigation may return from `IN_PROGRESS` or `REVIEW` to `OPEN` or `IN_PROGRESS` when the hypothesis or proposed change needs revision. A `DEFERRED`, `RESOLVED`, or `CLOSED` issue may be reopened by returning it to `OPEN`, with an explicit dated note explaining why. No special “reopened” status is needed: the note preserves the history.

Issues may be closed without code changes only when the disposition (for example, duplicate, not reproducible, or intentionally retained behavior) is documented and approved. Record the rationale in Resolution Description; do not delete the ticket.

## 5. Required structure of the master list and each ticket

### Master index — `docs/tasks/task_list.md`

Header and methodology link, then sections for `PLUS-DEV`, `PLUS-EMU`, and `PLUS-SYS`. A category with no formally raised tickets says so explicitly. Each category with tickets has a Markdown table with **exactly six columns:** `ID`, `Kind`, `Status`, `Title`, `Milestone`, and `Ticket`. The ID and/or Ticket cells link to `db/<ID>.md`. Sort by ID.

Each row is a compact *current index*. It is not the place for discussion, reproduction steps, acceptance tests, resolution notes or commits. Add/delete no ticket without updating the index and the corresponding individual file together. Keep archived and closed tickets in the index. Roadmap-only, unopened IDs need no row or placeholder file.

### Individual ticket — `docs/tasks/db/<ID>.md`

Each file has a heading and **one ticket**, with these required fields *in this exact order*: **ID, Status, Title, Description, Details, Notes, Proposed Change, Commits, Resolution Description**. Technical subsections can be nested in Details or Proposed Change; there is no length cap.

```markdown
# PLUS-XXX-0001 — <Title>

**ID:** `PLUS-XXX-0001`
**Status:** `OPEN`
**Title:** <Title>

**Description**
<Observable gap/problem/risk and why it matters.>

**Details**
- Kind: BUG / TASK / IMPROVEMENT
- Category: PLUS-XXX
- Milestone: related roadmap ID
<Relative paths, code evidence, hypotheses, expected behavior,
dependencies, constraints, reproduction and test plan.>

**Notes**
- **YYYY-MM-DD — Discovery:** <Initial observed evidence.>

**Proposed Change**
<Proposed implementation, alternatives and acceptance; not yet approved.>

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |

**Resolution Description**
Pending.
```

An empty Commits table is valid until implementation commits exist. Do not add a second issue or an index inside a ticket. The master list and each ticket must match on ID, Title, Status and Kind. An implementation proposal does not count as an approved implementation, and `Resolution Description` remains `Pending` until reviewed.

## 6. Writing effective issues

**Title:** name the missing capability, defect or improvement, not a vague activity such as “review controller”. Keep it short enough for a one-line summary.

**Description:** state the observable problem, missing capability, risk, inconsistency, or improvement opportunity. Say why it matters; do not assume a proposed fix is the cause.

**Details:** give kind, category, milestone, relative source paths, relevant functions, version/branch inspected, assumptions, expected versus actual behavior, triggering conditions, bus/interrupt/timing considerations where applicable, and tests or code evidence. Distinguish *confirmed*, *suspected*, and *not yet tested*. Optional descriptors such as defect / safety / cleanup / improvement can live here if useful; they are not mandatory extra fields.

**Proposed Change:** describe a possible remedy and its consequences, not an irreversible instruction. Include open questions and alternatives where appropriate. If no remedy is yet justified, write `To be determined after review`.

**Commits:** maintain the per-issue implementation history in the four-column table prescribed in Section 8. Keep superseded commits visible rather than deleting them.

**Resolution Description:** initially `Pending`. Once accepted, capture the final disposition, files changed, relevant commits/PRs, verification results and limitations, and the reason for any deviation from the original proposal. Do not claim tests were run unless they were.

## 7. Notes: the chronological engineering log

Notes are **append-only** and contain dated entries. Add a note whenever an issue is materially investigated, its description/proposal is corrected, code is changed, a test is run, a review finds a problem, rework occurs, its status changes, or a decision is made.

Suggested entry syntax:

```markdown
- **YYYY-MM-DD — <Activity>:** <Observation, decision, evidence, or change>
```

Useful activity labels include `Discovery`, `Investigation`, `Decision`, `Implementation`, `Test`, `Code Review`, `Rework`, `Status Change`, and `Reopened`.

Keep current Description, Details, and Proposed Change accurate; when editing their substance, append a note explaining what changed and why. Do not silently erase an earlier interpretation or unsuccessful approach. Link to commits and tests where applicable. Record reviewer approval explicitly; do not infer it from silence.

## 8. Commit tracking and code review

Each individual ticket contains its own **Commits** section, immediately after Proposed Change and before Resolution Description. Record commits that implement, test, revert, or rework that ticket. Documentation-only commits unrelated to a ticket do not need an entry.

Use a table with exactly four columns:

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |
| `example-branch` | `a1b2c3d` | `UNREVIEWED` | Illustrative implementation change |

The example row above is a format illustration, **not** a real tracked commit. In actual tickets, leave the table with no data rows until a relevant commit exists.

- **Branch:** the branch on which the commit was created or submitted for review. Preserve the historical branch entry if a change is later integrated elsewhere.
- **Commit (short hash):** an unambiguous abbreviated Git SHA (normally 7–12 hexadecimal characters). Prefer linking the short hash to the full GitHub commit URL. The linked full SHA identifies the exact reviewed revision.
- **Review Status:** the disposition of that specific commit, independent of the ticket's overall status.
- **Description:** one short line explaining the change made by the commit, rather than a proposed or future action.

Use these per-commit review statuses:

| Review Status | Meaning |
| --- | --- |
| `UNREVIEWED` | Commit recorded; code review has not started. |
| `IN_REVIEW` | Commit is undergoing review. |
| `CHANGES_REQUESTED` | Review identified changes required before acceptance. |
| `APPROVED` | Commit has been reviewed and accepted. |
| `SUPERSEDED` | Commit was replaced, amended, or made obsolete by a later change; retain its record. |

Add a row when the commit becomes available. Update its review status when review progresses, and log the review decision, date, reviewer feedback, test results, or required rework in **Notes**. If amending or rebasing changes the commit hash, retain the old row marked `SUPERSEDED` and add a row for the new hash. Never transfer an `APPROVED` status automatically to a different commit SHA; review the replacement as a new revision.

A single commit may be listed in more than one ticket when it genuinely addresses multiple issues; reference all relevant IDs in the commit message where practical. Do not mistake an `APPROVED` commit for an automatically `RESOLVED` issue: the complete behavior and resolution still require verification and acceptance.

## 9. Review and resolution workflow

1. **Discover:** inspect the relevant code or baseline and register an `OPEN` ticket with evidence or a clearly marked hypothesis. Create the `db/<ID>.md` file and index row together.
2. **Triage together:** decide whether it is a real defect, a safety or portability risk, historical residue, or an improvement worth making. Clarify expected behavior and scope; log the decision.
3. **Agree on a proposed change:** examine alternatives and dependencies, particularly public API, ownership, nonblocking behavior, interrupt masking, allocator use, and affected boards. If the change is not agreed, keep the issue open or defer it.
4. **Implement:** work only on the designated working branch; make focused changes and reference ticket IDs in commits. Record each relevant commit in the issue's Commits table as `UNREVIEWED`. Do not modify preserved historical baselines.
5. **Verify:** add or run relevant native tests, build checks, static review, and board/ISR-specific checks as appropriate. Record exactly what passed, failed, or could not be tested. A build alone is not proof of interrupt safety.
6. **Review:** set the issue to `REVIEW`, review relevant commits, update their review statuses, and examine implementation, API behavior, edge cases, ownership, interrupt-state restoration, error paths, and evidence. Log review findings and rework.
7. **Accept and close:** after explicit approval, set `RESOLVED` with a complete resolution; set `CLOSED` once integrated into the designated branch or the approved no-change disposition is complete. Update `task_list.md` at each transition.

If a test later exposes a regression, reopen the original issue when it concerns the same root cause; otherwise open a new issue and cross-reference it.

### Recording review findings and required follow-up

After each substantive review, retain a **Review Findings** subsection under **Details**, with a small current-action table: `Finding ID`, `Classification`, `Evidence`, `Required action / acceptance evidence`, `State`. This is not a second ticket or master index. Append a dated Notes entry describing the review, evidence and disposition. Do not erase previous notes or commit history.

Distinguish the following rather than treating every unfinished requirement as a bug:

- **`CORRECTION_REQUIRED`:** a specific in-scope implementation/API defect identified by inspection or test. Give the failure path and a precise corrective test. Mark affected implementation commits `CHANGES_REQUESTED` if applicable.
- **`VERIFICATION_REQUIRED`:** an acceptance criterion lacks sufficient evidence. Record the missing test, source/media prerequisites and result **`NOT TESTED`**; do not call it a failure or imply code changes are necessary.
- **`FUTURE_SCOPE`:** an optional or subsequent milestone requirement. Link the owning roadmap/ticket, and do not enlarge the current ticket without explicit approval.

Keep the ticket in `REVIEW` while review feedback is documented and verification is pending. Return it to `IN_PROGRESS` **when the agreed correction or further testing actively starts**; after new commits/evidence, return it to `REVIEW`. A separate `REWORK_REQUIRED` status is unnecessary. Per-commit review state is independent: `CHANGES_REQUESTED` for concrete corrections, `IN_REVIEW` where evidence is still incomplete, and `APPROVED` only after explicit acceptance.

Document which tests **passed**, were **reported by another developer but not independently reproduced**, **failed**, or were **NOT TESTED**. If an optional or resource-dependent integration test is not run, explicitly decide whether it blocks this ticket or belongs to another milestone. A blocking acceptance criterion prevents `RESOLVED`; an expressly agreed out-of-scope limitation does not automatically prevent closure. Open a new ticket only for a genuinely separable issue, not for correcting the same implementation under review. Once complete, mark findings `VERIFIED`, `DEFERRED` or `ACCEPTED_LIMITATION` with rationale and evidence.

## 10. Consistency and scope controls

- `task_list.md` is the **one master index**, grouped by category; `db/<ID>.md` is the **one authoritative record** for its ticket.
- Never combine multiple tickets in a file. Never create numbered category volumes or 16-ticket summary files.
- Each registered index row must resolve to exactly one existing file, and each ticket file must have one matching index row; keep status/title/kind/category synchronized.
- Existing roadmap IDs stay reserved even when not formally opened in this tracker. Opening a ticket early does not change the roadmap's execution order.
- Do not batch unrelated defects into one ticket. Keep corrective fixes and discretionary redesign separate when acceptance criteria differ.
- Keep dated append-only Notes and superseded commit records. Do not silently erase prior decisions or transfer review approval to a different SHA.
- Implementations require explicit acceptance evidence; a successful compile alone is not proof of hardware correctness or enough to close a ticket.
- Preserve the shared Tima 6.x SDK and its ownership boundaries; emulator-specific concerns stay with the emulator.

## 11. Maintaining this methodology

This document describes **how to work**, not **what work to perform**. Bug discoveries, subsystem implementation plans, priorities and results belong in individual ticket records and the project roadmap, not in this methodology.

Changes to the tracking format or lifecycle should be reviewed like other engineering decisions, committed to the repository, and applied consistently to the individual ticket files and master list. Preserve the issue IDs, historical notes, and commit review trail when evolving the format.
