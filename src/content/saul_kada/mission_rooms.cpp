// The routines Saul Kada's mission rooms share (see mission_rooms.hpp).
#include "content/saul_kada/mission_rooms.hpp"

#include "game/loaded_data.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/encounter.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/hud.hpp"
#include "game/player/player_state.hpp"
#include "game/room_scripts.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

constexpr uint16_t kBannerX = 0x54;
constexpr uint32_t kOpeningParam = 8; // the event's param: window 8
constexpr uint8_t kSweepFreeze = kSweepToggleRun, kSweepRelease = kSweepSetRun;
constexpr uint32_t kMissionMusic = 4;
constexpr int16_t kMusicReturnStep = 0x222, kClearedDip = 0x111;
enum class Opening : uint8_t { Message = 0, Freeze = 1, Wait = 2, Done = 3 };

uint8_t& openingStep(OpeningStep step) {
    RoomScriptState& room = roomScriptState();
    return step == OpeningStep::HandlerByte1 ? room.handlerByte1 : room.handlerByte2;
}

} // namespace

bool missionOpening(bool counter, uint16_t message, OpeningStep which, uint16_t y) {
    uint8_t& step = openingStep(which);
    switch (Opening(step)) {
    case Opening::Message:
        if (counter) hudBannerShow(kBannerX, y, HudBannerStyle::MissionStart);
        eventStartAt(0, nullptr, kOpeningParam, 0, message); // exe 0x543b40(0, 0, 8, 0, message)
        step += 1u;
        return false;
    case Opening::Freeze:
        actorsSweep(kSweepActors, kSweepFreeze); // exe 0x5e05d0
        step += 1u;
        return false;
    case Opening::Wait:
        if (eventState().active != 0) return false;
        step += 1u;
        if (counter) hudBanner().hide(); // exe 0x5f2680
        player().inputMode = 0;
        actorsSweep(kSweepActors, kSweepRelease);
        return true;
    default: return true;
    }
}

void missionMusicDip(int16_t step) { soundFadeDip(kMissionMusic, step, 0, 0); } // exe 0x5d5830
void missionMusicBack() { soundFadeRestore(kMissionMusic, kMusicReturnStep, 0); } // exe 0x5d58a0

void groupClearedMusic(uint32_t flag) {
    if (storyFlag(flag) || encounterScriptSlotAlive() != 0) return; // exe 0x5e67d0
    setStoryFlag(flag);
    missionMusicDip(kClearedDip);
}

void defeatsFromFlags(uint32_t rows, int n) {
    for (int i = 0; i < n; ++i, rows += 4) {
        const DefeatRow row = dataRecord<DefeatRow>(rows, DefeatRow::kBytes);
        if (storyFlag(row.flag)) enemyMarkDefeated(row.enemy); // exe 0x5e63c0
    }
}

void flagsFromDefeats(uint32_t rows, int n) {
    for (int i = 0; i < n; ++i, rows += 4) {
        const DefeatRow row = dataRecord<DefeatRow>(rows, DefeatRow::kBytes);
        if (enemyDefeated(row.enemy)) setStoryFlag(row.flag);
    }
}

} // namespace od::port
