// filesystem.h - search-path filesystem over loose directories and VPKs.
//
// Mounts are searched in the order they were added; the first match wins.
// All lookups are case-insensitive, so mixed-case files on Linux/Android
// resolve the same way they do on Windows.
#pragma once

#include "vpk.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vfs {

class FileSystem {
public:
    // Mounts a game folder the usual way: loose files first (so they override
    // packed ones), then every *_dir.vpk directly inside it, in name order.
    // VPKs that fail to open are skipped and described in `warnings`.
    bool mountGameDir(const std::string& dir, std::string* warnings = nullptr);

    bool mountDir(const std::string& dir, std::string* error = nullptr);
    bool mountVpk(const std::string& dirVpkPath, std::string* error = nullptr);

    bool exists(std::string_view path) const;
    bool read(std::string_view path, std::vector<uint8_t>& out,
              std::string* error = nullptr) const;

private:
    struct Mount {
        std::string label;
        std::unordered_map<std::string, std::string> loose;  // normalized -> real path
        std::unique_ptr<vpk::Archive> vpk;
    };
    std::vector<Mount> mounts_;
};

// Reads a whole file from the real disk.
bool readFile(const std::string& path, std::vector<uint8_t>& out);

} // namespace vfs
