# PLUS-EMU-0011 — Insert and reinsert the assigned uMac floppy with F11

**ID:** `PLUS-EMU-0011`

**Status:** `RESOLVED`

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

The two source observations above describe the inspected baseline. The implementation keeps the installed drive at the same address in the IWM and replaces its media state only after a successful load or checkpoint. F11 handles the first key-down while the drive is empty, consumes the host key and reloads the originally assigned path. Writable media acquire the existing sidecar lock on insertion and release it on successful eject. The launcher checks the current image and lock paths for aliases on each insertion because they can change between ejections.

### Review Findings

| Finding ID | Classification | Evidence | Required action / acceptance evidence | State |
| --- | --- | --- | --- | --- |
| `EMU11-R1` | `CORRECTION_REQUIRED` | `c7e9117` makes `-i` either a trace switch or a floppy assignment depending on the next token. | Give deferred floppy assignment its own required-argument option, `-a <floppy path>`. Verify bare `-i` traces, `-a` requires a path, and `-a <floppy path> -w` supports the Finder eject and F11 replacement cycle. See the rework evidence below. | `VERIFIED` |

### Initial verification (`c7e9117`)

| Configuration or test | Result | Evidence |
| --- | --- | --- |
| Baseline Release, 4 MiB, tagged DiskCopy, real Plus ROM | PASS, 19/19 CTest | Reference build of uMac at `82db564`, including original-ROM boot, write, format, SDL timing and DUART disk tests |
| Modified Release, 4 MiB, tagged DiskCopy, real Plus ROM | PASS, 19/19 CTest | Expanded DiskCopy test verifies Sony senses, lock contention and release, retry after malformed media, read-only remount and complete first-track data and tag decoding after rewriting the same path as raw. Checkpoint test reopens for writing while the ejected drive still exists. |
| Modified Release, 1 MiB, raw 800K, real Plus ROM | PASS, 18/18 CTest | Raw image derived from the tagged fixture's data area. Includes original-ROM boot, write and format. CMake registers the SDL timing test on 4 MiB only. |
| Modified Debug, stock M0001AP map, DUART disabled | PASS, 13/13 CTest | Generated fixtures and `m68681_production_map` |
| Floppy path guards | PASS | `iwm_media_protection` checks startup aliases for `-d` and `-i`, the `-w` requirement and grouped legacy options. The SDL session also rejected a replacement symlink to `ram.bin`. |
| Standalone Make and instruction tracing | PASS | `make -j3 DEBUG=1` in an isolated copy. Bare `-i` and `--trace-instructions` each emitted disassembly from a generated reset ROM. |
| HFS converter and heap checks | PASS | Four `test_hfs_to_device.py` cases. The DiskCopy, checkpoint and media protection CTests also passed with `MALLOC_PERTURB_=205`. |
| GCC AddressSanitizer and UndefinedBehaviorSanitizer | NOT TESTED | The host C compiler link check failed because `libasan.so.8.0.0` and `libubsan.so.1.0.0` were missing. |

The builds used GCC 15.2.1, CMake 3.31.6, SDL 2.32.64 and the local `vasmm68k_std`. Release retained the baseline Musashi compiler warnings. Source media were copied to a temporary directory. SHA-256 checks confirmed the workspace ROM, SCSI disk and both floppy files retained their original bytes. The raw CTest fixture SHA-256 was `7f9f78a08aa19bafe5700e81675aa3ac212f4f601b6d209f64d49d8cf339f600`.

### Original-ROM Finder insert, eject and replacement

The SDL frontend ran offscreen with the Plus V3 ROM (`4d1f8172`), `HD10_512.hda` read-only and `-i media/floppy-disk.image -w`. A temporary driver queued SDL key and mouse events. Finder booted from `Macintosh` on SCSI (driver `-34`) with the assigned directory absent. F11 reported the missing path and later rejected a malformed image and a RAM alias. The drive stayed empty through those attempts. A repeat key-down and key-up were ignored. Each subsequent F11 mounted the expected guest volume on `.Sony` (driver `-5`). Command-E in Finder ejected each disk, left only `Macintosh` mounted, and released the lock as verified by taking it from a second host process. F11 while mounted preserved the volume and lock.

| Host change before F11 | Finder volume | SHA-256 before F11 | SHA-256 after Finder eject |
| --- | --- | --- | --- |
| Create raw 800K file, 819,200 bytes | `Swap Raw One` | `1e9d17225877c63e78e7ad57a0f091f12e952b65af6180b2bea8894efd3f3b3c` | `512d6397c03db78c75853ba7cb9dbea7fc91f6a701329e47bfde3e649c51af77` |
| Overwrite the same inode with tagged DiskCopy 4.2, 838,484 bytes | `Swap Tagged2` | `3eb149fed1d48b9f55d25bc6c4815cc1dd6bd82cebf86c59f1c1e3e4d800f15b` | `f985439ebbb18a7e565865fe20c30a3552e60ccba34a342d41089447500b1791` |
| Rename a new tagged DiskCopy 4.2 file over the assigned path and verify the inode changed | `Swap Tagged3` | `b678bf2d5c71a83ca80f35da1025a44af7dc6240d92015f4cbda915be0422550` | `2e42006b3f058ddcf623212f5dc93cd77595a76c69fc62a06299790c67fd4c81` |

The first mount and eject produced the following IWM observations. The later two cycles repeated the same media and spindle transitions.

| C8M tick | ROM PC | Observation |
| ---: | --- | --- |
| 359,285,288 | `$418606` | Sony sense register 1 returned `$BF`, with `/CSTIN` high and the drive empty. |
| 1,758,534,192 | `$418606` | After F11, Sony sense register 1 returned `$3F`, with `/CSTIN` low and the spindle still off. |
| 4,545,051,552 | `$417FDA` | Finder's Sony EJECT strobed command 6 with CA2 high. The spindle was off, the medium was absent and the trace reported `save_error=0`. |
| 4,545,838,792 | `$418606` | The ROM read `$BF` from sense register 1 after eject. |

Separate read-only controls used a valid data floppy already on disk at startup. `-i` left it empty until F11. `-d` inserted it immediately, the ROM ejected it while falling back to SCSI boot, and F11 mounted it again. Finder eject left both control images byte-identical and neither acquired a writable lock. An unassigned session handled F11 and F12 without transmitting either key to the guest. Full logs and screenshots from this run are under `/tmp/plus-emu-0011.4oNkDc/`; the screenshots and filtered guest trace are in `replacement-zbgkkbzb/`.

### EMU11-R1 rework verification

| Check | Result and evidence |
| --- | --- |
| CLI separation | PASS. The old `-i floppy-disk.image -w` reached ROM loading in an empty working directory. The updated `iwm_media_protection` test verifies that `-w -i <floppy path>` leaves the floppy unassigned, `-a` requires a path, and path protection applies to both `-a` and `-d`. That test also passed with `POSIXLY_CORRECT=1`. |
| Trace switches | PASS. A standalone `make -j3 DEBUG=1` build emitted `E 400008: 60fe` with bare `-i`, `--trace-instructions`, and grouped `-iw -a pending.image`. The grouped run announced a deferred assignment and left the writable lock free. |
| Regression suites | PASS. Tagged Release with the original Plus ROM and 4 MiB passed 19/19 CTests. Raw Release with the original Plus ROM and 1 MiB passed 18/18. Debug with the production map passed 13/13. |
| Offscreen SDL with the original Plus ROM | PASS. With `-a media/floppy-disk.image -w`, SCSI Finder started with the drive empty. F11 mounted the raw `Swap Raw One` volume, the tagged `Swap Tagged2` volume after an in-place overwrite, and `Swap Tagged3` after a replacement by rename. Finder Command-E returned the drive to empty and released the writable lock after each mount. Read-only `-a` and `-d` startup controls and unassigned F11 also passed. |

On the first `-a` cycle, ROM PC `$418606` read Sony sense register 1 as `$BF` at C8M tick 352,037,952 and `$3F` after F11 at tick 1,844,385,964. Finder's EJECT strobed command 6 at PC `$417FDA`, tick 4,683,198,608, with `media=0` and `save_error=0`. Sense returned `$BF` at tick 4,683,985,928. The working ROM, SCSI disk and both floppy images matched the SHA-256 hashes from initial verification. Rerun evidence is under `/tmp/plus-emu-0011-a.h0KDWZ/`, including `trace-cli.log`, `desktop-insert.log`, and the filtered IWM trace and screenshots in `replacement-l0u63kkf/`.

To repeat the desktop check, start uMac in a private working directory with an original Plus ROM and bootable SCSI image. Pass `-a floppy-disk.image -w --trace-iwm` and use disposable floppy copies. Wait for SCSI Finder, copy a floppy image to the assigned name and press F11. Select its icon and press Command-E. Write a differently named Macintosh volume to the same host path and press F11 again. Confirm Finder shows the new volume and the trace shows `/CSTIN` changing between `$BF` and `$3F`. The test used an offscreen SDL window. Physical key delivery through the owner's Wayland session is pending owner review.

### Independent technical review — 22 November 2026

**Result: TECHNICAL_REVIEW_APPROVED, pending final project-owner acceptance. No additional implementation changes requested. Ticket remains `REVIEW`.**

Reviewed the six-commit delta from `pre-release-umac` to `pre-release-umac-disk-insert`, including `devel/umac/src/unix_main.c`, `disk_img.c`, `iwm.c`, new/reworked IWM tests and the current `-a` CLI correction.

- **Startup versus assignment:** `-d <path>` still inserts at process startup; `-a <path>` assigns the path while creating an empty installed drive. `-w` works with either. Existing bare `-i` retains instruction tracing, eliminating the ambiguous original `-i <path>` interpretation. The `-a` option requires its argument.
- **Safe host input handling:** The SDL frontend intercepts **F11** key-down (ignores repeats and key-up, does not forward it to the guest), does nothing if a medium is already present or no image was assigned, and revalidates aliases and the image before each insertion. The existing SCSI image's open file descriptor is checked against the new floppy image. Missing/invalid media remains retryable.
- **Correct drive/media separation:** The IWM retains its existing drive object and mechanical state. `disk_img_insert()` loads and validates fresh image bytes into an empty drive. Successful Sony EJECT aborts an active write, requires a successful checkpoint, releases the image and writable sidecar lock, clears cached tracks, sets media absent and stops the spindle; the IWM read latch is rearmed after media removal. A checkpoint failure retains mounted media for retry.
- **Regression evidence:** Developer records report **19/19** tagged 4 MiB, **18/18** raw 1 MiB and **13/13** production-map CTests passing. The host-side SDL test ran the **unmodified Plus V3 ROM**, booted Finder from **SCSI**, then inserted, Finder-ejected and reinserted 800K images after both in-place file overwrite and same-path rename. The user separately reports successfully exercising F11 and Finder eject/reinsert interactively. The CLI, write-lock, raw/DiskCopy, read-only, missing-image and keyboard-repeat guards have test coverage.

**Non-blocking observations:** This interface intentionally has **one assigned pathname**, not an OS file picker or arbitrary new path entry; that is consistent with the owner's requested development workflow. Replacing media at that path is supported **after successful Finder eject**, not while mounted. The device is an emulator feature and does not alter physical M0001AP hardware. No further UI or media-management feature should be added to this ticket merely for completeness.

**Evidence limitation:** This review inspected committed code, test logic and developer-recorded results, but did **not** rerun the compiler/emulator test suites or access their temporary host logs. It relies on the recorded test evidence and the owner's explicit live-session confirmation. The earlier review finding `EMU11-R1` is satisfactorily corrected.

**Disposition:** Technically ready for final owner approval and owner-controlled merge; retain `REVIEW` until the project owner confirms acceptance. Do not automatically close or merge.

**Current Work**

| State | Work / acceptance evidence | Responsible | Next action / reference |
| --- | --- | --- | --- |
| DONE | Register owner requirements and inspect startup, input and eject code | Reporter (Codex) | Opened on the designated branch |
| DONE | Implement assigned-path insertion and writable-media release | Assignee (Codex) | See the implementation commit and uMac README |
| DONE | Exercise Finder eject and host replacement with the original ROM | Assignee (Codex) | See the build, test and trace evidence above |
| DONE | Move deferred assignment to `-a` and verify the command-line and Finder behavior | Assignee (Codex) | `EMU11-R1` verified |
| DONE | Review the implementation and accept the result | Reviewer (project owner) | Owner manually verified SCSI startup, F11 insert, Finder eject and F11 reinsert; approval 2026-11-22 |
| PENDING | Merge approved work and perform integration housekeeping | Project owner | No merge has been performed as part of ticket approval |

**Notes**

- **2026-11-22 — REPORTER (Codex, on behalf of the project owner) — Request:** Preserve `-d devel/umac/floppy-disk.image` for insertion at startup. Add `-i devel/umac/floppy-disk.image` to assign the path with the drive empty. F11 should insert the assigned image after startup and after each successful Finder eject. The user can overwrite the file in place or replace it at that path while the disk is ejected. Finder controls eject on the M0001AP.
- **2026-11-22 — REPORTER (Codex) — Discovery:** Confirmed by source inspection that the frontend has no host insertion handler, `-i` is already a trace flag, and the existing eject path leaves the image and writable lock allocated. Runtime verification is pending.
- **2026-11-22 — ASSIGNEE (Codex) — Decision and status change:** The owner requested both the ticket and its implementation on `pre-release-umac-disk-insert`. Recorded the open ticket in `0d08ce6` and moved it to `IN_PROGRESS`. The existing IWM drive object will retain its mechanical state across media changes. Both floppy options will remember the path for F11, and bare `-i` will continue to enable the trace.
- **2026-11-22 — ASSIGNEE (Codex) — Implementation:** Added deferred CLI assignment and SDL F11 handling. Reused format validation and writable locking for each load, and reused the checkpoint path for Finder eject. Cleared the old image, encoded tracks and IWM latch on completed eject. Added regression checks for replacement and startup path aliases. Updated the working VS Code floppy option to `-i`; the local launch configuration and media were kept outside the implementation commit.
- **2026-11-22 — ASSIGNEE (Codex) — Rework and verification:** The first SDL run reported an incorrect error for a missing parent directory. Preserved the error from the failed path lookup and confirmed the frontend reports the missing path and can retry. The 4 MiB tagged, 1 MiB raw and production-map CTest runs passed 19/19, 18/18 and 13/13. Confirmed the Finder eject and replacement sequence using actual SDL input events and the unmodified ROM. Recorded logs, checksums and the optional sanitizer limitation above.
- **2026-11-22 — ASSIGNEE (Codex) — Status change:** Moved `IN_PROGRESS` → `REVIEW` after verification. Owner review and acceptance are pending.
- **2026-11-22 — ASSIGNEE (Codex) — Local commit and handoff:** Recorded the implementation, tests and verification as `c7e911787f5fe33e641886a7c47f0404b00ff16f` on the designated branch. The commit is ready for project-owner review.
- **2026-11-22 — REVIEWER (local) — Command:** The project owner, identified in the request as `REVEWER (local)`, directed replacing `-i <floppy path>` with `-a <floppy path>` to remove the conflict with bare `-i`. Recorded `EMU11-R1` and marked `c7e9117` `CHANGES_REQUESTED`.
- **2026-11-22 — ASSIGNEE (Codex) — Status change:** Moved `REVIEW` → `IN_PROGRESS` to implement `EMU11-R1`. The correction covers argument parsing, the CLI and launch examples, and regression verification.
- **2026-11-22 — ASSIGNEE (Codex) — Rework:** Gave `-a` a required path argument, restored trace-only parsing for `-i`, and updated CLI help, README and regression coverage. Updated the working VS Code launch to `-a devel/umac/floppy-disk.image` with its existing `-w`. The launch edit remains local to the working copy. The proposed change and reproduction instructions now use `-a`.
- **2026-11-22 — ASSIGNEE (Codex) — Verification and status change:** Confirmed separate parsing and debug traces, reran the 19/19 tagged, 18/18 raw and 13/13 production-map CTest suites, and repeated Finder eject and both same-path replacement methods with `-a`. Marked `EMU11-R1` `VERIFIED` and returned `IN_PROGRESS` → `REVIEW` for owner acceptance.
- **2026-11-22 — ASSIGNEE (Codex) — Local commit and handoff:** The review direction was recorded in `e286978`. The correction, tests and evidence are in `423822c5872533aff5fb566fa51fe5c43470d603` on `pre-release-umac-disk-insert`, ready for project-owner review.
- **2026-11-22 — TECHNICAL REVIEW (ChatGPT) — Approved pending owner:** Reviewed the F11 handler, deferred `-a` path handling, media image attach/eject lifecycle, checkpoint and lock release, IWM latch rearming, and recorded original-ROM SCSI/Finder regression evidence. No blocking code or design concerns identified. Developer test suites were not independently rerun here. Ticket remains `REVIEW` for final project-owner acceptance.

- **2026-11-22 — REVIEWER (Project owner) — Final approval and status change:** The owner explicitly accepted the feature after their manual test: boot using SCSI, F11 to insert the `-a` floppy, eject through Finder, and F11 to reinsert. Approved correction commit `423822c` and the overall outcome, changing `REVIEW` → `RESOLVED`. Merge remains owner-controlled and is not yet recorded; ticket is not `CLOSED`.

**Proposed Change**

Keep the emulated internal drive installed and track its medium separately from the configured host path. Use `-a <floppy path>` to assign the path with the drive empty and `-d <floppy path>` to insert at startup. Use `-i` for instruction tracing and `-w` to enable persistent writes with either floppy option. Load and validate the file afresh on each F11 press with an empty drive. On successful guest eject, checkpoint and release image data, cached tracks and host lock resources. Update Sony senses when the medium is accepted or released.

Acceptance criteria and required evidence

- [x] `-d` inserts at startup. `-a` starts with the drive installed and empty, including when the image file is absent at launch.
- [x] `-a` requires a path. Bare `-i` enables instruction tracing.
- [x] F11 inserts the assigned image through the original ROM's Sony drive interface. With a disk present or with no path assigned, F11 leaves the drive unchanged.
- [x] Finder eject releases the image and its writable lock after a successful checkpoint. After in-place overwrite or replacement at the same path, F11 mounts the new contents. The drive remains empty after a failed image load and can retry when the image is available.
- [x] Existing raw and DiskCopy validation, read-only behavior, writable checkpoint failure recovery, SCSI boot and desktop controls pass regression checks.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |
| `pre-release-umac-disk-insert` | `c7e9117` | `CHANGES_REQUESTED` | Add F11 insertion from the configured path, release media on successful Sony eject, and retain regression evidence. `EMU11-R1` requires a dedicated assignment option. |
| `pre-release-umac-disk-insert` | `423822c` | `APPROVED` | Correct `EMU11-R1` with `-a <floppy path>`, trace-only `-i`, and CLI and Finder regression evidence; accepted by project owner on 2026-11-22. |

**Resolution Description**

**Accepted and RESOLVED on 2026-11-22 by the project owner**, following independent technical review and the owner's successful interactive test.

- **Disposition:** `PLUS-EMU-0011` is approved. `-d <image>` continues to insert a floppy at startup; `-a <image>` assigns a deferred insertion path without inserting media before boot; `-i` remains instruction tracing; `-w` allows persistent writes. F11 inserts the assigned image only into an empty drive. Finder eject checkpoints pending changes and, on success, releases media and its writable lock, allowing replacement at the same host path and subsequent F11 reinsertion.
- **Implementation and tests:** `devel/umac/src/unix_main.c`, `disk_img.c`, `iwm.c`, supporting headers, CLI/README and IWM regression fixtures. Implementation [`c7e9117`](https://github.com/keroblabs/tima_plus/commit/c7e911787f5fe33e641886a7c47f0404b00ff16f) required rework after its ambiguous `-i` handling; corrected and accepted in [`423822c`](https://github.com/keroblabs/tima_plus/commit/423822c5872533aff5fb566fa51fe5c43470d603). Independent review recorded in `fce5cc2`.
- **Verification:** Developer-recorded CTest suites: **19/19** (4 MiB tagged DiskCopy), **18/18** (1 MiB raw 800K), and **13/13** (development UART disabled), with original Plus V3 ROM, Finder/SCSI boot, eject and reinsert behavior, malformed media retries, write locks, alias protection and existing IWM write/format. The project owner **personally confirmed** that the system boots from SCSI with `-a <image>`, F11 inserts the floppy, Finder ejects it and F11 successfully reinserts it. The independent reviewer inspected code and tests but did not reproduce the developer's local suite.
- **Scope boundary:** One fixed host pathname is intentional; replacing the image is supported only after successful eject. Optional sanitizers were **not tested** because their host runtime libraries were unavailable. Neither constitutes a blocker to this approved ticket.
- **Integration:** The implementation remains on `pre-release-umac-disk-insert`; **no merge has been made by this acceptance action**. Per project methodology, move to `CLOSED` after the project owner completes merge/integration and records the merge reference.

