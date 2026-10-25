// The player hanging from a bar or a ledge (see player_hang.hpp): the grabs, the hang sub-states (the grab, the hang,
// the pull-up / the climb, the drop) and the release.
#include "game/player/player_hang.hpp"

#include <cstdlib>

#include "game/actor_movement.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/fixed_point.hpp"
#include "game/player/player_animation.hpp"
#include "game/player/player_effects.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_substates.hpp"
#include "game/player/player_weapons.hpp"

namespace od::port {

namespace {

constexpr int kHandsHeight = 0xb4;            // the hands over the feet
constexpr uint32_t kHitsLetGo = 0x3f00;       // the push-out / collision bits of the wall hit that let the bar go
constexpr int16_t kBarDropSpeed = 0x30;       // the drop's vertical / forward speed
constexpr int16_t kBarDropForward = 0x2a;
constexpr int16_t kBarFlipUp = -0x140;        // the flip over the bar
constexpr int16_t kBarFlipForward = -0x280;
constexpr int16_t kLedgeClimbUp = -0x90;
constexpr int16_t kLedgeJumpUp = -0x1b0;      // the climb's jump onto the ledge (in water: kLedgeJumpUpInWater)
constexpr int16_t kLedgeJumpUpInWater = -0x230;
constexpr int16_t kLedgeJumpForward = -0x50;
constexpr int kLedgeFollowWindow = 0x20;      // the ledge's top within this of the hands: still hanging
constexpr int kLedgeReach = 0x20;             // the wall within this: the ledge grab
constexpr int kLedgeProbeSide = 0x200;        // the probes on both sides of the wall's normal
constexpr int kLedgeMinDrop = 0x68;           // the ledge's top 0x68..0xb4 above the feet
constexpr int kLedgeFallClearance = -0x40;    // falling past the ground by this before the grab
constexpr int kLedgeSplashes = 6;
constexpr uint8_t kBarFallLandFrame = 1;

// exe 0x577bd0: hanging from the bar: forward pressed (hanging) -> the pull-up; back pressed or the bar gone -> the drop
void barHangControl(Player& p) {
    if (p.pressedNow(PlayerButton::Forward) && p.stepAs<BarHangStep>() == BarHangStep::Hang) {
        requestAnim(p, PlayerAnim::BarPullUp, 0, false);
        p.step = uint8_t(p.step + 1u);
        return;
    }
    if (p.pressedNow(PlayerButton::Back) || p.interactionTarget.none()) {
        p.velocity.x = 0;
        p.velocity.y = kBarDropSpeed;
        p.velocity.z = kBarDropForward;
        p.wasAirborne = kAirborneFalling;
        p.airborne = kAirborneFalling;
        p.lockSuspended = 0;
        p.interactionTarget.clear();
        enableFiring(p);
        requestAnim(p, PlayerAnim::JumpFall, 0, true);
        cameraSetFollow(CameraFollow::Rigid);
        p.setStep(BarHangStep::Drop);
    }
}

// exe 0x577ed0: the ledge let go -> the jump's fall (its Peak step)
void releaseLedge(Player& p) {
    p.enterSubAt(PlayerSub::Jump, JumpStep::Peak);
    p.wasAirborne = kAirborneFalling;
    p.airborne = kAirborneFalling;
    p.lockSuspended = 0;
    stopMotion(p);
    enableFiring(p);
    requestAnim(p, PlayerAnim::JumpFall, 0, true);
    cameraSetFollow(CameraFollow::Rigid);
}

// the grab's first step (both hangs): the motion stopped, held up, the arm down
void startHang(Player& p) {
    stopMotion(p);
    p.spin.y = 0;
    p.lean = 0;
    p.leanTarget = 0;
    p.wasAirborne = kAirborneHanging;
    p.airborne = kAirborneHanging;
    p.onPlatform = 0;
    p.lockSuspended = 1;
    p.stopSkating();
}

} // namespace

int playerBarGrab(const Actor& bar) {
    Player& p = player();
    if (weaponBusy(p) != 0) return 0;
    if (p.interactionTarget.is(bar, kInteractBarMode)) { // hanging from this bar
        if (uint16_t(p.wallHit) & kHitsLetGo)
            p.interactionTarget.clear();
        else if (p.sub() == PlayerSub::BarHang)
            return 2;
        return 0;
    }
    const PlayerSub sub = p.sub();
    if ((sub != PlayerSub::Jump && sub != PlayerSub::RunningJump && sub != PlayerSub::SkateFall) ||
        p.airborne != kAirborneFalling)
        return 0;
    const int angle = uint16_t(bar.rotation.y) & kAngleMask;
    const InteractionBoxRecord box = *bar.interactionBox;
    const int bz = box.centreZ, bx = box.centreX;
    const int c = fixedCos(angle), sn = fixedSin(angle);
    int i1 = (bx * c) >> 12;
    int i4 = (bz * sn) >> 12;
    int d = p.pos.x.whole - bar.pos.x.whole - i1 - i4;
    if (d < 0) d = bar.pos.x.whole - p.pos.x.whole + i1 + i4;
    if (d > box.reach) return 0;
    i4 = (bx * sn) >> 12;
    const int i5 = (c * bz) >> 12;
    d = p.pos.z.whole - bar.pos.z.whole - i4 - i5;
    if (d < 0) d = bar.pos.z.whole - p.pos.z.whole + i4 + i5;
    if (d > box.reach) return 0;
    const int y = box.centreY - bar.pos.y.whole - kHandsHeight + p.pos.y.whole;
    if (y < box.minHeight || y > box.maxHeight) return 0;
    cameraSetFollow(CameraFollow::Lazy);
    p.rotation.y = int16_t(uint16_t(uint16_t(p.rotation.y) - uint16_t(p.directionOffset.y)));
    p.interactionTarget.set(kInteractBarMode, bar);
    p.directionOffset.y = 0;
    p.setMoveDir(MoveDir::Forward);
    p.enterStep(0);
    p.subState = uint8_t(PlayerSub::BarHang);
    return 1;
}

void playerPlaceAt(Actor& bar, int keepYaw, int x, int y, int z) {
    Player& p = player();
    uint32_t angle;
    if (keepYaw == 0) {
        angle = uint16_t(uint16_t(bar.rotation.y) - kHalfTurn) & kAngleMask;
        p.rotation.y = int16_t(angle);
    } else {
        bar.rotation.y = int16_t(uint16_t(uint16_t(p.rotation.y) - uint16_t(p.directionOffset.y)));
        angle = uint16_t(p.rotation.y);
    }
    angle &= kAngleMask;
    const int sn = fixedSin(int(angle)), c = fixedCos(int(angle));
    p.pos.x = int32_t(uint32_t(((c * x + sn * z) << 4) + bar.pos.x));
    p.pos.y = int32_t(uint32_t(((y + kHandsHeight) << 16) + bar.pos.y));
    p.pos.z = int32_t(uint32_t(((c * z + sn + x) << 4) + bar.pos.z));
}

void barHangInput(Player& p) {
    if (p.step != 0 && p.airborne == kAirborneFalling) p.interactionTarget.clear(); // the pull-up / the fall: the bar let go
}

void barHangMotion(Player& p) {
    switch (p.stepAs<BarHangStep>()) {
    case BarHangStep::Grab:
        startHang(p);
        p.carryFlags = 0;
        putArmDown(p);
        requestAnim(p, PlayerAnim::HangGrab, 0, false);
        p.step = uint8_t(p.step + 1u);
        return;
    case BarHangStep::GrabWait:
        if (p.bodyAnimEnded()) {
            requestAnim(p, PlayerAnim::Hang, 0, false);
            p.step = uint8_t(p.step + 1u);
        }
        barHangControl(p);
        return;
    case BarHangStep::Hang:
        turnByAnalogStick(p, kStandTurn, kAnalogGentle);
        barHangControl(p);
        return;
    case BarHangStep::PullUp:
        turnByAnalogStick(p, kStandTurn, kAnalogGentle);
        if (p.bodyAnimEnded()) {
            requestAnim(p, PlayerAnim::BarFlip, 0, false);
            p.velocity.y = kBarFlipUp;
            p.velocity.z = kBarFlipForward;
            p.wasAirborne = kAirborneFalling;
            p.airborne = kAirborneFalling;
            p.interactionTarget.clear();
            p.step = uint8_t(p.step + 1u);
        }
        return;
    case BarHangStep::Flip:
        turnByAnalogStick(p, kStandTurn, kAnalogGentle);
        if (p.bodyAnimEnded()) p.moveToSub(PlayerSub::RunningJump, RunningJumpStep::Peak);
        return;
    case BarHangStep::Drop:
        applyJumpGravity(p, kGravityRise);
        if (uint8_t(p.body.key.frame) == kBarFallLandFrame) p.enterSubAt(PlayerSub::Jump, JumpStep::Peak);
        return;
    default: return;
    }
}

void ledgeHangInput(Player& p) {
    if (p.step != 0 && p.airborne == kAirborneFalling) releaseLedge(p); // the hang ended in mid-air
}

void ledgeHangMotion(const PlayerEnv& env, Player& p) {
    const int8_t step = int8_t(p.step);
    switch (LedgeHangStep(step)) {
    case LedgeHangStep::Grab:
        startHang(p);
        putArmDown(p);
        requestAnim(p, PlayerAnim::HangGrab, 0, false);
        soundPlayAtPlayer(uint16_t(PlayerSound::Grab));
        p.step = uint8_t(step + 1);
        return;
    case LedgeHangStep::GrabWait:
        if (p.bodyAnimEnded()) {
            requestAnim(p, PlayerAnim::Hang, 0, false);
            p.step = uint8_t(step + 1);
        }
        return;
    case LedgeHangStep::Hang: {
        if (p.pressedNow(PlayerButton::Back)) { // back (down) pressed: the drop
            p.velocity.x = 0;
            p.velocity.y = 0;
            p.velocity.z = kBarDropForward;
            releaseLedge(p);
            return;
        }
        // the grab point follows the player's own move (a moving platform)
        p.ledgePoint.x = int32_t(p.ledgePoint.x + (p.pos.x - p.prevPos.x));
        p.ledgePoint.y = int32_t(p.ledgePoint.y + (p.pos.y - p.prevPos.y));
        p.ledgePoint.z = int32_t(p.ledgePoint.z + (p.pos.z - p.prevPos.z));
        const int16_t probe[3] = {p.ledgePoint.x.whole, p.ledgePoint.y.whole, p.ledgePoint.z.whole};
        const int top = probeGroundAt(env, probe, CollisionBoxRef::record(0), 0, kPlayerLayers) + kHandsHeight; // the exe adds 0x5a to a ushort*
        if (std::abs(int(p.pos.y.whole) - top) < kLedgeFollowWindow) {
            p.pos.y = int32_t(uint32_t(top) << 16);
            if (!p.holds(PlayerButton::Forward)) return;
            p.velocity.x = 0;
            p.velocity.y = kLedgeClimbUp;
            p.velocity.z = 0;
            requestAnim(p, PlayerAnim::LedgeClimb, 0, false);
            p.step = uint8_t(step + 1);
            return;
        }
        releaseLedge(p); // the ledge is gone
        return;
    }
    case LedgeHangStep::Climb:
        if (int8_t(p.body.key.events) == kAnimEventLand) p.step = uint8_t(step + 1);
        return;
    case LedgeHangStep::ClimbJump:
        p.velocity.x = 0;
        p.velocity.y = p.inWater == 0 ? kLedgeJumpUp : kLedgeJumpUpInWater;
        p.velocity.z = kLedgeJumpForward;
        p.step = uint8_t(step + 1);
        return;
    case LedgeHangStep::Airborne:
        applyJumpGravity(p, kGravityRise);
        if (p.airborne == 0) {
            requestAnim(p, PlayerAnim::JumpLand, 0, true);
            soundPlayAtPlayer(uint16_t(PlayerSound::Land));
            if (p.inWater != 0) splashes(p, kLedgeSplashes); // exe 0x57b990 x 6
            p.moveToSub(PlayerSub::RunningJump, RunningJumpStep::Landing);
        }
        if (!p.bodyAnimEnded()) return;
        releaseLedge(p);
        return;
    default:
        return;
    }
}

int tryLedgeGrab(const PlayerEnv& env, Player& p) {
    if (p.airborne == 0 || p.carryFlags != 0 || p.velocity.y <= 0) return 0;
    const int ground = actorGround(env, p);
    if (!(int(p.pos.y.whole) <= ground + (kLedgeFallClearance - p.collisionBox->maxY))) return 0;
    uint16_t wallDir = 0;
    const uint32_t w = actorWallQuery(env, p, &wallDir);
    const int dx = int16_t(w), dz = int32_t(w) >> 16;
    const int d = std::abs(std::abs(dx) < std::abs(dz) ? dx : dz);
    if (d > kLedgeReach) return 0;
    const int reach = int(p.collisionBox->minZ) * 0xd >> 3; // 13/8 of the box
    int32_t a[3], b[3]; // the probe points (16.16): to the left / right of the contact
    a[0] = fixedSin(wallDir - kLedgeProbeSide) * reach * 0x10 + p.pos.x;
    b[0] = fixedSin(wallDir + kLedgeProbeSide) * reach * 0x10 + p.pos.x;
    if (std::abs(dx) < kLedgeReach) {
        a[0] += dx * 0x10000;
        b[0] += dx * 0x10000;
    }
    a[2] = fixedCos(wallDir - kLedgeProbeSide) * reach * 0x10 + p.pos.z;
    b[2] = fixedCos(wallDir + kLedgeProbeSide) * reach * 0x10 + p.pos.z;
    if (std::abs(dz) < kLedgeReach) {
        a[2] += dz * 0x10000;
        b[2] += dz * 0x10000;
    }
    a[1] = b[1] = p.pos.y - (kHandsHeight << 16);
    const int16_t pa[3] = {int16_t(a[0] >> 16), int16_t(a[1] >> 16), int16_t(a[2] >> 16)};
    const int16_t pb[3] = {int16_t(b[0] >> 16), int16_t(b[1] >> 16), int16_t(b[2] >> 16)};
    const int ga = probeGroundAt(env, pa, CollisionBoxRef::record(0), 0, kPlayerLayers);
    const int gb = probeGroundAt(env, pb, CollisionBoxRef::record(0), 0, kPlayerLayers);
    const int py = p.pos.y.whole;
    if (ga != gb || !(ga < py - kLedgeMinDrop) || !(py - kHandsHeight < ga)) return 0;
    cameraSetFollow(CameraFollow::Lazy);
    p.ledgePoint.x = a[0];
    p.ledgePoint.y = a[1];
    p.ledgePoint.z = a[2];
    if (std::abs(dx) < kLedgeReach) p.pos.x = int32_t(p.pos.x + dx * 0x10000);
    if (std::abs(dz) < kLedgeReach) p.pos.z = int32_t(p.pos.z + dz * 0x10000);
    p.pos.y = int32_t(uint32_t(ga + kHandsHeight) << 16);
    p.rotation.y = int16_t(wallDir);
    p.directionOffset.y = 0;
    p.setMoveDir(MoveDir::Forward);
    p.enterSub(PlayerSub::LedgeHang);
    return 1;
}

} // namespace od::port
