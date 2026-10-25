// Appo and Dah's face (their classes' shared code: the fight exe 0x4c1b30, the script puppets exe 0x4c5f90 / 0x4c6650 /
// 0x4c6ae0 / 0x4c6fe0): the three mouth parts of their model and the talking mouth's steps, kept in their class blocks.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port {

// the talking mouth's step and its ticks (the same class-block bytes, +0x14f / +0x150, in every Appo / Dah class: a
// member of each class's state)
struct AppoDahMouth {
    uint8_t step = 0;
    uint8_t ticks = 0;
};
// the mouth parts (shown one at a time)
constexpr size_t kMouthClosedPart = 1, kMouthOpenPart = 8, kMouthWidePart = 9;

// exe 0x4c6510: the mouth closed (the open and wide parts hidden, the closed one shown), its steps cleared
void appoDahFaceReset(AnimatedActor& actor, AppoDahMouth& mouth);
// exe 0x4c6550 / 0x4c69e0 / 0x4c6ee0 / 0x4c7300 (the same code): the talking mouth -- every 4th tick the next of its 8
// steps
void appoDahTalk(AnimatedActor& actor, AppoDahMouth& mouth);

} // namespace od::port
