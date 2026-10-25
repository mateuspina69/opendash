// The extinguisher's state (game/shots/extinguisher.cpp; the effects' draw, game/effects_draw, draws the stream from
// it): the shot's fields and states.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/shots/shot_common.hpp"
#include "game/state_array.hpp"

namespace od::port {

constexpr uint32_t kStreamPoints = 16;

// The shot's fields: the parameter (the partner has 1), the stats (+0x64), the stream's point count and the first
// point that hits (the tail runs out after release), the sound's ticks, the partner, the stream's points (s16 x / y /
// z and a fourth lane)
struct ExtinguisherFields : ActorClassState {
    explicit ExtinguisherFields(ShotActor& actor) : isPartner(actor.params[0]) {}
    uint8_t& isPartner;              // the partner slot (isPartnerSlot: it holds StreamPartnerFields instead)
    ShotStats stats;
    uint16_t statsPad = 0;           // cleared with the stats (no reader)
    uint16_t pointCount = 0;
    uint16_t tail = 0;
    uint16_t soundTicks = 0;
    ActorRef<ShotActor> partner;     // the partner slot
    StateArray<StateArray<int16_t, 4>, kStreamPoints> points{};
};
// The partner's fields: the nozzle (the hand >> the stage shift, 4 lanes), the pitch / yaw each point had last tick,
// the points' hit boxes
struct StreamPartnerFields : ActorClassState {
    explicit StreamPartnerFields(ShotActor&) {}
    StateArray<int16_t, 4> nozzle{};
    StateArray<int16_t, kStreamPoints> pitches{};
    StateArray<int16_t, kStreamPoints> yaws{};
    StateArray<HitBoxHead, kStreamPoints> hitBoxes{};
};
// a shot slot of the class is the partner of another (its parameter 1): its class state is the partner's
// (StreamPartnerFields, game/shots/extinguisher.cpp)
inline bool isPartnerSlot(const ShotActor& shot) { return shot.params[0] != 0; }
// [0x8c4a7c]
enum class ExtinguisherState : uint8_t {
    Spawn = 0,
    Spray = 1,
    RunOut = 2,
    Bubbles = 3
}; // exe 0x5705d0 / 0x570770 /
   // 0x570870 / 0x5708f0

} // namespace od::port
