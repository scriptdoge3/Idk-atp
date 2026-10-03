#include "vpk.h"

#include <array>
#include <cstring>

namespace vpk {
namespace {

constexpr uint32_t kSignature = 0x55AA1234;

// Explicit little-endian decoding, so this never depends on host byte order.
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

bool fail(std::string* error, std::string msg) {
    if (error) *error = std::move(msg);
    return false;
}

char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

// Bounds-checked cursor over the in-memory directory tree.
struct Cursor {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;

    bool has(size_t n) const { return size - pos >= n; }
    bool u16(uint16_t& v) {
        if (!has(2)) return false;
        v = le16(data + pos);
        pos += 2;
        return true;
    }
    bool u32(uint32_t& v) {
        if (!has(4)) return false;
        v = le32(data + pos);
        pos += 4;
        return true;
    }
    bool cstr(std::string& s) {
        if (pos >= size) return false;
        const void* nul = std::memchr(data + pos, 0, size - pos);
        if (!nul) return false;
        const size_t len = size_t(static_cast<const uint8_t*>(nul) - (data + pos));
        s.assign(reinterpret_cast<const char*>(data + pos), len);
        pos += len + 1;
        return true;
    }
};

// The tree stores an empty directory or an empty extension as a single space.
std::string joinPath(const std::string& dir, const std::string& name, const std::string& ext) {
    std::string out;
    if (dir != " ") { out += dir; out += '/'; }
    out += name;
    if (ext != " ") { out += '.'; out += ext; }
    return out;
}

std::array<uint32_t, 256> makeCrcTable() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[i] = c;
    }
    return table;
}

bool endsWithDirVpk(const std::string& path) {
    static constexpr std::string_view kSuffix = "_dir.vpk";
    if (path.size() < kSuffix.size()) return false;
    const size_t base = path.size() - kSuffix.size();
    for (size_t i = 0; i < kSuffix.size(); ++i)
        if (lower(path[base + i]) != kSuffix[i]) return false;
    return true;
}

} // namespace

std::string normalizePath(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (char c : path) out += (c == '\\') ? '/' : lower(c);

    size_t start = 0;
    for (;;) {
        if (out.compare(start, 2, "./") == 0) start += 2;
        else if (start < out.size() && out[start] == '/') start += 1;
        else break;
    }
    return out.substr(start);
}

uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc) {
    static const auto table = makeCrcTable();
    crc = ~crc;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

bool Archive::open(const std::string& dirPath, std::string* error) {
    entries_.clear();
    preload_.clear();
    files_.clear();
    version_ = 0;
    dataBase_ = 0;

    if (!endsWithDirVpk(dirPath))
        return fail(error, "expected a *_dir.vpk file: " + dirPath);
    stem_ = dirPath.substr(0, dirPath.size() - 8);  // strip "_dir.vpk"

    FilePtr f(std::fopen(dirPath.c_str(), "rb"));
    if (!f) return fail(error, "cannot open " + dirPath);

    if (std::fseek(f.get(), 0, SEEK_END) != 0) return fail(error, "cannot seek " + dirPath);
    const long fileSize = std::ftell(f.get());
    std::rewind(f.get());

    // v1 header: signature, version, tree size.
    // v2 adds four section sizes we don't need for reading files.
    uint8_t hdr[28];
    if (std::fread(hdr, 1, 12, f.get()) != 12) return fail(error, "truncated header");
    if (le32(hdr) != kSignature) return fail(error, "not a VPK (bad signature)");
    const uint32_t version = le32(hdr + 4);
    const uint32_t treeSize = le32(hdr + 8);

    uint32_t headerSize = 12;
    if (version == 2) {
        if (std::fread(hdr + 12, 1, 16, f.get()) != 16) return fail(error, "truncated v2 header");
        headerSize = 28;
    } else if (version != 1) {
        return fail(error, "unsupported VPK version " + std::to_string(version));
    }

    if (treeSize == 0 || fileSize < 0 || uint64_t(headerSize) + treeSize > uint64_t(fileSize))
        return fail(error, "directory tree size doesn't fit in the file");

    std::vector<uint8_t> tree(treeSize);
    if (std::fread(tree.data(), 1, treeSize, f.get()) != treeSize)
        return fail(error, "truncated directory tree");

    // Tree layout: for each extension, for each directory, for each filename,
    // an 18-byte entry followed by its preload bytes. Each level ends with "".
    Cursor cur{tree.data(), tree.size()};
    std::string ext, dir, name;
    for (;;) {
        if (!cur.cstr(ext)) return fail(error, "truncated tree (extension)");
        if (ext.empty()) break;
        for (;;) {
            if (!cur.cstr(dir)) return fail(error, "truncated tree (directory)");
            if (dir.empty()) break;
            for (;;) {
                if (!cur.cstr(name)) return fail(error, "truncated tree (filename)");
                if (name.empty()) break;

                Entry e;
                uint16_t terminator = 0;
                if (!cur.u32(e.crc) || !cur.u16(e.preloadSize) || !cur.u16(e.archiveIndex) ||
                    !cur.u32(e.offset) || !cur.u32(e.length) || !cur.u16(terminator))
                    return fail(error, "truncated entry: " + joinPath(dir, name, ext));
                if (terminator != 0xFFFF)
                    return fail(error, "bad entry terminator: " + joinPath(dir, name, ext));
                if (!cur.has(e.preloadSize))
                    return fail(error, "truncated preload data: " + joinPath(dir, name, ext));

                e.preloadOffset = uint32_t(preload_.size());
                preload_.insert(preload_.end(), cur.data + cur.pos,
                                cur.data + cur.pos + e.preloadSize);
                cur.pos += e.preloadSize;

                entries_[normalizePath(joinPath(dir, name, ext))] = e;
            }
        }
    }

    version_ = version;
    dataBase_ = uint64_t(headerSize) + treeSize;
    files_[kDirArchive] = std::move(f);  // keep it open for embedded data
    return true;
}

const Entry* Archive::find(std::string_view path) const {
    auto it = entries_.find(normalizePath(path));
    return it == entries_.end() ? nullptr : &it->second;
}

std::FILE* Archive::archiveFile(uint16_t index, std::string* error) const {
    auto it = files_.find(index);
    if (it != files_.end()) return it->second.get();

    char suffix[16];
    std::snprintf(suffix, sizeof suffix, "_%03u.vpk", unsigned(index));
    const std::string path = stem_ + suffix;

    FilePtr f(std::fopen(path.c_str(), "rb"));
    if (!f) {
        fail(error, "cannot open " + path);
        return nullptr;
    }
    std::FILE* raw = f.get();
    files_.emplace(index, std::move(f));
    return raw;
}

bool Archive::read(std::string_view path, std::vector<uint8_t>& out,
                   bool verifyCrc, std::string* error) const {
    const Entry* e = find(path);
    if (!e) return fail(error, "not found: " + std::string(path));
    return read(*e, out, verifyCrc, error);
}

bool Archive::read(const Entry& e, std::vector<uint8_t>& out,
                   bool verifyCrc, std::string* error) const {
    out.resize(e.size());
    if (e.preloadSize)
        std::memcpy(out.data(), preload_.data() + e.preloadOffset, e.preloadSize);

    if (e.length) {
        std::FILE* f = archiveFile(e.archiveIndex, error);
        if (!f) return false;
        // Embedded data is addressed from the end of the tree; numbered
        // archives are addressed from the start of their own file.
        const uint64_t offset = (e.archiveIndex == kDirArchive ? dataBase_ : 0) + e.offset;
        // long is 64-bit on arm64 Android and x86_64 Linux, so fseek is fine here.
        if (std::fseek(f, long(offset), SEEK_SET) != 0 ||
            std::fread(out.data() + e.preloadSize, 1, e.length, f) != e.length)
            return fail(error, "short read from archive " + std::to_string(e.archiveIndex));
    }

    if (verifyCrc && crc32(out.data(), out.size()) != e.crc)
        return fail(error, "CRC mismatch");
    return true;
}

} // namespace vpk
