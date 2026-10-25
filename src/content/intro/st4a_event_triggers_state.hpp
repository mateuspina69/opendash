// The class states of st4a_event_triggers.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st4a_event_triggers_state {

// The class's bytes (its spawn record's): the message of its event, the story it stops at, the radius of variants 1 /
// 2 (<< 8), its arguments and the story flag it sets when it fires (kNoTriggerFlag: none). The arguments by variant:
// 1 / 2 the point (x, y, z); 3 .. 6 and 8 .. 11 the line (x: args[0], z: args[2]) and the gate (args[1]: a story flag
// for 3 .. 6, a weapon for 8 .. 11); 7 three story flags (kNoTriggerFlag: none); 12 the weapon (args[0])
struct EventTriggerFields : ActorClassState {
    explicit EventTriggerFields(ControllerActor& actor) : message(actor.params, 0), storyLimit(actor.params[2]), radius(actor.params[3]), flag(actor.recordPoint.spare) {}
    BytesField<int16_t, 4> message;
    FieldAlias<int8_t, uint8_t> storyLimit;
    FieldAlias<int8_t, uint8_t> radius;
    int16_t& flag;
    StateArray<int16_t, 3> args{};
};

} // namespace od::port::st4a_event_triggers_state
