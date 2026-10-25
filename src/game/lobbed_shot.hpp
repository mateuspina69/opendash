// The lobbed shot (pool 0x40 class 0x5c4a90, stage projectile type 7 of many stages: ST0F, ST14, ST1F, ST25, ST28..ST2A,
// ST2D..ST2F, ST30, ST34, ST3B, ST41 / ST42, ST4D / ST4E, ST50): a bomb / a shell that flies under gravity with a
// capsule hit entry between its last two places; a wall, the floor or a hit ends it in a spark -- or, variant | 0x80,
// in a blast (game/explosions) of its blast size. Its sprite is a 0x2c draw command.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

struct PlayerEnv;

// The shot's spawn parameters (its spawners write them): its damage, the sound of its launch (< 0: none), its size (the
// rotation's spare lane), the blast's size of a variant | 0x80 (the velocity's spare lane)
struct LobbedShotParams {
    explicit LobbedShotParams(const ProjectileActor& actor) : LobbedShotParams(const_cast<ProjectileActor&>(actor)) {}
    explicit LobbedShotParams(ProjectileActor& actor) : damage(actor.params, 0), launchSound(actor.params, 2), size(actor.rotation.spare), blastSize(actor.velocity.spare) {}
    BytesField<int16_t, 4> damage;
    BytesField<int16_t, 4> launchSound;
    int16_t& size;
    FieldAlias<uint16_t, int16_t> blastSize;
};
constexpr uint8_t kLobbedShotStageType = 7;
constexpr uint8_t kLobbedShotBlasts = 0x80; // variant bit: it ends in a blast

// exe 0x5c4a90: the lobbed shot's update (ActorUpdateFn)
void lobbedShotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
