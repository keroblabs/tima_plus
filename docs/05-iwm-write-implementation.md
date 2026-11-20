# Tima/Plus — Macintosh Plus IWM Write Implementation Plan

**Status:** **Approved architecture and implementation handoff** — project-owner decision of 20 November 2026: transactional temporary circular-track capture. Controller timing and ROM integration still require validation; no write implementation or tests are claimed.  
**Date:** 20 November 2026  
**Ticket:** [PLUS-EMU-0010](tasks/db/PLUS-EMU-0010.md) — OPEN  
**Completed prerequisite:** [PLUS-EMU-0003](tasks/db/PLUS-EMU-0003.md) — CLOSED, [PR #3](https://github.com/keroblabs/tima_plus/pull/3) merged  
**Architecture:** [04 — IWM Emulation](04-iwm-emulation.md), especially §§3.2, 4.3, 6.2 and 12  
**Target:** Macintosh Plus V3 **unmodified ROM** and original `.Sony` driver; Sony double-sided **800K GCR**, not a complete IWM or SuperDrive

## 1. Scope and actual code baseline

The write phase extends two **already working** components. The IWM owns the 68000-visible register and byte-timing contract; the disk backend owns the timed circular GCR bits, Sony drive and backing image. A ROM `_Write` must never be intercepted and mapped directly to a host 512-byte sector write.

| Existing module | Already implemented in Phase 1 | Required Phase-2 extension |
| --- | --- | --- |
| `devel/umac/src/iwm.c` / `include/iwm.h` | 16 soft switches, IWM mode, read latch/14-FCLK rearm, drive selection, motor-off timer, virtual C8M time, Sony command strobe | Replace handshake `0x3f` stub with timed write holding register, byte shift, bit output, underrun and exit |
| `devel/umac/src/disk_img.c` / `include/disk_img.h` | Read-only raw/DC42 images, 12-byte tags, 80-cylinder × two-head geometry, circular GCR buffers, drive motor/seek/sense | Bit-accurate track overwrites, dirty cache, inverse GCR/6-and-2, sector validation, lossless representable image persistence |
| `devel/umac/src/main.c` | Ordered 68000 bus timestamps, IWM access dispatch, VIA PA5, SCSI/RTC coexistence and traces | Preserve timing; propagate write/flush failures, add trace fields only as needed |
| `devel/umac/src/unix_main.c` | `-d` read-only floppy, `-s` SCSI, `-p` SCSI writes, currently rejects `-w` | Opt-in floppy `-w`, safe writable open/lock/flush and explicit host errors |
| Existing tests | Original ROM/System 6 boot, dual-drive/SCSI/RTC, all zones, tags, eject and read-only protection | Timed handshake, write/read/reopen, corruption and persistence failures, real ROM formatting |

Do **not** add Apple II 5.25-inch modes, MFM/SWIM 1.44MB, physical WRDATA transitions, STM32 peripheral code, ATF1508 RTL, host remount UI or an unmodified-ROM bypass.

## 2. Decisions to give the developer

**Approved policy (20 November 2026):** keep the original ROM-visible IWM write handshake, but stage all physical-positioned bit changes on a **temporary copy of the encoded circular track**. At a successful `Q7L` write-mode exit, validate the candidate, then atomically replace the *active in-memory track*. Host-file persistence is a **second, independent checkpoint**, not the same commit. The remaining unknowns are verification details, not a reason to reopen the buffer architecture. When the original ROM or Apple's specification contradicts an exact timing detail, preserve a trace and seek a focused correction rather than inventing the hardware behaviour.

| Decision | Required approach |
| --- | --- |
| **D01 — Clock** | Continue **virtual C8M = 7,833,600 ticks/s**, normal fast GCR bit cell **16 ticks**, ordinary eight-bit output **128 ticks**. Advance serial output *between CPU accesses*; no per-poll byte clock. |
| **D02 — Write register** | Model first `Q7H` data write and subsequent `Q6L` handshake / `Q6H` byte writes / `Q7L` exit exactly as original `Sony_WrData`. |
| **D03 — IWM register buffer** | **Single-byte holding register plus serial shift register**, not an unlimited FIFO. Handshake D7 = buffer empty; D6 = write healthy until sticky underrun; D5–D0 = ones. This is separate from the disk backend's temporary *track* buffer. |
| **D04 — Transactional stream** | Write **timed logical encoded bits into a temporary copy of the selected circular track** at the current angular position, including non-8-bit-aligned self-sync and wrap. On successful `Q7L`, validate and swap into the active track. No append, artificial alignment, decoded-sector shortcut or fake NRZI pin pulse. |
| **D05 — Cache** | Preserve active tracks and dirty state for **both sides** of the current cylinder. Maintain at most one in-flight temporary candidate for the selected side; never rebuild or evict a dirty active track from the old image. |
| **D06 — Writable flag** | `-d` remains **read-only by default**. Reuse reserved `-w` for explicit **floppy** writes (requires `-d`). `-p` still controls **SCSI**, independently. |
| **D07 — Image fidelity** | **Tagged 838,484-byte DiskCopy 4.2** is the primary lossless 512+12-byte write/reopen test. Raw 819,200-byte and tagless DC42 are data-only; never silently discard changed nonzero tags. |
| **D08 — Two commits** | **Transaction commit:** at valid `Q7L`, swap the validated temporary track into active RAM, immediately available for reads. **Persistence checkpoint:** save only dirty, validated active tracks before seek-cache replacement, guest eject, media replacement, clean exit and appropriate idle/power-down checkpoints. Clean tracks cause **no host write**. |
| **D09 — Durability** | Stage and validate a complete candidate image; use a same-directory temporary file + `fsync` + **atomic rename** and stable single-writer locking. Never clear dirty on a failed checkpoint. |
| **D10 — Format** | The ROM writes a full circular track and later measures gaps. Use **the same temporary track transaction**, retaining all transmitted syncs/gaps, and swap only after a verified complete format stream. Do not synthesize a host format on request. |
| **D11 — Regression** | Preserve original-ROM boot and read latch, SCSI, RTC/PRAM, read-only mode, guest eject/restart and SCSI + floppy coexistence. |
| **D12 — Handoff** | Implement stage-by-stage with test evidence and **local `git commit` only; do not `git push`**, create PR or merge as developer. |
| **D13 — Aborts** | On interrupted write (reset, motor/enable loss, seek, eject, incomplete/error condition), discard the *in-flight candidate* while retaining any **previously committed dirty active track**. An invalid `Q7L` candidate is also discarded with an explicit recorded error. |
| **D14 — Hardware reuse** | The future STM32F411 drive may use transactional write capture before SPI flash programming. Real-time pin/bit capture must continue independently of slower decoding/flash work; STM32-only versus ATF1508 assistance is still open. |

The logical interface **does not emulate electric WRDATA pin level**. Physical STM32/ATF1508 decisions remain open; the eventual physical encoder can implement the electrical representation of these exact logical bits.

## 3. IWM write protocol: precise ROM contract

Sources: [original Mac Plus ROM `Sony_WrData`, ~2537–2724](https://github.com/jonathanschilling/mac_rom/blob/main/plus/resources/res_drvr_sony.s), `Sony_DoFormat` ~2957–3078, and [Apple SWIM/IWM register specification](https://manualzz.com/doc/20452745/apple-swim-chip-user-manual) (IWM register set, write-handshake, write data). The SWIM document describes the IWM-compatible register set; **do not implement its separate ISM/MFM path**.

### 3.1 Register and soft-switch entry sequence

| CPU/ROM operation | IWM interpretation |
| --- | --- |
| Select drive/side, Sony `MOTORON=0` and wait for `/READY` | Existing drive-control machinery; before write, media must be present, spinning, and **writable** |
| Read `Q6H` while Q7=0 | Select Sony status/write-protect-sense state; **no write starts** |
| **Write first byte to `Q7H`** | Q7 0→1 while Q6=1 enters **IWM write state** and accepts that same first byte into holding buffer |
| Read `Q6L` with Q7=1 | Return the **write-handshake byte**, not read data or Sony sense |
| Write a subsequent byte to `Q6H` with Q7=1 | Load the **write-data holding register** when handshake says available |
| Read `Q6L` after last trailer byte | ROM checks D6 for `wrUnderrun (-74)` |
| Access `Q7L` | Exit write state at **actual virtual timestamp**; stop any further write emission |

The existing `iwm_read` `Q7` branch returns a fixed `0x3f`. Replace that with the real handshake; the `iwm_write` read-only rejection becomes real write loading only when explicit writable media is present. **The first `Q7H` write must not be thrown away merely because it is the same bus access that enters write mode.**

With a selected motor-on drive, Q7/Q6 states `00/01/10/11` correspond to **read data/status/write-handshake/write data**. Preserve mode `$1F`, Q6/Q7 state transitions, read-latch rearm and IWM delayed Motor-Off behaviour already verified in Phase 1.

### 3.2 Holding byte, shift register and handshake

Implement explicit state equivalent to `write_active`, `hold_valid`, `hold_byte`, `shift_bits_remaining`, `shift_byte`, `next_cell_tick` and `underrun` (names illustrative). One holding byte and one active serializer; **no sector-sized FIFO**.

The IWM write-handshake register is defined as:

- **D7 = 1**: holding buffer is empty / ready to accept another byte. **D7 = 0**: a byte is pending.
- **D6 = 1**: write state healthy. If the shifter requires another byte and none is pending, an **underrun latches D6=0** until write mode exits or the controller resets.
- **D5–D0 = 1**: unused, returned high (reference and present `0x3f` stub).

When the serializer takes a pending byte, D7 becomes 1, permitting the ROM to submit the next one. Serial output is **MSB first**, one bit per elapsed 16-C8M-tick cell. A CPU byte write while the holding register is full must not grow an unbounded queue: the IWM reference describes a **last-byte-wins** replacement of too-early writes. Record and test this.

On **underrun**, stop further valid writes according to the original IWM semantics, keep D6=0 until the appropriate exit/reset, and avoid silently feeding synthetic zero bytes. On **Q7L exit**, stop at the actual tick: **do not force-drain** any remaining byte instantaneously or automatically append extra bytes. The ROM deliberately writes two trailing `$FF` codewords after `DE AA` so that preceding meaningful bytes can reach the medium before exit. The *disk backend* may recognize harmless partially emitted trailing filler when validating a completed sector record, but it must not fabricate missing significant data, checksum, sync or format bits. A buffer in the disk backend is **not** an IWM FIFO and must never make handshake D7 ready early.

**Timing verification still required (architecture already approved):** the phase between first `Q7H` write and first emitted bit, exact holding-to-shifter transfer instant, and whether the final dummy byte has fully shifted at the ROM's `Q7L` access. Resolve using tiny timed synthetic tests and the original-ROM `_Write` trace; do not guess.

### 3.3 Write gating and reset

A serial bit can affect a **temporary candidate** only when the selected drive is effectively enabled, Sony spindle running, medium inserted, writable, and writing at a permitted time/head state. Exiting IWM write mode is **not** a spindle stop or guest eject. If /ENBL drops, the spindle stops, the CPU resets, a head step/eject interrupts an active write or media disappears, stop the serializer at the actual event tick and **discard the uncommitted candidate**. Preserve any *earlier, successfully swapped* dirty active track. Do not leave a stale read latch looking like newly read data.

## 4. Exact encoded disk format and inverse decoder

The existing encoder `append_sector()` and `append_payload()` are the reference fixture. The original ROM writes **48 raw bits** for its six packed sync bytes `FF 3F CF F3 FC FF`, which represent **four 10-bit self-sync words plus eight 1 bits**. Do not round these to new byte boundaries, add an extra ten-bit word or treat their packed representation as six legal GCR codewords.

| Field | Track representation |
| --- | --- |
| Address | `D5 AA 96`, then five **GCR codewords**: low track, sector, high track + side, format `$22`, XOR checksum, then `DE AA FF FF` |
| Sync before data | Packed `FF 3F CF F3 FC FF` (48 bits); other 10-bit gap sync may precede address |
| Data | `D5 AA AD`, one GCR **sector number**, then **703 encoded GCR codewords** |
| Data trailer | `DE AA FF FF` |
| Logical contents | **12-byte sector tag + 512-byte sector payload + three checksum bytes**, encoded with Sony 6-and-2/GCR |

**703 derivation:** 12+512 = 524 raw bytes → 174 full three-byte groups × 4 codewords + one two-byte group × 3 = **699**, plus 3 checksum bytes encoded as one 4-codeword group → **703**.

### 4.1 Decoder contract

1. Search the **circular bitstream**, allowing wraparound and any candidate bit alignment, for valid address prologues and GCR field boundaries. Do not assume the current read-buffer byte alignment or track index 0.
2. Invert the existing 64-entry `gcr[]` table; reject any illegal codeword in an encoded field. Decode the five header nibbles; validate track/side, sector number, format and address XOR checksum.
3. Find the associated `D5 AA AD` data field and its matching GCR sector number, with valid sync/trailer; do not pair unrelated adjacent address/data fields.
4. Decode the **699 payload codewords** into 524 bytes by recovering the high two bits from each group's first symbol and low six bits from the next two/three symbols. **Reverse the exact rolling A/B/C XOR-and-carry algorithm in current `append_payload()`** (including the special final two-byte group). Decode the last four codewords into the three transmitted checksums.
5. Recompute and validate all three running checksums; reject an illegal nibble, invalid mark or incomplete field. Compare decoded 512-byte data and **12 tag bytes** against the previous active/committed data **to identify changes**, not to reject legitimate changed sectors.
6. Before persistence, require **exactly one verified sector for each sector number expected** for the specific cylinder/side (12/11/10/9/8 according to zone); reject missing/duplicate/conflicting headers, wrong head, malformed format or invalid tags.
7. At `Q7L`, decode the complete **temporary track** and validate it for the operation. An ordinary sector write can reuse unchanged valid sectors, but must contain a newly valid targeted data field. A detected full-track format must establish every expected sector through the **current write transaction**, not by borrowing old sectors from the copied track. On failure discard only this temporary candidate; do **not** overwrite the previously active track, clear its existing dirty flag, modify the host image or report durable success.

**Mandatory inverse proof:** generate every track/side from deterministic data and deliberately **nonzero tags**, then decode **all 1,600 data sectors and 1,600 tags**. Change one GCR codeword, header checksum, data checksum, sector identity and circular bit alignment separately: each must be rejected. Do not use a “decode last 512 bytes written” shortcut.

## 5. Transactional circular-track capture and dirty tracking

**Approved 20 November 2026:** the persistent/guest-readable track is not modified while the IWM is writing. The **transaction** is a private, packed-bit copy of that side's active circular track, identified by drive, medium, cylinder, side, bit length and time-derived angular phase. This is a disk-backend buffer, *not* an unlimited IWM FIFO.

The current `disk_img` has two active encoded track buffers, `tracks[2]`, one for each head; each identifies a cylinder and is rebuilt from the image when necessary. Extend each with valid/dirty/medium identity and add **one in-flight candidate track** for the selected side (normally roughly 10–12 KiB of packed GCR bits, size to be derived from current geometry).

### 5.1 One write transaction

1. **Begin:** after the real IWM `Q6H`→first `Q7H` entry, snapshot the selected active track into a temporary candidate. Fix its drive/cylinder/side/length/medium identity; do not alter the active buffer. Treat the first byte (which is data, except that the format routine deliberately supplies a disposable initial byte) according to the normal shifter timing.
2. **Capture:** as virtual time advances, the IWM's **one-byte holding register and shifter** emit encoded bits at 16 C8M ticks per bit. The disk backend overwrites corresponding candidate bits at the current rotating angular position, including non-byte-aligned sync and circular wrap. Record the actual write coverage/extent and the emitted stream for validation; never append to a track or assume a 703-byte physical transfer was instantaneous.
3. **Finish:** on normal `Q7L`, advance to the exact access tick, stop emission and validate the candidate. For a sector write, require a correctly associated **new** data record, complete payload and checksum, plus a fully decodable remaining track. For a format, require **all** expected sector headers/data supplied during the current operation, valid GCR/checksums and measurable preserved sync/gaps. Truncated trailing filler may be acceptable if all meaningful records are complete; missing meaningful bits are not.
4. **Swap:** only after successful validation, replace the active track bits/metadata with the candidate (swap buffer ownership if convenient). The original ROM can immediately read its just-written stream and measure actual gaps. Mark the active side dirty **only if its bit contents changed**. Keep the other side intact.
5. **Abort/reject:** on interruption before `Q7L`, invalid/incomplete transaction, head/medium mismatch or unsupported write, **discard the temporary candidate**. The previously active track and its pre-existing dirty state remain unchanged. Report/trace rejection; never pretend it is a durable host-file write.

**Format recognition:** the approximately **200 initial six-byte sync groups** and a sequence of address/data records identify the ROM's format pattern. A 50+ sync-group heuristic may aid diagnostics, but must **not** be the sole proof of a complete format. Do not invent a special host `format_disk()` operation; retain genuine timed writes and their rotational position. If a transaction starts as a format but is aborted after only some records, previous sectors in the copied track **must not** make it pass validation.

**Guest-visible error limitation:** the ROM checks IWM handshake D6 **before** its final `Q7L`. A backend parse failure discovered at `Q7L` cannot honestly be retroactively reported as an IWM underrun. Keep transport-handshake status and media-validation status separate; emit a clear host-level error and test whether the ROM's subsequent read/verify detects it. Never silently acknowledge a rejected candidate as safely written or invent a status bit.

**Intentional fidelity boundary:** unlike magnetic media, whose partially written bits cannot be rolled back, this virtual disk deliberately rolls back an aborted/invalid write transaction. This is an approved emulator consistency policy, **not** a claim that real Sony media does the same. Exact pin-level electrical and partial-write effects are future hardware verification work.

### 5.2 Active tracks and later host-image checkpoint

- The active circular tracks remain independently cached for **both heads of the current cylinder**. A side switch must preserve the other's bits and dirty state; a dirty active track must **never** be rebuilt from the older image.
- A valid in-memory swap makes read-after-write immediate; **no host image flush is required for a same-cylinder read or verification**.
- Before a STEP that changes cylinder and would evict the old buffers, checkpoint **both dirty sides** to the backing image. On failure retain those buffers and report a blocking error rather than pretending a clean seek.
- Motor-off, /ENBL loss or CPU reset **do not eject media**. Preserve earlier committed dirty active tracks and checkpoint on an appropriate safe boundary. On guest EJECT, new medium or clean emulator exit, flush before detaching the old medium.
- An incomplete candidate is discarded independently of host persistence. A **completed but not-yet-saved** active track stays dirty until the separate host-image checkpoint succeeds. Failed decode/fsync/rename must not clear that dirty state or permit silent eviction.
- Clean tracks incur **no** host write. A forced crash can still lose dirty RAM without a journal; do not promise crash durability.

### 5.3 Transferable principle for STM32F411 and optional ATF1508

The same **capture → validate → commit** separation is a reference for the future Sony MFD-51W physical emulator: receive real-time write signals into RAM, decode/check sector or format transactions, and schedule SPI flash erase/program **only after** successful validation. A temporary track gives one mechanism for sector writes, full-track formatting, rewrites and aborted operations, and can coalesce writes to reduce flash wear.

This **does not** mean flash programming or expensive validation can block the Mac's incoming bitstream. An STM32 implementation needs sufficient capture RAM/buffering and correctly timed ISR/DMA or optional ATF1508 support while a transaction is in flight. Physical /WRREQ/WRDATA representation, SPI framing, flash-power-failure atomicity, removal during save and electrical compatibility remain separate design/measurement tasks. No particular ATF1508/STM32 partition or pin-level protocol is selected here.

## 6. Writable media and transaction safety

### 6.1 CLI and guest protection

`-d <image>` remains **read-only by default**; make existing reserved `-w` explicitly allow **floppy** writes and require `-d`. This is independent of SCSI write-enable `-p`. Missing or invalid images remain rejected before use; preserve ROM/PRAM/RAM/SCSI input-output alias protections.

Sony read index 3 (`/WRPROT`) reports **0** when inserted image is read-only, **1** only after a writable image has passed open, format and lock checks; never report a missing drive or image as writable. All real write tests must use disposable copies, not the project's reference System/ROM/HDD images.

### 6.2 Raw versus DiskCopy 4.2 tags

| Image | Persistable content |
| --- | --- |
| **Tagged DiskCopy 4.2**, 84+819200+19200 = **838,484 bytes** | Primary full-fidelity path: save both **512 data + 12 tag bytes** per sector, update checksums while keeping original header/size/encoding/format |
| **Raw .dsk**, **819,200 bytes** | Logical sector **data only**. Save supported data-only modifications, but **reject a checkpoint that would silently lose changed nonzero tag bytes**. Additional sidecar or format conversion is a separate owner decision |
| **Tagless DiskCopy 4.2**, **819,284 bytes** | Same data-only/tag limitation; don't alter its format to add tag fields without an approved conversion |

Tagged DC42 stores **big-endian checksums at header offsets 0x48 and 0x4C**. The current parser accepts two historical tag-checksum conventions: including all tags, or **skipping the first 12-byte tag**. Preserve whichever convention matched the image at open, recompute data and tag checksums over the **new** image contents, and preserve original header name, format `$22`, encoding `1` and signature `$0100`. Reopen with the strict existing reader to verify the committed image. <https://ciderpress2.com/formatdoc/DiskCopy-notes.html>

**Explicit representability question:** Original ROM write operations may supply nonzero tags even when the host input was a raw image. For safe Phase-2 scope, tagged DC42 is the writable test authority; raw/tagless data-only writes may succeed only if they do **not** discard guest tag changes. If that limitation blocks required usage, record the guest trace and seek an owner decision on sidecar tags or explicit conversion, **not silent loss**.

### 6.3 Atomic host image checkpoint

Use the existing small-floppy image size to favor correctness over premature speed optimization:

1. Hold a **single-writer lock tied to a stable lockfile** for the writable session, including after atomic image replacement; reject concurrent writers, symlink/hard-link surprises and dangerous aliases.
2. When a dirty **active** track is due for checkpoint, decode and validate every expected sector of the affected side(s). **Stage all candidate 512-byte logical payloads and 12-byte tags** before changing the file. A temporary write transaction that never passed the in-memory swap is *not* eligible for checkpoint.
3. Produce an independent complete next-image snapshot, including all untouched sectors and proper DC42 checksums. Save to a **unique same-directory temporary file** with checked short writes, `fsync` it, atomically rename over the target and `fsync` the directory where supported.
4. Only after a successful image checkpoint update the committed logical snapshot and **clear the corresponding active-track dirty flags**. Failure before rename must leave the previous image intact; after an uncertain post-rename failure reconcile path/content identity and do not report the image safely saved or evict dirty buffers without proof.
5. Do not rewrite a clean image. Handle file permissions, rename/path-identity changes, process close and failure injection explicitly.

An atomic full-file 800K replacement is acceptable even though only a subset of **logical** sectors changed. The alternative of writing individual 512-byte sectors in-place must not be chosen merely for speed: it provides weaker failure atomicity.

## 7. ROM formatting is a different case

The original `Sony_CtlFormat` / `Sony_DoFormat` routine writes an entire cylinder/side as an **actual encoded serial stream**:

1. Approximately **200 sync groups** at start, then gap groups before each sector.
2. A **27-byte address/data-mark template** per sector, with correct track/head, sector number, format and address checksum in **2:1 interleave**.
3. **703 `$96` GCR codewords** for each empty data field, then `DE AA FF FF`.
4. After writing, it uses **real read-data / address marks to measure the gap**. It can adjust GapSync and **rewrite the same track**; it formats both heads and steps through 80 cylinders.

A format stream may exceed one full revolution and overwrite earlier bit positions. The **temporary candidate** must handle **wraparound** and repeated writes to the same head/cylinder. A detected format must demonstrate a full track of freshly transmitted sector headers/data, not accept a partial format merely because the snapshot already contained old valid sectors. On successful `Q7L`, swap the exact candidate bitstream: the ROM then measures its **actual sync gaps** and may issue another format transaction with adjusted GapSync. A format test passes only if the unmodified ROM's own format/verify path succeeds, then all 1,600 sectors can be decoded after **reopening** the image. A fake host `format_disk()` callback is **prohibited**.

A blank writable disk's initial encoded representation may differ from the existing always-preformatted image; if the ROM trace demonstrates a missing distinction, make that a targeted design review rather than a disk-formatting shortcut.

## 8. Implementation stages within ONE open ticket

| Stage | Developer output | Acceptance gate |
| --- | --- | --- |
| **A — IWM writer** | Q6/Q7 first-byte, holding buffer, serializer, D7/D6 handshake, exit/reset | Timed unit traces; original ROM write loop does not stall; forced underrun is sticky |
| **B — Transactional circular writer** | Active track copy; timed candidate-bit overwrite; explicit complete/abort/validate/swap; both-side dirty cache | Off-byte, 48-bit sync, circular wrap, pre-Q7L read isolation, valid Q7L read-after-write, interrupted-write rollback tests |
| **C — Decoder and transaction validator** | Inverse 64-entry GCR, 6-and-2, checksums, address/data pairing, evidence that written record/complete format belongs to this transaction | All sectors+tags roundtrip; malformed records or partial format rejected *before active-track swap* |
| **D — Persistent image** | `-w`, writable sense, **separate** host-image checkpoint, lock/checksum and error handling | Host failure injection after RAM swap, clean-no-op, duplicate writer/alias rejection |
| **E — Original ROM writes** | Guest `_Write` then `_Read`, step-triggered flush, fresh process reopening | Data and tags persisted on **tagged DC42**; correct guest errors on read-only disk |
| **F — Actual format** | ROM `CtlFormat` and `CtlVerify` using the same bitstream writer | 80 cylinders × two heads, all zones, full logical decode after reload |
| **G — Regression** | Release/Debug/ASan/UBSan, 1/4 MiB, mixed SCSI/floppy, RTC and System 5 shutdown | Complete logs/media hashes and **local commit SHAs**; do not push |

Record each stage and changes in [PLUS-EMU-0010](tasks/db/PLUS-EMU-0010.md), preserving the task methodology and the reviewer's approval gate.

## 9. Mandatory acceptance cases

| ID | Test / outcome |
| --- | --- |
| **W-A01** | First `Q7H` byte accepted on write-mode entry; Q6L handshake D7 exactly follows holding-buffer empty; Q6H writes are time-ordered |
| **W-A02** | D6 sticky underrun (forced empty shifter) and reset/exit recovery; normal ROM write has no false underrun |
| **W-A03** | Early/late Q7L exit and trailing `DE AA FF FF` produce expected number of serial bits, without instant forced drain |
| **W-A04** | A live track is unchanged while writing; candidate captures sub-byte positions, 48-bit sync and wrap; valid Q7L swaps atomically, identical bits do not dirty, read-after-write uses swapped data |
| **W-A05** | Candidate decoder validates cylinder/side/sector pairing, inverse 703 codewords, 524 bytes and checksums; partial/invalid sector or format is discarded without modifying active track |
| **W-A06** | Tagged DC42 guest write/reopen preserves exactly changed **512+12** and checksum convention, with unaffected sectors unchanged |
| **W-A07** | Raw/tagless data-only write works where representable; nonzero changed tags produce **explicit error**, not silent loss |
| **W-A08** | Interrupt during write discards only in-flight candidate; earlier swapped dirty track survives reset/stop/step; flush dirty old cylinder on step (both sides), eject/normal close; clean image never rewritten |
| **W-A09** | Failures in image open, decode, write, fsync, rename, concurrent mount and file alias cannot silently destroy original data or dirty buffer |
| **W-A10** | Original unmodified ROM `_Write`, `_Read` and fresh-process image reopen report correct data/tags |
| **W-A11** | Original ROM `CtlFormat`, `CtlVerify` and complete image reread work: format contains new transaction-provided headers for every sector, preserves measurable gaps, accepts ROM gap rewrite and cross-zone changes |
| **W-A12** | No regression to original-ROM read-only boot, write-protection `wPrErr`, SCSI/RTC/PRAM, eject/restart and performance |

Use disposable copies of known-good tagged System media and blank/formatted fixtures, recording original and final SHA-256. Build a focused deterministic test for each failure and boundary; merely passing a generic integration suite is not enough.

## 10. Closed architecture decisions and remaining direct validation

**Approved, no longer open:** temporary circular-track capture (not direct active mutation); transaction validation and swap at completed `Q7L`; rollback of an interrupted/invalid candidate; distinct dirty-active-track/image checkpoint; preserving tags only where the image format can represent them; preserving real ROM formatting gaps; optional reuse of the buffering principle on STM32F411.

**Verification, not design reopening:**

1. **First-byte timing:** exact phase from the first `Q7H` write to first serial bit and holding→shift transfer edge, measured with actual ROM-timed writes.
2. **Final-byte/underrun:** exact interaction between dummy `$FF`, D6 handshake, current serial bit and `Q7L` exit; do not fabricate a sub-byte hardware phase or require final filler to finish if valid meaningful content already did.
3. **Tags on raw media:** measure actual tag bytes emitted during original ROM writes, including System 5/6 media. Data-only raw/tagless checkpoints may proceed if the represented tags are zero/unchanged; do not silently discard nonzero modifications.
4. **Format/gap:** observe actual ROM gap measurement and retry behaviour from the candidate-swapped circular track; prove aborted partial formats cannot pass by reusing old sectors.
5. **Error signalling:** IWM D6 reflects transport underrun, not post-Q7L parse failures. Define precise trace/host-blocking behaviour and observe what the ROM can independently verify. A failed host-image checkpoint must retain already-swapped dirty tracks and prevent false safe ejection/exit.
6. **STM32:** buffer sizing, real-time capture while decoding, bit/edge format, possible ATF1508 assistance, flash power-loss behaviour and cartridge removal are later physical implementation decisions, not prerequisites to implement the virtual IWM.

## 11. References and project review

- [Mac Plus original ROM `.Sony` disassembly](https://github.com/jonathanschilling/mac_rom/blob/main/plus/resources/res_drvr_sony.s): `Sony_WrData` (~lines 2542–2724), `Sony_DoFormat` (~2957–3078), `Sony_FillInMarks` (~3081 onwards).
- [Apple SWIM/IWM compatible register specification](https://manualzz.com/doc/20452745/apple-swim-chip-user-manual): handshake D7/D6, holding buffer, fast asynchronous mode, GCR track data layout.
- [800K sector/track description](https://savagetaylor.com/til/TA45016.html) and [DiskCopy 4.2 image format](https://ciderpress2.com/formatdoc/DiskCopy-notes.html).
- Accepted [`iwm.c`](../devel/umac/src/iwm.c), [`disk_img.c`](../devel/umac/src/disk_img.c), [`PLUS-EMU-0003`](tasks/db/PLUS-EMU-0003.md) and [Document 04](04-iwm-emulation.md).

**Approval boundary:** The project owner accepted the **transactional circular-track architecture** on **20 November 2026**. Remaining timing/ROM details must still be demonstrated, and the write implementation remains **OPEN**, neither built nor verified by this documentation change. Implementation developers must stop after local `git commit`(s), report SHA and test evidence, and must **not `git push`** or merge.
