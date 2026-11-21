"""Pack the Tima Boot ROM, or boot it in uMac and inspect uMac's RAM file."""

import argparse
import os
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import time

ROM_SIZE = 128 * 1024
RAM_SIZE = 4 * 1024 * 1024
MAILBOX = 0x400
MARKER = b"ROM_TEST_PASS\0"


def pack(raw_path, rom_path):
    raw = raw_path.read_bytes()
    if not 0x402 <= len(raw) <= ROM_SIZE:
        raise SystemExit(f"Unexpected unpadded ROM size: {len(raw)}")
    ssp, pc = struct.unpack_from(">II", raw)
    if (ssp, pc) != (0x007F0000, 0x00400400):
        raise SystemExit(f"Unexpected reset vectors: SSP={ssp:#010x}, PC={pc:#010x}")
    rom_path.write_bytes(raw.ljust(ROM_SIZE, b"\xff"))


def read_marker(ram):
    try:
        with ram.open("rb") as f:
            f.seek(MAILBOX)
            return f.read(len(MARKER))
    except FileNotFoundError:
        return b""


def run(emulator, rom):
    rom = rom.resolve()
    emulator = emulator.resolve()
    work = Path(tempfile.mkdtemp(prefix="run-", dir=rom.parent))
    ram = work / "ram.bin"
    log = work / "umac.log"
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software")
    base = [str(emulator), "-P", "test.pram"]

    def check_rejected(args, diagnostic):
        result = subprocess.run(base + args, cwd=work, env=env, capture_output=True,
                                timeout=5, check=False)
        if result.returncode != 1 or diagnostic not in result.stderr or ram.exists():
            raise SystemExit(f"Unexpected ROM validation result ({result.returncode}): "
                             f"{result.stderr.decode(errors='replace')}")

    check_rejected(["-r", str(rom)], b"expected Macintosh Plus V3")
    short = work / "short.rom"
    short.write_bytes(rom.read_bytes()[:8])
    check_rejected(["--raw-rom", "-r", str(short)], b"expected a regular 128 KiB ROM file")
    short.unlink()

    saw_marker = False
    with log.open("wb") as output, subprocess.Popen(
        base + ["--raw-rom", "-r", str(rom)], cwd=work, env=env,
        stdout=output, stderr=subprocess.STDOUT,
    ) as process:
        deadline = time.monotonic() + 10
        try:
            while process.poll() is None and time.monotonic() < deadline:
                observed = read_marker(ram)
                if observed == MARKER:
                    saw_marker = True
                    break
                if observed[:4] in (b"FAIL", b"TRAP"):
                    break
                time.sleep(0.01)
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGINT)
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    if not saw_marker or process.returncode != 0 or read_marker(ram) != MARKER:
        raise SystemExit(f"ROM did not pass (uMac status {process.returncode}, "
                         f"mailbox {read_marker(ram)!r}); see {log}")
    if ram.stat().st_size != RAM_SIZE:
        raise SystemExit(f"Unexpected RAM dump size: {ram.stat().st_size}")
    print(f"ROM_TEST_PASS at file offset {MAILBOX:#010x} in {ram} (during and after uMac)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    pack_cmd = commands.add_parser("pack")
    pack_cmd.add_argument("raw", type=Path)
    pack_cmd.add_argument("rom", type=Path)
    run_cmd = commands.add_parser("run")
    run_cmd.add_argument("emulator", type=Path)
    run_cmd.add_argument("rom", type=Path)
    args = parser.parse_args()
    if args.command == "pack":
        pack(args.raw, args.rom)
    else:
        run(args.emulator, args.rom)
