// The players a world holds for those who simulate their own Mega Man elsewhere: a real
// Player that takes its owner's state each tick instead of a pad. Enemies target it, it stands in the hit list and it
// collects pickups; its input, movement and animation are its owner's.
#pragma once

#include <cstddef>

#include "game/player/player_snapshot.hpp"

namespace od::port {

// the player of this id joins the world as remote, in the state s
void remotePlayerJoin(size_t id, const PlayerSnapshot& s);
// the owner's state of this tick
void remotePlayerState(size_t id, const PlayerSnapshot& s);
// the player of this id is out of the world
void remotePlayerLeave(size_t id);
bool playerIsRemote(size_t id);
// each remote player's pose and hit-list entry (after the local player's update, before the hit pass)
void remotePlayersUpdate();

} // namespace od::port
