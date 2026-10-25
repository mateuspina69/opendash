// The geometry state in the exe's layout: the geometry scratch 0x6d00c0 (game/geometry_scratch: the room collision
// queries' state, the carried point, the users' own lanes), the camera's view transform and eye 0x6d0014..0x6d0047
// (game/camera_state CameraTransform), the projection's registers 0xa599f0.. (game/projection_state ProjectionState)
// and the player pose's matrix stack 0xa7ab40..0xa7ad3f with its top 0xa7aaa0 (game/player/player_pose).
#include "game/camera_state.hpp"
#include "game/geometry_scratch.hpp"
#include "game/player/player_pose.hpp"
#include "game/projection_state.hpp"
#include "translator/layout.hpp"

namespace od::test {

using namespace od::port;

namespace {

void transformMembers(LayoutVisitor& v, uint32_t a, TransformMembers& t) {
    for (uint32_t i = 0; i < 9; ++i) v.field(a + i * 2, t.matrix.e[i]);
    v.field(a + 0x12, t.pad);
    v.field(a + 0x14, t.translation);
}

void collisionState(LayoutVisitor& v, uint32_t a, CollisionState& c) {
    for (uint32_t i = 0; i < 3; ++i) v.field(a + i * 2, c.point[i]);
    v.field(a + 0x06, c.airborne);
    v.field(a + 0x07, c.landed);
    for (uint32_t i = 0; i < 3; ++i) v.field(a + 0x08 + i * 2, c.previous[i]);
    v.field(a + 0x0e, c.reserved0E);
    for (uint32_t i = 0; i < 3; ++i) v.field(a + 0x10 + i * 2, c.delta[i]);
    v.field(a + 0x16, c.wallDirection);
    v.field(a + 0x18, c.hitX0);
    v.field(a + 0x1a, c.hitTop);
    v.field(a + 0x1c, c.hitZ0);
    v.field(a + 0x1e, c.noStepUp);
    v.field(a + 0x1f, c.lastPushAxis);
    v.field(a + 0x20, c.hitX1);
    v.field(a + 0x22, c.hitBottom);
    v.field(a + 0x24, c.hitZ1);
    v.field(a + 0x26, c.hitShape);
    for (uint32_t i = 0; i < 3; ++i) v.field(a + 0x28 + i * 2, c.pendingPush[i]);
    v.field(a + 0x2e, c.appliedPushZ);
    v.field(a + 0x30, c.appliedPushX);
    v.field(a + 0x32, c.appliedPushY);
    for (uint32_t i = 0; i < 4; ++i) v.field(a + 0x34 + i * 4, c.gatheredCells[i]);
    for (uint32_t i = 0; i < 6; ++i) v.field(a + 0x44 + i * 2, c.box[i]);
    for (uint32_t i = 0; i < 4; ++i) {
        v.field(a + 0x50 + i * 4, c.gatherOffsets[i][0]);
        v.field(a + 0x52 + i * 4, c.gatherOffsets[i][1]);
    }
    for (uint32_t i = 0; i < 4; ++i) v.field(a + 0x60 + i * 4, c.specialCells[i]);
    for (uint32_t i = 0; i < 8; ++i) v.field(a + 0x70 + i * 2, c.userLanes[i]);
}

// the bytes a visitor lays out, into a buffer of the exe's addresses [base, base + n)
class Bytes final : public LayoutVisitor {
public:
    Bytes(uint32_t base, uint8_t* out, size_t n) : base_(base), out_(out), n_(n) {}
    bool storing() const override { return true; }
    void bytes(uint32_t address, void* p, size_t n) override {
        for (size_t i = 0; i < n; ++i)
            if (address + i >= base_ && address + i < base_ + n_) out_[address + i - base_] = static_cast<uint8_t*>(p)[i];
    }

private:
    uint32_t base_;
    uint8_t* out_;
    size_t n_;
};

} // namespace

void collisionStateBytes(CollisionState& c, uint8_t out[0x80]) {
    Bytes b(0x6d00c0, out, 0x80);
    collisionState(b, 0x6d00c0, c);
}

void geometryLayout(LayoutVisitor& v) {
    GeometryScratch& g = geometryScratch();
    collisionState(v, 0x6d00c0, g.collision);
    v.field(0x6d0140, g.carriedPoint);
    v.field(0x6d0148, g.userLanes);

    CameraTransform& c = cameraTransform();
    transformMembers(v, 0x6d0014, c.viewMembers);
    v.field(0x6d0034, c.eyePoint);
    v.field(0x6d003a, c.eyePad);
    v.field(0x6d003c, c.eyeOffset);

    ProjectionState& p = projection();
    v.field(0xa599f0, p.point);
    v.field(0xa59a08, p.viewPoint);
    v.field(0xa59a20, p.nearDepth);
    transformMembers(v, 0xa59a28, p.cameraMembers);
    v.field(0xa59a48, p.clipFlags);
    v.field(0xa59d10, p.focal);
    v.field(0xa59d1c, p.centreY);
    v.field(0xa59d20, p.centreX);

    PoseStack& s = poseStack();
    v.field(0xa7aaa0, s.top);
    for (uint32_t k = 0; k < s.matrices.size(); ++k) {
        const uint32_t a = 0xa7ad40 - (k + 1) * 0x20;
        v.field(a, s.matrices[k].rotation);
        v.field(a + 0x14, s.matrices[k].translation);
    }
}

} // namespace od::test
