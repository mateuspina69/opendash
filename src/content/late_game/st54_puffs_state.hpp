// The class states of st54_puffs.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st54_puffs_state {

constexpr uint8_t kFading = 0x01, kLongSmoke = 0x02; // the emitter's fade flags
constexpr uint8_t kMaxFaded = 0x13;
constexpr uint32_t kPuffs = 0x12;

// one puff: its point and its life left
struct Puff {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
    int16_t life = 0;
};

// the emitter's own bytes (its puffs fill the slot's body)
struct PuffEmitterFields : ActorClassState {
    explicit PuffEmitterFields(StaticActor& actor) : spawnWord(actor.scale.y), fadeFlags(actor.scale.z, 0), fadedPuffs(actor.scale.z, 1), owner(actor.hitBox.result), keptWord(actor.hitBox.previousX), spawnDelay(actor.hitBox.previousZ) {}
    FieldAlias<uint16_t, int16_t> spawnWord; // the word the init keeps at keptWord
    PartField<uint8_t, int16_t> fadeFlags; // kFading | kLongSmoke
    PartField<uint8_t, int16_t> fadedPuffs; // the puffs the fade reaches (grows a tick, to kMaxFaded)
    uint32_t& owner; // the actor the puffs come out of (0: none)
    FieldAlias<uint16_t, int16_t> keptWord;
    FieldAlias<uint16_t, int16_t> spawnDelay; // ticks to the next puff
    StateArray<Puff, kPuffs> puffs{};
    StateArray<int8_t, kPuffs> brightness{};
    uint16_t liveMask = 0; // bit k: puff k alive
    uint8_t rowByte = 0; // the row's second byte (its reader is not traced)

    void clearBlock() override {
        liveMask = 0;
        rowByte = 0;
    }
};

} // namespace od::port::st54_puffs_state
