// The table of players (see player_state.hpp).
#include "game/player/player_state.hpp"

#include <memory>
#include <stdexcept>

namespace od::port {

namespace {
// a player constructed anew keeps its identity: its address and slot id
void setIdentity(Player& p, size_t id) {
    p.at = playerAddress(id);
    p.slot = uint8_t(id);
}

struct PlayerTable {
    std::array<Player, kPlayerSlots> players;
    std::array<bool, kPlayerSlots> present{true};
    PlayerTable() {
        for (size_t id = 0; id < kPlayerSlots; ++id) setIdentity(players[id], id);
    }
};

PlayerTable& table() {
    static PlayerTable t;
    return t;
}

void constructAnew(size_t id) {
    Player& p = table().players[id];
    std::destroy_at(&p);
    std::construct_at(&p);
    setIdentity(p, id);
}

void checkId(size_t id) {
    if (id >= kPlayerSlots) throw std::out_of_range("no player with this id");
}
} // namespace

Player& player() { return table().players[0]; }
Player& playerById(size_t id) {
    checkId(id);
    return table().players[id];
}
size_t playerId(const Player& p) {
    const int id = playerIdAtAddress(p.at);
    if (id < 0) throw std::logic_error("a player outside the table");
    return size_t(id);
}
namespace {
size_t g_acting = 0;
} // namespace
size_t actingPlayerId() { return g_acting; }
Player& actingPlayer() { return table().players[g_acting]; }
ScopedActingPlayer::ScopedActingPlayer(size_t id) : previous_(g_acting) {
    checkId(id);
    g_acting = id;
}
ScopedActingPlayer::~ScopedActingPlayer() { g_acting = previous_; }

bool playerPresent(size_t id) {
    checkId(id);
    return table().present[id];
}
void joinPlayer(size_t id) {
    checkId(id);
    constructAnew(id);
    table().present[id] = true;
}
void leavePlayer(size_t id) {
    checkId(id);
    table().present[id] = false;
}

Fixed16Vec3 playerStructAsPoint(const Player& p) {
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
    for (size_t id = 0; id < kPlayerSlots; ++id) constructAnew(id);
    // the 0x10 bytes after the player's (exe 0x5e00e0(1) clears up to 0xa72230)
    playerTail() = {};
}

} // namespace od::port
