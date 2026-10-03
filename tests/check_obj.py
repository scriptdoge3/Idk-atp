#!/usr/bin/env python3
"""Check the box-room OBJ: 6 quads, every one facing inward (toward the origin)."""
import sys

verts, polys = [], []
for line in open(sys.argv[1]):
    parts = line.split()
    if parts and parts[0] == "v":
        verts.append(tuple(map(float, parts[1:4])))
    elif parts and parts[0] == "f":
        polys.append([verts[int(p.split("/")[0]) - 1] for p in parts[1:]])

assert len(polys) == 6, f"expected 6 faces, got {len(polys)}"
for poly in polys:
    n = [0.0, 0.0, 0.0]
    for a, b in zip(poly, poly[1:] + poly[:1]):  # Newell normal, CCW-positive
        n[0] += (a[1] - b[1]) * (a[2] + b[2])
        n[1] += (a[2] - b[2]) * (a[0] + b[0])
        n[2] += (a[0] - b[0]) * (a[1] + b[1])
    c = [sum(v[i] for v in poly) / len(poly) for i in range(3)]
    assert sum(n[i] * c[i] for i in range(3)) < 0, f"face at {c} faces outward"
print("obj check passed: 6 faces, all facing into the room")
