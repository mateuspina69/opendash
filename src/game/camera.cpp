// The game camera: the tick, the view transform, the room setup, the talk camera and the shakes (see camera.hpp).
#include "game/camera.hpp"
#include "game/camera_state.hpp"

#include <cstdlib>

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/camera_drive.hpp"
#include "game/distances.hpp"
#include "game/fixed_point.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/projection_state.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"
#include "game/vectors.hpp"

namespace od::port {

namespace {

// The camera's model of exe 0x40bbb0 (a row of m times v / 4096, truncated): the exact integer sum of the three
// products, divided by 4096 with truncation. The exe sums on the x87 at 24-bit precision (v / 4096, each product and
// each partial sum rounded to a float mantissa), so the two differ when a rounding crosses an integer before the
// truncation: an approximation, not a proven equivalence
int32_t transformRow(const int16_t* row, const int32_t v[3]) {
    const int64_t s = int64_t(row[0]) * v[0] + int64_t(row[1]) * v[1] + int64_t(row[2]) * v[2];
    return int32_t(s / 4096);
}

// the rotated offset of the eye from the target at `dist` (yaw / pitch 12-bit), minus the target
void eyeFromTarget(int dist, int yaw, int pitch, const int32_t target[3], int32_t out[3]) {
    out[0] = ((fixedSin(yaw) * dist >> 12) * fixedCos(pitch) >> 12) - target[0];
    out[1] = (fixedSin(pitch) * dist >> 12) - target[1];
    out[2] = ((fixedCos(yaw) * dist >> 12) * fixedCos(pitch) >> 12) - target[2];
}

// exe 0x40de70(target, dist, yaw, pitch, roll): the view transform of the eye at `dist` from the target along yaw /
// pitch: the angles kept in the block, the rotation Ry(yaw) Rx(-pitch) Rz(roll), its transpose and the translation
// (in the stage's shifted units) into the view 0x6d0014, the eye (world units) into 0x6d0034 and the block, the
// unshifted translation into 0x6d003c
void buildViewTransform(int dist, int yaw, int pitch, uint16_t roll) {
    CameraState& cam = cameraState();
    CameraTransform& tf = cameraTransform();
    const int shift = stage().coordinateShift & 0x1f;
    cam.viewAngles.x = int16_t(-pitch & kAngleMask);
    cam.viewAngles.y = int16_t(yaw & kAngleMask);
    cam.viewAngles.z = int16_t(roll & kAngleMask);
    const Matrix3 r = rotationYXZ(cam.viewAngles.x, cam.viewAngles.y, cam.viewAngles.z);
    projectionScratch().transform.matrix = r;
    Matrix3 t{}; // exe 0x40c170: the transpose
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) t.e[i * 3 + j] = r.e[j * 3 + i];
    tf.viewScratch.matrix = t;
    const int32_t target[3] = {cam.target.x, cam.target.y, cam.target.z};
    const int32_t shiftedTarget[3] = {target[0] << shift, target[1] << shift, target[2] << shift};
    int32_t shifted[3];
    eyeFromTarget(dist << shift, yaw & kAngleMask, pitch & kAngleMask, shiftedTarget, shifted);
    for (size_t i = 0; i < 3; ++i) tf.viewScratch.translation[i] = transformRow(t.e + i * 3, shifted);
    tf.view = tf.viewScratch;
    int32_t world[3];
    eyeFromTarget(dist, yaw & kAngleMask, pitch & kAngleMask, target, world);
    cam.prevEye = cam.eye;
    tf.eyePoint.x = wrap16(-world[0]);
    cam.eye.x = tf.eyePoint.x;
    tf.eyePoint.y = wrap16(-world[1]);
    cam.eye.y = tf.eyePoint.y;
    tf.eyePoint.z = wrap16(-world[2]);
    cam.eye.z = tf.eyePoint.z;
    for (size_t i = 0; i < 3; ++i) tf.eyeOffset[i] = transformRow(t.e + i * 3, world);
}

// exe 0x40e360(camera, player) (CameraMode::Follow): the pose from the room's base pose and the look pitch (by the
// follow style), the target at the player's position raised by the height, the yaw behind the player (rigid or lazy),
// the roll, the shake's offset; OpenDash's modern controls drive the yaw / pitch / look instead (game/camera_drive)
void followPlayer() {
    CameraState& cam = cameraState();
    const Player& p = player();
    const bool driven = cameraDriven();
    switch (cam.followStyle()) {
    case FollowStyle::FullTilt:
    case FollowStyle::HalfTilt: {
        const int16_t look = driven ? int16_t(0) : int16_t(cam.lookPitch); // driven: the base distance / height
        cam.pose.pitch = wrap16(cam.basePose.pitch + look);
        const int16_t distance = cam.followStyle() == FollowStyle::FullTilt ? wrap16(cam.basePose.distance + look)
                                                                            : wrap16((look >> 1) + cam.basePose.distance);
        const int16_t height = wrap16(int16_t(-int(look) >> 4) + cam.basePose.height);
        cam.pose.distance = wrap16(distance + kFollowDistanceExtra);
        cam.pose.height = height;
        break;
    }
    case FollowStyle::Fixed:
        cam.pose.pitch = cam.basePose.pitch;
        cam.pose.distance = wrap16(cam.basePose.distance + kFollowDistanceExtra);
        cam.pose.height = cam.basePose.height;
        break;
    default: break;
    }
    cam.target.x = p.pos.x.whole;
    cam.target.y = int32_t(p.pos.y.whole) - int32_t(cam.pose.height);
    cam.target.z = p.pos.z.whole;
    const int16_t playerYaw = p.rotation.y;
    const int16_t behind = wrap16(cam.basePose.yaw + playerYaw);
    if (driven) {
        cameraDriveFollow(); // yaw, pitch, look from the controller
    } else if (cam.follow() == CameraFollow::Rigid) {
        cam.pose.yaw = behind;
    } else if (cam.follow() == CameraFollow::Lazy) { // turn kLazyFollowStep per tick toward the player's yaw + offset
        const int16_t yaw = cam.pose.yaw;
        int gap = (int(yaw) - int(playerYaw)) - kHalfTurn;
        if (gap < 0) gap = (int(playerYaw) - int(yaw)) + kHalfTurn;
        uint16_t d = uint16_t(gap) & kAngleMask;
        const bool shortWay = d < kHalfTurn + 1;
        if (!shortWay) d = uint16_t(kFullTurn - d);
        if (int16_t(d) < kLazyFollowStep + 1) {
            cam.pose.yaw = behind;
            cam.setBlendStep(BlendStep::AimAtNode); // the exe clears the step byte on the snap
        } else if (shortWay ? behind < yaw : yaw <= behind) {
            cam.pose.yaw = wrap16(yaw - kLazyFollowStep);
        } else {
            cam.pose.yaw = wrap16(yaw + kLazyFollowStep);
        }
    }
    const uint16_t roll = uint16_t(cam.rollOffset + cam.basePose.roll);
    cam.pose.roll = int16_t(roll);
    cam.target.y = int32_t(cam.target.y - (int(cam.shake) >> 8));
    buildViewTransform(driven ? cameraDriveDistance() : int(cam.pose.distance), cam.pose.yaw, cam.pose.pitch, roll);
}

// exe 0x512600 (CameraMode::Scripted, set by the script sequences): the script camera's point (absolute, or its offset
// from a script actor / the player) as the target minus the shake, viewed at the script angles -- or aimed from the
// target at the look-at point (exe 0x40e090: the distance their planar gap, one Newton step on its root). The exe
// writes an uninitialised local into the roll of the look-at view's angles block: OpenDash leaves it alone.
void scriptedView() {
    CameraState& cam = cameraState();
    ScriptCamera& sc = scriptCamera();
    switch (sc.anchor()) {
    case ScriptCameraAnchor::Actor: {
        const uint32_t actor = slotIdentityWord(scriptState().actors[sc.anchorActor]);
        if (actor != 0) {
            const Actor& a = actorLayoutAt(actor);
            sc.point[0] = int32_t(uint32_t(int32_t(a.pos.x)) + uint32_t(int32_t(sc.offset[0])));
            sc.point[1] = int32_t(uint32_t(int32_t(a.pos.y)) + uint32_t(int32_t(sc.offset[1])));
            sc.point[2] = int32_t(uint32_t(int32_t(a.pos.z)) + uint32_t(int32_t(sc.offset[2])));
        }
        break;
    }
    case ScriptCameraAnchor::Player: {
        const Player& p = player();
        sc.point[0] = int32_t(uint32_t(int32_t(sc.offset[0])) + uint32_t(int32_t(p.pos.x)));
        sc.point[1] = int32_t(uint32_t(int32_t(p.pos.y)) + uint32_t(int32_t(sc.offset[1])));
        sc.point[2] = int32_t(uint32_t(int32_t(sc.offset[2])) + uint32_t(int32_t(p.pos.z)));
        break;
    }
    default: break;
    }
    cam.target.y = (int32_t(sc.point[1]) >> 16) - (int(cam.shake) >> 8);
    cam.target.x = int32_t(sc.point[0]) >> 16;
    cam.target.z = int32_t(sc.point[2]) >> 16;
    if (sc.lookAt != 0) {
        const int32_t px = cam.target.x, py = cam.target.y, pz = cam.target.z;
        const int32_t qx = int32_t(sc.lookAtPoint[0]) >> 16, qy = int32_t(sc.lookAtPoint[1]) >> 16,
                      qz = int32_t(sc.lookAtPoint[2]) >> 16;
        const int32_t dx = int32_t(uint32_t(px) - uint32_t(qx)), dz = int32_t(uint32_t(pz) - uint32_t(qz));
        const int32_t d2 = int32_t(uint32_t(dx) * uint32_t(dx) + uint32_t(dz) * uint32_t(dz)); // imul (32-bit)
        int32_t r = isqrt(d2);
        if (r != 0) r = (d2 / r + r) >> 1; // one Newton step
        const int yaw = atanAngle(float(int32_t(uint32_t(qx) - uint32_t(px))), float(int32_t(uint32_t(qz) - uint32_t(pz)))) +
                        kHalfTurn;
        const int pitch = -atanAngle(float(int32_t(uint32_t(qy) - uint32_t(py))), float(r));
        buildViewTransform(r, yaw, pitch, 0);
        return;
    }
    buildViewTransform(int32_t(sc.angles[kScriptDistance]) >> 16, int32_t(sc.angles[kScriptYaw]) >> 16,
                       int32_t(sc.angles[kScriptPitch]) >> 16, uint16_t(uint32_t(sc.roll)));
}

// one lane of a blend at k / 8 ticks left: to - (to - from) * k / 8
void blendLane(int32_t& out, int32_t from, int32_t to, int k) { out = to - ((to - from) * k >> 3); }
// one angle of a blend, the short way round
void blendAngle(int16_t& out, int16_t from, int16_t to, int k) {
    int16_t d = int16_t(to - from);
    if (std::abs(int(d)) > kHalfTurn) d = int16_t(d < 1 ? d + kFullTurn : d - kFullTurn);
    out = wrap16(to - int16_t(d * k >> 3));
}

// exe 0x40e540 (CameraMode::Blend, set by the talk camera and the rooms' fixed camera points): a pending request
// first aims the end target at the blend node (its y minus the end pose's height) and turns the end yaw by the node's
// angle from the player; then an 8- or 1-tick blend of the target and the pose from the start to the end (angles the
// short way round); at its end the end pose is taken and the mode returns to Follow (kBlendBackToFollow) or waits
void blendView() {
    CameraState& cam = cameraState();
    switch (cam.blendStep()) {
    case BlendStep::AimAtNode: {
        if (cam.blendRequest == 0) break;
        cam.blendFromTarget = cam.target;
        const Fixed16Vec3& node = *cam.blendNode;
        cam.blendToTarget.x = node.x.whole;
        cam.blendToTarget.y = int32_t(node.y.whole) - cam.blendToPose.height;
        cam.blendToTarget.z = node.z.whole;
        const Player& p = player();
        const int a = angleOf(p.pos.x, p.pos.z, node.x, node.z);
        cam.blendToPose.yaw = signedAngle(a + cam.blendToPose.yaw);
        cam.setBlendStep(BlendStep::Start8);
        [[fallthrough]];
    }
    case BlendStep::Start8:
    case BlendStep::StartAtOnce:
        cam.blendTicks = cam.blendStep() == BlendStep::Start8 ? kBlendTicks : uint8_t(1);
        cam.setBlendStep(BlendStep::Blending);
        [[fallthrough]];
    case BlendStep::Blending: {
        const int8_t left = int8_t(cam.blendTicks - 1);
        cam.blendTicks = uint8_t(left);
        if (left == 0) {
            cam.target = cam.blendToTarget;
            cam.pose = cam.blendToPose;
            if (cam.blendRequest == kBlendBackToFollow) cam.enterMode(CameraMode::Follow, BlendStep::AimAtNode);
            else cam.setBlendStep(BlendStep::AimAtNode);
            cam.blendRequest = 0;
            break;
        }
        const int k = left;
        blendLane(cam.target.x, cam.blendFromTarget.x, cam.blendToTarget.x, k);
        blendLane(cam.target.y, cam.blendFromTarget.y, cam.blendToTarget.y, k);
        blendLane(cam.target.z, cam.blendFromTarget.z, cam.blendToTarget.z, k);
        const CameraPose& from = cam.blendFromPose;
        const CameraPose& to = cam.blendToPose;
        cam.pose.height = wrap16(to.height - int16_t((int(to.height) - from.height) * k >> 3));
        cam.pose.distance = wrap16(to.distance - int16_t((int(to.distance) - from.distance) * k >> 3));
        blendAngle(cam.pose.yaw, from.yaw, to.yaw, k);
        blendAngle(cam.pose.pitch, from.pitch, to.pitch, k);
        blendAngle(cam.pose.roll, from.roll, to.roll, k);
        break;
    }
    default: break;
    }
    buildViewTransform(cam.pose.distance, cam.pose.yaw, cam.pose.pitch, uint16_t(cam.pose.roll));
}

// exe 0x40eab0's body on the integer parts of the point
void shakeByDistance(uint32_t strength, uint32_t amount, uint32_t nearDistance, uint32_t farDistance, int x, int y, int z) {
    const ShortVec3& eye = cameraTransform().eyePoint;
    const int dx = int(eye.x) - x, dy = int(eye.y) - y, dz = int(eye.z) - z;
    const uint32_t d = uint32_t(isqrt(int32_t(uint32_t(dz * dz) + uint32_t(dy * dy) + uint32_t(dx * dx))));
    if (d > farDistance) return;
    const uint32_t k = farDistance - d, span = farDistance - nearDistance;
    const uint32_t offset = (k * strength) / span + 1;
    CameraState& cam = cameraState();
    if (int(cam.shake) > int32_t(offset)) return;
    cam.shake = int16_t(offset);
    cam.shakeAmplitude = int16_t((k * amount) / span + 1);
}

} // namespace

void cameraUpdate() {
    CameraState& cam = cameraState();
    cam.prevTarget = cam.target;
    cam.prevEye = cam.eye;
    cam.prevViewAngles = cam.viewAngles;
    switch (cam.mode()) {
    case CameraMode::Follow: followPlayer(); break;
    case CameraMode::Scripted: scriptedView(); break;
    case CameraMode::Blend: blendView(); break;
    default: break;
    }
    // the shake alternates and decays: while the offset is larger than the amplitude, offset = amplitude - offset and
    // the amplitude negated; else it stops
    const int16_t offset = cam.shake;
    if (offset != 0) {
        const int16_t amplitude = cam.shakeAmplitude;
        if (std::abs(int(amplitude)) < std::abs(int(offset))) {
            cam.shake = wrap16(amplitude - offset);
            cam.shakeAmplitude = wrap16(-amplitude);
        } else {
            cam.shake = 0;
        }
    }
}

void cameraReset() { cameraState().resetHeader(); }

namespace {
const TableFamily<RoomCamera> roomCameras{TableId::RoomCamera};
} // namespace

std::optional<RoomCamera> roomCamera(int stage, int room) {
    if (stage < 0 || room < 0) return std::nullopt;
    const uint32_t index = roomIndex(stage, room);
    if (!roomCameras.has(index)) return std::nullopt;
    return roomCameras[index];
}

void cameraRoomInit(const RoomCamera& rec) {
    CameraState& cam = cameraState();
    const Player& p = player();
    cam.setFollowModeByte(rec.followMode);
    const int32_t x = p.pos.x.whole, y = p.pos.y.whole, z = p.pos.z.whole;
    cam.prevTarget.x = x;
    cam.target.x = x;
    cam.prevEye.x = x;
    cam.eye.x = x;
    cam.prevTarget.y = y;
    cam.target.y = y;
    cam.prevEye.y = y;
    cam.eye.y = y;
    cam.setFollowStyleByte(rec.followStyle);
    cam.prevTarget.z = z;
    cam.target.z = z;
    cam.prevEye.z = z;
    cam.eye.z = z;
    cam.basePose.height = rec.height;
    cam.basePose.distance = rec.distance;
    cam.basePose.yaw = rec.yaw;
    cam.basePose.pitch = rec.pitch;
    cam.basePose.roll = rec.roll;
    cam.unreadD0 = 0;
    cam.lookPitch = 0;
    cam.rollOffset = 0;
    cam.shake = 0;
    cam.unreadDA = 0;
    cam.shakeAmplitude = 0;
}

void cameraTalkStart(const Fixed16Vec3* target) {
    CameraState& cam = cameraState();
    cam.savedTarget = cam.target;
    const int16_t height = cam.pose.height, distance = cam.pose.distance;
    cam.blendToPose.height = height;
    cam.blendFromPose.height = height;
    cam.savedPose.height = height;
    cam.blendToPose.distance = distance;
    cam.blendFromPose.distance = distance;
    cam.savedPose.distance = distance;
    const int16_t yaw = signedAngle(uint16_t(cam.pose.yaw));
    cam.blendToPose.yaw = yaw;
    cam.blendFromPose.yaw = yaw;
    cam.savedPose.yaw = yaw;
    const int16_t pitch = signedAngle(uint16_t(cam.pose.pitch));
    cam.blendToPose.pitch = pitch;
    cam.blendFromPose.pitch = pitch;
    cam.savedPose.pitch = pitch;
    const int16_t roll = signedAngle(uint16_t(cam.pose.roll));
    cam.blendToPose.roll = roll;
    cam.blendFromPose.roll = roll;
    cam.savedPose.roll = roll;
    if (target) cam.blendNode = target;
    cam.enterMode(CameraMode::Blend, BlendStep::AimAtNode);
}

void cameraTalkEnd(bool instant) {
    CameraState& cam = cameraState();
    cam.blendFromTarget = cam.target;
    cam.blendToTarget = cam.savedTarget;
    cam.blendFromPose = cam.pose;
    cam.blendToPose = cam.savedPose;
    cam.blendRequest = kBlendBackToFollow;
    cam.enterMode(CameraMode::Blend, instant ? BlendStep::StartAtOnce : BlendStep::Start8);
}

void cameraRestoreSaved() {
    CameraState& cam = cameraState();
    cam.target = cam.savedTarget;
    cam.pose = cam.savedPose;
    cam.blendRequest = 0;
    cam.enterMode(CameraMode::Follow, BlendStep::AimAtNode);
}

void cameraSetFollow(CameraFollow follow) { cameraState().setFollow(follow); }

void cameraShake(bool force, uint32_t amount, uint16_t decay) {
    CameraState& cam = cameraState();
    if (uint32_t(int32_t(cam.shake)) <= amount || force) {
        cam.shake = int16_t(uint16_t(amount));
        cam.shakeAmplitude = int16_t(decay);
    }
}

void cameraShakeAt(uint32_t strength, uint32_t amount, uint32_t nearDistance, uint32_t farDistance, const Fixed16Vec3& pos) {
    shakeByDistance(strength, amount, nearDistance, farDistance, pos.x.whole, pos.y.whole, pos.z.whole);
}

void cameraShakeAt(uint32_t strength, uint32_t amount, uint32_t nearDistance, uint32_t farDistance, const int16_t pos[3]) {
    shakeByDistance(strength, amount, nearDistance, farDistance, pos[0], pos[1], pos[2]);
}

} // namespace od::port
