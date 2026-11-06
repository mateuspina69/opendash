// The players a world holds for those who simulate their own Mega Man elsewhere: a real
// Player that takes its owner's state each tick instead of a pad. Enemies target it, it stands in the hit list and it
// collects pickups; its input, movement and animation are its owner's.
#pragma once

#include <cstddef>
#include <functional>

#include "engine/file_data.hpp"

#include "game/player/player_snapshot.hpp"

namespace od::port {

// the player of this id joins the world as remote, in the state s
void remotePlayerJoin(size_t id, const PlayerSnapshot& s);
// the owner's state of this tick
void remotePlayerState(size_t id, const PlayerSnapshot& s);
// the player of this id is out of the world
void remotePlayerLeave(size_t id);
bool playerIsRemote(size_t id);
// A world's scene: player 0 is its Mega Man, simulated here from the scene owner's pad, and the others are out of the fight
void remotePlayersSceneStart();
void remotePlayersSceneEnd();
// each remote player's pose and hit-list entry (after the local player's update, before the hit pass)
void remotePlayersUpdate();
// the loaded files each remote player poses on: their look's body and weapon over this machine's (a look's data this
// machine's own files do not hold -- a weapon without a PL00R, another costume -- is not read from them). nullptr: the
// bound files as they are
void setRemotePlayerFiles(std::function<LoadedFileData*(size_t id)> filesOf);
// ... this remote player's (nullptr: not remote, or none built)
LoadedFileData* remotePlayerFiles(size_t id);
// the owner pressed Use by nothing of theirs: this tick the classes see what it picks among the world's actors
void remotePlayerUse(size_t id);
// the tick is over: what it picked is let go
void remotePlayersTalkDone();

} // namespace od::port
