# PLUS-EMU-0011 — Insert and reinsert the assigned uMac floppy with F11

**ID:** `PLUS-EMU-0011`

**Status:** `REVIEW`

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

**Current Work**

| State | Work / acceptance evidence | Responsible | Next action / reference |
| --- | --- | --- | --- |
| DONE | Register owner requirements and inspect startup, input and eject code | Reporter (Codex) | Opened on the designated branch |
| DONE | Implement assigned-path insertion and writable-media release | Assignee (Codex) | See the implementation commit and uMac README |
| DONE | Exercise Finder eject and host replacement with the original ROM | Assignee (Codex) | See the build, test and trace evidence above |
| DONE | Move deferred assignment to `-a` and verify the command-line and Finder behavior | Assignee (Codex) | `EMU11-R1` verified |
| PENDING | Review the local implementation and accept the result | Reviewer (project owner) | Review the commit and exercise the Wayland launch |

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
| `pre-release-umac-disk-insert` | `423822c` | `UNREVIEWED` | Address `EMU11-R1` with `-a <floppy path>`, trace-only `-i`, and CLI and Finder regression evidence. |

**Resolution Description**

Pending.
