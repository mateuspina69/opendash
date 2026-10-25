// The class states of buster_cannon.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/shots/shot_common.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::buster_cannon_state {

struct CannonFields : ActorClassState {
    explicit CannonFields(ShotActor&) {}
    ShotStats stats;
};

constexpr uint32_t kBurstSpikes = 0x10, kBurstSparks = 0x20;

// The burst's fields: its size (the spawn's parameter), its ticks left, the spikes' angles and lengths (64-step / the
// size steps; the second half drawn as ring arcs) and the sparks' angles (& 0x3f; the top two bits a size step) and
// speeds
struct FlashBurstFields : ActorClassState {
    explicit FlashBurstFields(StaticActor& actor) : size(actor.params, 0), ticks(actor.params, 2) {}
    BytesField<uint16_t, 4> size;
    BytesField<int16_t, 4> ticks;
    StateArray<int8_t, kBurstSpikes> spikeAngles{};
    StateArray<int8_t, kBurstSpikes> spikeLengths{};
    StateArray<int8_t, kBurstSparks> sparkAngles{};
    StateArray<int8_t, kBurstSparks> sparkSpeeds{};

    void clearBlock() override {
        for (size_t i = 20; i < 32; ++i) sparkSpeeds[i] = {};
    }
};

} // namespace od::port::buster_cannon_state
