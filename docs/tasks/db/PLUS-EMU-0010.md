# PLUS-EMU-0010 — Add IWM write handling and persistent virtual floppy writes

**ID:** `PLUS-EMU-0010`
**Status:** `OPEN`
**Title:** Add IWM write handling and persistent virtual floppy writes

**Description**

Phase 1 (`PLUS-EMU-0003`) will deliberately expose a read-only virtual floppy backed by a circular encoded track bitstream. A real Macintosh Plus `.Sony` write, however, sends encoded data through the IWM's timed write-register handshake, overwriting media bits at the current rotation position. A standard `.dsk` holds only decoded 512-byte logical sectors; it must be updated by decoding valid modified track records after a write.

This ticket **extends** the Phase-1 IWM and disk components to support writes, dirty-track detection, write-back, persistence and formatting. It must not reintroduce ROM patching or replace the two-component boundary with a high-level sector-operation shortcut.

**Details**

- Kind: `TASK`
- Category: `PLUS-EMU`
- Primary milestone: [PLUS-DEV-0004](../../01-development-roadmap.md#plus-dev-0004--complete-the-virtual-800k-floppy-subsystem) — direct virtual-floppy write prerequisite for subsequent Unicorn/STM32 integration.
- Dependency: [PLUS-EMU-0003](PLUS-EMU-0003.md) **Phase 1 must be completed and accepted first**. This ticket is registered now to preserve component/API design, not to start simultaneous implementation.
- Architecture: [04-iwm-emulation.md](../../04-iwm-emulation.md), especially §§3.2, 4.3, 6.2 and 8.
- Intended code: extend the Phase-1 `iwm.c/.h` and `floppy_drive.c/.h`; storage-image and drive lifecycle integration in `devel/umac`; deterministic tests. Do not modify `disc_pv_hook()` as the authentic write implementation.
- Known ROM evidence: the original `Sony_WrData` waits for write-handshake bit 7, loads encoded codewords, checks bit 6 for underrun and exits write mode. It writes on-disk sync, marks, tags and checksum as well as data; the emulator must respect their serial timing.
- Confirmed limitation: ordinary 800K `.dsk` images do not losslessly represent arbitrary raw sync/gap, invalid encodings or all tag-field variations. The write-back policy must describe nonrepresentable data explicitly.

### IWM write behaviour

- Replace the Phase-1 write stub with the real IWM **holding buffer / write shift register / timed serial output** and correct Q6/Q7 state transitions in mode `$1F`.
- Implement bit 7 buffer-ready/write-handshake and bit 6 underrun indication, including start timing, subsequent 8-bit transfers, final-byte drain and error/reset semantics according to the actual IWM specification and original ROM traces.
- Emit logically encoded **bits** with the same virtual clock epoch as reads (16 C8M ticks/bit for normal fast mode), not instantaneous bytes or host-level sector writes. The logical bit boundary is sufficient; no electrical NRZI/pin waveform is required in the emulator.

### Circular track mutation and dirty state

- Overwrite bits in the selected circular track **at the current time-derived rotational bit index**, with wraparound, for the duration of the write-enable interval. Do not append or align an arbitrary write to the start of a GCR codeword.
- Handle sector prologues, data fields, self-sync patterns, checksums and format-initiated writes using the stream; do not assume every interval starts/ends on an 8-bit boundary.
- Keep the modified track readable immediately via the same in-memory circular stream (read-after-write). Set a dirty flag **only when bits actually differ** from their prior values.
- Preserve dirty track identity across seeks and side changes; do not replace a dirty buffer with a fresh `.dsk` encoding before successfully flushing it.

### Dirty-track flush and image persistence

- **Primary trigger:** when the Mac commands a head step to a different track, **flush the currently dirty track before switching**. Also consider head/side changes, drive selection, eject/media replacement, motor-off and orderly shutdown; ensure they do not silently discard unsaved data.
- **No-op for clean tracks:** do not decode, write or touch the image file unless there are actual modifications.
- To flush, scan the modified circular bitstream for valid Macintosh Sony headers/data prologues, identify the correct track/side/sector, verify markers/checksums and decode GCR/6-and-2 back to **512-byte logical sector payloads**.
- Update the correct offsets in the original `.dsk`, preserving untouched sectors and image geometry. Stage and validate all intended sector updates before committing; do not half-update the image after an incomplete or corrupt capture.
- Clearly report decode and I/O failures; retain dirty data for retry while possible rather than clearing dirty or reporting success. Define clean-exit/eject failure behaviour so failures cannot silently destroy the only valid version.
- Allow guest writes only for a writable image; preserve write-protect sense and reject writes for read-only media.
- Standard ROM-driven formatting should be handled via actual bitstream writes. Define how new headers/interleave map to `.dsk` and document limitations where the logical image cannot retain all formatted track features.

### Phase 2 acceptance checks

| ID | Test | Required evidence |
|---|---|---|
| `EMU10-A1` | IWM write handshake | Original ROM or faithful synthetic trace loads consecutive bytes only when ready; full shift timing and underrun bit/polarity/clearing pass timing tests |
| `EMU10-A2` | Write bitstream semantics | Bit-positioned overwrites (including off-byte alignment, circular boundary and sync) change the expected stream; identical bit writes do not dirty a track |
| `EMU10-A3` | Guest sector write and immediate read-back | Original `.Sony` driver writes data and reads back exactly the changed payload through authentic IWM paths |
| `EMU10-A4` | Step/side and lifecycle flush | Actual track step triggers dirty-track decode/save; side change, eject and orderly exit preserve changes; no file write for clean tracks |
| `EMU10-A5` | Persistence and integrity | Reopen `.dsk` and verify modified logical sector and untouched offsets; intentionally invalid/incomplete writes and simulated I/O failures never silently corrupt the image |
| `EMU10-A6` | Formatting and regression | Standard 800K format through original ROM write stream (including header/data) persists correctly, or an explicitly reviewed blocking limitation is recorded; read-only boot and SCSI/RTC regressions stay passing |
| `EMU10-A7` | Unmodified-ROM guarantee | No ROM patching or paravirtual `.Sony` interception; record ROM/image checksums, tested builds, and observed results |

**Notes**

- **2026-11-19 — Decision and registration:** Project owner approved a second ticket for writes following a read-only IWM/disk implementation. The two components are shared; writing modifies the circular bitstream and decodes/saves dirty tracks on head step, with additional lifecycle flush safety. This ticket uses `PLUS-EMU-0010` because `PLUS-EMU-0009` is already the closed RTC/PRAM ticket. No implementation or tests occurred at creation.

**Proposed Change**

After `PLUS-EMU-0003` is approved, implement actual IWM write-buffer/handshake behaviour and bitstream overwrites; add dirty marking and safe decoded logical-sector flush at track/side changes and lifecycle boundaries. Validate all acceptance cases, particularly real ROM write/reopen and corrupted/incomplete writes. Commit locally with `git commit` and provide SHA(s) and test results; **do not `git push`**. Leave future STM32-only versus ATF1508 decisions outside this ticket.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |

**Resolution Description**

Pending.
