// bsp.h - loader for Source-format BSP maps (versions 19 and 20).
//
// Decodes records field by field from the publicly documented layout rather
// than casting raw memory to structs, so it is safe on any alignment/ABI.
// Currently covers what's needed to build world geometry; lightmaps,
// displacements, visibility and collision come next.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace bsp {

using Vec3 = std::array<float, 3>;

enum Lump : int {
    kEntities = 0,
    kPlanes = 1,
    kTexData = 2,
    kVertexes = 3,
    kTexInfo = 6,
    kFaces = 7,
    kEdges = 12,
    kSurfEdges = 13,
    kModels = 14,
    kTexDataStringData = 43,
    kTexDataStringTable = 44,
    kFacesHdr = 58,
    kLumpCount = 64,
};

// Texinfo surface flags used to filter out non-visible faces.
constexpr uint32_t kSurfSky2D = 0x2;
constexpr uint32_t kSurfSky = 0x4;
constexpr uint32_t kSurfTrigger = 0x40;
constexpr uint32_t kSurfNoDraw = 0x80;
constexpr uint32_t kSurfHint = 0x100;
constexpr uint32_t kSurfSkip = 0x200;

struct Plane {
    Vec3 normal;
    float dist;
    int32_t type;
};

struct Edge {
    uint16_t v[2];
};

struct Face {
    uint16_t plane;
    uint8_t side;             // 1 = face points opposite the plane normal
    int32_t firstEdge;        // index into surfEdges
    int16_t numEdges;
    int16_t texInfo;
    int16_t dispInfo;         // -1 unless this is a displacement surface
    uint8_t styles[4];
    int32_t lightOfs;         // byte offset into the lighting lump, -1 if none
    int32_t lightmapMins[2];  // in luxels
    int32_t lightmapSize[2];  // in luxels, minus one
};

struct TexInfo {
    float textureVecs[2][4];  // s/t projection: (xyz, offset)
    float lightmapVecs[2][4];
    uint32_t flags;
    int32_t texData;
};

struct TexData {
    Vec3 reflectivity;        // average color of the texture
    int32_t nameId;
    int32_t width, height;
};

struct Model {
    Vec3 mins, maxs, origin;
    int32_t headNode, firstFace, numFaces;
};

struct Map {
    int version = 0;
    bool hdrFacesOnly = false;  // faces came from the HDR faces lump
    std::string entities;
    std::vector<Plane> planes;
    std::vector<Vec3> vertices;
    std::vector<Edge> edges;
    std::vector<int32_t> surfEdges;
    std::vector<Face> faces;
    std::vector<TexInfo> texInfos;
    std::vector<TexData> texDatas;
    std::vector<std::string> texDataNames;  // material path for each texDatas[i]
    std::vector<Model> models;              // models[0] is the world
};

bool load(const uint8_t* data, size_t size, Map& out, std::string* error = nullptr);

struct SurfaceVertex {
    Vec3 pos;
    float u, v;  // texture coordinates, normalized by texture size
};

// One convex polygon, wound counter-clockwise when seen from its front side.
// Triangulate as a fan: (0, i, i + 1).
struct Surface {
    int faceIndex;
    int texData;  // -1 if the face has no valid texdata
    std::vector<SurfaceVertex> vertices;
};

struct SurfaceStats {
    size_t exported = 0, triangles = 0;
    size_t displacement = 0, toolOrSky = 0, invalid = 0;
};

// Visible polygons of one brush model (0 = world). Skips sky, tool and
// trigger faces, and displacements (not supported yet).
std::vector<Surface> buildSurfaces(const Map& map, int modelIndex = 0,
                                   SurfaceStats* stats = nullptr);

} // namespace bsp
