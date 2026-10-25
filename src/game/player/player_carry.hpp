// The player's carry and grabs: lifting an actor (the sub-state the lifted actor sets through game/carry), holding it,
// throwing / putting it down, and being held by a grabbing actor (playerGrab / playerGrabHold, called by its class).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port {

struct Player;
struct Actor;

// exe 0x57b200(player, mode)'s modes: where the throw is asked from
constexpr int16_t kThrowStanding = 0; // the throw / put-down plays, idle
constexpr int16_t kThrowRunning = 1;  // the run stops first (the throw queued)
constexpr int16_t kThrowAirborne = 2; // only in the air: thrown at once
// exe 0x57b200: the special button (or a queued throw) releases the carried actor once the lock option allows; 1 = taken
int tryThrow(Player& p, int16_t mode);
// exe 0x573870: the throw / put-down (carryFlags kCarryThrowing, step +0x142)
void throwUpdate(Player& p);

// exe 0x573e20(actor, range, flag): the player within range of the actor on all three axes (integer parts, not airborne)
// is snapped onto it, sub-state Held (flag: the held pose, else the struggle); 1 = grabbed
int playerGrab(const Actor& actor, int range, int flag);
// exe 0x573ed0(actor, counter, heavy): keeps the held player on the actor; each tick with a press of the moves / jump
// counts the grabbing class's struggle counter down (unless the host's input is captured); at 0 the player is let go
// and 1 returned. heavy: the struggle meter (the hidden bones word) set while held
int playerGrabHold(const Actor& actor, uint16_t& counter, int heavy);

} // namespace od::port
