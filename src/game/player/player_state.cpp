// The player (see player_state.hpp).
#include "game/player/player_state.hpp"

#include <memory>

namespace od::port {

namespace {
Player& storage() {
    static Player p;
    static const bool identitySet = (p.at = kPlayer, true);
    (void)identitySet;
    return p;
}
} // namespace

Player& player() { return storage(); }
Fixed16Vec3 playerStructAsPoint() {
    const Player& p = player();
    Fixed16Vec3 v;
    v.x = int32_t(p.headWord());
    v.y = int32_t(uint32_t(p.type) | uint32_t(p.subtype) << 8 | uint32_t(p.variant) << 16 | uint32_t(p.spawnByte) << 24);
    v.z = int32_t(uint32_t(p.state) | uint32_t(p.subState) << 8 | uint32_t(p.step) << 16 | uint32_t(p.stepCounter) << 24);
    return v;
}
PlayerTail& playerTail() {
    static PlayerTail t;
    return t;
}

void resetPlayer() {
    Player& p = storage();
    std::destroy_at(&p);
    std::construct_at(&p);
    p.at = kPlayer;
    // the 0x10 bytes after the player's (exe 0x5e00e0(1) clears up to 0xa72230)
    playerTail() = {};
}

} // namespace od::port
