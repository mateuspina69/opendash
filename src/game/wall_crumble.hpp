// The crumbling of a broken wall (pool 0xe0 type 9 of the global controller table, exe 0x419a20; spawned by the
// breakable walls' crumble reaction, game/breakable_walls): variants 0 / 1 crumble the wall -- its debris, blasts, then
// 16 ticks of a grid of falling textured quads (draw commands) over the wall's face --, variants 2..5 only set off
// blasts (1, 2 along x, 2 along z, 4 in a square).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"

namespace od::port {

constexpr uint8_t kWallCrumbleType = 9;

// The crumble's fields (the controller slot past its header; written by the breakable walls' reaction)
struct WallCrumbleFields : ActorClassState {
    explicit WallCrumbleFields(ControllerActor& actor) : height(actor.params, 0), width(actor.params, 2) {}
    BytesField<int16_t, 4> height; // the wall's height / width (the segment's sizes << its size shift)
    BytesField<int16_t, 4> width;
    ShortVec3 point; // the wall's point (its anchor)
    uint32_t segment = 0; // the broken segment's entry (the reaction's) ...
    uint32_t poolEntry = 0; // the segment's pool entry
    PartField<uint8_t, uint32_t> side{segment, 0}; // ... whose low byte variant 1 replaces by the side it crumbles on (bit 0:
};

// The falling quads of a crumble (also ST13's wall-break burst, content/manda/st13_wall_break): the texture cell of
// the controller's tick (its state counts the ticks; while its sub-state is 0 the cells of a 4 x 2 sheet by tick, the
// sub-state set after tick 2, then two cells alternating with the tick's parity: the exe's globals 0x6d0160..0x6d0167),
// the quads' grey of tick t (((0x10 - t) >> 1) * 10 in the three colour bytes) and one quad (a billboard of that cell,
// CLUT 0x7c91 / tpage 0x2e) at (x, y, z) with the size / rotation word
void wallCrumbleSelectCell(ControllerActor& crumble);
uint32_t wallCrumbleGrey(uint32_t tick);
void wallCrumbleQuad(const ControllerActor& crumble, uint32_t grey, int16_t x, int16_t y, int16_t z, uint32_t sizeWord);

// exe 0x419a20 (pool 0xe0 type 9): the crumble by its variant
void wallCrumbleUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
