// The routines Saul Kada's mission rooms share (the town's ST29 / 2A / 2B, the pirate base ST3B, the fire ruin ST27 /
// ST28), see content/saul_kada/mission_rooms.cpp.
#pragma once

#include <cstdint>

#include "game/loaded_data.hpp"

namespace od::port {

// the room handler's byte a mission opening keeps its step in
enum class OpeningStep { HandlerByte1, HandlerByte2 };
// exe 0x583930 / 0x584350 / 0x582f50 / 0x5835f0 / 0x588340: the mission's opening -- (with the counter: the mission
// banner at y) the message of window 8, every enemy frozen, then once no event runs (the banner hidden) the player's
// input back and the enemies released; true when done
bool missionOpening(bool counter, uint16_t message, OpeningStep step = OpeningStep::HandlerByte1, uint16_t y = 0x30);
// the mission music's dip (exe 0x5d5830(4, step, 0, 0)) and its return (exe 0x5d58a0(4, 0x222, 0))
void missionMusicDip(int16_t step);
void missionMusicBack();
// the mission music ends once the script slot's group is down: the flag, the music dips 0x111
void groupClearedMusic(uint32_t flag);
// A row of a room's defeat table: an enemy id and the story flag that keeps its defeat
struct DefeatRow : DataRecord {
    static constexpr uint32_t kBytes = 0x4;
    uint16_t enemy = 0; // +0x0
    uint16_t flag = 0; // +0x2
    template <class R> static DefeatRow load(const R& r) {
        DefeatRow v;
        v.enemy = r.template read<uint16_t>(0x0);
        v.flag = r.template read<uint16_t>(0x2);
        return v;
    }
};
// the rows' flags set -> their enemies defeated / their enemies defeated -> their flags (exe 0x587fc0 ...)
void defeatsFromFlags(uint32_t rows, int n);
void flagsFromDefeats(uint32_t rows, int n);

} // namespace od::port
