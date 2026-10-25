// Room geometry (type-2 segment of ST##.DAT) -> world-space quads, plus ground tiles. Coordinates
// are game units, Y down; arithmetic keeps the game's 16-bit truncation.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include "engine/data_source.hpp"
#include "engine/game_file.hpp"

namespace od {

struct RoomGrid {
    int x0, z0, x1, z1;
    size_t cellsOffset; // byte offset of the first 12-byte cell inside the block
    int width() const { return x1 - x0 + 1; }
    bool contains(int gx, int gz) const { return x0 <= gx && gx <= x1 && z0 <= gz && gz <= z1; }
};

struct RoomSegment {
    int index, flags, cellIdx, height, gx, gz;
};

// PS1 GPU quad split (diagonal 1-2), in perimeter winding. Triangles are stored with v3 == v1:
// the (1,3,2) half degenerates and (0,1,2) keeps the right UVs (e.g. ST08 stair lattice).
constexpr int kQuadTris[6] = {0, 1, 2, 1, 3, 2};

struct RoomQuad {
    std::array<std::array<int16_t, 3>, 4> verts; // PS1 quad: perimeter 0,1,3,2, tris kQuadTris
    std::array<std::array<uint8_t, 2>, 4> uvs;   // texel bytes
    uint32_t tex = 0;                            // (tpage << 16) | clut
    int segment = -1;                            // -1 = floor tile
    std::array<uint8_t, 4> shades{128, 128, 128, 128}; // gouraud, 0x80 = neutral (tex*c/128)
    bool doubleSided = false;                          // scenery: d0 bit29, else front face only
    // floor tiles: Maybe_RenderFloorTileFar 0x5f6c50 skips the winding test when the tile word has bit 0x800 (the
    // near variant 0x5f7270 always tests it); Maybe_DispatchGridCell 0x5f68d0 draws the near one while the view z of
    // lodPoint (the cell's centre at y 0x190, through the view matrix and the unshifted translation 0x6d003c) >> 2 is
    // below the room's first LOD distance
    bool farBothSides = false;
    std::array<int16_t, 3> lodPoint{};
    uint8_t otMode = 1; // scenery d0 >> 30: OT depth 0 (avg+max)/2, 1 max, 2 max-16, 3 avg (FUN_005fbba0)
    uint8_t semi = 0;   // scenery (d0 >> 24) & 3: 0 opaque, else PS1 blend mode + 1 (1 avg, 2 add, 3 sub)
    // switchable segments (parseRoom(..., switchable = true)): variant (segment flags & 3) and strip list of the
    // quad, whether its variant marks door lists / camera-facing lists; -1 = always drawn (see segmentQuadDrawn)
    int8_t variant = -1;
    uint8_t list = 0;
    bool doorList = false, cameraLists = false;
    // scenery: byte offset in the room block of the face's 8-byte UV pair (the address the room UV animation
    // 0x4183d0 rewrites, game/room_uv_animation RoomUvWrite); 0 = none (floor tiles)
    uint32_t uvOfs = 0;
};

struct RoomStats {
    int segments = 0, quads = 0, skippedFaces = 0, noStrip = 0, badHeader = 0, noGrid = 0,
        ambiguousGrid = 0; // ambiguousGrid: segments no grid references (placed by containment)
};

std::span<const uint8_t> roomBlock(const GameFile& stFile); // throws if absent (the span points into the file)
std::vector<RoomGrid> roomGrids(std::span<const uint8_t> block);
std::vector<RoomSegment> roomSegments(std::span<const uint8_t> block);

// segment index -> sub-grid whose cells reference it (cell word bit15 && !bit14, & 0x7ff).
// The game only lights segments referenced by the active sub-grid: each sub-grid is a sub-room.
std::map<int, int> segmentOwners(std::span<const uint8_t> block);

// sub < 0: every segment, anchored on its owner grid; sub >= 0: only that sub-room.
// lod: variant slot 0 near (most detailed) / 1 mid / 2 far; -1 = slot from the file's flags.
// switchable: segments whose look changes at run time (alternate variant allowed: door corridors / open
// doorways; door strip lists) get the quads of both variants (flags & 3 with bit 0 clear / set), tagged.
std::vector<RoomQuad> parseRoom(std::span<const uint8_t> block, int shift = 0, int sub = -1,
                                RoomStats* stats = nullptr, int lod = 0, bool switchable = false);
// Whether a quad of parseRoom is drawn for the segment flags byte (segment list entry +3, 0x6d008c): its variant
// is flags & 3; door lists: list 0 (closed door) hidden while bit 2 is set (the door scripts, FUN_00600f50),
// list 1 (open doorway) hidden otherwise (Maybe_RenderSegments 0x5fb050 -> 0x6d01a8 -> 0x5fbba0).
bool segmentQuadDrawn(const RoomQuad& q, uint8_t flags);

using FloorUvTable = std::array<std::array<uint16_t, 2>, 32>;
using FloorOrientTable = std::array<std::array<uint8_t, 4>, 16>;
// the stage's floor UV table / the floor quads' corner orders as the data source holds them (TableId::FloorUvs by
// stage, TableId::FloorOrientations)
struct FloorUvs : DataRecord {
    static constexpr uint32_t kBytes = 32 * 4;
    FloorUvTable uv{};
    static FloorUvs load(const TableReader& r) {
        FloorUvs t;
        for (size_t i = 0; i < t.uv.size(); ++i) t.uv[i] = {r.u16(i * 4), r.u16(i * 4 + 2)};
        return t;
    }
};
struct FloorOrientations : DataRecord {
    static constexpr uint32_t kBytes = 16 * 4;
    FloorOrientTable orient{};
    static FloorOrientations load(const TableReader& r) {
        FloorOrientations t;
        for (size_t i = 0; i < t.orient.size(); ++i)
            for (size_t k = 0; k < 4; ++k) t.orient[i][k] = r.u8(i * 4 + k);
        return t;
    }
};
constexpr uint16_t kFloorTpage = 0x1E; // constant set by FUN_005f5e60

std::vector<RoomQuad> parseFloor(std::span<const uint8_t> block, const FloorUvTable& uv,
                                 const FloorOrientTable& orient, int shift = 0, int sub = 0);

} // namespace od
