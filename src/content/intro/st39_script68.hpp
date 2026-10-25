// ST39's script 0x68 (the second visit to the opening's stage: story 0x11 room 1) and its cast: the crew member of the
// opening's crew class (variant 2) and the class 0x44b940 (content/intro/st39_script68.cpp).
#pragma once

#include <cstdint>
#include "game/actor_state.hpp"

namespace od::port {

struct AnimatedActor;
struct PlayerEnv;

// exe 0x44b940 (ST39 pool 0x20 type 2 sub 0): script 0x68's cast member (registered in content/intro/register.cpp)
void script68CastUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4ea120: the crew class's variant 2 (script 0x68's record 0x8acce4): the variant only; the crew class
// (content/intro/opening_cast) runs it from its variant switch and runs its tail after it
void script68Crew(AnimatedActor& actor);

// Script 0x68, dispatched by its handler (game/story storyScriptHandler)
bool st39Script68Ported(uint8_t id);
bool st39Script68Tick(const PlayerEnv& env, uint8_t id);

} // namespace od::port
