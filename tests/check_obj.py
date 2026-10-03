#!/usr/bin/env python3
"""Check the box-room OBJ: 6 world quads, every one facing inward (toward the
origin), and the two displacement patches: 2 x 32 triangles, all facing up."""
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
