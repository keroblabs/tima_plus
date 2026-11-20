# PLUS-EMU-0003 — Implement unmodified-ROM read-only IWM and virtual floppy

**ID:** `PLUS-EMU-0003`
**Status:** `REVIEW`
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
- The IWM owns the soft switches, mode and read latch. The disk module owns the Sony drive, raw image loader and track encoder. [iwm.c](../../../devel/umac/src/iwm.c), [iwm.h](../../../devel/umac/include/iwm.h), [disk_img.c](../../../devel/umac/src/disk_img.c), [disk_img.h](../../../devel/umac/include/disk_img.h)
- The CPU bus integration accounts for peripheral access phases and connects VIA PA5 and the timers to the C8M timeline. [main.c](../../../devel/umac/src/main.c), [via.c](../../../devel/umac/src/via.c)
- At baseline `9c97aeb`, `main.c` had placeholder registers and the SDL frontend patched the Plus V3 ROM with a host-side floppy driver. The implementation uses the original ROM and omits the legacy `disc.c` from both builds.
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

### Verification commands and media

```sh
cmake -S devel/umac -B /tmp/tima-plus-iwm -DCMAKE_BUILD_TYPE=Release \
  -DUMAC_TEST_PLUS_ROM="$PWD/devel/umac/rom.bin" \
  -DUMAC_TEST_PLUS_FLOPPY=/tmp/tima-plus-system608.dsk
cmake --build /tmp/tima-plus-iwm --parallel 4
ctest --test-dir /tmp/tima-plus-iwm --output-on-failure
/tmp/tima-plus-iwm/umac_iwm_rom_test devel/umac/rom.bin \
  /tmp/tima-plus-system608.dsk --trace
```

- Macintosh Plus V3, 131,072 bytes, ROM checksum `4d1f8172`, SHA-256 `dd908e2b65772a6b1f0c859c24e9a0d3dcde17b1c6a24f4abd8955846d7895e7`. The loader verified the checksum and mapped the ROM read-only. The SDL `-W` output matched the input bytes.
- System 6.0.8 System Tools, 819,200 raw bytes, SHA-256 `4089f6ed6a39987a0f56207c25936bc61d1e7bcd0c969bbe92f3f6a28cfa7f50`. The data came from Apple's 800K disk 1 of 4. The Disk Copy data checksum `4027c0be` matched before extracting the raw logical sectors. [Apple System 6.0.8 distribution](https://download.info.apple.com/Apple_Support_Area/Apple_Software_Updates/English-North_American/Macintosh/System/Older_System/System_6.0.x/SSW_6.0.8-800k_Disk1of4.sea.bin)
- The IWM uses Apple's 14-FCLK acknowledgement timing and the Plus connection of FCLK to C8M. The track encoder follows the ROM's interleave, tag and checksum layout. [Apple IWM Rev. 19, data separator](https://www.brutaldeluxe.fr/documentation/iwm/apple2_IWM_Spec_Rev19_1982.pdf), [Apple Guide to the Macintosh Family Hardware, figure 9-5](https://vintageapple.org/inside_o/pdf/Guide_to_Macintosh_Family_Hardware_2nd_Edition_1990.pdf), [Apple SWIM reference, pp. 5–6 and 10–12](https://mirrors.apple2.org.za/www.bitsavers.org/pdf/apple/disk/sony/SWIM_Chip_Users_Ref_198801.pdf), [Apple TA45016](https://savagetaylor.com/til/TA45016.html)

### Desktop startup performance

The SDL loop added in `427e0d2` converts virtual C8M ticks into a host deadline and calls `SDL_Delay` after each approximately 5 ms slice. This caps desktop execution at one emulated second per wall-clock second on a sufficiently fast host. Baseline `9c97aeb` ran CPU slices as fast as the host could execute them and presented frames on host time. The current loop also presents one frame per guest VBL, so bypassing the sleep alone still presents every emulated frame during a fast boot. [SDL loop](../../../devel/umac/src/unix_main.c), [C8M run loop](../../../devel/umac/src/main.c)

All comparisons used a 4 MiB Debug build, fresh PRAM, SDL's dummy video driver with the software renderer, and tracing disabled. The SCSI case used the ROM and `HD10_512.hda` from the VS Code launch configuration, with an empty floppy drive. The floppy case used the 819,200-byte data portion of `plus-system.dsk`, verified against its Disk Copy checksum `6c542b16`. Its raw SHA-256 was `89c76e5f6b4fd24560933b0d85b0fa99aa70f11c1a92c3e8b4c1771ad5db6435`. The ROM and both media inputs retained their hashes.

Launch with `SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software` from a fresh working directory for each run, and use `-P test.pram`. For SCSI pass `-r rom.bin -s HD10_512.hda` with absolute input paths. For floppy pass the same ROM and `-d` with the extracted raw image. Measure wall time from launch to the first observation of the Pascal string `Finder` in `CurApName` at RAM offset `$910`. The measurements sampled `ram.bin` every 5 ms.

| Frontend configuration | SCSI only | 800K floppy |
| --- | ---: | ---: |
| Baseline `9c97aeb`, ROM patched at load | 0.561 s | 0.350 s |
| Current Debug, unmodified ROM and stock SDL loop | 43.505 s | 36.200 s |
| Current Debug core and ROM, host sleep bypassed in temporary SDL build | 3.521 s | 2.959 s |
| Current Debug core and ROM, host sleep bypassed and presentation capped at about 60 frames per host second in temporary SDL build | 0.599 s | 0.458 s |

- Instrumenting the paced frontend measured 38.527 s in `SDL_Delay` on the SCSI boot and 32.239 s on the floppy boot, about 89% of elapsed time in each case. The ROM completed RAM initialization at 30.432 virtual seconds in both cases. A ROM PC sample during that interval showed repeated execution of the memory-test loops around `$400E82` and `$400E96`.
- The A/B builds used the same CPU, VIA and IWM core. Every variant reached the first Finder marker at the same virtual timestamp for its medium, 43.502 s for SCSI and 36.198 s for floppy. With the sleep bypassed, drawing consumed 2.973 s for 2,616 SCSI boot frames and 2.519 s for 2,176 floppy boot frames. Limiting host presentation reduced the frame counts to 36 and 27 while still delivering guest VBL events.
- The release headless ROM test with the same extracted floppy completed in 0.512 s wall time. It booted to Finder, matched all eleven sector probes, rejected the write, and rebooted after recalibrating from cylinder 79. Waypipe presentation and interactive latency were **NOT TESTED** in this investigation.

### Review Findings

| Finding ID | Classification | Evidence | Required action / acceptance evidence | State |
| --- | --- | --- | --- | --- |
| `EMU3-R1` | `CORRECTION_REQUIRED` | After an explicit reset, the ROM at `$4007D8` waited for system ticks. Musashi cleared its IRQ input while VIA held a pre-reset interrupt. | Reset VIA registers and IRQ state on the current C8M timeline. Boot the original ROM from cylinder 79 and verify it returns to Finder. | `VERIFIED` |
| `EMU3-R2` | `FUTURE_SCOPE` | Bus phase, spindle spin-up, cylinder settling and TACH use deterministic software timing. Physical measurements were **NOT TESTED**. | Validate electrical timing and mechanics as part of [PLUS-DEV-0008](../../01-development-roadmap.md#plus-dev-0008--validate-the-physical-floppy-replacement). The software boot gate is covered by `EMU3-A5`. | `DEFERRED` |
| `EMU3-R3` | `CORRECTION_REQUIRED` | UBSan stopped the original System Software boot in Musashi's 32-bit `BCLR` when a signed mask shifted into bit 31. | Use unsigned masks for the 32-bit register bit operations. Repeat the boot and regression suite under ASan and UBSan. | `VERIFIED` |
| `EMU3-R4` | `CORRECTION_REQUIRED` | The new SDL host pacing raised the 4 MiB SCSI startup from 0.561 s to 43.505 s offscreen. A/B runs isolated the host sleep and per-guest-frame rendering costs. | Agree on the desktop speed policy. Measure comparable startup throughput with the same media, confirm presentation cadence, and verify original-ROM IWM boot and the SCSI, RTC/PRAM and input regressions. | `OPEN` |

**Notes**

- **2026-11-19 — Decision and registration:** Project owner approved splitting the authentic IWM + disk implementation into two components and two phases. This ticket covers Phase 1 read-only, **including disabling ROM patching**, with writing explicitly stubbed. The follow-on write ticket is `PLUS-EMU-0010` because `PLUS-EMU-0009` is already reserved/closed for RTC and PRAM. No code changes or tests were performed when this ticket was opened.
- **2026-11-19 — Status Change:** Started the approved implementation on local branch `plus-emu-0003-iwm` from `9c97aeb`. The requested disk module is `disk_img.c/.h`. The legacy paravirtual floppy source is excluded from the emulator build.
- **2026-11-19 — Baseline Test:** All six CTest cases passed in the Release build with the local Plus V3 ROM, including the ROM SCSI Manager test.
- **2026-11-19 — Implementation:** Added timed IWM soft switches, a single asynchronous read latch, Sony register multiplexing and read-only circular GCR tracks. The SDL frontend accepts raw 800K images and rejects floppy writes and media/output aliases. Both builds exclude the legacy floppy source. The native 512 × 342 display replaces the former ROM-patched resolution options.
- **2026-11-19 — Timing Investigation:** Musashi charges base instructions after their handlers. The trace compares its callback clock with an ordered 68000 bus-access estimate and explicit waits. For `MOVE.B (A4),Dn` at ROM PCs `$418C60` and `$418D02` the modeled sample was seven ticks after Musashi's elapsed timestamp at the callback. The displacement-based status access at `$418606` was eleven ticks later. The trace stayed monotonic. The Mac Plus PAL wait approximation uses a 352-clock line, a 256-clock display window, and a sound fetch slot. VIA T1 drives ROM Time Manager waits and VIA T2 runs at C8M/10 while the ROM measures TACH. The disk uses 120 ms spin-up, 3 ms step completion, and nominal 394/429/472/525/590 RPM. [Mac Plus PAL comparison](https://github.com/MiSTer-devel/MacPlus_MiSTer/blob/master/rtl/cpu_wait.v)
- **2026-11-19 — ROM Test:** `EMU3-A1` and `EMU3-A5` passed. The ROM wrote mode `$1F` at PC `$40011C` and read it back on Q7L at `$400120`. Trace events include the enable gate, the separate Sony spindle command, ready and track-zero sense, seek strobes, both heads, GCR reads and TACH edges near 9,940 C8M ticks apart at track zero. System 6.0.8 reached Finder in the headless integration test and SDL's offscreen frontend. The original `.Sony` matched eleven raw sectors covering both heads and all five zones.
- **2026-11-19 — Reset Rework:** The original-ROM reboot test exposed `EMU3-R1`. Reset now clears VIA interrupt and timer state at the current clock while the drive retains its cylinder. The repeat boot recalibrated from cylinder 79 and reached Finder.
- **2026-11-19 — Regression and Protection Tests:** `EMU3-A2` through `EMU3-A4` and `EMU3-A6` passed. Generated-image tests decoded all 1,600 sectors, checked ten-bit sync and wrap, and compared the encoded track and raw file after a rejected IWM write. The ROM returned `wPrErr` for a sector write and the input hashes were unchanged. The boot test exercised display, mouse movement and key down/up. The existing SCSI controller, disk, original-ROM SCSI Manager, RTC and PRAM tests passed. The prepared `HD10_512.hda` booted SCSI-only to Finder with the unmodified ROM and the image SHA-256 remained `d3796a03029cdb3a71b9f943375ee6e711541e2dc0c7406694d3913fc7fc2d53`. The four HFS image converter tests passed.
- **2026-11-19 — Build and Coexistence Tests:** All ten CTest cases passed at 1 MiB and 4 MiB of RAM. The standalone Make build linked and exposed the SDL CLI. With the 800K floppy and the read-only `320_32MB_drive.hda` both attached, System 6.0.8 booted and its volume queue contained `System Tools` and `3.2 32MB (D)`. The hard-disk SHA-256 stayed `fd03fdc74308848200c18d4690ef2db8159df7c650255e826ab56a3779236481`.
- **2026-11-19 — Sanitizer Tests:** Corrected `EMU3-R3` in the Musashi opcode templates. All ten CTest cases then passed under ASan and UBSan with `halt_on_error=1` for UBSan. Leak detection was disabled because LeakSanitizer could not attach in the sandbox.
- **2026-11-19 — Verification and Status Change:** The SDL loader rejected a 1.44 MB floppy and a corrupted V3 ROM before creating RAM or PRAM output. The implementation and tests are in local commit `427e0d2`. All six acceptance checks have developer evidence. Moved `IN_PROGRESS` → `REVIEW` for project-owner review.
- **2026-11-20 — Performance investigation and review:** Reproduced the owner-reported slowness with the current SCSI-only launch inputs and an 800K floppy. The controlled frontend comparison attributes the slowdown to host pacing and confirms the additional cost of drawing every guest frame during accelerated boot. Added `EMU3-R4` and marked `427e0d2` `CHANGES_REQUESTED`. The ticket stays in `REVIEW` pending agreement on the desktop speed policy. Diagnostic changes were confined to temporary builds.
- **2026-11-20 — Regression retest:** All ten existing CTest cases passed in the Release build, including the original-ROM floppy boot and SCSI Manager checks.

**Proposed Change**

Use `iwm.c/.h` and `disk_img.c/.h` with a timed-bit and drive-signal boundary. Load the original ROM and synthesize circular GCR tracks from read-only raw 800K sectors. Retain the write stub for Phase 1 and review the implementation and verification against the acceptance matrix.

For `EMU3-R4`, restore fast desktop execution or expose a choice of host pacing. Schedule host presentation independently of guest VBL. Derive guest VBL, VIA and IWM timing from C8M in each host speed mode.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |
| `plus-emu-0003-iwm` | `427e0d2` | `CHANGES_REQUESTED` | Implement and verify read-only IWM, Sony drive and original-ROM 800K boot. |

**Resolution Description**

Pending.
