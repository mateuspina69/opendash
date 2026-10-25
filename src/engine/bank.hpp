// Room model bank: the type-1 segment of ST##.DAT (loaded to 0x76c800 / 0x754800).
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "engine/game_file.hpp"

namespace od {

struct BankEntry {
    int index;
    uint8_t cls, id, variant; // cls high nibble = actor class (0x20 animated, 0x60 static)
    uint32_t modelOfs;        // model header, relative to the bank
};

struct ModelFace {
    int count;                                  // 3 or 4 (PS1 quad order 0,1,2,3)
    std::array<uint8_t, 4> idx;
    std::array<std::array<uint8_t, 2>, 4> uvs;  // texel bytes
    uint8_t material;                           // index into the model's texture table
    // Maybe_BuildModelDrawCommands 0x5f3820: triangles always pass the GTE NCLIP test (FUN_0040cac0, drawn when
    // >= 0); quads skip it when index word bit 0x40000000 is set (both sides, OT depth wrapped)
    bool doubleSided = false;
    // FUN_005f4980: index word bit 31 -- the corners take the part colour 0x6d0100, never the vertex colours
    bool flatColour = false;
    // FUN_005f4980: index word bit 30 (triangles and quads) -- the face's OTZ (mean SZ / 4, FUN_0040d670) is pulled
    // 1 << stage shift (0x6d00b5) slots nearer (when at least that), so it draws over the faces at its depth: the
    // decals on an actor's model (Yuna's bracelet on her arm)
    bool otNearer = false;
};

struct ModelPart {
    std::vector<std::array<int16_t, 3>> verts;  // model space, game units (Y down)
    std::vector<ModelFace> faces;
};

struct BankModel {
    std::vector<ModelPart> parts;
    std::vector<uint32_t> textures;             // (clut << 16) | tpage, table ends at 0x1
};

// (the spans below point into the GameFile: it outlives them)
std::span<const uint8_t> modelBank(const GameFile& stFile); // throws if absent
// the stage file holds a type-1 (model bank) segment (ST57, the staff roll's stage, has none)
bool hasModelBank(const GameFile& stFile);
// The whole type-1 segment as FUN_005f3200 walks it after the stage load: leading records (header bit 31: the
// player's per-stage animation table -> 0xa71fb8 = player +0x298; bit 30: 0xa71fbc = record + 4), then the bank
struct BankSegment {
    std::span<const uint8_t> seg;
    size_t bankOffset = 0;
    long playerAnims = -1; // offset of the bit-31 record (or -1)
    long extra = -1;       // offset of the bit-30 record (or -1)
};
BankSegment modelBankSegment(const GameFile& stFile); // throws if absent
std::vector<BankEntry> bankEntries(std::span<const uint8_t> bank);
// Parts at a LOD level (0 = near). Static part headers are 24 bytes, animated (class 0x20) 16.
BankModel decodeModel(std::span<const uint8_t> bank, const BankEntry& e, int lod = 0);
// One submesh (model.decode_submesh): header {packed counts/scale, tri, quad, vertex offsets} at `header`.
ModelPart decodeSubmesh(std::span<const uint8_t> blob, size_t header);

} // namespace od
