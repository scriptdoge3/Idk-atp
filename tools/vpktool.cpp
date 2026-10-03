// vpktool - inspect, extract and verify files in VPK archives.
#include "vpk.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  vpktool <name_dir.vpk> info\n"
                 "  vpktool <name_dir.vpk> list [substring]\n"
                 "  vpktool <name_dir.vpk> extract <path-in-vpk> <output-file>\n"
                 "  vpktool <name_dir.vpk> verify\n");
    return 2;
}

std::vector<const std::string*> sortedPaths(const vpk::Archive& a, const std::string& filter = "") {
    std::vector<const std::string*> paths;
    for (const auto& kv : a.entries())
        if (filter.empty() || kv.first.find(filter) != std::string::npos) paths.push_back(&kv.first);
    std::sort(paths.begin(), paths.end(),
              [](const std::string* x, const std::string* y) { return *x < *y; });
    return paths;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();

    vpk::Archive archive;
    std::string err;
    if (!archive.open(argv[1], &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    const std::string cmd = argv[2];

    if (cmd == "info") {
        uint64_t bytes = 0;
        size_t embedded = 0;
        int maxArchive = -1;
        for (const auto& kv : archive.entries()) {
            const vpk::Entry& e = kv.second;
            bytes += e.size();
            if (e.archiveIndex == vpk::kDirArchive) ++embedded;
            else maxArchive = std::max(maxArchive, int(e.archiveIndex));
        }
        std::printf("VPK v%u: %zu files, %.1f MiB total, %zu stored in the _dir file",
                    archive.version(), archive.entries().size(), bytes / 1048576.0, embedded);
        if (maxArchive >= 0) std::printf(", data archives 000-%03d", maxArchive);
        std::printf("\n");
        return 0;
    }

    if (cmd == "list") {
        const std::string filter = argc > 3 ? vpk::normalizePath(argv[3]) : "";
        for (const std::string* p : sortedPaths(archive, filter))
            std::printf("%10u  %s\n", archive.entries().at(*p).size(), p->c_str());
        return 0;
    }

    if (cmd == "extract") {
        if (argc < 5) return usage();
        std::vector<uint8_t> data;
        if (!archive.read(argv[3], data, /*verifyCrc=*/true, &err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        std::FILE* out = std::fopen(argv[4], "wb");
        if (!out || std::fwrite(data.data(), 1, data.size(), out) != data.size()) {
            std::fprintf(stderr, "error: cannot write %s\n", argv[4]);
            if (out) std::fclose(out);
            return 1;
        }
        std::fclose(out);
        std::printf("wrote %zu bytes to %s\n", data.size(), argv[4]);
        return 0;
    }

    if (cmd == "verify") {
        size_t checked = 0, failed = 0;
        std::vector<uint8_t> buf;
        for (const std::string* p : sortedPaths(archive)) {
            ++checked;
            if (!archive.read(archive.entries().at(*p), buf, /*verifyCrc=*/true, &err)) {
                std::fprintf(stderr, "FAIL %s: %s\n", p->c_str(), err.c_str());
                ++failed;
            }
        }
        std::printf("%zu files checked, %zu failed\n", checked, failed);
        return failed ? 1 : 0;
    }

    return usage();
}
