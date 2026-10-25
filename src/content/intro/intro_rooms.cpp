// The game's start's rooms in the room setup (see intro_rooms.hpp), one routine per exe routine.
#include "content/intro/intro_rooms.hpp"

#include "game/game_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/room_uv_animation.hpp"

namespace od::content {

using namespace od::port;

namespace {

// ST00 / ST03 (exe 0x5d63b0 / 0x5d6820): sub 1's UV-animation list (exe 0x5f5bd0)
void st00UvAnimations() {
    if (gameState().room == 1) roomUvAnimationSetup(stateAddress(TableId::IntroRoomsSt00UvAnimList), stateAddress(TableId::IntroRoomsSt00UvAnimTable));
}
void st03UvAnimations() {
    if (gameState().room == 1) roomUvAnimationSetup(stateAddress(TableId::IntroRoomsSt03UvAnimList), stateAddress(TableId::IntroRoomsSt03UvAnimTable));
}
// ST4A (exe 0x5de260, the tutorial): the field's weapons outside sub 0
void st4aWeaponMode() { weaponMode(gameState().room != 0 ? kWeaponModeField : kWeaponModeTown); }

} // namespace

void registerIntroRooms(ContentRegistry& r) {
    r.roomFunctions[0x00] = {.afterEncounterSlots = &st00UvAnimations};
    r.roomFunctions[0x03] = {.afterEncounterSlots = &st03UvAnimations};
    r.roomFunctions[0x4a] = {.weaponMode = &st4aWeaponMode};
}

} // namespace od::content
