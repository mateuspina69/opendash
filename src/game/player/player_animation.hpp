// The player's animation: its two channels (the body +0x94, the upper body +0xa4) played from the animation scripts
// (the player's tables in PL00P000.DAT segment 2, the weapon arms', the stage's: docs/dat_format.md section 10), the
// requests the sub-states make (walk / run keep their phase), the blend toward the next frame, and the two idle-variation
// tracks (the face) stepped with the RNG.
#pragma once

#include <cstdint>

namespace od::port {

struct Player;
enum class PlayerAnim : uint8_t;

// exe 0x571780(player, id, step, force): ask for animation id at step (the dispatch starts it when it changes); force:
// even the playing one. Walk <-> run keep the phase (the run's step * 2 + 4 / (the walk's step - 4) / 2)
void requestAnim(Player& p, PlayerAnim id, int16_t step, bool force);
// ... an id the code computes (a table's, the upper body's, a script's)
void requestAnimId(Player& p, int8_t id, int16_t step, bool force);
// exe 0x5e1a90 on the player's body channel: animation id from its step, the blend, the pose
// (without the blend)
void startBodyAnimation(Player& p, PlayerAnim id, int step);
// exe 0x5e1b60: the upper body channel: animation id from its step with the channel mix mode (no pose)
void startUpperAnimation(Player& p, uint8_t mixMode, PlayerAnim id, int step);
// exe 0x5e1cc0 + the pose: the body channel's key ticks
void advanceBodyAnimation(Player& p);
// exe 0x5e1db0: the upper body channel's key ticks with the channel mix mode
void advanceUpperAnimation(Player& p, uint8_t mixMode);

// exe 0x571650: the two idle-variation tracks stepped (not while a script runs) -> the face values
void stepIdleTracks(Player& p);
// exe 0x571820(player, a, b): the idle-variation tables selected (0x8c4ad4[a], 0x8c4ae4[b]; the same one: kept)
void selectIdleTables(Player& p, uint8_t a, uint8_t b);

// the idle tables the code selects
constexpr uint8_t kIdleCalm = 0;     // standing
constexpr uint8_t kIdleStrained = 1; // hurt, held, struggling

} // namespace od::port
