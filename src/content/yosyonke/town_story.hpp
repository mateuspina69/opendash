// The story chapter after the crash landing (story 0): the scripts 0x4d (the junk shop), 0x4e (Roll runs off to Joe),
// 0x4f (Joe's lab) and 0x71 (the failed train defence), and the chapter's room-script handlers
// (content/yosyonke/town_story.cpp). Bit-exact against tests/data/story_*.bin (test_story). The chapter's classes:
// content/yosyonke/roll_follower, roll_spawners, st0d_critter, abandoned_mine_roll, town_story_cast, flutter_cabin
// (Roll in the Flutter); the snow: game/snow.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5f30e0's caller script 0x71 (the failed train defence): the white screen in every sub-state of its wrap up
bool trainDefenceWhiteScreen();
// The room-script handlers of the chapter (0x57c820 ST08 sub 1, 0x57cd40 ST0A sub 0, 0x57d5d0 / 0x57d600 / 0x57d630 /
// 0x57d660 ST0D, 0x57d760 ST0E, 0x57da20 ST0F sub 0 at story 0); run = false only asks whether fn is known
// (game/room_scripts roomScriptHandler)
bool townRoomHandler(RoutineId fn, bool run);
// Scripts 0x4d (exe 0x5305d0, ST0A: the junk-shop scene), 0x4e (exe 0x531030, ST09: Roll runs off to Joe), 0x4f (exe
// 0x531210, ST08 sub 1: Joe's lab), 0x71 (exe 0x53d220, ST0E: the train attack failed) -- called by game/story
// storyScriptTick for the ids it does not know
bool townStoryScriptPorted(uint8_t id);
bool townStoryScriptTick(const PlayerEnv& env, uint8_t id);

} // namespace od::port
