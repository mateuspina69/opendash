// The mouse on the message windows' choices (game/choice_pointer): over an option the hand moves there, a left click
// on one confirms it (circle), a right click is the choice's cancel (cross).
#pragma once

#include <SDL3/SDL_events.h>

#include <cstdint>


namespace od::app {

// e (window pixels x density) on the 320 x 240 frame at scale s, origin x ox; true = a choice is up (the event is its)
bool choiceMouse(const SDL_Event& e, float density, float s, float ox, int& hover,
                 uint16_t& clickPress, int& clickDelay);

} // namespace od::app
