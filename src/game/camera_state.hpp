// The camera's state (game/camera; the camera script interpreter, game/camera_script, drives the script camera
// block): the camera block 0xa6bca0 (0xe4 bytes: the mode, the follow camera's tunables, the target / eye / angles of
// the view and the blend between two poses), the script camera block 0xa6bba0 (the point / angles a camera script
// moves, its interpolations), and the transform the camera hands the draw and projection code (the view matrix
// 0x6d0014, the eye 0x6d0034).
#pragma once

#include <cstddef>
#include <cstdint>

#include "game/geometry_scratch.hpp"
#include "game/matrix.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"
#include "game/view_transform.hpp"

namespace od::port {

// ---- the camera block's composites -----------------------------------------------------------------------------------

// A point of the camera (integer world units, not 16.16): x / y / z and a spare 4th dword. The exe copies these points
// as four dwords (the spare one included); the interpolations touch the three coordinates only.
struct CameraVec {
    int32_t x = 0, y = 0, z = 0;
    int32_t spare = 0;
};

// A pose of the camera around its target (five shorts): the target's height above the followed point, the distance of
// the eye, and the yaw / pitch / roll (12-bit angles)
struct CameraPose {
    int16_t height = 0, distance = 0, yaw = 0, pitch = 0, roll = 0;
};

// The point a blend camera aims at (exe data / an actor's position: 16.16 x / y / z)

// ---- the camera block 0xa6bca0 ---------------------------------------------------------------------------------------


// +4: what the camera does each tick (exe 0x40e190)
enum class CameraMode : uint8_t {
    Follow = 0,   // behind the player (exe 0x40e360)
    Scripted = 1, // at the script camera's point / angles (exe 0x512600; the camera scripts drive them)
    Blend = 2,    // blending to a target pose (exe 0x40e540: the talk camera, the rooms' fixed camera points)
};
// +5 in the blend mode: where the blend is
enum class BlendStep : uint8_t {
    AimAtNode = 0,  // a pending blend request aims the target pose at the blend node first
    Start8 = 1,     // start an 8-tick blend
    StartAtOnce = 2, // start a 1-tick blend
    Blending = 3,   // the ticks run
};
// +0xa (exe 0x40ea70 sets it): how the follow camera's yaw follows the player's
enum class CameraFollow : uint8_t {
    Rigid = 0, // behind the player at once
    Lazy = 1,  // turning 0x80 a tick toward it (the player hanging, talking, posing)
};
// +0xb: how the follow camera's pose takes the look pitch (lookPitch)
enum class FollowStyle : uint8_t {
    FullTilt = 0, // pitch = base + look, distance = base + look + 0x200, height = base - look / 16
    HalfTilt = 1, // the same with half the look on the distance
    Fixed = 2,    // the base pose (+ 0x200 on the distance), no look
};
// +3: a blend's request byte (0: none; any other value starts a blend at the node; kBlendBackToFollow also ends it in
// the follow mode)
constexpr int8_t kBlendBackToFollow = -1;
// the distance the follow camera adds to its base distance
constexpr int16_t kFollowDistanceExtra = 0x200;
// the lazy follow's yaw step per tick (and the gap below which it snaps)
constexpr int kLazyFollowStep = 0x80;
// the ticks of a blend started by BlendStep::Start8
constexpr uint8_t kBlendTicks = 8;

struct CameraState {
    uint8_t initialised = 0;  // set to 1 by the game-mode reset (exe 0x40e170); no reader in the exe
    uint8_t unread01 = 0;     // cleared by the reset; no reader in the exe
    uint8_t unread02 = 0;     // ... (the reset's word store over it and the blend request)
    int8_t blendRequest = 0;  // a blend's request (kBlendBackToFollow: back to the follow mode at its end)
    uint8_t modeByte = 0;     // CameraMode (mode / setMode)
    uint8_t blendStepByte = 0; // BlendStep (blendStep / setBlendStep)
    StateArray<uint8_t, 2> modeSpare{}; // the bytes after the step that entering a mode clears (no reader)
    uint8_t blendTicks = 0;   // the blend's ticks left
    uint8_t unread09 = 0;     // cleared with the blend ticks (the reset's dword store); no reader
    uint8_t followModeByte = 0;  // CameraFollow (the room's camera record's byte as is)
    uint8_t followStyleByte = 0; // FollowStyle (ditto)
    CameraVec target;         // the point the camera looks at
    CameraVec prevTarget;     // ... last tick
    CameraVec eye;            // the eye (the transform's unshifted eye, sign-extended)
    CameraVec prevEye;        // ... last tick
    CameraVec blendFromTarget; // the blend's start target
    CameraVec blendToTarget;  // the blend's end target
    CameraVec savedTarget;    // the follow target saved by the talk camera (restored at the event's end)
    ShortVec4 viewAngles;     // the transform's angles: x = -pitch, y = yaw, z = roll (& 0xfff)
    ShortVec4 prevViewAngles; // ... last tick
    CameraPose pose;          // the pose the view is built from
    int16_t unread9e = 0;     // the word after the pose (no reader in the exe)
    StateArray<int16_t, 4> unreadA0{}; // the words before the base pose (no reader in the exe)
    CameraPose basePose;      // the room's follow tunables (yaw: the offset from the player's yaw)
    CameraPose blendFromPose; // the blend's start pose
    CameraPose blendToPose;   // the blend's end pose
    CameraPose savedPose;     // the follow pose saved by the talk camera
    int16_t unreadD0 = 0;     // cleared by the room init (exe 0x40e290); no reader in the exe
    int16_t lookPitch = 0;    // the look pitch (the player's aim pitch +0x28 follows it; L2's look mode)
    int16_t rollOffset = 0;   // added to the base roll
    int16_t unreadD6 = 0;     // no reader in the exe
    int16_t shake = 0;        // the shake's vertical offset (<< 8), alternating and decaying
    int16_t unreadDA = 0;     // cleared with the shake; no reader in the exe
    int16_t shakeAmplitude = 0; // the shake's next swing
    int16_t unreadDE = 0;     // no reader in the exe
    const Fixed16Vec3* blendNode = nullptr; // the point a blend request aims the target at (the talk event's speaker)

    CameraMode mode() const { return CameraMode(modeByte); }
    void setMode(CameraMode m) { modeByte = uint8_t(m); }
    BlendStep blendStep() const { return BlendStep(blendStepByte); }
    void setBlendStep(BlendStep s) { blendStepByte = uint8_t(s); }
    CameraFollow follow() const { return CameraFollow(followModeByte); }
    void setFollow(CameraFollow f) { followModeByte = uint8_t(f); }
    FollowStyle followStyle() const { return FollowStyle(followStyleByte); }
    // the room's camera record's byte (exe 0x40e290 copies it as is)
    void setFollowModeByte(uint8_t v) { followModeByte = v; }
    void setFollowStyleByte(uint8_t v) { followStyleByte = v; }

    // the mode and its step in one dword store (+6 / +7 cleared), as the exe enters a mode
    void enterMode(CameraMode m, BlendStep s) {
        modeByte = uint8_t(m);
        blendStepByte = uint8_t(s);
        modeSpare.fill(0);
    }
    // exe 0x40e170's header: initialised, the bytes +1 / +2 and the blend request cleared, the mode word and the blend
    // ticks / follow mode / style word cleared
    void resetHeader() {
        initialised = 1;
        unread01 = 0;
        unread02 = 0;
        blendRequest = 0;
        enterMode(CameraMode::Follow, BlendStep::AimAtNode);
        blendTicks = 0;
        unread09 = 0;
        followModeByte = 0;
        followStyleByte = 0;
    }
};
inline CameraState& cameraState() {
    static CameraState s;
    return s;
}

// ---- the script camera block 0xa6bba0 (exe 0x5118a0 drives it, exe 0x512600 views it) ------------------------------

// +4: what the script camera's point is relative to
enum class ScriptCameraAnchor : uint8_t {
    Absolute = 0, // point
    Actor = 1,    // the script actor [anchorActor]'s position + offset
    Player = 2,   // the player's position + offset
};
// +7: the interpolation bits: the low nibble the point's (its easing | kEasePoint while it runs), the high nibble the
// angles' (easing << 4 | kEaseAngles)
constexpr uint8_t kEasePoint = 0x08;
constexpr uint8_t kEaseAngles = 0x80;
constexpr uint8_t kEaseKindMask = 0x07;
// the lanes of ScriptCamera::angles
constexpr size_t kScriptYaw = 0, kScriptPitch = 1, kScriptDistance = 2;

// One interpolation of the script camera (exe 0x512260): the tick, the length, the start and end values (16.16)
struct CameraEase {
    int32_t tick = 0;
    int32_t length = 0;
    StateArray<int32_t, 3> from{};
    StateArray<int32_t, 3> to{};
};

struct ScriptCamera {
    uint8_t headByte2 = 0;    // +2 of the block's head (0xa6bba2): read / cleared only by the rider walk past the static
                              // pool (game/carry, exe 0x414e70)
    uint8_t anchorByte = 0;   // ScriptCameraAnchor (anchor / setAnchor)
    uint8_t lookAt = 0;       // 0: the angles (yaw / pitch / distance), else aimed at lookAtPoint
    uint8_t anchorActor = 0;  // the script actor of ScriptCameraAnchor::Actor
    uint8_t easing = 0;       // kEasePoint / kEaseAngles / kEaseKindMask nibbles
    StateArray<int32_t, 3> point{};       // 16.16 (absolute anchor)
    uint32_t roll = 0;                    // the roll (its low word)
    StateArray<int32_t, 3> offset{};      // 16.16, from the anchor (actor / player)
    StateArray<int32_t, 3> pointSpeed{};  // added to the point / offset each tick when not interpolated
    StateArray<int32_t, 3> lookAtSpeed{}; // added to lookAtPoint each tick
    StateArray<int32_t, 3> lookAtPoint{}; // 16.16
    StateArray<int32_t, 3> angles{};      // 16.16: [kScriptYaw] yaw, [kScriptPitch] pitch, [kScriptDistance] distance
    StateArray<int32_t, 3> angleSpeed{};  // added to the angles each tick
    StateArray<CameraEase, 2> eases{};    // [0] the point's, [1] the angles'
    uint32_t script = 0;                  // the camera script's word pointer (exe data)

    ScriptCameraAnchor anchor() const { return ScriptCameraAnchor(anchorByte); }
    void setAnchor(ScriptCameraAnchor a) { anchorByte = uint8_t(a); }
    // the anchor, look-at, anchor actor and easing bytes cleared (the exe's dword store at +4)
    void clearSettings() {
        anchorByte = 0;
        lookAt = 0;
        anchorActor = 0;
        easing = 0;
    }
};
inline ScriptCamera& scriptCamera() {
    static ScriptCamera s;
    return s;
}

// ---- the transform the camera builds (exe 0x40de70) ------------------------------------------------------------------

struct CameraTransform {
    TransformMembers viewMembers;            // the view the draw and projection code use (its matrix, pad, translation)
    ViewTransform view = viewMembers.lanes();
    ShortVec3 eyePoint;                      // the eye in world units (not shifted)
    int16_t eyePad = 0;                      // the short after it (no reader)
    StateArray<int32_t, 3> eyeOffset{};      // the translation of the unshifted view
    // the transform's scratch: its transpose and translation (copied whole into `view`); the rotation is built in the
    // projections' scratch transform (game/projection_state ProjectionScratch). Its bytes are the geometry scratch's
    // (+0x20: game/geometry_scratch, over the room collision queries' state).
    ViewTransform viewScratch = geometryScratch().transform(0x20);
};
inline CameraTransform& cameraTransform() {
    static CameraTransform s;
    return s;
}

} // namespace od::port
