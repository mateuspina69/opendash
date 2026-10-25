// The class states of nino_return_scripts.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/script_state.hpp"
#include "game/vectors.hpp"

namespace od::port::nino_return_scripts_state {

// a flying point (pool e0 type 0x1c, content/nino/glyde_attack): from its point to its target in `ticks`, its flag set
// at the end (kNoFlyFlag: none)
struct FlyPointFields : ActorClassState {
    explicit FlyPointFields(ControllerActor& actor) : ticks(actor.params, 0), flag(actor.params, 2) {}
    BytesField<int16_t, 4> ticks;
    BytesField<int16_t, 4> flag;
    ShortVec3 from;
    ShortVec3 to;
};

// the script's own bytes: the ripples' interval, the countdown to the next and the next one's size
struct AttackEndBytes : ScriptPart<AttackEndBytes> {
    int16_t rippleInterval = 0; // +0xc
    uint16_t rippleCountdown = 0; // +0x10
    int8_t rippleSize = 0; // +0x12
};

// the script's own byte: the cast started (the voice plays)
struct RollToMachineBytes : ScriptPart<RollToMachineBytes> {
    uint8_t castStarted = 0; // +0xc
};

} // namespace od::port::nino_return_scripts_state
