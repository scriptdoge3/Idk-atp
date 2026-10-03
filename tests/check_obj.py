#!/usr/bin/env python3
"""Check the box-room OBJ: 6 world quads, every one facing inward (toward the
origin), and the two displacement patches: 2 x 32 triangles, all facing up.
With --lightmap, also check the lightmap atlas the MTL points at."""
import os
import re
import struct
import sys

verts, objects, current = [], {}, None
for line in open(sys.argv[1]):
    parts = line.split()
    if parts and parts[0] == "o":
        current = objects.setdefault(parts[1], [])
    elif parts and parts[0] == "v":
        verts.append(tuple(map(float, parts[1:4])))
    elif parts and parts[0] == "f":
        current.append([verts[int(p.split("/")[0]) - 1] for p in parts[1:]])


def newell(poly):  # CCW-positive normal
    n = [0.0, 0.0, 0.0]
    for a, b in zip(poly, poly[1:] + poly[:1]):
        n[0] += (a[1] - b[1]) * (a[2] + b[2])
        n[1] += (a[2] - b[2]) * (a[0] + b[0])
        n[2] += (a[0] - b[0]) * (a[1] + b[1])
    return n


world = objects.get("world", [])
assert len(world) == 6, f"expected 6 faces, got {len(world)}"
for poly in world:
    n = newell(poly)
    c = [sum(v[i] for v in poly) / len(poly) for i in range(3)]
    assert sum(n[i] * c[i] for i in range(3)) < 0, f"face at {c} faces outward"

disp = objects.get("displacements", [])
assert len(disp) == 64, f"expected 64 displacement triangles, got {len(disp)}"
for tri in disp:
    assert newell(tri)[1] > 0, f"displacement triangle {tri} faces down"  # OBJ is Y-up
print("obj check passed: 6 faces facing into the room, 64 displacement triangles facing up")

if "--lightmap" in sys.argv:
    mtl = open(os.path.splitext(sys.argv[1])[0] + ".mtl").read()
    tga_name = re.search(r"^map_Kd (\S+)$", mtl, re.M).group(1)
    tga = open(os.path.join(os.path.dirname(sys.argv[1]), tga_name), "rb").read()
    width, height = struct.unpack_from("<HH", tga, 12)
    assert tga[2] == 2 and tga[16] == 24 and tga[17] & 0x20, "expected a 24-bit top-down TGA"
    assert len(tga) == 18 + width * height * 3, "TGA size doesn't match its header"
    assert any(tga[18:]), "lightmap atlas is all black"
    print(f"lightmap check passed: {width}x{height} atlas")
