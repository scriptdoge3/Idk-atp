// Checks displacement decoding against the synthetic map from make_test_bsp.py,
// whose layout the constants below mirror.
#include "bsp.h"
#include "filesystem.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

static bool near(float a, float b) { return std::fabs(a - b) <= 1e-4f; }
static bool near(const bsp::Vec3& a, const bsp::Vec3& b) {
    return near(a[0], b[0]) && near(a[1], b[1]) && near(a[2], b[2]);
}
static bsp::Vec3 lerp(const bsp::Vec3& a, const bsp::Vec3& b, float t) {
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
}

static float dispHeight(float x, float y) { return 12.0f + 0.25f * x + y * y / 64.0f; }

static void checkDisplacements(const bsp::Map& map) {
    check(map.dispInfos.size() == 3 && map.dispVerts.size() == 50, "dispinfo and dispvert lumps");

    bsp::SurfaceStats st;
    const std::vector<bsp::Displacement> disps = bsp::buildDisplacements(map, &st);
    check(disps.size() == 2 && st.exported == 2 && st.triangles == 64,
          "two displacements built, 32 triangles each");
    check(st.invalid == 1, "dispinfo whose face isn't a displacement rejected");
    if (disps.size() != 2) return;

    // Stored corners rotated to the one nearest startPosition.
    const bsp::Vec3 corners[2][4] = {
        {{0, 16, -32}, {0, -16, -32}, {-32, -16, -32}, {-32, 16, -32}},
        {{32, -16, -32}, {32, 16, -32}, {0, 16, -32}, {0, -16, -32}},
    };
    bool cornersOk = true, gridOk = true, alphaOk = true, uvOk = true, facingOk = true;
    for (size_t i = 0; i < 2; ++i) {
        const bsp::Displacement& d = disps[i];
        for (size_t k = 0; k < 4; ++k) cornersOk = cornersOk && near(d.corners[k], corners[i][k]);
        if (d.power != 2 || d.vertices.size() != 25 || d.indices.size() != 96) {
            gridOk = false;
            continue;
        }

        for (int r = 0; r <= 4; ++r) {
            const bsp::Vec3 a = lerp(corners[i][0], corners[i][1], r / 4.0f);
            const bsp::Vec3 b = lerp(corners[i][3], corners[i][2], r / 4.0f);
            for (int c = 0; c <= 4; ++c) {
                const bsp::Vec3 base = lerp(a, b, c / 4.0f);
                const bsp::SurfaceVertex& v = d.vertices[size_t(r * 5 + c)];
                const bsp::Vec3 want{base[0], base[1], base[2] + dispHeight(base[0], base[1])};
                gridOk = gridOk && near(v.pos, want);
                alphaOk = alphaOk && near(v.alpha, r / 4.0f);
                // Texture axes (1,0,0) and (0,-1,0) over a 128x128 texture,
                // projected from the flat base position.
                uvOk = uvOk && near(v.u, base[0] / 128.0f) && near(v.v, -base[1] / 128.0f);
            }
        }

        for (size_t t = 0; t + 2 < d.indices.size(); t += 3) {
            const bsp::Vec3& p0 = d.vertices[d.indices[t]].pos;
            const bsp::Vec3& p1 = d.vertices[d.indices[t + 1]].pos;
            const bsp::Vec3& p2 = d.vertices[d.indices[t + 2]].pos;
            const float nz = (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p1[1] - p0[1]) * (p2[0] - p0[0]);
            facingOk = facingOk && nz > 0;
        }
    }
    check(cornersOk, "grid starts at the corner nearest startPosition, in stored order");
    check(gridOk, "vertex (r, c) = bilerp of the corners + vec * dist");
    check(alphaOk, "per-vertex alpha normalized to 0-1");
    check(uvOk, "texture coordinates projected from the base position");
    check(facingOk, "triangles face the base face's front, whatever the stored winding");

    size_t seam = 0;
    for (const bsp::SurfaceVertex& v : disps[0].vertices) {
        if (!near(v.pos[0], 0.0f)) continue;
        for (const bsp::SurfaceVertex& w : disps[1].vertices) seam += near(v.pos, w.pos);
    }
    check(seam == 5, "shared edge closes: all 5 seam vertices coincide");

    bsp::SurfaceStats fst;
    bsp::buildSurfaces(map, 0, &fst);
    check(fst.displacement == 2 && fst.exported == 6, "buildSurfaces leaves displacement faces out");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: bsp_test <room.bsp>\n");
        return 2;
    }
    std::vector<uint8_t> data;
    bsp::Map map;
    std::string err;
    if (!vfs::readFile(argv[1], data) || !bsp::load(data.data(), data.size(), map, &err)) {
        std::fprintf(stderr, "error: cannot load %s %s\n", argv[1], err.c_str());
        return 1;
    }
    checkDisplacements(map);
    return failures ? 1 : 0;
}
