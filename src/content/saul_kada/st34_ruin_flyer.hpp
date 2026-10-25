// The ST34 ruin's flyer (sub 1, story 12+; pool 0x20 type 0x67 sub 1, exe 0x4edff0), see
// content/saul_kada/st34_ruin_flyer.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

constexpr uint8_t kRuinFlyerType = 0x67, kRuinFlyerSubtype = 1;
// The flyer's spawn parameter its shell writes: it rises out of the shell first
struct RuinFlyerParams {
    explicit RuinFlyerParams(const AnimatedActor& actor) : RuinFlyerParams(const_cast<AnimatedActor&>(actor)) {}
    explicit RuinFlyerParams(AnimatedActor& actor) : risesFirst(actor.params[3]) {}
    uint8_t& risesFirst;
};

// exe 0x4edff0: patrols back and forth around its home point, closes in on the player (within 0x400 of home, or once
// hit) and hovers firing pairs of shots (static type 0x4c), dives when the player lifts / grabs, flies home when the
// player leaves; staggers on hits, dies in an explosion with drops
void ruinFlyerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
