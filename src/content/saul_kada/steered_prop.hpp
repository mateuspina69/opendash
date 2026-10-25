// The steered prop (static ST2F / ST58 type 0x39, exe 0x5af6d0): a turret-like prop its owner steers (the blob boss,
// content/subruins/blob_boss, drives one). See content/saul_kada/steered_prop.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

// The prop's parameters (its owner sets them): its colour (RGB555 components) and a white flash for one tick
struct SteeredPropParams {
    explicit SteeredPropParams(const StaticActor& actor) : SteeredPropParams(const_cast<StaticActor&>(actor)) {}
    explicit SteeredPropParams(StaticActor& actor) : red(actor.params[0]), green(actor.params[1]), blue(actor.params[2]), flash(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> red;
    FieldAlias<int8_t, uint8_t> green;
    FieldAlias<int8_t, uint8_t> blue;
    uint8_t& flash;
};
// Its targets (its owner sets them): the yaw / pitch it turns toward (0x40 a tick)
struct SteeredPropFields : ActorClassState {
    explicit SteeredPropFields(StaticActor&) {}
    uint16_t targetYaw = 0;
    uint16_t targetPitch = 0;

    void clearBlock() override {
        targetYaw = 0;
        targetPitch = 0;
    }
};

// exe 0x5af6d0 (static ST2F / ST58 type 0x39): a prop steered toward its target angles, coloured by its parameters
void steeredPropUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
