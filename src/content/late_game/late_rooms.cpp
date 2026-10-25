// The late game's rooms in the stage / room setup and the room scripts (see late_rooms.hpp), one routine per exe
// routine.
#include "content/late_game/late_rooms.hpp"

#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/game_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"
#include "game/visited_rooms.hpp"

namespace od::content {

using namespace od::port;

namespace {

// ---- ST40 (the switch rooms) ----------------------------------------------------------------------------------------
// A sub-room of ST40: the switches set (bits 0..3: their lock flags), the variant of the floor cell (0x3f, 0x3f) (-1:
// none) and the room's flag set last (0: none)
struct SwitchRoom {
    uint8_t switches;
    int8_t floorVariant;
    uint16_t doneFlag;
};
constexpr SwitchRoom kSwitchRooms[14] = {
    {0x7, -1, 0},     {0, -1, 0},       {0x2, 2, 0},      {0x8, 3, 0x680}, {0x6, 3, 0x681},
    {0x1, 0, 0x682},  {0xc, 0, 0x683},  {0x1, 1, 0x684},  {0xd, 1, 0x685}, {0x4, 2, 0x686},
    {0xb, 2, 0x687},  {0x7, 3, 0x688},  {0xe, 0, 0x689},  {0xd, 1, 0x68a},
};
constexpr uint32_t kSwitchLocks = 4;                       // the switches' lock flags: kFlagLocked + 4 + k
constexpr uint32_t kSwitchRoomFlags = 0xb;                 // the room flags 0x680..0x68a
constexpr uint32_t kFlagDoorLock1 = kFlagLocked + 1;
constexpr uint32_t kFlagSt40GateOpen = 0x2f6;              // sub 1's gate opened (then sub 0's door is unlocked too)
constexpr uint8_t kSt40Rooms = sizeof kSwitchRooms / sizeof kSwitchRooms[0];
// ST40's room function's second helper: the sound stage flag and the weapon mode by the sub (its weapon-mode part)
constexpr RoutineId kSt40SoundHelper = RoutineId::LateRoomsSt40SoundHelper;

// ST59..ST5B (the time trial courses): the flags their helper sets
constexpr uint32_t kCourseFlags[3] = {0x24d, 0x6f2, 0x24e};

// ST54 / ST56 / ST57's scripts and flags
constexpr uint32_t kFlagSt54First = kFlagGroupStage + 0, kFlagSt54Second = kFlagGroupStage + 1;
constexpr uint8_t kScriptSt54First = 0x6f, kScriptGameClear = 0x76, kScriptSt57 = 0x6e;

// ---- the room helpers --------------------------------------------------------------------------------------------

bool lateRoomHelper(RoutineId fn, bool run) {
    const GameState& g = gameState();
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    case RoutineId::St40RoomHelper: {
        // ST40: no room list; the switches' locks and the room flags cleared; by sub the switches, the
        // floor cell (0x3f, 0x3f)'s variant and the room's flag; sub 1: the gate closed -> the cell (0x3f, 0x43) open
        // and the door locked, else the cell shut; sub 0: the door locked until the gate opens
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        clearLockFlags(8);
        for (uint32_t f = kFlagGroupStage; f < kFlagGroupStage + kSwitchRoomFlags; ++f) clearStoryFlag(f);
        const uint8_t sub = g.room;
        if (sub >= kSt40Rooms) return true;
        if (sub == 0 && !storyFlag(kFlagSt40GateOpen)) setStoryFlag(kFlagDoorLock1);
        if (sub == 1) {
            const bool open = !storyFlag(kFlagSt40GateOpen);
            switchGridCellVariant(0x3f, 0x43, open ? 1 : 0);
            if (open) setStoryFlag(kFlagDoorLock1);
            return true;
        }
        const SwitchRoom& r = kSwitchRooms[sub];
        for (uint32_t k = 0; k < 4; ++k)
            if (r.switches & (1u << k)) setStoryFlag(kFlagLocked + kSwitchLocks + k);
        if (r.floorVariant >= 0) switchGridCellVariant(0x3f, 0x3f, uint8_t(r.floorVariant));
        if (r.doneFlag) setStoryFlag(r.doneFlag);
        return true;
    }
    case kSt40SoundHelper: // its part is the room function's weapon-mode part
        return true;
    case RoutineId::St59RoomHelper: // ST59 / ST5A / ST5B: no room list; the courses' flags
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        for (uint32_t f : kCourseFlags) setStoryFlag(f);
        return true;
    default: return false;
    }
}

// ---- the room functions' parts ------------------------------------------------------------------------------------

// ST40 (exe 0x5dd330): the sound's stage flags set to the unread bit 0 in sub 1 (0 elsewhere), the town's weapons in
// sub 1, else the field's
void st40WeaponMode() {
    const bool gate = gameState().room == 1;
    stageSoundFlagsSet(gate ? kSoundStageUnreadBit0 : 0u);
    weaponMode(gate ? kWeaponModeTown : kWeaponModeField);
}
// ST4B (exe 0x5de420): the field's weapons in sub 5
void st4bWeaponMode() { weaponMode(gameState().room == 5 ? kWeaponModeField : kWeaponModeTown); }

// ---- the room scripts --------------------------------------------------------------------------------------------

bool lateRoomScriptHandler(RoutineId fn, bool run) {
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    case RoutineId::St54RoomCase:
        // ST54 (the stage case): step 0: its first flag -> script 0x6f (the step kept), else on; step 1: its
        // second flag -> script 0x76 (the game clear), else step 2
        if (!run) return true;
        if (room.handlerStep == 0) {
            if (!storyFlag(kFlagSt54First)) {
                setStoryFlag(kFlagSt54First);
                scriptRun(kScriptSt54First);
                return true;
            }
            ++room.handlerStep;
        } else if (room.handlerStep != 1) {
            return true;
        }
        if (!storyFlag(kFlagSt54Second)) {
            setStoryFlag(kFlagSt54Second);
            scriptRun(kScriptGameClear);
        } else {
            ++room.handlerStep;
        }
        return true;
    case RoutineId::St56RoomCase: // ST56 (the stage case): its record once
        if (!run || room.handlerStep != 0) return true;
        spawnRecords(stateAddress(TableId::LateRoomsSt56Spawns), 1);
        ++room.handlerStep;
        return true;
    case RoutineId::St57RoomCase: // ST57 (the stage case): script 0x6e once (no door test)
        if (!run || room.handlerStep != 0) return true;
        scriptRun(kScriptSt57);
        ++room.handlerStep;
        return true;
    default: return false;
    }
}

} // namespace

void registerLateGameRooms(ContentRegistry& r) {
    r.roomHelpers.push_back(&lateRoomHelper);
    r.roomHandlers.push_back(&lateRoomScriptHandler);
    r.roomFunctions[0x40] = {.weaponMode = &st40WeaponMode}; // its render parameters by sub: render output
    r.roomFunctions[0x4b] = {.weaponMode = &st4bWeaponMode};
}

} // namespace od::content
