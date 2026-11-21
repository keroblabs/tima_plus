# Tima Boot

Tima Boot is a bare-metal 68000 test ROM for uMac's M0001AP hardware model.
It contains no Apple code and boots without System Software or disk media.
The vbcc, vasm and vlink executables run on Linux. The guest runs only the
reset code and the resulting C program.

## Build and run

With `vbcc_pack` and `tima_plus` next to each other, build the
[vport host tools](../../../vbcc_pack/vport/docs/08-state-1-implementation.md),
then run from the `tima_plus` root:

```sh
make -C devel/tima_boot test
```

This needs GNU Make, Python 3, a host C compiler, CMake, and the SDL2
development package. Set `TOOLCHAIN=/absolute/path/to/bin` if the vport
binaries are elsewhere. Plain `make -C devel/tima_boot` builds only the
ROM and the ELF inspection copy. `tima-boot.rom` is a 128 KiB raw reset
image, not ELF or a `.v68b` application.

The test boots the SDL emulator with the dummy video driver and no media.
It checks that the default ROM mode rejects this image, raw mode still
rejects a short file, and the correctly sized raw ROM writes the complete
14-byte NUL-terminated marker at guest address `0x00000400`. The emulator
is built with 4 MiB of RAM. The test prints the unique `build/run-*/ram.bin`
path and checks the same bytes both while the guest runs and after uMac exits.
Inspect the recorded result with:

```sh
od -An -c -j 1024 -N 14 /path/from/test/output/ram.bin
```

For an interactive test, use the built uMac with
`--raw-rom -r /absolute/path/to/tima-boot.rom -P test.pram` from a fresh run
directory. uMac creates/truncates `ram.bin` in its working directory,
so don't reuse the directory of a normal Macintosh session. A black
display is expected: the ROM masks interrupts, disables the reset
overlay via the VIA, installs vectors, initializes RAM statics, writes
the marker and stops the CPU. Close the window when done.
`FAIL` or `TRAP` at the mailbox indicates a guest test failure.

The ROM links at `0x00400000` against
[uMac's map](../umac/include/machw.h), copies `.data` from ROM to low RAM
and clears `.bss` without storing it in the image. The first 4 KiB of RAM
are reserved for vectors and the mailbox; the top 64 KiB are reserved
for display memory. See [PLUS-ROM-0001](../../docs/tasks/db/PLUS-ROM-0001.md)
for the acceptance record. This is not a physical ROM installation procedure.
