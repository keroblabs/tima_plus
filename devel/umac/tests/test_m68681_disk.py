"""Boot a guest DUART probe from an 800K floppy with the original Plus V3 ROM."""

import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time


FLOPPY_SIZE = 800 * 1024
LOG_LIMIT = 64 * 1024
UART_LINE = b"MAC_PLUS_DUART_PASS\n"


def verify_disk_uart(emulator, rom, assembler):
    work = Path(tempfile.mkdtemp(prefix="m68681-disk-", dir=Path.cwd()))
    source = Path(__file__).with_name("m68681_disk_probe.s").resolve()
    boot = work / "boot-blocks.bin"
    subprocess.run([assembler, "-quiet", "-gas", "-m68000", "-elfregs", "-Fbin", "-o", boot, source],
                   check=True, timeout=5)
    payload = boot.read_bytes()
    if not 0x8a < len(payload) <= 1024 or payload[:8] != bytes.fromhex("4c4b600000864418"):
        raise RuntimeError(f"Unexpected Plus boot blocks in {boot}")

    disk = work / "duart-probe.dsk"
    disk.write_bytes(payload.ljust(FLOPPY_SIZE, b"\0"))
    log = work / "umac.log"
    command = [emulator, "-r", rom, "-d", disk, "-P", "test.pram"]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software")
    saw_output = False
    with log.open("wb") as output, log.open("rb") as capture, subprocess.Popen(
        command, cwd=work, env=env, stdout=output, stderr=subprocess.STDOUT,
    ) as process:
        deadline = time.monotonic() + 15
        try:
            while process.poll() is None and time.monotonic() < deadline:
                capture.seek(0)
                observed = capture.read(LOG_LIMIT)
                if UART_LINE in observed.splitlines(keepends=True):
                    saw_output = True
                    break
                if len(observed) == LOG_LIMIT:
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
        capture.seek(0)
        observed = capture.read(LOG_LIMIT)

    if (not saw_output or process.returncode != 0 or len(observed) == LOG_LIMIT or
            observed.splitlines(keepends=True).count(UART_LINE) != 1 or
            b"\nMacintosh Plus V3 ROM, checksum " not in observed):
        raise RuntimeError(f"Plus ROM disk UART-B probe failed (uMac status {process.returncode}); see {log}")
    print(f"MAC_PLUS_DUART_PASS from floppy guest code under the Plus V3 ROM; see {log}")


if __name__ == "__main__":
    verify_disk_uart(*(Path(argument).resolve() for argument in sys.argv[1:]))
