// The fire mission's rooms (ST1E's room case, exe 0x580710; see fire_mission.hpp): the room handler of each of the
// Flutter's three rooms spawns its fires, counts the room's time (out of time, or the player down: the result script
// 0xd), shows the hints (message triggers, game/message_triggers) and, once the room's fires are out, opens its door.
//   room 0 (exe 0x580750): 4 fires, 0x708 ticks, the briefing's banner
//   room 1 (exe 0x5808d0): 13 fires, one more once the fire before the wall is out, 0xe10 ticks
//   room 2 (exe 0x580a70): 5 fires, Data, the scripted message in the kitchen, 0x1518 ticks; all out: the success
#include "content/intro/fire_mission.hpp"

#include "engine/data_source.hpp"
#include "game/actor_state.hpp"
#include "game/room_scripts.hpp"
#include "game/actor_pools.hpp"
#include "game/event_state.hpp"
#include "game/game_state.hpp"
#include "game/hud.hpp"
#include "game/message.hpp"
#include "game/event.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_engine.hpp"
#include "game/spawn.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/message_triggers.hpp"

namespace od::port {

namespace {

constexpr uint8_t kST1E = 0x1e;
constexpr uint8_t kResultScript = 0xd;
// the rooms' handlers (exe table 0x8c8228: a routine per room; the named routines of engine/routines.csv)
const DataTable<RoutineTable<3>> roomHandlers{TableId::FireMissionRoomsRoomHandlers};
// the rooms' spawn records (exe data) and their counts
constexpr uint32_t kRoom0FireCount = 4;
constexpr uint32_t kRoom1FireCount = 0xd;
constexpr uint32_t kRoom2FireCount = 5;
// the rooms' time limits and their hint ticks
constexpr uint16_t kRoom0Limit = 0x708, kRoom1Limit = 0xe10, kRoom2Limit = 0x1518;
constexpr uint16_t kRoom0Hint = 0x546, kLateHint = 0xa8c, kRoom2SecondHint = 0xfd2;
constexpr uint16_t kHintEvery = 900;
// the hints' messages (the stage bank)
constexpr int16_t kHintHurry = 0x28, kHintLast = 0x29, kHintIdle = 0x2a;
// the briefing (room 0): its events and its banner
constexpr uint32_t kBriefingFlags = 9;
constexpr uint16_t kBriefingMessage = 0x32;
constexpr uint16_t kBriefingBannerX = 0x54, kBriefingBannerY = 0x28;
constexpr HudBannerStyle kBriefingBanner = HudBannerStyle::MissionStart;
constexpr uint32_t kEventFlags = 1;
// the doors' locks of the rooms (kFlagLocked + 1 ..)
constexpr uint32_t kFirstLock = kFlagLocked + 1;
constexpr uint32_t kRoom0LastLock = kFlagLocked + 5, kRoomLastLock = kFlagLocked + 4;
constexpr uint32_t kRoom0Door = kFlagLocked + 1, kRoom1Door = kFlagLocked + 3;
constexpr uint32_t kHintShownFlag = 0x681; // a hint is shown (the next waits for it)

enum class Room0Step : uint8_t { Spawn = 0, Unused = 1, Brief = 2, BriefEnd = 3, Run = 4 };
enum class Room1Step : uint8_t { Spawn = 0, Run = 1, LateFire = 2 };
enum class Room2Step : uint8_t { Spawn = 0, WaitMessage = 1, Run = 2 };

// the mission's block
FireMissionState& mission() { return fireMission(); }

// exe 0x580ca0(timer, limit): room `room`'s time counted while the player lives; 1 once it ran out (or the player is
// down) and no event runs
int missionTimer(size_t room, uint16_t limit) {
    FireMissionState& m = mission();
    if (m.roomTimers[room] < limit && player().hitPoints != 0) {
        m.roomTimers[room] += 1u;
        return 0;
    }
    return eventState().active == 0 ? 1 : 0;
}

// exe 0x580bf0: a hint due every 900 ticks without one shown (and while the extinguisher's arm is down)
int hintDue() {
    FireMissionState& m = mission();
    if (!storyFlag(kHintShownFlag) && !(player().armFlags & kArmSpecial)) {
        if (m.hintTicks < kHintEvery) {
            m.hintTicks += 1u;
            return 0;
        }
        if (eventState().active == 0) {
            m.hintTicks = 0;
            return 1;
        }
    } else {
        m.hintTicks = 0;
    }
    return 0;
}

// exe 0x580c50(message): a message trigger shown at once with the hint
int hint(int16_t message) {
    const uint32_t at = allocActorE0();
    if (at == 0) return 0;
    setStoryFlag(kHintShownFlag);
    ControllerActor& c = controllerActorAt(at);
    MessageTriggerFields& t = classState<MessageTriggerFields>(c);
    c.flags = kActorLive;
    c.type = kMessageTriggerType;
    c.variant = kMessageTriggerAtOnce;
    t.flag = kNoMessageFlag;
    t.message = message;
    t.lastStory = 0;
    return 1;
}

// a room's run: out of time -> the result; the hints at their ticks; true when the tick was taken
bool roomTimeAndHints(size_t room, uint16_t limit, uint16_t hurryTick, uint16_t lastTick) {
    FireMissionState& m = mission();
    if (missionTimer(room, limit)) {
        scriptRun(kResultScript);
        return true;
    }
    const uint16_t timer = m.roomTimers[room];
    if (timer == hurryTick) {
        m.hintTicks = 0;
        hint(kHintHurry);
        return true;
    }
    if (lastTick != 0 && timer == lastTick) {
        m.hintTicks = 0;
        hint(kHintLast);
        return true;
    }
    if (hintDue()) {
        hint(kHintIdle);
        return true;
    }
    return false;
}

void room0(uint8_t& step) { // exe 0x580750
    FireMissionState& m = mission();
    switch (Room0Step(step)) {
    case Room0Step::Spawn:
        FireMissionState::clearBlock();
        for (uint32_t f = kFirstLock; f <= kRoom0LastLock; ++f) setStoryFlag(f);
        spawnRecords(stateAddress(TableId::FireMissionRoomsRoom0Fires), kRoom0FireCount);
        m.firesLeft = uint8_t(kRoom0FireCount);
        eventStartAt(0, nullptr, kEventFlags, 0, 0);
        step += 2u;
        return;
    case Room0Step::Unused: ++step; return;
    case Room0Step::Brief:
        if (eventState().active != 0) return;
        hudBannerShow(kBriefingBannerX, kBriefingBannerY, kBriefingBanner);
        eventStartAt(0, nullptr, kBriefingFlags, 0, kBriefingMessage);
        ++step;
        return;
    case Room0Step::BriefEnd:
        if (eventState().active != 0) return;
        hudBanner().hide();
        player().inputMode = 0;
        ++step;
        return;
    case Room0Step::Run:
        if (roomTimeAndHints(0, kRoom0Limit, kRoom0Hint, 0)) return;
        if (m.firesLeft != 0) return;
        clearStoryFlag(kRoom0Door);
        ++step;
        return;
    default: return;
    }
}

void room1(uint8_t& step) { // exe 0x5808d0
    FireMissionState& m = mission();
    switch (Room1Step(step)) {
    case Room1Step::Spawn:
        m.firesLeft = 0;
        m.specialFireOut = 0;
        m.hintTicks = 0;
        m.dataFlags = 0;
        for (uint32_t f = kFirstLock; f <= kRoomLastLock; ++f) setStoryFlag(f);
        spawnRecords(stateAddress(TableId::FireMissionRoomsRoom1Fires), kRoom1FireCount);
        m.firesLeft = uint8_t(kRoom1FireCount);
        ++step;
        return;
    case Room1Step::Run:
        if (int16_t(m.specialFireOut) != 0) {
            spawnRecords(stateAddress(TableId::FireMissionRoomsRoom1LateFire), 1);
            ++m.firesLeft;
            ++step;
        }
        roomTimeAndHints(1, kRoom1Limit, kLateHint, 0);
        return;
    case Room1Step::LateFire:
        if (roomTimeAndHints(1, kRoom1Limit, kLateHint, 0)) return;
        if (m.firesLeft != 0) return;
        clearStoryFlag(kRoom1Door);
        ++step;
        return;
    default: return;
    }
}

void room2(uint8_t& step) { // exe 0x580a70
    FireMissionState& m = mission();
    switch (Room2Step(step)) {
    case Room2Step::Spawn:
        m.firesLeft = 0;
        m.specialFireOut = 0;
        m.hintTicks = 0;
        for (uint32_t f = kFirstLock; f <= kRoomLastLock; ++f) setStoryFlag(f);
        spawnRecords(stateAddress(TableId::FireMissionRoomsRoom2Fires), kRoom2FireCount);
        m.firesLeft = uint8_t(kRoom2FireCount);
        spawnRecords(stateAddress(TableId::FireMissionRoomsRoom2Data), 1);
        spawnRecords(stateAddress(TableId::FireMissionRoomsRoom2Message), 1);
        setStoryFlag(kHintShownFlag);
        m.dataFlags = FireMissionState::kDataPanicking;
        ++step;
        return;
    case Room2Step::WaitMessage:
        if (!storyFlag(kHintShownFlag)) {
            m.dataFlags = 0;
            ++step;
        }
        return;
    case Room2Step::Run:
        if (roomTimeAndHints(2, kRoom2Limit, kLateHint, kRoom2SecondHint)) return;
        if (m.firesLeft != 0) return;
        setStoryFlag(kMissionSuccessFlag);
        scriptRun(kResultScript);
        ++step;
        return;
    default: return;
    }
}

} // namespace

bool fireMissionRoomCaseKnown(uint8_t stage) { return stage == kST1E; }

bool fireMissionRoomCase() { // exe 0x580710
    const GameState& g = gameState();
    if (g.stage != kST1E) return false;
    fireMissionPointers().roomBlock = kFireMissionBlock;
    uint8_t& step = roomScriptState().handlerStep;
    const RoutineId h = roomHandlers->idAt(g.room);
    switch (h) {
    case RoutineId::FireMissionRoom0: room0(step); break;
    case RoutineId::FireMissionRoom1: room1(step); break;
    case RoutineId::FireMissionRoom2: room2(step); break;
    default: notPortedAt(kStoryPort, "ST1E room handler", roomHandlers->pointerAt(g.room));
    }
    return true;
}

} // namespace od::port
