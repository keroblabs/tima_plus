"""Pack the Tima Boot ROM, or boot it in uMac and verify its UART-B output."""

import argparse
import os
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import time

ROM_SIZE = 128 * 1024
MAX_LOG_SIZE = 64 * 1024
BOOT_OUTPUT = b"Tima Boot: cookie=0x13579bdf bss=0\nROM_TEST_PASS\n"
FAILURE_LINES = (b"ROM_TEST_FAIL\n", b"ROM_TEST_TRAP\n")


def pack(raw_path, rom_path):
    raw = raw_path.read_bytes()
    if not 0x402 <= len(raw) <= ROM_SIZE:
        raise SystemExit(f"Unexpected unpadded ROM size: {len(raw)}")
    ssp, pc = struct.unpack_from(">II", raw)
    if (ssp, pc) != (0x007F0000, 0x00400400):
        raise SystemExit(f"Unexpected reset vectors: SSP={ssp:#010x}, PC={pc:#010x}")
    rom_path.write_bytes(raw.ljust(ROM_SIZE, b"\xff"))


def read_console(log):
    with log.open("rb") as capture:
        return capture.read(MAX_LOG_SIZE)


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
    print("Plus V3 checksum and raw ROM size checks rejected invalid images")

    expected = b"\n" + BOOT_OUTPUT
    saw_output = False
    with log.open("wb") as output, subprocess.Popen(
        base + ["--raw-rom", "-r", str(rom)], cwd=work, env=env,
        stdout=output, stderr=subprocess.STDOUT,
    ) as process:
        deadline = time.monotonic() + 10
        try:
            while process.poll() is None and time.monotonic() < deadline:
                observed = read_console(log)
                if expected in b"\n" + observed:
                    saw_output = True
                    break
                if len(observed) == MAX_LOG_SIZE or any(line in observed for line in FAILURE_LINES):
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
    observed = read_console(log)
    if (not saw_output or process.returncode != 0 or len(observed) == MAX_LOG_SIZE or
            (b"\n" + observed).count(expected) != 1 or any(line in observed for line in FAILURE_LINES)):
        raise SystemExit(f"UART-B test failed (uMac status {process.returncode}); see {log}")
    print(f"UART-B output verified during emulation and after clean exit in {log}")
    print(BOOT_OUTPUT.decode("ascii"), end="")


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
