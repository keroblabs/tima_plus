# Tima/Plus — Development Roadmap

**Status:** **Phase 1 — functional uMac/M0001AP reference: engineering implementation complete in pre-release, final owner acceptance pending** · 20 November 2026  
**Release candidate:** [`pre-release-umac`](https://github.com/keroblabs/tima_plus/tree/pre-release-umac), branched from reviewed write implementation `385a55d`; **not merged into `main`**  
**Outstanding Phase-1 gate:** Project-owner manual IWM writable-floppy test, acceptance/review, then owner-controlled PR and merge; optional owner GUI format repetition  
**Next active milestone when work resumes:** **PLUS-DEV-0011 — STM32F411 production firmware / Unicorn integration**; project intentionally parked until then  
**Companion documents:** [Introduction](00-introduction.md) · [Unicorn–uMac Integration](02-unicorn-umac-integration.md) · [Tima SDK Integration](03-tima-sdk-integration.md) · [IWM implementation](04-iwm-emulation.md) · [IWM write plan](05-iwm-write-implementation.md)

## Scheduling principles

The roadmap remains **floppy-first for Tima/Plus OS boot**, even though the *uMac emulator* already implements the NCR 5380 and a working virtual SCSI hard disk. Earlier sequencing is superseded by the verified development state below. The end goal remains the same Tima/Plus system running under emulation and on the original M0001AP. Ticket/milestone numbers are **stable identifiers, not execution order**: adding a later milestone does not renumber or reinterpret existing IDs.

**Milestone naming convention:** “Phase 1” is a **retrospective release boundary for the functional uMac reference**, not a new ticket or a claim of analogue/cycle-exact hardware replication. “Phase 2” refers to *future* firmware/Unicorn work. No claim of physical Sony drive, CPLD or BlueSCSI hardware validation follows from emulator tests.

Development takes place along two related tracks:
- **Virtual Macintosh + Tima/Plus:** completed uMac/Musashi reference with original ROM, IWM floppy read/write, RTC/PRAM, virtual SCSI and System Software; next, Unicorn/STM32 production firmware integration, boot-image generation, common Tima SDK and desktop.
- **Physical peripherals:** STM32F411 plus possible ATF1508-assisted floppy emulator, internal mechanism replacement and later external variant; physical BlueSCSI hardware/enclosure and eventual LCD/PSU. This track may proceed independently when resumed; actual STM32/ATF1508 partitioning, level shifting and pin timing remain to be verified.

Do not confuse three media artifacts:
1. **Logical 800K floppy image:** Macintosh-visible sectors, boot blocks and eventual Tima/Plus content.
2. **Raw SPI-NOR device image:** Complete byte-for-byte contents of the physical cartridge/storage chip, including the firmware-defined encoding, track metadata and unused regions.
3. **SCSI hard-drive image:** A virtual SCSI disk **already supported by uMac** and a future physical BlueSCSI-compatible boot medium; Tima/Plus's own SCSI bootstrap and hardware deployment remain future tasks.

## Phase 1 release checkpoint — working uMac / original Macintosh Plus

**Candidate status: engineering complete; release acceptance pending.** The emulator runs the **unmodified Macintosh Plus V3 ROM** and original Sony `.Sony` driver without paravirtual disk-operation interception. The authentic software-visible model currently includes the 68000, ROM overlay and RAM/display path, VIA timing, input/SCC behavior needed by the original OS, RTC and battery-backed PRAM, internal Sony/IWM 800K drive, and **NCR 5380 plus a virtual SCSI hard-disk target**. The real M0001AP board has a SCSI controller even though the project owner's physical computer has **no SCSI drive installed yet**; virtual SCSI functionality is an emulator achievement, not evidence of physical BlueSCSI installation.

| Implemented capability | Evidence and release disposition |
| --- | --- |
| Original-ROM boot and machine services | Original Macintosh System Software boots with display, keyboard/mouse, timers, RTC/PRAM and original-ROM control paths; Phase-1 read-only IWM `PLUS-EMU-0003` is **CLOSED**. |
| Sony 800K IWM reads, sector writes and full format | `PLUS-EMU-0010` is **REVIEW** with reviewed code and developer-reported successful original-ROM write, readback, format retry, GUI TeachText save and Finder erase on raw/tagged media, plus reopen and failure-injection tests. **The owner has not yet manually verified an IWM write.** |
| SCSI controller and emulated hard drive | `PLUS-EMU-0004` and `PLUS-EMU-0005` are **CLOSED**, independently of the unfinished physical BlueSCSI setup and future Tima/Plus SCSI driver. |
| Checkout and pre-release boundary | `pre-release-umac` snapshots the reviewed implementation at `385a55d`; it is parked for owner tests and PR/merge. `main` is not updated merely by declaring this milestone. |

**Phase-1 definition of done:** functionally complete for the *original-ROM/guest-visible emulator scope*; not a claim that every Macintosh serial peripheral, Sony analogue flux edge, or electrical timing is proven. ROM and read-only boot regressions and test evidence are recorded in the relevant tickets. Closing the phase for release additionally requires:

1. The owner tests a **disposable writable floppy** in the pre-release build (guest write, readback, eject/shutdown and fresh-process reopen). Developer GUI format tests already exist; an owner repeat of format is optional.
2. Any discovered regression is fixed and reviewed on the candidate branch under the established ticket methodology, without silently altering the frozen baseline.
3. The owner approves `PLUS-EMU-0010` and performs the **PR/merge into `main`**. Only then may the ticket be marked `CLOSED`; until acceptance it remains `REVIEW`.

**Parking policy (20 November 2026):** no new feature work on the pre-release branch while the project is paused. Preserve the branch and test evidence as a reproducible reference. Resume at the STM32 firmware milestone; do not treat this pause as completion of the physical floppy emulator, new operating system, or BlueSCSI hardware.
## PLUS-DEV programme milestones

### PLUS-DEV-0001 — Establish the uMac reference baseline

**Assessment:** **Achieved in the Phase-1 pre-release candidate.**

**Scope:** Build upstream uMac on the development host with Musashi, an identified supported Macintosh Plus ROM, and a known-good historical Macintosh System disk. Preserve the original uMac behavior as the reference. Record reproducible build steps and ROM-image version/checks without distributing copyrighted ROM or System Software.

**Acceptance:** SDL frontend boots historical Macintosh System Software with display, keyboard, mouse and virtual floppy; test results and known limitations are documented.

**Implementation:** PLUS-EMU-0001, PLUS-EMU-0002.

### PLUS-DEV-0002 — Establish the M0001AP hardware profile

**Assessment:** **Functional original-ROM machine model achieved in pre-release** (RTC/PRAM, necessary VIA/SCC/input/video/RAM/ROM paths), subject to the non-cycle-exact hardware boundary described above.

**Scope:** Evolve uMac into a dedicated Macintosh Plus model: MC68000, accurate 1–4 MB RAM configurations as applicable, original 512 × 342 1bpp screen, ROM overlay/address decoding, VIA, SCC, RTC/keyboard/mouse and IWM **register-level** control. Identify gaps in timers, interrupts, video page selection and resets. Describe SCSI controller presence/addressing, with full disk boot implementation deferred.

**Acceptance:** A documented hardware-profile test suite exercises ROM startup and peripheral accesses. Original System Software remains usable in the patched/virtual-media bring-up configuration. The model does not silently substitute virtual storage for accurate hardware tests.

**Implementation:** PLUS-EMU-0008, supported by PLUS-EMU-0006.

**Boundary:** This milestone establishes the **M0001AP machine model**, not yet an unmodified-ROM boot from a physical-equivalent drive.

### PLUS-DEV-0003 — Run the unmodified Macintosh Plus ROM

**Assessment:** **Achieved.** Phase-1 `PLUS-EMU-0003` is closed and original-ROM disk boot is validated; the original ROM is preserved byte-for-byte.

**Scope:** Remove the ROM patch requirements for the authentic mode and implement IWM control/status interactions required by the ROM and original `.Sony` driver. Retain patched uMac mode solely as a regression baseline.

**Acceptance:** Boot progresses through original ROM initialization with an **unmodified, identified ROM image**; authentic mode never relies on `PV_SONY_ADDR` or a replaced `.Sony` driver. Full media boot is accepted in the following milestone, together with firmware integration.

**Implementation:** PLUS-EMU-0003, PLUS-EMU-0008.

### PLUS-DEV-0004 — Complete the virtual 800K floppy subsystem

**Assessment:** **Implemented and code-reviewed in Phase-1 pre-release; final manual IWM write acceptance pending.** The authentic emulator disk backend is **software** (`iwm.c` + `disk_img.c`), *not* the future STM32 firmware. This distinction corrects the founding roadmap's premature conflation of the two milestones.

**Scope delivered:** Original Macintosh Plus ROM and `.Sony` driver run against real 68000-visible IWM soft-switch, write-handshake and serial-bit timing. A virtual Sony mechanism exposes both sides, 80 cylinders with 12/11/10/9/8 zone sectors, GCR encoding/decoding, track rotation, sense/control, transactional circular-track writes, provisional full-track format retries, dirty-track checkpointing and host image persistence. Read-only is the default; `-w` opts in to floppy writes separately from SCSI `-p`.

**Acceptance evidence:** The closed `PLUS-EMU-0003` and reviewed `PLUS-EMU-0010` tests cover original-ROM boot/read, sector/tag comparisons, write/read/reopen, full 160-side formatting, fault injection, GUI file save and erase for raw 800K and DiskCopy 4.2 variants. Developer-run integration tests are recorded; **owner-run manual write and owner release acceptance are still pending**.

**Implementation:** [PLUS-EMU-0003](tasks/db/PLUS-EMU-0003.md) (**CLOSED**) and [PLUS-EMU-0010](tasks/db/PLUS-EMU-0010.md) (**REVIEW**).

**Boundary:** No STM32 binary, Unicorn peripheral model, ATF1508 RTL, real drive signal timing, physical flash writing, or hardware compatibility is claimed here. The validated emulator is the **reference model for those subsequent implementations**.

### PLUS-DEV-0005 — Establish the floppy-image programmer

**Assessment:** **Not started; follows the production firmware/media-layout contract in PLUS-DEV-0011.** It may be developed alongside STM32 firmware once that contract is stable.

**Scope:** Create a development-host tool that transforms a Macintosh-visible 800K floppy image into the **exact raw SPI-NOR layout** expected by the production STM32 firmware, with format/version validation and read-back verification. It should support producing a file for the virtual flash model first and programming/verifying actual flash media later through a suitable programmer.

**Acceptance:** The tool deterministically emits a flash binary accepted unchanged by Unicorn/STM32 and by the selected physical SPI-flash programming path. Data extracted/read through the firmware matches the logical input image; malformed images and capacity limits are rejected.

**Implementation:** PLUS-SYS-0002. The NOR layout is not frozen until the firmware and media-format design are agreed.

### PLUS-DEV-0006 — Boot Tima/Plus from an 800K floppy

**Assessment:** **Future Tima/Plus operating-system work**; the ability to boot original Macintosh System Software in uMac is not this milestone's completion.

**Scope:** Implement the Macintosh-compatible bootstrap and integrate the **shared Tima 6.x SDK** as one pinned Git submodule. The initial boot may rely on original ROM services behind Tima/Plus-specific abstractions; common kernel/framework code must not be copied into this repo. Bring up a console or minimal monochrome UI, input and basic storage.

**Acceptance:** A developer-built raw SPI-flash image boots the **same Tima/Plus binary** under the virtual Macintosh and, when hardware is ready, the original M0001AP through its replacement drive. First emulator-only acceptance is permitted, explicitly marked as such. Boot stays independent of BlueSCSI.

**Implementation:** PLUS-SYS-0001, PLUS-SYS-0003, PLUS-SYS-0004.

**Parallel physical track:** ATF1508 + STM32 firmware bring-up and the internal drive replacement proceed independently after the emulator/firmware interface is testable.

### PLUS-DEV-0007 — Develop the monochrome Tima/Plus desktop

**Scope:** Build a responsive 512 × 342, 1bpp Tima Desktop target with appropriate memory and UI constraints. Use common Tima services and keep window-management implementation choices open; Retro WM may inform design but is not required. Transition from ROM-backed services to native drivers only where justified.

**Acceptance:** Boot, input, drawing, window/event processing and representative applications operate on the 1bpp display using the common Tima SDK; memory footprint is measured for supported RAM configurations.

**Implementation:** PLUS-SYS-0004, PLUS-SYS-0005, PLUS-SYS-0007.

### PLUS-DEV-0008 — Validate the physical floppy replacement

**Assessment:** **Future physical-hardware track; not completed by the Phase-1 emulator.**

**Scope:** Assemble and validate the ATF1508/STM32F411/SPI-flash replacement for the Mac Plus **internal Sony drive mechanism**. Prove IWM-side timing, signals, GCR behavior and electrical compatibility in physical tests. Expand to the external floppy unit after internal success.

**Acceptance:** Original Macintosh System Software and Tima/Plus both load from and write to real replacement hardware; power cycling retains data. Emulator successes alone do not close this milestone.

**Schedule:** Runs partly **in parallel with PLUS-DEV-0006 and 0007**, not as a prerequisite for developing the OS in the emulator.

### PLUS-DEV-0009 — Migrate to SCSI/BlueSCSI

**Assessment:** **Emulator SCSI portion achieved** (`PLUS-EMU-0004`/`0005` closed); **physical BlueSCSI attachment and Tima/Plus SCSI services remain future work**. The owner has not yet fitted a physical SCSI drive to the M0001AP.

**Scope:** Finish NCR 5380 modelling with a compatible virtual SCSI disk target, prepare full BlueSCSI-compatible hard-drive images, and implement Tima/Plus SCSI bootstrap and storage services. Use a classic-looking externally powered BlueSCSI enclosure on the physical Mac after electrical and termination checks.

**Acceptance:** Tima/Plus boots and uses the **same hard-drive image** in the emulator and on the real M0001AP through BlueSCSI. Floppy boot remains supported. Original Macintosh System Software remains bootable from appropriate media.

**Implementation:** PLUS-EMU-0004, PLUS-EMU-0005, PLUS-SYS-0006.

### PLUS-DEV-0010 — Evaluate display and power modernization (deferred)

**Scope:** Mechanical measurements, FPGA direct digital-video capture, original monochrome output representation, panel selection, PSU architecture and electrical safety.

**Acceptance:** No hardware commitment until the display physically fits, the timing interface and power requirements are validated, and changes preserve the desired hardware compatibility.

### PLUS-DEV-0011 — STM32F411 production firmware and Unicorn integration (next)

**Assessment:** **NEXT when development resumes; not implemented.** Introduced as a separate milestone on 20 November 2026, using a **new** permanent ID to avoid renumbering `PLUS-DEV-0004`–`0010`. This is the logical **Phase-2 firmware milestone** following the completed virtual-IWM reference.

**Scope:** Specify and build the actual STM32F411 production firmware for the Sony MFD-51W-compatible removable 800K drive emulator. The firmware consumes the Mac-side encoded stream through a defined STM32-facing interface, manages GCR/sector and full-track format transactions with RAM staging, and persists verified updates to an SPI-NOR cartridge. Reuse the candidate-track/validate/commit principle proven by the Phase-1 virtual drive, without assuming that firmware can pause external real-time bit capture.

Run the **unmodified production Cortex-M4 binary** under Unicorn with the STM32 peripherals the firmware actually uses (for example SPI, DMA, timers, IRQ/NVIC and GPIO), an appropriate virtual SPI-NOR device, and a host bridge into the **already-working** uMac/IWM model. The bridge may be an intentionally simplified software transport but may **not** bypass the firmware's production write/format/persistence logic. Its framing must match the subsequently agreed hardware-facing contract.

**Acceptance:** Original Plus ROM and guest System Software boot, read/write and format through the executed STM32 firmware binary, with media preserved across virtual power cycle/reopen and verified flash-device image hashes. Record firmware binary identity, ARM/68000 traces, flash erase/program behavior and failures. Do not count ordinary `disk_img.c` write tests as proof of firmware execution. **Physical timing, electrical levels, CPLD implementation, connector and cartridge mechanics are excluded here** and belong to `PLUS-DEV-0008`.

**Implementation:** reserved roadmap item `PLUS-EMU-0007`; formally open its task file in `docs/tasks/db/` **only on resumption**. The programmer `PLUS-SYS-0002` (`PLUS-DEV-0005`) and physical interface design may proceed in parallel.

**Open design boundary:** The use and role of ATF1508, STM32 SPI framing, 3.3/5 V interfacing, DMA/capture architecture, and raw NOR metadata/storage format are **not frozen by this roadmap**. Avoid reviving the old 4×-oversampled SPI assumption as if approved.


## PLUS-EMU implementation tickets

These IDs preserve the suffixes of the earlier KL-EMU catalogue.

| Ticket | Description | Scheduled against |
|---|---|---|
| **PLUS-EMU-0001** | Build and run upstream uMac/Musashi on Fedora; record baseline | DEV-0001 |
| **PLUS-EMU-0002** | Boot original Macintosh System Software through existing uMac paravirtual disk | DEV-0001 |
| **PLUS-EMU-0003** | **CLOSED:** original-ROM IWM read/control with time-driven Sony drive and raw/tagged 800K boot | DEV-0003, DEV-0004 |
| **PLUS-EMU-0004** | **CLOSED:** NCR 5380 SCSI controller and bus-facing operations | DEV-0009 (emulator achieved) |
| **PLUS-EMU-0005** | **CLOSED:** virtual SCSI disk target and original-System SCSI boot | DEV-0009 (emulator achieved) |
| **PLUS-EMU-0006** | Debugger: execution trace, breakpoints, register/memory views, peripheral transactions, deterministic test hooks | Begins DEV-0002, grows throughout |
| **PLUS-EMU-0007** | **NEXT / not opened:** Unicorn STM32F411 production binary, peripherals, agreed IWM↔firmware bridge, raw NOR device and end-to-end verification | DEV-0011 |
| **PLUS-EMU-0008** | Functional M0001AP profile including ROM overlay, 1bpp video and necessary VIA/SCC/RTC/keyboard/mouse; full historical hardware matrix is a separate fidelity goal | DEV-0002 |
| **PLUS-EMU-0010** | **REVIEW / code approved:** transactional IWM writes, ROM formatting and durable raw/DiskCopy image checkpoint; owner manual write acceptance remains | DEV-0004 |

### PLUS-EMU-0007 detailed acceptance conditions — next firmware milestone (PLUS-DEV-0011)

- Load and execute the **production** Cortex-M4 firmware binary, with ELF debug symbols only as supplemental metadata.
- Model the STM32 peripherals required by the binary, including interrupts, timers, SPI and DMA where used.
- Emulate the **agreed production STM32-facing transport** (ATF1508-assisted if that hardware partition is eventually selected); no internal CPLD RTL or electrical model is required.
- Keep original Macintosh ROM and driver code unchanged in authentic mode.
- Read and write a byte-for-byte raw SPI-NOR image using appropriate NOR status/erase/program semantics.
- Capture both endpoint traces and verify reads, writes, formatting/full-track operations, resets and persistent flash contents.
- Retain a separate physical-validation checklist for electrical and timing correctness.

## PLUS-SYS implementation tickets

| Ticket | Description | Scheduled against |
|---|---|---|
| **PLUS-SYS-0001** | Macintosh Plus boot blocks and Tima/Plus stage-1/2 floppy loader; verify original ROM boot contract | DEV-0006 |
| **PLUS-SYS-0002** | Host-side floppy programmer: logical 800K image → exact raw SPI-NOR image, plus verify/extract and future physical-flash write adapter | DEV-0005 |
| **PLUS-SYS-0003** | Macintosh platform API: ROM-backed startup, display, input and storage services behind stable boundaries | DEV-0006 |
| **PLUS-SYS-0004** | Integrate one pinned `keroblabs/tima` SDK, selected Mac Plus BSP, common lifecycle and framework | DEV-0006/0007 |
| **PLUS-SYS-0005** | 512 × 342, 1bpp Tima/Plus desktop and application/event layer | DEV-0007 |
| **PLUS-SYS-0006** | Tima/Plus SCSI storage + boot migration, BlueSCSI disk-image tools | DEV-0009 |
| **PLUS-SYS-0007** | Gradual replacement of ROM-backed services with tested native implementations, without breaking API or floppy boot | DEV-0007 onward |

## Definition of done and evidence

Every ticket should eventually record: scope, dependencies, source revision and build/toolchain identity; a reproducible test; observable acceptance evidence; known hardware-vs-emulator gaps; and explicit approval to close. **Phase 1** records the `pre-release-umac` baseline and original-ROM IWM tests, with owner manual write/merge acceptance outstanding. Developer GUI screenshot/trace artifacts cited under `/tmp/` are not durable repository files; future reviewers should rely on committed test programs and the ticket's recorded hashes or rerun the GUI tests as needed. For **PLUS-EMU-0007 / PLUS-DEV-0011**, record the **STM32 firmware binary hash**, initial and final raw flash-image hashes, and evidence that the firmware (not a bypass) performed the I/O.

The Tima framework repository is authoritative for generic kernel/framework/BSP improvements. This repo is authoritative for the Mac Plus target, programmer and Tima/Plus application integration. The uMac-derived emulator is **present under `devel/umac/` in this repository** and its accepted Phase-1 version is staged on `pre-release-umac`. Its firmware-facing backend will be added later without replacing the original-ROM/IWM regression model.

## Open decisions (do not guess)

- **Release gate (Phase 1):** owner's manual IWM write test on disposable media, decision on additional owner format test, and owner-controlled PR/merge. The ROM/IWM write protocol and virtual-floppy architecture are **implemented**, not undecided.
- **STM32 Phase 2:** firmware peripheral usage, useful pre-existing Cortex-M4/Unicorn models, real-time encoded-data capture and buffer sizing.
- **Physical interface:** whether and how ATF1508 assists STM32F411, GPIO/SPI framing, electrical level shifting and final Sony-drive signal timing. Do not assume the physical architecture is settled by virtual uMac.
- **Storage:** final raw NOR image layout, track metadata, GCR sector representation, tags and atomic flash write/recovery protocol; host removable-flash programmer interface.
- **Physical M0001AP:** fitted ROM/RAM configuration and measured electrical behavior for hardware validation (the emulator itself has an identified Plus V3 ROM test fixture).
- **Tima/Plus OS:** minimum RAM, 6.x SDK bring-up, ROM/native driver transition, boot-block and desktop decisions.
- **BlueSCSI:** physical drive/enclosure/cabling/termination and Tima/Plus SCSI bootstrap; the uMac NCR 5380 and virtual SCSI disk are already working.
