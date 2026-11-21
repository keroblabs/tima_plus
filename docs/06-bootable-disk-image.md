# Tima/Plus — Bootable 800K Disk Image and ROM Bootloader

**Status:** ROM boot-path reconnaissance and proposed architecture (21 November 2026). Not yet implemented or boot-tested as a Tima/Plus image.  
**Target:** Macintosh Plus M0001AP, unmodified Macintosh Plus V3 ROM, MC68000, internal 800K Sony floppy as modelled in uMac.  
**Working branch:** `pre-release-rom-test`. This document belongs to the Tima/Plus software track, not the parked `pre-release-umac` acceptance branch or the Apple CPLD project.  
**Related:** [Roadmap](01-development-roadmap.md), [Tima SDK](03-tima-sdk-integration.md), [IWM](04-iwm-emulation.md), [IWM write](05-iwm-write-implementation.md), [standalone ROM experiment](../devel/tima_rom/README.md), [PLUS-ROM-0001](tasks/db/PLUS-ROM-0001.md).

## 1. Goal and the essential distinction

Produce a deterministic, 819,200-byte **logical 800K floppy image** which the **unmodified Macintosh Plus ROM** recognises and executes, loading a vbcc/vasm/vlink-built Tima/Plus program. The previous `devel/tima_rom` experiment replaces the ROM with `--raw-rom` and is **not** the disk bootstrap or the production boot method. Retain it as a separately testable compiler/startup experiment.

Two very different boot targets exist:

- **A. Native Tima/Plus boot, recommended first:** the ROM reads and executes the first-stage code in the two disk boot blocks; that code loads a larger program from reserved logical sectors and **does not return to the Macintosh ROM's normal System-file boot flow**. This route needs no HFS filesystem, System file or Finder during initial bring-up. It still requires the actual ROM Sony/IWM driver, valid logical sectors, a working floppy image and correct boot-block execution. The term 'bootable' initially means *accepted and executed by this specific Plus ROM*, not *mountable in Finder*.
- **B. Conventional Macintosh startup:** after any boot-block code returns, the ROM initialises the File Manager, mounts an MFS/HFS volume, and loads named System resources and startup software. A synthetic `LK` signature alone will **not** satisfy these later requirements. Creating a complete classic Macintosh System volume is a separate, more complex project and is not needed for native Tima/Plus.

Do not modify the original ROM, patch the Sony driver, use paravirtual disk reads, or call the host directly from guest boot code. Image generation is host tooling; the bootloader runs as real 68000 code.

## 2. Evidence and authoritative source locations

### 2.1 Original Plus ROM disassembly

Main reference: [jonathanschilling/mac_rom — plus/](https://github.com/jonathanschilling/mac_rom/tree/main/plus).

| Component | Source | Observed behaviour |
| --- | --- | --- |
| Cold startup and hardware/memory preparation | [`plus/boot/vectors.s`](https://github.com/jonathanschilling/mac_rom/blob/main/plus/boot/vectors.s) (`EBootStart`, `E_SoftReset`, `ENABLE_INTERRUPTS`) | Initial ROM-overlay handoff; RAM test; VIA/IWM/SCC, traps, managers, driver and screen setup |
| Boot-media search | Same file, `WaitForBootDisk`, `HAPPYMAC_SHOWN` | Synchronous `_Read` of 0x400 bytes from logical byte offset 0; verify `0x4C4B`; show Happy Mac |
| Disk code handoff / conventional startup | [`plus/boot/boot_load.s`](https://github.com/jonathanschilling/mac_rom/blob/main/plus/boot/boot_load.s), `BootFromDisk` | Check high byte of version at boot-buffer offset +6; when equal to `0x44` execute `JSR 0x2(A6)`; if returned, initialise heaps, `_InitEvents`, `_InitFS`, `_MountVol`, resource files and startup app |
| I/O parameter block | [`include/sysequ.inc`](https://github.com/jonathanschilling/mac_rom/blob/main/include/sysequ.inc) | Offsets of `ioVRefNum`, `ioRefNum`, `ioBuffer`, `ioReqCount`, `ioActCount`, `ioPosMode`, `ioPosOffset` |
| Boot drive low memory | [`include/lowmem.inc`](https://github.com/jonathanschilling/mac_rom/blob/main/include/lowmem.inc) | `BootDrive` = `0x0210`; `BtDskRfn` = `0x0B34` |
| Physical-style Sony drive reads | [`plus/resources/res_drvr_sony.s`](https://github.com/jonathanschilling/mac_rom/blob/main/plus/resources/res_drvr_sony.s), `Sony_DiskPrime` | The ROM's `.Sony` driver services `_Read` as logical 512-byte disk blocks; IWM/GCR detail stays behind this interface |
| Current emulator image interpretation | [`devel/umac/src/disk_img.c`](../devel/umac/src/disk_img.c), [`devel/umac/src/unix_main.c`](../devel/umac/src/unix_main.c) | Raw 800K and DiskCopy 4.2 media handled by the existing emulator; do not add a new disk-reading shortcut |

Secondary specifications: [Inside Macintosh: Boot Blocks](https://dev.os9.ca/techpubs/mac/Files/Files-101.html), [Inside Macintosh: System Initialization](https://dev.os9.ca/techpubs/mac/OSUtilities/OSUtilities-189.html), [CiderPress II HFS notes](https://ciderpress2.com/formatdoc/HFS-notes.html). An independently published [68000 floppy boot-code example](https://gist.github.com/LongJohnCoder/503ccc12789483d68086c955de67b4a7) uses signature `4C4B` and version `4418`; useful as corroboration, **not** as a substitute for testing our ROM or a known bootable image.

### 2.2 What is confirmed, and what is not

**Confirmed by reading the referenced disassembly and current uMac source:** the two-block read; signature check; precise ROM version-byte equality; `JSR` into the disk buffer; ROM I/O fields; the subsequent HFS/System-oriented path; 800K raw image size and the geometry used by uMac.

**Not yet confirmed experimentally for a new Tima/Plus disk:** successfully entering our own disk code; 68000 registers and safe RAM bounds at the actual handoff; repeated ROM `_Read` calls from inside the custom stage one; executable location/relocation; multi-sector CRC and control transfer; full image boot under original ROM. These are explicit acceptance gates, **not claimed results**.

**Historical disk-image evidence still required:** an existing bootable raw 800K Macintosh Plus `.image` or `.dsk`, or a DiskCopy 4.2 `.image` with validated conversion. The project's previously used `plus-system.image` (local, not checked into Git) is the best initial reference because it is already known to boot in uMac. No byte-for-byte inspection of that private/local image has been performed while authoring this document. Do not invent its header values, filesystem type or hash.

## 3. Original ROM startup and exact handoff

The relevant flow, reconstructed from `vectors.s` and `boot_load.s`:

~~~text
MC68000 reset; ROM overlay active
  -> ROM validates/initialises RAM and disables overlay
  -> establishes VIA, SCC, IWM, RTC, A-line traps, I/O Manager
  -> installs ROM .Sony drive support; initialises display
  -> boot drive queue; optional SCSI discovery
  -> WaitForBootDisk:
       ioPosMode   = 1 (fsFromStart)
       ioPosOffset = 0
       ioBuffer    = A6
       ioReqCount  = 0x400 (1024 bytes)
       ioRefNum    = driver reference from drive queue
       ioVRefNum   = drive number from drive queue
       _Read
       if failure, or big-endian word at (A6) != 0x4C4B:
           eject/retry/try next boot device
  -> show Happy Mac
  -> BootFromDisk:
       if byte at A6+0x06 == 0x44:
           JSR (A6+0x02)       <-- optional disk-resident entry
       if disk code returns:
           configure heap/event/file managers
           _MountVol
           find System/Finder, load resources, launch app
~~~

**Important:** this is the **Plus ROM disassembly's specific test** (`CMPI.B #0x44,0x6(A6)`), not merely the generic later documentation's 'bit 6 is set' wording. Use a known-compatible test version such as **`0x4418`** for our initial hardware target, then verify the entire first 8 bytes in the image. Preserve this ROM-specific choice as a testable invariant; it is not automatically portable to every Macintosh ROM.

`JSR 0x2(A6)` runs a **68000 instruction** at offset `+2`, not a dereference of a 32-bit absolute pointer. The four bytes starting at `+2` should contain a fixed-size branch instruction (`BRA.W stage1_entry`, or equivalent) to the actual code beyond the header. **Force its width** so it occupies four bytes and does not move the version field away from `+6`.

The ROM initialises interrupts before this handoff; it is *not* a fresh CPU reset. The ROM `.Sony` driver and A-line traps remain available until our loader intentionally takes control and/or replaces vectors. Do not transplant `tima_rom/crt0.s` unchanged: its reset vectors, ROM overlay changes and 128K ROM linker layout are specifically for the separate ROM replacement experiment.

On the disassembly's startup path, `A6` is initially set to about half `MemTop` and is used as the 1024-byte boot-block buffer. This is **context, not a stable promised load address**: the stage-one code must not hardcode the physical address of `A6`. Its stack, buffer and system-heap assumptions need runtime and emulator confirmation.

## 4. Boot-block header: the first bytes of the image

Logical Macintosh floppy blocks are 512 bytes each. The **first two blocks, 0 and 1, are both loaded at once into the boot buffer**. Apple calls them the boot blocks, not an IBM PC MBR or a partition map. Motorola 68000 fields are big-endian; names are 16-byte Pascal strings (one length byte, up to 15 text bytes, then padding).

| Offset (hex) | Bytes | Field | Notes for Tima/Plus |
| --- | ---: | --- | --- |
| `00` | 2 | `bbID` | **`4C 4B` = `LK`**, checked by ROM |
| `02` | 4 | `bbEntry` | 68000 branch instruction to stage one; executed by Plus ROM if byte `+06` = `44` |
| `06` | 2 | `bbVersion` | Proposed **`44 18`** for initial Plus-only test; high byte controls observed ROM call |
| `08` | 2 | `bbPageFlags` | Initial zero, confirm against real reference |
| `0A` | 16 | `bbSysName` | Pascal string; typically `System`; not used if our code never returns |
| `1A` | 16 | `bbShellName` | Typically `Finder` |
| `2A` | 16 | `bbDbg1Name` | Optional first debugger/patch filename |
| `3A` | 16 | `bbDbg2Name` | Optional second debugger/patch filename |
| `4A` | 16 | `bbScreenName` | Typically `StartUpScreen` |
| `5A` | 16 | `bbHelloName` | Startup application, typically `Finder` |
| `6A` | 16 | `bbScrapName` | Typically `Clipboard` |
| `7A` | 2 | `bbCntFCBs` | FCB count; inspect reference |
| `7C` | 2 | `bbCntEvts` | Event queue count; inspect reference |
| `7E` | 4 | `bb128KSHeap` | System heap setting |
| `82` | 4 | `bb256KSHeap` | Reserved/legacy |
| `86` | 4 | `bbSysHeapSize` | Heap size for ≥512K Macintosh |
| `8A` | 2 | Filler | Usually zero |
| `8C` | optional 4 | `bbSysHeapExtra` | Additional newer-style header field |
| `90` | optional 4 | `bbSysHeapFract` | Additional newer-style header field |
| `8C` or `94` | remainder | First-stage code | Old- vs new-style header length; explicitly branch to chosen offset |

For our **first `0x4418` experiment**, use the old-style header through `+0x8B`, starting code at `+0x8C`; explicitly branch from `+2` to `stage1_entry`. Both blocks together give 1024 bytes, so this leaves approximately 884 bytes for code and static data. All offsets and sizes must be checked by the assembler/linker and host-side image validator. Do not let a changing instruction size or compiler padding silently move fields.

**Compatibility warning:** Apple's later general boot-block format describes version high-byte bits and two header styles, but the Plus V3 code cited above compares for **`0x44`** exactly. That legacy-specific value includes bits not described as general-purpose in later documentation. Our implementation must prioritise observed ROM behaviour and an empirical known-good reference over spec-based guesses.

## 5. Raw 800K format, sector addresses and image layouts

The standard raw data image is exactly:

- **80 cylinders × 2 sides**, with 512-byte logical blocks and five variable-density zones.
- Cylinder ranges `0–15`: 12 sectors/side; `16–31`: 11; `32–47`: 10; `48–63`: 9; `64–79`: 8.
- Total = `2 × 16 × (12+11+10+9+8) = 1600` blocks = **819,200 bytes**.
- The project's `disk_img.c` uses `first_block(cylinder,side)` to arrange logical data sequentially: for each cylinder, side 0 sectors then side 1 sectors. It builds GCR-encoded tracks on the fly; **the host image contains logical 512-byte sector payloads, not GCR bitstreams**.

The first logical blocks in our proposed **native Tima/Plus image**:

| LBA | Byte range (raw image) | Proposed contents |
| --- | --- | --- |
| `0` | `0x0000–0x01FF` | Boot header and start of stage-one code |
| `1` | `0x0200–0x03FF` | Remaining stage-one code and padding |
| `2` | `0x0400–0x05FF` | Versioned stage-two manifest/header |
| `3 … N` | `0x0600 …` | Padded stage-two executable payload, optionally followed by resources |
| Remaining | Through `0x0C7FFF` | Zero-fill initially; future filesystem/other storage uses must be versioned |

There is **no HFS Master Directory Block** in this initial native layout: block `2` is ours. In a conventional HFS floppy, the MDB begins at block 2 and has its own signature `BD` (`0x4244`). We must never simultaneously claim that block 2 is our payload manifest **and** a valid HFS MDB. A later hybrid/HFS-compatible design needs an explicit separate layout and builder.

**Image formats:** uMac reads raw 819,200-byte media and DiskCopy 4.2 media. DiskCopy 4.2 has an **84-byte header**, 819,200 bytes of data and optional **12-byte per-sector tags**; those tags total 19,200 bytes. Thus one tagged example is **838,484 bytes**. DiskCopy header and checksum fields are implemented in `disk_img.c`; do not treat byte 0 of a DiskCopy file as the first boot block. The primary new image builder should emit **raw 819,200-byte files** first. Conversion to DiskCopy/tagged format is a separate tested option if needed.

For the first-stage loader, reading a logical byte offset of `0x600` is **LBA 3**, independent of which GCR zone or side holds that block. Delegate cylinder/side selection and GCR conversion to the original ROM disk driver.

## 6. Proposed bootloader architecture

### 6.1 Stage one: assembly in boot blocks 0–1

Use `vasm` to build a tiny MC68000 stage one and a fixed-width `BRA.W` in `bbEntry`. **Do not compile a full C runtime into the 1024 boot bytes.** Initial responsibilities:

1. Establish a small private work area and preserve any ROM state needed to call `_Read`; the ROM has already set up an execution stack. Do not write into the 1024-byte boot buffer while code runs from it.
2. Record a diagnostic marker at an explicitly safe location and/or draw a distinct framebuffer pattern, for the *first, no-stage-two* smoke test. A framebuffer write must respect the current screen page and RAM size.
3. For the full loader, issue **synchronous** `_Read` (`A002`) requests using the ROM's boot drive and driver reference. Read LBA 2 first, validate the stage-two header, then load the declared number of blocks beginning at LBA 3.
4. Check both I/O status and the transferred byte count; bounds-check load destination, sector count and image length. Verify manifest version, size, entry offset and payload checksum before transfer.
5. Set up the Tima/Plus program's agreed entry registers/stack and **jump** to its entry point; **do not RTS into `BootFromDisk`** on success.
6. On failures, show a predictable diagnostic marker/error pattern, stop or enter a defined recovery path; never execute partially loaded contents. A clean re-boot/eject path is a later improvement.

A direct ROM disk I/O call uses these parameter-block offsets (from `include/sysequ.inc`):

| Offset | Field | Value |
| --- | --- | --- |
| `0x0C` | `ioCompletion` | Zero for synchronous operation |
| `0x10` | `ioResult` | Inspect after call |
| `0x16` | `ioVRefNum` | Word from boot drive (`BootDrive` at `0x0210`) |
| `0x18` | `ioRefNum` | Word from boot driver (`BtDskRfn` at `0x0B34`) |
| `0x20` | `ioBuffer` | Destination address |
| `0x24` | `ioReqCount` | Number of bytes, a multiple of 512 |
| `0x28` | `ioActCount` | Number of bytes actually read |
| `0x2C` | `ioPosMode` | `1` (`fsFromStart`) |
| `0x2E` | `ioPosOffset` | Byte offset, e.g. `0x400` for LBA 2 |

Parameter block is **0x32 (50) bytes**, at least word-aligned, fully initialised with reserved fields set to zero. Pass its address in `A0` to the `_Read` trap; validate `D0`, `ioResult` and `ioActCount`. The ROM's initial boot read demonstrates this interface; its repeatability from our own code must be tested. No direct IWM soft-switch programming is required.

A schematic (not copy-paste-ready or tested) for the first-stage read:

~~~asm
    ; A0 -> a zeroed 0x32-byte IOParam block in safe RAM
    move.w  (0x0210).w,0x16(a0)    ; boot drive
    move.w  (0x0B34).w,0x18(a0)    ; boot driver refnum
    move.l  #stage2_header,0x20(a0)
    move.l  #512,0x24(a0)
    move.w  #1,0x2c(a0)           ; fsFromStart
    move.l  #1024,0x2e(a0)        ; LBA 2
    .word   0xA002               ; _Read, synchronous
    tst.w   d0
    bne     boot_error
    cmpi.l  #512,0x28(a0)
    bne     boot_error
~~~

The real stage one must include work-area allocation, preservation of live registers, correct trap semantics, and destination relocation. Do **not** copy `tima_rom`'s reset vector table or ROM overlay logic into it.

### 6.2 Stage-two manifest: proposal, not yet a frozen format

Reserve LBA 2 as a **single 512-byte manifest** with a versioned, big-endian structure. Suggested fields: four-byte magic `TPLS`; two-byte format version; two-byte header size; 32-bit payload byte count; 32-bit payload sector count; 32-bit load address; 32-bit entry offset; 32-bit CRC-32 of the unpadded payload; 32-bit required RAM profile / feature flags; remaining reserved zeros. Finalise offsets, CRC parameters, maximum size, padding and endianness in the implementation ticket **before writing the packer or loader**.

For the *first end-to-end experiment*, a 4 MiB-only, link-at-fixed-address stage two is acceptable if we document and verify a safe address against the original ROM's stack, system heap, boot-block buffer and screen/sound buffers. **Do not assume the former ROM experiment's `0x00400000` ROM linker address is a usable RAM execution address.** A more general relocatable/PIC or runtime-allocated loader should follow once the smallest working path is proven.

The loader's entry contract must define supervisor state, interrupt mask/ownership, stack, initial `A0/A1/D0` values if any, RAM ownership, vector ownership, ROM trap availability and the eventual transition from ROM services to native drivers. Avoid disabling interrupts or replacing traps before the final disk read has finished. The C runtime for stage two should initialise its own `.data` and `.bss` (or have the loader do so by a documented agreement); it should not assume reset-time machine state.

### 6.3 Stage two: current C experiment adapted

Move the *behavioural test* (RAM marker, then 1bpp graphics) from `devel/tima_rom/boot.c` into a separate disk-loaded target when ready. Keep its compiler validation and existing ROM tests intact. Create a **separate linker script and startup assembly** for a RAM-loaded program. Reuse the proven vbcc → vasm → vlink toolchain, **not** the old ROM image packer or reset startup unchanged.

The acceptance display can be a recognisable text/rectangle pattern on the **512×342, 1bpp** screen, with a distinct marker so screenshot and RAM-content checks agree. Run through the normal uMac `-r` ROM path plus `-d` disk image; never `--raw-rom` for disk-boot acceptance.

## 7. Deterministic image-builder and inspector (planned)

Create a host-side `tools/` program (e.g. `make_tima_floppy.py`; **not present yet**) with separate commands to **inspect**, **build**, and **verify**. Initially target raw images only; produce no Apple software, ROM or copyrighted System resources in the repository.

Required builder checks:
- Zero-fill exactly 819,200 bytes; install the 1024-byte stage one at offset 0, manifest at offset 1024 and padded stage two at offset 1536.
- Validate first word `4C4B`; byte `+6 == 44`; branch width/target; first stage ≤1024 bytes; manifest magic/version; stage-two count ≤ remaining 1597 blocks; nonzero payload; entry in payload bounds; load region and maximum RAM profile; CRC; padding.
- Reject incompatible or truncated input; deterministic output/hash for identical binaries.
- Emit an inspectable manifest file (build metadata, toolchain version and SHA-256 hashes); do **not** incorporate the copyrighted Macintosh ROM/System image.

A minimal inspection of a *known* image can be performed without adding a project utility yet:

~~~sh
# Historical or project-owned image. Do not modify the source.
stat -c '%n %s bytes' plus-system.image
sha256sum plus-system.image
python3 - plus-system.image <<'PY'
from pathlib import Path
import sys, struct
p = Path(sys.argv[1])
buf = p.read_bytes()
if len(buf) == 819200:
    data = buf; fmt = 'raw 800K'
elif len(buf) in (819284, 838484):
    data = buf[84:84+819200]; fmt = 'DiskCopy 4.2 candidate'
else:
    raise SystemExit(f'Unknown image size: {len(buf)}')
print('format:', fmt, 'size:', len(buf))
print('boot block first 16:', data[:16].hex(' '))
print('signature:', hex(struct.unpack_from('>H', data)[0]))
print('entry bytes:', data[2:6].hex(' '))
print('version:', hex(struct.unpack_from('>H', data, 6)[0]))
print('MFS/HFS volume signature at LBA2:', data[1024:1026].hex(' '))
for name, offset in [('System', 0x0A), ('Shell', 0x1A),
                     ('Startup app', 0x5A)]:
    n = data[offset]
    print(name, ':', data[offset+1:offset+1+min(n,15)].decode('mac_roman',
                                                               errors='replace'))
PY
~~~

**Inspector caveat:** a size-based DiskCopy candidate is only a first pass. A proper inspector must check DiskCopy type/format, data and tag lengths and both checksums using the implementation in `disk_img.c` before accepting it as valid. Conversely, an image having `LK` alone is not proof that executable code is enabled or that Macintosh System Software will load.

### Existing boot-disk comparison checklist — evidence to collect

For at least one **known-booting 800K image** (prefer the same local `plus-system.image` used in uMac tests), record in this document on the development branch:

1. Filename outside Git, image size, container type, SHA-256, whether uMac boots it with the unmodified Plus V3 ROM.
2. Actual first `0xA0` bytes (short hex dump), signature, branch bytes, version, names and heap settings; disassemble the first-stage region if enabled.
3. Confirm block-2 signature (`BD` for HFS, another value if MFS), separately from the `LK` boot-block signature; identify filesystem only after checking the relevant structure.
4. Compare initial read sector ranges and code execution in an emulator trace; if version does not request execution, show that the ROM instead follows the conventional System-file path.
5. Record whether tags are present and image format is raw or DiskCopy; **never commit historical System/ROM images**.

At document creation these values are **PENDING IMAGE INSPECTION**. A public candidate raw image exists in [DosFox1/OSHintosh — Mac Classic XO ROM System Disk.dsk](https://github.com/DosFox1/OSHintosh/blob/main/ROM-inator%20data/disk%20images/Mac%20Classic%20XO%20ROM%20System%20Disk.dsk) and is listed as 819,200 bytes, but its boot-block bytes and Plus V3 compatibility **have not been verified**. Prefer the already-known local working Plus disk over that unverified public candidate.

## 8. Implementation sequence and verification gates

| Gate | Deliverable | Acceptance / recorded evidence |
| --- | --- | --- |
| **B0 — inspection** | Parse one original booting disk and record header, image hashes, structure and ROM trace | Real byte values replace the pending checklist; no source ROM modification |
| **B1 — minimal boot sector** | Fixed 1024-byte boot blocks, `LK` + validated `4418`, with 68000 diagnostic code | uMac with original Plus V3 ROM and `-d new.img` reaches the diagnostic and does not attempt `_MountVol` |
| **B2 — disk reads** | Stage one reads manifest block at LBA 2 using `_Read`, then sector pattern at LBA ≥3 | Correct bytes/`ioActCount`/status, including cross-track and cross-zone reads |
| **B3 — complete loader** | Versioned manifest + CRC, N-sector stage-two, RAM startup/linker contract | Stage two executes from RAM and shows a unique framebuffer marker; failing CRC cannot execute |
| **B4 — reproducibility and regressions** | Image builder/inspector, automated tests, failure cases | Rebuild SHA stable; wrong LK, wrong flag, truncated media, bad manifest, oversized payload and failed read handled; historical Macintosh floppy and SCSI boots still pass |
| **B5 — expand to Tima/Plus** | Macintosh BSP and shared Tima 6.x SDK integration, later RAM profiles | Same RAM-loaded application consumes stable SDK APIs; 1/2/4 MiB constraints addressed individually, and physical floppy track tested separately |

Initial **B1** may create an 800K file with only two meaningful boot sectors and zero-fill afterwards. That test is intentionally *not* a valid HFS volume and should never be described as one. **B3** is the first true multi-stage OS-style boot.

## 9. Scope boundaries and open technical decisions

- **First accepted RAM profile:** 4 MiB in uMac, to preserve the verified `tima_rom` test setup; later 1/2 MiB support requires independent memory-layout acceptance.
- **ROM variants:** target **Plus V3** first. Repeat version-byte and bootstrap tests for other ROMs only after it works; do not assume the precise dispatch is universal.
- **Stage-two payload:** initial contiguous fixed logical sectors with explicit versioned header; no HFS catalogue, forks, extents or blessing required for the native takeover. A future Finder-mountable/hybrid image requires a separately specified HFS allocation map, MDB and catalogue.
- **Read path:** use original `_Read`/`.Sony`, not direct IWM registers. This preserves later real-hardware compatibility and allows IWM write work to remain independent.
- **Memory safety:** choose and verify a stage-two load address, work area and stack; avoid A6 boot buffer, ROM-managed heap/stack, low-memory globals, high-RAM screen/sound, and any overlay alias. This is the key design risk.
- **Development separation:** `devel/tima_rom` stays a ROM compiler/runtime test; the new `tima_disk` (name provisional) gets its own startup, linker and image packer. The physical STM32F411 disk emulator/Unicorn work (`PLUS-DEV-0011`) is a parallel track and not a prerequisite.
- **Ownership/review:** new ticket(s) should follow `docs/tasks/` methodology, committing locally and recording evidence; the owner retains final review/PR authority. Do not silently close `PLUS-ROM-0001` or the uMac release gate as a side effect of this research.

## 10. Recommended first engineering ticket

**`PLUS-SYS-0001` (existing reserved programme identifier) — bring up a ROM-invoked boot-block stub and prove normal uMac floppy boot.**

The developer should implement **B0 and B1 only** first: inspect a verified original image; write a 1024-byte 68000 boot block (forced four-byte branch at `+2`; `LK` and `4418`; static checks); pack it into a raw 819,200-byte image; boot it through the unmodified ROM; record a guest RAM diagnostic and/or visible graphics; and prove the normal System disk still boots. Then raise/decompose stage-two manifest and ROM-read work as the next ticket when the observed entry contract is known.

**Do not merge this research with the outstanding `tima_rom` owner review.** They are different artifacts: ROM reset experiment versus disk-loaded code.

---

### Research record

**2026-11-21:** Located and reviewed the exact Plus ROM code paths (`WaitForBootDisk` and `BootFromDisk`), ROM `_Read` parameter-block definitions and uMac's logical 800K/DiskCopy media model. Established a two-stage native boot plan with no early HFS requirement, and identified the remaining empirical disk-image, RAM-location and execution tests. This document is an implementation plan, **not a claim that a Tima/Plus boot image has already run**.
