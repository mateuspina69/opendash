// The two shots of the ST2F / ST58 blob boss (content/subruins/blob_boss): the falling glowing shot of its ring
// attack and the lobbed blob of its lobs / its spray, and the spawn parameters the boss writes for them.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

struct PlayerEnv;

// the per-stage projectile types (pool 0x40, the stage's class table)
constexpr uint8_t kFallingShotType = 0xd;
constexpr uint8_t kLobbedBlobType = 0x1d;

// The falling shot's spawn parameter: big (the 0x28 billboard, the larger hit entry) or small; the boss writes the
// parameter word (1: big)
struct FallingShotParams {
    explicit FallingShotParams(const ProjectileActor& actor) : FallingShotParams(const_cast<ProjectileActor&>(actor)) {}
    explicit FallingShotParams(ProjectileActor& actor) : big(actor.params[0]) {}
    uint8_t& big;
};
// The lobbed blob's spawn parameters: its hit entry's size (it grows while the blob flattens) and its attack
struct LobbedBlobParams {
    explicit LobbedBlobParams(const ProjectileActor& actor) : LobbedBlobParams(const_cast<ProjectileActor&>(actor)) {}
    explicit LobbedBlobParams(ProjectileActor& actor) : hitSize(actor.params, 0), attack(actor.params, 2) {}
    BytesField<uint16_t, 4> hitSize;
    BytesField<uint16_t, 4> attack;
};

// exe 0x5c5d20 (pool 0x40 stage type 0xd): the falling shot -- the 0x28 billboard under gravity for 0x64 ticks with its
// hit entry; a hit, a wall or the timeout end it in a spark (the same routines as states 5..7 of the ice ruin's
// charged shot 0x5c5980, content/yosyonke/ice_ruin_burrowing_shooter)
void fallingShotUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5caa00 (pool 0x40 stage type 0x1d): the lobbed blob (model variant 0) -- ballistic; it lands (sound 0x25d),
// then flattens and sinks for 0x40 ticks with its hit entry growing
void lobbedBlobUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
