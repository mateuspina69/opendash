// The ice floor's marker of the sub-ruins ST2D / ST2E / ST2F (pool 0xe0 type 1, exe 0x425b10; hypothesis names): every
// tick the player's floor made slippery (exe 0x571ee0) where the rooms are ice: everywhere in ST2E, in ST2D but its
// sub-room 0 and in ST2F but its sub-room 7; in those two rooms only while he stands on an ice block (static type
// 0x1f).
#include "content/saul_kada/ice_floor_marker.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_status.hpp"

namespace od::port {

namespace {

constexpr uint8_t kSt2d = 0x2d, kSt2e = 0x2e, kSt2f = 0x2f;
constexpr uint8_t kSt2dDryRoom = 0, kSt2fDryRoom = 7;
constexpr uint8_t kIceBlockType = 0x1f; // the stage's static type of the ice blocks
constexpr uint8_t kSlotPoolMask = 0xf0;

// the player stands on an ice block (a static slot of the ice block's type)
bool onIceBlock() {
    const Player& p = player();
    if (p.onPlatform == 0) return false;
    const ActorHeader& on = *p.platform;
    return (on.slot & kSlotPoolMask) == kStaticClass && on.type == kIceBlockType;
}

void icePlayerMarker() { // exe 0x425b10
    const GameState& g = gameState();
    const uint8_t stage = g.stage, room = g.room;
    if (stage == kSt2e || (stage == kSt2d && room != kSt2dDryRoom) || (stage == kSt2f && room != kSt2fDryRoom))
        slipperyFloor(player());
    else if ((stage == kSt2d || stage == kSt2f) && onIceBlock())
        slipperyFloor(player());
}

} // namespace

void icePlayerMarkerUpdate(const PlayerEnv&, uint32_t) { icePlayerMarker(); }

} // namespace od::port
