#include "filesystem.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace vfs {
namespace stdfs = std::filesystem;
namespace {

bool fail(std::string* error, std::string msg) {
    if (error) *error = std::move(msg);
    return false;
}

bool endsWith(const std::string& s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    bool ok = std::fseek(f, 0, SEEK_END) == 0;
    const long size = ok ? std::ftell(f) : -1;
    ok = ok && size >= 0 && std::fseek(f, 0, SEEK_SET) == 0;
    if (ok) {
        out.resize(size_t(size));
        ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    }
    std::fclose(f);
    return ok;
}

bool FileSystem::mountDir(const std::string& dir, std::string* error) {
    std::error_code ec;
    if (!stdfs::is_directory(dir, ec)) return fail(error, "not a directory: " + dir);

    Mount m;
    m.label = dir;
    const std::string base = stdfs::path(dir).generic_string();
    const auto opts = stdfs::directory_options::skip_permission_denied;

    for (auto it = stdfs::recursive_directory_iterator(dir, opts, ec);
         !ec && it != stdfs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code fileEc;
        if (!it->is_regular_file(fileEc)) continue;
        // Iterator paths are always "<dir>/<relative>", so slice off the base.
        std::string key = vpk::normalizePath(it->path().generic_string().substr(base.size()));
        if (endsWith(key, ".vpk")) continue;  // archives are mounted separately
        m.loose.emplace(std::move(key), it->path().string());
    }
    if (ec) return fail(error, "error scanning " + dir + ": " + ec.message());

    mounts_.push_back(std::move(m));
    return true;
}

bool FileSystem::mountVpk(const std::string& dirVpkPath, std::string* error) {
    auto archive = std::make_unique<vpk::Archive>();
    if (!archive->open(dirVpkPath, error)) return false;
    Mount m;
    m.label = dirVpkPath;
    m.vpk = std::move(archive);
    mounts_.push_back(std::move(m));
    return true;
}

bool FileSystem::mountGameDir(const std::string& dir, std::string* warnings) {
    std::string err;
    if (!mountDir(dir, &err)) return fail(warnings, err);

    std::vector<std::string> vpks;
    std::error_code ec;
    for (const auto& entry : stdfs::directory_iterator(dir, ec)) {
        std::error_code fileEc;
        if (!entry.is_regular_file(fileEc)) continue;
        if (endsWith(vpk::normalizePath(entry.path().filename().string()), "_dir.vpk"))
            vpks.push_back(entry.path().string());
    }
    std::sort(vpks.begin(), vpks.end());

    std::string notes;
    for (const std::string& path : vpks)
        if (!mountVpk(path, &err)) notes += "skipped " + path + ": " + err + "\n";
    if (warnings) *warnings = notes;
    return true;
}

bool FileSystem::exists(std::string_view path) const {
    const std::string key = vpk::normalizePath(path);
    for (const Mount& m : mounts_) {
        if (m.vpk ? m.vpk->find(key) != nullptr : m.loose.count(key) != 0) return true;
    }
    return false;
}

bool FileSystem::read(std::string_view path, std::vector<uint8_t>& out,
                      std::string* error) const {
    const std::string key = vpk::normalizePath(path);
    for (const Mount& m : mounts_) {
        if (m.vpk) {
            if (const vpk::Entry* e = m.vpk->find(key)) return m.vpk->read(*e, out, false, error);
        } else if (auto it = m.loose.find(key); it != m.loose.end()) {
            return readFile(it->second, out) || fail(error, "cannot read " + it->second);
        }
    }
    return fail(error, "not found: " + std::string(path));
}

} // namespace vfs
