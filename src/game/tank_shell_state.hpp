// The class states of tank_shell.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/tank_shell.hpp"
#include "game/field_alias.hpp"

namespace od::port::tank_shell_state {

struct TankShellFields : ActorClassState, TankShellParams {
    explicit TankShellFields(ProjectileActor& actor) : TankShellParams(actor), hitBox(actor.hitBox) {}
    HitBoxHead& hitBox;
};

} // namespace od::port::tank_shell_state
