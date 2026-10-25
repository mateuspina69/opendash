// The room's texture animations: room faces whose texture flips through UV frames. The room setup of the stages that
// have them builds their list from the stage's table (exe 0x5f5bd0: each entry finds its face in the segment's face
// lists of the room pool); the pool-0xe0 controller of those rooms (exe 0x418350) steps every entry each tick and, when
// its frame's ticks run out, writes the next frame's texels into the face (in the room block, as the exe does). The
// host redraws the rewritten faces from the side channel below.
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor_movement.hpp"
#include "game/state_array.hpp"

namespace od::port {

// One texture animation of the list (0x14 bytes in the exe; the list at StageState::textureAnimations, its exe address
// in the stage's globals, textureAnimationCount entries)
struct RoomUvAnimation {
    uint8_t flags = 0;       // +0x0: kUvAnimationPaused; & kUvCornerOrderMask the corner order of a frame's two texels;
                             // kUvFlipOrder / kUvFlipSide the other two corners' copies
    uint8_t frameCount = 0;  // +0x1
    uint8_t frame = 0;       // +0x2: the frame shown
    uint8_t ticksLeft = 0;   // +0x3: the ticks before the next frame
    uint32_t faceTexels = 0; // +0x4: the face's texels (two words) in the room pool
    uint32_t faceShades = 0; // +0x8: the face's shades in the room pool (their reader, the room draw, is not traced)
    uint32_t steps = 0;      // +0xc: the frame steps (UvFrameStep, one a frame; the stage's table)
    uint32_t frames = 0;     // +0x10: the frames' texel pairs (4 bytes each)
};
constexpr uint32_t kRoomUvAnimationSize = 0x14;
// The room's list (the room setup builds it at the stage's list address; one list is the room's at a time)
struct RoomUvAnimations {
    StateArray<RoomUvAnimation, 0x100> entries{}; // up to the table's count (a byte)
};
RoomUvAnimations& roomUvAnimations();
constexpr uint8_t kUvAnimationPaused = 0x80; // the controller skips the entry (its exe step is a bare return)
constexpr uint8_t kUvCornerOrderMask = 0x07;
constexpr uint8_t kUvFlipSide = 0x01;
constexpr uint8_t kUvFlipOrder = 0x04;

// exe 0x5f5bd0(list, table): the list built from the stage's table (its first byte the count; then 8 bytes an entry:
// the segment pool entry, the face lists to skip, the faces to skip in the next; the flags, the frame count, the dword
// offset of the frame steps); the count 0 leaves the list empty
void roomUvAnimationSetup(uint32_t list, uint32_t table);
// exe 0x418350 (pool 0xe0 type 4 of ST00 / ST0A): freed at once when the room built no list, else every tick each entry
// not paused counts its frame down (exe 0x4183d0) and shows the next one when it runs out
void roomUvAnimationUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x514ea0(k): entry k's frame of its first step written into its face at once (the attract's script 4; as the
// exe, no redraw is queued on the side channel below)
void showRoomUvFirstFrame(uint32_t k);

// One texel store of the animation: the 8 bytes (two words) written at addr -- the four u / v pairs of a room face in
// the room block. A presentation side channel: the renderer re-reads the faces whose texels are at addr.
struct RoomUvWrite {
    uint32_t addr;
    uint32_t w0, w1;
};
// the writes queued since the last call, in order
std::vector<RoomUvWrite> takeRoomUvWrites();

} // namespace od::port
