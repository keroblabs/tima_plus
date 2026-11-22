#!/usr/bin/env python3
"""Check Tima/Plus ticket provenance addendum and migrated ticket/index consistency.

Run from any directory: python3 tests/test_ticket_provenance.py
Uses only the standard library. Existing legacy tickets are intentionally
not required to have stakeholder headers until they are explicitly migrated.
"""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
METHOD = ROOT / "docs/tasks/bug-tracking-methodology.md"
INDEX = ROOT / "docs/tasks/task_list.md"
DB = ROOT / "docs/tasks/db"


def require(condition, message):
    if not condition:
        raise AssertionError(message)


method = METHOD.read_text(encoding="utf-8")
index = INDEX.read_text(encoding="utf-8")
for field in ("Reporter", "Reviewer", "Assignee", "Current Work"):
    require(field in method, "methodology missing " + field)
for keyword in (
    "### 7.1 Stakeholder attribution",
    "prospective",
    "Retrospective provenance",
    "UNKNOWN",
    "git log --follow",
    "DONE",
    "PENDING",
    "BLOCKED",
    "CLOSED",
):
    require(keyword.lower() in method.lower(), "methodology missing " + keyword)

# Only these known migrated Tima/Plus tickets are checked for the new template.
migrated = ("CPLD-DOC-0001", "PLUS-EMU-0011", "PLUS-ROM-0002")
checks = 0
for issue_id in migrated:
    ticket = (DB / (issue_id + ".md")).read_text(encoding="utf-8")
    for field in ("ID", "Status", "Title", "Reporter", "Reviewer", "Assignee"):
        require("**" + field + ":**" in ticket, issue_id + " missing " + field)
        checks += 1
    require("**Current Work**" in ticket, issue_id + " missing Current Work")
    require("| State | Work / acceptance evidence | Responsible | Next action / reference |" in ticket,
            issue_id + " invalid Current Work columns")
    require("| DONE |" in ticket, issue_id + " missing DONE evidence")
    require("**Notes**" in ticket, issue_id + " missing Notes")
    require(re.search(r"- \\*\\*20\\d\\d-\\d\\d-\\d\\d — (REPORTER|REVIEWER|ASSIGNEE) \\(", ticket),
            issue_id + " lacks a prospective actor-labelled Note")

    status_match = re.search(r"\\*\\*Status:\\*\\* \\x60([A-Z_]+)\\x60", ticket)
    require(status_match is not None, issue_id + " missing status")
    status = status_match.group(1)
    rows = [line for line in index.splitlines()
            if line.startswith("| ") and issue_id in line and "db/" + issue_id + ".md" in line]
    require(len(rows) == 1, issue_id + " must appear exactly once in master index")
    require("| \\x60" + status + "\\x60 |" in rows[0],
            issue_id + " status mismatch between master index and canonical ticket")
    if status == "CLOSED":
        resolution = ticket.split("**Resolution Description**", 1)[-1]
        require("integration" in resolution.lower() or "merged" in resolution.lower(),
                issue_id + " CLOSED without integration/disposition evidence")
    checks += 7

# The approved IWM write ticket must not be closed without owner formatting.
write_ticket = (DB / "PLUS-EMU-0010.md").read_text(encoding="utf-8")
require("**Status:** \\x60REVIEW\\x60" in write_ticket,
        "PLUS-EMU-0010 must remain in REVIEW pending owner formatting")
write_rows = [line for line in index.splitlines()
              if line.startswith("| ") and "db/PLUS-EMU-0010.md" in line]
require(len(write_rows) == 1 and "| \\x60REVIEW\\x60 |" in write_rows[0],
        "PLUS-EMU-0010 must be REVIEW in task list as well")

print("Ticket provenance/index checks PASS ({} migrated ticket field/status checks)".format(checks))
