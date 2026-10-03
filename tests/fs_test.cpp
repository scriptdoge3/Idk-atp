// Checks mount order, VPK lookups, loose-file overrides and case-insensitivity.
#include "filesystem.h"

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: fs_test <gamedir>\n");
        return 2;
    }
    vfs::FileSystem fs;
    std::string notes;
    check(fs.mountGameDir(argv[1], &notes) && notes.empty(), "mount game dir");

    std::vector<uint8_t> buf;
    const std::string vmtStart = "\"LightmappedGeneric\"";
    check(fs.read("Materials\\Concrete\\Floor01.VMT", buf) &&
              std::string(buf.begin(), buf.end()).rfind(vmtStart, 0) == 0,
          "file from VPK via mixed case + backslashes");
    check(fs.read("credits.txt", buf) && std::string(buf.begin(), buf.end()) == "loose override\n",
          "loose file overrides the VPK copy");
    check(fs.exists("maps/room.bsp"), "mixed-case loose file (MAPS/Room.BSP) found");
    check(!fs.exists("nope.txt"), "missing file reported missing");
    return failures ? 1 : 0;
}
