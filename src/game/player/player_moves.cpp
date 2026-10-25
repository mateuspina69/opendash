// What the input starts (see player_moves.hpp).
#include "game/player/player_moves.hpp"

#include "game/angles.hpp"
#include "game/player/moving_fire.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

constexpr uint32_t kFlagNoLook = 0x24d; // the event flag that turns the look mode and the camera stick off (and starts
                                        // the jet skates at once)
constexpr int8_t kSkateHoldTicks = 9;   // the walk modifier held this long starts the skates
constexpr int kStickDeadZone = 0x37;
constexpr int kQuickTurnWindow = 0x600; // from idle: the stick within +-0x300 of the facing is no quick turn
constexpr uint32_t kQuickTurnBias = 0x300;
constexpr uint8_t kRunJumpStep = 3;     // the run's steps after which the running jump starts

// the run's start (exe 0x579fa0 .. 0x57a150's shared part): fresh at its start, turned from the run at Running, else at
// the start's turn
void startRun(Player& p, int mode) {
    p.subState = uint8_t(PlayerSub::Run);
    if (mode == kRunFresh)
        p.resetStep(RunStep::Begin);
    else if (int8_t(p.body.anim) == int8_t(PlayerAnim::Run))
        p.resetStep(RunStep::Running);
    else
        p.resetStep(RunStep::BeginTurn);
}
// a turned start needs the current sub-state past its first steps
bool runMayStart(const Player& p, int mode) { return mode == kRunFresh || p.step > 1; }
// one of left / right held, not both
bool oneSideHeld(const Player& p) {
    const uint16_t sides = uint16_t(p.button(PlayerButton::Right) | p.button(PlayerButton::Left));
    return (sides & p.held) != sides;
}

} // namespace

// exe 0x579fa0: the run forward
int tryRunForward(Player& p, int mode) {
    if (p.holds(PlayerButton::Forward) && !p.holds(PlayerButton::Walk) && runMayStart(p, mode)) {
        startRun(p, mode);
        const MoveDir d = p.moveDir();
        p.setMoveDir(MoveDir::Forward);
        p.setPrevMoveDir(d);
        return 1;
    }
    return 0;
}

// exe 0x57a020: the run to the left
int tryRunLeft(Player& p, int mode) {
    if (oneSideHeld(p) && p.holds(PlayerButton::Left) && !p.holds(PlayerButton::Walk) && runMayStart(p, mode)) {
        startRun(p, mode);
        p.setPrevMoveDir(p.moveDir());
        p.setMoveDir(MoveDir::Left);
        return 1;
    }
    return 0;
}

// exe 0x57a0b0: the run to the right
int tryRunRight(Player& p, int mode) {
    if (oneSideHeld(p) && p.holds(PlayerButton::Right) && !p.holds(PlayerButton::Walk) && runMayStart(p, mode)) {
        startRun(p, mode);
        const MoveDir d = p.moveDir();
        p.setMoveDir(MoveDir::Right);
        p.setPrevMoveDir(d);
        return 1;
    }
    return 0;
}

// exe 0x57a150: the run back (its arm off unless the buster fires: game/player/moving_fire)
int tryRunBack(Player& p, int mode) {
    if (p.holds(PlayerButton::Back) && !p.holds(PlayerButton::Walk) && runMayStart(p, mode)) {
        startRun(p, mode);
        clearArmForBackRun(p); // game/player/moving_fire
        p.setPrevMoveDir(p.moveDir());
        p.setMoveDir(MoveDir::Back);
        return 1;
    }
    return 0;
}

// exe 0x57a1e0: the jet skates
int trySkates(Player& p) {
    const bool onSkates = (p.legPart == kLegsJetSkates && p.inWater == 0) || (p.legPart == kLegsWaterSkates && p.inWater != 0);
    if (!(p.armFlags & kArmUp) && p.carryFlags == 0 && p.holds(PlayerButton::Walk) && onSkates) {
        const int8_t ticks = int8_t(p.skateHoldTicks + 1);
        p.skateHoldTicks = ticks;
        if (ticks < kSkateHoldTicks && !storyFlag(kFlagNoLook)) return 0;
        if (p.skating == 0) {
            p.enterSub(PlayerSub::Skate);
            return 1;
        }
    } else {
        p.stopSkating();
    }
    return 0;
}

// exe 0x57a280: the turn in place
int tryTurnInPlace(Player& p) {
    if (p.turnAxis != 0 && p.lockedActor == nullptr) {
        p.enterSub(PlayerSub::TurnInPlace);
        return 1;
    }
    return 0;
}

// exe 0x57a2b0: the quick turn
int tryQuickTurn(Player& p, uint16_t directions) {
    if (p.pressedNow(PlayerButton::Use) && (p.held & directions) && p.step < uint8_t(RunStep::Stop)) {
        if (p.sub() == PlayerSub::Idle) {
            const uint32_t a = uint32_t(atanAngle(float(-int(p.moveX)), float(-int(p.moveY)))) & kAngleMask;
            if (((a + kQuickTurnBias) & kAngleMask) < uint32_t(kQuickTurnWindow)) return 0;
            p.directionOffset.y = int16_t(a);
        }
        p.enterSub(PlayerSub::QuickTurn);
        p.latchedDirections = uint16_t(p.directionButtons() & p.held);
        putArmDown(p);
        return 1;
    }
    return 0;
}

// exe 0x579f20: the walk forward
int tryWalkForward(Player& p) {
    if (p.holds(PlayerButton::Forward) && p.holds(PlayerButton::Walk)) {
        p.enterSub(PlayerSub::Walk);
        return 1;
    }
    return 0;
}

// exe 0x57a390: the side step
int trySideStep(Player& p) {
    if (((p.button(PlayerButton::Right) | p.button(PlayerButton::Left)) & p.held) && p.holds(PlayerButton::Walk)) {
        p.enterSub(PlayerSub::SideStep);
        return 1;
    }
    return 0;
}

// exe 0x579f60: the walk backwards
int tryWalkBack(Player& p) {
    if (p.holds(PlayerButton::Back) && p.holds(PlayerButton::Walk)) {
        p.enterSub(PlayerSub::Walk, kWalkBackward);
        return 1;
    }
    return 0;
}

// exe 0x57a3e0: the jump
int tryJump(Player& p) {
    if ((p.pressedNow(PlayerButton::Jump) || p.jumpBuffered != 0) && !inputCaptured()) {
        p.enterSub(PlayerSub::Jump);
        return 1;
    }
    return 0;
}

// exe 0x57a420: the running jump
int tryRunningJump(Player& p) {
    const uint16_t dirs = uint16_t(p.button(PlayerButton::Right) | p.button(PlayerButton::Left) |
                                   p.button(PlayerButton::Forward) | p.button(PlayerButton::Back));
    if ((dirs & p.held) && (p.pressedNow(PlayerButton::Jump) || p.jumpBuffered != 0) && !inputCaptured()) {
        if (p.step > kRunJumpStep && (!p.holds(PlayerButton::Back) || p.moveDir() == MoveDir::Back)) {
            p.enterSub(PlayerSub::RunningJump);
            return 1;
        }
        p.jumpBuffered = 1;
    }
    return 0;
}

// exe 0x57a4b0: the side hop
int trySideHop(Player& p) {
    if (p.carryFlags != 0) return 0;
    const int stick = p.turnAxis;
    const bool right = p.holds(PlayerButton::Right), left = p.holds(PlayerButton::Left);
    const int tilt = stick < 0 ? -stick : stick;
    // to the left: the stick to the left without right held, or left held with the stick at rest
    if ((stick < 0 && !right) || (tilt <= kStickDeadZone && left)) {
        p.enterSub(PlayerSub::SideHop);
        return 1;
    }
    if ((stick < 1 || left) && (tilt > kStickDeadZone || !right)) return 0;
    p.enterSub(PlayerSub::SideHop, kHopRight);
    return 1;
}

// exe 0x57a8b0: the look mode
int tryLook(Player& p) {
    if (!storyFlag(kFlagNoLook) && p.holds(PlayerButton::Look) && !p.holds(PlayerButton::Lock) &&
        p.recentreAim == 0) {
        p.enterSub(PlayerSub::Look);
        return 1;
    }
    return 0;
}

} // namespace od::port
