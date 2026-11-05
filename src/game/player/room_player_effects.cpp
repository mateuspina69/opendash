// What a room's logic does to its player (see room_player_effects.hpp).
#include "game/player/room_player_effects.hpp"

#include <utility>

#include "game/hit_points.hpp"
#include "game/player/players.hpp"

namespace od::port {

namespace {
std::function<void(RoomPlayerField, int16_t)> g_hook;
} // namespace

void applyRoomPlayerField(Player& p, RoomPlayerField field, int16_t value) {
    switch (field) {
    case RoomPlayerField::InWater: p.inWater = uint8_t(value); break;
    case RoomPlayerField::Burden: p.burden = uint8_t(value); break;
    case RoomPlayerField::HitPoints: p.hitPoints = value; break;
    case RoomPlayerField::FullLife: hitPointsChange(p, p.maxHitPoints + 1); break;
    }
}

void roomPlayersSet(RoomPlayerField field, int16_t value) {
    if (!g_hook) {
        applyRoomPlayerField(player(), field, value);
        return;
    }
    forEachPlayer([&](Player& p) { applyRoomPlayerField(p, field, value); });
    g_hook(field, value);
}

void setRoomPlayersHook(std::function<void(RoomPlayerField, int16_t)> hook) { g_hook = std::move(hook); }

} // namespace od::port
