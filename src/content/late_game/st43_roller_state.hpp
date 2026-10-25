// The class states of st43_roller.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st43_roller_state {

// The roller's own bytes: the damage taken toward its roll over (the step word), its timer (its first byte: the spawn's
// rise wait), its wobble and yaw drift, the shield's cooldown (the velocity's spare lane), its spawner, its travel yaw,
// its pause (wander) / target yaw (turn to the player), its hit box
struct RollerFields : ActorClassState {
    explicit RollerFields(StaticActor& actor) : damageTaken(actor.step, actor.stepCounter), timer(actor.params, 0), riseWait(actor.params[0]), wobble(actor.params[2]), yawDrift(actor.params[3]), shieldCooldown(actor.velocity.spare), travelYaw(actor.hitBox.previousX), pauseOrTarget(actor.hitBox.previousZ) {}
    BytesOfField<int16_t> damageTaken;
    BytesField<int16_t, 4> timer;
    uint8_t& riseWait;
    FieldAlias<int8_t, uint8_t> wobble;
    FieldAlias<int8_t, uint8_t> yawDrift;
    int16_t& shieldCooldown;
    int16_t& travelYaw;
    int16_t& pauseOrTarget;
    ActorRef<ControllerActor> spawner;
    HitBoxHead hitBox;
    void clearMoveBlockBytes() {
        hitBox = {};
        travelYaw = 0;
        pauseOrTarget = 0;
    }

    void clearBlock() override {
        spawner = {};
        hitBox = {};
    }
};

} // namespace od::port::st43_roller_state
