// The script block's and the stage block's parts (game/block_parts: game/script_state ScriptPart, game/stage_state
// StagePart) in the exe's layout: each part's members at their offsets in its block, by the part's type.
#include <algorithm>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "content/calbania/base_explosion_script_state.hpp"
#include "content/calbania/calbania_arrival_script_state.hpp"
#include "content/calbania/calbania_departure_script_state.hpp"
#include "content/calbania/calbania_town_scripts_state.hpp"
#include "content/calbania/ch3_story.hpp"
#include "content/calbania/nino_return_scripts_state.hpp"
#include "content/calbania/square_defence_scripts_state.hpp"
#include "content/calbania/st24_script56_state.hpp"
#include "content/calbania/st32_quiz.hpp"
#include "content/forbidden_island/elephant_boss_state.hpp"
#include "content/forbidden_island/st1c_script13.hpp"
#include "content/forbidden_island/st3e_reports_state.hpp"
#include "content/forbidden_island/st49_landing_state.hpp"
#include "content/intro/crash_landing.hpp"
#include "content/intro/fire_mission.hpp"
#include "content/intro/mission_result_state.hpp"
#include "content/intro/opening_script_state.hpp"
#include "content/intro/st02_attract_script_state.hpp"
#include "content/intro/st03_attract_script_state.hpp"
#include "content/intro/st39_script68_state.hpp"
#include "content/late_game/ch4_story_state.hpp"
#include "content/late_game/end_scripts_state.hpp"
#include "content/late_game/st4b_scripts_state.hpp"
#include "content/late_game/st4b_st4c_rooms_state.hpp"
#include "content/late_game/st4b_state.hpp"
#include "content/late_game/st4c_story_state.hpp"
#include "content/late_game/st53_story_state.hpp"
#include "content/late_game/st54_clear_screen_state.hpp"
#include "content/late_game/st59_state.hpp"
#include "content/manda/manda_ruin_scripts_state.hpp"
#include "content/manda/pokte_rooms_state.hpp"
#include "content/manda/st25_tron_scenes_state.hpp"
#include "content/nino/glyde_attack_cast_state.hpp"
#include "content/nino/st16_script33_state.hpp"
#include "content/nino/st18_gunships.hpp"
#include "content/saul_kada/fire_ruin_visit_scripts_state.hpp"
#include "content/saul_kada/pirate_base_scripts_state.hpp"
#include "content/saul_kada/race_bonus_state.hpp"
#include "content/saul_kada/st48_sand_script_state.hpp"
#include "content/yosyonke/abandoned_mine_exit_state.hpp"
#include "content/yosyonke/abandoned_mine_scripts.hpp"
#include "content/yosyonke/abandoned_mine_scripts_state.hpp"
#include "content/yosyonke/arrival_state.hpp"
#include "content/yosyonke/ice_ruin_scripts_state.hpp"
#include "content/yosyonke/st0e_vehicles.hpp"
#include "content/yosyonke/town_room_cases_state.hpp"
#include "content/yosyonke/town_side_scripts_state.hpp"
#include "content/yosyonke/town_story_state.hpp"
#include "game/exit_transitions_state.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"
#include "game/travel_state.hpp"
#include "translator/actor_layout.hpp"
#include "translator/block_parts_layout.hpp"

namespace od::test {

using namespace od::port;

namespace {

constexpr uint32_t kStageBlockAddress = 0xa6d298; // the stage block in the exe

void part0(LayoutVisitor& v, void* p0) { // od::port::base_explosion_script_state::BaseBlownBytes
    auto& s = *static_cast<od::port::base_explosion_script_state::BaseBlownBytes*>(p0);
    v.field(kScriptBlock + 0xc, s.whiteScreen);
}
void part1(LayoutVisitor& v, void* p0) { // od::port::calbania_arrival_script_state::ArrivalBytes
    auto& s = *static_cast<od::port::calbania_arrival_script_state::ArrivalBytes*>(p0);
    v.field(kScriptBlock + 0xc, s.restY);
    v.field(kScriptBlock + 0x1c, s.restTicks);
}
void part2(LayoutVisitor& v, void* p0) { // od::port::calbania_departure_script_state::DepartureBytes
    auto& s = *static_cast<od::port::calbania_departure_script_state::DepartureBytes*>(p0);
    v.field(kScriptBlock + 0xc, s.musicFaded);
}
void part3(LayoutVisitor& v, void* p0) { // od::port::calbania_town_scripts_state::TownReturnBytes
    auto& s = *static_cast<od::port::calbania_town_scripts_state::TownReturnBytes*>(p0);
    v.field(kScriptBlock + 0x10, s.lookYaw);
    v.field(kScriptBlock + 0x14, s.lookStep);
}
void part4(LayoutVisitor& v, void* p0) { // od::port::IslandBlownBytes
    auto& s = *static_cast<od::port::IslandBlownBytes*>(p0);
    v.field(kScriptBlock + 0xc, s.whiteScreen);
    v.field(kScriptBlock + 0xd, s.castStarted);
}
void part5(LayoutVisitor& v, void* p0) { // od::port::nino_return_scripts_state::AttackEndBytes
    auto& s = *static_cast<od::port::nino_return_scripts_state::AttackEndBytes*>(p0);
    v.field(kScriptBlock + 0xc, s.rippleInterval);
    v.field(kScriptBlock + 0x10, s.rippleCountdown);
    v.field(kScriptBlock + 0x12, s.rippleSize);
}
void part6(LayoutVisitor& v, void* p0) { // od::port::nino_return_scripts_state::RollToMachineBytes
    auto& s = *static_cast<od::port::nino_return_scripts_state::RollToMachineBytes*>(p0);
    v.field(kScriptBlock + 0xc, s.castStarted);
}
void part7(LayoutVisitor& v, void* p0) { // od::port::square_defence_scripts_state::SquareBytes
    auto& s = *static_cast<od::port::square_defence_scripts_state::SquareBytes*>(p0);
    v.field(kScriptBlock + 0xd, s.phaseHold);
    v.field(kScriptBlock + 0xf, s.liftCount);
    v.field(kScriptBlock + 0x10, s.unread4);
}
void part8(LayoutVisitor& v, void* p0) { // od::port::st24_script56_state::RiseBytes
    auto& s = *static_cast<od::port::st24_script56_state::RiseBytes*>(p0);
    v.field(kScriptBlock + 0xc, s.restY);
    v.field(kScriptBlock + 0x1c, s.restTicks);
}
void part9(LayoutVisitor& v, void* p0) { // od::port::QuizShowBlock
    auto& s = *static_cast<od::port::QuizShowBlock*>(p0);
    v.field(kStageBlockAddress + 0x0, s.course);
    v.field(kStageBlockAddress + 0x1, s.questionSet);
    v.field(kStageBlockAddress + 0x2, s.headPad);
    v.field(kStageBlockAddress + 0x4, s.state);
    v.field(kStageBlockAddress + 0x5, s.subState);
    v.field(kStageBlockAddress + 0x6, s.step);
    v.field(kStageBlockAddress + 0x7, s.subStep);
    v.field(kStageBlockAddress + 0x8, s.questions);
    v.field(kStageBlockAddress + 0x9, s.rightAnswers);
    v.field(kStageBlockAddress + 0xa, s.question);
    v.field(kStageBlockAddress + 0xb, s.doneFlags);
    v.field(kStageBlockAddress + 0xc, s.host);
    v.field(kStageBlockAddress + 0x10, s.messageBase);
    v.field(kStageBlockAddress + 0x11, s.misses);
    v.field(kStageBlockAddress + 0x12, s.rank);
    v.field(kStageBlockAddress + 0x14, s.savedPlace);
    for (uint32_t i = 0; i < s.contestants.size(); ++i) v.field(kStageBlockAddress + 0x1c + i * 4, s.contestants[i]);
}
void part10(LayoutVisitor& v, void* p0) { // od::port::elephant_boss_state::Script28Bytes
    auto& s = *static_cast<od::port::elephant_boss_state::Script28Bytes*>(p0);
    v.field(kScriptBlock + 0xc, s.fallPoint);
    v.field(kScriptBlock + 0x1c, s.waitTicks);
}
void part11(LayoutVisitor& v, void* p0) { // od::port::St1cScriptBytes
    auto& s = *static_cast<od::port::St1cScriptBytes*>(p0);
    v.field(kScriptBlock + 0x12, s.faceKeyCursors);
    v.field(kScriptBlock + 0x14, s.effect);
    v.field(kScriptBlock + 0x1c, s.waitTicks);
}
void part12(LayoutVisitor& v, void* p0) { // od::port::st3e_reports_state::Script64Bytes
    auto& s = *static_cast<od::port::st3e_reports_state::Script64Bytes*>(p0);
    v.field(kScriptBlock + 0xc, s.secondTalk);
}
void part13(LayoutVisitor& v, void* p0) { // od::port::st49_landing_state::Script18Bytes
    auto& s = *static_cast<od::port::st49_landing_state::Script18Bytes*>(p0);
    v.field(kScriptBlock + 0xc, s.destStage);
    v.field(kScriptBlock + 0xe, s.destRoom);
    v.field(kScriptBlock + 0x10, s.destination);
}
void part14(LayoutVisitor& v, void* p0) { // od::port::CrashScriptBytes
    auto& s = *static_cast<od::port::CrashScriptBytes*>(p0);
    v.field(kScriptBlock + 0xc, s.savedRng);
    v.field(kScriptBlock + 0x10, s.backdropX);
    v.field(kScriptBlock + 0x12, s.backdropY);
    v.field(kScriptBlock + 0x14, s.backdropSpeedX);
    v.field(kScriptBlock + 0x15, s.backdropSpeedY);
    v.field(kScriptBlock + 0x16, s.backdropShown);
    v.field(kScriptBlock + 0x17, s.secondFileLoaded);
    v.field(kScriptBlock + 0x18, s.walkSpeed);
    v.field(kScriptBlock + 0x1a, s.alarmOn);
    v.field(kScriptBlock + 0x1c, s.ticks);
}
void part15(LayoutVisitor& v, void* p0) { // od::port::FireMissionState
    auto& s = *static_cast<od::port::FireMissionState*>(p0);
    v.field(kStageBlockAddress + 0x0, s.heat);
    v.field(kStageBlockAddress + 0x4, s.firesLeft);
    v.field(kStageBlockAddress + 0x5, s.firesOut);
    v.field(kStageBlockAddress + 0x6, s.specialFireOut);
    v.field(kStageBlockAddress + 0x8, s.dataFlags);
    v.field(kStageBlockAddress + 0xa, s.roomTimers);
    v.field(kStageBlockAddress + 0x10, s.hintTicks);
}
void part16(LayoutVisitor& v, void* p0) { // od::port::mission_result_state::ResultScriptBytes
    auto& s = *static_cast<od::port::mission_result_state::ResultScriptBytes*>(p0);
    v.field(kScriptBlock + 0xc, s.countdown);
    v.field(kScriptBlock + 0x1c, s.ticks);
}
void part17(LayoutVisitor& v, void* p0) { // od::port::opening_script_state::OpeningScriptBytes
    auto& s = *static_cast<od::port::opening_script_state::OpeningScriptBytes*>(p0);
    v.field(kScriptBlock + 0xc, s.choice);
    v.field(kScriptBlock + 0xd, s.engineHum);
}
void part18(LayoutVisitor& v, void* p0) { // od::port::st02_attract_script_state::Script3Bytes
    auto& s = *static_cast<od::port::st02_attract_script_state::Script3Bytes*>(p0);
    v.field(kScriptBlock + 0x12, s.scrollTicks);
    v.field(kScriptBlock + 0x14, s.fadeQuadTicks);
    v.field(kScriptBlock + 0x15, s.flipQuadTicks);
}
void part19(LayoutVisitor& v, void* p0) { // od::port::st03_attract_script_state::Script4Bytes
    auto& s = *static_cast<od::port::st03_attract_script_state::Script4Bytes*>(p0);
    v.field(kScriptBlock + 0xc, s.ringCursor);
    v.field(kScriptBlock + 0x10, s.ringRadius);
    v.field(kScriptBlock + 0x11, s.engineSound);
    v.field(kScriptBlock + 0x12, s.loop);
    v.field(kScriptBlock + 0x13, s.loopHandle);
    v.field(kScriptBlock + 0x14, s.loopPan);
    v.field(kScriptBlock + 0x16, s.loopDip);
    v.field(kScriptBlock + 0x1c, s.sparkleClock);
}
void part20(LayoutVisitor& v, void* p0) { // od::port::st39_script68_state::Script68Bytes
    auto& s = *static_cast<od::port::st39_script68_state::Script68Bytes*>(p0);
    v.field(kScriptBlock + 0x1c, s.clock);
}
void part21(LayoutVisitor& v, void* p0) { // od::port::ch4_story_state::Script62Fields
    auto& s = *static_cast<od::port::ch4_story_state::Script62Fields*>(p0);
    v.field(kScriptBlock + 0x12, s.cue);
    v.field(kScriptBlock + 0x1c, s.counter);
}
void part22(LayoutVisitor& v, void* p0) { // od::port::end_scripts_state::SavedRngWalkFields
    auto& s = *static_cast<od::port::end_scripts_state::SavedRngWalkFields*>(p0);
    v.field(kScriptBlock + 0xc, s.savedRng);
    v.field(kScriptBlock + 0x10, s.walk);
}
void part23(LayoutVisitor& v, void* p0) { // od::port::end_scripts_state::Script3fFields
    auto& s = *static_cast<od::port::end_scripts_state::Script3fFields*>(p0);
    v.field(kScriptBlock + 0xc, s.savedRng);
    v.field(kScriptBlock + 0x10, s.walk);
    v.field(kScriptBlock + 0x12, s.flip);
    v.field(kScriptBlock + 0x13, s.flipRow);
}
void part24(LayoutVisitor& v, void* p0) { // od::port::end_scripts_state::Script6eFields
    auto& s = *static_cast<od::port::end_scripts_state::Script6eFields*>(p0);
    v.field(kScriptBlock + 0xc, s.nextUnit);
    v.field(kScriptBlock + 0xe, s.fade);
    v.field(kScriptBlock + 0x10, s.page);
    v.field(kScriptBlock + 0x14, s.pageBrightness);
    v.field(kScriptBlock + 0x15, s.shown);
    v.field(kScriptBlock + 0x16, s.background);
    v.field(kScriptBlock + 0x18, s.ramp);
    v.field(kScriptBlock + 0x19, s.ticks);
    v.field(kScriptBlock + 0x1a, s.units);
}
void part25(LayoutVisitor& v, void* p0) { // od::port::end_scripts_state::Script6fFields
    auto& s = *static_cast<od::port::end_scripts_state::Script6fFields*>(p0);
    v.field(kScriptBlock + 0x10, s.grey);
    v.field(kScriptBlock + 0x11, s.level);
}
void part26(LayoutVisitor& v, void* p0) { // od::port::end_scripts_state::Script75Fields
    auto& s = *static_cast<od::port::end_scripts_state::Script75Fields*>(p0);
    v.field(kScriptBlock + 0xc, s.counter);
    v.field(kScriptBlock + 0xe, s.poseTicks);
    v.field(kScriptBlock + 0x10, s.side);
    v.field(kScriptBlock + 0x11, s.frame);
    v.field(kScriptBlock + 0x12, s.cleared);
    v.field(kScriptBlock + 0x14, s.shakeTicks);
}
void part27(LayoutVisitor& v, void* p0) { // od::port::st4b_scripts_state::St4bScriptFields
    auto& s = *static_cast<od::port::st4b_scripts_state::St4bScriptFields*>(p0);
    v.field(kScriptBlock + 0xc, s.savedRng);
}
void part28(LayoutVisitor& v, void* p0) { // od::port::st4b_st4c_rooms_state::Script59Fields
    auto& s = *static_cast<od::port::st4b_st4c_rooms_state::Script59Fields*>(p0);
    v.field(kScriptBlock + 0x10, s.prop);
    v.field(kScriptBlock + 0x14, s.shake);
    v.field(kScriptBlock + 0x18, s.loaded);
    v.field(kScriptBlock + 0x19, s.propSpawned);
    v.field(kScriptBlock + 0x1a, s.faceRows);
}
void part29(LayoutVisitor& v, void* p0) { // od::port::st4b_state::Script59Cue
    auto& s = *static_cast<od::port::st4b_state::Script59Cue*>(p0);
    v.field(kScriptBlock + 0x1c, s.cue);
}
void part30(LayoutVisitor& v, void* p0) { // od::port::st4c_story_state::St4cScriptFields
    auto& s = *static_cast<od::port::st4c_story_state::St4cScriptFields*>(p0);
    v.field(kScriptBlock + 0xc, s.savedRng);
    v.field(kScriptBlock + 0x10, s.clearedA);
    v.field(kScriptBlock + 0x12, s.clearedB);
    v.field(kScriptBlock + 0x14, s.rumble);
}
void part31(LayoutVisitor& v, void* p0) { // od::port::st53_story_state::St53ScriptFields
    auto& s = *static_cast<od::port::st53_story_state::St53ScriptFields*>(p0);
    v.field(kScriptBlock + 0x12, s.talkCue);
    v.field(kScriptBlock + 0x13, s.datLoaded);
    v.field(kScriptBlock + 0x1c, s.waitTicks);
}
void part32(LayoutVisitor& v, void* p0) { // od::port::st54_clear_screen_state::ClearScreenFields
    auto& s = *static_cast<od::port::st54_clear_screen_state::ClearScreenFields*>(p0);
    v.field(kScriptBlock + 0xc, s.hours);
    v.field(kScriptBlock + 0xd, s.minutes);
    v.field(kScriptBlock + 0xe, s.seconds);
}
void part33(LayoutVisitor& v, void* p0) { // od::port::st59_state::RaceBlock
    auto& s = *static_cast<od::port::st59_state::RaceBlock*>(p0);
    v.field(kStageBlockAddress + 0x0, s.time);
    v.field(kStageBlockAddress + 0x4, s.countdown);
    v.field(kStageBlockAddress + 0x8, s.gauge);
    v.field(kStageBlockAddress + 0xc, s.watcher);
    v.field(kStageBlockAddress + 0x10, s.wait);
    v.field(kStageBlockAddress + 0x12, s.freeze);
    v.field(kStageBlockAddress + 0x14, s.progress);
    v.field(kStageBlockAddress + 0x15, s.course);
    v.field(kStageBlockAddress + 0x16, s.tickSoundTimer);
    v.field(kStageBlockAddress + 0x18, s.jitterRow);
    v.field(kStageBlockAddress + 0x19, s.jitterColumn);
}
void part34(LayoutVisitor& v, void* p0) { // od::port::manda_ruin_scripts_state::RuinScriptBytes
    auto& s = *static_cast<od::port::manda_ruin_scripts_state::RuinScriptBytes*>(p0);
    v.field(kScriptBlock + 0xc, s.datLoaded);
}
void part35(LayoutVisitor& v, void* p0) { // od::port::manda_ruin_scripts_state::TerminalScriptBytes
    auto& s = *static_cast<od::port::manda_ruin_scripts_state::TerminalScriptBytes*>(p0);
    v.field(kScriptBlock + 0x12, s.walkSpeed);
    v.field(kScriptBlock + 0x14, s.sparkAngle);
}
void part36(LayoutVisitor& v, void* p0) { // od::port::pokte_rooms_state::PokteVillageState
    auto& s = *static_cast<od::port::pokte_rooms_state::PokteVillageState*>(p0);
    v.field(kStageBlockAddress + 0x20, s.repairCost);
}
void part37(LayoutVisitor& v, void* p0) { // od::port::st25_tron_scenes_state::PokteScriptBytes
    auto& s = *static_cast<od::port::st25_tron_scenes_state::PokteScriptBytes*>(p0);
    v.field(kScriptBlock + 0x12, s.talkPose);
    v.field(kScriptBlock + 0x14, s.walkSpeed);
    v.field(kScriptBlock + 0x16, s.brokenArms);
}
void part39(LayoutVisitor& v, void* p0) { // od::port::st16_script33_state::CatchScriptFields
    auto& s = *static_cast<od::port::st16_script33_state::CatchScriptFields*>(p0);
    v.field(kScriptBlock + 0xc, s.carrier);
    v.field(kScriptBlock + 0x10, s.startX);
    v.field(kScriptBlock + 0x12, s.startZ);
}
void part40(LayoutVisitor& v, void* p0) { // od::port::ShipFightBlock
    auto& s = *static_cast<od::port::ShipFightBlock*>(p0);
    v.field(kStageBlockAddress + 0x0, s.freeIds);
    v.field(kStageBlockAddress + 0x4, s.dropped);
    v.field(kStageBlockAddress + 0x8, s.spare);
}
void part41(LayoutVisitor& v, void* p0) { // od::port::fire_ruin_visit_scripts_state::VisitScriptFields
    auto& s = *static_cast<od::port::fire_ruin_visit_scripts_state::VisitScriptFields*>(p0);
    v.field(kScriptBlock + 0xc, s.talkTicks);
}
void part42(LayoutVisitor& v, void* p0) { // od::port::pirate_base_scripts_state::BaseScriptFields
    auto& s = *static_cast<od::port::pirate_base_scripts_state::BaseScriptFields*>(p0);
    v.field(kScriptBlock + 0xd, s.opened);
}
void part43(LayoutVisitor& v, void* p0) { // od::port::race_bonus_state::RaceClock
    auto& s = *static_cast<od::port::race_bonus_state::RaceClock*>(p0);
    v.field(kStageBlockAddress + 0x12, s.frozenTicks);
}
void part44(LayoutVisitor& v, void* p0) { // od::port::st48_sand_script_state::SandScriptFields
    auto& s = *static_cast<od::port::st48_sand_script_state::SandScriptFields*>(p0);
    v.field(kScriptBlock + 0xc, s.restHeight);
    v.field(kScriptBlock + 0x1c, s.holdTicks);
}
void part45(LayoutVisitor& v, void* p0) { // od::port::abandoned_mine_exit_state::RuinExitBytes
    auto& s = *static_cast<od::port::abandoned_mine_exit_state::RuinExitBytes*>(p0);
    v.field(kScriptBlock + 0x10, s.countdown);
}
void part46(LayoutVisitor& v, void* p0) { // od::port::AbandonedMineStageBlock
    auto& s = *static_cast<od::port::AbandonedMineStageBlock*>(p0);
    v.field(kStageBlockAddress + 0x0, s.guardian);
    v.field(kStageBlockAddress + 0x4, s.roll);
}
void part47(LayoutVisitor& v, void* p0) { // od::port::abandoned_mine_scripts_state::RuinScriptBytes
    auto& s = *static_cast<od::port::abandoned_mine_scripts_state::RuinScriptBytes*>(p0);
    v.field(kScriptBlock + 0x12, s.musicWait);
    v.field(kScriptBlock + 0x14, s.turnTarget);
    v.field(kScriptBlock + 0x16, s.headTarget);
    v.field(kScriptBlock + 0x18, s.rate);
    v.field(kScriptBlock + 0x1a, s.lifting);
}
void part48(LayoutVisitor& v, void* p0) { // od::port::arrival_state::CabinScriptBytes
    auto& s = *static_cast<od::port::arrival_state::CabinScriptBytes*>(p0);
    v.field(kScriptBlock + 0xf, s.delay);
}
void part49(LayoutVisitor& v, void* p0) { // od::port::arrival_state::DescentScriptBytes
    auto& s = *static_cast<od::port::arrival_state::DescentScriptBytes*>(p0);
    v.field(kScriptBlock + 0xc, s.landingY);
    v.field(kScriptBlock + 0xe, s.landingZ);
    v.field(kScriptBlock + 0x1c, s.ticks);
}
void part50(LayoutVisitor& v, void* p0) { // od::port::ice_ruin_scripts_state::CritterSceneBytes
    auto& s = *static_cast<od::port::ice_ruin_scripts_state::CritterSceneBytes*>(p0);
    v.field(kScriptBlock + 0xc, s.walkEndX);
    v.field(kScriptBlock + 0xe, s.walkEndY);
    v.field(kScriptBlock + 0x10, s.walkEndZ);
}
void part51(LayoutVisitor& v, void* p0) { // od::port::VehicleChaseBlock
    auto& s = *static_cast<od::port::VehicleChaseBlock*>(p0);
    v.field(kStageBlockAddress + 0x0, s.carriedHp);
}
void part52(LayoutVisitor& v, void* p0) { // od::port::town_room_cases_state::LicenceOfficeBlock
    auto& s = *static_cast<od::port::town_room_cases_state::LicenceOfficeBlock*>(p0);
    v.field(kStageBlockAddress + 0x0, s.timeUpFlash);
}
void part53(LayoutVisitor& v, void* p0) { // od::port::town_side_scripts_state::ExamDoorsBytes
    auto& s = *static_cast<od::port::town_side_scripts_state::ExamDoorsBytes*>(p0);
    v.field(kScriptBlock + 0x12, s.destinationRoom);
    v.field(kScriptBlock + 0x14, s.doorSegmentA);
    v.field(kScriptBlock + 0x16, s.doorSegmentB);
}
void part54(LayoutVisitor& v, void* p0) { // od::port::town_side_scripts_state::VisitorBytes
    auto& s = *static_cast<od::port::town_side_scripts_state::VisitorBytes*>(p0);
    v.field(kScriptBlock + 0xc, s.timer);
    v.field(kScriptBlock + 0xe, s.doorSegment);
    v.field(kScriptBlock + 0x10, s.soundTimer);
}
void part55(LayoutVisitor& v, void* p0) { // od::port::town_story_state::ChapterScriptBytes
    auto& s = *static_cast<od::port::town_story_state::ChapterScriptBytes*>(p0);
    v.field(kScriptBlock + 0x12, s.counter);
    v.field(kScriptBlock + 0x16, s.soundTimer);
}
void part56(LayoutVisitor& v, void* p0) { // od::port::exit_transitions_state::DoorScriptBytes
    auto& s = *static_cast<od::port::exit_transitions_state::DoorScriptBytes*>(p0);
    v.field(kScriptBlock + 0x14, s.doorSegment);
    v.field(kScriptBlock + 0x16, s.farSegment);
    v.field(kScriptBlock + 0x18, s.otherStage);
}
void part57(LayoutVisitor& v, void* p0) { // od::port::exit_transitions_state::DoorActorScriptBytes
    auto& s = *static_cast<od::port::exit_transitions_state::DoorActorScriptBytes*>(p0);
    v.field(kScriptBlock + 0xc, s.doorSlot);
    v.field(kScriptBlock + 0xd, s.exitIndex);
    v.field(kScriptBlock + 0xe, s.sinkSpeed);
}
void part58(LayoutVisitor& v, void* p0) { // od::port::exit_transitions_state::ClimbScriptBytes
    auto& s = *static_cast<od::port::exit_transitions_state::ClimbScriptBytes*>(p0);
    v.field(kScriptBlock + 0xc, s.climbSpeed);
    v.field(kScriptBlock + 0x1c, s.ticksLeft);
}
void part59(LayoutVisitor& v, void* p0) { // od::port::exit_transitions_state::TeleportScriptBytes
    auto& s = *static_cast<od::port::exit_transitions_state::TeleportScriptBytes*>(p0);
    v.field(kScriptBlock + 0xc, s.sparkPoint);
    v.field(kScriptBlock + 0x14, s.padSide);
    v.field(kScriptBlock + 0x16, s.savedRng);
    v.field(kScriptBlock + 0x1c, s.ticks);
}
void part60(LayoutVisitor& v, void* p0) { // od::port::CastTimelineTick
    auto& s = *static_cast<od::port::CastTimelineTick*>(p0);
    v.field(kScriptBlock + 0xc, s.stepTick);
}
void part61(LayoutVisitor& v, void* p0) { // od::port::CastTimelineStep
    auto& s = *static_cast<od::port::CastTimelineStep*>(p0);
    v.field(kScriptBlock + 0x10, s.stepId);
    v.field(kScriptBlock + 0x11, s.stepSub);
}
void part62(LayoutVisitor& v, void* p0) { // od::port::ScriptCastStart
    auto& s = *static_cast<od::port::ScriptCastStart*>(p0);
    v.field(kScriptBlock + 0xc, s.started);
}
void part63(LayoutVisitor& v, void* p0) { // od::port::ScriptHeadTarget
    auto& s = *static_cast<od::port::ScriptHeadTarget*>(p0);
    v.field(kScriptBlock + 0x10, s.yaw);
    v.field(kScriptBlock + 0x14, s.step);
}
void part64(LayoutVisitor& v, void* p0) { // od::port::StageBlockHead
    auto& s = *static_cast<od::port::StageBlockHead*>(p0);
    v.field(kStageBlockAddress + 0x0, s.word);
}
void part65(LayoutVisitor& v, void* p0) { // od::port::travel_state::TravelMap
    auto& s = *static_cast<od::port::travel_state::TravelMap*>(p0);
    v.field(kScriptBlock + 0xc, s.flags);
    v.field(kScriptBlock + 0xd, s.destination);
    v.field(kScriptBlock + 0xe, s.destStage);
    v.field(kScriptBlock + 0xf, s.destRoom);
    v.field(kScriptBlock + 0x10, s.destPlace);
    v.field(kScriptBlock + 0x18, s.cameraX);
    v.field(kScriptBlock + 0x1a, s.cameraZ);
    v.field(kScriptBlock + 0x34, s.hintShown);
    v.field(kScriptBlock + 0x35, s.hintTicks);
    v.field(kScriptBlock + 0x36, s.idleTicks);
    v.field(kScriptBlock + 0x37, s.picked);
    v.field(kScriptBlock + 0x38, s.map);
    v.field(kScriptBlock + 0x39, s.storyMarker);
    v.field(kScriptBlock + 0x3a, s.island);
    v.field(kScriptBlock + 0x3b, s.ready);
    v.field(kScriptBlock + 0x3c, s.arrivalFade);
    v.field(kScriptBlock + 0x4c, s.iconX);
    v.field(kScriptBlock + 0x4e, s.iconZ);
    v.field(kScriptBlock + 0x50, s.prevIconX);
    v.field(kScriptBlock + 0x52, s.prevIconZ);
    v.field(kScriptBlock + 0x54, s.leadX);
    v.field(kScriptBlock + 0x56, s.leadZ);
    v.field(kScriptBlock + 0x5c, s.secondX);
    v.field(kScriptBlock + 0x5e, s.secondZ);
    v.field(kScriptBlock + 0x60, s.prevSecondX);
    v.field(kScriptBlock + 0x62, s.prevSecondZ);
    v.field(kScriptBlock + 0x64, s.secondLeadX);
    v.field(kScriptBlock + 0x66, s.secondLeadZ);
}

using PartLayout = void (*)(LayoutVisitor&, void*);
const std::unordered_map<std::type_index, PartLayout>& partLayouts() {
    static const std::unordered_map<std::type_index, PartLayout> m = {
        {typeid(od::port::base_explosion_script_state::BaseBlownBytes), &part0},
        {typeid(od::port::calbania_arrival_script_state::ArrivalBytes), &part1},
        {typeid(od::port::calbania_departure_script_state::DepartureBytes), &part2},
        {typeid(od::port::calbania_town_scripts_state::TownReturnBytes), &part3},
        {typeid(od::port::IslandBlownBytes), &part4},
        {typeid(od::port::nino_return_scripts_state::AttackEndBytes), &part5},
        {typeid(od::port::nino_return_scripts_state::RollToMachineBytes), &part6},
        {typeid(od::port::square_defence_scripts_state::SquareBytes), &part7},
        {typeid(od::port::st24_script56_state::RiseBytes), &part8},
        {typeid(od::port::QuizShowBlock), &part9},
        {typeid(od::port::elephant_boss_state::Script28Bytes), &part10},
        {typeid(od::port::St1cScriptBytes), &part11},
        {typeid(od::port::st3e_reports_state::Script64Bytes), &part12},
        {typeid(od::port::st49_landing_state::Script18Bytes), &part13},
        {typeid(od::port::CrashScriptBytes), &part14},
        {typeid(od::port::FireMissionState), &part15},
        {typeid(od::port::mission_result_state::ResultScriptBytes), &part16},
        {typeid(od::port::opening_script_state::OpeningScriptBytes), &part17},
        {typeid(od::port::st02_attract_script_state::Script3Bytes), &part18},
        {typeid(od::port::st03_attract_script_state::Script4Bytes), &part19},
        {typeid(od::port::st39_script68_state::Script68Bytes), &part20},
        {typeid(od::port::ch4_story_state::Script62Fields), &part21},
        {typeid(od::port::end_scripts_state::SavedRngWalkFields), &part22},
        {typeid(od::port::end_scripts_state::Script3fFields), &part23},
        {typeid(od::port::end_scripts_state::Script6eFields), &part24},
        {typeid(od::port::end_scripts_state::Script6fFields), &part25},
        {typeid(od::port::end_scripts_state::Script75Fields), &part26},
        {typeid(od::port::st4b_scripts_state::St4bScriptFields), &part27},
        {typeid(od::port::st4b_st4c_rooms_state::Script59Fields), &part28},
        {typeid(od::port::st4b_state::Script59Cue), &part29},
        {typeid(od::port::st4c_story_state::St4cScriptFields), &part30},
        {typeid(od::port::st53_story_state::St53ScriptFields), &part31},
        {typeid(od::port::st54_clear_screen_state::ClearScreenFields), &part32},
        {typeid(od::port::st59_state::RaceBlock), &part33},
        {typeid(od::port::manda_ruin_scripts_state::RuinScriptBytes), &part34},
        {typeid(od::port::manda_ruin_scripts_state::TerminalScriptBytes), &part35},
        {typeid(od::port::pokte_rooms_state::PokteVillageState), &part36},
        {typeid(od::port::st25_tron_scenes_state::PokteScriptBytes), &part37},
        {typeid(od::port::st16_script33_state::CatchScriptFields), &part39},
        {typeid(od::port::ShipFightBlock), &part40},
        {typeid(od::port::fire_ruin_visit_scripts_state::VisitScriptFields), &part41},
        {typeid(od::port::pirate_base_scripts_state::BaseScriptFields), &part42},
        {typeid(od::port::race_bonus_state::RaceClock), &part43},
        {typeid(od::port::st48_sand_script_state::SandScriptFields), &part44},
        {typeid(od::port::abandoned_mine_exit_state::RuinExitBytes), &part45},
        {typeid(od::port::AbandonedMineStageBlock), &part46},
        {typeid(od::port::abandoned_mine_scripts_state::RuinScriptBytes), &part47},
        {typeid(od::port::arrival_state::CabinScriptBytes), &part48},
        {typeid(od::port::arrival_state::DescentScriptBytes), &part49},
        {typeid(od::port::ice_ruin_scripts_state::CritterSceneBytes), &part50},
        {typeid(od::port::VehicleChaseBlock), &part51},
        {typeid(od::port::town_room_cases_state::LicenceOfficeBlock), &part52},
        {typeid(od::port::town_side_scripts_state::ExamDoorsBytes), &part53},
        {typeid(od::port::town_side_scripts_state::VisitorBytes), &part54},
        {typeid(od::port::town_story_state::ChapterScriptBytes), &part55},
        {typeid(od::port::exit_transitions_state::DoorScriptBytes), &part56},
        {typeid(od::port::exit_transitions_state::DoorActorScriptBytes), &part57},
        {typeid(od::port::exit_transitions_state::ClimbScriptBytes), &part58},
        {typeid(od::port::exit_transitions_state::TeleportScriptBytes), &part59},
        {typeid(od::port::CastTimelineTick), &part60},
        {typeid(od::port::CastTimelineStep), &part61},
        {typeid(od::port::ScriptCastStart), &part62},
        {typeid(od::port::ScriptHeadTarget), &part63},
        {typeid(od::port::StageBlockHead), &part64},
        {typeid(od::port::travel_state::TravelMap), &part65},
    };
    return m;
}

template <class Base> void layoutPart(LayoutVisitor& v, Base& part) {
    const auto it = partLayouts().find(typeid(part));
    if (it == partLayouts().end()) unknownBlockPart(typeid(part).name());
    it->second(v, &part);
}


// ---- the blocks' retained bytes and the parts' lifetimes ---------------------------------------------------------
// Each block's own bytes as the exe keeps them between its owners (the script's +0xc..+0x1f, the stage block): loaded
// with the image, stored back before the held parts; a part made loads its members from them, a part dropped leaves
// its members in them, a clear zeroes them.
struct RetainedRange {
    uint32_t address;
    std::vector<uint8_t> bytes;
};
RetainedRange scriptBytesRange{kScriptBlock + 0xc, std::vector<uint8_t>(0x14)};
RetainedRange stageBytesRange{kStageBlockAddress, std::vector<uint8_t>(0x40)};

// a visitor over the retained bytes (loading: into the part; else: from it)
class Retained final : public LayoutVisitor {
public:
    Retained(RetainedRange& r, bool load) : r_(r), load_(load) {}
    bool loading() const override { return load_; }
    bool storing() const override { return !load_; }
    void bytes(uint32_t address, void* p, size_t n) override {
        for (size_t i = 0; i < n; ++i) {
            const uint32_t a = address + uint32_t(i);
            if (a < r_.address || a >= r_.address + r_.bytes.size()) continue;
            uint8_t& b = r_.bytes[a - r_.address];
            if (load_) static_cast<uint8_t*>(p)[i] = b;
            else b = static_cast<uint8_t*>(p)[i];
        }
    }

private:
    RetainedRange& r_;
    bool load_;
};

// The script block's bytes as the native state holds them (a part's members past the retained bytes lie over the
// block's shared fields: the actor records): the shared fields stored into it, the retained bytes over them
constexpr uint32_t kScriptWindowSize = 0xc0;
class ScriptWindow final : public LayoutVisitor {
public:
    explicit ScriptWindow(bool load) : load_(load) {}
    bool loading() const override { return load_; }
    bool storing() const override { return !load_; }
    bool strictSlots() const override { return false; }
    void bytes(uint32_t address, void* p, size_t n) override {
        for (size_t i = 0; i < n; ++i) {
            const uint32_t a = address + uint32_t(i);
            if (a < kScriptBlock || a >= kScriptBlock + kScriptWindowSize) continue;
            uint8_t& b = bytes_[a - kScriptBlock];
            if (load_) static_cast<uint8_t*>(p)[i] = b;
            else b = static_cast<uint8_t*>(p)[i];
        }
    }
    void pointer(uint32_t a, void*& p, const std::type_info& t) override {
        if (a < kScriptBlock || a + 4 > kScriptBlock + kScriptWindowSize) return;
        uint32_t word = load_ ? 0 : nativePointerWord(a, p, t);
        bytes(a, &word, 4);
        if (load_) resolveNativePointer(a, word, t, p);
    }
    void setLoading(bool load) { load_ = load; }
    // the shared fields' bytes, then the retained ones over them
    void fill() {
        const bool was = load_;
        load_ = false;
        stageStateLayout(*this);
        std::copy(scriptBytesRange.bytes.begin(), scriptBytesRange.bytes.end(),
                  bytes_.begin() + (scriptBytesRange.address - kScriptBlock));
        load_ = was;
    }

private:
    bool load_;
    std::vector<uint8_t> bytes_ = std::vector<uint8_t>(kScriptWindowSize);
};

class ScriptPartsObserver final : public BlockPartsObserver<ScriptBytesPart> {
public:
    // a part made takes the block's bytes (the retained ones, and past them the shared fields' that it lies over)
    void created(ScriptBytesPart& part) override {
        ScriptWindow w(true);
        w.fill();
        layoutPart(w, part);
    }
    // a part dropped leaves its members in the bytes: the retained ones, and the shared fields it lay over
    void dropped(ScriptBytesPart& part) override {
        Retained store(scriptBytesRange, false);
        layoutPart(store, part);
        ScriptWindow w(false);
        w.fill();
        layoutPart(w, part);
        w.setLoading(true);
        stageStateLayout(w);
    }
    void bytesCleared(unsigned which) override {
        auto& b = scriptBytesRange.bytes;
        switch (ScriptBytesClear(which)) {
        case ScriptBytesClear::All: std::fill(b.begin(), b.end(), uint8_t(0)); break;
        case ScriptBytesClear::SetUpRun: std::fill(b.begin(), b.begin() + 0x10, uint8_t(0)); break;
        case ScriptBytesClear::StageLoadWords:
            std::fill(b.begin(), b.begin() + 4, uint8_t(0));
            std::fill(b.begin() + 0x10, b.end(), uint8_t(0));
            break;
        }
    }
};
class StagePartsObserver final : public BlockPartsObserver<StageBlockPart> {
public:
    void created(StageBlockPart& part) override {
        Retained load(stageBytesRange, true);
        layoutPart(load, part);
    }
    void dropped(StageBlockPart& part) override {
        Retained store(stageBytesRange, false);
        layoutPart(store, part);
    }
    void bytesCleared(unsigned which) override {
        if (StageBlockClear(which) == StageBlockClear::All)
            std::fill(stageBytesRange.bytes.begin(), stageBytesRange.bytes.end(), uint8_t(0));
    }
};
ScriptPartsObserver scriptObserver;
StagePartsObserver stageObserver;

} // namespace

void blockPartsLayout(LayoutVisitor& v) {
    BlockParts<ScriptBytesPart>::setObserver(&scriptObserver);
    BlockParts<StageBlockPart>::setObserver(&stageObserver);
    // the retained bytes (a load: from the image; a store: into it), then the held parts over them
    v.bytes(scriptBytesRange.address, scriptBytesRange.bytes.data(), scriptBytesRange.bytes.size());
    v.bytes(stageBytesRange.address, stageBytesRange.bytes.data(), stageBytesRange.bytes.size());
    scriptState().scriptBytes.forEach([&](ScriptBytesPart& p) { layoutPart(v, p); });
    stageBlock().parts.forEach([&](StageBlockPart& p) { layoutPart(v, p); });
    v.field(kStageBlockAddress + 0x28, stageBlock().unwritten28);
}

[[noreturn]] void unknownBlockPart(const char* name) {
    throw std::runtime_error(std::string("translator: a block part with no layout: ") + name);
}

} // namespace od::test

