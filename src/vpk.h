// vpk.h - reader for Valve Pack (VPK) v1/v2 directory archives.
//
// Written from the publicly documented VPK file format, not from any Valve
// engine code. Plain C++17, no dependencies, builds for desktop Linux and the
// Android NDK (arm64).
//
// Not thread-safe: reads share cached FILE handles. Use one Archive per thread.
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vpk {

// Archive index meaning "this file's data is stored inside the _dir.vpk itself".
constexpr uint16_t kDirArchive = 0x7FFF;

struct Entry {
    uint32_t crc = 0;            // CRC-32 of the complete file contents
    uint16_t archiveIndex = 0;   // which name_###.vpk holds the data, or kDirArchive
    uint32_t offset = 0;         // byte offset inside that archive
    uint32_t length = 0;         // bytes stored in the archive (not counting preload)
    uint32_t preloadOffset = 0;  // start of this entry's bytes in Archive's preload blob
    uint16_t preloadSize = 0;    // bytes stored inline in the directory tree

    uint32_t size() const { return uint32_t(preloadSize) + length; }
};

class Archive {
public:
    // Opens "name_dir.vpk". The numbered "name_000.vpk", "name_001.vpk", ...
    // siblings are opened lazily the first time a file inside them is read.
    bool open(const std::string& dirPath, std::string* error = nullptr);

    // Lookups are case-insensitive and accept either slash style,
    // so "Materials\\Concrete\\Floor01.VMT" finds "materials/concrete/floor01.vmt".
    const Entry* find(std::string_view path) const;

    bool read(std::string_view path, std::vector<uint8_t>& out,
              bool verifyCrc = false, std::string* error = nullptr) const;
    bool read(const Entry& entry, std::vector<uint8_t>& out,
              bool verifyCrc = false, std::string* error = nullptr) const;

    uint32_t version() const { return version_; }

    // Keys are normalized paths (see normalizePath).
    const std::unordered_map<std::string, Entry>& entries() const { return entries_; }

private:
    struct FileCloser {
        void operator()(std::FILE* f) const { if (f) std::fclose(f); }
    };
    using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

    std::FILE* archiveFile(uint16_t index, std::string* error) const;

    std::string stem_;        // ".../hl2_misc" for ".../hl2_misc_dir.vpk"
    uint32_t version_ = 0;
    uint64_t dataBase_ = 0;   // where kDirArchive data starts inside the _dir file
    std::unordered_map<std::string, Entry> entries_;
    std::vector<uint8_t> preload_;
    mutable std::unordered_map<uint16_t, FilePtr> files_;
};

// Lowercases, turns '\\' into '/', and strips any leading "./" or "/".
std::string normalizePath(std::string_view path);

// Standard CRC-32 (same polynomial and result as zlib's crc32).
uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0);

} // namespace vpk
