# CPLD-DOC-0001 — Add stakeholder attribution and current-work summary to tickets

**ID:** `CPLD-DOC-0001`
**Status:** `RESOLVED`
**Title:** Add stakeholder attribution and current-work summary to tickets
**Reporter:** Project owner (Kerob Labs)
**Reviewer:** Project owner; GPT-6
**Assignee:** GPT-6 (methodology and documentation change)

**Description**

The chronological Notes are valuable but long tickets do not show the latest completion state or who made a decision, performed a review, or implemented a change. The project owner requests visible `REPORTER`, `REVIEWER`, and `ASSIGNEE` stakeholders and provenance for historical notes when supported by evidence.

**Details**

- Kind: IMPROVEMENT
- Category: `CPLD-DOC`
- Milestone: Engineering workflow
- Baseline: `rtl-compile-work` at 2026-11-21
- Scope: `docs/tasks/bug-tracking-methodology.md`, relevant touched tickets, index and documentation validation; no change to hardware RTL or runtime.
- Repository Git commits historically use the `Kerob Labs` identity for reviewer-written notes as well as owner activity. Commit author/committer alone is not proof of the stakeholder responsible for the underlying engineering decision.

**Current Work**

| State | Work / acceptance evidence | Responsible | Next action / reference |
| --- | --- | --- | --- |
| DONE | Methodology defines visible ticket stakeholders, editable work summary, actor-labelled notes, and conservative retrospective attribution | GPT-6 | Examine updated methodology and validation result |
| DONE | Add test script validating touched tickets' index/state/role/note requirements | GPT-6 | Run `python3 tests/test_ticket_provenance.py` |
| DONE | Project owner requested acceptance of the methodology and explicit RESOLVED status | Reporter / Reviewer (project owner) | Accepted, awaiting merge into `main` |
| DONE | Established an incremental historical-attribution policy; unresolved authorship stays UNKNOWN | Reviewer (GPT-6) | Apply when future tickets are touched; not a blocker to this methodology ticket |
| PENDING | Close methodology change when accepted commits are merged to `main` | Project owner | Owner-controlled PR and merge |

**Notes**

- **2026-11-21 — REPORTER (Project owner) — Request:** Make completed versus pending work evident at a glance; identify reporter and reviewer as the project owner and GPT-6, with assignee as the implementing person or agent. Trace existing Notes to their source where reasonably possible.
- **2026-11-21 — REVIEWER (GPT-6) — Investigation:** Existing methodology had append-only date/activity Notes and per-commit tracking but no roles or mutable work summary. Git history identifies commit authors, but historical author `Kerob Labs` does not alone identify the actual reviewer, reporter or assignee.
- **2026-11-21 — ASSIGNEE (GPT-6) — Implementation:** Added three mandatory roles on new/migrated tickets, a short DONE/PENDING/BLOCKED table, prospective role-labelled notes, and a conservative attribution protocol for legacy Notes. Existing note text remains append-only and unmodified.
- **2026-11-21 — ASSIGNEE (GPT-6) — Verification:** Repository-level `tests/test_ticket_provenance.py` document validator added to check the two migrated tickets and index. Additionally, 26/26 direct checks against the committed GitHub document blobs passed: role headers, title/status consistency, current-work state, prospective labelled notes, preservation of original HW-0004 historical Notes, and outstanding post-change fitting criteria. The Python file was not executed on `devel` during this commit; the direct checks are static and not RTL or hardware tests. Owner execution and review remain required.

- **2026-11-21 — REPORTER (Project owner) — Acceptance request:** The owner directed implementation of the methodology change as specified in this ticket and explicitly instructed setting CPLD-DOC-0001 to RESOLVED; CLOSED must wait for merge to `main`.
- **2026-11-21 — ASSIGNEE (GPT-6) — Methodology refinement:** The proposed roles, editable Current Work summary, append-only actor-labelled Notes and historical-attribution policy were already present on `rtl-compile-work` from `d7cde1b`. Added an explicit `main`-merge closure gate and updated the validator in `2305614`. No historic Notes were rewritten.
- **2026-11-21 — REVIEWER (GPT-6) — Verification and disposition:** Six direct structural assertions passed for the clarification (role fields retained, provenance section intact, CLOSED tied to main, validator checks revised); previous DOC-0001/HW-0004 direct cross-document verification recorded 26/26. This is static source review, not execution of the Python validator or RTL regression. The owner requested RESOLVED; status and single index row updated together, and CLOSED deferred until actual merge.

- **2026-11-21 — REVIEWER (GPT-6) — Index consistency correction:** Final GitHub branch-head review exposed that a later HW-0004 documentation commit reused a stale task index and unintentionally restored DOC-0001's index row to REVIEW while the authoritative ticket remained RESOLVED. Corrected only the DOC-0001 index row back to RESOLVED and recorded this reconciliation. Acceptance remains unchanged; CLOSED is still reserved for main merge.

**Proposed Change**

Add `Reporter`, `Reviewer`, and `Assignee` to the top of new/migrated tickets. Include editable `Current Work` summary, preserve immutable Notes, tag new notes with role and actor, and separately identify the provenance and confidence of any historical attribution. Do not re-label legacy notes without evidence. Index stays six columns; status/title changes propagate in the same commit.

**Acceptance criteria / required evidence**

- [x] Methodology describes roles, summary, actor-labelled notes, historical provenance and uncertainty.
- [x] Touching an existing ticket does not overwrite existing Notes.
- [x] Add validator for matching index/ticket status and roles and prospective note format.
- [x] Owner requests and accepts implementation of the proposed ticket readability and actor distinctions; mark RESOLVED, not CLOSED.
- [ ] Close only after owner merges approved changes into `main`.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- |
| `rtl-compile-work` | `d7cde1b` | APPROVED | Introduce stakeholder/Current Work methodology, provenance validator, and DOC-0001 ticket/index |
| `rtl-compile-work` | `2305614` | APPROVED | Clarify closure after main merge and update workflow validator |

**Resolution Description**

**RESOLVED (2026-11-21):** The project owner requested acceptance of the stakeholder/provenance methodology and explicitly instructed marking this ticket RESOLVED. Updated `docs/tasks/bug-tracking-methodology.md` establishes Reporter, Reviewer and Assignee; an editable current DONE/PENDING/BLOCKED summary; append-only dated actor-labelled Notes; conservative Git-based historical provenance with UNKNOWN when unprovable; and owner-controlled `RESOLVED → CLOSED` transition only on merge to `main` (or approved no-change disposition). `tests/test_ticket_provenance.py` checks the format and now avoids freezing HW-0004 in a particular working status. Connector-backed static assertions verified the updated method and validator; **the Python script has not been executed on `devel` in this review**. Historical backfill remains incremental rather than a requirement for resolving the methodology. Await `main` merge before CLOSED.
