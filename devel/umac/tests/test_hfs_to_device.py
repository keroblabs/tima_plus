"""Converter tests with generated metadata and no proprietary driver code."""

import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SECTOR = 512
TOOL = Path(__file__).resolve().parents[1] / "tools" / "hfs_to_device.py"


def make_template() -> bytearray:
    image = bytearray(128 * SECTOR)
    struct.pack_into(">2sHI", image, 0, b"ER", SECTOR, 128)
    struct.pack_into(">HIHH", image, 16, 1, 16, 8, 1)
    for index, (kind, start, size) in enumerate((
        (b"Apple_partition_map", 1, 15),
        (b"Apple_Driver43", 16, 16),
        (b"Apple_HFS", 32, 96),
    ), 1):
        offset = index * SECTOR
        struct.pack_into(">2sHIII", image, offset, b"PM", 0, 3, start, size)
        image[offset + 48:offset + 48 + len(kind)] = kind
        struct.pack_into(">II", image, offset + 80, 0, size)
    image[16 * SECTOR:32 * SECTOR] = b"\x7e" * (16 * SECTOR)
    return image


class HfsToDeviceTest(unittest.TestCase):
    def setUp(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        self.volume = root / "volume.hda"
        self.template = root / "template.hda"
        self.output = root / "device.hda"
        source = bytearray(b"\x5a" * (32 * SECTOR))
        struct.pack_into(">2s", source, 0, b"LK")
        struct.pack_into(">2s", source, 2 * SECTOR, b"BD")
        struct.pack_into(">HI", source, 2 * SECTOR + 18, 26, SECTOR)
        struct.pack_into(">H", source, 2 * SECTOR + 28, 4)
        self.volume.write_bytes(source)
        self.template.write_bytes(make_template())

    def run_converter(self, output: Path, volume: Path | None = None) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(TOOL), str(volume or self.volume),
             "--template", str(self.template), "--output", str(output)],
            capture_output=True, text=True, check=False,
        )

    def test_preserves_volume_and_driver_while_updating_geometry(self) -> None:
        original_volume = self.volume.read_bytes()
        original_template = self.template.read_bytes()
        result = self.run_converter(self.output)
        self.assertEqual(result.returncode, 0, result.stderr)
        image = self.output.read_bytes()
        self.assertEqual(len(image), 64 * SECTOR)
        self.assertEqual(struct.unpack_from(">I", image, 4), (64,))
        self.assertEqual(struct.unpack_from(">II", image, 3 * SECTOR + 8), (32, 32))
        self.assertEqual(struct.unpack_from(">II", image, 3 * SECTOR + 80), (0, 32))
        self.assertEqual(image[16 * SECTOR:32 * SECTOR], original_template[16 * SECTOR:32 * SECTOR])
        self.assertEqual(image[32 * SECTOR:], original_volume)
        self.assertEqual(self.volume.read_bytes(), original_volume)
        self.assertEqual(self.template.read_bytes(), original_template)

    def test_cannot_overwrite_input_with_output(self) -> None:
        original_volume = self.volume.read_bytes()
        self.assertNotEqual(self.run_converter(self.volume).returncode, 0)
        self.assertEqual(self.volume.read_bytes(), original_volume)

    def test_rejects_an_already_partitioned_volume(self) -> None:
        self.assertNotEqual(self.run_converter(self.output, volume=self.template).returncode, 0)
        self.assertFalse(self.output.exists())

    def test_rejects_driver_outside_preserved_prefix(self) -> None:
        template = make_template()
        struct.pack_into(">I", template, 18, 32)
        self.template.write_bytes(template)
        result = self.run_converter(self.output)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("outside its driver partition", result.stderr)
        self.assertFalse(self.output.exists())
