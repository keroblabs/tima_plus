# PLUS-EMU-0005 — Emulate a SCSI hard disk backed by a BlueSCSI-compatible image

**ID:** `PLUS-EMU-0005`  
**Status:** `OPEN`  
**Title:** Emulate a SCSI hard disk backed by a BlueSCSI-compatible image

**Description**

The Macintosh Plus emulator needs a **real SCSI target device model attached to its emulated SCSI bus**, not merely the NCR 5380 host controller. The present uMac has no SCSI disk target, and its `devel/umac/src/disc.c` is a ROM-patched/paravirtual **floppy** driver that bypasses SCSI hardware and must not be reused as a SCSI command shortcut.

Implement a deterministic, testable **direct-access SCSI hard disk** that is selectable through `PLUS-EMU-0004`'s NCR 5380 controller, accepts SCSI commands and reads/writes a **complete block-device image**. The target must be able to mount the *same byte-for-byte disk image* intended to be used later on the physical M0001AP through BlueSCSI, without requiring an emulator-specific filesystem or ROM patch.

**Details**

### Ownership, dependencies and baseline

- **Kind:** `TASK` — roadmap-planned SCSI target and image support, not an independent regression.
- **Category:** `PLUS-EMU`; **Roadmap ID:** `PLUS-EMU-0005` under `PLUS-DEV-0009`.
- **Primary dependency:** [PLUS-EMU-0004](PLUS-EMU-0004.md), NCR 5380 Macintosh Plus initiator/controller and a defined SCSI bus/target attachment contract. An isolated target can be developed/tested independently before integration.
- **Related, outside scope:** `PLUS-SYS-0006` (Tima/Plus SCSI boot/driver and image preparation), plus ROM/IWM authenticity milestones. Keep the established **floppy-first** roadmap sequencing; opening this ticket does not itself approve or prioritize implementation.
- **Inspected baseline:** `keroblabs/tima_plus`, `main`, 18 November 2026, static inspection. `devel/umac/src/unix_main.c` accepts `-d` for a floppy image and `-w` for that floppy's persistent writes; `devel/umac/src/disc.c` / `include/disc.h` implement a paravirtual .Sony path. No emulated SCSI hard-disk command target or SCSI-disk image mount exists. No runtime tests performed for this ticket.

### Architecture — the target is separate from the controller and host filesystem

```text
 MC68000 -> Plus MMIO decoder -> NCR 5380 initiator [PLUS-EMU-0004]
                                      |
                            virtual SCSI bus
                         (selection, REQ/ACK,
                          phases, status, msg)
                                      |
                              SCSI target ID 1
                        [PLUS-EMU-0005: disk]
                         | command / sense |
                         | block interface |
                                      |
                          raw device image file
                          e.g. HD10_512.hda
```

- Define or consume a clear **SCSI target-facing bus contract** in concert with `PLUS-EMU-0004`. The controller owns *Mac CPU register and A9-selected pseudo-DMA access*; the bus mediates selection, phases and REQ/ACK; the target owns SCSI command interpretation, device status/sense and block I/O. Name the owner of each transition to avoid double handshakes.
- A target must be reached **only** by guest-originating SCSI bus transactions, never by a direct 68K trap, ROM hook, memory-to-disk copy, or special-case synthetic success in the 5380 controller.
- The target has **no direct access to guest RAM**, CPU address space, CAS PAL or NCR registers. No host DMA or separate Apple SCSI interrupt is required. Device transfers move bytes through the controller's CPU-driven PIO/pseudo-DMA paths.
- Start with **one attached hard disk**, default **SCSI ID 1 / LUN 0**; host initiator is normally ID 7. Provide a documented configuration path for valid target IDs (0–6), with explicit rejection of duplicate/out-of-range IDs. Keep the bus/target interface extensible for further IDs later; multiple simultaneous drives are **not required for initial acceptance**.
- Use deterministic emulator time and controllable, bounded command/phase state. Do not depend on wall-clock sleeps for emulated status/handshake completion.

### SCSI direct-access device behaviour

- Parse **6-byte and 10-byte Command Descriptor Blocks** (CDBs), decode LUN where applicable, maintain command context, and return correct **DATA IN / DATA OUT / STATUS / MESSAGE IN** phases and completion (including COMMAND COMPLETE); handle bus reset, aborted/incomplete command and selection/disconnection as supported by the agreed bus model.
- Minimum commands for a useful disk model, with exact response fields, lengths and completion semantics tested:

| Opcode | Command | Required target behaviour |
| --- | --- | --- |
| `0x00` | TEST UNIT READY | Ready vs missing/unavailable medium status. |
| `0x03` | REQUEST SENSE | Return meaningful sense key/ASC/ASCQ for previous CHECK CONDITION; verify clearing rules. |
| `0x12` | INQUIRY | Direct-access fixed disk identity; consistent vendor/product/revision, standard response and allocation lengths. |
| `0x08` / `0x0A` | READ(6) / WRITE(6) | Byte-accurate 512-byte sectors, 21-bit READ(6) LBA, transfer-length 0 meaning 256 blocks. |
| `0x25` | READ CAPACITY(10) | Big-endian **last valid LBA** and logical block length (512), not total sector count. |
| `0x28` / `0x2A` | READ(10) / WRITE(10) | LBA and transfer lengths, including 0 length meaning no blocks; multi-sector transfers and bounds. |
| `0x1A` | MODE SENSE(6) | Initial compatible subset of mode pages/geometry and write-protect indication; unsupported page handling must return CHECK CONDITION with sense. |

- Investigate legacy Macintosh driver requirements before promoting optional commands to mandatory: `MODE SELECT(6)`, `START STOP UNIT`, `REZERO UNIT`, `SEEK(6)`, `FORMAT UNIT` and additional mode pages. Unsupported opcodes/fields **must not** silently report GOOD; return meaningful CHECK CONDITION / ILLEGAL REQUEST or an appropriate protocol error. Do not claim complete SCSI-1/SCSI-2 conformance.
- Implement status/sense lifecycle: GOOD and CHECK CONDITION, not-ready and no-medium, illegal opcode/field, invalid LBA, write-protected medium and failed/short host I/O. Handle REQUEST SENSE following an error, reset and optional UNIT ATTENTION behaviour compatibly with observed Macintosh driver use.
- Validate image bounds and integer overflow before accessing storage. Correctly process multi-block transfers without requiring a full-disk RAM allocation; preserve in-order block contents and explicit end-of-transfer handling.
- **No fake SCSI device identity solely to satisfy a particular ROM**: configuration may choose a compatibility profile, but record what bytes and quirks are guest-visible and why.

### Backing image and BlueSCSI interoperability

- Use a **raw whole-device image** with sector 0 at file offset 0 and sector N at byte offset `N * 512`; no uMac-specific header, footer, geometry overlay, hidden sectors, host-native filesystem representation or sector remapping.
- The initial supported medium has **512-byte logical sectors**, nonzero whole-sector file length and a validated capacity. Keep size limits explicit (host file-size API and SCSI command limits). Validate missing file, empty file, non-multiple-of-512 size, invalid permissions and truncated reads deterministically.
- BlueSCSI convention example: **`HD10_512.hda`** means disk target **ID 1**, **LUN 0**, **512-byte sectors**. The file name is a *host-side attachment convention*, not bytes stored inside the image. An explicit uMac CLI/configuration may map an arbitrary path to a target ID; optionally recognize BlueSCSI naming but avoid requiring it for core logic.
- A **Macintosh SCSI device image** includes its disk driver and partition map as well as one or more partitions (e.g. HFS). An HFS-only *volume image* is not an interchangeable physical-disk image. The **emulated SCSI disk reads the image as opaque blocks**; it does not synthesize Apple driver/partition structures, parse HFS to service commands, or patch ROM/guest OS.
- **Writable persistence** should be explicit, with a read-only mode that correctly advertises and enforces data protection. Writes committed by a successful command must survive emulator shutdown/restart, with deterministic flush/close and host I/O failure propagation; no silent discard or immutable MAP_PRIVATE masquerading as writable storage. Make backup/copy use for integration tests clear.
- An image-construction/partition/driver tool is **outside this target's scope** and belongs with `PLUS-SYS-0006` / separate approved tooling; the target must faithfully consume such images regardless of creator.

### Integration / candidate module boundaries (not fixed API)

- `devel/umac/include/scsi_disk.h`, `devel/umac/src/scsi_disk.c`: per-target state, command decode, sense/status and block translation.
- `devel/umac/include/scsi_block.h` (and optional source): host-independent block backend operations such as read/write blocks, flush, capacity and readonly; inject a memory-backed fake in tests.
- `devel/umac/src/unix_main.c`: mount/unmount, explicit separate SCSI image path, ID and persistence options. Keep existing `-d` and `-w` floppy behaviour intact.
- `devel/umac/src/scsi_bus.*` / `scsi5380.*` from `PLUS-EMU-0004`: attach the disk as a target and perform real bus-phase transactions.
- `devel/umac/CMakeLists.txt` and `devel/umac/tests/`: build wiring and deterministic headless command/protocol/image tests.

Avoid coupling target code to SDL, Unix-only syscalls, `disc_pv_hook()`, `.Sony` ROM replacement or a specific host mapping style. A memory-backed block backend plus a Unix file backend is sufficient initially. Preserve a portable integration boundary for other emulated hosts.

### Acceptance checks — evidence required

- [ ] Clean uMac build plus existing patched-ROM/floppy display/input regressions; existing `-d` / `-w` behaviour unchanged.
- [ ] Target can be mounted/configured at ID 1/LUN 0 by default, selected by the NCR 5380 bus model, and accessed solely through guest register/PIO/pseudo-DMA transactions; no paravirtual shortcut.
- [ ] Headless unit tests verify command CDB decoding, INQUIRY fields, TEST UNIT READY, REQUEST SENSE, MODE SENSE subset, READ CAPACITY and READ/WRITE(6)/(10) transfer lengths, data phases, status and command-complete message.
- [ ] An initialized test image proves precise sector 0, first/middle/last LBA, partial command, multiple-block transfers, endianness and out-of-bounds handling; zero-length and edge-length semantics independently checked.
- [ ] Real file-backed read/write persistence confirmed by **close, reopen and byte-for-byte comparison**; read-only mode prevents mutation and reports SCSI data-protect sense; malformed/absent/unwritable image errors never cause false success or out-of-bounds reads.
- [ ] With an emulator-target image mounted as `HD10_512.hda` (or equivalent explicit path/ID mapping), block 0 and representative further sectors equal their source bytes, and the resulting image remains **byte-for-byte valid for BlueSCSI media mounting**. Do not mistake naming alone for successful hardware boot.
- [ ] Integration tests exercise selection failure/absent target, reset during command, missing/short data, error sense then REQUEST SENSE, unsupported opcode, bus-phase mismatch and interrupted/mid-transfer operations with deterministic results.
- [ ] With a **legally obtained complete Macintosh SCSI device image**, an identified original Macintosh Plus ROM/SCSI Manager can detect/mount the drive and, where image/driver/System setup allows, boot from it. Record ROM revision/hash, image type/hash, observed behaviour and limitations. Mark **NOT TESTED** if required media are unavailable; do not claim boot success from synthetic block tests alone.
- [ ] Check that the same unmodified image can be put on a BlueSCSI SD card and recognized by a physical Macintosh Plus in a separate hardware validation, when available; if not performed, clearly mark it **NOT TESTED** rather than asserting physical boot compatibility.
- [ ] Implementation commits, tests, review/approval and exact remaining compatibility gaps are documented in this ticket before closure.

### Out of scope

Do **not** build a second NCR 5380, a CPU/DMA engine, physical BlueSCSI firmware, host filesystem shortcut, generic SCSI controller simulator, a SCSI CD-ROM/tape target, a Mac partition-map/HFS editor, copyrighted ROMs/drivers/System Software, or the Tima/Plus SCSI driver/bootstrap. The hard disk is a **target device connected to the controller**; its disk image is an opaque backing block store.

### References

- [PLUS-EMU-0004 — NCR 5380 host-controller ticket](PLUS-EMU-0004.md).
- [PLUS-DEV-0009 — SCSI/BlueSCSI milestone](../../01-development-roadmap.md#plus-dev-0009--migrate-to-scsibluescsi).
- [BlueSCSI v2 documentation — Disk Images](https://bluescsi.com/docs/Disk-Images): whole-device images vs HFS-only volume images and Macintosh Plus driver compatibility.
- [BlueSCSI v2 documentation — Usage](https://bluescsi.com/docs/Usage): target/LUN/sector-size filename convention.
- [BlueSCSI v2 documentation — supported command comparison](https://bluescsi.com/docs/Developers-(Advanced)): historical command-set comparison; verify required commands against original ROM/driver traces.
- [NCR 5380 SCSI Interface Chip Design Manual (1985)](https://ftpmirror.your.org/pub/misc/bitsavers/components/ncr/scsi/NCR_5380_SCSI_Interface_Chip_Design_Manual_May85.pdf): controller-side data/phase handshakes.

**Notes**

- **2026-11-18 — Scope decision:** Following the NCR 5380 address-selected pseudo-DMA discussion, the user identified the missing **actual disk**: add an emulated SCSI hard disk connected to the emulator's SCSI bus. Use the preallocated `PLUS-EMU-0005` ID and keep `PLUS-EMU-0004` host-controller-only. Specify a whole-device BlueSCSI-compatible image so the later hardware can use the same bytes.
- **2026-11-18 — Investigation:** Static inspection finds the existing uMac disk implementation is the patched `.Sony` floppy paravirtual interface, not a SCSI target. Checked BlueSCSI documentation for ID/LUN filename convention and Macintosh whole-device image requirements. No implementation, build or end-to-end original-ROM test was performed.

**Proposed Change**

**For review/approval before implementation:**

1. Agree a minimal initiator–bus–target contract jointly with `PLUS-EMU-0004`, including target ID, phase ownership, command/data/status/message lifetime and reset.
2. Implement a standalone SCSI fixed-disk target using a memory-backed block backend and tests for required 6/10-byte CDB commands, errors, sense, multi-sector transfer and read-only semantics.
3. Implement file-backed opaque 512-byte whole-device image mounting with explicit persistence, bounds and error reporting; keep its CLI options separate from current paravirtual floppy parameters.
4. Attach the target at configurable ID (default 1) to the bus; test *through guest-visible NCR 5380 registers and pseudo-DMA/PIO*, not by invoking target CDB entrypoints only.
5. Validate byte-for-byte BlueSCSI image interoperability and, when lawful original ROM/media are available, boot/mount behaviour through the original Macintosh Plus SCSI Manager. Record any additional command/quirk requirements observed, and only extend the supported command subset deliberately.
6. Review results against the acceptance list and retain `PLUS-SYS-0006` for creating/installing Tima/Plus images, drivers and boot logic.

**Alternative rejected:** feed HFS filesystem calls or host block reads directly to the ROM/SCSI Manager as a paravirtual device. That would skip SCSI bus/controller semantics and undermine the required same-image-on-hardware test.

**Open questions for implementation:** exact target/bus interface after `PLUS-EMU-0004` is reviewed; minimum classic-Mac compatible mode-page/profile responses; which legally sourced device image and 5380/ROM revision to use for end-to-end smoke testing. These do not justify conflating the controller and disk tickets.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |

**Resolution Description**

Pending.
