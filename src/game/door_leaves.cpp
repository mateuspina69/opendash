// The door leaves (see door_leaves.hpp).
#include "game/actor_state.hpp"
#include "game/door_leaves.hpp"

#include <cstdlib>

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

constexpr const char* kDoorLeafArea = "door leaf port"; // the area of its port-gap errors

// the quadrant axis signs of each type (exe tables, s8 pairs by quadrant: the quadrant's pair is [q], [q + 1])
const DataTable<ScalarTable<int8_t, 5>> hingedLeafSigns{TableId::DoorLeavesHingedLeafSigns};
const DataTable<ScalarTable<int8_t, 5>> doubleLeafSigns{TableId::DoorLeavesDoubleLeafSigns};
const DataTable<ScalarTable<int8_t, 5>> slidingLeafSigns{TableId::DoorLeavesSlidingLeafSigns};

constexpr uint16_t kLeafPushSound = 0xb8;   // the hinged leaf starts to open
constexpr uint16_t kLeafBounceSound = 0xb9; // it bounces closed
constexpr int kHingeAcross = 0x50, kHingeAlong = 0x40; // the hinge's offset from the trigger (type 1)
constexpr int kMaxOpening = 0x3c0;                     // the hand's angle turned into the opening
constexpr int16_t kSwingBackAcceleration = -3;
constexpr int kBounceAccelerationScale = 7;
constexpr int kSlideTicks = 10;  // the slide's speed: (|offset| - kSlideInset) / kSlideTicks a tick
constexpr int kSlideInset = 8;
constexpr uint32_t kQuadrantShift = 10, kQuadrantMask = 3;
constexpr uint32_t kQuadrantBias = 0x200; // the double / sliding leaves' quadrant: the player's yaw + this
constexpr uint32_t kShiftMask = 0x1f;

// the player's yaw quadrant (biased by `bias`)
uint32_t playerQuadrant(uint32_t bias) {
    return ((uint32_t(uint16_t(player().rotation.y)) + bias) >> kQuadrantShift) & kQuadrantMask;
}
// the yaw facing the player (its yaw turned half round)
int16_t facingPlayer() { return int16_t((uint32_t(uint16_t(player().rotation.y)) - uint32_t(kHalfTurn)) & kAngleMask); }

// the leaf's offset (its parameters) turned into the quadrant q of the sign table and added to its position; odd
// variants mirrored
void placeLeafOffset(StaticActor& leaf, const DataTable<ScalarTable<int8_t, 5>>& signs, uint32_t q) {
    const DoorLeafParams params(leaf);
    const int c0 = signs[q], c1 = signs[q + 1];
    const int ox = params.offsetX, oz = params.offsetZ;
    int16_t dx, dz;
    if (!(leaf.variant & 1)) {
        dx = int16_t(ox * c0 + oz * c1);
        dz = int16_t(ox * c1 - c0 * oz);
    } else {
        dx = int16_t(oz * c1 - ox * c0);
        dz = int16_t(-(c0 * oz + ox * c1));
    }
    leaf.pos.x.whole = int16_t(leaf.pos.x.whole + dx);
    leaf.pos.z.whole = int16_t(leaf.pos.z.whole + dz);
}

// the model bound (exe 0x5e0be0); none: the leaf freed (exe 0x5e1540)
bool bindLeafModel(StaticActor& leaf) {
    if (bindActorModel(leaf, leaf.variant)) return true;
    freeActor(leaf);
    return false;
}

// exe 0x5628d0: type 1 Init. A leaf without an offset: its hinge 0x50 / 0x40 off the trigger in the player's quadrant,
// facing the player (the closed yaw), not open
void hingedLeafInit(StaticActor& leaf) {
    if (!bindLeafModel(leaf)) return;
    SwingLeafFields& f = classState<SwingLeafFields>(leaf);
    if (uint8_t(uint16_t(f.offsetX)) == 0) {
        const uint32_t q = playerQuadrant(0);
        const int c1 = hingedLeafSigns[q + 1], c0 = hingedLeafSigns[q];
        leaf.rotation.z = 0;
        leaf.rotation.x = 0;
        leaf.pos.x.whole = int16_t(leaf.pos.x.whole + c0 * kHingeAcross + c1 * kHingeAlong);
        leaf.pos.z.whole = int16_t(leaf.pos.z.whole + c1 * kHingeAcross + c0 * -kHingeAlong);
        f.opening = 0;
        const int16_t yaw = facingPlayer();
        leaf.rotation.y = yaw;
        f.closedYaw = yaw;
    }
    leaf.prevRotation = leaf.rotation;
    leaf.state = uint8_t(DoorLeafState::Moving);
    leaf.subState = uint8_t(SwingLeafSub::Idle);
    leaf.step = 0;
    leaf.stepCounter = 0;
    leaf.prevPos = leaf.pos;
}

// exe 0x5629a0 (type 1) / 0x562c70 (type 2, the even leaf only): the swing of +9. FollowHand opens as far as the
// player's left hand pushes it (the angle from the hinge to the hand's bone point >> the coordinate shift; it only
// grows); SwingBack swings closed with one bounce; SwingOn swings by its speed until the speed turns -> SwingBack
void swingLeafMove(StaticActor& leaf, bool doubleLeaf) {
    if (doubleLeaf && (leaf.variant & 1)) return;
    SwingLeafFields& f = classState<SwingLeafFields>(leaf);
    const auto applyYaw = [&] { leaf.rotation.y = int16_t((f.closedYaw - f.opening) & kAngleMask); };
    switch (SwingLeafSub(leaf.subState)) {
    case SwingLeafSub::FollowHand: {
        if (leaf.step == 0) { // the push's sound once
            soundPlayAtPos(kLeafPushSound, 0);
            ++leaf.step;
        }
        const uint32_t shift = stage().coordinateShift & kShiftMask;
        const ShortVec3& hand = player().bonePoints[kLeftHandBone];
        const int32_t px = (int32_t(hand.x) << 16) >> shift, pz = (int32_t(hand.z) << 16) >> shift;
        const int angle = angleOf(leaf.pos.x, leaf.pos.z, px, pz) & kAngleMask;
        const int16_t open = int16_t(kMaxOpening - (angle & 0x3ff));
        if (f.opening < open) {
            f.opening = open;
            leaf.rotation.y = int16_t((f.closedYaw - open) & kAngleMask);
        }
        return;
    }
    case SwingLeafSub::SwingBack:
        leaf.spin.y = int16_t(leaf.spin.y + f.swingAcceleration);
        f.opening = int16_t(f.opening + leaf.spin.y);
        if (f.opening < 0) {
            f.opening = 0;
            if (leaf.stepCounter == 0) { // the bounce
                leaf.spin.y = int16_t(-(leaf.spin.y >> 1));
                f.swingAcceleration = int16_t(f.swingAcceleration * kBounceAccelerationScale);
                soundPlayAtPos(kLeafBounceSound, 0);
                ++leaf.stepCounter;
                if (!doubleLeaf) {
                    applyYaw();
                    return;
                }
            } else {
                leaf.subState = uint8_t(SwingLeafSub::Idle);
                if (doubleLeaf) ++leaf.stepCounter;
            }
        }
        applyYaw();
        return;
    case SwingLeafSub::SwingOn:
        leaf.spin.y = int16_t(leaf.spin.y + f.swingAcceleration);
        f.opening = int16_t(f.opening + leaf.spin.y);
        applyYaw();
        if (leaf.spin.y < 0) {
            leaf.subState = uint8_t(SwingLeafSub::SwingBack);
            leaf.spin.y = 0;
            f.swingAcceleration = kSwingBackAcceleration;
        }
        return;
    default: return;
    }
}

// exe 0x562b70: type 2 Init: the offset in the player's quadrant (biased), facing the player, not open
void doubleLeafInit(StaticActor& leaf) {
    if (!bindLeafModel(leaf)) return;
    SwingLeafFields& f = classState<SwingLeafFields>(leaf);
    placeLeafOffset(leaf, doubleLeafSigns, playerQuadrant(kQuadrantBias));
    leaf.rotation.z = 0;
    leaf.rotation.x = 0;
    f.opening = 0;
    leaf.state = uint8_t(DoorLeafState::Moving);
    leaf.subState = uint8_t(SwingLeafSub::Idle);
    leaf.step = 0;
    leaf.stepCounter = 0;
    leaf.rotation.y = facingPlayer();
    leaf.prevRotation = leaf.rotation;
    f.closedYaw = leaf.rotation.y;
    leaf.prevPos = leaf.pos;
}

// exe 0x562e30: type 3 Init: the offset in the player's quadrant, facing away from the player while leaving (the
// sliding door's first run), along the player when arriving; still
void slidingLeafInit(StaticActor& leaf) {
    if (!bindLeafModel(leaf)) return;
    placeLeafOffset(leaf, slidingLeafSigns, playerQuadrant(0));
    leaf.rotation.z = 0;
    leaf.rotation.x = 0;
    leaf.rotation.y = storyFlag(kFlagTransitionArriving) ? int16_t(player().rotation.y) : facingPlayer();
    leaf.prevRotation = leaf.rotation;
    leaf.velocity.z = 0;
    leaf.prevPos.x = leaf.pos.x;
    leaf.velocity.y = 0;
    leaf.prevPos.y = leaf.pos.y;
    leaf.velocity.x = 0;
    ++leaf.state;
    leaf.prevPos.z = leaf.pos.z;
}

// exe 0x562f40: type 3 Moving: Open / Close slide (|offset| - 8) / 10 a tick (its sign by the variant's parity, flipped
// while arriving) along the player's biased quadrant, 10 ticks to the end point; Stop holds it
void slidingLeafMove(StaticActor& leaf) {
    SlidingLeafFields& f = classState<SlidingLeafFields>(leaf);
    const uint32_t q = playerQuadrant(kQuadrantBias);
    const SlidingLeafSub sub = SlidingLeafSub(leaf.subState);
    if (sub == SlidingLeafSub::Stop) {
        leaf.velocity.x = 0;
        leaf.velocity.z = 0;
    } else if ((sub == SlidingLeafSub::Open || sub == SlidingLeafSub::Close) && leaf.step == 0) {
        const int c = std::abs(int(f.offsetX)) - kSlideInset;
        // open: even variants +, odd -; close: the reverse
        const bool positive = (sub == SlidingLeafSub::Open) == !(leaf.variant & 1);
        int16_t speed = int16_t(positive ? c / kSlideTicks : c / -kSlideTicks);
        if (storyFlag(kFlagTransitionArriving)) speed = int16_t(-speed);
        const int c0 = slidingLeafSigns[q], c1 = slidingLeafSigns[q + 1];
        leaf.velocity.x = int16_t(c0 * speed);
        leaf.velocity.z = int16_t(c1 * speed);
        f.targetX = int16_t(c0 * speed * kSlideTicks + leaf.pos.x.whole);
        f.targetZ = int16_t(leaf.pos.z.whole + leaf.velocity.z * kSlideTicks);
        ++leaf.step;
    }
    // one axis: moved by its speed, stopped on the end point once it reaches it
    const auto slide = [](int16_t& speedField, const int16_t& target, int16_t& pos) {
        const int16_t speed = speedField;
        if (speed == 0) return;
        const int16_t t = target, p = pos;
        if (speed < 1 ? p <= t : t <= p) {
            pos = t;
            speedField = 0;
            return;
        }
        pos = int16_t(p + speed);
    };
    slide(leaf.velocity.x, f.targetX, leaf.pos.x.whole);
    slide(leaf.velocity.z, f.targetZ, leaf.pos.z.whole);
}

// the leaf's update by its state
void doorLeafTick(StaticActor& leaf) {
    const uint8_t type = leaf.type, state = leaf.state;
    if (DoorLeafState(state) == DoorLeafState::Free) { // exe 0x5afdd0
        freeActor(leaf);
        return;
    }
    if (state > uint8_t(DoorLeafState::Free)) notPortedAt(kDoorLeafArea, "door leaf state", state);
    const bool init = DoorLeafState(state) == DoorLeafState::Init;
    switch (type) {
    case kHingedLeafType: init ? hingedLeafInit(leaf) : swingLeafMove(leaf, false); break;
    case kDoubleLeafType: init ? doubleLeafInit(leaf) : swingLeafMove(leaf, true); break;
    case kSlidingLeafType: init ? slidingLeafInit(leaf) : slidingLeafMove(leaf); break;
    default: break;
    }
}

// the leaves the pass updates: global-table (not stage-table) statics of types 1..3
bool isDoorLeaf(const StaticActor& actor) {
    return !(actor.loopFlags & kActorStageClass) && actor.type >= kHingedLeafType && actor.type <= kSlidingLeafType;
}

} // namespace

void doorLeafUpdate(const PlayerEnv&, uint32_t actor) { doorLeafTick(staticActorAt(actor)); }

int staticActorsUpdate() {
    int n = 0;
    for (size_t i = 0; i < StaticPool::size(); ++i) {
        StaticActor& a = staticPool()[i];
        if (!a.live() || (a.loopFlags & kActorLateLoop) || !isDoorLeaf(a)) continue;
        doorLeafTick(a);
        ++n;
    }
    return n;
}

bool staticActorPorted(uint32_t actor) { return isDoorLeaf(staticActorAt(actor)); }

} // namespace od::port
