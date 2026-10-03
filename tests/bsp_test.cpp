// Checks displacements and lightmaps against the synthetic map from
// make_test_bsp.py, whose layout the constants below mirror.
#include "bsp.h"
#include "filesystem.h"

#include <algorithm>
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

// Face indices and lightmap layout from make_test_bsp.py.
constexpr int kFloor = 0, kCeiling = 1, kDispA = 7, kDispB = 8;

// Luxel (x, y) of style slot `style`, bump `bump`: bytes (12x, 12y, 50 style +
// 10 bump + 5) with exponent 1. Linear in x and y, so bilinear filtering
// at a fractional position gives the same formula.
static bsp::Vec3 expectedLuxel(float x, float y, int style, int bump) {
    return {24.0f * x / 255.0f, 24.0f * y / 255.0f, 2.0f * float(50 * style + 10 * bump + 5) / 255.0f};
}

static bool lightmapMatches(const bsp::Lightmap& lm, int w, int h, int style, int bump) {
    if (lm.width != w || lm.height != h || lm.luxels.size() != size_t(w * h)) return false;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (!near(lm.luxels[size_t(y * w + x)], expectedLuxel(float(x), float(y), style, bump)))
                return false;
    return true;
}

static bsp::Vec3 sampleBilinear(const bsp::LightmapAtlas& a, float u, float v) {
    const float x = u * float(a.width) - 0.5f, y = v * float(a.height) - 0.5f;
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float fx = x - float(x0), fy = y - float(y0);
    auto at = [&](int xi, int yi) {
        xi = std::clamp(xi, 0, a.width - 1);
        yi = std::clamp(yi, 0, a.height - 1);
        return a.luxels[size_t(yi) * size_t(a.width) + size_t(xi)];
    };
    bsp::Vec3 out{};
    for (int k = 0; k < 3; ++k) {
        const float top = at(x0, y0)[k] + (at(x0 + 1, y0)[k] - at(x0, y0)[k]) * fx;
        const float bottom = at(x0, y0 + 1)[k] + (at(x0 + 1, y0 + 1)[k] - at(x0, y0 + 1)[k]) * fx;
        out[k] = top + (bottom - top) * fy;
    }
    return out;
}

static void checkLightmaps(const bsp::Map& map) {
    check(map.lighting.size() == 1024, "lighting lump");
    const bsp::Face& floor = map.faces[kFloor];
    const bsp::Face& dispA = map.faces[kDispA];
    check(bsp::lightStyleCount(floor) == 2 && bsp::lightStyleCount(dispA) == 2 &&
              bsp::lightStyleCount(map.faces[kDispB]) == 0 &&
              bsp::lightStyleCount(map.faces[kCeiling]) == 0,
          "light style counts (unlit faces have none)");

    bsp::Lightmap lm;
    check(bsp::decodeLightmap(map, floor, 0, 0, lm) && lightmapMatches(lm, 10, 9, 0, 0),
          "floor lightmap: 10x9 luxels, row by row, after the average colors");
    check(bsp::decodeLightmap(map, floor, 1, 0, lm) && lightmapMatches(lm, 10, 9, 1, 0),
          "second light style follows the first");
    check(bsp::decodeLightmap(map, dispA, 0, 2, lm) && lightmapMatches(lm, 3, 3, 0, 2) &&
              bsp::decodeLightmap(map, dispA, 1, 0, lm) && lightmapMatches(lm, 3, 3, 1, 0) &&
              bsp::decodeLightmap(map, dispA, 1, 3, lm) && lightmapMatches(lm, 3, 3, 1, 3),
          "bump-lit face: per style, the flat map then one per bump direction");
    check(!bsp::decodeLightmap(map, floor, 2, 0, lm) && !bsp::decodeLightmap(map, floor, 0, 1, lm) &&
              !bsp::decodeLightmap(map, dispA, 0, 4, lm) &&
              !bsp::decodeLightmap(map, map.faces[kCeiling], 0, 0, lm),
          "missing styles, bumps and unlit faces are refused");

    // Lightmap coordinates: (x/16 + z/64 + 0.5, y/16 + z/32) minus the face's
    // mins, projected from the flat base position on displacements.
    auto expectLu = [](const bsp::Vec3& p, int minS) {
        return p[0] / 16.0f + p[2] / 64.0f + 0.5f - float(minS);
    };
    auto expectLv = [](const bsp::Vec3& p, int minT) { return p[1] / 16.0f + p[2] / 32.0f - float(minT); };
    const std::vector<bsp::Surface> surfaces = bsp::buildSurfaces(map);
    const std::vector<bsp::Displacement> disps = bsp::buildDisplacements(map);
    const bsp::Surface* floorSurface = nullptr;
    for (const bsp::Surface& s : surfaces)
        if (s.faceIndex == kFloor) floorSurface = &s;
    bool floorOk = floorSurface && floorSurface->vertices.size() == 4;
    for (size_t i = 0; floorOk && i < 4; ++i) {
        const bsp::SurfaceVertex& v = floorSurface->vertices[i];
        floorOk = near(v.lu, expectLu(v.pos, -5)) && near(v.lv, expectLv(v.pos, -6));
    }
    check(floorOk, "floor vertices' lightmap coordinates");

    bool dispOk = !disps.empty() && disps[0].faceIndex == kDispA && disps[0].vertices.size() == 25;
    for (int r = 0; dispOk && r <= 4; ++r) {
        const bsp::Displacement& d = disps[0];
        const bsp::Vec3 a = lerp(d.corners[0], d.corners[1], r / 4.0f);
        const bsp::Vec3 b = lerp(d.corners[3], d.corners[2], r / 4.0f);
        for (int c = 0; dispOk && c <= 4; ++c) {
            const bsp::Vec3 base = lerp(a, b, c / 4.0f);
            const bsp::SurfaceVertex& v = d.vertices[size_t(r * 5 + c)];
            dispOk = near(v.lu, expectLu(base, -2)) && near(v.lv, expectLv(base, -2));
        }
    }
    check(dispOk, "displacement lightmap coordinates from the base position");

    const bsp::LightmapAtlas atlas = bsp::buildLightmapAtlas(map);
    bool rectsOk = atlas.faces.size() == map.faces.size() &&
                   atlas.luxels.size() == size_t(atlas.width) * size_t(atlas.height);
    for (size_t i = 0; rectsOk && i < atlas.faces.size(); ++i) {
        const bsp::LightmapAtlas::Rect& r = atlas.faces[i];
        const bool lit = int(i) == kFloor || int(i) == kDispA;
        rectsOk = lit ? r.x >= 0 && r.y >= 0 && r.x + r.width <= atlas.width &&
                            r.y + r.height <= atlas.height
                      : r.width == 0;
    }
    const bsp::LightmapAtlas::Rect fr = rectsOk ? atlas.faces[kFloor] : bsp::LightmapAtlas::Rect{};
    const bsp::LightmapAtlas::Rect dr = rectsOk ? atlas.faces[kDispA] : bsp::LightmapAtlas::Rect{};
    rectsOk = rectsOk && fr.width == 10 && fr.height == 9 && dr.width == 3 && dr.height == 3 &&
              (fr.x + fr.width <= dr.x || dr.x + dr.width <= fr.x || fr.y + fr.height <= dr.y ||
               dr.y + dr.height <= fr.y);
    check(rectsOk, "atlas holds exactly the lit faces, without overlap");

    bool atlasOk = rectsOk && floorSurface;
    for (size_t i = 0; atlasOk && i < floorSurface->vertices.size(); ++i) {
        const bsp::SurfaceVertex& v = floorSurface->vertices[i];
        const std::array<float, 2> uv = bsp::atlasCoords(atlas, kFloor, v);
        atlasOk = near(sampleBilinear(atlas, uv[0], uv[1]), expectedLuxel(v.lu, v.lv, 0, 0));
    }
    check(atlasOk, "sampling the atlas at a vertex's coordinates gives its light");
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
    checkLightmaps(map);
    return failures ? 1 : 0;
}
