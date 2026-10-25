// The class states of st3c_minion_spawner.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st3c_minion_spawner_state {

// The spawner's own bytes (its block, +0x10..+0x1f cleared at the start): its timer, the minions alive (its slot
// bytes set), its three slot bytes, the near tries of its placing
struct SpawnerFields : ActorClassState {
    explicit SpawnerFields(ControllerActor& actor) : timer(actor.recordPoint.x), alive(actor.recordPoint.y), owner(actor) {}
    FieldAlias<uint16_t, int16_t> timer;
    FieldAlias<uint16_t, int16_t> alive;
    StateArray<uint8_t, 3> slots{}; // (+0x14)
    uint8_t nearTries = 0;
    StateArray<uint8_t, 8> clearedPad0{}; // bytes the block clear covers (no reader)
    void clearBlock() {
        slots = {};
        nearTries = 0;
        clearedPad0 = {};
        timer = 0;
        alive = 0;
    }
    ControllerActor& owner; // its slot (the slot bytes' exe addresses its minions keep)
};

// The exe globals st3c_minion_spawner.cpp keeps (their exe addresses in the comments)
struct St3cMinionSpawnerGlobals {
    uint32_t spawnerBlock = 0; // 0xa59dd0
};
inline St3cMinionSpawnerGlobals& st3cMinionSpawnerGlobals() {
    static St3cMinionSpawnerGlobals s;
    return s;
}

} // namespace od::port::st3c_minion_spawner_state
