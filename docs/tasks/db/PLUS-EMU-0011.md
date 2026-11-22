# PLUS-EMU-0011 — Insert and reinsert the assigned uMac floppy with F11

**ID:** `PLUS-EMU-0011`

**Status:** `OPEN`

**Title:** Insert and reinsert the assigned uMac floppy with F11

**Reporter:** Codex, on behalf of the project owner

**Reviewer:** Project owner (Kerob Labs)

**Assignee:** Codex

**Description**

uMac can insert an 800K floppy at startup with `-d`. After ejecting it through Finder, the user has to restart uMac to insert a disk again. The development workflow needs an empty drive at startup and the ability to insert an image, eject it in Finder, and put a new image at the same host path for the next insertion.

**Details**

- Kind: `TASK`
- Category: `PLUS-EMU`
- Milestone: [PLUS-DEV-0004](../../01-development-roadmap.md#plus-dev-0004--complete-the-virtual-800k-floppy-subsystem).
- Working branch: `pre-release-umac-disk-insert`, inspected at `82db564`.
- `devel/umac/src/unix_main.c` opens `-d` at startup. Its `-i` currently enables instruction tracing, and its SDL keyboard handler reserves F12 for releasing the mouse.
- `devel/umac/src/disk_img.c` completes Sony EJECT by checkpointing dirty tracks, clearing media presence and stopping the spindle. The drive retains the old image and any writable sidecar lock until host close. The IWM drive lifecycle and Sony sense signals are defined in [Document 04 §12.10](../../04-iwm-emulation.md#1210-disk-lifecycle-insertion-eject-and-drive-power-software-and-future-stm32). [PLUS-EMU-0010](PLUS-EMU-0010.md) records the underlying checkpoint and persistence behavior.

**Current Work**

| State | Work / acceptance evidence | Responsible | Next action / reference |
| --- | --- | --- | --- |
| DONE | Register owner requirements and inspect startup, input and eject code | Reporter (Codex) | Opened on the designated branch |
| PENDING | Implement assigned-path insertion and writable-media release | Assignee (Codex) | Verify the proposed behavior |
| PENDING | Exercise Finder eject and host replacement with the original ROM | Assignee (Codex) | Record test results and local commits |
| PENDING | Review the implementation and evidence | Reviewer (project owner) | Review the assignee handoff |

**Notes**

- **2026-11-22 — REPORTER (Codex, on behalf of the project owner) — Request:** Preserve `-d devel/umac/floppy-disk.image` for insertion at startup. Add `-i devel/umac/floppy-disk.image` to assign the path with the drive empty. F11 should insert the assigned image after startup and after each successful Finder eject. The user can overwrite the file in place or replace it at that path while the disk is ejected. Finder controls eject on the M0001AP.
- **2026-11-22 — REPORTER (Codex) — Discovery:** Confirmed by source inspection that the frontend has no host insertion handler, `-i` is already a trace flag, and the existing eject path leaves the image and writable lock allocated. Runtime verification is pending.

**Proposed Change**

Keep the emulated internal drive installed and track its medium separately from the configured host path. Load and validate the file afresh on each F11 press with an empty drive. On successful guest eject, checkpoint and release image data, cached tracks and host lock resources. Update Sony senses when the medium is accepted or released. Preserve the existing instruction trace and floppy write options.

Acceptance criteria and required evidence

- [ ] `-d` still inserts at startup. `-i` starts with the drive installed and empty, including when the image file is absent at launch.
- [ ] F11 inserts the assigned image through the original ROM's Sony drive interface. With a disk present or with no path assigned, F11 leaves the drive unchanged.
- [ ] Finder eject releases the image and its writable lock after a successful checkpoint. After in-place overwrite or replacement at the same path, F11 mounts the new contents. The drive remains empty after a failed image load and can retry when the image is available.
- [ ] Existing raw and DiskCopy validation, read-only behavior, writable checkpoint failure recovery, SCSI boot and desktop controls pass regression checks.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |

**Resolution Description**

Pending.
