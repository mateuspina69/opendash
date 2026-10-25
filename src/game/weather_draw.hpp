// Weather overlays as the game draws them (presentation only: reads the game state, never writes it).
//
// besides the room / sky / HUD only 99 textured gouraud quads of 0x431c96 remain: the blizzard layer.
//  * effect 0x431af0 (pool 0xa0 type 9 of ST02 / 03 / 10 / 11 / 1D / 49; ST1D: record 0x8c80ac spawned by the room
//    handlers 0x5804b0 / 0x5805f0 of stories 0 / 1 only): after scrolling its x / y by the speed record (logic,
//    game/blizzard_layer), while drawn and the frame draws, 9 rows x 11 columns of 32 x 32 POLY_GT4 (cmd 0x3e:
//    additive, tpage 0x2e, CLUT 0x7c92) over the screen, linked at its OT slot (ST1D: 1, in front of the scene).
//    Scroll: X = (x >> 4) + the camera yaw, Y = (y >> 4) + the camera's pitch angle; the quads start at -(X & 0x1f) /
//    -(Y & 0x1f); the cell of the 128 x 128 texture (u 0..0x7f, v 0x80..0xff) follows (X + col * 32) & 0x60 / (Y + row
//    * 32) & 0x60 (mirrored by the variant's bits 0 / 1); grey brightness per vertex = rec[0] + (rec[1] * sin(phase))
//    >> 12 with the exe's 4096-step table, phase = X + Y + 0x20 + (row + col) * 0x20 (+-0x20 at the left / right
//    corners).
//  * exe 0x591640 (static pool 0x60 type 2, global or per-stage: the snow of the towns, e.g. seven in ST09): its move
//    loop 0x5916d0 calls exe 0x591c10 with every live particle's wrapped s16 {x, y, z}: z >= 100 -> a POLY_FT4 (cmd
//    0x2e: additive, tpage 0x2f, CLUT 0x7c50, uv 0x20..0x3f square) at (x + 0xa0, y - 0x14), side ((0x400 - z) >> 8) + 1
//    pixels, grey (z >> 3) + 0x40, OT slot z >> 1 (the depth OT). The packed slots +0x2c lose the sign of a wrapped y,
//    so the logic port keeps the call inputs (SnowDrawBlock below).
// Verified packet word for word against the oracle: tests/test_weather_draw.cpp.
#pragma once

#include <cstdint>
#include <vector>

#include "engine/routine_ids.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_state.hpp"
#include "game/effects_draw.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port {

// The blizzard layer's fields (effect 0x431af0; its logic: game/blizzard_layer): its OT slot, its scroll (x / y,
// 1/16 pixels) and its speed record (exe data: {u16 base grey, u16 the grey's swing, the scroll's speeds x / y})
struct BlizzardSpeed : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    uint16_t grey = 0; // +0x0
    uint16_t swing = 0; // +0x2
    int16_t scrollX = 0; // +0x4
    int16_t scrollY = 0; // +0x6
    template <class R> static BlizzardSpeed load(const R& r) {
        BlizzardSpeed v;
        v.grey = r.template read<uint16_t>(0x0);
        v.swing = r.template read<uint16_t>(0x2);
        v.scrollX = r.template read<int16_t>(0x4);
        v.scrollY = r.template read<int16_t>(0x6);
        return v;
    }
};
struct BlizzardLayerFields : ActorClassState {
    explicit BlizzardLayerFields(EffectActor& actor) : otSlot(actor.params, 0), scrollX(actor.pos.x), scrollY(actor.pos.z) {}
    BytesField<uint16_t, 4> otSlot;
    int16_t& scrollX;
    int16_t& scrollY;
    DataPtr<BlizzardSpeed> speed;
};
// the layer's variant bits: its texture mirrored in u / v
constexpr uint8_t kBlizzardMirrorU = 0x01, kBlizzardMirrorV = 0x02;

// The snow's draw block: the input of every exe 0x591c10 call of the last tick, per snow static (written by the snow
// logic, game/snow, for the draw below; render input, not game state): kSnowParticles s16 points {x, y, z} and the
// particles drawn (the mask before the sub-state step adds / drops one)
constexpr uint32_t kSnowParticles = 32;
struct SnowDrawBlock {
    StateArray<ShortVec3, kSnowParticles> particles{};
    uint32_t drawn = 0;
};
// the draw block of the snow static of pool-0x60 slot index i
inline SnowDrawBlock& snowDrawBlock(uint32_t staticSlotIndex) {
    static StateArray<SnowDrawBlock, 64> blocks{};
    return blocks[staticSlotIndex];
}

} // namespace od::port

namespace od::weather {

// One textured quad of the PS1 frame (320 x 240, packet coordinates), PS1 vertex order (triangles 0 1 2 / 1 3 2).
struct Quad {
    int16_t x[4]{}, y[4]{};
    uint8_t u[4]{}, v[4]{};  // texel coordinates as the packet holds them
    uint8_t grey[4]{};       // vertex colour (R = G = B), 0x80 = neutral
    uint16_t tpage = 0, clut = 0;
    uint16_t ot = 0;         // the OT slot the game links it at
    RoutineId source = RoutineId::None; // the drawing routine (BlizzardLayerUpdate / SnowParticleDraw)
    bool original = true;    // false: added beyond the 320-pixel frame for wider windows (not drawn by the game)
};

// Every weather quad of the post-tick state (the game state). `extra` = PS1 pixels to cover left and right of the
// 320-pixel frame (wider windows): the blizzard grid continues with its own pattern, the snow repeats its 360-pixel
// box.
void collect(std::vector<Quad>& out, int extra = 0);

// The texture key (od::fx::TextureCache) of a quad: tpage / CLUT with the neutral tint.
inline int texture(fx::TextureCache& tex, const Quad& q) { return tex.index(q.tpage, q.clut); }

} // namespace od::weather
