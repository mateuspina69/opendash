// A player at the end of their life in a session: down instead of dead, lying where they fell, until a teammate revives them or
// they come back next to one. The game over starts only when everyone is down (app/world_host).
#pragma once

#include <cstdint>

namespace od::port {

struct Player;

constexpr int kReviveTicks = 90;         // a teammate holds Use this long, about 3 s
constexpr int kReviveReach = 0x100;      // ... this close
constexpr int kRespawnAloneTicks = 300;  // down this long: they may come back alone, 10 s

void setPlayerDownsInstead(bool downs);
bool playerDownsInstead();
bool playerIsDowned(const Player& p);
// they get up, with this life
void playerRevive(Player& p, int16_t life);

} // namespace od::port
