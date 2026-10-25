// The player driven by the scripts: the switch to the scripted state (the cutscenes, the exits' walks) and back, the
// animation a script queues, and the scripted state's update.
#pragma once

#include <cstdint>

namespace od::port {

struct Player;
enum class PlayerAnim : uint8_t;

// exe 0x572370: the player to the scripted state: the upper body unmixed, drawn, the lean off, the weapons / carry /
// lock-on state cleared, firing enabled, the camera rigid
void playerToScripted();
// exe 0x572300: ... when it plays (state Playing); 0 = done, -1 = not yet
int playerToScriptIfFree();
// exe 0x572320: ... only once it stands free (playing, idle, its step past the entry); 0 = done, -1 = not yet (a
// hanging player let fall)
int playerToScriptWhenStanding();
// exe 0x5723c0: the player back to its play (state Playing, sub-state Idle)
void playerBackToPlay();
// exe 0x5723d0(anim, step, force): the animation the scripted player plays next (force: even the playing one)
void playerScriptAnim(PlayerAnim anim, uint8_t step, bool force);
// ... an id the scripts compute or hold (a stage animation 0x80.., a table's)
void playerScriptAnimId(uint8_t anim, uint8_t step, bool force);
// ... the stage's animation n (kStageAnims + n: the stage bank's own, game/stage_files stageBankWalk), forced, from its
// first key
void playerStageAnim(uint8_t n);

// exe 0x57b710: the scripted state's tick: the previous pose kept, the queued animation started, the key events'
// sounds (a cue, the steps, a splash in water)
void scriptedUpdate(Player& p);

} // namespace od::port
