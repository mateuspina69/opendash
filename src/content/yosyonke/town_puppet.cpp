// The town's cutscene puppets (see town_puppet.hpp).
#include "content/yosyonke/town_puppet.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/cutscene_actor.hpp"

namespace od::port {

namespace {

int16_t t16(int v) { return int16_t(uint16_t(uint32_t(v))); }

constexpr size_t kHeadEntry = 2; // the decoded frame's entry of the head part (part 1)
constexpr int16_t kHeadYawLimit = 0x300, kHeadPitchLimit = 0x200;

// a head lane relaxing toward 0 by the rate
void headRelax(const PuppetFields& f, int16_t& lane) {
    const int16_t v = lane, r = f.headRate;
    if (v < 0) {
        const int16_t n = t16(r + v);
        lane = n > 0 ? int16_t(0) : n;
    } else {
        const int16_t n = t16(v - r);
        lane = n < 0 ? int16_t(0) : n;
    }
}

} // namespace

bool puppetBind(AnimatedActor& a, uint32_t box, uint8_t anim) {
    if (!bindActorModel(a, a.variant)) { // exe 0x5e0bc0(a, a + 6)
        freeActor(a);                     // exe 0x5e1490
        return false;
    }
    a.collisionBox.pointAt(box);
    a.clearClassBlock();
    a.anim = anim;
    a.enterState(1);
    a.prevAnim = kNoAnimShown;
    return true;
}

void puppetTail(const PlayerEnv& env, AnimatedActor& a) {
    PuppetFields& f = classState<PuppetFields>(a);
    if (f.floating == 0) a.pos.y.whole = int16_t(actorGround(env, a)); // exe 0x40f190
    if (a.anim != a.prevAnim || (f.puppetFlags & kPuppetResetAnim)) animSet(a, a.anim, f.animKey); // exe 0x5e1c50
    else animStep(a);                                                                              // exe 0x5e1ea0
    if (f.headOn == 0) {
        headRelax(f, f.headYaw);
        headRelax(f, f.headPitch);
    } else {
        const int16_t rate = f.headRate, head = f.headYaw;
        int d = int((uint32_t(uint16_t(a.rotation.y)) + uint16_t(head)) & kAngleMask) -
                int(uint16_t(f.headTarget) & kAngleMask);
        if (uint32_t(d + kHalfTurn) > 0x1000u) d = int((uint32_t(-d) ^ 0xfffu) + 1u);
        const int half = rate >> 1;
        if (d < -half) {
            const int16_t n = t16(rate + head);
            f.headYaw = n > kHeadYawLimit ? kHeadYawLimit : n;
        } else if (d > half) {
            const int16_t n = t16(head - rate);
            f.headYaw = n < -kHeadYawLimit ? int16_t(-kHeadYawLimit) : n;
        }
        const int16_t pitch = f.headPitch;
        const int v = ((int(a.pos.y.whole) - int(f.headHeight)) >> 1) + pitch;
        if (v < -half) {
            const int16_t n = t16(pitch + rate);
            f.headPitch = n > kHeadPitchLimit ? kHeadPitchLimit : n;
        } else if (v > half) {
            const int16_t n = t16(pitch - rate);
            f.headPitch = n < -kHeadPitchLimit ? int16_t(-kHeadPitchLimit) : n;
        }
    }
    LaneVec4 head = decodedFrame().entries[kHeadEntry];
    head.x = int16_t(uint16_t(uint16_t(f.headPitch) + uint16_t(head.x)));
    head.y = int16_t(uint16_t(uint16_t(f.headYaw) + uint16_t(head.y)));
    animPose(a); // exe 0x5e2d90
}

} // namespace od::port
