# Tima Boot

Tima Boot is a bare-metal 68000 test ROM for uMac's M0001AP hardware model.
It contains no Apple code and boots without System Software or disk media.
The Linux-hosted vbcc, vasm and vlink tools build the reset code and
freestanding C program.

## Build and run

Install or build the vport host tools, then run from the `tima_plus` root.

```sh
make -C devel/tima_rom test
```

The build defaults to `/home/marcio/Projects/languages/vbcc_pack/vport/build/host/bin`.
Set `TOOLCHAIN=/absolute/path/to/bin` if the vport binaries are elsewhere.
The test also needs GNU Make, Python 3, a host C compiler, CMake and the
SDL2 development package. Plain `make -C devel/tima_rom` builds the 128 KiB
raw reset image `tima-boot.rom` and the ELF inspection copy.

The test boots the SDL emulator with 4 MiB of RAM, the dummy video driver
and no media. It verifies the default Plus V3 checksum check and the raw
ROM size check. It then checks the following exact UART-B output during
emulation and after clean shutdown, and prints the `build/run-*/umac.log`
path.

```text
Tima Boot: cookie=0x13579bdf bss=0
ROM_TEST_PASS
```

For an interactive test, use the built uMac with
`--raw-rom -r /absolute/path/to/tima-boot.rom -P test.pram` from a fresh run
directory. uMac creates/truncates `ram.bin` in its working directory,
and prints UART-B to stdout. A black display is expected because the ROM
stops the CPU after reporting the result. `ROM_TEST_FAIL` indicates an
unexpected startup value. `ROM_TEST_TRAP` indicates a guest exception.

The ROM configures the MC68681 UART-B transmitter and polls its status
before each byte. The project-local `printf` supports `%%`, `%c`, `%s`,
`%d`, `%i`, `%u`, `%x`, `%X` and `%p`, with `l` for integers. It returns
the number of bytes written, or `-1` at an unsupported conversion. The
ROM links this console directly with `-nostdlib`.

The ROM links at `0x00400000` against
[uMac's map](../umac/include/machw.h), copies `.data` from ROM to low RAM
and clears `.bss` at startup. The first 4 KiB of RAM are reserved for the
vector table and low memory. The top 64 KiB are reserved for display memory.
See [PLUS-ROM-0002](../../docs/tasks/db/PLUS-ROM-0002.md) for the acceptance
record.
