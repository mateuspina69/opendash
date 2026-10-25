// The stage / room transitions: the game task's field modes (exe 0x541070's handlers [0x8ae898 + mode * 4] for the
// modes 2 and 3, game/game_state GameMode) -- the field play and its transition controller (exe 0x5413a0: the arrival
// fade, the request's handshake with the transition script, the in-stage warp, the sub-room switch in place, the fade
// out and the room / stage load), the room loaded mode (exe 0x541340), the state part of the stage load (exe 0x5411c0)
// -- the arrival placement (exe 0x542370), the room (re)setup (exe 0x5d5b30), the room scripts' door test (exe
// 0x512890), the frame body (exe 0x542280) and the play time. Bit-exact against tests/data/trans_*.bin.
#pragma once

#include <cstdint>
#include <functional>

#include "game/actor_movement.hpp"
#include "game/fade.hpp"

namespace od::port {

// What the host runs inside the game task's frame (OpenDash: the systems that are not ported, the tests' checks)
struct TransitionHooks {
    std::function<void()> frameStart;        // the frame body's entry (exe 0x542280; the tests compare here)
    std::function<void()> frame;             // the frame body after the script runner, before its idle step
    std::function<void(int param)> roomSetup; // exe 0x5d5b30's per-stage room setup [0x8d397c + stage * 4](param) for
                                              // the stage / room of the game state (actors; OpenDash: load the room)
    std::function<void()> camera;            // the room loaded mode's camera update (exe 0x541340)
    std::function<void(int dx, int dz)> warp; // the in-stage warp (request 0xff, no script): every actor and the player
                                              // moved by (dx, 0, dz) (exe 0x5e08d0; OpenDash: the render side follows)
    std::function<void()> roomHandler;       // the room scripts' per-stage handler call (exe 0x512890, before its
                                             // flag-0x700 door test; OpenDash: game/room_scripts roomScriptHandler when
                                             // the room was spawned by the port)
    std::function<void()> roomScripts;       // after the room scripts
    std::function<void()> render;            // the draw pass (exe 0x5422f0; its minimap step exe 0x4155b0 is
                                             // game/minimap's, the HUD elements' draw exe 0x416010 the host's)
};

// exe 0x541070's body for the modes 2 and 3 (mode 1 is the caller's loader), then the pending actor frees applied (exe
// 0x5e15f0). Other modes: a port gap.
void gameTick(const PlayerEnv& env, TransitionHooks& hooks, FadeTask& fade);
// exe 0x5413a0: one tick of the field mode
void fieldMode(const PlayerEnv& env, TransitionHooks& hooks, FadeTask& fade);
// exe 0x541340: one tick of the room loaded mode
void roomLoadedMode(const PlayerEnv& env, TransitionHooks& hooks, FadeTask& fade);
// exe 0x542370: the player at the request's destination (unless it keeps its position), the stage / room of the game
// state, the story moved on
void arrivalPlacement();
// The stage load's state part after OpenDash loaded the new stage (exe 0x5411c0's substates 2..4: the arrival, the
// game's display mode back after the title, the HUD's elements, the stage's bytes (exe 0x5d5a80 / 0x5d5ae0), the
// stage's flag groups and the script block cleared (exe 0x5127a0(0)), the buffer mode) -> the room loaded mode at its
// setup. Not covered by a golden (the stage load itself is not ported); the following mode-2 tick is.
void stageLoaded();
// exe 0x5426e0: the play time counters += the pace (capped at 99:59:59 of 60 Hz frames)
void playTimeTick();
// exe 0x53f6b0 from a script step: the fade task of the running game task frame
void scriptFade(uint32_t request, uint8_t param);

} // namespace od::port
