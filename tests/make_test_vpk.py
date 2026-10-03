#!/usr/bin/env python3
"""Build small VPK v1 and v2 test archives, plus the expected file contents.

Covers every storage case the reader has to handle: preload-only, data
embedded in the _dir file, data in a numbered archive, a file split between
preload and archive, a root-level file, and a file with no extension.
"""
import os
import random
import struct
import sys
import zlib
from collections import defaultdict

DIR_ARCHIVE = 0x7FFF


def test_files():
    rnd = random.Random(2004)
    blob = lambda n: bytes(rnd.getrandbits(8) for _ in range(n))
    return [
        ("materials/concrete/floor01.vmt",
         b'"LightmappedGeneric"\n{\n\t"$basetexture" "concrete/floor01"\n}\n', "preload"),
        ("scripts/readme", b"no extension, stored inside the _dir file\n" * 4, "dir"),
        ("credits.txt", b"root-level file\n", "archive"),
        ("maps/test.bsp", blob(50000), "split"),
        ("sound/ambient/hum.wav", blob(12345), "archive"),
    ]


def split_path(path):
    directory, _, filename = path.rpartition("/")
    name, dot, ext = filename.rpartition(".")
    if not dot:
        name, ext = filename, ""
    return directory or " ", name, ext or " "


def build(out_dir, stem, version):
    tree = defaultdict(lambda: defaultdict(list))
    dir_data, arc_data = bytearray(), bytearray()

    for path, data, mode in test_files():
        if mode == "preload":
            pre, idx, off, body = data, DIR_ARCHIVE, 0, b""
        elif mode == "dir":
            pre, idx, off, body = b"", DIR_ARCHIVE, len(dir_data), data
            dir_data += body
        elif mode == "archive":
            pre, idx, off, body = b"", 0, len(arc_data), data
            arc_data += body
        else:  # split
            pre, idx, off, body = data[:64], 0, len(arc_data), data[64:]
            arc_data += body
        d, n, e = split_path(path)
        entry = struct.pack("<IHHIIH", zlib.crc32(data), len(pre), idx, off, len(body), 0xFFFF)
        tree[e][d].append(n.encode() + b"\0" + entry + pre)

    t = bytearray()
    for ext, dirs in tree.items():
        t += ext.encode() + b"\0"
        for d, items in dirs.items():
            t += d.encode() + b"\0"
            for item in items:
                t += item
            t += b"\0"
        t += b"\0"
    t += b"\0"

    if version == 1:
        header = struct.pack("<III", 0x55AA1234, 1, len(t))
    else:
        header = struct.pack("<IIIIIII", 0x55AA1234, 2, len(t), len(dir_data), 0, 0, 0)

    with open(os.path.join(out_dir, f"{stem}_dir.vpk"), "wb") as f:
        f.write(header + t + dir_data)
    with open(os.path.join(out_dir, f"{stem}_000.vpk"), "wb") as f:
        f.write(arc_data)


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "testdata"
    os.makedirs(out_dir, exist_ok=True)
    build(out_dir, "test_v1", 1)
    build(out_dir, "test_v2", 2)
    for path, data, _ in test_files():
        dest = os.path.join(out_dir, "expected", path)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as f:
            f.write(data)
    print(f"test archives written to {out_dir}")


if __name__ == "__main__":
    main()
