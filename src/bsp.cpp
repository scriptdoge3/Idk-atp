#include "bsp.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace bsp {
namespace {

constexpr uint32_t kIdent = 0x50534256;  // "VBSP" read as little-endian
constexpr size_t kHeaderSize = 8 + kLumpCount * 16 + 4;

uint16_t u16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
int16_t s16(const uint8_t* p) { return int16_t(u16(p)); }
int32_t s32(const uint8_t* p) { return int32_t(u32(p)); }
float f32(const uint8_t* p) {
    const uint32_t bits = u32(p);
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}
Vec3 vec3(const uint8_t* p) { return {f32(p), f32(p + 4), f32(p + 8)}; }

float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float dot(const float* a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

bool fail(std::string* error, std::string msg) {
    if (error) *error = std::move(msg);
    return false;
}

struct Lumps {
    const uint8_t* file;
    uint32_t ofs[kLumpCount];
    uint32_t len[kLumpCount];
    uint32_t packedSize[kLumpCount];  // nonzero = LZMA-compressed lump
};

// Splits a lump into fixed-size records and decodes each one with `fn`.
template <typename T, typename Fn>
bool decode(const Lumps& l, int lump, size_t recSize, std::vector<T>& out, Fn fn,
            std::string* error) {
    if (l.packedSize[lump] != 0)
        return fail(error, "lump " + std::to_string(lump) + " is compressed (not supported yet)");
    if (l.len[lump] % recSize != 0)
        return fail(error, "lump " + std::to_string(lump) + " size isn't a multiple of " +
                               std::to_string(recSize));
    const size_t count = l.len[lump] / recSize;
    const uint8_t* base = l.file + l.ofs[lump];
    out.clear();
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) out.push_back(fn(base + i * recSize));
    return true;
}

Face decodeFace(const uint8_t* p) {
    Face f{};
    f.plane = u16(p);
    f.side = p[2];
    f.firstEdge = s32(p + 4);
    f.numEdges = s16(p + 8);
    f.texInfo = s16(p + 10);
    f.dispInfo = s16(p + 12);
    std::memcpy(f.styles, p + 16, 4);
    f.lightOfs = s32(p + 20);
    f.lightmapMins[0] = s32(p + 28);
    f.lightmapMins[1] = s32(p + 32);
    f.lightmapSize[0] = s32(p + 36);
    f.lightmapSize[1] = s32(p + 40);
    return f;
}

TexInfo decodeTexInfo(const uint8_t* p) {
    TexInfo t{};
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 4; ++j) {
            t.textureVecs[i][j] = f32(p + (i * 4 + j) * 4);
            t.lightmapVecs[i][j] = f32(p + 32 + (i * 4 + j) * 4);
        }
    t.flags = u32(p + 64);
    t.texData = s32(p + 68);
    return t;
}

// Field offsets inside the 176-byte dispinfo record; the neighbor tables
// and allowed-vertex bits after MapFace aren't needed to build geometry.
DispInfo decodeDispInfo(const uint8_t* p) {
    DispInfo d{};
    d.startPosition = vec3(p);
    d.dispVertStart = s32(p + 12);
    d.power = s32(p + 20);
    d.mapFace = u16(p + 36);
    return d;
}

DispVert decodeDispVert(const uint8_t* p) { return DispVert{vec3(p), f32(p + 12), f32(p + 16)}; }

// Newell's method: robust normal for any planar polygon, CCW = positive.
Vec3 polygonNormal(const std::vector<SurfaceVertex>& vs) {
    Vec3 n{0, 0, 0};
    for (size_t i = 0; i < vs.size(); ++i) {
        const Vec3& a = vs[i].pos;
        const Vec3& b = vs[(i + 1) % vs.size()].pos;
        n[0] += (a[1] - b[1]) * (a[2] + b[2]);
        n[1] += (a[2] - b[2]) * (a[0] + b[0]);
        n[2] += (a[0] - b[0]) * (a[1] + b[1]);
    }
    return n;
}

Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
}

constexpr uint32_t kHidden =
    kSurfSky2D | kSurfSky | kSurfTrigger | kSurfNoDraw | kSurfHint | kSurfSkip;

// The face's vertex positions in stored order. Walks the surfedges: a
// positive index uses the edge's first vertex, a negative one means the
// edge is reversed, so use its second.
bool faceCorners(const Map& map, const Face& f, std::vector<Vec3>& out) {
    out.clear();
    for (int i = 0; i < f.numEdges; ++i) {
        const int64_t sei = int64_t(f.firstEdge) + i;
        if (sei < 0 || size_t(sei) >= map.surfEdges.size()) return false;
        const int64_t se = map.surfEdges[size_t(sei)];
        const uint64_t ei = uint64_t(se >= 0 ? se : -se);
        if (ei >= map.edges.size()) return false;
        const uint16_t vi = map.edges[ei].v[se >= 0 ? 0 : 1];
        if (vi >= map.vertices.size()) return false;
        out.push_back(map.vertices[vi]);
    }
    return true;
}

// Texture projection for one face.
struct Mapping {
    const TexInfo* ti = nullptr;
    int texData = -1;
    float texW = 1.0f, texH = 1.0f;

    // Texture coordinates come from the flat base position, so displacement
    // textures stretch over steep terrain the way they do in the game.
    SurfaceVertex vertex(const Vec3& pos, const Vec3& base) const {
        return {pos, (dot(ti->textureVecs[0], base) + ti->textureVecs[0][3]) / texW,
                (dot(ti->textureVecs[1], base) + ti->textureVecs[1][3]) / texH};
    }
};

Mapping faceMapping(const Map& map, const TexInfo& ti) {
    Mapping m;
    m.ti = &ti;
    if (ti.texData >= 0 && size_t(ti.texData) < map.texDatas.size()) {
        const TexData& td = map.texDatas[size_t(ti.texData)];
        m.texData = ti.texData;
        if (td.width > 0) m.texW = float(td.width);
        if (td.height > 0) m.texH = float(td.height);
    }
    return m;
}

// The direction the face is seen from.
Vec3 frontNormal(const Map& map, const Face& f) {
    Vec3 n = map.planes[f.plane].normal;
    if (f.side) n = {-n[0], -n[1], -n[2]};
    return n;
}

} // namespace

bool load(const uint8_t* data, size_t size, Map& map, std::string* error) {
    map = Map{};
    if (size < kHeaderSize) return fail(error, "file too small to be a BSP");
    if (u32(data) != kIdent) return fail(error, "not a Source BSP (bad ident)");

    map.version = s32(data + 4);
    if (map.version != 19 && map.version != 20)
        return fail(error, "unsupported BSP version " + std::to_string(map.version) +
                               " (expected 19 or 20)");

    Lumps l{};
    l.file = data;
    for (int i = 0; i < kLumpCount; ++i) {
        const uint8_t* p = data + 8 + i * 16;
        l.ofs[i] = u32(p);
        l.len[i] = u32(p + 4);
        l.packedSize[i] = u32(p + 12);
        if (l.len[i] == 0) { l.ofs[i] = 0; continue; }
        if (uint64_t(l.ofs[i]) + l.len[i] > size)
            return fail(error, "lump " + std::to_string(i) + " points past the end of the file");
    }

    // Entities: one text blob of { "key" "value" } blocks.
    map.entities.assign(reinterpret_cast<const char*>(data + l.ofs[kEntities]), l.len[kEntities]);
    while (!map.entities.empty() && map.entities.back() == '\0') map.entities.pop_back();

    // HDR-only maps leave the LDR faces lump empty.
    int faceLump = kFaces;
    if (l.len[kFaces] == 0 && l.len[kFacesHdr] != 0) {
        faceLump = kFacesHdr;
        map.hdrFacesOnly = true;
    }

    std::vector<int32_t> stringTable;
    const bool ok =
        decode(l, kPlanes, 20, map.planes,
               [](const uint8_t* p) { return Plane{vec3(p), f32(p + 12), s32(p + 16)}; }, error) &&
        decode(l, kVertexes, 12, map.vertices, vec3, error) &&
        decode(l, kEdges, 4, map.edges,
               [](const uint8_t* p) { return Edge{{u16(p), u16(p + 2)}}; }, error) &&
        decode(l, kSurfEdges, 4, map.surfEdges, s32, error) &&
        decode(l, faceLump, 56, map.faces, decodeFace, error) &&
        decode(l, kTexInfo, 72, map.texInfos, decodeTexInfo, error) &&
        decode(l, kTexData, 32, map.texDatas,
               [](const uint8_t* p) {
                   return TexData{vec3(p), s32(p + 12), s32(p + 16), s32(p + 20)};
               }, error) &&
        decode(l, kModels, 48, map.models,
               [](const uint8_t* p) {
                   return Model{vec3(p), vec3(p + 12), vec3(p + 24),
                                s32(p + 36), s32(p + 40), s32(p + 44)};
               }, error) &&
        decode(l, kTexDataStringTable, 4, stringTable, s32, error) &&
        decode(l, kDispInfo, 176, map.dispInfos, decodeDispInfo, error) &&
        decode(l, kDispVerts, 20, map.dispVerts, decodeDispVert, error);
    if (!ok) return false;
    if (map.models.empty()) return fail(error, "map has no models (no world)");

    // Material names: texdata -> string table -> offset into the string blob.
    const char* strings = reinterpret_cast<const char*>(data + l.ofs[kTexDataStringData]);
    const size_t stringsLen = l.len[kTexDataStringData];
    map.texDataNames.reserve(map.texDatas.size());
    for (const TexData& td : map.texDatas) {
        std::string name;
        if (td.nameId >= 0 && size_t(td.nameId) < stringTable.size()) {
            const int32_t ofs = stringTable[size_t(td.nameId)];
            if (ofs >= 0 && size_t(ofs) < stringsLen) {
                const char* s = strings + ofs;
                name.assign(s, strnlen(s, stringsLen - size_t(ofs)));
            }
        }
        map.texDataNames.push_back(std::move(name));
    }
    return true;
}

std::vector<Surface> buildSurfaces(const Map& map, int modelIndex, SurfaceStats* stats) {
    SurfaceStats st;
    std::vector<Surface> out;
    if (modelIndex < 0 || size_t(modelIndex) >= map.models.size()) {
        if (stats) *stats = st;
        return out;
    }

    const Model& model = map.models[size_t(modelIndex)];
    std::vector<Vec3> corners;

    for (int64_t fi = model.firstFace; fi < int64_t(model.firstFace) + model.numFaces; ++fi) {
        if (fi < 0 || size_t(fi) >= map.faces.size()) { ++st.invalid; continue; }
        const Face& f = map.faces[size_t(fi)];

        if (f.dispInfo != -1) { ++st.displacement; continue; }
        if (f.texInfo < 0 || size_t(f.texInfo) >= map.texInfos.size() ||
            f.numEdges < 3 || f.plane >= map.planes.size()) {
            ++st.invalid;
            continue;
        }
        const TexInfo& ti = map.texInfos[size_t(f.texInfo)];
        if (ti.flags & kHidden) { ++st.toolOrSky; continue; }
        if (!faceCorners(map, f, corners)) { ++st.invalid; continue; }

        const Mapping mapping = faceMapping(map, ti);
        Surface s;
        s.faceIndex = int(fi);
        s.texData = mapping.texData;
        s.vertices.reserve(corners.size());
        for (const Vec3& p : corners) s.vertices.push_back(mapping.vertex(p, p));

        // Normalize winding so every surface is CCW seen from its front side,
        // whatever order the compiler stored the edges in.
        if (dot(polygonNormal(s.vertices), frontNormal(map, f)) < 0)
            std::reverse(s.vertices.begin(), s.vertices.end());

        ++st.exported;
        st.triangles += s.vertices.size() - 2;
        out.push_back(std::move(s));
    }

    if (stats) *stats = st;
    return out;
}

std::vector<Displacement> buildDisplacements(const Map& map, SurfaceStats* stats) {
    SurfaceStats st;
    std::vector<Displacement> out;
    std::vector<Vec3> corners;

    for (size_t di = 0; di < map.dispInfos.size(); ++di) {
        const DispInfo& info = map.dispInfos[di];
        if (info.mapFace >= map.faces.size()) { ++st.invalid; continue; }
        const Face& f = map.faces[info.mapFace];
        if (f.dispInfo != int64_t(di) || f.numEdges != 4 || f.texInfo < 0 ||
            size_t(f.texInfo) >= map.texInfos.size() || f.plane >= map.planes.size() ||
            info.power < 1 || info.power > 4) {
            ++st.invalid;
            continue;
        }
        const TexInfo& ti = map.texInfos[size_t(f.texInfo)];
        if (ti.flags & kHidden) { ++st.toolOrSky; continue; }

        const uint32_t n = 1u << info.power;
        const uint32_t side = n + 1;
        if (info.dispVertStart < 0 ||
            uint64_t(info.dispVertStart) + uint64_t(side) * side > map.dispVerts.size() ||
            !faceCorners(map, f, corners)) {
            ++st.invalid;
            continue;
        }

        // The grid starts at the corner nearest startPosition.
        size_t first = 0;
        float best = 0.0f;
        for (size_t i = 0; i < 4; ++i) {
            const Vec3 d = sub(corners[i], info.startPosition);
            if (i == 0 || dot(d, d) < best) { best = dot(d, d); first = i; }
        }
        std::rotate(corners.begin(), corners.begin() + std::ptrdiff_t(first), corners.end());

        const Mapping mapping = faceMapping(map, ti);
        Displacement d;
        d.faceIndex = int(info.mapFace);
        d.texData = mapping.texData;
        d.power = int(info.power);
        std::copy(corners.begin(), corners.end(), d.corners.begin());

        d.vertices.reserve(size_t(side) * side);
        const DispVert* dv = map.dispVerts.data() + info.dispVertStart;
        for (uint32_t r = 0; r < side; ++r) {
            const float tr = float(r) / float(n);
            const Vec3 a = lerp(corners[0], corners[1], tr);
            const Vec3 b = lerp(corners[3], corners[2], tr);
            for (uint32_t c = 0; c < side; ++c, ++dv) {
                const Vec3 base = lerp(a, b, float(c) / float(n));
                const Vec3 pos{base[0] + dv->vec[0] * dv->dist, base[1] + dv->vec[1] * dv->dist,
                               base[2] + dv->vec[2] * dv->dist};
                SurfaceVertex v = mapping.vertex(pos, base);
                v.alpha = dv->alpha / 255.0f;
                d.vertices.push_back(v);
            }
        }

        // Every triangle below winds the same way as (r,c), (r+1,c), (r,c+1),
        // which faces along (c1 - c0) x (c3 - c0) on the base face. Swap two
        // corners of each triangle if that points away from the front.
        const bool flip =
            dot(cross(sub(corners[1], corners[0]), sub(corners[3], corners[0])),
                frontNormal(map, f)) < 0;
        auto tri = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
            d.indices.insert(d.indices.end(), {i0, flip ? i2 : i1, flip ? i1 : i2});
        };
        d.indices.reserve(size_t(n) * n * 6);
        for (uint32_t r = 0; r < n; ++r) {
            for (uint32_t c = 0; c < n; ++c) {
                const uint32_t i00 = r * side + c, i10 = i00 + side;
                const uint32_t i01 = i00 + 1, i11 = i10 + 1;
                if ((r + c) % 2 == 0) {
                    tri(i00, i10, i11);
                    tri(i00, i11, i01);
                } else {
                    tri(i00, i10, i01);
                    tri(i10, i11, i01);
                }
            }
        }

        ++st.exported;
        st.triangles += d.indices.size() / 3;
        out.push_back(std::move(d));
    }

    if (stats) *stats = st;
    return out;
}

} // namespace bsp
