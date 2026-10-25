// The class states of base_alarm.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::base_alarm_state {

// the alarm's fields
struct AlarmFields : ActorClassState {
    explicit AlarmFields(ControllerActor&) {}
    uint16_t phase = 0; // the flash's phase (+0x80 a tick)
    uint16_t sirenTicks = 0; // the siren's countdown
};

} // namespace od::port::base_alarm_state
