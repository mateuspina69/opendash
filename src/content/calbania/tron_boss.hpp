// Tron Bonne's tank (ST25 sub 6, story 3): the boss after script 0x1b. See content/calbania/tron_boss.cpp.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "game/actor_movement.hpp"

namespace od::port {

// 0x46d7e0 (pool 0x20, the ST25 stage table type 0x11 sub 0, spawned by the room handler 0x581bb0 from record
// 0x8c9220): the fight (attacks chosen by distance and RNG, hit points with Roll's / Tron's messages in window 4,
// arms that break off, death -> flag 0x589)
void tronBossUpdate(const PlayerEnv& env, uint32_t actor);
// OpenDash's co-op (ContentRegistry::netPictures): the tank's picture is its end and its broken arms; the tank a scene's
// rebuild spawns goes on from the world's, beaten -- the scene after it (script 0x1c) reads its arms
std::vector<uint8_t> tronBossPicture(uint32_t actor);
void tronBossLayPicture(uint32_t actor, std::span<const uint8_t> picture);

} // namespace od::port
