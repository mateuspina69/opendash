// The player's carry and grabs (see player_carry.hpp).
#include "game/player/player_carry.hpp"

#include <cstdlib>

#include "game/actor_pools.hpp"
#include "game/audio_logic.hpp"
#include "game/game_state.hpp"
#include "game/player/player_animation.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_moves.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_substates.hpp"
#include "game/player/player_weapons.hpp"

namespace od::port {

namespace {

constexpr uint8_t kThrowReleaseFrame = 2;   // the upper body's throw animation starts at this step
constexpr uint16_t kStruggleHiddenBones = 0x7ffc; // a heavy grab: only the body and the head shown
constexpr uint16_t kArmReadyBits = kArmReady | kArmSpecialEnergy | kArmBusterFree;

} // namespace

int tryThrow(Player& p, int16_t mode) {
    const uint16_t f = p.carryFlags;
    if (!((p.armFlags & kArmReadyBits) && (f & kCarryHolding) && !(f & kCarryThrowing))) return 0;
    const bool lockReached = (p.lockFlags & (kLockPitchOn | kLockYawOn)) == (kLockPitchOn | kLockYawOn);
    if (gameOptions().autoLockSpecial == 0 || p.lockedActor == nullptr || lockReached) {
        if ((f & kCarryThrowQueued) || p.pressedNow(PlayerButton::Special)) {
            if (mode == kThrowStanding) {
                p.carryFlags = kCarryThrowing;
                p.resetCarryStep();
                p.enterSub(PlayerSub::Idle);
            } else if (mode == kThrowRunning) {
                p.subState = uint8_t(PlayerSub::Run);
                p.carryFlags = uint16_t(f | kCarryThrowQueued);
                p.resetStep(RunStep::Stop);
                return 1;
            } else if (mode == kThrowAirborne) {
                if (p.airborne == 0) return 0;
                p.carryFlags = kCarryThrowing | kCarryHolding;
                p.resetCarryStep();
                return 1;
            }
            return 1;
        }
    } else if (p.pressedNow(PlayerButton::Special)) {
        p.carryFlags = uint16_t(f | kCarryThrowQueued);
    }
    return 0;
}

void throwUpdate(Player& p) {
    const uint8_t step = p.carryStep;
    if (step == 0) { // the throw's arms
        putArmDown(p);
        p.lean = 0;
        p.leanTarget = 0;
        if (p.carryFlags & kCarryHolding) {
            p.upper.anim = uint8_t(PlayerAnim::CarryThrow);
            p.upper.key.frame = kThrowReleaseFrame;
        }
        p.carryStep = uint8_t(p.carryStep + 1u);
    } else if (step == 1) { // the animation's event: the actor leaves the hands
        if (p.upper.key.events != 0) {
            p.setCarryBits(kCarryReleased);
            p.interactionTarget.clear();
            p.carryStep = 2;
            soundPlayAtPlayer(uint16_t(PlayerSound::Throw));
        }
    } else if (step == 2) { // the arms' animation ends: everything released
        p.carryFlags &= uint16_t(~kCarryReleased);
        if (p.upperAnimEnded()) {
            enableFiring(p);
            p.carryFlags = 0;
            if (p.sub() != PlayerSub::Jump && p.sub() != PlayerSub::RunningJump) {
                p.enterSub(PlayerSub::Idle);
                return;
            }
            p.upper.anim = p.body.anim;
            p.upper.key.frame = p.body.key.frame;
        }
    }
}

void liftMotion(Player& p) {
    if (p.stepAs<LiftStep>() == LiftStep::Lift) { // stop, the lift animation
        p.setStep(LiftStep::Lifting);
        p.lean = 0;
        p.leanTarget = 0;
        stopMotion(p);
        p.clearLockOnTarget();
        p.homingTarget = 0;
        p.lockedActor = 0;
        p.lockSuspended = 1;
        putArmDown(p);
        requestAnim(p, PlayerAnim::Lift, 0, false);
        soundPlayAtPlayer(uint16_t(PlayerSound::Lift));
        return;
    }
    if (!p.bodyAnimEnded()) return;
    // holding: the arms' hold on the upper body; idle, or back to the run in the direction held, else the run's stop
    p.setCarryBits(kCarryHolding);
    p.upper.anim = uint8_t(PlayerAnim::CarryHold);
    p.upper.key.frame = 0;
    uint16_t dir;
    switch (p.moveDir()) {
    case MoveDir::Forward:
        enableFiring(p);
        turnOrStopVelocities(p, 0, kAlsoLocalVelocity);
        p.lockSuspended = 0;
        p.enterSub(PlayerSub::Idle);
        return;
    case MoveDir::Left: dir = p.button(PlayerButton::Left); break;
    case MoveDir::Back: dir = p.button(PlayerButton::Back); break;
    case MoveDir::Right: dir = p.button(PlayerButton::Right); break;
    default: dir = 0; break;
    }
    if (dir & p.held) {
        enableFiring(p);
        p.lockSuspended = 0;
        p.setPrevMoveDir(p.moveDir());
        p.enterSub(PlayerSub::Run);
        return;
    }
    p.enterSubAt(PlayerSub::Run, RunStep::Stop);
}

void heldInput(Player& p) {
    if (p.step < uint8_t(HeldStep::Struggle)) tryJump(p);
}

void heldMotion(Player& p) {
    const uint8_t step = p.step;
    if (step == uint8_t(HeldStep::Struggle)) {
        requestAnim(p, PlayerAnim::Struggle, 0, false);
        selectIdleTables(p, kIdleStrained, kIdleStrained);
        p.step = uint8_t(step + 2);
        return;
    }
    if (step != uint8_t(HeldStep::Grabbed) && step != uint8_t(HeldStep::GrabbedStruggling)) return;
    stopMotion(p);
    p.clearLockOnTarget();
    p.homingTarget = 0;
    p.lockedActor = 0;
    p.lockSuspended = 1;
    p.directionOffset.y = 0;
    p.setMoveDir(MoveDir::Forward);
    p.lean = 0;
    p.leanTarget = 0;
    p.immunity = 0;
    putArmDown(p);
    if (p.stepAs<HeldStep>() == HeldStep::Grabbed) {
        requestAnim(p, PlayerAnim::HeldPose, 0, false);
    } else {
        requestAnim(p, PlayerAnim::Struggle, 0, false);
        selectIdleTables(p, kIdleStrained, kIdleStrained);
    }
    p.step = uint8_t(p.step + 1u);
}

int playerGrab(const Actor& a, int range, int flag) {
    Player& p = player();
    if (p.airborne != 0) return 0;
    if (std::abs(int(p.pos.x.whole) - int(a.pos.x.whole)) >= range || std::abs(int(p.pos.y.whole) - int(a.pos.y.whole)) >= range ||
        std::abs(int(p.pos.z.whole) - int(a.pos.z.whole)) >= range)
        return 0;
    p.pos = a.pos;
    p.enterStep(flag ? uint8_t(HeldStep::Grabbed) : uint8_t(HeldStep::GrabbedStruggling));
    p.subState = uint8_t(PlayerSub::Held);
    return 1;
}

int playerGrabHold(const Actor& actor, uint16_t& counter, int heavy) {
    Player& p = player();
    p.pos = actor.pos;
    const uint16_t moves = uint16_t(p.button(PlayerButton::Forward) | p.button(PlayerButton::Back) |
                                    p.button(PlayerButton::Left) | p.button(PlayerButton::Right) |
                                    p.button(PlayerButton::Jump));
    if ((moves & p.pressed) != 0 && !inputCaptured()) { // (the exe also tests the counter pointer: a class field, never null)
        counter = uint16_t(counter - 1u); // the grabbing class's struggle counter
        if (counter == 0) { // broke free
            selectIdleTables(p, kIdleCalm, kIdleCalm);
            p.stageAnimMarker = 0;
            p.enterSub(PlayerSub::Idle);
            return 1;
        }
    }
    if (heavy) p.hiddenBones = kStruggleHiddenBones;
    return 0;
}

} // namespace od::port
