// The player in the air: the standing jump, the running jump (whose last steps are every jump's landing: the landing
// animation by the move direction, or straight into the run), the side hop, and the air control with the landing they
// share (exe 0x577310).
#include <cstdlib>

#include "game/audio_logic.hpp"
#include "game/angles.hpp"
#include "game/fixed_point.hpp"
#include "game/player/player_animation.hpp"
#include "game/player/player_carry.hpp"
#include "game/player/player_effects.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_jumps.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_moves.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_substates.hpp"
#include "game/player/player_weapons.hpp"

namespace od::port {

namespace {

constexpr int16_t kJumpSpeed = -0x320;       // the take-off's vertical speed
constexpr int16_t kCarryJumpSpeed = -0x280;  // ... carrying
constexpr int kJumpCutSpeed = 0x240;         // the jump released faster than this rising: the jump cut
constexpr int8_t kDeepWade = 0x30;           // a slowed jump in a deeper wade: half the speed (else 3/4)
constexpr int16_t kRunningJumpSpeed = -0x200; // the running jump's forward speed
constexpr int kAirStickTilt = 0x68;          // the stick steers in the air from this tilt
constexpr int kAirTopSpeed = 0x280;          // the air control's forward speed limit (the skate fall's held speed)
constexpr int kAirSideLimit = 0x100;         // the air control's side speed limit (the skate fall: kFallSideLimit)
constexpr int kFallSideLimit = 0x80;
constexpr int kFallForwardLimit = 0x400;     // the skate fall's forward speed limit
constexpr int kFallAccel = 0x18;             // the skate fall's forward steering
constexpr int kAirAccel = 8;                 // the jumps' forward steering (above kAirFastSpeed: kAirFastAccel)
constexpr int kAirFastAccel = 4;
constexpr int kAirFastSpeed = 0x200;
constexpr int16_t kHopSpeed = 0x200;         // the side hop's speed (to the left: +)
constexpr int16_t kHopRise = -0x80;
constexpr int kLandingSplashes = 6;
constexpr uint8_t kJumpLandDustFrame = 0;    // the frame of the landing animation the dust shows at
constexpr uint8_t kLandDustFrame = 2;
constexpr int16_t kLandingGlideDecay = 0x10;

// the take-off's vertical speed: cut in a wade (half in a deep one) / raised in water
void takeOffSpeed(Player& p) {
    p.velocity.y = p.carryFlags == 0 ? kJumpSpeed : kCarryJumpSpeed;
    if (p.drag != 0) {
        if (p.wadeDepth > kDeepWade)
            p.velocity.y = int16_t(p.velocity.y >> 1);
        else
            p.velocity.y = wrap16((p.velocity.y * 3) >> 2);
    }
}

// a jump's peak (step Peak of both jumps): the falling animation; an arm that holds the moves goes down; then its
// Falling step
template <class Step> void jumpPeak(Player& p, PlayerAnim fall, Step falling) {
    requestAnim(p, fall, 0, true);
    if (armHoldsMoves(p)) {
        p.armFlags &= kArmDownMask;
        if (!(p.carryFlags & kCarryThrowing)) startUpperAnimation(p, 0, fall, 0);
    }
    p.jumpBuffered = 0;
    p.setStep(falling);
}

} // namespace

void airControl(Player& p, int mode) {
    int xmax, accel, zlim;
    if (mode == kAirSkateFall) {
        accel = kFallAccel;
        xmax = kFallSideLimit;
        zlim = kFallForwardLimit;
    } else {
        xmax = kAirSideLimit;
        zlim = kAirTopSpeed;
        accel = std::abs(p.velocity.z) <= kAirFastSpeed ? kAirAccel : kAirFastAccel;
    }
    if (p.burden > 0) zlim = zlim - ((p.burden * int(int16_t(zlim))) >> 6);
    const int x = p.moveX, y = p.moveY;
    const int tilt = std::abs(x) > std::abs(y) ? std::abs(x) : std::abs(y);
    if (tilt >= kAirStickTilt && uint8_t(p.wadeDepth) == 0) {
        const int ang = atanAngle(float(-x), float(-y));
        const int idx = int(p.directionOffset.y) - ang;
        if ((p.armFlags & kArmSpecial) && p.activeSpecial == 0) {
            if (p.slippery == 0) {
                p.velocity.x = 0;
                p.velocity.z = 0;
            }
        } else {
            const int dx = (fixedSin(idx) << 3) >> 12;
            p.velocity.x = wrap16(p.velocity.x - dx);
            const int16_t ax = wrap16(p.velocity.x - dx); // subtracted twice (as the game does)
            p.velocity.x = ax;
            if (std::abs(ax) > int(int16_t(xmax))) p.velocity.x = wrap16(ax > 0 ? xmax : -xmax);
            const int dz = (fixedCos(idx) * int(int16_t(accel))) >> 12;
            p.velocity.z = wrap16(p.velocity.z - dz);
            const int16_t az = wrap16(p.velocity.z - dz);
            p.velocity.z = az;
            if (std::abs(az) > int(int16_t(zlim))) {
                p.velocity.z = wrap16(az > 0 ? zlim : -zlim);
            } else {
                int top = kAirTopSpeed;
                if (p.burden > 0) top = kAirTopSpeed - ((p.burden * 5 << 7) >> 6);
                if (mode == kAirSkateFall) {
                    if (az > 0 && az < int16_t(top)) p.velocity.z = wrap16(top);
                    const int16_t v = p.velocity.z;
                    if (v < 0 && -int(int16_t(top)) < int(v)) p.velocity.z = wrap16(-top);
                }
            }
        }
    }
    turnByAnalogStick(p, kAirTurn, kAnalogGentle);
    leanTowardMove(p);
    if (p.airborne != 0) return;
    // landed
    if (p.slippery == 0) p.velocity.x = 0;
    p.velocity.y = 0;
    p.pushVelocity.x = 0;
    p.pushVelocity.y = 0;
    p.pushVelocity.z = 0;
    if ((p.carryFlags & kCarryThrowing) && !p.interactionTarget.none()) {
        p.carryFlags = kCarryHolding;
        p.upper.anim = uint8_t(PlayerAnim::CarryHold);
        p.upper.key.frame = 0;
    }
    if (mode == kAirJump || mode == kAirRunningJump) {
        requestAnim(p, mode == kAirJump ? PlayerAnim::JumpLand : PlayerAnim::RunningJumpLand, 0, true);
        p.moveToSub(PlayerSub::RunningJump, RunningJumpStep::Landing);
    } else if (mode == kAirSkateFall) {
        requestAnim(p, PlayerAnim::SkateLand, 0, true);
        p.moveToSub(PlayerSub::SkateFall, SkateFallStep::Land);
    }
    putArmDown(p);
    soundPlayAtPlayer(uint16_t(PlayerSound::Land)); // exe 0x5d53e0
    if (p.inWater != 0) splashes(p, kLandingSplashes); // exe 0x57b990 x 6
}

void jumpInput(const PlayerEnv& env, Player& p) {
    if (tryLedgeGrab(env, p) != 0) return;
    if (p.airborne == 0 && p.stepAs<JumpStep>() == JumpStep::TakeOff && trySideHop(p) != 0) return;
    if (p.step > uint8_t(JumpStep::Falling) && p.pressedNow(PlayerButton::Jump) && !inputCaptured()) p.jumpBuffered = 1;
    if (p.moveDir() != MoveDir::Back && tryThrow(p, kThrowAirborne) == 0) fireButtons(p, FireContext::Airborne);
}

void jumpMotion(Player& p) {
    switch (p.stepAs<JumpStep>()) {
    case JumpStep::Crouch:
        p.setMoveDir(MoveDir::Forward);
        p.directionOffset.y = 0;
        p.velocity.y = 0;
        p.pushVelocity.y = 0;
        p.spin.y = 0;
        p.jumpBuffered = 0;
        putArmDown(p);
        requestAnim(p, PlayerAnim::JumpCrouch, 0, true);
        p.setStep(JumpStep::TakeOff);
        return;
    case JumpStep::TakeOff:
        if (!p.bodyAnimEnded()) return;
        p.body.prevAnim = p.body.anim;
        p.airborne = kAirborneFalling;
        takeOffSpeed(p);
        if (p.inWater != 0) p.velocity.y = wrap16((p.velocity.y * 5) >> 2);
        enableFiring(p);
        requestAnim(p, PlayerAnim::JumpRise, 0, true);
        airControl(p, kAirJump);
        if (p.inWater != 0) splash(p); // exe 0x57b990
        soundPlayAtPlayer(uint16_t(PlayerSound::Jump));
        p.setStep(JumpStep::Rising);
        return;
    case JumpStep::Rising:
        if (!p.holds(PlayerButton::Jump) || inputCaptured()) {
            p.setStep(JumpStep::Released);
        } else {
            applyJumpGravity(p, kGravityRise);
            if (p.velocity.y >= 0) {
                requestAnim(p, PlayerAnim::JumpFall, 0, true);
                p.setStep(JumpStep::Peak);
            }
        }
        airControl(p, kAirJump);
        return;
    case JumpStep::Released:
        if (-int(p.velocity.y) > kJumpCutSpeed) {
            applyJumpGravity(p, kGravityCut);
            airControl(p, kAirJump);
            return;
        }
        requestAnim(p, PlayerAnim::JumpFall, 0, true);
        p.setStep(JumpStep::Falling);
        p.velocity.y = 0;
        airControl(p, kAirJump);
        return;
    case JumpStep::Peak:
        jumpPeak(p, PlayerAnim::JumpFall, JumpStep::Falling);
        [[fallthrough]];
    case JumpStep::Falling:
        applyJumpGravity(p, kGravityFall);
        airControl(p, kAirJump);
        return;
    default: return;
    }
}

void runningJumpMotion(Player& p) {
    const uint16_t held = p.held;
    switch (p.stepAs<RunningJumpStep>()) {
    case RunningJumpStep::TakeOff: {
        p.body.prevAnim = p.body.anim;
        p.airborne = kAirborneFalling;
        p.jumpBuffered = 0;
        const int a = atanAngle(float(-int(p.moveX)), float(-int(p.moveY)));
        const uint8_t inWater = p.inWater;
        p.directionOffset.y = int16_t(uint32_t(a) & kAngleMask);
        p.velocity.x = 0;
        if (inWater == 0)
            p.velocity.z = kRunningJumpSpeed;
        else if (p.velocity.z < kRunningJumpSpeed)
            p.velocity.z = kRunningJumpSpeed;
        if (p.burden != 0) p.velocity.z = wrap16(p.velocity.z - ((p.burden * p.velocity.z) >> 6));
        takeOffSpeed(p);
        p.pushVelocity.y = 0;
        if (inWater != 0) p.velocity.y = wrap16((p.velocity.y * 5) >> 2);
        const int16_t sink = p.collisionBox->maxY; // sunk into the floor: the forward speed cut
        if (sink != 0) p.velocity.z = wrap16((((sink >> 2) + 0x10) * p.velocity.z) >> 4);
        enableFiring(p);
        requestAnim(p, PlayerAnim::RunningJumpRise, 0, true);
        airControl(p, kAirRunningJump);
        if (p.inWater != 0) splash(p); // exe 0x57b990
        soundPlayAtPlayer(uint16_t(PlayerSound::Jump));
        p.setStep(RunningJumpStep::RisingHeld);
        return;
    }
    case RunningJumpStep::Rising:
    case RunningJumpStep::RisingHeld: {
        // the steps after Rising / after RisingHeld (the take-off enters RisingHeld)
        const bool rising = p.stepAs<RunningJumpStep>() == RunningJumpStep::Rising;
        if (!(p.button(PlayerButton::Jump) & held) || inputCaptured()) {
            p.setStep(rising ? RunningJumpStep::RisingHeld : RunningJumpStep::Released);
        } else {
            applyJumpGravity(p, kGravityRise);
            if (p.velocity.y >= 0) {
                requestAnim(p, PlayerAnim::RunningJumpFall, 0, true);
                p.setStep(rising ? RunningJumpStep::Peak : RunningJumpStep::Falling);
            }
        }
        airControl(p, kAirRunningJump);
        return;
    }
    case RunningJumpStep::Released:
        if (-int(p.velocity.y) > kJumpCutSpeed) {
            applyJumpGravity(p, kGravityCut);
            airControl(p, kAirRunningJump);
            return;
        }
        requestAnim(p, PlayerAnim::RunningJumpFall, 0, true);
        p.setStep(RunningJumpStep::Falling);
        p.velocity.y = 0;
        airControl(p, kAirRunningJump);
        return;
    case RunningJumpStep::Peak:
        jumpPeak(p, PlayerAnim::RunningJumpFall, RunningJumpStep::Falling);
        [[fallthrough]];
    case RunningJumpStep::Falling:
        applyJumpGravity(p, kGravityFall);
        airControl(p, kAirRunningJump);
        return;
    case RunningJumpStep::Landing: {
        const int8_t frame = p.body.key.frame;
        if (p.body.anim == uint8_t(PlayerAnim::JumpLand) ? frame == kJumpLandDustFrame : frame == kLandDustFrame)
            dustPuff(p, kDustAtFeet); // the landing dust
        if (p.body.key.flags != 0xff) {
            glideDecay(p, kLandingGlideDecay);
            return;
        }
        p.lockSuspended = 0;
        const MoveDir dir = moveDirOfAngle(uint16_t(p.directionOffset.y));
        p.setMoveDir(dir);
        const bool walk = p.holds(PlayerButton::Walk);
        const uint16_t right = p.button(PlayerButton::Right), left = p.button(PlayerButton::Left),
                       back = p.button(PlayerButton::Back), forward = p.button(PlayerButton::Forward);
        bool toRun = false;
        PlayerAnim anim = PlayerAnim::Idle;
        switch (dir) {
        case MoveDir::Forward:
            if (!walk && ((right | left | forward) & held))
                toRun = true;
            else
                anim = PlayerAnim::LandForward;
            break;
        case MoveDir::Left:
            if (!walk && ((left | back | forward) & held)) {
                toRun = true;
            } else if (p.carryFlags & kCarryHolding) {
                p.leanLocked = 1;
                toRun = true;
            } else {
                anim = PlayerAnim::LandLeft;
            }
            break;
        case MoveDir::Back:
            if (!walk && ((right | left | back) & held)) {
                toRun = true;
                break;
            }
            requestAnim(p, PlayerAnim::LandBack, 0, false);
            stopUnlessSlippery(p);
            p.setStep(RunningJumpStep::Landed);
            return;
        case MoveDir::Right:
            if (!walk && ((right | back | forward) & held)) {
                toRun = true;
            } else if (p.carryFlags & kCarryHolding) {
                p.leanLocked = 1;
                toRun = true;
            } else {
                anim = PlayerAnim::LandRight;
            }
            break;
        }
        if (toRun) { // land straight into the run (its BeginStraight step, the landing-into-run animation)
            p.velocity.y = 0;
            p.enterSubAt(PlayerSub::Run, RunStep::BeginStraight);
            requestAnim(p, PlayerAnim::LandIntoRun, 0, false);
            return;
        }
        requestAnim(p, anim, 0, false);
        stopUnlessSlippery(p);
        p.leanTarget = 0;
        p.setStep(RunningJumpStep::Landed);
        return;
    }
    case RunningJumpStep::Landed:
        if (p.bodyAnimEnded() ||
            (p.moveDir() == MoveDir::Back && int16_t(p.body.key.frame) > int16_t((uint16_t(p.directionOffset.y) >> 10) & 3))) {
            turnOrStopVelocities(p, 0, kAlsoLocalVelocity);
            p.enterSub(PlayerSub::Idle);
            p.lockSuspended = 0;
            p.directionOffset.y = 0;
            p.setMoveDir(MoveDir::Forward);
            p.spin.y = 0;
            p.leanLocked = 0;
            enableFiring(p);
            requestAnim(p, PlayerAnim::Idle, 0, false);
        }
        return;
    default: return;
    }
}

void sideHopMotion(Player& p) {
    const bool right = (p.stepCounter & kHopRight) != 0;
    switch (p.stepAs<SideHopStep>()) {
    case SideHopStep::Crouch: {
        p.immunity = kImmuneHeld;
        p.lockSuspended = 1;
        if (int8_t(p.body.key.flags) != -1) return;
        p.airborne = kAirborneFalling;
        p.directionOffset.y = 0;
        PlayerAnim anim;
        if (!right) {
            p.velocity.x = kHopSpeed;
            p.velocity.y = 0;
            p.velocity.z = 0;
            anim = PlayerAnim::HopLeft;
        } else {
            p.velocity.x = int16_t(-kHopSpeed);
            p.velocity.y = -1; // the exe's dword store of -0x200 over x and y
            p.velocity.z = 0;
            anim = PlayerAnim::HopRight;
        }
        requestAnim(p, anim, 0, true);
        p.pushVelocity.x = 0;
        p.pushVelocity.y = 0;
        p.pushVelocity.z = 0;
        if (p.inWater != 0) splash(p); // exe 0x57b990
        soundPlayAtPlayer(uint16_t(PlayerSound::Hop));
        p.setStep(SideHopStep::Launch);
        return;
    }
    case SideHopStep::Launch:
        if (p.bodyAnimEnded()) {
            if (!right) {
                p.setMoveDir(MoveDir::Left);
                p.directionOffset.y = kQuarterTurn;
            } else {
                p.setMoveDir(MoveDir::Right);
                p.directionOffset.y = int16_t(-kQuarterTurn);
            }
            p.velocity.x = 0;
            p.velocity.y = kHopRise;
            p.velocity.z = kRunningJumpSpeed;
            requestAnim(p, PlayerAnim::HopAir, 0, true);
            p.setStep(SideHopStep::Air);
        }
        p.airborne = kAirborneFalling;
        return;
    case SideHopStep::Air:
        applyJumpGravity(p, kGravityRise);
        if (p.bodyAnimEnded()) {
            if ((p.stepCounter & kHopKeepImmunity) == 0) p.immunity = 0;
            if (p.airborne == 0) {
                requestAnim(p, PlayerAnim::HopLand, 0, true);
                p.velocity.y = 0;
                p.setStep(SideHopStep::Land);
                return;
            }
            requestAnim(p, PlayerAnim::HopFall, 0, true);
            p.setStep(SideHopStep::Falling);
            return;
        }
        p.airborne = kAirborneFalling;
        return;
    case SideHopStep::Falling:
        applyJumpGravity(p, kGravityRise);
        if (p.airborne == 0) {
            requestAnim(p, PlayerAnim::HopLand, p.body.key.frame, true);
            p.velocity.y = 0;
            p.setStep(SideHopStep::Land);
            return;
        }
        if (p.bodyAnimEnded()) {
            if (p.stepCounter & kHopKeepImmunity) p.immunity = 0;
            requestAnim(p, PlayerAnim::RunningJumpFall, 0, true);
            enableFiring(p);
            p.lockSuspended = 0;
            p.moveToSub(PlayerSub::RunningJump, RunningJumpStep::Falling);
        }
        return;
    case SideHopStep::Land:
        if (int8_t(p.body.key.events) == kAnimEventLand) {
            if (p.stepCounter & kHopKeepImmunity) p.immunity = 0;
            soundPlayAtPlayer(uint16_t(PlayerSound::Land)); // exe 0x5d53e0
            if (p.inWater != 0) splashes(p, kLandingSplashes); // exe 0x57b990 x 6
            p.moveToSub(PlayerSub::RunningJump, RunningJumpStep::Landing);
        }
        return;
    default: return;
    }
}

} // namespace od::port
