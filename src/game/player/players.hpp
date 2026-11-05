// What the world's code asks of "the player" when a session has several. With nobody else
// in the session every answer is player 0, the exe's one player.
#pragma once

#include <cstddef>

#include "game/player/player_state.hpp"

namespace od::port {

// the player in the session nearest to a point on the ground plane; of two as near, the lower id
Player& nearestPlayer(const Fixed16Vec3& point);

// `f(player)` for each player in the session, by id
template <class F> void forEachPlayer(F&& f) {
    for (size_t id = 0; id < kPlayerSlots; ++id)
        if (playerPresent(id)) f(playerById(id));
}

} // namespace od::port
