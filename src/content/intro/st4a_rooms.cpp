// The room handlers of the tutorial (see st4a_rooms.hpp). Each runs on the room scripts' handler step (game/room_scripts;
// the case runs the door test after the handler), keyed by the handler's routine.
//   sub 0       exe 0x58af30  the start room: its doors' locks, the first fight's records or the room reset after it,
//                             the return to the title at the tutorial's end
//   subs 1/3/5  exe 0x58b160  the doors unlocked, once
//   sub 2       exe 0x58b0e0  the doors unlocked; the second fight's record once enemy 1 or 2 is down
//   sub 4       exe 0x58b1a0  the doors locked until the lesson flag 0x3b2
//   sub 6       exe 0x58b210  the fight of the zone: its doors locked while it lasts
#include "content/intro/st4a_rooms.hpp"

#include "game/audio_logic.hpp"
#include "game/display.hpp"
#include "game/encounter.hpp"
#include "game/event_state.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/spawn.hpp"
#include "game/stage_files.hpp"
#include "game/story_flags.hpp"
#include "game/title.hpp"
#include "game/transition.hpp"

namespace od::port {

namespace {

constexpr RoutineId kStartRoomHandler = RoutineId::St4aRoomsStartRoomHandler,
                    kUnlockHandler = RoutineId::St4aRoomsUnlockHandler,
                    kSecondFightHandler = RoutineId::St4aRoomsSecondFightHandler,
                   kLessonHandler = RoutineId::St4aRoomsLessonHandler,
                   kZoneFightHandler = RoutineId::St4aRoomsZoneFightHandler;

// the doors' lock flags (game/story_flags kFlagLocked + the door)
constexpr uint32_t kLockFirst = kFlagLocked, kLockSecond = kFlagLocked + 1, kLockThird = kFlagLocked + 2;
// the tutorial's flags (the stage group: a stage load clears them)
constexpr uint32_t kFlagZoneCleared = 0x681;  // the zone's fight won
constexpr uint32_t kFlagLessonDoor = 0x682;   // sub 4's door held until the lesson
constexpr uint32_t kFlagZoneFight = 0x683;    // the zone's fight runs
constexpr uint32_t kFlagRoomReset = 0x684;    // the start room reset after the fight (its first lesson flag)
constexpr uint32_t kFlagSecondFight = 0x687;  // the second fight already started
constexpr uint32_t kFlagZoneReady = 0x68d;    // the zone's fight may start
constexpr uint32_t kFlagTutorialEnd = 0x68e;  // the tutorial is over: back to the title
// the lesson flags the room reset clears (after it set kFlagRoomReset)
constexpr uint32_t kLessonFlags[] = {kFlagRoomReset, 0x685, 0x686, kFlagSecondFight, 0x688, 0x68c,
                                     kFlagZoneReady, 0x68f, 0x690};
constexpr uint32_t kFlagLessonDone = 0x3b2;   // the lesson of sub 4 learnt (a story flag)
// the enemies of the start room revived by its reset (the defeated ids 1 .. 7)
constexpr uint32_t kFirstEnemy = 1, kLastEnemy = 7;
constexpr uint32_t kSecondFightEnemyA = 1, kSecondFightEnemyB = 2;
// the return to the title: the music faded out
constexpr int16_t kTitleMusicFadeStep = 0x111;

// the handler steps
enum class StartRoomStep : uint8_t { Enter = 0, AfterFight = 1, Running = 2 };
enum class ZoneStep : uint8_t { Enter = 0, Fight = 1, Done = 2 };
constexpr uint8_t kOnceDone = 1;

// the room handlers' step byte (game/room_scripts)
uint8_t& handlerStep() { return roomScriptState().handlerStep; }

// exe 0x58af30 (sub 0)
void startRoom() {
    switch (StartRoomStep(handlerStep())) {
    case StartRoomStep::Enter:
        clearStoryFlag(kLockFirst);
        clearStoryFlag(kFlagTutorialEnd);
        setStoryFlag(kLockSecond);
        setStoryFlag(kLockThird);
        if (storyFlag(kFlagZoneCleared)) {
            spawnRecordTracked(stateAddress(TableId::St4aRoomsResumeRecord));
            clearStoryFlag(kFlagRoomReset);
            handlerStep() = uint8_t(StartRoomStep::AfterFight);
        } else {
            spawnRecordTracked(stateAddress(TableId::St4aRoomsStartRecord));
            handlerStep() = uint8_t(StartRoomStep::Running);
        }
        break;
    case StartRoomStep::AfterFight:
        if (storyFlag(kFlagZoneCleared)) break;
        setStoryFlag(kFlagRoomReset);
        for (uint32_t f : kLessonFlags) clearStoryFlag(f);
        for (uint32_t id = kFirstEnemy; id <= kLastEnemy; ++id) enemyClearDefeated(id); // exe 0x5e63f0
        handlerStep() = uint8_t(StartRoomStep::Running);
        break;
    default: break;
    }
    if (storyFlag(kFlagRoomReset)) clearStoryFlag(kLockSecond);
    if (storyFlag(kFlagTutorialEnd) && display().drawBuffer != 0) {
        scriptFade(kScriptSkipWipe, 0);                        // exe 0x53f6b0
        soundFadeOut(kMusicFades, kTitleMusicFadeStep, 0);     // exe 0x5d5780
        fileLoads().clearLowFlags(kStreamBusy | kLoaderBit7);
        windowCloseAll(WindowClose::Force);                    // exe 0x5577d0
        padSetRepeat(kPadRepeatDelay, kPadRepeatRate);         // exe 0x55d1b0
        // and exe 0x604b06(the main task, exe 0x542ff0): the game task ends here (the app's title flow reacts to the
        // main task's mode)
        mainTask().enterMode(MainMode::ToTitle);
    }
}

// exe 0x58b160 (subs 1 / 3 / 5): the doors unlocked, once
void unlockOnce() {
    if (handlerStep() != 0) return;
    clearStoryFlag(kLockFirst);
    clearStoryFlag(kLockSecond);
    clearStoryFlag(kLockThird);
    ++handlerStep();
}

// exe 0x58b0e0 (sub 2)
void secondFight() {
    switch (handlerStep()) {
    case 0:
        clearStoryFlag(kLockFirst);
        clearStoryFlag(kLockSecond);
        clearStoryFlag(kLockThird);
        ++handlerStep();
        [[fallthrough]];
    case 1:
        if (!storyFlag(kFlagSecondFight) &&
            (enemyDefeated(kSecondFightEnemyA) || enemyDefeated(kSecondFightEnemyB))) { // exe 0x5e6420
            spawnRecordTracked(stateAddress(TableId::St4aRoomsSecondFightRecord));
            ++handlerStep();
        }
        return;
    default: return;
    }
}

// exe 0x58b1a0 (sub 4)
void lessonRoom() {
    if (handlerStep() == 0) {
        clearStoryFlag(kLockFirst);
        setStoryFlag(kLockSecond);
        setStoryFlag(kLockThird);
        setStoryFlag(kFlagLessonDoor);
        ++handlerStep();
    }
    if (storyFlag(kFlagLessonDone)) {
        clearStoryFlag(kFlagLessonDoor);
        clearStoryFlag(kLockSecond);
        clearStoryFlag(kLockThird);
    }
}

// exe 0x58b210 (sub 6)
void zoneFight() {
    switch (ZoneStep(handlerStep())) {
    case ZoneStep::Enter:
        clearStoryFlag(kLockFirst);
        setStoryFlag(kLockSecond);
        setStoryFlag(kLockThird);
        clearStoryFlag(kFlagZoneFight);
        if (storyFlag(kFlagZoneCleared)) {
            handlerStep() = uint8_t(ZoneStep::Done);
        } else if (eventState().active == 0 && storyFlag(kFlagZoneReady)) {
            setStoryFlag(kFlagZoneFight);
            handlerStep() = uint8_t(ZoneStep::Fight);
        }
        break;
    case ZoneStep::Fight:
        if (encounterZoneAlive() == 0) { // exe 0x5e67a0
            setStoryFlag(kFlagZoneCleared);
            clearStoryFlag(kFlagZoneFight);
            ++handlerStep();
        }
        break;
    default: break;
    }
    if (!storyFlag(kFlagZoneFight)) clearStoryFlag(kLockThird);
}

} // namespace

// the handler of the stage case's table (by its routine)
bool st4aRoomHandler(RoutineId fn, bool run) {
    switch (fn) {
    case kStartRoomHandler:
        if (run) startRoom();
        return true;
    case kUnlockHandler:
        if (run) unlockOnce();
        return true;
    case kSecondFightHandler:
        if (run) secondFight();
        return true;
    case kLessonHandler:
        if (run) lessonRoom();
        return true;
    case kZoneFightHandler:
        if (run) zoneFight();
        return true;
    default: return false;
    }
}

} // namespace od::port
