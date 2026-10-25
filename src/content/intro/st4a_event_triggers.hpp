// The event triggers of the tutorial (ST4A, pool 0xe0 stage class type 0xa, exe 0x4223c0): a controller that starts
// the event of its message once the player is near its point, crosses its line or holds its weapon / story flags
// (content/intro/st4a_event_triggers.cpp).
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x4223c0: an event trigger's update (registered in content/intro/register.cpp)
void st4aEventTriggerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
