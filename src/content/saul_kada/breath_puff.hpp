// The spawn parameters of the stages' breath puff (pool 0x40 stage type 5, exe 0x5c4820: content/late_game/st45_boss)
// as the fire ruin's spawners write them (content/saul_kada/st28_gunner_boss, st34_flyer_shell).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kBreathPuffType = 5;
// its damage, its alternating frame, its life; its spin (the velocity's spare lane), its size (the rotation's spare
// lane)
struct BreathPuffParams {
    explicit BreathPuffParams(const ProjectileActor& actor) : BreathPuffParams(const_cast<ProjectileActor&>(actor)) {}
    explicit BreathPuffParams(ProjectileActor& actor) : damage(actor.params, 0), frame(actor.params[2]), life(actor.params[3]), spin(actor.velocity.spare), size(actor.rotation.spare) {}
    BytesField<uint16_t, 4> damage;
    uint8_t& frame;
    uint8_t& life;
    int16_t& spin;
    int16_t& size;
};

} // namespace od::port
