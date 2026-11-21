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

**Historical disk-image evidence:** The owner has supplied the **entire first 128 bytes**, a subsequent **272-byte boot-block code/table region (`0x80–0x18F`)**, and the **first 48 bytes of LBA 2** from a non-tagged `.dsk`. Sections 2.3–2.5 decode these. The HFS MDB suggests a **1,474,560-byte 1.44 MB disk**, not an 819,200-byte 800K floppy; confirm by checking the actual file size. Neither a full boot trace nor a file hash has been supplied. The separately used local `plus-system.image` is known to boot in uMac, but must **not** be assumed to be this particular `.dsk`.

### 2.3 User-supplied historical boot-block bytes (complete header, 21 November 2026)

The owner supplied **all 128 initial data bytes** (`0x00–0x7F`), then **272 bytes at `0x80–0x18F`**, from a **non-tagged** Macintosh `.dsk`. Taken together they provide the 138-byte legacy header followed by the entire 262-byte code/table region ending at the observed `RTS`. The full original disk image remains outside Git.

```text
Offset    Bytes             Meaning
0x0000    4C 4B             bbID = 'LK' (accepted by Plus ROM)
0x0002    60 00 00 86       68000 BRA.W +0x0086 (bbEntry)
0x0006    44 18             bbVersion = 0x4418
0x0008    00 00             bbPageFlags = 0
0x000A    06                Pascal System name length = 6
0x000B    53 79 73 74 65    'Syste'; following 'm' is beyond the supplied bytes
```

**Critical correction (verified against actual executable bytes):** at `A6+0x02` the ROM's `JSR 0x2(A6)` executes the four-byte `BRA.W` instruction. On MC68000, its `0x0086` displacement is relative to **opcode address +2**, i.e. `0x0004`, so **target `0x0004 + 0x0086 = 0x008A`**, not `0x008C`. At `0x8A`, the specimen begins `4A 78 02 8E` (`TST.W ($028E).W`, the `ROM85` low-memory word); `0x8C` is the address **extension word** of that first instruction and not an executable start. The byte at offset `+6` is `0x44`, satisfying the Plus V3 ROM's executable-boot-code condition. This agrees with Motorola's [M68000 Programmer's Reference Manual (BRA instruction)](https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf) and a [standalone 68000 Macintosh boot-block assembly example](https://gist.github.com/LongJohnCoder/503ccc12789483d68086c955de67b4a7) which places `start` immediately after `bbSysHeapSize`. **Note:** CiderPress II's HFS notes describe the same encoding but calculate `0x8C`; that contradicts the machine-code boundary observed here. Avoid copying that erroneous destination into our assembler or image validator.

**Measured region:** the supplied 272-byte stream begins at `0x80`. Its first ten bytes finish the old-style header (through offset `0x89`). Executable code and embedded lookup tables span `0x8A–0x18F` (**262 bytes total**), ending with `4E 75` (`RTS`) at `0x18E`. The owner reports that subsequent bytes repeat `0xDA` (padding), consistent with a published, byte-for-byte **matching first-sector specimen** in [a November 2022 MacRumors boot-block dump](https://forums.macrumors.com/threads/question-how-powerful-of-a-graphics-card-will-work-in-a-beige-power-macintosh-g3.2303689/page-31), which displays the same bytes from offset `0x80` through `0x18F` and `DA` padding at `0x190–0x1FF`. **This does not establish that the owner's `.dsk` is the same disk or that its whole image is valid.** The code itself is partly instructions and partly embedded ROM-version tables, not 262 consecutive instruction bytes.

**Names and final header bytes verified from the complete supplied prefix:** `System` (`+0x0A`, 6), `Finder` (`+0x1A`, 6), `MacsBug` (`+0x2A`, 7), `Disassembler` (`+0x3A`, 12), `StartUpScreen` (`+0x4A`, 13), `Finder` (`+0x5A`, 6), `Clipboard` (`+0x6A`, 9). Each occupies a 16-byte Pascal-string field (length, characters, zero padding). Boot header fields: `bbCntFCBs` `+0x7A` = 10; `bbCntEvts` `+0x7C` = 20; `bb128KSHeap` `+0x7E` = `0x00004300`; `bb256KSHeap` `+0x82` = `0x00008000`; `bbSysHeapSize` `+0x86` = `0x00020000`. These are historical System-startup configuration values, not requirements for the native Tima/Plus no-return loader.

**Still pending for this specimen:** full 1024-byte boot blocks including padding and any second-block contents, actual total file size and SHA-256, and the exact execution/ROM trap trace. LBA 2 has now been partially inspected in §2.5. **Do not assume this source image boots in the current 800K-only uMac floppy path.**

### 2.4 Decoding the actual legacy boot-block program (0x8A–0x18F)

**Purpose:** The supplied historical first-stage code is a *Macintosh System startup compatibility shim*, **not a raw block loader**. It uses many ROM traps but contains **no `_Read` (`A002`)** instruction in the available code. Rather than read a kernel from LBA 2+, it initialises classic Macintosh managers, mounts the boot volume, and either jumps to a ROM-specific continuation or a `'boot'` resource. This behaviour must **not** be copied indiscriminately into the Tima/Plus native boot stub.

**First instructions and dispatch:**

~~~asm
008A: 4A78 028E      TST.W   ($028E).W          ; ROM85 flag
008E: 6B46           BMI.S   $00D6              ; error path
0090: 2078 02AE      MOVEA.L ($02AE).W,A0       ; ROMBase
0094: 3228 0008      MOVE.W  $0008(A0),D1       ; ROM machine/version word
0098: 7CFE           MOVEQ   #-2,D6
009A: 5446           ADDQ.W  #2,D6
009C: 303B 603C      MOVE.W  (0x3C,PC,D6.W),D0 ; ROM-version table at $DA
00A0: 6758           BEQ.S   $00FA              ; unknown/sentinel version
00A2: B240           CMP.W   D0,D1
00A4: 66F4           BNE.S   $009A              ; scan next table entry
...
00BC: 584F           ADDQ.W  #4,A7              ; discard original JSR return
00BE: 2E0F           MOVE.L  A7,D7              ; save stack/IOParam position
00C0: 6138           BSR.S   $00FA              ; common startup helper
00C2: 323B 6022      MOVE.W  (0x22,PC,D6.W),D1 ; success ROM-offset table at $E6
00C6: 4A40           TST.W   D0                 ; helper result
00C8: 6704           BEQ.S   $00CE
00CA: 323B 6024      MOVE.W  (0x24,PC,D6.W),D1 ; failure ROM-offset table at $F0
00CE: 2078 02AE      MOVEA.L ($02AE).W,A0     ; ROMBase
00D2: 4EF0 1000      JMP     (0,A0,D1.W)      ; continue *inside* ROM
00D6: 7062           MOVEQ   #$62,D0
00D8: A9C9           _SysError
~~~

For Macintosh Plus V3, the ROM machine/version word at `ROMBase+8` is **`0x0075`**, matching the **first table entry at `0xDA`** (`D6 = 0`). Its two ROM continuation offsets are `0x0A44` (success; within the ROM's resource-startup sequence near `REOPEN_RESOURCES`) and `0x0B82` (error; maps to `MOUNT_FAILED_EJECT` in the Plus V3 symbol map). This is a strong independent confirmation that the shim is returning control to the ROM startup process rather than loading raw sectors itself.

The code also conditionally adjusts `SysZone`/`_SetApplBase` using the boot header longword at `0x86` (`0x00020000` in this specimen). The data bytes between `0xDA` and `0xF9` are **lookup tables**, not independent executable instructions. Do not blindly linear-disassemble through this area.

**Common helper at `0xFA` (major calls):**

| Offset | Key bytes/trap | Interpreted operation |
| --- | --- | --- |
| `0xFA–0x104` | `LEA` of header `+0x0A`, `A02E` | Copy 16-byte `System` Pascal string to `SysResName` at `0x0AD8` |
| `0x106–0x110` | `LEA` of header `+0x1A`, `A02E` | Copy `Finder` name to `FinderName` at `0x02E0` |
| `0x112–0x120` | `LEA` of header `+0x6A`, `A02E` | Copy Scrap/Clipboard name; initialise `ScrapName` pointer `0x096C` and data at `0x0970` |
| `0x122–0x12C` | `A06D`, `A06C` | `_InitEvents` using header `+0x7C`, `_InitFS` using header `+0x7A` |
| `0x12E–0x138` | `A00F` | Set boot volume refNum from `BootDrive` (`0x0210`); `_MountVol` |
| `0x13A–0x160` | `A207`, `A260`, `A015` | `_HGetVInfo`, process blessed folder and working directory (`'ERIK'`), `_SetVol` |
| `0x162–0x182` | `A995`, `A9A0`, `4ED0` | `_InitResources`, request **resource type `'boot'`, ID `2`**, then jump through resource handle if present |
| `0x184–0x18E` | `A00E`, `4E75` | Error cleanup via `_UnmountVol`; return `RTS` |

Trap names and low-memory symbols are cross-referenced against [`mac_rom/include/traps.inc`](https://github.com/jonathanschilling/mac_rom/blob/main/include/traps.inc), [`include/lowmem.inc`](https://github.com/jonathanschilling/mac_rom/blob/main/include/lowmem.inc) and [the published `_UnmountVol` trap listing](https://vintageapple.org/macprogramming/pdf/Inside_Macintosh_X-Ref_1988.pdf). The listing is a manual, control-flow-aware decode of the supplied bytes; exact execution branches and resource availability remain **untraced in uMac**.

**Design consequence for Tima/Plus:** Preserve the **ROM's 1024-byte read + `LK` + `0x44` + branch mechanism**. Replace this legacy Macintosh manager/resource logic with a much smaller, self-contained stage one that reads Tima/Plus sectors through the already-installed `_Read`/`.Sony` driver and jumps to RAM-loaded stage two. Unlike the historical shim, our native stage one should have an explicit safe no-return policy once loading succeeds.

### 2.5 User-provided LBA 2: HFS Master Directory Block (21 November 2026)

The owner also supplied the **first 48 bytes of logical block 2** (raw file offset `0x400`) from the same historical `.dsk`. These are **HFS filesystem metadata, not more 68000 boot instructions**:

~~~text
42 44 b3 f8 18 0b e2 27 14 60 01 00 00 07 00 03
00 00 0b 3a 00 00 02 00 00 00 08 00 00 04 00 00
00 22 00 00 0a 53 79 73 74 65 6d 20 37 2e 30 00
~~~

Relative to **LBA 2** (not to the start of the disk), the HFS MDB has these fields, verified against [`mac_rom/include/fsequ.inc`](https://github.com/jonathanschilling/mac_rom/blob/main/include/fsequ.inc) and [Apple's public HFS format definitions](https://github.com/apple-oss-distributions/hfs/blob/main/core/hfs_format.h):

| MDB offset | Field | Decoded value | Meaning |
| --- | --- | --- | --- |
| `0x00` | `drSigWord` | `0x4244` ('BD') | Classic HFS volume |
| `0x0A` | `drAtrb` | `0x0100` | Volume attributes |
| `0x0C` | `drNmFls` | 7 | Files in the root |
| `0x0E` | `drVBMSt` | 3 | Volume bitmap begins at logical block 3 |
| `0x10` | `drAllocPtr` | 0 | Next allocation search hint |
| `0x12` | `drNmAlBlks` | 2874 | Number of allocation blocks |
| `0x14` | `drAlBlkSiz` | 512 bytes | Each allocation block's size |
| `0x18` | `drClpSiz` | 2048 bytes | Allocation clump size |
| `0x1C` | `drAlBlSt` | 4 | First allocation block is logical LBA 4 |
| `0x1E` | `drNxtCNID` | 34 | Next catalog node ID |
| `0x22` | `drFreeBks` | 0 | No free allocation blocks |
| `0x24` | `drVN` | length 10, 'System 7.0' | **HFS volume label**, not the content of the `System` file |

**Important capacity mismatch with our hardware target:** 2874 × 512 = **1,471,488 bytes** allocated, beginning at LBA 4. The allocation area therefore spans LBAs **4–2877** (inclusive). Adding the usual 6 non-allocation blocks yields 2880 × 512 = **1,474,560 bytes**, which is the size of a **1.44 MB high-density floppy**; this is a **strong inference from the MDB, not a measured file length**. Standard Macintosh Plus internal Sony/IWM hardware supports **800K GCR (1600 logical blocks)**, not 1.44MB high-density MFM (which requires SuperDrive/SWIM-class hardware). The current uMac `-d` media handler also accepts **800K logical images only**. Therefore **do not use this particular sample as the source geometry for the Tima/Plus 800K packer**; reuse its boot-block entry contract and HFS insights only. Ask for or measure `stat -c '%s' disk.dsk` to settle the container size.

**What continues disk loading on the historical HFS boot:** After `WaitForBootDisk` has read LBAs 0–1, the `0x4418` boot code (entered at `0x8A`) initialises `_InitEvents`/`_InitFS` and invokes **`_MountVol`** on the selected boot drive. File Manager reads **LBA 2** and other HFS structures (volume bitmap, extents and catalog B-trees) through the already-loaded `.Sony` disk driver. The boot code copies `System` into low-memory `SysResName`, invokes Resource Manager startup, and can look up a `'boot'` resource ID 2. Otherwise it jumps to a ROM-specific continuation; in Plus V3, the success continuation is near `REOPEN_RESOURCES` (`ROMBase+0x0A44`), where further System resources are loaded before the Finder is launched. These file reads **follow the HFS catalog/extents**, not sequential LBAs 3, 4, 5, …, so **LBA 2 is not executable code and the ROM does not automatically treat LBA 3 as 'stage two'.**

This contrasts with **our proposed native Tima/Plus image**: stage one will use `_Read` explicitly to fetch our versioned manifest from LBA 2 and contiguous payload from LBA 3+, and **will never call `_MountVol`** in the no-HFS configuration. Both layouts are valid independently, but **cannot coexist on the same image with the same LBA 2**.

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
| `8A` | remainder | **First-stage code (old style)** | **Actual entry in the supplied `0x4418` boot block.** `4A78 028E` starts here |

In this tested old-style header layout, offsets `0x82–0x85` contain `00 00 80 00` (legacy heap parameter), `0x86–0x89` contain `00 02 00 00` (SysHeapSize), and instruction bytes begin **immediately at `0x8A`**. Newer-style formats may extend the header; such formats need a separate versioned definition rather than overlaying extra fields on these old-style instructions.

For our **first `0x4418` experiment**, use the old-style header through **`+0x89`**, starting code at **`+0x8A`**. The fixed-width `BRA.W` at `+2` must encode `60 00 00 86`: Motorola's branch PC base is `+4`, so the target is `+0x8A`. Both boot blocks together provide 1024 bytes; **886 bytes** remain for code/data from `0x8A` to `0x3FF`. Assert all offsets in the builder, not just the assembled file length. In the real specimen the code/tables end at `0x18F`, and the remaining bytes are padded with `0xDA`.

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
    data = buf; fmt = 'raw 800K (uMac floppy target)'
elif len(buf) == 1474560:
    data = buf; fmt = 'raw 1.44MB high-density (reference inspection ONLY; not Plus IWM)'
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
if data[1024:1026] == b'BD':
    blocks, alloc_bytes, first_lba = struct.unpack_from('>HI', data, 1024 + 0x12)[0], struct.unpack_from('>I', data, 1024 + 0x14)[0], struct.unpack_from('>H', data, 1024 + 0x1c)[0]
    name_len = data[1024 + 0x24]
    name = data[1024 + 0x25:1024 + 0x25 + min(name_len, 27)]
    print('HFS allocation blocks:', blocks, 'size:', alloc_bytes,
          'first LBA:', first_lba, 'volume:', name.decode('mac_roman', errors='replace'))
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

**Expanded owner-supplied inspection 2026-11-21:** Complete 128-byte boot prefix, bytes `0x80–0x18F`, and the first 48 bytes of LBA 2 confirm `LK`, `BRA.W` destination **`0x8A`**, version `0x4418`, named boot fields and System heap values, legacy code and tables, **classic HFS MDB (`BD`) volume named `System 7.0`**. Its 2874×512-byte allocation area strongly suggests **a 1,474,560-byte high-density volume, not our 800K target**. File size/hash, complete blocks and execution traces remain pending. A public candidate raw image exists in [DosFox1/OSHintosh — Mac Classic XO ROM System Disk.dsk](https://github.com/DosFox1/OSHintosh/blob/main/ROM-inator%20data/disk%20images/Mac%20Classic%20XO%20ROM%20System%20Disk.dsk) and is listed as 819,200 bytes, but its boot-block bytes and Plus V3 compatibility **have not been verified**. Prefer the already-known local working Plus disk over that unverified public candidate.

## 8. Implementation sequence and verification gates

| Gate | Deliverable | Acceptance / recorded evidence |
| --- | --- | --- |
| **B0 — inspection (partial)** | Full header `0x00–0x89`, code/tables `0x8A–0x18F`, first 48 bytes of LBA 2; still inspect full media length and verify original boot trace | `LK`, entry `0x8A`, `0x4418`, seven Pascal filenames, HFS 'BD' volume `System 7.0`; **allocation area indicates 1.44MB, not 800K** |
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

**2026-11-21:** Located and reviewed the exact Plus ROM code paths (`WaitForBootDisk` and `BootFromDisk`), ROM `_Read` parameter-block definitions and uMac's logical 800K/DiskCopy media model. Established a two-stage native boot plan with no early HFS requirement, and identified the remaining empirical disk-image, RAM-location and execution tests.

**2026-11-21, subsequent review:** Owner supplied 16 initial bytes plus `0x80–0x18F` from historical untagged media; decoded boot-block dispatch and classic Mac System/resource path. **Corrected first executable address from the earlier erroneous `0x8C` to `0x8A`**, cross-checked with Motorola BRA semantics, actual instruction alignment, a public disassembly exemplar and Plus ROM symbols. The historical code contains no `_Read` and does not implement the proposed Tima/Plus stage-two loader. This remains a design/evidence document, **not a claim that a new Tima/Plus disk has booted**.

**2026-11-21, complete supplied header and LBA2:** Owner provided `0x00–0x7F` and LBA2 first 48 bytes. Recorded all seven boot-block filenames and FCB/event/heap fields; identified `BD` HFS MDB and `System 7.0` **volume name**. Calculated 2874×512-byte allocation blocks, corresponding to 1.44MB high-density geometry, **not** the target 800K. Clarified that the historical System startup mounts HFS and reads files via catalog/extents, whereas the new native bootloader uses explicitly addressed sequential sectors, with its own manifest at LBA2.
