// The projection's registers: the exe's emulation of the PS1 geometry unit (exe 0x40c3c0.. : the camera matrix and
// translation loaded, the point, its view-space coordinates, the clip flags) as the routines that project a point leave
// them. Game code reads some back (the on-screen tests), so the projections keep them.
#pragma once

#include <array>
#include <cstdint>

#include "game/camera_state.hpp"
#include "game/field_alias.hpp"
#include "game/geometry_scratch.hpp"
#include "game/state_array.hpp"
#include "game/view_transform.hpp"

namespace od::port {

struct ProjectionState {
    TransformMembers cameraMembers;      // the loaded camera matrix (+ padding) and translation (exe 0x40c3c0 / 0x40c410)
    ViewTransform camera = cameraMembers.lanes();
    ShortVec4 point;                     // the point being projected (s16; exe 0x40c6a0 loads its 4th lane too)
    StateArray<int32_t, 3> viewPoint{};  // its view-space coordinates
    int32_t nearDepth = 0;               // the near plane: view z at or past it projects as is
    uint32_t clipFlags = 0;              // kProjectionClipped when the point is behind the near plane
    int32_t focal = 0;                   // the projection distance (screen x / y = focal * view x / y / view z)
    int32_t centreX = 0;                 // the screen centre
    int32_t centreY = 0;
};
inline ProjectionState& projection() {
    static ProjectionState s;
    return s;
}
constexpr uint32_t kProjectionClipped = 8;

// A quad's four texels (lanes of the geometry scratch): corner c's u / v bytes at 2c / 2c + 1, written byte by byte, a
// corner (u | v << 8) at a time or as two words, read as two words
struct QuadTexels {
    explicit QuadTexels(uint32_t at)
        : uvBytes{Lane8(lane(at, 0), 0), Lane8(lane(at, 0), 1), Lane8(lane(at, 2), 0), Lane8(lane(at, 2), 1),
                  Lane8(lane(at, 4), 0), Lane8(lane(at, 4), 1), Lane8(lane(at, 6), 0), Lane8(lane(at, 6), 1)},
          words_{geometryScratch().word(at), geometryScratch().word(at + 4)},
          corners_{lane(at, 0), lane(at, 2), lane(at, 4), lane(at, 6)} {}
    std::array<Lane8, 8> uvBytes;
    uint32_t word(size_t i) const { return uint32_t(words_[i].get()); }
    void setWord(size_t i, uint32_t v) { words_[i] = int32_t(v); }
    void setCorner(size_t c, uint16_t uv) { corners_[c] = int16_t(uv); }

private:
    static Lane16 lane(uint32_t at, uint32_t k) { return geometryScratch().lane(at + k); }
    std::array<Lane32, 2> words_;
    std::array<Lane16, 4> corners_;
};
// Two corner texels (u | v << 8 each) stored as one word, read a corner at a time
struct TexelPair {
    explicit TexelPair(uint32_t at) : corners{geometryScratch().lane(at), geometryScratch().lane(at + 2)} {}
    std::array<Lane16, 2> corners;
    void set(uint32_t pair) {
        corners[0] = int16_t(uint16_t(pair));
        corners[1] = int16_t(uint16_t(pair >> 16));
    }
};
// Four corner indices (0..3) stored as one word, read one at a time
struct CornerOrder {
    explicit CornerOrder(uint32_t at)
        : corners{Lane8(geometryScratch().lane(at), 0), Lane8(geometryScratch().lane(at), 1),
                  Lane8(geometryScratch().lane(at + 2), 0), Lane8(geometryScratch().lane(at + 2), 1)} {}
    std::array<Lane8, 4> corners;
    void set(uint32_t order) {
        for (size_t i = 0; i < 4; ++i) corners[i] = uint8_t(order >> (8 * i));
    }
};

// The transforms' and projections' share of the geometry scratch (game/geometry_scratch, over the room collision
// queries' state): a transform (the camera's rotation while it is built, an encounter formation's turn), a point and
// its projected screen point; the same lanes as a projected quad's corners, a view point stored and the corners'
// screen points. Each user leaves whatever it wrote. The bytes past the scratch's shared ones are the users' own: a
// transformed / normalized vector, the quad's texels, order depth and texture words, the room texture animation's
// frame texels and corner order.
struct ProjectionScratch {
    ViewTransform transform = geometryScratch().transform(0x00);
    std::array<Lane32, 8> transformWords = words(0x00); // the transform as the exe copies it (8 dwords)
    LaneVec4 point = geometryScratch().vec4(0x60);
    Lane16 pointYaw = geometryScratch().lane(0x66); // the point's 4th lane: a facing carried with it
    Lane16 screenX = geometryScratch().lane(0x68);  // the point projected
    Lane16 screenY = geometryScratch().lane(0x6a);
    std::array<LaneVec4, 2> pointPair{geometryScratch().vec4(0x60), geometryScratch().vec4(0x68)}; // two points side by side
    LaneVec3 rotatedPoint = geometryScratch().vec3(0x68); // a point turned by the transform (exe 0x40bb30's output)

    // the same lanes as a projected quad's: the corners loaded for the geometry unit (exe 0x40c770: corners 0..2,
    // 0x40c6a0: corner 3; the first is also a single point loaded alone), a view point stored (exe 0x40caa0), the
    // corners' screen points (x | y << 16)
    std::array<LaneVec4, 4> quadCorners{geometryScratch().vec4(0x00), geometryScratch().vec4(0x08),
                                        geometryScratch().vec4(0x10), geometryScratch().vec4(0x18)};
    std::array<Lane32, 3> storedViewPoint{geometryScratch().word(0x20), geometryScratch().word(0x24),
                                          geometryScratch().word(0x28)};
    std::array<Lane32, 4> quadScreenPoints{geometryScratch().word(0x50), geometryScratch().word(0x54),
                                           geometryScratch().word(0x58), geometryScratch().word(0x5c)};

    // the lanes the users keep for themselves (each reads what it wrote): a transformed vector and the dword after it
    // (the exe's four-dword copies of a vector), a vector scaled to length 4096 (exe 0x40c320's output), the quad's
    // texels, order depth and texture words, the room texture animation's frame texels and corner order
    std::array<Lane32, 3> transformed{geometryScratch().word(0xa0), geometryScratch().word(0xa4),
                                      geometryScratch().word(0xa8)};
    Lane32 transformedPad = geometryScratch().word(0xac);
    std::array<Lane32, 3> normalized{geometryScratch().word(0xb0), geometryScratch().word(0xb4),
                                     geometryScratch().word(0xb8)};
    QuadTexels quadTexels{0xa0};
    Lane32 quadDepth = geometryScratch().word(0xb4);
    Lane32 quadTexture = geometryScratch().word(0xb8);
    TexelPair frameTexels{0xa8};
    CornerOrder frameCorners{0xc0};

private:
    static std::array<Lane32, 8> words(uint32_t at) {
        GeometryScratch& g = geometryScratch();
        return {g.word(at), g.word(at + 4), g.word(at + 8), g.word(at + 0xc), g.word(at + 0x10), g.word(at + 0x14),
                g.word(at + 0x18), g.word(at + 0x1c)};
    }
};
inline ProjectionScratch& projectionScratch() {
    static ProjectionScratch s;
    return s;
}

} // namespace od::port
