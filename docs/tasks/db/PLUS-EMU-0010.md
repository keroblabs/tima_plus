# PLUS-EMU-0010 — Add IWM write handling and persistent virtual floppy writes

**ID:** `PLUS-EMU-0010`
**Status:** `REVIEW`
**Title:** Add IWM write handling and persistent virtual floppy writes

**Description**

Phase 1 (`PLUS-EMU-0003`) is **CLOSED** following owner-approved PR #3 to `main`. It established the original-ROM read-only IWM and circular encoded track bitstream. A real Macintosh Plus `.Sony` write sends encoded data through the IWM's timed write-register handshake. **Approved 20 November 2026:** capture those bits on a **temporary copy** of the circular track, at the correct rotating position; never modify the active track until the completed candidate is validated and swapped. A standard `.dsk` holds only decoded 512-byte logical sectors; it must be updated by decoding valid modified track records after a write.

This ticket **extends** the Phase-1 IWM and disk components to support writes, temporary-track transactions, dirty-track detection, write-back, persistence and formatting. The architecture is approved, **but no write implementation is yet approved**. It must not reintroduce ROM patching or replace the two-component boundary with a high-level sector-operation shortcut.

**Details**

- Kind: `TASK`
- Category: `PLUS-EMU`
- Primary milestone: [PLUS-DEV-0004](../../01-development-roadmap.md#plus-dev-0004--complete-the-virtual-800k-floppy-subsystem) — direct virtual-floppy write prerequisite for subsequent Unicorn/STM32 integration.
- **Dependency satisfied:** [PLUS-EMU-0003](PLUS-EMU-0003.md) is **CLOSED**, merged by [PR #3](https://github.com/keroblabs/tima_plus/pull/3). Phase 2 remains separately `OPEN`.
- **Dedicated developer implementation guide:** [05-iwm-write-implementation.md](../../05-iwm-write-implementation.md), with the **owner-approved transactional circular-track architecture**, plus remaining controller-timing and ROM-verification gates.
- Architecture: [04-iwm-emulation.md](../../04-iwm-emulation.md), especially §§3.2, 4.3, 6.2 and 8.
- **Actual accepted source names:** extend `devel/umac/src/iwm.c`, `include/iwm.h`, `src/disk_img.c`, `include/disk_img.h`, plus narrowly scoped `main.c`, `unix_main.c`, tests and build configuration. The original-ROM `.Sony` driver remains unchanged; do not revive `disc_pv_hook()` or add a host-sector shortcut.
- Known ROM evidence: the original `Sony_WrData` waits for write-handshake bit 7, loads encoded codewords, checks bit 6 for underrun and exits write mode. It writes on-disk sync, marks, tags and checksum as well as data; the emulator must respect their serial timing.
- Confirmed limitation: ordinary 800K `.dsk` images do not losslessly represent arbitrary raw sync/gap, invalid encodings or all tag-field variations. The write-back policy must describe nonrepresentable data explicitly.

### Approved implementation architecture (Document 05, 20 November 2026)

The developer must follow [Document 05](../../05-iwm-write-implementation.md): exact `Q6H`→first `Q7H` data write entry, `Q6L` handshake D7/D6, subsequent `Q6H` writes and `Q7L` exit; **single-byte controller holding/shifter** at 16 C8M ticks per bit; **separate temporary circular-track buffer** receiving bits at their real angular positions; **candidate validate/swap at successful `Q7L`**; discard incomplete/invalid candidates; and retain earlier committed dirty active tracks for a **separate, safe host-image checkpoint**. Include both heads, inverse GCR (703 codewords to 524 tag+data bytes plus checksums), full tagged DC42 fidelity, raw/tagless only when tags can be represented, explicit floppy `-w` independent of SCSI `-p`, and actual ROM `CtlFormat` gap layout.

**Internal stages:** A — IWM writer; B — transactional circular capture; C — inverse GCR and candidate validation; D — durable image checkpoint; E — ROM sector writes; F — ROM formatting/gap verification; G — regression. See Document 05 §§5, 8–10. **Do not synthesize a sector-level ROM bypass, invent register timing or remove the underrun flag.**

### IWM write behaviour

- Replace the Phase-1 write stub with the real IWM **holding buffer / write shift register / timed serial output** and correct Q6/Q7 state transitions in mode `$1F`.
- Implement bit 7 buffer-ready/write-handshake and bit 6 underrun indication, including start timing, subsequent 8-bit transfers, final-byte drain and error/reset semantics according to the actual IWM specification and original ROM traces.
- Emit logically encoded **bits** with the same virtual clock epoch as reads (16 C8M ticks/bit for normal fast mode), not instantaneous bytes or host-level sector writes. The logical bit boundary is sufficient; no electrical NRZI/pin waveform is required in the emulator.

### Transactional circular-track capture and dirty state

- On entry through first `Q7H`, copy the selected active encoded circular track into a temporary, medium/cylinder/side-identified candidate. This is **not** an IWM FIFO; the holding register and serializer still obey the original byte handshake.
- Emit timed bits into the **candidate** at the current rotating bit position, respecting sub-byte alignment, self-sync, nonstandard gaps and wraparound. Do not mutate active bits before the transaction is complete.
- At normal `Q7L`, stop output at that exact timestamp. A sector operation must yield a newly valid transmitted record and decodable resulting track. A format must have **transmitted all expected new headers/data**; a complete attempt can still overwrite its first mark through an undersized circular gap. Swap that complete attempt **provisionally** into the active track so the ROM's `RdAddr`/GapSync measurement can detect the shortage and retry. Provisional invalid formats are dirty/readable but **not eligible for host-image checkpoint** until a later valid full-track result.
- On interrupt, reset, enable/spindle loss, seek or eject before successful completion, **discard only the in-flight candidate**. Preserve earlier swapped dirty tracks. An incomplete format is rejected even if the copied track still contains valid old sectors; do **not** confuse it with a complete but gap-invalid attempt requiring ROM retry.
- Cache both heads' active bits/dirty flags independently. **Never** rebuild a dirty active track from the stale host image or evict it on failed checkpoint.
- **Error boundary:** ROM reads IWM underrun D6 **before** final `Q7L`; decoder rejection at exit cannot truthfully be reported as a retroactive underrun. Trace a separate host/media validation failure and test the ROM's subsequent verification path.
- **Fidelity note:** rolling back incomplete writes is an *approved virtual-media consistency policy*. Real magnetic media cannot undo partial physical writes; physical effects are a separate hardware study.

### Dirty-track flush and image persistence

- **Primary trigger:** when the Mac commands a head step to a different cylinder, **checkpoint both dirty active sides before eviction**. Also consider head/side changes, drive selection, eject/media replacement, motor-off and orderly shutdown; ensure they do not silently discard unsaved data.
- **No-op for clean tracks:** do not decode, write or touch the image file unless there are actual modifications.
- To checkpoint, scan each successfully swapped **active dirty** circular bitstream for valid Macintosh Sony headers/data prologues, identify the correct track/side/sector, verify markers/checksums and decode GCR/6-and-2 back to **512-byte logical sector payloads and 12-byte tags**.
- Update the correct offsets in the original `.dsk`, preserving untouched sectors and image geometry. Stage and validate all intended sector updates before committing; do not half-update the image after an incomplete or corrupt capture.
- Clearly report decode and I/O failures; retain **active dirty data** for retry rather than clearing dirty or reporting success. Define clean-exit/eject failure behaviour so failures cannot silently destroy the only valid version.
- Allow guest writes only for a writable image; preserve write-protect sense and reject writes for read-only media.
- Standard ROM-driven formatting uses the **same temporary track transaction** and preserves transmitted sync/gap lengths for the ROM's immediate verification and possible retry. The eventual logical image stores decoded sectors/tags, not arbitrary physical gaps after reopen.

### Phase 2 acceptance checks

| ID | Test | Required evidence |
|---|---|---|
| `EMU10-A1` | IWM write handshake | Original ROM or faithful synthetic trace loads consecutive bytes only when ready; full shift timing and underrun bit/polarity/clearing pass timing tests |
| `EMU10-A2` | Transactional bitstream | Candidate captures off-byte positions, wrap/sync, leaving active data unchanged mid-write; valid Q7L swaps; interrupted/invalid Q7L rolls back; identical bits do not dirty |
| `EMU10-A3` | Guest sector write and immediate read-back | Original `.Sony` driver writes data, valid Q7L swaps the candidate and immediate read sees the changed payload before host persistence |
| `EMU10-A4` | Step/side and lifecycle checkpoint | Step checkpoints both dirty active sides; interrupted candidate is discarded without evicting dirty active data; side change/eject/exit preserve writes; clean image not rewritten |
| `EMU10-A5` | Persistence and integrity | Reopen `.dsk`/tagged DC42 and verify changed sector/tags and untouched offsets; invalid candidates cannot change active track; checkpoint I/O failure retains dirty RAM |
| `EMU10-A6` | Formatting and regression | Original ROM full format transmits all fresh sector records; **provisionally publish a complete pass with a bad circular gap** so ROM measures/retries; reject interrupted format; never checkpoint an invalid provisional track; verify final format after reopen and SCSI/RTC regression |
| `EMU10-A7` | Unmodified-ROM guarantee | No ROM patching or paravirtual `.Sony` interception; record ROM/image checksums, tested builds, and observed results |

### Developer verification

| Check | Result |
|---|---|
| `EMU10-A1` / `W-A01`–`W-A03` | **PASSED.** `iwm_write_timing` checks the seven-tick first transfer, 16 ticks per bit, 128 ticks per byte, MSB-first output, D7 pending/ready, last-byte-wins replacement, sticky D6 underrun, enable-loss abort, reset and the exact `Q7L` cutoff. The ROM entered at PC `$418F5E`, tick `380122916`, with first transfer scheduled at `380122923`. The matching `Q7L` at `$41907C` emitted 5,727 bits through `DE AA` and seven filler bits, with no underrun. |
| `EMU10-A2` / `W-A04`–`W-A05` | **PASSED.** Active bits stayed unchanged during capture. An identical off-byte write stayed clean. A well-formed sector stream overwriting an address, an incomplete format, an illegal codeword, a bad header or data checksum, a mismatched sector number and a bit slip each rolled back. Known nonzero tags and payloads for all 1,600 sectors passed inverse GCR across both sides, all five zones and circular wrap. |
| `EMU10-A3` / `W-A10` | **PASSED.** Original `.Sony` wrote sectors on both sides, read the new payloads and tags before saving, then stepped and saved the two sides together. A deliberately invalid emitted GCR symbol left the ROM transport result at `noErr`. The subsequent original read-verify returned `dataVerErr` (`-68`) for the new payload and succeeded for the prior payload. |
| `EMU10-A4` / `W-A08` | **PASSED.** Verified both-side retention across side change, reset, failed step and failed eject. An interrupted transaction left earlier dirty tracks intact. Tested step, eject and orderly close checkpoints, including a desktop close refusal while dirty data had no representable host save. Clean and gap-only saves preserved the original inode. |
| `EMU10-A5` / `W-A06`–`W-A09` | **PASSED.** A new process reopened raw, tagless DiskCopy and tagged DiskCopy with both historical checksum conventions. The expected image matched every byte, including untouched sectors and preserved header fields. A nonzero guest tag on data-only media was readable in RAM, failed checkpoint with `ENOTSUP`, and persisted successfully after the guest supplied a zero tag. Injected write, file `fsync`, rename and directory `fsync` failures. All kept both dirty sides for retry. Pre-rename failures preserved the original bytes, and the post-rename retry verified the replacement inode before saving again. Tests also checked concurrent writer locks, hard links, symlinks, path replacement and the CLI's media/output aliases. |
| `EMU10-A6` / `W-A11`–`W-A12` | **PASSED.** Original `CtlFormat`/`CtlVerify` covered both heads of all 80 cylinders on tagged DiskCopy, raw `.dsk` and tagless DiskCopy. The test set guest `GapSync` to 11 during the seek, after the ROM initialized it to 7. The 85,824-bit first pass was published provisionally with the first address overwritten. Checkpoint failed while it was provisional. The ROM retried each shorter gap and produced a checkpointable pass at 84,096 bits. After reopen, all 1,600 sectors and representable tags were zero. The existing read-only boot, disk protection, keyboard and mouse, desktop clock and VBL, SCSI and RTC/PRAM regressions passed. SDL reproduced Finder 5.3 eject and restart from SCSI both with a protected floppy and with an opted-in writable tagged copy. |
| `EMU10-A7` | **PASSED.** The Macintosh Plus V3 ROM checksum was `4d1f8172`. The headless test compared its input bytes after operation and the SDL `-W` dump matched them byte for byte. The tests used the ROM's `.Sony` entry points and timed IWM accesses. |

Linux verification used 4 MiB Release with the tagged System Tools disk (17/17 CTests), 1 MiB Release with the extracted raw System Tools disk (16/16), 4 MiB Debug without external assets (12/12), and 4 MiB Debug with AddressSanitizer and UndefinedBehaviorSanitizer (17/17, `UBSAN_OPTIONS=halt_on_error=1`). LeakSanitizer could not attach in the sandbox, so the sanitizer build and tests used `ASAN_OPTIONS=detect_leaks=0`. Standalone `make prepare` and `make -j4`, all four HFS converter tests, and `git diff --check` passed. Physical Sony hardware and later STM32 integration are outside this emulator ticket.

All reference inputs retained their hashes. Writes and formats used disposable copies. For data-only media the test fixture's write-protect sense is held during system startup to keep the OS's automatic tagged write out of the data-only probes. The test restores normal sense and lets the unchanged `.Sony` driver reread it before issuing the write, verify or format request.

| Input or output | SHA-256 |
|---|---|
| Plus V3 `rom.bin` and SDL `-W` dump | `dd908e2b65772a6b1f0c859c24e9a0d3dcde17b1c6a24f4abd8955846d7895e7` |
| Tagged `plus-system.image` with the first tag omitted from the checksum | `11eef5df148b45c0e8e8a7cb1290a1c552e298f497ab7a15d92770479d77aca6` |
| Tagged copy recalculated with all tags included | `c4cc9255da766119ef1bdc193ff73ffe5addf5ba76d1d547a34e06a7a168a454` |
| Raw System Tools `.dsk` extracted from `plus-system.image` | `89c76e5f6b4fd24560933b0d85b0fa99aa70f11c1a92c3e8b4c1771ad5db6435` |
| Tagless DiskCopy derived from `plus-system.image` | `554b27e252334acce032c41d46833738b5e1c04a626f748fed751bcb3995a354` |
| Separate raw System 6.0.8 boot fixture | `4089f6ed6a39987a0f56207c25936bc61d1e7bcd0c969bbe92f3f6a28cfa7f50` |
| SCSI `HD10_512.hda` | `d3796a03029cdb3a71b9f943375ee6e711541e2dc0c7406694d3913fc7fc2d53` |
| Writable tagged SDL copy after Finder Shut Down and eject | `55d0e02486d7ff4bd27d3029feea1b991d18dc5a03508d97ceb5639b93dd9338` |

### Review Findings

| Finding ID | Classification | Evidence | Required action / acceptance evidence | State |
|---|---|---|---|---|
| `EMU10-R1` | `CORRECTION_REQUIRED` | Original `CtlFormat` shares the second address-trailer `$FF` with the data sync group. The first decoder also dropped bits when a byte straddled a partial final packed byte. | Parse the ROM's 27-byte template and handle both packed bytes before wrapping. Run full ROM format/verify and inverse checks on all zones and both heads. | `VERIFIED` |
| `EMU10-R2` | `FUTURE_SCOPE` | Ordinary writable raw startup on the supplied System Tools image and a separate System 6.0.8 image issued writes with nonzero tags. Checkpoint failed explicitly, and the raw file retained its SHA-256. The desktop stayed open on a close request after the failed save. | The approved data-only path is verified. General Finder writes carrying tags on raw media require an owner decision on [tag storage or explicit conversion](../../05-iwm-write-implementation.md#62-raw-versus-diskcopy-42-tags) under [PLUS-DEV-0004](../../01-development-roadmap.md#plus-dev-0004--complete-the-virtual-800k-floppy-subsystem). | Owner decision pending |

**Notes**

- **2026-11-19 — Decision and registration:** Project owner approved a second ticket for writes following a read-only IWM/disk implementation. The two components are shared; writing modifies the circular bitstream and decodes/saves dirty tracks on head step, with additional lifecycle flush safety. This ticket uses `PLUS-EMU-0010` because `PLUS-EMU-0009` is already the closed RTC/PRAM ticket. No implementation or tests occurred at creation.
- **2026-11-20 — Planning update:** Owner completed final PR review and merged [PR #3](https://github.com/keroblabs/tima_plus/pull/3); Phase-1 `PLUS-EMU-0003` is CLOSED. Reviewed actual `iwm.c`, `disk_img.c`, host CLI and original Mac Plus ROM `Sony_WrData`/`Sony_DoFormat` against the Apple IWM handshake/GCR description. Created [dedicated Document 05](../../05-iwm-write-implementation.md) with bounded implementation decisions, two-component interfaces, image-tag policy, atomic checkpoint, format behaviour and acceptance gates. **No Phase-2 code or write tests were performed; ticket stays OPEN pending developer implementation.**

- **2026-11-20 — Owner architectural decision:** Approved candidate circular-track copy → timed bit capture → validate completed operation → swap at `Q7L`, with rollback on interruption. **Format exception:** a fully transmitted pass whose resulting circular gap is invalid must be provisionally visible for the original ROM to measure and retry; an incomplete format is discarded, and invalid provisional tracks are never host-checkpointed. Host-image checkpoint remains independent and retains dirty active tracks on failure. Retain IWM D7/D6 semantics. The pattern is reusable for future STM32F411/optional ATF1508, without settling physical partitioning. **Documentation-only approval, no implementation/test claims.**

- **2026-11-20 — Investigation, baseline test and status change:** Started the owner-requested implementation on `docs-iwm-transactional-write` at `0e5f8dd` and moved `OPEN` → `IN_PROGRESS`. Confirmed that the controller returns a fixed handshake and the backend opens immutable raw or DiskCopy images. The untouched 4 MiB Release build passed all 13 CTests with the Plus V3 ROM and `plus-system.image`, including guest boot/read/protection, desktop input and timing, SCSI and RTC/PRAM. Apple's IWM design specifies the first holding-register transfer seven fast CLK periods after write-state entry and subsequent loads at eight bit cells. The ROM checks underrun before `Q7L` and uses two trailing filler bytes to leave the sector epilogue on the medium. [Apple IWM design, sheet 52](https://www.bitsavers.org/pdf/apple/disk/sony/SWIM_343S0061-A_1988.pdf)
- **2026-11-20 — Implementation, stages A–D:** Added timed holding and shift registers, candidate capture, inverse GCR validation and separate image persistence. Kept dirty sides attached through motor-off, drive change and reset until a cylinder step, eject or close. Added stable single-writer locking and kept the dirty tracks if directory sync failed after atomic rename. Chose the standard omitted-first-tag convention when both DiskCopy tag checksums match. Added the opt-in floppy `-w` and rejection of writable media and lock-path aliases.
- **2026-11-20 — ROM investigation and rework, stages E–F:** Original-ROM tests initially wrote and read sectors successfully but exposed an automatic Finder write to HFS block 2 before the probes. Compared the explicit writes against the checkpointed post-boot fixture. The first format validator failed the original 27-byte template. After correcting the shared sync byte, the authentic oversized gap reached ROM retry. The subsequent full format exposed the partial-final-byte bug at cylinder 2. Corrected both issues and verified `EMU10-R1` through ROM format/verify, reopen and all-track native tests.
- **2026-11-20 — Data-only investigation:** Reproduced `EMU10-R2` with raw System Tools and System 6.0.8 startup. The approved policy rejects the unrepresentable checkpoint and preserves the dirty track and original file. Verified data-only `.Sony` writes and formats after a protected startup fixture, and verified recovery after the guest deliberately wrote a nonzero tag and then zeroed it. Requested the owner's scope decision for ordinary Finder writes carrying tags on raw disks.
- **2026-11-20 — Regression verification and developer review, stage G:** Reviewed validation, drive transitions, persistence error paths, and the ROM and frontend traces. Recorded the acceptance results above for release, debug, sanitizer, Make, HFS and dual-media tests. All required emulator checks passed within the approved data-only policy. Moved `IN_PROGRESS` → `REVIEW` for independent review and owner sign-off.

**Proposed Change**

Implement the approved [Document 05](../../05-iwm-write-implementation.md) contract: real IWM write holding/shifter/handshake and underrun, one temporary encoded circular-track candidate, actual timed bit capture, validation and in-memory swap on successful `Q7L`, discard incomplete candidates without affecting previously dirty active tracks, and separate safe image checkpoint on step/eject/exit. Validate authentic-ROM sector writes, read-after-write and formatting, including physical gap verification, and report host validation/persistence failures distinctly from IWM transport underruns. Commit implementation **locally** with `git commit`, report SHAs and test evidence; **do not `git push`**. STM32-only versus ATF1508 physical implementation remains future scope.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |

**Resolution Description**

Pending.
