# PLUS-EMU-0005 — Emulate a SCSI hard disk backed by a BlueSCSI-compatible image

**ID:** `PLUS-EMU-0005`  
**Status:** `REVIEW`\
**Title:** Emulate a SCSI hard disk backed by a BlueSCSI-compatible image

**Description**

The Macintosh Plus emulator needs a **real SCSI target device model attached to its emulated SCSI bus**, not merely the NCR 5380 host controller. The inspected uMac baseline had no SCSI disk target. `devel/umac/src/disc.c` remains a ROM-patched/paravirtual **floppy** driver that bypasses SCSI hardware and must not be reused as a SCSI command shortcut.

Implement a **lightweight direct-access SCSI target** attached behind `PLUS-EMU-0004`'s register-visible controller. Decode a completed command, read one sector from the image into a reusable **512-byte buffer**, and allow firmware to consume it **one byte per completed handshake** (or reverse the flow for a write). No dedicated bus/wire simulation or multiple-target framework is needed. The target must be able to mount the *same byte-for-byte disk image* intended to be used later on the physical M0001AP through BlueSCSI, without requiring an emulator-specific filesystem or ROM patch.

**Details**

### Ownership, dependencies and baseline

- **Kind:** `TASK` — roadmap-planned SCSI target and image support, not an independent regression.
- **Category:** `PLUS-EMU`; **Roadmap ID:** `PLUS-EMU-0005` under `PLUS-DEV-0009`.
- **Primary dependency:** [PLUS-EMU-0004](PLUS-EMU-0004.md), NCR 5380 Macintosh Plus initiator/controller and a defined SCSI bus/target attachment contract. An isolated target can be developed/tested independently before integration.
- **Related, outside scope:** `PLUS-SYS-0006` (Tima/Plus SCSI boot/driver and image preparation), plus ROM/IWM authenticity milestones. Keep the established **floppy-first** roadmap sequencing; opening this ticket does not itself approve or prioritize implementation.
- **Inspected baseline:** `keroblabs/tima_plus`, `main`, 18 November 2026, static inspection. `devel/umac/src/unix_main.c` accepts `-d` for a floppy image and `-w` for that floppy's persistent writes; `devel/umac/src/disc.c` / `include/disc.h` implement a paravirtual .Sony path. No emulated SCSI hard-disk command target or SCSI-disk image mount exists. No runtime tests performed for this ticket.

### Architecture — the minimal command-and-sector model

```text
 MC68000 -> Mac Plus address decoder -> NCR 5380 register interface (EMU-0004)
                                            |
                                    shared SCSI state
                                 phase + REQ/ACK owner
                                            |
                                   one hard-disk target
                                    SCSI ID 1 / LUN 0
                                  CDB + sector[512]
                                            |
                                    raw image backend
                                    HD10_512.hda
```

This is **one lightweight SCSI subsystem**, possibly one compiled module, with **two logical responsibilities**: NCR firmware-facing registers/handshakes (`PLUS-EMU-0004`), and target CDB decoding, status/sense and image I/O (this ticket). No separate `scsi_bus.c`, physical signal simulation, disk-driver firmware or generic device framework is mandatory.

- The Macintosh normally acts as initiator ID 7. **Keep one configurable disk target**, default ID 1/LUN 0. On a valid selection of ID 1, the target responds with BSY; other IDs remain absent so the guest's probing/timeout works. There is **no SCSI device-list register** to implement.
- On selection, transition to **COMMAND** and set `command_pos = 0`. Each *completed* REQ/ACK byte transfer appends one command byte. The **first** byte is identified by COMMAND state plus position zero; its opcode determines the CDB length (e.g. READ(6) = 6 bytes). Only dispatch when all CDB bytes have been accepted, not when firmware merely writes the NCR output register.
- The target's internal operations must not access guest RAM, CPU address space, NCR registers, PAL state or ROM traps. For DATA IN and DATA OUT, advance sector byte position **only when the controller reports a completed valid PIO/pseudo-DMA handshake**; register polling/observation never consumes data. One shared phase owner prevents duplicate advancement.
- Ordinary bytes may become logically ready immediately; any guest-visible not-ready state, reset, selection timeout or error must remain deterministic. Do not introduce host sleeps or an idealised hardware DRQ stall absent on the real Plus.

### Concrete example — READ(6), LBA 100, one sector

1. Firmware selects target ID 1 and sends six COMMAND-phase bytes `08 00 00 64 01 00`, each accepted through REQ/ACK.
2. After the sixth byte, the disk decodes READ(6), LBA `100`, block count `1`, checks capacity, and reads 512 bytes at image offset `100 * 512` into `uint8_t sector[512]`. Report CHECK CONDITION / sense on bounds or I/O failure.
3. Enter DATA IN; each **valid CPU byte transfer** returns `sector[data_pos]`, incrementing `data_pos` **once**. 512 transfers produce exactly the 512 bytes; repeated status/data-bus observations do not advance.
4. When byte 511 has been acknowledged, return GOOD status (`0x00`), COMMAND COMPLETE message (`0x00`), then BUS FREE.
5. For multiple blocks, load the next 512-byte sector into the **same buffer** after the previous sector's last byte. A READ(6) transfer count of **zero means 256 blocks**. No full-image or whole-command allocation.
6. For WRITE(6), receive bytes into the same fixed buffer, write one sector after its final byte, then continue as needed. Report read-only and host errors accurately; no silent loss of data.

### Minimal command support — staged by real ROM need

SCSI commands are decoded in the **target**, never in the NCR 5380 module. Prefer **six-byte CDB commands first** and add ten-byte CDBs as the original ROM/driver trace or later requirements demonstrate. Recognise group-dependent CDB lengths safely; unsupported/reserved opcodes do not silently succeed.

| Command | Initial importance |
| --- | --- |
| `TEST UNIT READY (0x00)` | Core readiness/no-medium. |
| `REQUEST SENSE (0x03)` | Core error reporting after CHECK CONDITION. |
| `READ(6) (0x08)` | Core buffered 512-byte sectors, multiblock/count-0=256. |
| `WRITE(6) (0x0A)` | Core inverse byte flow and persistent image writes. |
| `INQUIRY (0x12)` | Core direct-access device identity/length. |
| `MODE SENSE(6) (0x1A)` | Current header and block descriptor advertise write protection and 512-byte blocks. Add mode pages only when needed. |
| `READ CAPACITY(10) (0x25)` | Add when traced/needed; report **last valid LBA**, not block count. |
| `READ(10)/WRITE(10) (0x28/0x2A)` | Add when traced/needed; zero block count means no transfer. |

Other opcodes including MODE SELECT, START STOP, SEEK, REZERO and FORMAT UNIT need not be implemented before evidence; unsupported commands return CHECK CONDITION and meaningful sense instead of GOOD. The initial subset is **not** a claim of complete SCSI-1/SCSI-2 compliance. Implement correct STATUS, MESSAGE IN / COMMAND COMPLETE, reset/abort, bounds, readonly, failed I/O and sense lifecycle for commands actually supported. **A working READ(6) unit test alone is not evidence that the original Macintosh can boot.**

### Backing image and BlueSCSI interoperability

- Use a **raw whole-device image** with sector 0 at file offset 0 and sector N at byte offset `N * 512`; no uMac-specific header, footer, geometry overlay, hidden sectors, host-native filesystem representation or sector remapping.
- The initial supported medium has **512-byte logical sectors**, nonzero whole-sector file length and a validated capacity. Keep size limits explicit (host file-size API and SCSI command limits). Validate missing file, empty file, non-multiple-of-512 size, invalid permissions and truncated reads deterministically.
- BlueSCSI convention example: **`HD10_512.hda`** means disk target **ID 1**, **LUN 0**, **512-byte sectors**. The file name is a *host-side attachment convention*, not bytes stored inside the image. An explicit uMac CLI/configuration may map an arbitrary path to a target ID; optionally recognize BlueSCSI naming but avoid requiring it for core logic.
- A **Macintosh SCSI device image** includes its disk driver and partition map as well as one or more partitions (e.g. HFS). An HFS-only *volume image* is not an interchangeable physical-disk image. The **emulated SCSI disk reads the image as opaque blocks**; it does not synthesize Apple driver/partition structures, parse HFS to service commands, or patch ROM/guest OS.
- **Writable persistence** should be explicit, with a read-only mode that correctly advertises and enforces data protection. Writes committed by a successful command must survive emulator shutdown/restart, with deterministic flush/close and host I/O failure propagation; no silent discard or immutable MAP_PRIVATE masquerading as writable storage. Make backup/copy use for integration tests clear.
- An image-construction/partition/driver tool is **outside this target's scope** and belongs with `PLUS-SYS-0006` / separate approved tooling; the target must faithfully consume such images regardless of creator.

### Integration boundary — compact source, complete observable behaviour

The controller and disk can share a **single compiled SCSI subsystem** with a minimal internal transaction/byte interface; use separate `scsi_disk.c` or block helper only if it improves readability/testing. Do not create standalone physical bus/signal libraries just to mirror chips. Keep the existing `devel/umac/src/disc.c`/.Sony paravirtual floppy path unchanged.

- Host-side `unix_main.c` attaches a separate SCSI image (path, target ID, read-only/persistent option); leave existing `-d`/`-w` floppy flags alone.
- A small block I/O helper exposes read-sector, write-sector, capacity and flush; unit tests can substitute a memory-backed image. All SCSI opcode/sector handling stays on the target side, not in NCR register callbacks.
- Headless tests exercise the target alone **and** through NCR guest-visible MMIO; preserve existing ROM/floppy regression tests.

### Acceptance checks — staged, not full bus conformance

- [x] Build and patched-ROM floppy/display/input smoke tests pass; `-d` / `-w` still select the independent floppy path.
- [x] One configured disk at target ID 1/LUN 0 responds to a valid selection; absent IDs do not respond. No invented ID discovery register or multiple-target engine.
- [x] COMMAND entry resets CDB position. Only six completed REQ/ACK handshakes deliver six READ(6) bytes; duplicate output-register writes or polling do not append bytes.
- [x] CDB `08 00 00 64 01 00` loads exactly 512 bytes from image LBA 100 / offset `100 * 512`; 512 valid guest CPU reads return exactly those bytes, then GOOD → COMMAND COMPLETE → BUS FREE. Raw status/data observation never advances the buffer.
- [x] WRITE(6) commits 512 bytes per completed sector, including write protection and host errors. Tests cover multiblock I/O, a count-zero 256-block read, bounds and the last block.
- [x] Core TEST UNIT READY, INQUIRY, REQUEST SENSE and CHECK CONDITION/error pathways are tested. MODE SENSE(6) advertises write protection; no speculative mode pages or ten-byte commands are implemented.
- [x] Both regular PIO and A9-selected pseudo-DMA exercise a complete command/data/status/message transaction through the guest-visible 5380. Tests cover reset, invalid opcode, absent medium, not-ready, premature accesses and phase mismatches without bypassing registers or copying disk data into guest RAM directly.
- [x] Raw whole-device 512-byte-sector handling validates file size and permissions, read-only/persistent semantics, and write survival after closing/reopening. Tests compare raw file bytes at the written LBAs and their neighbors.
- [ ] Full detection/mount/boot from a partitioned device image in supported patched-ROM startup is **NOT TESTED**. Isolated unmodified V3 SCSI Manager read/write passed on generated raw media; authentic ROM startup is tracked separately.
- [ ] Physical BlueSCSI/M0001AP same-image testing is **NOT TESTED**.
- [ ] Record implementation commits, review approval, any ROM-required commands and remaining unsupported functionality before closure.

### Out of scope

Do **not** build a second NCR 5380, a full electrical SCSI bus, multi-initiator/target manager, CPU/DMA engine, physical BlueSCSI firmware, host filesystem shortcut, generic SCSI controller/device simulator, a SCSI CD-ROM/tape target, a Mac partition-map/HFS editor, copyrighted ROMs/drivers/System Software, or the Tima/Plus SCSI driver/bootstrap. The hard disk is a **target device connected to the controller**; its disk image is an opaque backing block store.

### References

- [PLUS-EMU-0004 — NCR 5380 host-controller ticket](PLUS-EMU-0004.md).
- [PLUS-DEV-0009 — SCSI/BlueSCSI milestone](../../01-development-roadmap.md#plus-dev-0009--migrate-to-scsibluescsi).
- [BlueSCSI v2 documentation — Disk Images](https://bluescsi.com/docs/Disk-Images): whole-device images vs HFS-only volume images and Macintosh Plus driver compatibility.
- [BlueSCSI v2 documentation — Usage](https://bluescsi.com/docs/Usage): target/LUN/sector-size filename convention.
- [BlueSCSI v2 documentation — supported command comparison](https://bluescsi.com/docs/Developers-(Advanced)): historical command-set comparison; verify required commands against original ROM/driver traces.
- [NCR 5380 SCSI Interface Chip Design Manual (1985)](https://ftpmirror.your.org/pub/misc/bitsavers/components/ncr/scsi/NCR_5380_SCSI_Interface_Chip_Design_Manual_May85.pdf): controller-side data/phase handshakes.

### Review Findings — round 1 (18 November 2026)

**Result:** Disk command/sector-image architecture accepted in principle; **verification still required**. No confirmed disk-command implementation defect was found in this static review, so missing integration evidence is not presented as a code failure.

| Finding ID | Classification | Evidence | Required action / acceptance evidence | State |
| --- | --- | --- | --- | --- |
| `EMU5-R1` | `VERIFICATION_REQUIRED` | Tests exercised an unmodified Plus V3 ROM's **low-level SCSI routines**; normal Macintosh startup has not detected, mounted or booted a SCSI device. | Obtain a lawful *whole-device* Macintosh SCSI image containing partition map, Mac disk driver and bootable System. In the **current supported uMac ROM startup mode**, attempt actual disk detection/mount/boot, recording ROM/image hashes, commands, phase/status traces and result. Original *unmodified-ROM startup* is part of the separate authentic ROM/IWM milestone and must not be silently introduced as an extra requirement of this ticket. | **Attempted with the supplied partitioned image; only the floppy mounted. Boot from a known-good SCSI image remains NOT TESTED.** |
| `EMU5-R2` | `VERIFICATION_REQUIRED` (conditional) | The target intentionally supports a limited command subset. READ CAPACITY(10), READ(10) and WRITE(10) have not been implemented; lack of unused commands alone is not a defect. | Use `EMU5-R1`'s driver command trace. Implement and test the smallest additional command/page *only if the actual boot/mount path requires it*; otherwise accept the documented limited command set. | **PENDING TRACE; no speculative expansion.** |
| `EMU5-R3` | `FUTURE_SCOPE` | Same-image operation on physical BlueSCSI/M0001AP and Tima/Plus OS bootstrap are later program milestones. | Preserve the opaque 512-byte whole-device image and track physical tests under `PLUS-DEV-0009` / OS support under `PLUS-SYS-0006`. | **OUT OF CURRENT EMULATOR ACCEPTANCE; physical test NOT TESTED.** |

**Review gate:** Attempt complete Macintosh SCSI device detection/mount/boot; record failures before adding features. Continue to reference the successful reported low-level ROM test as such, not as proof of full boot compatibility.

**Notes**

- **2026-11-18 — Scope decision:** Following the NCR 5380 address-selected pseudo-DMA discussion, the user identified the missing **actual disk**: add an emulated SCSI hard disk connected to the emulator's SCSI bus. Use the preallocated `PLUS-EMU-0005` ID and keep `PLUS-EMU-0004` host-controller-only. Specify a whole-device BlueSCSI-compatible image so the later hardware can use the same bytes.
- **2026-11-18 — Investigation:** Static inspection finds the existing uMac disk implementation is the patched `.Sony` floppy paravirtual interface, not a SCSI target. Checked BlueSCSI documentation for ID/LUN filename convention and Macintosh whole-device image requirements. No implementation, build or end-to-end original-ROM test was performed.


- **2026-11-18 — Scope reduction (user discussion):** The disk can be a compact command decoder + one reusable 512-byte sector buffer in the same compiled SCSI subsystem as the register interface. Upon COMMAND entry the first accepted handshake supplies opcode; CDB dispatch waits for the full command. READ(6) loads requested sector into that buffer, and the original firmware consumes one byte per valid PIO/pseudo-DMA handshake; multi-sector operations refill and reuse it. One configured target ID suffices, while selection failure for other IDs remains real. Prior generic bus/framework and speculative command requirements reduced; original-ROM trace still determines additional needed commands. No code changes or approval.

- **2026-11-18 — Decision / status change:** The user requested both implementations. Moved `OPEN` → `IN_PROGRESS` on `plus-emu-0004-0005-scsi` without rearranging the roadmap or changing the `.Sony` floppy path.
- **2026-11-18 — Implementation:** [`scsi_disk.c`](../../../devel/umac/src/scsi_disk.c) decodes completed CDBs and reuses a single 512-byte buffer. [`scsi_image.c`](../../../devel/umac/src/scsi_image.c) validates regular files (1–2,097,152 sectors, the READ(6) LBA limit), reads/writes at the raw offsets, and syncs successful WRITE(6) commands before GOOD. No rollback of previously completed sectors is promised. MODE SENSE(6) supplies the write-protect bit and an optional block descriptor, not mode pages. The frontend mounts with `-s`, read-only by default; `-p` explicitly permits persistence and `-t` sets ID 0–6 (default 1). Paths aliasing emulator RAM, ROM input/output or floppy are rejected before writes to those files.
- **2026-11-18 — Test:** Clean CMake build and [`umac_scsi_test disk`](../../../devel/umac/README.md#scsi-tests) passed. The test read LBA 100 over PIO, wrote LBA 98 over PIO and LBAs 100–101 over pseudo-DMA, closed/reopened and verified exact bytes in the file and through guest MMIO. LBA 99 stayed unchanged when reset interrupted the last ACK of its write. A count-zero read returned 256 blocks ending at the last block. Tests also exercised readiness, INQUIRY, sense consumption, write protection, bounds, invalid CDBs, truncated reads, write/sync errors, malformed media and a target with no medium. Offscreen SDL smoke also passed with `-d`/`-w` and `-s`/`-t`/`-p` on disposable copies; the original fixtures were not modified.
- **2026-11-18 — Original firmware test:** The unmodified Plus V3 low-level SCSI Manager routines wrote and read back 512 bytes at LBA 100 after the raw image was closed and reopened read-only, with GOOD and COMMAND COMPLETE both times. See the Notes in [PLUS-EMU-0004](PLUS-EMU-0004.md) for the ROM hash and actual register trace. Only a generated, unpartitioned device image was used; full detection/mount/boot and physical BlueSCSI/M0001AP compatibility are **NOT TESTED**.
- **2026-11-18 — Status change:** `IN_PROGRESS` → `REVIEW`. The implementation and evidence are ready for review, not yet approved or resolved.

- **2026-11-18 — Independent code review / verification pending:** Static review of branch at `0318e03`; developer's command, persistence and original Plus V3 low-level SCSI test results were not independently rerun. No concrete disk command defect was found. `EMU5-R1` remains **NOT TESTED** for real disk detection/mount/boot with a bootable whole-device image; `EMU5-R2` requires additional opcodes only when actual ROM/driver traces demonstrate a need. Physical BlueSCSI is later scope (`EMU5-R3`). Set commit `179b13a` to `IN_REVIEW` and retain ticket `REVIEW`; no claim of resolution or source changes.
- **2026-11-18 — Test media investigation (`EMU5-R1`):** The only local System image, `System70_boot.dsk`, is an HFS floppy image with `LK` boot blocks, not a whole-device image with a partition map and disk driver. No suitable lawful, known-good bootable device image was available. Did not fetch an unofficial System image or substitute the floppy for a SCSI device.
- **2026-11-18 — Startup attempt (`EMU5-R1`):** Ran the supported `.Sony`-patched CLI offscreen with `-r <Plus V3 ROM> -s HD10_512.hda`, read-only and no `-d`. ROM input SHA-256 `dd908e2b65772a6b1f0c859c24e9a0d3dcde17b1c6a24f4abd8955846d7895e7`; synthetic all-zero 300-sector image SHA-256 `0c5cc90b079d0d9c1ded1376357d23a9782a704a83e01731f50ccd162e246492` (unchanged). The emulator reported `DISC: EJECT` and exited 1 before selecting the SCSI target. The legacy [`SonyOpen()`](../../../devel/umac/src/disc.c) reports a floppy inserted without backing media; its eject callback terminates the SDL build. A bounded diagnostic run skipping only that exit in the debugger observed the initial controller reset but no CDB, data, status or message phase for 5 seconds. This is a startup blocker, not evidence that any SCSI opcode or a bootable image failed. The `.Sony` path was not changed in this rework.
- **2026-11-18 — Regression / disposition:** The native disk and unmodified Plus V3 SCSI Manager tests passed on the corrected controller revision. No driver CDB trace justified adding commands or mode pages, so `EMU5-R2` remains pending. Keep `179b13a` `IN_REVIEW` and the ticket `REVIEW`. Full boot/mount and physical BlueSCSI remain **NOT TESTED**.
- **2026-11-18 — Supplied media (`EMU5-R1`):** The user supplied `320_32MB_drive.hda` and directed use of `System70_boot.dsk`. The hard disk is a 512-byte-sector Apple whole-device image with an Apple Partition Map, `Apple_Driver43` at LBA 64, and HFS at LBA 96. Its HFS boot block starts with `00 00`, not `4c 4b` (`LK`), so it was not treated as a known-bootable System image. Hard-disk SHA-256 `fd03fdc74308848200c18d4690ef2db8159df7c650255e826ab56a3779236481`; floppy SHA-256 `19819ffcd4d9d07cb9a0c68b4acf2993485cbc2182ea9f98e7d7a65179667d54`; Plus V3 ROM input SHA-256 as in the previous attempt. Both images were used without persistent writes.
- **2026-11-18 — Supported startup with supplied media:** Ran patched Plus V3 with `-r rom.bin -d System70_boot.dsk -s 320_32MB_drive.hda` and no `-p` or `-w`. The 12-second offscreen debugger trace observed the initial NCR reset but no selection or CDB. The guest volume queue held only `System 7.0` on the `.Sony` driver (drive 1, refnum −5); no SCSI volume mounted or booted. The earlier no-floppy exit remains a separate startup finding; this run did not bypass it in code or in the debugger.
- **2026-11-18 — Supplied-image I/O diagnostic:** Isolated Plus V3 SCSI Manager routines selected ID 1 and issued `08 00 00 <LBA> 01 00` for LBAs 0, 1, 64 and 98, covering the driver map, partition map, driver and HFS metadata. For each `READ(6)`, 512 DACK reads matched the raw sector byte-for-byte, followed by STATUS `00`, MESSAGE IN `00` and BUS FREE. This confirms read-only access to the supplied image, not startup detection or mounting. No actual startup driver CDB justifies expanding the command set, so `EMU5-R2` stays pending and both tickets stay in `REVIEW`.

**Proposed Change**

**Review candidate — one disk target, fixed sector buffer:**

1. Agree the minimal completed-byte handshake and phase transition contract with `PLUS-EMU-0004`; retain a single transaction owner and configurable target ID 1, without an electrical bus simulator.
2. Implement COMMAND-phase CDB collection (first-byte opcode → CDB length) and a 512-byte reusable buffer for READ(6) / WRITE(6); prove LBA 100 flow through CPU register accesses and status/message completion.
3. Add essential readiness, INQUIRY, sense/error handling, whole-device image file backend and persistent/read-only options; test multiblock buffering without whole-image copies.
4. Trace legitimate Mac Plus ROM/SCSI Manager driver use, implementing additional commands (e.g. MODE SENSE / READ CAPACITY / READ(10)) only as evidence requires; unsupported opcodes return proper errors.
5. Verify whole-device byte fidelity and boot/mount in supported patched-ROM startup with suitable media; retain authentic ROM startup under `PLUS-EMU-0003` and OS image/driver creation under `PLUS-SYS-0006`.

**Rejected:** paravirtual disk calls, guest-RAM shortcuts, a simulated electrical SCSI bus, or a generic multi-device framework as an initial requirement. Those increase scope without improving the specified Plus firmware-visible behaviour.

**Open verification:** triage the legacy empty-floppy startup exit, obtain a lawful bootable whole-device image, and capture the supported startup path's actual driver commands and mount/boot result. Physical BlueSCSI interchange remains later scope.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |
| `plus-emu-0004-0005-scsi` | `179b13a` | `IN_REVIEW` | Attach sector-buffered disk and image backend; full ROM-managed device boot evidence pending (`EMU5-R1`). |

**Resolution Description**

Pending.
