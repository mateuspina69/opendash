// Story chapter 3: Calbania (story 7 -> ): the 0x582 hint on Nino (ST1B), the travel to Calbania (ST23) and what
// follows there. Transcribed from dash2.exe one function per exe function (conventions as game/story.cpp).
#pragma once

#include <cstdint>
#include <vector>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"
#include "game/hud.hpp"
#include "game/script_state.hpp"

namespace od::port {

// Scripts of the chapter (0x57 = 0x533520: ST23, the arrival on Calbania)
bool ch3ScriptPorted(RoutineId handler);
bool ch3ScriptTick(const PlayerEnv& env, RoutineId handler);
// exe 0x5f30e0's callers of the chapter: script 0x26 draws the white screen while its whiteScreen byte is 0xff (Glyde's
// base blown up without the escape); script 0x3c (the island blown up) while its whiteScreen byte is set and no fade
// runs, its state 2 also during a fade from its sub-state 3 on
bool baseBlownWhiteScreen();
bool islandBlownWhiteScreen();
// Room-script handlers of the chapter (ST23 table 0x8c8c28): fn = the handler, run = tick it (false: only "is it
// known")
bool ch3RoomHandler(RoutineId fn, bool run);

// Script 0x3c's own bytes (the island blown up; its cast, content/calbania/ch3_script_cast, keys on them): the white
// screen and the cast's start (the voice plays)
struct IslandBlownBytes : ScriptPart<IslandBlownBytes> {
    uint8_t whiteScreen = 0; // +0xc
    uint8_t castStarted = 0; // +0xd
};

// Pool e0 type 0x22 exe 0x4270a0 (ST30 sub 2, the escape): the escape timer on screen (logic part; the picture is
// escapeTimerDraw)
void escapeTimerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4270e0 (the packets of the controller 0x4270a0): the timer as seconds = ticks / 30 in up to 3 digits (leading
// zeros left out) after a stopwatch icon (its frame) over a 0x48 x 0x20 plate: sprites of tpage 0x0e, the digits' clut
// 0x3c78 (0x3c38 at 30 s and below), the places / sizes of the exe table 0x88bff8, the plate drawn first
std::vector<hud::Prim> escapeTimerDraw();
// pool e0 type 0x2a exe 0x428de0 (ST31): the alarm (siren + red flash; picture alarmFlashDraw)
void alarmFlashUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x428de0's packet: a semi-transparent flat quad over the screen, red = 0x50 + (sin(phase) * 64 >> 12), green /
// blue 0 (the PS1 blend mode is the current draw mode's, not set by the packet; hypothesis: mode 0 as Prim::semi)
std::vector<hud::Prim> alarmFlashDraw();

// ST20 cast (content/calbania/calbania_cast): pool 0x20 type 0 sub 1 exe 0x446720 (Appo / Dah script puppets, variants
// 0x11 / 0x12)
void appoDahPuppetUpdate(const PlayerEnv& env, uint32_t actor);
// type 0 sub 2 exe 0x446b90: the sitting puppets of script 0x11 (head turned toward a point)
void sittingPuppetUpdate(const PlayerEnv& env, uint32_t actor);
// type 0xa sub 0 exe 0x45d970: the town birds (idle / wander / flee / follow, lifted and thrown, hits cost balance)
void townBirdUpdate(const PlayerEnv& env, uint32_t actor);
// type 0xa sub 1 exe 0x45f010: a standing cast member (variant 0 at its record, 1 in front of the player)
void townStandingCastUpdate(const PlayerEnv& env, uint32_t actor);

// Appo / Dah outside the town (content/calbania/appo_dah_a): pool 0x20 type 0x43 sub 0 exe 0x4c4980 (ST21 / ST22: guide
// along the field's node path, wait shouting, lifted / thrown) and sub 2 exe 0x4c6650 (ST1F: script puppets of scripts
// 0x1e / 0x1f)
void appoDahGuideUpdate(const PlayerEnv& env, uint32_t actor);
void appoDahGatePuppetUpdate(const PlayerEnv& env, uint32_t actor);
// ST1F cast (content/calbania/appo_dah_b): pool 0x20 type 0x43 sub 0 exe 0x4c1b30 (Appo / Dah in the fight: waypoints,
// throws, knock-backs, the gate flags 0x580..0x582, lifted and thrown by the player), sub 1 exe 0x4c5f90 (script 0x1d
// puppets), sub 3 exe 0x4c6ae0 (script 0x1f puppets)
void appoDahFightUpdate(const PlayerEnv& env, uint32_t actor);
void script1dAppoDahUpdate(const PlayerEnv& env, uint32_t actor);
void script1fAppoDahUpdate(const PlayerEnv& env, uint32_t actor);
// pool 0x40 ST1F type 0x15 exe 0x5c8220: the bomb Appo / Dah throw (billboard draw command 0x2c)
void appoDahBombUpdate(const PlayerEnv& env, uint32_t actor);


} // namespace od::port
