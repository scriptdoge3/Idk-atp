// bsp2obj - export a map's world geometry to Wavefront OBJ + MTL.
//
// A quick way to check the BSP loader against real maps in Blender before
// the renderer exists. Output is Y-up and in meters (Source is Z-up, inches).
#include "bsp.h"
#include "filesystem.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
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

    bsp::SurfaceStats st;
    std::vector<bsp::Surface> surfaces = bsp::buildSurfaces(map, 0, &st);
    std::stable_sort(surfaces.begin(), surfaces.end(),
                     [](const bsp::Surface& a, const bsp::Surface& b) { return a.texData < b.texData; });

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
    for (const bsp::Surface& s : surfaces) {
        if (!written.insert(s.texData).second) continue;
        bsp::Vec3 kd{0.8f, 0.8f, 0.8f};
        if (s.texData >= 0) kd = map.texDatas[size_t(s.texData)].reflectivity;
        std::fprintf(mtl, "newmtl %s\nKd %.4f %.4f %.4f\n\n", materialName(map, s.texData).c_str(),
                     kd[0], kd[1], kd[2]);
    }

    constexpr float kInchToMeter = 0.0254f;
    std::fprintf(obj, "mtllib %s\n", mtlPath.filename().string().c_str());
    size_t next = 1;  // OBJ indices are 1-based
    int currentTex = -2;
    for (const bsp::Surface& s : surfaces) {
        if (s.texData != currentTex) {
            currentTex = s.texData;
            std::fprintf(obj, "usemtl %s\n", materialName(map, currentTex).c_str());
        }
        for (const bsp::SurfaceVertex& v : s.vertices) {
            // Source (x, y, z) Z-up -> (x, z, -y) Y-up. A pure rotation, so winding is kept.
            std::fprintf(obj, "v %.5f %.5f %.5f\nvt %.5f %.5f\n", v.pos[0] * kInchToMeter,
                         v.pos[2] * kInchToMeter, -v.pos[1] * kInchToMeter, v.u, 1.0f - v.v);
        }
        std::fprintf(obj, "f");
        for (size_t i = 0; i < s.vertices.size(); ++i) std::fprintf(obj, " %zu/%zu", next + i, next + i);
        std::fprintf(obj, "\n");
        next += s.vertices.size();
    }
    std::fclose(obj);
    std::fclose(mtl);

    std::printf("BSP v%d%s: %zu world faces -> %zu exported (%zu triangles)\n", map.version,
                map.hdrFacesOnly ? " (HDR faces)" : "", st.exported + st.displacement + st.toolOrSky + st.invalid,
                st.exported, st.triangles);
    std::printf("skipped: %zu displacement, %zu tool/sky/trigger, %zu invalid\n", st.displacement,
                st.toolOrSky, st.invalid);
    std::printf("wrote %s and %s\n", objPath.c_str(), mtlPath.string().c_str());
    return 0;
}
