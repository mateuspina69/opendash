// The rising puff of smoke (ST48 pool 0x40 type 0xb, exe 0x5c5650), content/saul_kada/smoke_puff.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kSmokePuffType = 0xb; // the stage's pool-0x40 type (ST48)
// The puff's spawn parameter (its spawners write it): its life (ticks)
struct SmokePuffParams {
    explicit SmokePuffParams(const ProjectileActor& actor) : SmokePuffParams(const_cast<ProjectileActor&>(actor)) {}
    explicit SmokePuffParams(ProjectileActor& actor) : life(actor.params, 0) {}
    BytesField<uint16_t, 4> life;
};

// exe 0x5c5650 (ST48 pool 0x40 type 0xb): a rising puff of smoke (two fading billboards + a hit-list entry)
void smokePuffUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
