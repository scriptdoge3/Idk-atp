// bsp.h - loader for Source-format BSP maps (versions 19 and 20).
//
// Decodes records field by field from the publicly documented layout rather
// than casting raw memory to structs, so it is safe on any alignment/ABI.
// Covers world geometry (brush faces and displacements) and lightmaps;
// visibility and collision come next.
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
    kLighting = 8,
    kEdges = 12,
    kSurfEdges = 13,
    kModels = 14,
    kDispInfo = 26,
    kDispVerts = 33,
    kTexDataStringData = 43,
    kTexDataStringTable = 44,
    kLightingHdr = 53,
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

// Texinfo flag: each light style has four lightmaps instead of one (flat,
// then one per bump-map basis direction).
constexpr uint32_t kSurfBumpLight = 0x800;

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

// A displacement replaces one four-sided face with a grid of offset vertices.
struct DispInfo {
    Vec3 startPosition;     // the grid starts at the base face corner nearest this
    int32_t dispVertStart;  // index of this displacement's first DispVert
    int32_t power;          // grid is (2^power + 1)^2 vertices; 2, 3 or 4 in practice
    uint16_t mapFace;       // the face being replaced
};

struct DispVert {
    Vec3 vec;     // unit direction from the flat base position to the vertex
    float dist;   // distance along vec
    float alpha;  // blend between the material's two textures, 0-255
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
    std::vector<DispInfo> dispInfos;
    std::vector<DispVert> dispVerts;
    std::vector<uint8_t> lighting;          // lightmap samples; faces[i].lightOfs points in
};

bool load(const uint8_t* data, size_t size, Map& out, std::string* error = nullptr);

struct SurfaceVertex {
    Vec3 pos;
    float u, v;          // texture coordinates, normalized by texture size
    float lu = 0.0f;     // lightmap coordinates in luxels from the face's
    float lv = 0.0f;     //   lightmap corner; luxel (x, y) is at exactly (x, y)
    float alpha = 0.0f;  // displacements only: texture blend, 0-1
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
// trigger faces, and displacement faces (see buildDisplacements).
std::vector<Surface> buildSurfaces(const Map& map, int modelIndex = 0,
                                   SurfaceStats* stats = nullptr);

// One displacement as an indexed triangle mesh. The base face's corners are
// taken in stored order, starting from the one nearest startPosition, and
// vertex (row r, column c) of the n x n-quad grid sits at
//     lerp(lerp(c0, c1, r / n), lerp(c3, c2, r / n), c / n) + vec * dist
// Triangles are wound counter-clockwise seen from the face's front, and the
// grid's quads are split along alternating diagonals.
struct Displacement {
    int faceIndex;
    int texData;                          // -1 if the face has no valid texdata
    int power;                            // n = 2^power quads per side
    std::array<Vec3, 4> corners;          // c0..c3 of the flat base face
    std::vector<SurfaceVertex> vertices;  // (n + 1)^2, row by row
    std::vector<uint32_t> indices;        // three per triangle
};

// Every visible displacement, in dispinfo order. Skips the same sky and tool
// faces as buildSurfaces; stats->exported counts displacements.
std::vector<Displacement> buildDisplacements(const Map& map,
                                             SurfaceStats* stats = nullptr);

// One lightmap: (lightmapSize[0] + 1) x (lightmapSize[1] + 1) luxels, row by
// row, in linear RGB: (r, g, b) * 2^exponent / 255, so above 1 is overbright.
struct Lightmap {
    int width = 0, height = 0;
    std::vector<Vec3> luxels;
};

// How many light styles light the face (0-4); 0 means it has no lightmap.
int lightStyleCount(const Face& face);

// Decodes the lightmap of style slot `style` (an index into face.styles,
// not a style number). For kSurfBumpLight faces `bump` picks 0 = flat or
// 1-3 = a bump basis direction; other faces only have bump 0.
bool decodeLightmap(const Map& map, const Face& face, int style, int bump, Lightmap& out);

// Every lit face's first-style, flat lightmap packed into one page, so the
// renderer can upload a single texture.
struct LightmapAtlas {
    struct Rect {
        int x = 0, y = 0, width = 0, height = 0;  // width 0: the face is unlit
    };
    int width = 0, height = 0;
    std::vector<Vec3> luxels;  // width * height, row by row
    std::vector<Rect> faces;   // one per map.faces entry
};

LightmapAtlas buildLightmapAtlas(const Map& map);

// Normalized atlas coordinates of a vertex of face `faceIndex`; meaningless
// if the face is unlit (its rect has width 0).
std::array<float, 2> atlasCoords(const LightmapAtlas& atlas, int faceIndex,
                                 const SurfaceVertex& v);

} // namespace bsp
