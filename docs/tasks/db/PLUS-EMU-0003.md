# PLUS-EMU-0003 — Implement unmodified-ROM read-only IWM and virtual floppy

**ID:** `PLUS-EMU-0003`
**Status:** `IN_PROGRESS`
**Title:** Implement unmodified-ROM read-only IWM and virtual floppy

**Description**

Before this task, uMac booted floppy media through a patched Macintosh Plus ROM and a host-side paravirtual `.Sony` replacement. The placeholder IWM registers could not serve the original ROM's `.Sony` driver.

This phase implements IWM register timing and an independent virtual drive with a circular bitstream encoded from raw or DiskCopy 4.2 Macintosh 800K images. The drive is read-only and the write path is stubbed. Successful original-ROM boot is the release gate.

**Details**

- Kind: `TASK`
- Category: `PLUS-EMU`
- Primary milestone: [PLUS-DEV-0003](../../01-development-roadmap.md#plus-dev-0003--run-the-unmodified-macintosh-plus-rom); read-only prerequisite for [PLUS-DEV-0004](../../01-development-roadmap.md#plus-dev-0004--complete-the-virtual-800k-floppy-subsystem).
- Architecture: [04-iwm-emulation.md](../../04-iwm-emulation.md), especially §§1–2, 4–6 and 8.
- Dependent follow-on: [PLUS-EMU-0010](PLUS-EMU-0010.md) implements writing and persistence; **not part of this ticket**.
- The IWM owns the soft switches, mode and read latch. The disk module owns the Sony drive, image loader and track encoder. [iwm.c](../../../devel/umac/src/iwm.c), [iwm.h](../../../devel/umac/include/iwm.h), [disk_img.c](../../../devel/umac/src/disk_img.c), [disk_img.h](../../../devel/umac/include/disk_img.h)
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

- Open raw 819,200-byte 800K images and Macintosh 800K DiskCopy 4.2 images with valid headers and checksums. Preserve every source byte and reject unsupported or malformed images with a diagnostic.
- For the selected (track, side), load its 512-byte sectors using zone geometry 12/11/10/9/8 sectors over five 16-track bands, and the correct logical/physical sector ordering.
- Place the stored 12-byte tag ahead of its sector data in the encoded field. Use zero-filled tags for raw or tagless media. Encode both with Macintosh 6-and-2 packing, GCR codewords and the sector checksum, with address fields, prologues, epilogues, sync and gaps.
- Build a **circular bitstream** with a well-defined bit length/rotation position. Read from it according to elapsed virtual IWM ticks, not on demand from a requested sector number or from `PV_SONY_ADDR`. Rebuild/select buffers when stepping tracks or changing side; optionally cache more than one track/side.
- Open the backing image read-only and keep an immutable in-memory snapshot behind the disk and timing API.

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
| `EMU3-A3` | Disk encoder and circular stream | Decode both image formats and compare sector data, tags, address fields and checksums. Test ten-bit sync, track zones, circular wrap and bit positions. |
| `EMU3-A4` | Read-only medium protection | The ROM sees write protection and returns `wPrErr`. Compare track and image bytes after a rejected write. |
| `EMU3-A5` | **System Software boot** | Boot raw and tagged 800K media with the identified original ROM. Compare original `.Sony` data and tag reads across sides and zones. |
| `EMU3-A6` | Regression | Check desktop boot performance, guest input and clock rates, SCSI, RTC/PRAM, and display and input behavior. |

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

The SDL loop added in `427e0d2` converted virtual C8M ticks into a host deadline and called `SDL_Delay` after each approximately 5 ms slice. This capped desktop execution at one emulated second per wall-clock second. Baseline `9c97aeb` ran CPU slices as fast as the host could execute them and presented frames on host time. The paced loop also presented one frame per guest VBL, so bypassing its sleep alone still presented every emulated frame during a fast boot. [SDL loop](../../../devel/umac/src/unix_main.c), [C8M run loop](../../../devel/umac/src/main.c)

All comparisons used a 4 MiB Debug build, fresh PRAM, SDL's dummy video driver with the software renderer, and tracing disabled. The SCSI case used the ROM and `HD10_512.hda` from the VS Code launch configuration, with an empty floppy drive. The floppy case used the 819,200-byte data portion of `plus-system.dsk`, verified against its Disk Copy checksum `6c542b16`. Its raw SHA-256 was `89c76e5f6b4fd24560933b0d85b0fa99aa70f11c1a92c3e8b4c1771ad5db6435`. The ROM and both media inputs retained their hashes.

Launch with `SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software` from a fresh working directory for each run, and use `-P test.pram`. For SCSI pass `-r rom.bin -s HD10_512.hda` with absolute input paths. For floppy pass the same ROM and `-d` with the extracted raw image. Measure wall time from launch to the first observation of the Pascal string `Finder` in `CurApName` at RAM offset `$910`. The measurements sampled `ram.bin` every 5 ms.

| Frontend configuration | SCSI only | 800K floppy |
| --- | ---: | ---: |
| Baseline `9c97aeb`, ROM patched at load | 0.561 s | 0.350 s |
| `427e0d2`, unmodified ROM and paced SDL loop | 43.505 s | 36.200 s |
| Unmodified ROM, host sleep bypassed in temporary SDL build | 3.521 s | 2.959 s |
| Unmodified ROM, host sleep bypassed and presentation capped at about 60 FPS in temporary SDL build | 0.599 s | 0.458 s |
| Corrected SDL frontend, unmodified ROM | 0.728 s | 0.578 s |

- Instrumenting the paced frontend measured 38.527 s in `SDL_Delay` on the SCSI boot and 32.239 s on the floppy boot, about 89% of elapsed time in each case. The ROM completed RAM initialization at 30.432 virtual seconds in both cases. A ROM PC sample during that interval showed repeated execution of the memory-test loops around `$400E82` and `$400E96`.
- The A/B builds used the same CPU, VIA and IWM core. Every variant reached the first Finder marker at the same virtual timestamp for its medium, 43.502 s for SCSI and 36.198 s for floppy. With the sleep bypassed, drawing consumed 2.973 s for 2,616 SCSI boot frames and 2.519 s for 2,176 floppy boot frames. Limiting host presentation reduced the frame counts to 36 and 27 while still delivering guest VBL events.
- The release headless ROM test with the same extracted floppy completed in 0.512 s wall time. It booted to Finder, matched all eleven sector probes, rejected the write, and rebooted after recalibrating from cylinder 79. Waypipe presentation and interactive latency were **NOT TESTED** in this investigation.

### Correction verification

- The corrected 4 MiB Debug frontend booted the original `plus-system.image` in 0.575 s offscreen. As in the table, this measures from launch to the first Finder marker. A separate run measured 59.93 SDL presentations per second, 60.0 Hz guest `TickCount`, and two seconds of guest `Time` over 2.10 wall seconds. The desktop uses wall time for CA2/VBL and CA1/RTC. IWM and VIA timers continue to use virtual C8M.
- With Wayland and the system SDL library from the VS Code launch setup, the 4 MiB Debug frontend reached Finder in 1.035 s from `HD10_512.hda` and 1.061 s from `plus-system.image`. The floppy run measured 59.73 presentations per second, 60.5 Hz guest `TickCount` and two seconds of guest `Time` over 2.10 wall seconds. The integration tests checked guest input and framebuffer state. Manual Wayland gestures were **NOT TESTED**.
- The generated `iwm_diskcopy_image` fixture decodes all 1,600 data sectors and tags, checks both checksum conventions for the tag area and covers tagless DiskCopy. It rejects invalid header fields, inconsistent lengths and corrupted sector or tag data. Block zero has nonzero tags and is explicitly covered. The CLI rejected a checksum-corrupted copy before creating RAM or PRAM output.
- The original `.Sony` driver matched eleven 512-byte sectors and their 12-byte tags via its tag-buffer control call, reported `wPrErr` for a write, and rebooted from cylinder 79. The test passed with the reported DiskCopy image, Apple's original tagged System 6.0.8 System Tools image, and the raw extraction of Apple's image. The ROM and floppy files matched their input bytes after testing.
- The reported 838,484-byte `devel/umac/plus-system.image` has SHA-256 `11eef5df148b45c0e8e8a7cb1290a1c552e298f497ab7a15d92770479d77aca6`, data checksum `6c542b16` and tag checksum `ae2402e6`. Apple's tagged image has SHA-256 `c4abf2faa74fd789a081452e6df7fbe5afc73aa5ed432f60741688d95620dc02`, data checksum `4027c0be` and tag checksum `227a5720`. In both files the recorded tag checksum starts with the second tag. The drive retains the first tag and also accepts the full tag-area checksum described by Apple. [Apple DiskCopy 4.2 format](https://nulib.com/library/FTN.e00005.htm)

To repeat the corrected 4 MiB Release suite with the reported image, supply the ROM and disk to CMake. The desktop test also needs Python 3.

```sh
cmake -S devel/umac -B /tmp/tima-plus-iwm -DCMAKE_BUILD_TYPE=Release -DMEMSIZE=4096 \
  -DUMAC_TEST_PLUS_ROM="$PWD/devel/umac/rom.bin" \
  -DUMAC_TEST_PLUS_FLOPPY="$PWD/devel/umac/plus-system.image"
cmake --build /tmp/tima-plus-iwm --parallel 4
ctest --test-dir /tmp/tima-plus-iwm --output-on-failure
```

### Review Findings

| Finding ID | Classification | Evidence | Required action / acceptance evidence | State |
| --- | --- | --- | --- | --- |
| `EMU3-R1` | `CORRECTION_REQUIRED` | After an explicit reset, the ROM at `$4007D8` waited for system ticks. Musashi cleared its IRQ input while VIA held a pre-reset interrupt. | Reset VIA registers and IRQ state on the current C8M timeline. Boot the original ROM from cylinder 79 and verify it returns to Finder. | `VERIFIED` |
| `EMU3-R2` | `FUTURE_SCOPE` | Bus phase, spindle spin-up, cylinder settling and TACH use deterministic software timing. Physical measurements were **NOT TESTED**. | Validate electrical timing and mechanics as part of [PLUS-DEV-0008](../../01-development-roadmap.md#plus-dev-0008--validate-the-physical-floppy-replacement). The software boot gate is covered by `EMU3-A5`. | `DEFERRED` |
| `EMU3-R3` | `CORRECTION_REQUIRED` | UBSan stopped the original System Software boot in Musashi's 32-bit `BCLR` when a signed mask shifted into bit 31. | Use unsigned masks for the 32-bit register bit operations. Repeat the boot and regression suite under ASan and UBSan. | `VERIFIED` |
| `EMU3-R4` | `CORRECTION_REQUIRED` | The SDL host pacing raised the 4 MiB SCSI startup from 0.561 s to 43.505 s offscreen. A/B runs isolated the host sleep and per-guest-frame rendering costs. | Verify comparable startup throughput, host presentation and guest clock rates, original-ROM IWM boot, SCSI, RTC/PRAM and input. | `VERIFIED` |
| `EMU3-R5` | `CORRECTION_REQUIRED` | The SDL loader rejected `devel/umac/plus-system.image` because the disk backend required exactly 819,200 bytes. The reported 838,484-byte file has a DiskCopy 4.2 header, 1,600 data sectors and 1,600 sector tags. | Load the image directly, preserve all tags through GCR encoding, reject malformed containers, and test raw and tagged original-ROM boot and media protection. | `VERIFIED` |
| `EMU3-R6` | `CORRECTION_REQUIRED` | Before the correction, inactive /ENBL left the Sony spindle running and the next selection reported motor-on. The generated fixture reproduced the active TACH and ready state with both enables inactive. | Propagate effective per-drive enable transitions at their exact C8M ticks. Verify immediate and delayed motor-off, cancellation, drive selection, reset and original-ROM boot. | `VERIFIED` |

### EMU3-R6 — Motor-enable correction: developer handoff

**Approved 2026-11-20.** Phase-1 rework on branch `plus-emu-0003-iwm`. Only motor/enable propagation, its timing and regression tests are required here. Do not implement Phase-2 writing or media hot-swap; [Document 04 `12.10](../../04-iwm-emulation.md#1210-disk-lifecycle-insertion-eject-and-drive-power-software-and-future-stm32) specifies future eject/insertion behaviour.

**Reproduction:** enable internal drive, command Sony MOTORON=0, wait for ready, then deassert effective /ENBL1 (or switch selection to absent external). The current `iwm.c` has no drive-disable notification. `disk_img.c` stops the spindle only for explicit Sony motor-off/eject, so the previous spindle state survives and the next enable incorrectly reports motor already on.

**Required implementation:**

1. In `iwm.c`, compute *per-drive effective enables* before and after each IWM Motor-On/Drive-Sel state change. Drive select chooses /ENBL1 or /ENBL2. Note that `iwm_enabled()` remains true when the delayed motor-off timer is still pending. Notify only when an output actually changes; do not dispatch phantom commands to an absent external drive.
2. Add a narrow call such as `disk_img_set_enabled(disk, bool enabled, uint64_t tick)` (name is illustrative). Before a disable event, advance the disk to the **exact virtual C8M tick** of the change. Keep IWM control and timer bookkeeping in `iwm.c`; Sony spindle/ready/TACH/track behaviour remains in `disk_img.c`.
3. On genuine enabled→disabled, power down the Sony spindle, suppress active TACH and RDDATA, invalidate stale spin-up/ready eligibility and rearm the IWM read latch where the stream is lost. **Retain cylinder, side, inserted medium, write protection and disk image**; power-down is not eject. Handle any in-progress step deterministically without adding phantom steps.
4. On disabled→enabled, the interface becomes selectable but **Sony spindle stays off** until the ROM sends Sony MOTORON=0. Then normal spin-up and /READY timing must apply. Do not rewind the circular track or reset cylinder.
5. With IWM timer enabled, the Motor-Off soft switch schedules a future disable: **do not stop the spindle at the request**. When virtual time reaches/crosses `motor_off_at`, advance the drive to that precise tick, drop enable and stop the spindle. If Motor-On returns **before** expiry, cancel the timer without a disable event or spindle interruption. When the motor-off timer is disabled, disable is immediate.
6. Switching Drive-Sel while enabled deasserts the old drive and asserts the new; the former powers down. On IWM/CPU reset, if enable is lost, stop the spindle but preserve the mounted disk and current cylinder. Do not regress reset/recalibration from cylinder 79.

**Acceptance tests (add focused deterministic cases to `tests/test_iwm.c`):**

| ID | Stimulus | Required observable result |
| --- | --- | --- |
| `R6-T1` | Enable internal, start Sony spindle, immediate IWM Motor-Off (timer disabled) | /ENBL1 falls immediately; motor/TACH/data stop; next enable reports motor off |
| `R6-T2` | Motor-Off with timer enabled; sample just before and at deadline, including one long time jump | Spindle remains running while effective enable asserted; stops **at expiry**, not before |
| `R6-T3` | Re-enable before pending deadline | No spurious enable drop, stop or new spin-up |
| `R6-T4` | While internal is spinning, select absent external then return | Internal powers down; no cross-talk or auto-restart |
| `R6-T5` | No-op repeated switches, CPU/IWM reset, nonzero track | No duplicate transitions/phantom seeks/eject; media/cylinder survive reset and motor is off |
| `R6-T6` | Original unmodified-ROM boot, raw + tagged image, full existing CTest suite | Finder, tags, read-only write-protect, SCSI, RTC/PRAM, SDL speed, reset/recalibration and file hashes remain good |

Record timestamped trace with IWM Motor-On, mode, Motor-Off deadline, Drive-Sel, **effective /ENBL1/2**, Sony spindle, /READY, TACH, cylinder and medium-present. Report actual test commands and pass/fail. **Developer must make a local `git commit` and report SHA; must not `git push`, open a PR or merge.**

**Not part of this correction:** implementing media insertion UI, hot swapping, actuator timing or new eject mechanics. Losing /ENBLx must **never** eject the inserted medium.

#### EMU3-R6 verification

- `R6-T1` through `R6-T5` passed in `iwm_motor_enable`. The pre-fix library failed `R6-T1` at tick 1,202,870 with both /ENBL pins inactive, the spindle turning, /READY asserted and TACH high. After the fix the spindle and TACH stop at disable, a pending clear and the read latch are reset, and a new Sony start requires a fresh spin-up. At an enable cutoff the drive applies any step already due and cancels a step whose deadline is still ahead.
- `R6-T6` passed 13/13 CTests on 4 MiB Release with each of the raw and tagged 800K images. The unmodified V3 ROM reached Finder, matched 11 sectors and tags, returned `wPrErr` and recalibrated on reboot from cylinder 79. The reset checks confirmed the mounted disk, side and cylinder survived power-down. Both Release SDL tests reached Finder within 0.41 s and measured 60.0 Hz guest `TickCount` and two guest seconds in 2.10 wall seconds. The 4 MiB Debug SDL build also reached Finder in 0.791 s with SCSI alone and 0.768 s with SCSI and the tagged floppy attached. The ROM and all three image file hashes matched their inputs.
- The standard Debug suite passed 10/10 CTests. The 4 MiB Debug tagged suite under ASan/UBSan passed 13/13 with leak detection disabled in the sandbox. The standalone Make build linked and all four HFS converter tests passed.

The deterministic trace records C8M ticks and active-low pin levels. In `R6-T2` the exact deadline and the overshooting advance stop at the same rotational position.

```text
R6-T1 running t=1202870 motor=1 mode=1f off_at=0 drive=0 enbl=0/1 spindle=1 ready_n=0 tach=1 trk=0 side=0 media=1 bit=621
R6-T1 stopped t=1202870 motor=0 mode=1f off_at=0 drive=0 enbl=1/1 spindle=0 ready_n=1 tach=0 trk=0 side=0 media=1 bit=621
R6-T2 last enabled tick t=9581535 motor=0 mode=1b off_at=9581536 drive=0 enbl=0/1 spindle=1 ready_n=0 tach=1 trk=0 side=0 media=1 bit=2381
R6-T2 at deadline t=9581536 motor=0 mode=1b off_at=0 drive=0 enbl=1/1 spindle=0 ready_n=1 tach=0 trk=0 side=0 media=1 bit=2382
R6-T2 after jump t=10774720 motor=0 mode=1b off_at=0 drive=0 enbl=1/1 spindle=0 ready_n=1 tach=0 trk=0 side=0 media=1 bit=2382
R6-T3 canceled t=9581792 motor=1 mode=1b off_at=0 drive=0 enbl=0/1 spindle=1 ready_n=0 tach=1 trk=0 side=0 media=1 bit=2398
R6-T4 external selected t=1192928 motor=0 mode=1b off_at=9581536 drive=1 enbl=1/0 spindle=0 ready_n=1 tach=0 trk=0 side=0 media=1 bit=0
R6-T5 before reset t=987031 motor=1 mode=1f off_at=0 drive=0 enbl=0/1 spindle=1 ready_n=1 tach=1 trk=1 side=1 media=1 bit=61689
R6-T5 after reset t=987031 motor=0 mode=00 off_at=0 drive=0 enbl=1/1 spindle=0 ready_n=1 tach=0 trk=1 side=1 media=1 bit=61689
```

Executed test commands for the Debug, tagged Release, raw Release and sanitizer builds. The tagged and sanitizer builds used `devel/umac/plus-system.image` and the original `devel/umac/rom.bin`.

```sh
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake --build /tmp/tima-plus-iwm --parallel 3
ctest --test-dir /tmp/tima-plus-iwm --output-on-failure
ctest --test-dir /tmp/tima-plus-iwm --verbose -R '^iwm_motor_enable$'
/tmp/tima-plus-iwm/umac_iwm_rom_test devel/umac/rom.bin devel/umac/plus-system.image --trace
cmake -S devel/umac -B /tmp/tima-plus-r6.skxZ1r/raw -G Ninja -DCMAKE_BUILD_TYPE=Release -DMEMSIZE=4096 \
  -DUMAC_TEST_PLUS_ROM="$PWD/devel/umac/rom.bin" -DUMAC_TEST_PLUS_FLOPPY=/tmp/tima-plus-system608.dsk
cmake --build /tmp/tima-plus-r6.skxZ1r/raw --parallel 3
ctest --test-dir /tmp/tima-plus-r6.skxZ1r/raw --output-on-failure
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 cmake --build /tmp/tima-plus-iwm-sanitize --parallel 2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir /tmp/tima-plus-iwm-sanitize --output-on-failure
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s devel/umac/tests -p 'test_hfs_to_device.py'
```

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
- **2026-11-20 — Discovery:** Reproduced the reported DiskCopy mount failure with `-d devel/umac/plus-system.image` and the Plus V3 ROM. The file has valid 800K size fields and a matching data checksum. Its tag checksum matches after omitting the first 12-byte tag. Added `EMU3-R5`.
- **2026-11-20 — Decision and Status Change:** The project owner requested fixes for both open findings and a tagged-image regression test. Expanded the read-only media scope to Macintosh 800K DiskCopy 4.2 images. Started both corrections on `plus-emu-0003-iwm` and moved `REVIEW` → `IN_PROGRESS`.
- **2026-11-20 — Regression reproduction:** The generated tagged fixture failed to open when linked against the previous disk backend. The SDL boot regression timed out at 15 s on the paced frontend with the original V3 ROM and raw floppy.
- **2026-11-20 — Rework:** Removed CPU sleeps and scheduled rendering on its own host deadline. The initial uncapped build retained guest-clock VBL and drove `TickCount` at 6,856 Hz in a desktop test. Restored the pre-IWM wall-time VBL policy to preserve the original ROM's key-repeat and double-click intervals. VIA timer and floppy bit timing remain on C8M. Scheduled the RTC interrupt on wall time because the ROM adds one second for each CA1 interrupt. Added the read-only DiskCopy parser and threaded sector tags through the running GCR payload checksum.
- **2026-11-20 — ROM test correction:** The official tagged System Tools image reached Finder, then the probe hit System Error `$1C` after borrowing 1 KiB below the live stack pointer. The integration test now keeps the live stack and allocates its probe buffers through the guest Memory Manager. The tagged image then passed all sector, tag, protection and reset checks.
- **2026-11-20 — Verification:** Passed 12/12 CTests in 4 MiB Release with the reported tagged image, 11/11 in 1 MiB Release with raw System Tools, and 9/9 in Debug without external assets. Passed 12/12 in 4 MiB Debug under ASan and UBSan with leak detection disabled in the sandbox. All four HFS converter tests and the standalone Make build passed. Confirmed offscreen and Wayland startup and guest timing. The ROM and media hashes matched before and after the runs. Marked `EMU3-R4` and `EMU3-R5` `VERIFIED`.
- **2026-11-20 — Status Change:** Recorded both corrections in local commit `eb4322d3936afb16334d29ac04f023bd82b98ced`. Moved `IN_PROGRESS` → `REVIEW` for project-owner review.

- **2026-11-20 — Code Review / Changes Requested / Status Change:** Reviewed `db1d257b5bc8f861e2fae176003253667f88d89f` after developer and owner boot/SCSI/RTC checks. New `EMU3-R6`: effective /ENBLx loss does not stop Sony spindle. Owner approved the bounded correction, returning `REVIEW` → `IN_PROGRESS`. Added exact timing, reset, drive-switch and original-ROM regression acceptance. Eject/insertion is separately documented in 04 for later STM32 design, not added to Phase-1 code. No code changes or new tests in this documentation commit.
- **2026-11-20 — Rework:** Reproduced the stale spindle with the generated fixture linked to the pre-fix core at `b65729c`. The IWM now compares effective enable outputs when applying switches, expiring the off-delay or resetting. The disk backend advances to the transition tick and cancels any step still pending when enable is lost. Controller initialization is separate from live reset so reset can process a pending timer before clearing IWM state.
- **2026-11-20 — Verification:** Added `iwm_motor_enable` and reset assertions in the original-ROM test. Recorded the deterministic trace, test commands and raw/tagged boot and regression results above. Marked `EMU3-R6` `VERIFIED`.

**Proposed Change**

Use `iwm.c/.h` and `disk_img.c/.h` with a timed-bit and drive-signal boundary. Load the original ROM and synthesize circular GCR tracks from read-only 800K media. Retain the Phase 1 write stub.

For `EMU3-R4`, restore uncapped CPU execution in SDL. Use independent host deadlines for presentation at up to 60 FPS, guest VBL at about 60 Hz and the VIA one-second notification. Keep IWM and VIA timer timing on the virtual C8M clock. Verify fast startup alongside near-real-time guest `TickCount` and `Time`.

For `EMU3-R5`, validate Macintosh 800K DiskCopy 4.2 headers, lengths and checksums before attaching the disk. Encode each stored 12-byte tag ahead of its corresponding 512-byte sector. Use zero-filled tags for raw and tagless images. [Apple DiskCopy 4.2 format](https://nulib.com/library/FTN.e00005.htm)

For **EMU3-R6**, implement and test the precise effective-enable transition contract above; maintain the existing read-only/GCR/tag architecture and original-ROM boot. The broader medium lifecycle in Document 04 is future scope, except the mandatory rule that /ENBL loss does not eject media.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |
| `plus-emu-0003-iwm` | `427e0d2` | `CHANGES_REQUESTED` | Implement and verify read-only IWM, Sony drive and original-ROM 800K boot. |
| `plus-emu-0003-iwm` | `eb4322d` | `CHANGES_REQUESTED` | Tag/media and performance work reviewed; follow-up motor-enable correction `EMU3-R6` pending. |

**Resolution Description**

Pending.
