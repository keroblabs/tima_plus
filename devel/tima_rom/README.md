# Tima ROM — standalone reset-ROM experiment

**Tima ROM** is a bare-metal MC68000 reset-ROM experiment for uMac's Macintosh
Plus (M0001AP) hardware model. It is **not** the Tima/Plus disk bootloader:
the future `devel/tima_boot` project will use the **unmodified Apple Macintosh
Plus ROM** to start code from an 800K floppy image. This experiment instead
uses uMac's `--raw-rom` option to replace the reset ROM temporarily.

The test image contains no Apple code and boots without System Software or disk
media. Linux-hosted vbcc, vasm and vlink build its reset code and freestanding
C application. The 128 KiB artifact is still named `tima-boot.rom` in the
current Makefile, and the test prints `Tima Boot:` for historical compatibility.
**Those names identify the current test output, not the future disk-loader project.**

## Build and run

Install or build the vport host tools, then run from the `tima_plus` root.

```sh
make -C devel/tima_rom test
```

The build defaults to
`/home/marcio/Projects/languages/vbcc_pack/vport/build/host/bin`.
Set `TOOLCHAIN=/absolute/path/to/bin` if the vport binaries are elsewhere.
The test also needs GNU Make, Python 3, a host C compiler, CMake, and the
SDL2 development package. Plain `make -C devel/tima_rom` builds the 128 KiB
raw reset image `tima-boot.rom` and an ELF inspection copy.

The test configures a development uMac build and boots it with 4 MiB of
RAM, the SDL dummy video driver and no media. It verifies the Plus V3
checksum and the raw ROM size check. It then verifies these exact UART-B
lines during emulation and after clean shutdown, and reports the
`build/run-*/umac.log` path.

```text
Tima Boot: cookie=0x13579bdf bss=0
ROM_TEST_PASS
```

For an interactive test, run the development uMac with
`--raw-rom -r /absolute/path/to/tima-boot.rom -P test.pram` from a fresh
working directory. uMac creates/truncates `ram.bin` in that directory and
sends UART-B bytes to stdout. A black screen is expected because the ROM
stops the CPU after reporting its result.
`ROM_TEST_FAIL` means the startup-value check failed; `ROM_TEST_TRAP`
reports a guest exception.

The ROM configures the MC68681 UART-B transmitter and polls its status
before each byte. The project-local `printf` supports `%%`, `%c`, `%s`,
`%d`, `%i`, `%u`, `%x`, `%X` and `%p`, with `l` for integer
conversions. It returns the number of bytes written, or `-1` on an
unsupported conversion. The ROM links this console directly with `-nostdlib`.

The ROM links at `0x00400000` against
[uMac's memory map](../umac/include/machw.h), copies `.data` from ROM
to low RAM and clears `.bss` at startup. The first 4 KiB of RAM are
reserved for the vector table and low-memory globals. The top 64 KiB
are reserved for the display.

## Development-only MC68681 hardware

This standalone ROM and its console code are development fixtures. They
use the MC68681 at `0xC00000–0xC0001F`, with UART-B status and transmit
data at `0xC00013` and `0xC00017`. A development uMac build exposes the
same registers to code booted by the original Plus V3 ROM from an 800K floppy.

For the original M0001AP map, build uMac with CMake option
`-DUMAC_DEVELOPMENT_DUART=OFF` or Make option `UMAC_DEVELOPMENT_DUART=0`.
Physical firmware uses that map. Keep future guest use of this console
development-only or provide a fallback.

See [PLUS-ROM-0002](../../docs/tasks/db/PLUS-ROM-0002.md) for the
verification evidence and review status.
