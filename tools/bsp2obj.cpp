// bsp2obj - export a map's world geometry to Wavefront OBJ + MTL.
//
// A quick way to check the BSP loader against real maps in Blender before
// the renderer exists. Output is Y-up and in meters (Source is Z-up, inches).
#include "bsp.h"
#include "filesystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage: bsp2obj [-g <gamedir>]... <map> <out.obj>\n"
                 "  <map> is a .bsp on disk, or a path like maps/foo.bsp looked up\n"
                 "  in the mounted game dirs (loose files first, then VPKs).\n");
    return 2;
}

std::string materialName(const bsp::Map& map, int texData) {
    std::string name = texData >= 0 ? map.texDataNames[size_t(texData)] : "";
    if (name.empty()) name = "missing_texdata";
    for (char& c : name)
        if (c == ' ') c = '_';
    return name;
}

// Sewn neighbors share a base edge and should meet exactly along it, so on a
// real map a wide gap means the displacement grid is being laid out wrong.
struct Seams {
    size_t shared = 0;
    float widestGap = 0.0f;
};

Seams checkSeams(const std::vector<bsp::Displacement>& disps) {
    // Grid edge e runs from corner kEnds[e][0] to corner kEnds[e][1].
    static constexpr int kEnds[4][2] = {{0, 1}, {1, 2}, {3, 2}, {0, 3}};
    auto point = [&](size_t di, int e, uint32_t k) -> const bsp::Vec3& {
        const bsp::Displacement& d = disps[di];
        const uint32_t n = 1u << d.power, side = n + 1;
        const uint32_t idx = e == 0 ? k * side : e == 1 ? n * side + k : e == 2 ? k * side + n : k;
        return d.vertices[idx].pos;
    };
    auto quantize = [](const bsp::Vec3& v) {
        return std::array<long, 3>{std::lround(v[0] * 8), std::lround(v[1] * 8), std::lround(v[2] * 8)};
    };

    struct Open { size_t disp; int edge; bool reversed; };
    std::map<std::array<long, 6>, Open> open;
    Seams out;
    for (size_t di = 0; di < disps.size(); ++di) {
        const uint32_t n = 1u << disps[di].power;
        for (int e = 0; e < 4; ++e) {
            auto a = quantize(disps[di].corners[size_t(kEnds[e][0])]);
            auto b = quantize(disps[di].corners[size_t(kEnds[e][1])]);
            const bool reversed = b < a;
            if (reversed) std::swap(a, b);
            const std::array<long, 6> key{a[0], a[1], a[2], b[0], b[1], b[2]};

            const auto it = open.find(key);
            if (it == open.end()) {
                open.emplace(key, Open{di, e, reversed});
                continue;
            }
            const Open& o = it->second;
            if (disps[o.disp].power != disps[di].power) continue;
            ++out.shared;
            for (uint32_t k = 0; k <= n; ++k) {
                const bsp::Vec3& p = point(o.disp, o.edge, o.reversed ? n - k : k);
                const bsp::Vec3& q = point(di, e, reversed ? n - k : k);
                const float gap = std::sqrt((p[0] - q[0]) * (p[0] - q[0]) +
                                            (p[1] - q[1]) * (p[1] - q[1]) +
                                            (p[2] - q[2]) * (p[2] - q[2]));
                out.widestGap = std::max(out.widestGap, gap);
            }
        }
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    vfs::FileSystem fs;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-g") {
            if (++i >= argc) return usage();
            std::string notes;
            if (!fs.mountGameDir(argv[i], &notes)) {
                std::fprintf(stderr, "error: %s\n", notes.c_str());
                return 1;
            }
            if (!notes.empty()) std::fprintf(stderr, "%s", notes.c_str());
        } else {
            args.push_back(a);
        }
    }
    if (args.size() != 2) return usage();
    const std::string& mapArg = args[0];
    const std::string& objPath = args[1];

    std::vector<uint8_t> data;
    std::string err;
    std::error_code ec;
    if (std::filesystem::is_regular_file(mapArg, ec)) {
        if (!vfs::readFile(mapArg, data)) {
            std::fprintf(stderr, "error: cannot read %s\n", mapArg.c_str());
            return 1;
        }
    } else if (!fs.read(mapArg, data, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }

    bsp::Map map;
    if (!bsp::load(data.data(), data.size(), map, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }

    bsp::SurfaceStats st, dst;
    std::vector<bsp::Surface> surfaces = bsp::buildSurfaces(map, 0, &st);
    std::vector<bsp::Displacement> disps = bsp::buildDisplacements(map, &dst);
    auto byTexData = [](const auto& a, const auto& b) { return a.texData < b.texData; };
    std::stable_sort(surfaces.begin(), surfaces.end(), byTexData);
    std::stable_sort(disps.begin(), disps.end(), byTexData);

    // Materials: texdata reflectivity is the texture's average color, which
    // makes a decent flat-shaded preview until VTF decoding exists.
    const std::filesystem::path mtlPath = std::filesystem::path(objPath).replace_extension(".mtl");
    std::FILE* mtl = std::fopen(mtlPath.string().c_str(), "w");
    std::FILE* obj = std::fopen(objPath.c_str(), "w");
    if (!mtl || !obj) {
        std::fprintf(stderr, "error: cannot write output files\n");
        if (mtl) std::fclose(mtl);
        if (obj) std::fclose(obj);
        return 1;
    }

    std::set<int> written;
    auto writeMaterial = [&](int texData) {
        if (!written.insert(texData).second) return;
        bsp::Vec3 kd{0.8f, 0.8f, 0.8f};
        if (texData >= 0) kd = map.texDatas[size_t(texData)].reflectivity;
        std::fprintf(mtl, "newmtl %s\nKd %.4f %.4f %.4f\n\n", materialName(map, texData).c_str(),
                     kd[0], kd[1], kd[2]);
    };
    for (const bsp::Surface& s : surfaces) writeMaterial(s.texData);
    for (const bsp::Displacement& d : disps) writeMaterial(d.texData);

    constexpr float kInchToMeter = 0.0254f;
    std::fprintf(obj, "mtllib %s\n", mtlPath.filename().string().c_str());
    size_t next = 1;  // OBJ indices are 1-based
    int currentTex = -2;
    auto useMaterial = [&](int texData) {
        if (texData == currentTex) return;
        currentTex = texData;
        std::fprintf(obj, "usemtl %s\n", materialName(map, currentTex).c_str());
    };
    auto writeVertex = [&](const bsp::SurfaceVertex& v) {
        // Source (x, y, z) Z-up -> (x, z, -y) Y-up. A pure rotation, so winding is kept.
        std::fprintf(obj, "v %.5f %.5f %.5f\nvt %.5f %.5f\n", v.pos[0] * kInchToMeter,
                     v.pos[2] * kInchToMeter, -v.pos[1] * kInchToMeter, v.u, 1.0f - v.v);
    };

    std::fprintf(obj, "o world\n");
    for (const bsp::Surface& s : surfaces) {
        useMaterial(s.texData);
        for (const bsp::SurfaceVertex& v : s.vertices) writeVertex(v);
        std::fprintf(obj, "f");
        for (size_t i = 0; i < s.vertices.size(); ++i) std::fprintf(obj, " %zu/%zu", next + i, next + i);
        std::fprintf(obj, "\n");
        next += s.vertices.size();
    }

    if (!disps.empty()) {
        std::fprintf(obj, "o displacements\n");
        currentTex = -2;
    }
    for (const bsp::Displacement& d : disps) {
        useMaterial(d.texData);
        for (const bsp::SurfaceVertex& v : d.vertices) writeVertex(v);
        for (size_t i = 0; i + 2 < d.indices.size(); i += 3) {
            std::fprintf(obj, "f");
            for (size_t k = 0; k < 3; ++k) std::fprintf(obj, " %zu/%zu", next + d.indices[i + k], next + d.indices[i + k]);
            std::fprintf(obj, "\n");
        }
        next += d.vertices.size();
    }
    std::fclose(obj);
    std::fclose(mtl);

    std::printf("BSP v%d%s: %zu world faces -> %zu polygons (%zu triangles), "
                "%zu displacements (%zu triangles)\n",
                map.version, map.hdrFacesOnly ? " (HDR faces)" : "",
                st.exported + st.displacement + st.toolOrSky + st.invalid, st.exported, st.triangles,
                dst.exported, dst.triangles);
    std::printf("skipped: %zu tool/sky/trigger, %zu invalid faces\n", st.toolOrSky, st.invalid);
    if (dst.toolOrSky || dst.invalid)
        std::printf("skipped displacements: %zu tool/sky, %zu invalid\n", dst.toolOrSky, dst.invalid);
    const Seams seams = checkSeams(disps);
    if (seams.shared)
        std::printf("displacement seams: %zu shared edges, widest gap %.3f units\n", seams.shared,
                    seams.widestGap);
    std::printf("wrote %s and %s\n", objPath.c_str(), mtlPath.string().c_str());
    return 0;
}
