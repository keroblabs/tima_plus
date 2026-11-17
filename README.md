# Tima/Plus

**A new operating system for the original Apple Macintosh Plus (M0001AP), powered by the shared Tima SDK.**

This is the Macintosh Plus **application and integration repository**, not a fork of the Tima kernel. It begins as a documentation-first project: original MC68000 hardware, original ROM, 512 × 342 1bpp screen and floppy-first boot. The final binary must run on the original computer, with a dedicated uMac/Musashi development emulator as the primary debugger.

## The plan

1. Establish a working uMac reference, then a faithful M0001AP hardware profile.
2. Boot using the **unmodified Macintosh Plus ROM** and original `.Sony` driver.
3. Connect the emulated Macintosh IWM to **actual STM32F411 firmware running in Unicorn**, through a deliberately simplified software IWM-to-SPI bridge.
4. Build a host-side **floppy-image programmer** that produces the full raw SPI-NOR image from an 800K Macintosh logical floppy image.
5. Boot **Tima/Plus from the emulated floppy**, then test using the real ATF1508 + STM32 floppy replacement (physical firmware/hardware work in parallel).
6. Develop the lightweight 1bpp desktop using the **one authoritative Tima 6.x SDK** as a pinned Git submodule.
7. **Later**, implement/finish SCSI emulation and BlueSCSI-compatible hard-disk boot; preserve floppy boot.
8. Consider LCD/PSU modernization only after hardware measurements and safety review.

Nothing here implies the emulator, firmware or OS is already implemented. Documents describe the agreed design and planned acceptance gates.

## Project documents

- [Introduction and project charter](docs/00-introduction.md)
- [Development roadmap: PLUS-DEV / PLUS-EMU / PLUS-SYS](docs/01-development-roadmap.md)
- [Unicorn–uMac–STM32 floppy integration design](docs/02-unicorn-umac-integration.md)
- [Shared Tima SDK and board-support integration](docs/03-tima-sdk-integration.md)

## Ticket naming

| Prefix | Purpose |
|---|---|
| `PLUS-DEV-xxxx` | End-to-end programme milestones and sequencing |
| `PLUS-EMU-xxxx` | Macintosh hardware emulation, Unicorn, SCSI and debug tooling |
| `PLUS-SYS-xxxx` | Bootloader, Tima/Plus OS, floppy programmer and desktop |

Earlier `KL-EMU-0001`–`0007` ideas retain numeric suffixes as `PLUS-EMU-0001`–`0007`; see the roadmap for the revised **floppy-first** execution order.

## Related work

- [Tima SDK](https://github.com/keroblabs/tima) — authoritative shared kernel, framework, interfaces and BSPs; **the only Tima source submodule** that the application will need.
- [uMac](https://github.com/evansm7/umac) — minimalist Musashi-based classic Macintosh emulator, initial hardware reference.
- [Macintosh Plus ROM disassembly](https://github.com/jonathanschilling/mac_rom/tree/main/plus) — source-level reference for original ROM interfaces.

The ROM, original System Software and third-party emulator components have their own ownership and license terms. Do not commit proprietary ROM/System Software images or copy GPL-licensed emulator pieces into the Tima runtime without appropriate review.

**Initial repository status:** Documentation charter only; source layout and Tima submodule will be introduced in subsequent implementation tickets.
