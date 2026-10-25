// The class states of spread_buster.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/shots/shot_common.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::spread_buster_state {

constexpr uint32_t kSparkleRays = 8, kSparkleSparks = 4;

// The shot's parameter (+0xc: which copy of the fan it is; 0 = the first, which spawns the others) and its stats
struct SpreadFields : ActorClassState {
    explicit SpreadFields(ShotActor& actor) : copy(actor.params[0]), copyWord_(actor.params, 0) {}
    FieldAlias<int8_t, uint8_t> copy;
    BytesField<uint32_t, 4> copyWord_;
    ShotStats stats;
    void setCopy(uint32_t i) { copyWord_ = i; }
};

// The sparkle's fields: its ticks left, its rays' angles (<< 4) and lengths, its sparks' angles (<< 4) and speeds
struct SparkleFields : ActorClassState {
    explicit SparkleFields(StaticActor& actor) : ticks(actor.wallHit) {}
    uint32_t& ticks;
    StateArray<uint8_t, kSparkleRays> rayAngles{};
    StateArray<uint8_t, kSparkleRays> rayLengths{};
    StateArray<uint8_t, kSparkleSparks> sparkAngles{};
    StateArray<uint8_t, kSparkleSparks> sparkSpeeds{};
};

} // namespace od::port::spread_buster_state
