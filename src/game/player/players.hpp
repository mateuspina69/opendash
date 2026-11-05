// What the world's code asks of "the player" when a session has several. With nobody else
// in the session every answer is player 0, the exe's one player.
#pragma once

#include <cstddef>

#include "game/player/player_state.hpp"

namespace od::port {

// the player in the session nearest to a point on the ground plane; of two as near, the lower id
Player& nearestPlayer(const Fixed16Vec3& point);
Player& nearestPlayer(const int32_t point[3]); // the point's three 16.16 words
Player& nearestPlayer(int16_t x, int16_t z);   // the point's integer parts

// the player an actor a player can carry asks about: its carrier while carried, else the nearest
Player& carrierOrNearest(const Actor& actor);
// a player's Use has picked this actor: in a session it need not be the player nearest to it
bool anyPlayerTalksTo(const ActorHeader& actor);

// `f(player)` for each player in the session, by id
template <class F> void forEachPlayer(F&& f) {
    for (size_t id = 0; id < kPlayerSlots; ++id)
        if (playerPresent(id)) f(playerById(id));
}

// the first player in the session, by id, for whom `f(player)` holds; else null
template <class F> Player* findPlayer(F&& f) {
    for (size_t id = 0; id < kPlayerSlots; ++id)
        if (playerPresent(id) && f(playerById(id))) return &playerById(id);
    return nullptr;
}
template <class F> bool anyPlayer(F&& f) { return findPlayer(f) != nullptr; }
// a room's condition on its player: true when a player meets it; that player's machine shows what follows (game/event)
bool roomPlayerMeets(bool (*cond)(const Player& p));
bool roomPlayerStands(); // a player of the room is not down
template <class F> bool allPlayers(F&& f) {
    return findPlayer([&](const Player& p) { return !f(p); }) == nullptr;
}

} // namespace od::port
