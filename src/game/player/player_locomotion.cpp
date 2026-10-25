// The player on its feet: the sub-states idle, walk, run (with its directional starts and stops), turn in place, quick
// turn, side step and look -- their input handlers (table A) and their motion / animation handlers (table B).
#include <cstdlib>

#include "engine/data_source.hpp"
#include "game/audio_logic.hpp"
#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/fixed_point.hpp"
#include "game/player/moving_fire.hpp"
#include "game/player/player_animation.hpp"
#include "game/player/player_carry.hpp"
#include "game/player/player_effects.hpp"
#include "game/player/player_interaction.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_moves.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_substates.hpp"
#include "game/player/player_weapons.hpp"

namespace od::port {

namespace {

constexpr int kStickDeadZone = 0x38;
constexpr int kDigitalTilt = 0x7f;
constexpr int16_t kWalkSpeed = 0x50;        // the walk's speed (forward: -)
constexpr int16_t kWalkRamp = 0x10;         // its ramp on a slippery floor
constexpr int16_t kWalkSlipLimit = 0x60;
constexpr int16_t kSideStepSpeed = 0x2e;    // the side step's (to the right: -)
constexpr int16_t kSideStepRamp = 0x20;
constexpr int16_t kSideStepSlipLimit = 0x4e;
constexpr int16_t kQuickTurnSpeed = 0xc0;   // the quick turn's yaw per tick
constexpr int kQuickTurnEnd = 0xc0;         // ... until the move direction is this close to the half turn
constexpr int16_t kRunStopDeceleration = 0x30;
constexpr uint8_t kRunRestartFrame = 9;     // the run's frame a left start continues at
constexpr uint8_t kRunStartFrame = 1;       // the start animation's frame that ends BeginHold
constexpr uint8_t kRunSettleFrame = 5;      // the run's frame that ends Settling
constexpr int16_t kIdleGlideDecay = 0x20;
constexpr int16_t kTurnGlideDecay = 0x10;
constexpr int kLookAimMax = 0x40;           // the look's aim turn limit / the body turn's
constexpr int kLookTurnMax = 0x60;

// The run's start animations by the move direction and the previous one (exe 0x8c5014: [dir * 4 + previous])
const DataTable<ScalarTable<int8_t, 16>> runStartAnims{TableId::PlayerLocomotionRunStartAnims};

// the jump, the four directions
uint16_t movesAndJump(const Player& p) {
    return uint16_t(p.button(PlayerButton::Jump) | p.button(PlayerButton::Right) | p.button(PlayerButton::Left) |
                    p.button(PlayerButton::Back) | p.button(PlayerButton::Forward));
}
// the two triggers (the special weapon, the buster)
uint16_t triggers(const Player& p) { return uint16_t(p.button(PlayerButton::Special) | p.button(PlayerButton::Fire)); }
// an arm up or a refire asked for
bool armOrRefire(const Player& p) { return (p.armFlags & kArmAnyRequest) != 0; }
// a foot down: a splash in water and the step sound
void footfall(Player& p, PlayerSound sound) {
    if (p.body.key.events & kAnimEventFootfall) {
        if (p.inWater != 0) splash(p); // exe 0x57b990
        soundPlayAtPlayer(uint16_t(sound));
    }
}

} // namespace

void idleInput(Player& p) {
    const uint16_t held = p.held;
    if (tryTalkTarget(p, kTalkOnPress) != 0) {
        p.stopSkating();
        return;
    }
    if (tryThrow(p, kThrowStanding) != 0 || tryLook(p) != 0) return;
    if (p.stepCounter != 0) { // waiting for the held directions to change
        const uint16_t d = uint16_t(p.directionButtons() & held);
        if (d != p.latchedDirections || d == 0) p.stepCounter = 0;
    }
    if (((triggers(p) & held) || (p.armFlags & (kArmSpecialQueued | kArmBusterQueued))) && !armHoldsMoves(p)) {
        fireButtons(p, FireContext::Standing);
        tryLiftTarget(p);
    }
    if ((movesAndJump(p) & held) || p.turnAxis != 0) {
        p.stopSkating();
        if (!armHoldsMoves(p)) {
            if (tryJump(p) != 0) return;
            if (!mustStopToFire(p) && armPastFireFrame(p) && p.stepCounter == 0) {
                if (tryQuickTurn(p, p.directionButtons())) return;
                if (tryRunForward(p, kRunFresh)) return;
                if (tryRunBack(p, kRunFresh)) return;
                if (tryRunLeft(p, kRunFresh)) return;
                if (tryRunRight(p, kRunFresh)) return;
            }
            if (!armOrRefire(p)) {
                if (tryWalkForward(p)) return;
                if (trySideStep(p)) return;
                if (tryWalkBack(p)) return;
            }
        }
        if (!armHoldsTurns(p)) tryTurnInPlace(p);
    }
    trySkates(p);
}

void idleMotion(Player& p) {
    if (p.stepAs<IdleStep>() == IdleStep::Enter) {
        p.armFlags &= uint16_t(~kArmMixMask);
        p.setStep(IdleStep::Standing);
        p.lean = 0;
        p.leanTarget = 0;
        p.stopSkating();
        p.lockSuspended = 0;
        p.jumpBuffered = 0;
        stopUnlessSlippery(p);
        if (!armOrRefire(p)) {
            if ((p.carryFlags & kCarryThrowing) == 0) {
                enableFiring(p);
                requestAnim(p, PlayerAnim::Idle, 0, false);
            } else {
                requestAnim(p, PlayerAnim::CarryThrow, 0, false);
            }
        } else {
            const int16_t frame = p.upper.key.frame;
            const PlayerAnim id = (p.armFlags & kArmSpecial) == 0 ? busterArmAnim(p) : specialArmAnim(p);
            requestAnim(p, id, frame, false);
        }
    } else {
        if (!armOrRefire(p) && (p.carryFlags & kCarryThrowing) == 0) requestAnim(p, PlayerAnim::Idle, 0, false);
        if ((p.armFlags & (kArmSpecial | kArmSpecialRefire)) == 0) stopUnlessSlippery(p);
    }
    if (p.airborne == 0) glideDecay(p, kIdleGlideDecay);
    p.spin.y = 0;
    p.directionOffset.y = 0;
    p.setMoveDir(MoveDir::Forward);
}

void walkInput(Player& p) {
    const uint16_t held = p.held;
    const uint16_t moves = movesAndJump(p);
    if (tryTalkTarget(p, kTalkWhileHeld) != 0) {
        if (moves & held) return;
    } else if ((!(triggers(p) & held) || (fireButtons(p, FireContext::Standing) == 0 && tryLiftTarget(p) == 0)) &&
               (moves & held)) {
        if (tryJump(p)) return;
        if (tryRunForward(p, kRunFresh)) return;
        if (tryRunBack(p, kRunFresh)) return;
        if (!armOrRefire(p)) {
            if (tryWalkForward(p)) return;
            if (tryWalkBack(p)) return;
            if (p.stepCounter == 0) {
                if (p.holds(PlayerButton::Back)) { // walking forward, back held without the modifier: the back run
                    if (p.holds(PlayerButton::Walk)) return;
                    p.enterStep(0);
                    p.subState = uint8_t(PlayerSub::Run);
                    p.directionOffset.y = kHalfTurn;
                    p.setMoveDir(MoveDir::Back);
                    return;
                }
            } else if (p.holds(PlayerButton::Forward)) {
                if (p.holds(PlayerButton::Walk)) return;
                p.enterSub(PlayerSub::Run);
                p.directionOffset.y = 0;
                p.setMoveDir(MoveDir::Forward);
                return;
            }
        }
    }
    turnOrStopVelocities(p, 0, kAlsoLocalVelocity);
    p.enterSub(PlayerSub::Idle);
}

void walkMotion(Player& p) {
    int sx, sy;
    if (p.stepCounter == 0) { // forward
        if (p.slippery == 0 || -(kWalkSlipLimit + 1) < p.velocity.z)
            p.velocity.z = int16_t(-kWalkSpeed);
        else
            p.velocity.z = wrap16(p.velocity.z + kWalkRamp);
        sy = -int(p.moveY);
        sx = -int(p.moveX);
    } else { // backward
        if (p.slippery == 0 || p.velocity.z < kWalkSlipLimit + 1)
            p.velocity.z = kWalkSpeed;
        else
            p.velocity.z = wrap16(p.velocity.z - kWalkRamp);
        sy = p.moveY;
        sx = p.moveX;
    }
    p.velocity.x = 0;
    p.velocity.y = 0;
    p.directionOffset.y = int16_t(uint32_t(atanAngle(float(sx), float(sy))) & kAngleMask);
    if (p.stepAs<WalkStep>() == WalkStep::Enter) {
        p.setArmBits(kArmUpperOnly);
        p.setStep(WalkStep::Walking);
        p.leanTarget = wrap16(-p.directionOffset.y);
        p.stopSkating();
        if (armOrRefire(p)) {
            requestAnim(p, (p.armFlags & kArmSpecial) ? PlayerAnim::SpecialArm : PlayerAnim::BusterArm, p.upper.key.frame,
                        false);
            return;
        }
    }
    if (armOrRefire(p)) return;
    if (p.stepCounter == 0) {
        requestAnim(p, PlayerAnim::Walk, 0, false);
        turnByAnalogStick(p, kWalkTurn, kAnalogGentle);
    } else {
        requestAnim(p, PlayerAnim::Shuffle, 0, false);
        turnByAnalogStick(p, kWalkBackTurn, kAnalogGentle);
    }
    p.leanTarget = wrap16(p.leanTarget + p.directionOffset.y);
    footfall(p, PlayerSound::Step);
}

void runInput(Player& p) {
    if (p.step > uint8_t(RunStep::Running)) {
        rearmWhileFiring(p); // OpenDash (game/player/moving_fire): shots keep going through the stop animation
        return;
    }
    if (tryThrow(p, p.moveDir() == MoveDir::Forward ? kThrowStanding : kThrowRunning) != 0) return;
    const uint16_t held = p.held;
    const uint16_t pressedNow = p.pressed;
    const uint16_t moves = movesAndJump(p);
    const uint16_t fire = p.button(PlayerButton::Fire), special = p.button(PlayerButton::Special);
    auto queueShotAndStop = [&]() {
        p.setArmBits((fire & pressedNow) ? kArmBusterQueued : kArmSpecialQueued);
        p.setStep(RunStep::Stop);
    };
    switch (p.moveDir()) {
    case MoveDir::Forward:
        if ((special | fire) & held) {
            fireButtons(p, FireContext::Moving);
            tryLiftTarget(p);
        }
        if (moves & held) {
            if (tryRunningJump(p)) return;
            if (!p.holds(PlayerButton::Walk)) {
                if (p.holds(PlayerButton::Forward)) return;
                if (tryRunBack(p, kRunFresh)) return;
                if (tryRunLeft(p, kRunTurned)) return;
                if (tryRunRight(p, kRunTurned)) return;
            } else if (p.holds(PlayerButton::Forward)) {
                p.enterSub(PlayerSub::Walk);
                return;
            }
        }
        turnOrStopVelocities(p, 0, kAlsoLocalVelocity);
        p.enterSub(PlayerSub::Idle);
        p.lean = 0;
        p.leanTarget = 0;
        cancelArmUnlessFiring(p); // exe 0x5721e0 (game/player/moving_fire)
        return;
    case MoveDir::Left:
    case MoveDir::Right: {
        const bool left = p.moveDir() == MoveDir::Left;
        if (((fire | special) & pressedNow) && p.carryFlags == 0 && mustStopToFire(p)) {
            queueShotAndStop();
            return;
        }
        if ((fire | special) & held) {
            fireButtons(p, FireContext::Moving);
            tryLiftTarget(p);
        }
        const uint16_t sides = uint16_t(p.button(PlayerButton::Left) | p.button(PlayerButton::Right));
        const uint16_t ownSide = p.button(left ? PlayerButton::Left : PlayerButton::Right);
        if ((moves & held) && (sides & held) != sides) {
            if (tryRunningJump(p)) return;
            if (tryQuickTurn(p, ownSide)) return;
            if (!p.holds(PlayerButton::Walk)) {
                if (tryRunForward(p, kRunTurned)) return;
                if (tryRunBack(p, kRunTurned)) return;
                if (left ? tryRunRight(p, kRunFresh) : tryRunLeft(p, kRunFresh)) return;
                if (ownSide & held) return;
                if (left) {
                    p.setStep(RunStep::Stop);
                    return;
                }
            }
        }
        break;
    }
    case MoveDir::Back:
        if ((fire | special) & pressedNow) {
            if (p.carryFlags == 0) {
                if (fire & pressedNow) {
                    p.setArmBits(kArmBusterQueued);
                    p.setStep(RunStep::Stop);
                    return;
                }
                if (specialEnergyReady(p)) {
                    p.setArmBits(kArmSpecialQueued);
                    if (p.activeSpecial != kChargeWeapon) break;
                }
            } else if (special & pressedNow) {
                p.setStep(RunStep::Stop);
                p.carryFlags |= kCarryThrowQueued;
                return;
            }
        }
        if ((fire | special) & held) tryLiftTarget(p);
        if (fireHeld(p)) fireButtons(p, FireContext::Moving); // OpenDash (game/player/moving_fire): firing backing off
        if (moves & held) {
            if (tryRunningJump(p)) return;
            if (tryQuickTurn(p, p.button(PlayerButton::Back))) return;
            if (!p.holds(PlayerButton::Walk)) {
                if (p.holds(PlayerButton::Back)) return;
                if (p.step > uint8_t(RunStep::Running)) return;
                if (tryRunForward(p, kRunFresh)) return;
                if (tryRunLeft(p, kRunTurned)) return;
                if (tryRunRight(p, kRunTurned)) return;
                p.setStep(RunStep::Stop);
                return;
            }
        }
        break;
    default: break;
    }
    p.setStep(RunStep::Stop);
}

void runMotion(Player& p) {
    const int8_t step = int8_t(p.step);
    switch (RunStep(step)) {
    case RunStep::Begin: {
        requestAnimId(p, runStartAnims[uint32_t(int(p.moveDir()) * 4 + int(p.prevMoveDir()))], 0, true);
        runSpeed(p, kRunStop);
        if (p.inWater != 0 || p.slippery != 0) runSpeed(p, 0);
        if (p.moveDir() != p.prevMoveDir()) cancelArmUnlessFiring(p); // exe 0x5721e0 (game/player/moving_fire)
        if (p.moveDir() == MoveDir::Forward && (p.armFlags & kArmUp)) {
            const int frame = p.upper.key.frame;
            const PlayerAnim id = (p.armFlags & kArmBuster) == 0 ? specialArmAnim(p) : busterArmAnim(p);
            startUpperAnimation(p, uint8_t(p.armFlags & kArmMixMask), id, frame);
        }
        p.setArmBits(kArmUpperOnly);
        p.setPrevMoveDir(MoveDir::Forward);
        p.directionOffset.y = moveDirAngle(p.moveDir());
        p.step = uint8_t(p.step + 2u);
        return;
    }
    case RunStep::BeginHold:
        if (p.body.key.frame == kRunStartFrame) p.step = uint8_t(step + 1);
        return;
    case RunStep::BeginTurn:
        steerMoveDirection(p);
        [[fallthrough]];
    case RunStep::BeginStraight:
        if (p.bodyAnimEnded()) {
            requestAnim(p, PlayerAnim::Run, p.moveDir() == MoveDir::Left ? kRunRestartFrame : 0, false);
            p.lockSuspended = 0;
            enableFiring(p);
            p.step = uint8_t(p.step + 2u);
        }
        runSpeed(p, 0);
        return;
    case RunStep::Settling:
        if (p.body.key.frame == kRunSettleFrame) p.step = uint8_t(step + 1);
        [[fallthrough]];
    case RunStep::Running:
        steerMoveDirection(p);
        runSpeed(p, 0);
        footfall(p, PlayerSound::Footstep);
        return;
    case RunStep::Stop: {
        const MoveDir dir = p.moveDir();
        if (dir == MoveDir::Left) {
            cancelArmUnlessFiring(p); // exe 0x5721e0 (game/player/moving_fire)
            requestAnim(p, PlayerAnim::RunStopLeft, 0, false);
        } else if (dir == MoveDir::Back) {
            cancelArmUnlessFiring(p);
            requestAnim(p, PlayerAnim::RunStopBack, 0, false);
        } else if (dir == MoveDir::Right) {
            cancelArmUnlessFiring(p);
            requestAnim(p, PlayerAnim::RunStopRight, 0, false);
        }
        if (p.slippery == 0) p.velocity.z = 0;
        p.leanTarget = 0;
        p.acceleration.x = kRunStopDeceleration;
        p.step = uint8_t(p.step + 1u);
        if (p.carryFlags & kCarryHolding) p.leanLocked = 1;
        return;
    }
    case RunStep::Stopping:
        if (int8_t(p.body.key.events) == kAnimEventLand) {
            p.enterSub(PlayerSub::Idle);
            runSpeed(p, kRunStop);
            p.directionOffset.y = 0;
            p.setMoveDir(MoveDir::Forward);
            p.leanTarget = 0;
            p.leanLocked = 0;
            enableFiring(p);
            requestAnim(p, PlayerAnim::Idle, 0, false);
            turnOrStopVelocities(p, 0, kAlsoLocalVelocity);
        }
        return;
    default: return;
    }
}

void turnInPlaceInput(Player& p) {
    const uint16_t held = p.held;
    const uint16_t moves = movesAndJump(p);
    if (tryTalkTarget(p, kTalkWhileHeld) != 0) {
        if (!(moves & held) && p.turnAxis == 0) p.enterSub(PlayerSub::Idle);
        return;
    }
    if (tryThrow(p, kThrowStanding) != 0) return;
    if (!armHoldsTurns(p) && p.lockedActor == nullptr) {
        if (triggers(p) & held) {
            if (fireButtons(p, FireContext::Standing)) return;
            if (tryLiftTarget(p)) return;
        }
        if ((moves & held) || p.turnAxis != 0) {
            if (!armHoldsMoves(p)) {
                if (tryJump(p)) return;
                if (!mustStopToFire(p)) {
                    if (tryRunLeft(p, kRunFresh)) return;
                    if (tryRunRight(p, kRunFresh)) return;
                    if (tryRunForward(p, kRunFresh)) return;
                    if (!(p.armFlags & kArmUp) && tryRunBack(p, kRunFresh)) return;
                }
                if (!armOrRefire(p)) {
                    if (tryWalkForward(p)) return;
                    if (trySideStep(p)) return;
                    if (tryWalkBack(p)) return;
                }
            }
            if (p.turnAxis != 0) return;
        }
    }
    p.enterSub(PlayerSub::Idle);
}

void turnInPlaceMotion(Player& p) {
    if (p.stepAs<TurnStep>() == TurnStep::Enter) {
        stopUnlessSlippery(p);
        p.stopSkating();
        p.lastTurnStep = 0;
        p.setStep(TurnStep::Turning);
    }
    int8_t anim;
    if ((p.armFlags & kArmUp) == 0)
        anim = std::abs(p.turnAxis) < kStickDeadZone ? int8_t(PlayerAnim::Idle) : int8_t(PlayerAnim::Shuffle);
    else
        anim = int8_t((p.armFlags & kArmBuster) == 0 ? specialArmAnim(p) : busterArmAnim(p));
    requestAnimId(p, anim, 0, false);
    if (!p.holds(PlayerButton::Walk))
        turnByAnalogStick(p, kStandTurn, kAnalogSharp);
    else
        turnByAnalogStick(p, kStandSlowTurn, kAnalogSharp);
    glideDecay(p, kTurnGlideDecay);
    footfall(p, PlayerSound::Step);
}

void quickTurnMotion(Player& p) {
    const int8_t step = int8_t(p.step);
    if (step == int8_t(QuickTurnStep::Enter)) {
        p.quickTurnSpeed = (uint16_t(p.directionOffset.y) & kAngleMask) <= kHalfTurn ? int16_t(-kQuickTurnSpeed)
                                                                                     : kQuickTurnSpeed;
        p.lockSuspended = 1;
        p.setStep(QuickTurnStep::Turning);
    }
    if (step == int8_t(QuickTurnStep::Enter) || step == int8_t(QuickTurnStep::Turning)) {
        const uint16_t d = uint16_t(p.directionButtons() & p.held);
        if (d != p.latchedDirections || d == 0) p.latchedDirections = 0;
        const int dir = uint16_t(p.directionOffset.y) & kAngleMask;
        int fromHalf = dir - kHalfTurn;
        if (fromHalf < 0) fromHalf = kHalfTurn - dir;
        if (kHalfTurn - fromHalf <= kQuickTurnEnd) { // turned: the yaw keeps the new heading
            turnOrStopVelocities(p, 0, kAlsoLocalVelocity);
            const int16_t offset = p.directionOffset.y;
            p.directionOffset.y = 0;
            p.setMoveDir(MoveDir::Forward);
            p.lockSuspended = 0;
            p.subState = uint8_t(PlayerSub::Idle);
            p.rotation.y = int16_t(uint32_t(p.rotation.y - offset) & kAngleMask);
            p.resetStep(IdleStep::Enter, kLatchDirections);
            enableFiring(p);
        } else {
            p.rotation.y = int16_t(uint32_t(p.quickTurnSpeed + p.rotation.y) & kAngleMask);
            p.directionOffset.y = int16_t(uint32_t(p.quickTurnSpeed + uint16_t(p.directionOffset.y)) & kAngleMask);
        }
    }
    if (!armOrRefire(p)) {
        p.leanTarget = 0;
        requestAnim(p, PlayerAnim::Run, 0, false);
    }
}

void sideStepInput(Player& p) {
    const uint16_t held = p.held;
    const uint16_t moves = movesAndJump(p);
    if (tryTalkTarget(p, kTalkWhileHeld) != 0) {
        if (moves & held) return;
        p.enterSub(PlayerSub::Idle);
        return;
    }
    if (tryThrow(p, kThrowStanding) != 0) return;
    if ((!(triggers(p) & held) || (fireButtons(p, FireContext::Standing) == 0 && tryLiftTarget(p) == 0)) &&
        (moves & held)) {
        if (tryJump(p)) return;
        if (tryWalkForward(p)) return;
        if (tryWalkBack(p)) return;
        if (tryRunBack(p, kRunFresh)) return;
        const uint16_t sides = uint16_t(p.button(PlayerButton::Left) | p.button(PlayerButton::Right));
        if ((sides & held) != sides && p.holds(PlayerButton::Walk)) return;
    }
    p.enterSub(PlayerSub::Idle);
}

void sideStepMotion(Player& p) {
    if (p.stepAs<SideStepStep>() == SideStepStep::Enter) {
        p.setArmBits(kArmUpperOnly);
        p.setStep(SideStepStep::Stepping);
        p.setMoveDir(MoveDir::Forward);
        p.directionOffset.y = 0;
        p.lean = 0;
        p.leanTarget = 0;
        p.stopSkating();
    }
    const uint16_t sides = uint16_t(p.button(PlayerButton::Right) | p.button(PlayerButton::Left));
    if ((sides & p.held) == sides) { // both: stand
        p.velocity.x = 0;
        p.velocity.y = 0;
        p.velocity.z = 0;
        p.pushVelocity.x = 0;
        p.pushVelocity.y = 0;
    } else if (!p.holds(PlayerButton::Left)) {
        requestAnim(p, PlayerAnim::SideStepRight, 0, false);
        if (p.slippery == 0 || -(kSideStepSlipLimit + 1) < p.velocity.x)
            p.velocity.x = int16_t(-kSideStepSpeed);
        else
            p.velocity.x = wrap16(p.velocity.x + kSideStepRamp);
        p.velocity.y = 0;
        p.velocity.z = 0;
        p.pushVelocity.y = 0;
    } else {
        requestAnim(p, PlayerAnim::SideStepLeft, 0, false);
        if (p.slippery == 0 || p.velocity.x < kSideStepSlipLimit + 1)
            p.velocity.x = kSideStepSpeed;
        else
            p.velocity.x = wrap16(p.velocity.x - kSideStepRamp);
        p.velocity.y = 0;
        p.velocity.z = 0;
        p.pushVelocity.y = 0;
    }
    p.pushVelocity.z = 0;
    if (p.body.key.frame == 0) soundPlayAtPlayer(uint16_t(PlayerSound::SideStep)); // the exe's 0x576aa5
}

void lookInput(Player& p) {
    if (tryThrow(p, kThrowStanding) != 0) return;
    fireButtons(p, FireContext::Standing);
    tryLiftTarget(p);
    if (!p.holds(PlayerButton::Look) || p.recentreAim != 0) p.enterSubAt(PlayerSub::Idle, IdleStep::Standing);
}

void lookMotion(Player& p) {
    if (p.holds(PlayerButton::Lock) && p.lockedActor != nullptr) {
        if (!armOrRefire(p) && (p.carryFlags & kCarryThrowing) == 0) {
            requestAnim(p, PlayerAnim::Idle, 0, false);
            p.leanTarget = 0;
        }
        return;
    }
    const int look = std::abs(p.lookAxis), turn = std::abs(p.turnAxis);
    CameraState& camera = cameraState();
    int step, limit;
    if ((look == 0 || look == kDigitalTilt) && (turn == 0 || turn == kDigitalTilt)) { // digital: the full rates
        if (turnAimByLookStick(p, 4, kLookAimMax) != 0)
            camera.lookPitch = int16_t(approachAngle(p.rotation.x, camera.lookPitch, kLookAimMax));
        step = 4;
        limit = kLookTurnMax;
    } else {
        int rate = (((look - kStickDeadZone) * (look - kStickDeadZone)) >> 7) + 1;
        if (rate > kLookAimMax) rate = kLookAimMax;
        if (turnAimByLookStick(p, wrap16(rate), rate) != 0)
            camera.lookPitch = int16_t(approachAngle(p.rotation.x, camera.lookPitch, rate));
        step = (((turn - kStickDeadZone) * (turn - kStickDeadZone)) >> 6) + 1;
        if (step > kLookTurnMax) step = kLookTurnMax;
        limit = step;
    }
    if (!armOrRefire(p) && (p.carryFlags & kCarryThrowing) == 0)
        turnInPlace(p, wrap16(step), limit, true);
    else if (!armHoldsTurns(p))
        turnInPlace(p, wrap16(step), limit, false);
    else
        p.spin.y = 0;
    if ((p.armFlags & (kArmSpecial | kArmSpecialRefire)) == 0) stopMotion(p);
    footfall(p, PlayerSound::Step);
}

} // namespace od::port
