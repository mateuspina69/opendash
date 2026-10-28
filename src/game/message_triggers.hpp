// The message triggers (pool 0xe0 controllers): the message trigger (exe 0x4263f0, type 0x1b: a stage message shown in
// window 4 when its condition holds -- at once, the player in reach, a line crossed, flags, the special weapon, a
// countdown -- and its flag set) and the scripted message (exe 0x426950, type 0x1d: the player taken over and a message
// event run once no script / event runs). The rooms' spawn records and the scripts (the fire mission's hints) place
// them.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port {

struct PlayerEnv;

// The variant's three shorts: the record point's x / y / z lanes (+0x10 / +0x12 / +0x14, where the exe's conditions
// read them, exe 0x426530 / 0x426750 / 0x4267c0): its point (x, y, z), its line (x / z) and its gate (y), its flags
// (kNoMessageFlag: met), its countdown (x)
struct MessageTriggerLanes {
    ShortVec4& lanes;
    int16_t& operator[](size_t i) const {
        switch (i) {
        case 0: return lanes.x;
        case 1: return lanes.y;
        case 2: return lanes.z;
        default: stateIndexError(i, size());
        }
    }
    static constexpr size_t size() { return 3; }
};
// The message trigger's bytes
struct MessageTriggerFields : ActorClassState {
    explicit MessageTriggerFields(ControllerActor& actor) : message(actor.params, 0), lastStory(actor.params[2]), range(actor.params[3]), flag(actor.recordPoint.spare), params{actor.recordPoint} {}
    BytesField<int16_t, 4> message; // the stage bank's message window 4 shows
    FieldAlias<int8_t, uint8_t> lastStory; // the trigger is gone once the story is past it
    FieldAlias<int8_t, uint8_t> range; // the reach (* 0x100) of the distance variants / the flags variant's mask
    int16_t& flag; // the flag set when it fires (kNoMessageFlag: none); once set: gone
    MessageTriggerLanes params; // the variant's
};
constexpr int16_t kNoMessageFlag = -1;
// the message trigger's class (pool 0xe0 type) and its first variant (shown at once)
constexpr uint8_t kMessageTriggerType = 0x1b;
constexpr uint8_t kMessageTriggerAtOnce = 0;

// exe 0x4263f0 (pool 0xe0 type 0x1b): the message trigger
void messageTriggerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x426950 (pool 0xe0 type 0x1d): the scripted message
void scriptedMessageUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
