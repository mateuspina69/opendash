// The ST3B cast of script 0x5c: the class 0x448ae0 and the gate debris effect 0x43aa30,
// content/saul_kada/st3b_script5c_cast.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"

namespace od::port {

constexpr uint8_t kGateDebrisType = 0x1c; // the stage's effect type
// The debris' bytes (script 0x5c's step 0x536630 writes them): its spin (x / y), its angles, its velocity (1/16)
struct GateDebrisFields : ActorClassState {
    explicit GateDebrisFields(EffectActor& actor) : spinX(actor.params, 0), spinY(actor.params, 2) {}
    BytesField<uint16_t, 4> spinX;
    BytesField<uint16_t, 4> spinY;
    ShortVec4 angles;
    ShortVec3 velocity;
};

// exe 0x448ae0 (pool 0x20 ST3B type 0 sub 1; script 0x5c record 0x8ab634): the figure at the gate: its cue animations,
// hidden at the script's step sub 5, following the script's first actor
void gateFigureUpdate(const PlayerEnv& env, uint32_t actor);

// exe 0x43aa30 (pool 0xa0 ST3B stage type 0x1c; script 0x5c step 0x536630): a tumbling piece of the gate's debris (FT4)
void gateDebrisUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
