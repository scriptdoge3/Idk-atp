#!/usr/bin/env python3
"""Write a tiny synthetic v20 BSP: a 128-unit box room seen from inside.

Deliberately awkward so the loader has to get things right: faces use a mix
of positive and negative surfedges, windings are stored in inconsistent
directions, the ceiling uses side=1 on an outward plane, and there's one
nodraw face that must be skipped.

Two displacement patches float above the floor, sharing the edge x = 0.
They're stored with opposite windings and start their grids at different
corners; both sample one height field, so a correct loader closes the seam.
A third dispinfo points at a face that isn't a displacement and must be
rejected.

The floor and the first displacement are lit by two light styles, and the
displacement is bump-lit too (four lightmaps per style). Every luxel encodes its own (x, y), style and
bump index, and each face's average colors sit before its lightofs as
decoys the reader must skip.
"""
import math
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
DISP_POWER = 2
DISP_Z = -32.0
DISPS = [  # (corners in stored order, index of the corner the grid starts at, reverse_edges)
    ([(-32, -16, DISP_Z), (-32, 16, DISP_Z), (0, 16, DISP_Z), (0, -16, DISP_Z)], 2, False),
    ([(0, -16, DISP_Z), (32, -16, DISP_Z), (32, 16, DISP_Z), (0, 16, DISP_Z)], 1, True),
]


# Lit texinfos: one luxel per 16 units. The 0.5 offset and the z terms make
# face extents start off luxel boundaries and depend on the base height.
LIGHTMAP_VECS = ((1 / 16, 0, 1 / 64, 0.5), (0, 1 / 16, 1 / 32, 0))
TEXINFO_LIT, TEXINFO_LIT_BUMP = 2, 3
STYLE_NUMBERS = (0, 5, 9, 11)


def luxel(x, y, style, bump):
    """ColorRGBExp32 with exponent 1, so the linear color is 2 * byte / 255."""
    return struct.pack("<3Bb", 12 * x, 12 * y, 50 * style + 10 * bump + 5, 1)


def disp_height(x, y):
    """Offset straight up from the base face; asymmetric so a transposed grid shows."""
    return 12.0 + 0.25 * x + y * y / 64.0


def lerp(a, b, t):
    return tuple(p + (q - p) * t for p, q in zip(a, b))


def disp_grid(corners, start):
    """Base positions row by row: corners rotated to start, then
    lerp(lerp(c0, c1, r/n), lerp(c3, c2, r/n), c/n)."""
    c = corners[start:] + corners[:start]
    n = 1 << DISP_POWER
    for r in range(n + 1):
        a, b = lerp(c[0], c[1], r / n), lerp(c[3], c[2], r / n)
        for col in range(n + 1):
            yield r, col, lerp(a, b, col / n)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "room.bsp"
    planes, verts, edges, surfedges, faces = [], [], [(0, 0)], [], []
    lighting = bytearray()

    def add_face(normal, dist, side, corners, texinfo, dispinfo, reverse_edges, light=None):
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

        styles, lightofs, mins, size = (0, 255, 255, 255), -1, (0, 0), (0, 0)
        if light:  # (number of styles, bump-lit)
            num_styles, bumped = light
            proj = [[sum(a * p for a, p in zip(vec[:3], c)) + vec[3] for c in corners]
                    for vec in LIGHTMAP_VECS]
            mins = [math.floor(min(e)) for e in proj]
            size = [math.ceil(max(e)) - m for e, m in zip(proj, mins)]
            lighting.extend(b"\xee" * 4 * num_styles)  # average color per style
            lightofs = len(lighting)
            for style in range(num_styles):
                for bump in range(4 if bumped else 1):
                    for y in range(size[1] + 1):
                        for x in range(size[0] + 1):
                            lighting.extend(luxel(x, y, style, bump))
            styles = STYLE_NUMBERS[:num_styles] + (255,) * (4 - num_styles)
        faces.append(struct.pack("<HBBihhhh4Bif2i2iiHHI", len(planes) - 1, side, 0,
                                 first_edge, n, texinfo, dispinfo, -1, *styles,
                                 lightofs, 0.0, *mins, *size, -1, 0, 0, 0))

    for i, (normal, dist, side, corners) in enumerate(ROOM):
        floor = i == 0
        add_face(normal, dist, side, corners, TEXINFO_LIT if floor else 0, -1,
                 reverse_edges=(i % 2 == 1), light=(2, False) if floor else None)
    nodraw_face = len(faces)
    add_face((0, 0, 1), 0.0, 0, [(-8, -8, 0), (8, -8, 0), (8, 8, 0), (-8, 8, 0)], 1, -1,
             reverse_edges=False)

    dispinfos, dispverts = [], []
    for i, (corners, start, reverse) in enumerate(DISPS):
        map_face = len(faces)
        lit = i == 0
        add_face((0, 0, 1), DISP_Z, 0, corners, TEXINFO_LIT_BUMP if lit else 0, i,
                 reverse_edges=reverse, light=(2, True) if lit else None)
        # startPosition is the grid's first corner, nudged off it a little.
        start_pos = (corners[start][0] + 0.5, corners[start][1] - 0.25, DISP_Z + 0.1)
        dispinfos.append((start_pos, len(dispverts), map_face))
        n = 1 << DISP_POWER
        for r, _, (x, y, _) in disp_grid(corners, start):
            dispverts.append(struct.pack("<3fff", 0, 0, 1, disp_height(x, y), 255.0 * r / n))
    dispinfos.append(((0, 0, 0), 0, nodraw_face))  # its face isn't a displacement

    def pack_dispinfo(start_pos, vert_start, map_face):
        head = struct.pack("<3fiiiifiH2xii", *start_pos, vert_start, 0, DISP_POWER, 0, 0.0, 1,
                           map_face, 0, 0)
        return head + bytes(176 - len(head))  # neighbor tables and allowed verts unused

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
           + struct.pack("<8f8fii", 1, 0, 0, 0, 0, -1, 0, 0, *([0.0] * 8), 0x80, 1)
           + struct.pack("<8f8fii", 1, 0, 0, 0, 0, -1, 0, 0, *sum(LIGHTMAP_VECS, ()), 0, 0)
           + struct.pack("<8f8fii", 1, 0, 0, 0, 0, -1, 0, 0, *sum(LIGHTMAP_VECS, ()), 0x800, 0),
        7: b"".join(faces),
        8: bytes(lighting),
        12: b"".join(struct.pack("<2H", *e) for e in edges),
        13: b"".join(struct.pack("<i", s) for s in surfedges),
        14: struct.pack("<9fiii", -H, -H, -H, H, H, H, 0, 0, 0, 0, 0, len(faces)),
        26: b"".join(pack_dispinfo(*d) for d in dispinfos),
        33: b"".join(dispverts),
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
