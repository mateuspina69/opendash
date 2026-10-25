// The player's interactions (see player_interaction.hpp): the talk / lift target query, talking and the event poses.
#include "game/player/player_interaction.hpp"

#include <cstdlib>

#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/camera.hpp"
#include "game/distances.hpp"
#include "game/event_state.hpp"
#include "game/fixed_point.hpp"
#include "game/message.hpp"
#include "game/player/player_animation.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_substates.hpp"
#include "game/player/player_weapons.hpp"

namespace od::port {

namespace {

constexpr int kTalkReach = 0xc0;      // the talk query's reach past the box's (the lift query's: kLiftReach)
constexpr int kLiftReach = 0x80;
constexpr int kAimCone = 0x200;       // the box's centre within this of the player's yaw
constexpr int kFacingCone = 0x200;    // a kInteractFacing actor faces the player within this
constexpr int kNoTarget = 0x7fff;
constexpr int16_t kHeadTurnLimit = 0x280; // the talking player's head toward the speaker
constexpr uint8_t kEventPoseTicks = 0x10;  // the first pose's ticks (the item get's: kItemGetTicks)
constexpr uint8_t kItemGetTicks = 0x20;

// exe 0x57b5a0(player, actor, yaw, reach): the actor's score as an interaction target of the player looking along yaw:
// the centre of its interaction box (on its 16.16 position) within kAimCone of the yaw, closer than the box's reach +
// reach, the height over the feet inside the box's range; a kInteractFacing actor must also face the player. 0 = no;
// else the distance + the yaw difference / 4
int targetScore(const Player& p, const Actor& a, int16_t yaw, int reach) {
    if (a.interactionBox == nullptr) return 0;
    const InteractionBoxRecord box = *a.interactionBox;
    const int32_t centre[3] = {int32_t(uint32_t(int32_t(box.centreX) << 16) + uint32_t(a.pos.x)),
                               int32_t(uint32_t(int32_t(box.centreY) << 16) + uint32_t(a.pos.y)),
                               int32_t(uint32_t(int32_t(box.centreZ) << 16) + uint32_t(a.pos.z))};
    // exe 0x5e3bc0: the angle from the box's centre to the player
    const uint32_t ang = uint32_t(angleOf(centre[0], centre[2], p.pos.x, p.pos.z));
    if (a.interaction & kInteractFacing) {
        const int d = signedAngle(int(uint16_t(uint16_t(a.rotation.y) - uint16_t(ang))) - kHalfTurn);
        if (std::abs(d) > kFacingCone) return 0;
    }
    int16_t off = signedAngle(int(uint16_t(uint16_t(yaw) - uint16_t(ang))));
    if (off < 0) off = wrap16(-off);
    if (off >= kAimCone) return 0;
    // exe 0x5e3a60: the 3D distance of the integer parts
    const int dist = distance3(p.pos, centre);
    const int h = int(int16_t(uint32_t(centre[1]) >> 16)) - p.pos.y.whole;
    if (dist >= box.reach + reach || h <= box.minHeight || h >= box.maxHeight) return 0;
    return (int(off) >> 2) + dist;
}

// exe 0x57b3b0(player, mode): the nearest interaction target among the model pools (mode 0: the talk targets, reach
// kTalkReach, the body's yaw; mode 1: the liftable ones, kLiftReach, the move direction) -> interactionTarget = the
// query's mode | the actor
int pickTarget(Player& p, int mode) {
    int16_t yaw = p.rotation.y;
    int reach;
    uint8_t m;
    if (mode == 0) {
        reach = kTalkReach;
        m = kInteractTalk;
    } else {
        yaw = wrap16(yaw - p.directionOffset.y);
        reach = kLiftReach;
        m = kInteractLift;
    }
    int best = kNoTarget;
    const Actor* hit = nullptr;
    auto consider = [&](const Actor& a) {
        if ((a.flags & kActorQueued) != kActorQueued || !(m & a.interaction)) return;
        const int r = targetScore(p, a, yaw, reach);
        if (r != 0 && r < best) {
            best = r;
            hit = &a;
        }
    };
    for (size_t i = 0; i < kAnimatedFieldSlots; ++i) consider(animatedPool()[i]);
    for (size_t i = 0; i < ShotPool::size(); ++i) consider(shotPool()[i]);
    for (size_t i = 0; i < ProjectilePool::size(); ++i) consider(projectilePool()[i]);
    for (size_t i = 0; i < StaticPool::size(); ++i) consider(staticPool()[i]);
    if (best == kNoTarget) {
        p.interactionTarget.clear();
        return 0;
    }
    if (m == kInteractLift && (hit->interaction & kInteractLiftTalks)) m = kInteractTalk;
    p.interactionTarget.set(uint32_t(m) << 24, *hit);
    return 1;
}

// exe 0x578e90 / 0x578fd0: the event poses (first for ticks, then second): the motion and the targets cleared, the pose
// plays, the event's window waited for (event flags kEventWaitsWindow), then idle (the input mode cleared unless
// kEventKeepsInput)
void eventPose(Player& p, PlayerAnim first, PlayerAnim second, uint8_t ticks) {
    EventState& event = eventState();
    switch (EventPoseStep(int8_t(p.step))) {
    case EventPoseStep::Enter:
        p.rotation.y = int16_t(uint16_t(uint16_t(p.rotation.y) - uint16_t(p.directionOffset.y)));
        p.directionOffset.y = 0;
        p.setMoveDir(MoveDir::Forward);
        p.leanTarget = 0;
        p.lean = 0;
        p.spin.y = 0;
        stopMotion(p);
        p.clearLockOnTarget();
        p.homingTarget = 0;
        p.lockedActor = 0;
        p.lockSuspended = 1;
        putArmDown(p);
        requestAnim(p, first, 0, false);
        cameraSetFollow(CameraFollow::Lazy);
        p.stepCounter = ticks;
        p.step = uint8_t(p.step + 1u);
        return;
    case EventPoseStep::FirstPose:
        p.stepCounter = uint8_t(p.stepCounter - 1u);
        if (p.stepCounter == 0) {
            requestAnim(p, second, 0, false);
            p.step = uint8_t(p.step + 1u);
        }
        return;
    case EventPoseStep::Wait:
        if (!p.bodyAnimEnded()) return;
        if ((event.flags & kEventWaitsWindow) && (windowStatus(uint8_t(event.window)) & kWindowStatusOpen)) return;
        [[fallthrough]];
    case EventPoseStep::End:
        cameraSetFollow(CameraFollow::Rigid);
        enableFiring(p);
        p.lockSuspended = 0;
        if (!(event.flags & kEventKeepsInput)) p.inputMode = 0;
        p.immunity = 0;
        p.enterSub(PlayerSub::Idle);
        return;
    default: return;
    }
}

} // namespace

int tryTalkTarget(Player& p, int mode) {
    const int busy = weaponBusy(p);
    if (busy < 0) return 0;
    p.interactionTarget.clear();
    if (busy != 0) return 0;
    if (eventState().active != 0) return 0;
    if (mode == kTalkOnPress) {
        if (!p.pressedNow(PlayerButton::Use)) return 0;
    } else if (!p.holds(PlayerButton::Use)) {
        return 0;
    }
    return pickTarget(p, 0);
}

int tryLiftTarget(Player& p) {
    const int busy = weaponBusy(p);
    if (busy < 0) return 0;
    p.interactionTarget.clear();
    if (busy == 0 && eventState().active == 0 && p.holds(PlayerButton::Special) && p.activeSpecial == 0)
        return pickTarget(p, 1);
    return 0;
}

void talkMotion(Player& p) {
    EventState& event = eventState();
    switch (TalkingStep(int8_t(p.step))) {
    case TalkingStep::Enter: {
        requestAnim(p, PlayerAnim::Talk, 0, false);
        stopMotion(p);
        p.spin.y = 0;
        p.directionOffset.y = 0;
        if (event.speakerPosition != nullptr) { // the head toward the speaker (exe 0x5e3bc0)
            const Fixed16Vec3& speaker = *event.speakerPosition;
            const int a = angleOf(speaker, p.pos);
            const int16_t d = signedAngle(a - p.rotation.y);
            p.leanTarget = d;
            if (std::abs(d) > kHeadTurnLimit) p.leanTarget = d < 1 ? int16_t(-kHeadTurnLimit) : kHeadTurnLimit;
        }
        p.step = uint8_t(p.step + 1u);
        return;
    }
    case TalkingStep::Turned:
        if (p.bodyAnimEnded()) p.step = uint8_t(p.step + 1u);
        return;
    case TalkingStep::Talking:
        if (event.newLine != 0) { // a nod on each new line
            if (int8_t(p.body.anim) != int8_t(PlayerAnim::TalkLine) || p.bodyAnimEnded())
                requestAnim(p, PlayerAnim::TalkLine, 0, true);
            event.newLine = 0;
        }
        if ((windowStatus(uint8_t(event.window)) & kWindowStatusOpen) == 0) { // exe 0x557810
            requestAnim(p, PlayerAnim::TalkEnd, 0, false);
            p.leanTarget = 0;
            p.step = uint8_t(p.step + 1u);
        }
        return;
    case TalkingStep::Closing:
        if (p.bodyAnimEnded()) p.enterSub(PlayerSub::Idle);
        return;
    default:
        return;
    }
}

void eventPoseMotion(Player& p) { eventPose(p, PlayerAnim::EventPose, PlayerAnim::EventPoseHold, kEventPoseTicks); }
void itemGetMotion(Player& p) { eventPose(p, PlayerAnim::ItemGet, PlayerAnim::ItemGetHold, kItemGetTicks); }

} // namespace od::port
