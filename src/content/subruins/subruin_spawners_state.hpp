// The class states of subruin_spawners.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port::subruin_spawners_state {

// Its own fields: its point and the drop's point
struct DropSpawnerFields : ActorClassState {
    explicit DropSpawnerFields(ControllerActor&) {}
    ShortVec3 point;
    ShortVec3 drop;
};

// Its own fields: the line's x / z
struct LineTriggerFields : ActorClassState {
    explicit LineTriggerFields(ControllerActor& actor) : x(actor.recordPoint.x), z(actor.recordPoint.z) {}
    int16_t& x;
    int16_t& z;
    StateArray<uint8_t, 8> ownBytes{};
};

// ---- the group spawners (0x4227a0 / 0x422cb0) -----------------------------------------------------------------------
// Their own bytes: the record actor the variant 1 waits on
struct GroupSpawnerFields : ActorClassState {
    explicit GroupSpawnerFields(ControllerActor&) {}
    uint32_t recordActor = 0;
    StateArray<uint8_t, 4> clearedPad0{}; // bytes the block clear covers (no reader)
    void clearOwnBytesBytes() {
        recordActor = 0;
        clearedPad0 = {};
    }
};

} // namespace od::port::subruin_spawners_state
