"""Boot the unmodified ROM with the SDL frontend and check wall-clock timing."""

import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time


CUR_AP_NAME = 0x910
MAC_TIME = 0x20C
MAC_TICKS = 0x16A
# The paced 4 MiB ROM's RAM test alone took over 30 seconds.
BOOT_TIMEOUT = 15


def read_ram(path, offset, count):
    try:
        with path.open("rb") as ram:
            ram.seek(offset)
            return ram.read(count)
    except FileNotFoundError:
        return b""


def verify_desktop(emulator, rom, floppy):
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software")
    with tempfile.TemporaryDirectory(prefix="umac-iwm-desktop-") as directory:
        ram = Path(directory) / "ram.bin"
        command = [emulator, "-r", rom, "-d", floppy, "-P", "test.pram"]
        start = time.monotonic()
        with subprocess.Popen(
            command, cwd=directory, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        ) as process:
            try:
                while process.poll() is None:
                    if read_ram(ram, CUR_AP_NAME, 7) == b"\x06Finder":
                        break
                    if time.monotonic() - start >= BOOT_TIMEOUT:
                        raise RuntimeError(f"Finder failed to start within {BOOT_TIMEOUT} seconds")
                    time.sleep(0.01)
                else:
                    raise RuntimeError("Emulator exited before Finder started")
                print(f"Finder started in {time.monotonic() - start:.3f} seconds")

                before = int.from_bytes(read_ram(ram, MAC_TIME, 4), "big")
                ticks_before = int.from_bytes(read_ram(ram, MAC_TICKS, 4), "big")
                started = time.monotonic()
                time.sleep(2.1)
                elapsed = time.monotonic() - started
                after = int.from_bytes(read_ram(ram, MAC_TIME, 4), "big")
                ticks_after = int.from_bytes(read_ram(ram, MAC_TICKS, 4), "big")
                advance = (after - before) % (1 << 32)
                if process.poll() is not None or abs(advance - elapsed) >= 1.5:
                    raise RuntimeError(f"Guest clock advanced {advance} s in {elapsed:.2f} wall seconds")
                print(f"Guest clock advanced {advance} s in {elapsed:.2f} wall seconds")
                tick_rate = ((ticks_after - ticks_before) % (1 << 32)) / elapsed
                if abs(tick_rate - 60) > 8:
                    raise RuntimeError(f"Guest TickCount advanced at {tick_rate:.1f} Hz")
                print(f"Guest TickCount advanced at {tick_rate:.1f} Hz")
            finally:
                if process.poll() is None:
                    process.send_signal(signal.SIGINT)
                try:
                    output, _ = process.communicate(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    output, _ = process.communicate()
                print(output.decode(errors="replace"), end="")
            if process.returncode:
                raise RuntimeError(f"Emulator exited with status {process.returncode}")


if __name__ == "__main__":
    verify_desktop(*(str(Path(arg).resolve()) for arg in sys.argv[1:]))
