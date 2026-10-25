// The class states of st22_fire_shots.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st22_fire_shots_state {

// =====================================================================================================================
// ---- exe 0x5c2a60: pool 0x40 stage type 1 (ST22): fire projectiles. States [0x8d0f5c]: 0 0x5c2aa0 init (box 0x8d0f10 +
// variant * 12, the frame 5, the hit box's previous x / z), 1 0x5c2af0 -> by the variant [0x8d0f68], 2 0x5c32b0 the end
// by the variant. Variants: 0 0x5c2b10 a bouncing fireball (3 bounces off walls, heights 0x8d0f7c, hit 0x604008), 1
// 0x5c2c40 a homing lob at the player (a loop sound, flame capsule 0x60, 3 billboards, a stage type-1 flame effect under
// it each tick), 2 0x5c2ea0 an ember (random arc, 2 billboards), 3 0x5c3020 a rising bomb (a static stage type 3
// variant 3 smoke emitter riding it, hit 0xc04020), 4 0x5c3170 a shard (random spread, hit 0xc04008). Ends: 0 / 4 a hit
// spark (pool 0xa0 global type 0, 0x55d210); 1 sound 0x8b + 8 embers (variant 2) with sparks; 3 a spark + 16 shards
// (variant 4) at its angles.
struct FireShotFields : ActorClassState {
    explicit FireShotFields(ProjectileActor& actor) : phase(actor.params[0]), unreadD(actor.params[1]), hitBox(actor.hitBox) {}
    FieldAlias<int8_t, uint8_t> phase; // the fireball's bounces / the lob's flicker phase / the ember's size
    uint8_t& unreadD; // cleared by the ember (its reader is not traced)
    HitBoxHead& hitBox;
    int8_t frame = 0; // the fireball's billboard frame (5..0)
    uint8_t loopSound = 0; // the lob's loop sound handle
    void keepHitPoint(ProjectileActor& actor) {
        hitBox.result = 0;
        hitBox.previousX = actor.pos.x.whole;
        hitBox.previousZ = actor.pos.z.whole;
    }
};

} // namespace od::port::st22_fire_shots_state
