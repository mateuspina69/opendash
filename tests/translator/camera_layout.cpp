// The camera's state in the exe's layout (game/camera_state): the camera block 0xa6bca0 and the script camera block
// 0xa6bba0.
#include "game/camera_state.hpp"
#include "translator/layout.hpp"

namespace od::test {

using namespace od::port;

namespace {

void vec(LayoutVisitor& v, uint32_t a, CameraVec& p) {
    v.field(a, p.x);
    v.field(a + 4, p.y);
    v.field(a + 8, p.z);
    v.field(a + 12, p.spare);
}
void pose(LayoutVisitor& v, uint32_t a, CameraPose& p) {
    v.field(a, p.height);
    v.field(a + 2, p.distance);
    v.field(a + 4, p.yaw);
    v.field(a + 6, p.pitch);
    v.field(a + 8, p.roll);
}

} // namespace

void cameraLayout(LayoutVisitor& v) {
    CameraState& c = cameraState();
    constexpr uint32_t k = 0xa6bca0;
    v.field(k + 0x00, c.initialised);
    v.field(k + 0x01, c.unread01);
    v.field(k + 0x02, c.unread02);
    v.field(k + 0x03, c.blendRequest);
    v.field(k + 0x04, c.modeByte);
    v.field(k + 0x05, c.blendStepByte);
    v.field(k + 0x06, c.modeSpare);
    v.field(k + 0x08, c.blendTicks);
    v.field(k + 0x09, c.unread09);
    v.field(k + 0x0a, c.followModeByte);
    v.field(k + 0x0b, c.followStyleByte);
    vec(v, k + 0x0c, c.target);
    vec(v, k + 0x1c, c.prevTarget);
    vec(v, k + 0x2c, c.eye);
    vec(v, k + 0x3c, c.prevEye);
    vec(v, k + 0x4c, c.blendFromTarget);
    vec(v, k + 0x5c, c.blendToTarget);
    vec(v, k + 0x6c, c.savedTarget);
    v.field(k + 0x7c, c.viewAngles);
    v.field(k + 0x84, c.prevViewAngles);
    pose(v, k + 0x94, c.pose);
    v.field(k + 0x9e, c.unread9e);
    v.field(k + 0xa0, c.unreadA0);
    pose(v, k + 0xa8, c.basePose);
    pose(v, k + 0xb2, c.blendFromPose);
    pose(v, k + 0xbc, c.blendToPose);
    pose(v, k + 0xc6, c.savedPose);
    v.field(k + 0xd0, c.unreadD0);
    v.field(k + 0xd2, c.lookPitch);
    v.field(k + 0xd4, c.rollOffset);
    v.field(k + 0xd6, c.unreadD6);
    v.field(k + 0xd8, c.shake);
    v.field(k + 0xda, c.unreadDA);
    v.field(k + 0xdc, c.shakeAmplitude);
    v.field(k + 0xde, c.unreadDE);
    v.field(k + 0xe0, c.blendNode);

    ScriptCamera& s = scriptCamera();
    constexpr uint32_t b = 0xa6bba0;
    v.field(b + 0x02, s.headByte2);
    v.field(b + 0x04, s.anchorByte);
    v.field(b + 0x05, s.lookAt);
    v.field(b + 0x06, s.anchorActor);
    v.field(b + 0x07, s.easing);
    v.field(b + 0x08, s.point);
    v.field(b + 0x14, s.roll);
    v.field(b + 0x18, s.offset);
    v.field(b + 0x28, s.pointSpeed);
    v.field(b + 0x38, s.lookAtSpeed);
    v.field(b + 0x48, s.lookAtPoint);
    v.field(b + 0x58, s.angles);
    v.field(b + 0x64, s.angleSpeed);
    for (size_t i = 0; i < 2; ++i) {
        CameraEase& e = s.eases[i];
        const uint32_t a = b + 0x70 + uint32_t(i) * 0x20;
        v.field(a, e.tick);
        v.field(a + 4, e.length);
        v.field(a + 8, e.from);
        v.field(a + 0x14, e.to);
    }
    v.field(b + 0xb0, s.script);
}

} // namespace od::test
