// The flying point (pool 0xe0 type 0x1c): a spark trail flown from a point to a target, a blast and a story flag at the
// end, content/nino/fly_point.cpp. Its spawners: the ST19 machine (content/nino/st19), the end-of-mission scripts.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"

namespace od::port {

// Its fields: from its point to its target (the first state turns the target into the step a tick) in `ticks`, its
// flag set at the end (kNoFlyFlag: none)
struct FlyPointFields : ActorClassState {
    explicit FlyPointFields(ControllerActor& actor) : ticks(actor.params, 0), flag(actor.params, 2) {}
    BytesField<int16_t, 4> ticks;
    BytesField<int16_t, 4> flag;
    ShortVec3 from;
    ShortVec3 to;
};
constexpr int16_t kNoFlyFlag = -1;
constexpr uint8_t kFlyPointType = 0x1c;

// exe 0x426820: the point flies its ticks leaving a hit spark (variant 8) a tick at its place; at the end a blast
// (the global static type 7) there and its flag set (unless kNoFlyFlag); then freed
void flyPointUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
