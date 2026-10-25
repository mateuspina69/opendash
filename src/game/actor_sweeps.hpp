// The sweeps over every actor pool: the class-wide frees and run / shown flag changes the transitions, the scripts and
// the room setup make (exe 0x5e05d0, its flag change 0x5e0890), the in-stage warp's move of the kept actors (exe
// 0x5e08d0 with 0x5e0b30 / 0x5e0b80) and the room setup's kill of every actor (exe 0x5d5b30's part).
#pragma once

#include <cstdint>

namespace od::port {

// The class bits of a sweep (the pools and, for the animated / static / effect pools, the slot's group: loop flags & 3)
constexpr uint32_t kSweepPlayer = 0x1;
constexpr uint32_t kSweepAnimatedGroup0 = 0x2, kSweepAnimatedGroup2 = 0x4;
constexpr uint32_t kSweepShots = 0x8;
constexpr uint32_t kSweepProjectiles = 0x10;
constexpr uint32_t kSweepStaticsGroup0 = 0x20, kSweepStaticsGroup1 = 0x40, kSweepStaticsGroup2 = 0x80;
constexpr uint32_t kSweepEffectsGroup0 = 0x100, kSweepEffectsGroup1 = 0x200, kSweepEffectsGroup2 = 0x400;
constexpr uint32_t kSweepControllers = 0x800;
constexpr uint32_t kSweepSkyLayers = 0x1000;
constexpr uint32_t kSweepAllButPlayer = 0x1ffe;
// every actor pool but the player's (the sky layers kept): a cutscene's clear
constexpr uint32_t kSweepActors = 0xffe;
// the classes a transition / a cutscene clears: all but the player, the group-0 animated actors, statics, effects
// and the controllers
constexpr uint32_t kSweepTransition = 0x6dc;
// The ops of a sweep (bits): the run / shown flags toggled or set, or the actors freed
constexpr uint8_t kSweepToggleRun = 0x1, kSweepSetRun = 0x2, kSweepToggleShown = 0x4, kSweepSetShown = 0x8;
constexpr uint8_t kSweepFree = 0x80;

// exe 0x5e05d0(classes, op): op kSweepFree frees the actors of the classes (riders detached), else their run / shown
// flags change by op (exe 0x5e0890); the sky layers' descriptors are cleared / flagged the same way
void actorsSweep(uint32_t classes, uint8_t op);
// exe 0x5e08d0(classes, offset): the actors of the classes the warp keeps (kActorWarpKept) move by (dx, dz) (none
// without an offset), the others are freed
void actorsWarp(uint32_t classes, const int16_t* offset);
// exe 0x5d5b30's kill: every actor but the player (and the sky layers) freed, the frees applied
void killAllActors();

} // namespace od::port
