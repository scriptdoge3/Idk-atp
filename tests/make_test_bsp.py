#!/usr/bin/env python3
"""Write a tiny synthetic v20 BSP: a 128-unit box room seen from inside.

Deliberately awkward so the loader has to get things right: faces use a mix
of positive and negative surfedges, windings are stored in inconsistent
directions, the ceiling uses side=1 on an outward plane, and there's one
nodraw face and one displacement face that must be skipped.
"""
import struct
import sys

H = 64.0
ROOM = [  # (plane normal, dist, side, corners) - corners in arbitrary loop order
    ((0, 0, 1), -H, 0, [(-H, -H, -H), (H, -H, -H), (H, H, -H), (-H, H, -H)]),   # floor
    ((0, 0, 1), H, 1, [(-H, -H, H), (-H, H, H), (H, H, H), (H, -H, H)]),       # ceiling
    ((1, 0, 0), -H, 0, [(-H, -H, -H), (-H, -H, H), (-H, H, H), (-H, H, -H)]),
    ((-1, 0, 0), -H, 0, [(H, -H, -H), (H, H, -H), (H, H, H), (H, -H, H)]),
    ((0, 1, 0), -H, 0, [(-H, -H, -H), (H, -H, -H), (H, -H, H), (-H, -H, H)]),
    ((0, -1, 0), -H, 0, [(-H, H, -H), (-H, H, H), (H, H, H), (H, H, -H)]),
]
EXTRA = [  # (texinfo, dispinfo) for faces that must be skipped
    (1, -1),  # nodraw
    (0, 0),   # displacement
]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "room.bsp"
    planes, verts, edges, surfedges, faces = [], [], [(0, 0)], [], []

    def add_face(normal, dist, side, corners, texinfo, dispinfo, reverse_edges):
        planes.append(struct.pack("<3ffi", *normal, dist, 0))
        first_vert = len(verts)
        verts.extend(corners)
        first_edge = len(surfedges)
        n = len(corners)
        for i in range(n):
            a, b = first_vert + i, first_vert + (i + 1) % n
            if reverse_edges:  # store reversed, reference it negatively
                edges.append((b, a))
                surfedges.append(-(len(edges) - 1))
            else:
                edges.append((a, b))
                surfedges.append(len(edges) - 1)
        faces.append(struct.pack("<HBBihhhh4Bif2i2iiHHI", len(planes) - 1, side, 0,
                                 first_edge, n, texinfo, dispinfo, -1, 0, 255, 255, 255,
                                 -1, 0.0, 0, 0, 0, 0, -1, 0, 0, 0))

    for i, (normal, dist, side, corners) in enumerate(ROOM):
        add_face(normal, dist, side, corners, 0, -1, reverse_edges=(i % 2 == 1))
    quad = [(-8, -8, 0), (8, -8, 0), (8, 8, 0), (-8, 8, 0)]
    for texinfo, dispinfo in EXTRA:
        add_face((0, 0, 1), 0.0, 0, quad, texinfo, dispinfo, reverse_edges=False)

    names = [b"test/concrete01", b"test/nodraw"]
    string_data, string_table = b"", []
    for name in names:
        string_table.append(len(string_data))
        string_data += name + b"\0"

    lumps = {
        0: b'{\n"classname" "worldspawn"\n}\n{\n"classname" "info_player_start"\n'
           b'"origin" "0 0 -64"\n}\n\0',
        1: b"".join(planes),
        2: struct.pack("<3fiiiii", 0.5, 0.4, 0.3, 0, 128, 128, 128, 128)
           + struct.pack("<3fiiiii", 0.0, 0.0, 0.0, 1, 64, 64, 64, 64),
        3: b"".join(struct.pack("<3f", *v) for v in verts),
        6: struct.pack("<8f8fii", 1, 0, 0, 0, 0, -1, 0, 0, *([0.0] * 8), 0, 0)
           + struct.pack("<8f8fii", 1, 0, 0, 0, 0, -1, 0, 0, *([0.0] * 8), 0x80, 1),
        7: b"".join(faces),
        12: b"".join(struct.pack("<2H", *e) for e in edges),
        13: b"".join(struct.pack("<i", s) for s in surfedges),
        14: struct.pack("<9fiii", -H, -H, -H, H, H, H, 0, 0, 0, 0, 0, len(faces)),
        43: string_data,
        44: b"".join(struct.pack("<i", o) for o in string_table),
    }

    header_size = 8 + 64 * 16 + 4
    body, directory = bytearray(), []
    for i in range(64):
        data = lumps.get(i, b"")
        offset = header_size + len(body) if data else 0
        directory.append(struct.pack("<iiii", offset, len(data), 0, 0))
        body += data + b"\0" * (-len(data) % 4)

    with open(out, "wb") as f:
        f.write(b"VBSP" + struct.pack("<i", 20) + b"".join(directory) + struct.pack("<i", 1) + body)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
