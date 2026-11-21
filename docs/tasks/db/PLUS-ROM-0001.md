# PLUS-ROM-0001 — Boot a standalone test ROM and record its RAM marker

**ID:** `PLUS-ROM-0001`

**Status:** `REVIEW`

**Title:** Boot a standalone test ROM and record its RAM marker

**Description**

Create the Tima Boot test project under `devel/tima_boot`. Build an original
68000 reset ROM with the vbcc toolchain, boot it on the uMac M0001AP hardware
model, and confirm that the running guest writes `ROM_TEST_PASS` to a known
offset in the emulator's `ram.bin`. No Apple ROM or System Software is needed.

**Details**

- Kind: `TASK`
- Category: `PLUS-ROM`
- Milestone: independent ROM smoke test on the
  [PLUS-DEV-0002 hardware profile](../../01-development-roadmap.md#plus-dev-0002--establish-the-m0001ap-hardware-profile);
  does not change the original-ROM or floppy roadmap.
- Working branch: `pre-release-rom-test`; the parked `pre-release-umac`
  reference is not a target of this work.
- The SDL frontend currently requires the Macintosh Plus V3 checksum. Its
  `O_TRUNC`, `ftruncate`, and `MAP_SHARED` setup suggests guest RAM writes
  should appear in `ram.bin` in the current directory. That behavior must be
  verified with this ROM, including after the emulator exits.
- Test profile: 4 MiB guest RAM. Reserve a 16-byte mailbox at low RAM
  address `0x00000400` (offset `1024` in the dump). Read the 13 bytes
  `ROM_TEST_PASS` followed by NUL there, not just anywhere in the file.
- Compiler and linker implementation are tracked in the vbcc_pack project's
  `vport/docs`, not in this ticket.

**Notes**

- **2026-11-21 — Discovery:** Owner requested a standalone ROM test and a
  check of the actual emulator RAM file. Examined reset and file mapping in
  [uMac](../../../devel/umac/src/unix_main.c); no project or dump test exists yet.
- **2026-11-21 — Decision:** Implement on `pre-release-rom-test` with an
  explicit `--raw-rom` option, rather than replacing the default ROM check
  or writing a RAM dump from a separate runner.
- **2026-11-21 — Implementation:** Added [Tima Boot](../../../devel/tima_boot/):
  reset vectors, overlay handoff and C startup, a 128 KiB ROM build, and an
  SDL dummy-driver test that reads the emulator's own `ram.bin`. The original
  boot path retains its checksum check; both modes retain the size check.
- **2026-11-21 — Test:** `make -C devel/tima_boot test` passed. The default
  mode rejected this ROM and raw mode rejected a short file before creating
  RAM. In raw mode, bytes `52 4f 4d 5f 54 45 53 54 5f 50 41 53 53 00`
  were present at file offset `0x400` of the 4,194,304-byte `ram.bin` both
  while the guest ran and after uMac exited with status zero. The ROM
  SHA-256 was `3e853e1b4fd332b55c979610a47b3ef6b645afeb4a18c81bb3e3ea96eded622f`.
  Builds, images and RAM dumps stay out of Git.
- **2026-11-21 — Regression check:** Eleven of twelve native uMac tests
  passed. `iwm_checkpoint` failed at `tests/test_iwm.c:981`; the same
  failure reproduced in a fresh build from unmodified `85f8fc3`. No IWM
  code was changed; this is outside the ROM marker acceptance check.
- **2026-11-21 — Original ROM regression:** `scsi_plus_rom` and
  `iwm_desktop_boot` passed using the local unmodified Plus V3 ROM and
  `plus-system.image`, without `--raw-rom`. Neither image is part of the
  test project or the commit.
- **2026-11-21 — Status Change:** Ready for owner review based on the
  emulator acceptance test. No physical ROM or board was tested.
- **2026-11-21 — Local commit:** Recorded implementation and verification
  in `21eef1d` on `pre-release-rom-test`. Owner review and approval are
  pending.

**Proposed Change**

Use a scoped, explicit raw ROM opt-in in the SDL frontend so the existing
original-ROM checks remain in force by default. Link the test against the
M0001AP ROM/RAM map, switch off the reset overlay, and write the mailbox from
guest C only after initialized and zero-filled storage have been verified.
Boot with no attached media in an isolated working directory and compare
the exact bytes at offset `1024` of a fresh 4 MiB `ram.bin`.

**Commits**

| Branch | Commit (short hash) | Review Status | Description |
| --- | --- | --- | --- |
| `pre-release-rom-test` | `21eef1d` | `UNREVIEWED` | Build and boot Tima Boot, add the uMac raw-ROM opt-in, and verify the RAM marker. |

**Resolution Description**

Pending.
