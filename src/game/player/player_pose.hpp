// The player's pose (its bone matrices), transcribed from dash2.exe:
//   exe 0x5e1f90  decode the current animation frame of both channels (blended when the channel's blend is on) into the
//                 scratch 0x6d00e0 (16 bone angle triples, the body) and 0x6d0160 (9, the upper body)
//   exe 0x5e25b0  walk the skeleton (the bone offsets at PS1 0x110800 = PL00P000.DAT segment 0) from the actor matrix
//                 Rx * Ry * Rz(0, yaw - the move direction's offset, 0) and write per bone b:
//                   the bone's matrix (+0x2b0 + b * 0x20): the rotation (s16, 4096 = 1) with the 1/8 model scale applied
//                   to its columns, the padding short, the s32 world translation (game units << the stage shift)
//                   the bone's point (+0x490 + b * 8): ((pos * 7 + 4 + t) >> 3), used by effects / the lock-on
//                 and the actor matrix itself at +0x74.
// Validated by test_player (the pose bytes of the golden). Bones: docs/dat_format.md sections 7 / 9.
#pragma once

#include <cstdint>
#include <array>
#include "engine/file_data.hpp"

#include "game/player/player_state.hpp"
#include "game/state_array.hpp"

namespace od::port {

// The pose's matrix stack (0x20 bytes per matrix, growing down from the exe's kPoseStackBottom: [0] the bottom one)
// and its top (the exe's stack pointer, a value: an entry's exe address)
constexpr uint32_t kPoseStackBottom = 0xa7ad40;
struct PoseStack {
    StateArray<PoseMatrix, 0x10> matrices{};
    uint32_t top = 0;
};
PoseStack& poseStack();
struct Player;
struct PlayerSnapshot;

// The file records and environment for one pose, captured before posing. Remote
// players supply their snapshot frames; they never need local animation cursors.
struct PlayerPoseData {
    uint8_t coordinateShift = 0;
    std::array<int8_t, 2> frames{};
    FileDataReader skeleton;
    std::array<FileDataReader, 2> frameBanks;
};
PlayerPoseData playerPoseData(const LoadedFileData& files, const PlayerSnapshot& player, uint8_t coordinateShift);
void playerPose(Player& p, const PlayerPoseData& data, bool blend = true);


// blend: exe 0x5e1f90 (the player's update; a channel blends when its blend is on); false: exe 0x5e1fe0 (the animation's
// start / advance decode both channels without the blend)
void playerPose(Player& p, bool blend = true);
// the player's own frame table (exe 0x743a00, the animations < 0x60): the packed frames' offset of frame-data index
// `frameData` (the host counts an animation's frames from two neighbours)
uint32_t playerFrameOffset(uint32_t frameData);

} // namespace od::port
