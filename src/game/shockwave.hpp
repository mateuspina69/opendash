// The ground shockwave and its ring (stages ST0F, ST12 / ST14, ST17, ST25, ST2B, ST3B, ST41..ST43, ST4B / ST4C, ST51,
// ST5C): the shock (pool 0x40 class 0x5c3ae0, stage projectile type 3) -- a ring that grows from its centre with a
// hit entry on its edge toward the player (or a disc), its parameters by variant from the exe table 0x8d0fd8; variant |
// 0x80 a sequencer spawning ring variants with delays (0x8d1148) -- and the ring's visual (pool 0xa0 class 0x42ec70,
// stage effect type 3: 16 gouraud quads around the centre, one RNG step each, freed with its shock).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

struct PlayerEnv;

// The shock's spawn parameters (its spawners write them): the hit entry it makes (kShockHitEdge / kShockHitDisc) and the
// ring's wall hanging down
struct ShockwaveParams {
    explicit ShockwaveParams(const ProjectileActor& actor) : ShockwaveParams(const_cast<ProjectileActor&>(actor)) {}
    explicit ShockwaveParams(ProjectileActor& actor) : hitMode(actor.params[0]), downward(actor.params[1]) {}
    FieldAlias<int8_t, uint8_t> hitMode;
    uint8_t& downward;
};
constexpr int8_t kShockHitEdge = 1; // a hit entry on the ring's edge toward the player (not while it is airborne)
constexpr int8_t kShockHitDisc = 2; // a disc hit entry

constexpr uint8_t kShockwaveStageType = 3;     // the shock's stage projectile type ...
constexpr uint8_t kShockwaveSequencer = 0x80;  // ... its sequencer variant bit
constexpr uint8_t kShockRingFxStageType = 3;   // the ring visual's stage effect type

// exe 0x5c3ae0: the shock's update (ActorUpdateFn)
void shockwaveUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x42ec70: the ring visual's update (ActorUpdateFn)
void shockRingUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
