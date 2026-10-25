// The Blade Arm's state (game/shots/blade_arm.cpp; the effects' draw, game/effects_draw, draws its trail from it): the
// swing's block, the trail's fields and states.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/shots/shot_common.hpp"
#include "game/state_array.hpp"

namespace od::port {

constexpr uint32_t kBladeRingSize = 8;
// The swing's block (the shot's class state, which the trail draws from): the stats, the ring's newest entry, per entry
// the blade's root point (s16, >> the stage shift), its pitch and its yaw
struct BladeFields : ActorClassState {
    explicit BladeFields(ShotActor&) {}
    ShotStats stats;
    uint16_t ringIndex = 0;
    StateArray<ShortVec3, kBladeRingSize> ringPoints{};
    StateArray<int16_t, kBladeRingSize> ringPitches{};
    StateArray<int16_t, kBladeRingSize> ringYaws{};
};
// The trail's fields: the shot whose swing block it draws
struct BladeTrailFields : ActorClassState {
    explicit BladeTrailFields(EffectActor&) {}
    ActorRef<ShotActor> swingShot; // the blade's shot (the exe keeps the address of its swing block)
};
// [0x8c3f60]
enum class BladeTrailState : uint8_t { Start = 0, Follow = 1 }; // exe 0x55e890 / 0x55e8b0

} // namespace od::port
