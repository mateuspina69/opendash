// What a room's logic does to its player (in the water, a burden, life set): in a session, every player of the room, each on their
// own machine (the world tells them). In the game: its one player.
#pragma once

#include <cstdint>
#include <functional>

namespace od::port {

struct Player;

enum class RoomPlayerField : uint8_t { InWater = 0, Burden = 1, HitPoints = 2, FullLife = 3 };
void roomPlayersSet(RoomPlayerField field, int16_t value);
void setRoomPlayersHook(std::function<void(RoomPlayerField, int16_t)> hook);
void applyRoomPlayerField(Player& p, RoomPlayerField field, int16_t value);

} // namespace od::port
