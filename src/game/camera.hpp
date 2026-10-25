// The game camera (exe 0x40de70..0x40eb40): the tick that follows the player, runs the script camera or blends to a
// pose, and builds the view transform; the room's camera setup, the game-mode reset, the talk camera of the events,
// the follow mode the player's states pick, and the shakes. The state is game/camera_state; Validated by
// test_player_camera against the ST09 player golden (and every golden that runs a field tick).
#pragma once

#include <cstdint>
#include <optional>

#include "engine/data_source.hpp"
#include "game/camera_state.hpp"
#include "game/vectors.hpp"

namespace od::port {

// exe 0x40e190: one camera tick: the previous target / eye / angles kept, the mode's update (follow the player,
// the script camera, the blend), then the shake swings and decays
void cameraUpdate();
// exe 0x40e170 (the game-mode init): the block's header reset, the follow mode
void cameraReset();
// A room's camera record (exe data, 12 bytes: the per-stage room setup's FUN_0040e290(table + sub * 12)): the follow
// mode, the follow style, the base pose (height, distance, yaw, pitch, roll)
struct RoomCamera : DataRecord {
    static constexpr uint32_t kBytes = 12;
    uint8_t followMode = 0, followStyle = 0;
    int16_t height = 0, distance = 0, yaw = 0, pitch = 0, roll = 0;
    static RoomCamera load(const TableReader& r) {
        return {{}, r.u8(0), r.u8(1), r.s16(2), r.s16(4), r.s16(6), r.s16(8), r.s16(0xa)};
    }
};
// the camera record of the stage's room (nullopt: the stage's room setup sets none)
std::optional<RoomCamera> roomCamera(int stage, int room);
// exe 0x40e290(record): the room's camera record and the target / eye at the player's position (integer parts); the
// look, roll offset and shake cleared. Called by the per-stage room setup when its param >= 0.
void cameraRoomInit(const RoomCamera& record);
// exe 0x40e810(target): the talk camera: the follow target / pose saved, the blend poses from the current one, aimed at
// the 16.16 point at `target` (an exe pointer: the speaker's position; 0 keeps the node), the blend mode
void cameraTalkStart(const Fixed16Vec3* target);
// exe 0x40e8f0(instant): back from the talk camera: a blend from the current pose to the saved follow pose (8 ticks, 1
// when instant), then the follow mode
void cameraTalkEnd(bool instant);
// exe 0x40e9f0: the saved follow target / pose restored at once, the follow mode (an event ends)
void cameraRestoreSaved();
// exe 0x40ea70: how the follow camera's yaw follows the player's
void cameraSetFollow(CameraFollow follow);
// exe 0x40ea80(force, amount, decay): a shake of `amount` swinging by `decay`, unless a stronger one runs (force: always)
void cameraShake(bool force, uint32_t amount, uint16_t decay);
// exe 0x40eab0(strength, amount, near, far, pos): a shake (its offset from `strength`, its swing from `amount`) scaled
// by the distance of the eye (0x6d0034, world units) to the integer parts of `pos`: none past `far`, full at `near`;
// it replaces a weaker one
void cameraShakeAt(uint32_t strength, uint32_t amount, uint32_t nearDistance, uint32_t farDistance, const Fixed16Vec3& pos);
// exe 0x40eb40: the same at an s16 point (taken as 16.16 with zero fractions)
void cameraShakeAt(uint32_t strength, uint32_t amount, uint32_t nearDistance, uint32_t farDistance, const int16_t pos[3]);

} // namespace od::port
