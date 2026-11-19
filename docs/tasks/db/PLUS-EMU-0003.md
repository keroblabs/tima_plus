# PLUS-EMU-0003 — Implement unmodified-ROM read-only IWM and virtual floppy

**ID:** `PLUS-EMU-0003`
**Status:** `OPEN`
**Title:** Implement unmodified-ROM read-only IWM and virtual floppy

**Description**

uMac currently boots floppy media through a patched Macintosh Plus ROM and a host-side paravirtual `.Sony` replacement. Its IWM memory-mapped registers are mostly placeholders, so the **unmodified** ROM's actual `.Sony` driver cannot yet read a normal 800K floppy image through authentic IWM operations.

This first implementation phase establishes the **IWM register/timing behaviour** plus an independent **virtual disk/drive model**, fed by a logical `.dsk` image and a circular encoded track bitstream. It is deliberately **read-only**; the write data path is **stubbed**, without modifying either the buffer or the disk image. Successful original-ROM boot is the release gate.

**Details**

- Kind: `TASK`
- Category: `PLUS-EMU`
- Primary milestone: [PLUS-DEV-0003](../../01-development-roadmap.md#plus-dev-0003--run-the-unmodified-macintosh-plus-rom); read-only prerequisite for [PLUS-DEV-0004](../../01-development-roadmap.md#plus-dev-0004--complete-the-virtual-800k-floppy-subsystem).
- Architecture: [04-iwm-emulation.md](../../04-iwm-emulation.md), especially §§1–2, 4–6 and 8.
- Dependent follow-on: [PLUS-EMU-0010](PLUS-EMU-0010.md) implements writing and persistence; **not part of this ticket**.
- Relevant current paths: `devel/umac/src/main.c` (stub IWM), `devel/umac/include/machw.h` (address decode), `devel/umac/src/rom.c` and `devel/umac/src/unix_main.c` (ROM patching), `devel/umac/src/disc.c` (legacy paravirtual disk), `devel/umac/src/via.c` (timing/drive signals), `devel/umac/CMakeLists.txt` and tests. Suggested new components `devel/umac/src/iwm.c` + `include/iwm.h`; `src/floppy_drive.c` + `include/floppy_drive.h`; exact names can be revised by review.
- Evidence (inspected): `main.c` has `iwm_regs[16]`, hardcoded IWM reads and no complete controller; `unix_main.c` calls `rom_patch()`; `rom.c` patches the original ROM/`.Sony`. This is a missing capability, not a completed implementation.
- Primary reference: [original Macintosh Plus ROM `.Sony` disassembly](https://github.com/jonathanschilling/mac_rom/blob/main/plus/resources/res_drvr_sony.s); Apple IWM/SWIM specifications for mode, latch and soft-switch semantics; [MAME IWM](https://github.com/mamedev/mame/blob/master/src/devices/machine/iwm.cpp) for comparison only.

### Component A — IWM software-visible behaviour

- Decode all sixteen soft switches and correct Q6/Q7/motor/drive-selection side effects for both reads and writes; mode `$1F` and status/SENSE responses required by the original ROM.
- Model the **read shift register, valid-byte latch, read acknowledgement/rearming** and operating mode. Bit 7 of a valid encoded codeword is what the ROM tests; there is no large FIFO and CPU polling does not directly advance the media stream.
- Advance a monotonic virtual C8M timeline, nominally 7,833,600 ticks/s with 16 ticks per bit cell and 128 per ordinary 8-bit encoded codeword. Account for independent disk progression, missed reads and synchronization fields requiring bit-level phase. Measure Musashi memory-access timing; do not assume a memory callback is instruction-end or that RAM bus arbitration adds no waits.
- Drive-control output and multiplexed status/SENSE inputs communicate with the virtual disk through a clear interface. Support motor, drive selection, head/side, track stepping, disk inserted, write protected, track-zero and tachometer behaviour **as used by the original ROM**, with documented timing approximations.
- **Write stub only:** allow safe Q6/Q7/write-state transitions required by startup, but do not implement the timed write serializer, write-buffer-empty/underrun completion or actual track writes. Present the attached disk as write-protected so the original ROM avoids ordinary writes. If it nevertheless attempts a write, reject/log it and do not imply durable success.

### Component B — read-only virtual disk/drive

- Open a valid `819200`-byte logical 800K `.dsk` and reject unsupported sizes/formats clearly; preserve byte-for-byte image contents throughout this ticket.
- For the selected (track, side), load its 512-byte sectors using zone geometry 12/11/10/9/8 sectors over five 16-track bands, and the correct logical/physical sector ordering.
- Implement the Macintosh 6-and-2 packing and GCR mapping, data/address fields, tags/checksums, prologues, epilogues, synchronization and gaps. Match expected ROM sector format; do not assume eight-bit alignment in ten-bit sync.
- Build a **circular bitstream** with a well-defined bit length/rotation position. Read from it according to elapsed virtual IWM ticks, not on demand from a requested sector number or from `PV_SONY_ADDR`. Rebuild/select buffers when stepping tracks or changing side; optionally cache more than one track/side.
- No mutations, dirty flags requiring persistence or writes to `.dsk` during this phase. Expose a stable disk/timing API that Phase 2 can extend without breaking IWM reads.

### Unmodified-ROM requirement

- **Disable all `rom_patch()` effects** in authentic mode (not just the host `.Sony` entrypoint replacement); load and execute an **unmodified, identified Macintosh Plus ROM** with its original `.Sony` resource. Verify ROM bytes/checksum are unchanged before/after load.
- Make authentic mode the normal tested path; the existing paravirtual patched implementation may be kept behind an **explicit opt-in legacy regression mode**, never counted as an authentic result.
- Preserve already working SCSI, RTC/PRAM, display, keyboard/mouse and basic emulator behaviour. Do not silently replace storage calls with host `disc_pv_hook()` in authentic mode.
- Keep implementation/tests and corresponding ticket history **locally committed**, with **no `git push` by developer**, per [methodology](../bug-tracking-methodology.md).

### Phase 1 acceptance checks

| ID | Test | Required evidence |
|---|---|---|
| `EMU3-A1` | Original ROM and IWM initialization | Unmodified ROM hash/bytes recorded; no `rom_patch()` in authentic mode; mode and soft-switch traces match actual `.Sony` accesses |
| `EMU3-A2` | Deterministic read-only unit tests | IWM Q6/Q7, SENSE, mode `$1F`, latch clear/rearm, missed codeword, reset, track/side selection and 16-tick bit-cell progression |
| `EMU3-A3` | Disk encoder and circular stream | Known logical sectors produce correct address/data GCR records, checksums and ten-bit sync; track zones, circular wrap and bit positions tested |
| `EMU3-A4` | Read-only medium protection | Guest sees write-protected disk; no code path mutates buffer/media file; `.dsk` hash unchanged after run and rejected/stubbed writes |
| `EMU3-A5` | **System Software boot** | Unmodified ROM, original `.Sony` driver and known-good 800K image read sectors across sides/zones and boot original System Software; record command, media hash, ROM identity and observed result |
| `EMU3-A6` | Regression | Existing explicit patched mode (if retained), SCSI, RTC/PRAM and representative emulator tests pass or have documented, approved exceptions |

**Phase 1 exclusion:** write serialization, write handshake/underrun correctness, dirty-track flushing, formatting and persistent mutations belong to `PLUS-EMU-0010`. A synthetic-controller test alone or a patched-ROM boot does not close `PLUS-EMU-0003`.

**Notes**

- **2026-11-19 — Decision and registration:** Project owner approved splitting the authentic IWM + disk implementation into two components and two phases. This ticket covers Phase 1 read-only, **including disabling ROM patching**, with writing explicitly stubbed. The follow-on write ticket is `PLUS-EMU-0010` because `PLUS-EMU-0009` is already reserved/closed for RTC and PRAM. No code changes or tests were performed when this ticket was opened.

**Proposed Change**

Implement the above components in separate source/header pairs with a narrow timed-bit/drive-signal boundary; disable ROM patching in authentic mode; synthesize circular 800K encoded tracks from read-only logical `.dsk` sectors; meet the original ROM read-only boot and regression acceptance matrix. Keep the write path stubbed until the dependent write ticket is authorized. Developer supplies local `git commit` SHA(s) and evidence, **without `git push`**. Review against the original ROM, Apple IWM specification and project methodology before acceptance.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |

**Resolution Description**

Pending.
