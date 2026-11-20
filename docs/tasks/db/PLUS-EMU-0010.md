# PLUS-EMU-0010 — Add IWM write handling and persistent virtual floppy writes

**ID:** `PLUS-EMU-0010`
**Status:** `OPEN`
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

**Notes**

- **2026-11-19 — Decision and registration:** Project owner approved a second ticket for writes following a read-only IWM/disk implementation. The two components are shared; writing modifies the circular bitstream and decodes/saves dirty tracks on head step, with additional lifecycle flush safety. This ticket uses `PLUS-EMU-0010` because `PLUS-EMU-0009` is already the closed RTC/PRAM ticket. No implementation or tests occurred at creation.
- **2026-11-20 — Planning update:** Owner completed final PR review and merged [PR #3](https://github.com/keroblabs/tima_plus/pull/3); Phase-1 `PLUS-EMU-0003` is CLOSED. Reviewed actual `iwm.c`, `disk_img.c`, host CLI and original Mac Plus ROM `Sony_WrData`/`Sony_DoFormat` against the Apple IWM handshake/GCR description. Created [dedicated Document 05](../../05-iwm-write-implementation.md) with bounded implementation decisions, two-component interfaces, image-tag policy, atomic checkpoint, format behaviour and acceptance gates. **No Phase-2 code or write tests were performed; ticket stays OPEN pending developer implementation.**

- **2026-11-20 — Owner architectural decision:** Approved candidate circular-track copy → timed bit capture → validate completed operation → swap at `Q7L`, with rollback on interruption. **Format exception:** a fully transmitted pass whose resulting circular gap is invalid must be provisionally visible for the original ROM to measure and retry; an incomplete format is discarded, and invalid provisional tracks are never host-checkpointed. Host-image checkpoint remains independent and retains dirty active tracks on failure. Retain IWM D7/D6 semantics. The pattern is reusable for future STM32F411/optional ATF1508, without settling physical partitioning. **Documentation-only approval, no implementation/test claims.**

**Proposed Change**

Implement the approved [Document 05](../../05-iwm-write-implementation.md) contract: real IWM write holding/shifter/handshake and underrun, one temporary encoded circular-track candidate, actual timed bit capture, validation and in-memory swap on successful `Q7L`, discard incomplete candidates without affecting previously dirty active tracks, and separate safe image checkpoint on step/eject/exit. Validate authentic-ROM sector writes, read-after-write and formatting, including physical gap verification, and report host validation/persistence failures distinctly from IWM transport underruns. Commit implementation **locally** with `git commit`, report SHAs and test evidence; **do not `git push`**. STM32-only versus ATF1508 physical implementation remains future scope.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |

**Resolution Description**

Pending.
