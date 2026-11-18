#!/usr/bin/env python3
"""Wrap a raw HFS volume in a 512-byte-sector Macintosh SCSI device image."""

import argparse
import os
import struct
from pathlib import Path
from typing import BinaryIO


SECTOR_SIZE = 512
MAX_DEVICE_BLOCKS = 1 << 21  # uMAC's current image limit.


def image_blocks(image: BinaryIO) -> int:
    size = os.fstat(image.fileno()).st_size
    if not size or size % SECTOR_SIZE:
        raise ValueError(f"{image.name}: expected a nonempty whole-sector image")
    return size // SECTOR_SIZE


def read_sector(image: BinaryIO, block: int) -> bytes:
    image.seek(block * SECTOR_SIZE)
    data = image.read(SECTOR_SIZE)
    if len(data) != SECTOR_SIZE:
        raise ValueError(f"{image.name}: truncated sector {block}")
    return data


def volume_blocks(volume: BinaryIO) -> int:
    blocks = image_blocks(volume)
    if blocks < 5 or read_sector(volume, 0).startswith(b"ER"):
        raise ValueError("input must be a raw HFS volume, not a whole-device image")
    mdb = read_sector(volume, 2)
    if not mdb.startswith(b"BD"):
        raise ValueError("input must have a classic HFS master directory block at sector 2")
    allocation_blocks, allocation_size = struct.unpack_from(">HI", mdb, 18)
    (allocation_start,) = struct.unpack_from(">H", mdb, 28)
    if (not allocation_blocks or not allocation_size or allocation_size % SECTOR_SIZE
            or allocation_start < 4
            or allocation_start + allocation_blocks * (allocation_size // SECTOR_SIZE) > blocks - 2):
        raise ValueError("HFS allocation blocks do not fit the input image")
    return blocks


def template_records(template: BinaryIO) -> tuple[bytearray, int, bytearray]:
    # Driver descriptor and APM layout: Inside Macintosh: Devices, SCSI Manager.
    blocks = image_blocks(template)
    descriptor = bytearray(read_sector(template, 0))
    signature, sector_size, disk_blocks = struct.unpack_from(">2sHI", descriptor)
    if signature != b"ER" or sector_size != SECTOR_SIZE or not 1 < disk_blocks <= blocks:
        raise ValueError("template must have a valid driver descriptor for 512-byte sectors")

    first = read_sector(template, 1)
    entry_count, map_start, map_blocks = struct.unpack_from(">III", first, 4)
    if (first[:2] != b"PM" or first[48:80].rstrip(b"\0") != b"Apple_partition_map"
            or map_start != 1 or not 3 <= entry_count <= map_blocks < disk_blocks):
        raise ValueError("template has an invalid Apple Partition Map")

    ranges: list[tuple[int, int, bytes]] = []
    hfs = None
    for index in range(1, entry_count + 1):
        entry = read_sector(template, index)
        count, start, size = struct.unpack_from(">III", entry, 4)
        kind = entry[48:80].rstrip(b"\0")
        if entry[:2] != b"PM" or count != entry_count or not 1 <= start < start + size <= disk_blocks:
            raise ValueError(f"template partition entry {index} is invalid")
        if kind == b"Apple_HFS" and hfs is None:
            hfs = (index, entry)
        elif index != 1 and not kind.startswith(b"Apple_Driver"):
            raise ValueError("template must contain only a partition map, drivers and one HFS volume")
        ranges.append((start, start + size, kind))

    ranges.sort()
    if any(left[1] > right[0] for left, right in zip(ranges, ranges[1:])):
        raise ValueError("template partitions overlap")
    if hfs is None or ranges[-1][2] != b"Apple_HFS":
        raise ValueError("template must have one final Apple_HFS partition")
    hfs_index, hfs_entry = hfs
    (hfs_size,) = struct.unpack_from(">I", hfs_entry, 12)
    if struct.unpack_from(">II", hfs_entry, 80) != (0, hfs_size) or any(hfs_entry[92:100]):
        raise ValueError("template HFS partition must contain only a volume starting at its first sector")

    (driver_count,) = struct.unpack_from(">H", descriptor, 16)
    if not 1 <= driver_count <= (SECTOR_SIZE - 18) // 8:
        raise ValueError("template has an invalid driver descriptor count")
    has_mac_driver = False
    for index in range(driver_count):
        start, size, system = struct.unpack_from(">IHH", descriptor, 18 + index * 8)
        if not size or not any(kind.startswith(b"Apple_Driver") and lo <= start < start + size <= hi
                               for lo, hi, kind in ranges):
            raise ValueError("a template driver points outside its driver partition")
        has_mac_driver |= system == 1
    if not has_mac_driver:
        raise ValueError("template lacks a Macintosh driver descriptor (type 1)")
    return descriptor, hfs_index, bytearray(hfs_entry)


def copy_exact(source: BinaryIO, destination: BinaryIO, length: int) -> None:
    source.seek(0)
    while length:
        size = min(length, 1024 * 1024)
        data = source.read(size)
        if len(data) != size:
            raise ValueError(f"{source.name}: image was truncated during conversion")
        destination.write(data)
        length -= size


def convert(volume_path: Path, template_path: Path, output_path: Path) -> tuple[int, int]:
    for path in (volume_path, template_path):
        if not path.is_file():
            raise ValueError(f"{path}: expected a regular image file")

    with volume_path.open("rb") as volume, template_path.open("rb") as template:
        blocks = volume_blocks(volume)
        descriptor, entry_index, entry = template_records(template)
        start, capacity = struct.unpack_from(">II", entry, 8)
        if blocks > capacity:
            raise ValueError("HFS volume does not fit the template; use a larger template")
        device_blocks = start + blocks
        if device_blocks > MAX_DEVICE_BLOCKS:
            raise ValueError("result would exceed uMAC's 1 GiB image limit")

        struct.pack_into(">I", descriptor, 4, device_blocks)
        struct.pack_into(">I", entry, 12, blocks)
        struct.pack_into(">I", entry, 84, blocks)

        output = output_path.open("xb")
        try:
            with output:
                copy_exact(template, output, start * SECTOR_SIZE)
                copy_exact(volume, output, blocks * SECTOR_SIZE)
                output.seek(0)
                output.write(descriptor)
                output.seek(entry_index * SECTOR_SIZE)
                output.write(entry)
        except (OSError, ValueError, KeyboardInterrupt):
            output_path.unlink(missing_ok=True)
            raise
    return start, device_blocks


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("volume", type=Path, help="raw classic HFS volume (no partition map)")
    parser.add_argument("--template", required=True, type=Path,
                        help="whole-device image with a known-good Macintosh Plus driver")
    parser.add_argument("--output", required=True, type=Path, help="new device image; must not exist")
    args = parser.parse_args()
    try:
        start, blocks = convert(args.volume, args.template, args.output)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"{parser.prog}: {exc}\n")
    except KeyboardInterrupt:
        parser.exit(130, "\nConversion interrupted\n")
    print(f"Wrote {args.output}: {blocks} sectors, HFS volume starts at LBA {start}")


if __name__ == "__main__":
    main()
