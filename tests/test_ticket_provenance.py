#!/usr/bin/env python3
"""Validate the Tima/Plus ticket provenance addendum and migrated tickets.

Run: python3 tests/test_ticket_provenance.py
Standard library only. Unmigrated historical tickets may retain legacy headers.
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
for keyword in (
    "Reporter", "Reviewer", "Assignee", "Current Work",
    "### 7.1 Stakeholder attribution", "Prospective Notes",
    "Retrospective provenance", "UNKNOWN", "git log --follow",
    "DONE", "PENDING", "BLOCKED", "CLOSED"
):
    require(keyword.casefold() in method.casefold(),
            "methodology missing " + keyword)

migrated = ("CPLD-DOC-0001", "PLUS-EMU-0011", "PLUS-ROM-0002")
tick = chr(96)
checks = 0
for issue_id in migrated:
    ticket = (DB / (issue_id + ".md")).read_text(encoding="utf-8")
    for field in ("ID", "Status", "Title", "Reporter", "Reviewer", "Assignee"):
        require("**" + field + ":**" in ticket, issue_id + " missing " + field)
        checks += 1

    require("**Current Work**" in ticket, issue_id + " missing Current Work")
    require("| State | Work / acceptance evidence | Responsible | Next action / reference |" in ticket,
            issue_id + " incorrect Current Work headings")
    require("| DONE |" in ticket, issue_id + " missing DONE evidence")
    require("**Notes**" in ticket, issue_id + " missing Notes")
    require(re.search(r"- \*\*20\d\d-\d\d-\d\d — (REPORTER|REVIEWER|ASSIGNEE) \(", ticket),
            issue_id + " lacks a prospective actor-labelled Note")

    status_match = re.search(r"\*\*Status:\*\* " + tick + r"([A-Z_]+)" + tick, ticket)
    require(status_match is not None, issue_id + " missing status")
    status = status_match.group(1)
    rows = [line for line in index.splitlines()
            if line.startswith("| ") and issue_id in line and "db/" + issue_id + ".md" in line]
    require(len(rows) == 1, issue_id + " must occur once in master index")
    require("| " + tick + status + tick + " |" in rows[0],
            issue_id + " index status differs from canonical ticket")
    if status == "CLOSED":
        resolution = ticket.split("**Resolution Description**", 1)[-1]
        require("integration" in resolution.casefold() or "merged" in resolution.casefold(),
                issue_id + " closed without recorded integration or disposition")
    checks += 7

# Owner's outstanding floppy-formatting acceptance must remain pending.
write_ticket = (DB / "PLUS-EMU-0010.md").read_text(encoding="utf-8")
require("**Status:** " + tick + "REVIEW" + tick in write_ticket,
        "PLUS-EMU-0010 must remain REVIEW pending owner formatting")
write_rows = [line for line in index.splitlines()
              if line.startswith("| ") and "db/PLUS-EMU-0010.md" in line]
require(len(write_rows) == 1 and "| " + tick + "REVIEW" + tick + " |" in write_rows[0],
        "PLUS-EMU-0010 index must also remain REVIEW")

print("Ticket provenance/index checks PASS ({} field/status checks)".format(checks))
