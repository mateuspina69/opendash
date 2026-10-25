// The player hurt (see player_damage.hpp).
#include "game/player/player_damage.hpp"

#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/fixed_point.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/message.hpp"
#include "game/player/player_animation.hpp"
#include "game/player/player_effects.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_status.hpp"
#include "game/player/player_substates.hpp"
#include "game/player/player_weapons.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

// the attack bits of a result box the reaction reads (game/hit_list names the shared ones; the others by their effect
// on the player, hypotheses)
constexpr uint32_t kHitStatusBurn = 0x1000;  // the floor statuses a hit carries
constexpr uint32_t kHitStatusDrain = 0x2000;
constexpr uint32_t kHitStatusSlow = 0x4000;
constexpr uint32_t kHitStatusOrbit = 0x8000;
constexpr uint32_t kHitPushes = 0x200000;                      // with kHitAttackSolid: a push that hurts (hypothesis)
constexpr uint32_t kHitGuarded = 0x7000000;                    // these spare a push: only kHitGuardPierced bits hurt
constexpr uint32_t kHitGuardPierced = 0x6000000;
constexpr uint32_t kHitKnocksDown = kHitAttackThrown;          // the reaction's mask: these bits knock down
constexpr uint32_t kHitKnocksDownHard = 0x4000000 | kHitAttackThrown;
constexpr int16_t kStatusTicks = 0x200;
constexpr int16_t kHurtDecay = 4;          // the recent damage's decay per tick without a hit
constexpr int kKnockDownHits = 3;          // past this many recent hits: knocked down
constexpr int8_t kHelmetQuickRecovery = 2; // the helmet part with the recovery hop out of a knock-back
constexpr int16_t kKnockBackSpeed = 0x200;
constexpr int16_t kKnockBackBrake = 0x40;
constexpr int8_t kKnockBackImmuneFrames = 4;
constexpr int16_t kThrownAgainFrame = 6; // a second knock-down's throw animation starts from this frame
constexpr int16_t kThrownUp = -0x160;      // the knock-down's throw
constexpr int16_t kThrownBack = 0x200;
constexpr int16_t kSlideBrake = 0x40;
constexpr int16_t kLieGlideDecay = 0x10;
constexpr int kBounceDamage = -8;          // the second bounce hurts without a helmet
constexpr int8_t kGetUpImmuneTicks = 0x40;
constexpr uint32_t kHurtVibration = 0x301, kHurtVibrationTicks = 0x10aa0;
constexpr uint32_t kKnockVibration = 0x501, kKnockVibrationTicks = 0x210ff;

// exe 0x57aa50(player, mask, flags, n): a hit of n points: the damage scaled by the armour ((4 - armour / 2) * n / 4,
// at least 1), the recent damage / hits counted; the knock-back, or past the limits, with a knock-down bit (mask &
// flags), airborne or dead, the knock-down. Hit from behind or not (stepCounter), the move direction toward the source
// (the solid's contact, else the hit list's contact or its motion's angle), the hit sparks
void takeHit(Player& p, uint32_t mask, uint32_t flags, int n) {
    const int k = 4 - (int(p.armourPart) >> 1);
    int damage = (k * n) >> 2;
    if (damage == 0) damage = 1;
    const int dead = hitPointsChange(p, -damage); // exe 0x5e5320 (game/hit_points)
    p.hurtSum = wrap16(p.hurtSum + damage);
    p.hurtCount = wrap16(p.hurtCount + 1);
    if (k * 4 + 0x10 < p.hurtSum || p.hurtCount > kKnockDownHits || (mask & flags) != 0 || p.airborne != 0 || dead != 0) {
        p.subState = uint8_t(PlayerSub::KnockDown);
    } else {
        if (p.sub() == PlayerSub::KnockBack && p.step < uint8_t(KnockBackStep::Slowing)) return;
        p.subState = uint8_t(PlayerSub::KnockBack);
    }
    p.step = 0;
    int angle;
    if (p.solidHit.result == 0u && p.hitBox.contact.y != 0) {
        angle = p.hitBox.contact.x - kHalfTurn; // the hit list's contact: the attacker's motion angle
    } else {
        int dx, dz;
        if (p.solidHit.result == 0u) {
            dz = int(p.pos.z.whole) - p.hitBox.contact.z;
            dx = int(p.pos.x.whole) - p.hitBox.contact.x;
        } else {
            dz = int(p.pos.z.whole) - p.solidHit.contact.z;
            dx = int(p.pos.x.whole) - p.solidHit.contact.x;
        }
        angle = atanAngle(float(dx), float(dz));
    }
    const int rel = p.rotation.y - angle;
    if (((uint32_t(rel) - uint16_t(p.directionOffset.y)) + kQuarterTurn & kAngleMask) < uint32_t(kHalfTurn)) { // hit from the front
        p.stepCounter = 0;
        p.directionOffset.y = int16_t(uint32_t(rel) & kAngleMask);
    } else {
        p.stepCounter = kHitFromBehind;
        p.directionOffset.y = int16_t(uint32_t(uint16_t(rel) - kHalfTurn) & kAngleMask);
    }
    p.setMoveDir(moveDirOfAngle(uint16_t(p.directionOffset.y)));
    p.lean = 0;
    p.leanTarget = 0;
    hitSparks(p, angle);
}

// exe 0x57a560: the recovery hop out of a knock-back: jump or a side (the strafe pair, else the turn pair) -> the side
// hop keeping the immunity (to the left: its side held / the jump while facing away from the slide; else right)
int recoveryHop(Player& p) {
    const uint16_t held = p.held;
    if (!p.holds(PlayerButton::Jump) || inputCaptured()) {
        uint16_t first = p.button(PlayerButton::Left);
        uint16_t both = uint16_t(p.button(PlayerButton::Right) | first);
        uint16_t d = both & held;
        if (d == 0 || d == both) {
            first = p.button(PlayerButton::TurnLeft);
            both = uint16_t(p.button(PlayerButton::TurnRight) | first);
            d = both & held;
            if (d == 0 || d == both) return 0;
        }
        p.subState = uint8_t(PlayerSub::SideHop);
        if (first & held) {
            p.enterStep(0, kHopKeepImmunity);
            selectIdleTables(p, kIdleCalm, kIdleCalm);
            return 1;
        }
    } else {
        p.subState = uint8_t(PlayerSub::SideHop);
        if (kHalfTurn < int(uint32_t(p.directionOffset.y + (p.velocity.z < 0 ? -kHalfTurn : 0)) & kAngleMask)) {
            p.enterStep(0, kHopKeepImmunity);
            selectIdleTables(p, kIdleCalm, kIdleCalm);
            return 1;
        }
    }
    p.enterStep(0, kHopKeepImmunity | kHopRight);
    selectIdleTables(p, kIdleCalm, kIdleCalm);
    return 1;
}

// the reaction's common start (the knock-back's and the knock-down's): the homing and interaction targets dropped, the
// carry let go
void startReaction(Player& p) {
    p.homingTarget = 0;
    p.interactionTarget.clear();
    p.carryFlags = 0;
}
// ... and its flags: immune, the lean and the jump buffer off, the lock suspended, the aim recentred, the strained idle
// tables, off the skates
void reactionFlags(Player& p) {
    p.immunity = kImmuneHeld;
    p.leanLocked = 0;
    p.lockSuspended = 1;
    p.recentreAim = 1;
    p.jumpBuffered = 0;
    selectIdleTables(p, kIdleStrained, kIdleStrained);
    p.stopSkating();
}

} // namespace

void knockDownThrow(Player& p, int mode) {
    if (mode == 0) {
        const int16_t from = p.step != 0 ? kThrownAgainFrame : 0;
        if (p.stepCounter == 0) {
            requestAnim(p, PlayerAnim::KnockDownFront, from, true);
            p.velocity.z = kThrownBack;
        } else {
            requestAnim(p, PlayerAnim::KnockDownBehind, from, true);
            p.velocity.z = int16_t(-kThrownBack);
        }
    } else {
        p.velocity.z = 0;
    }
    startReaction(p);
    p.velocity.x = 0;
    p.velocity.y = kThrownUp;
    p.airborne = kAirborneFalling;
    reactionFlags(p);
    putArmDown(p);
    cameraSetFollow(CameraFollow::Rigid);
}

void applyHits(Player& p) {
    const uint32_t flags = p.solidHit.result | p.hitBox.result;
    const int n = int(p.solidHit.result & kHitDamageMask) + int(p.hitBox.result & kHitDamageMask);
    if (n == 0) {
        if ((flags & (kHitAttackSolid | kHitAttackThrown)) == (kHitAttackSolid | kHitAttackThrown))
            takeHit(p, kHitKnocksDown, flags, 0);
        else if (p.sub() != PlayerSub::KnockBack && p.sub() != PlayerSub::KnockDown) {
            p.hurtSum = wrap16(p.hurtSum - kHurtDecay);
            if (p.hurtSum < 0) {
                p.hurtSum = 0;
                p.hurtCount = 0;
            }
        }
    } else if (!(flags & kHitAttackStrong) && (!(flags & (kHitAttackSolid | kHitPushes)) || (flags & kHitGuarded))) {
        if (flags & kHitGuardPierced) takeHit(p, kHitKnocksDownHard, flags, n);
    } else {
        takeHit(p, kHitKnocksDown, flags, n);
    }
    if (flags & kHitStatusDrain) {
        p.setStatusBits(kStatusDrain);
        p.statusTicks[0] = kStatusTicks;
    }
    if ((flags & kHitStatusBurn) && !storyFlag(kFlagBurnArmour)) {
        p.setStatusBits(kStatusBurn);
        p.statusTicks[1] = kStatusTicks;
    }
    if ((flags & kHitStatusSlow) && !storyFlag(kFlagSlowArmour)) {
        p.setStatusBits(kStatusSlowed);
        p.statusTicks[2] = kStatusTicks;
    }
    if (flags & kHitStatusOrbit) {
        p.setStatusBits(kStatusOrbit);
        p.statusTicks[3] = kStatusTicks;
    }
}

void knockBackInput(Player& p) {
    if (p.stepAs<KnockBackStep>() == KnockBackStep::Slowing && p.helmetPart == kHelmetQuickRecovery) recoveryHop(p);
}

void knockBackMotion(Player& p) {
    switch (KnockBackStep(int8_t(p.step))) {
    case KnockBackStep::Hit: {
        if (p.stepCounter == 0) {
            requestAnim(p, PlayerAnim::KnockBackFront, 0, true);
            p.velocity.z = kKnockBackSpeed;
        } else {
            requestAnim(p, PlayerAnim::KnockBackBehind, 0, true);
            p.velocity.z = int16_t(-kKnockBackSpeed);
        }
        p.velocity.x = 0;
        p.velocity.y = 0;
        startReaction(p);
        reactionFlags(p);
        {
            // OpenDash (modern aim, not the exe): the lock-on survives a hit -- exe 0x5721e0 clears the lock request
            // bits here, so the lock dropped on every hit and came back ~20 ticks later with Megaman and the lock camera
            // turning back to the target (the train fight: every bomb)
            const uint16_t lock = p.lockRequest & (kLockSpecial | kLockBuster);
            putArmDown(p);
            p.lockRequest |= lock;
        }
        soundPlayAtPlayer(uint16_t(PlayerSound::Hurt));
        padVibrate(kHurtVibration, kHurtVibrationTicks);
        cameraSetFollow(CameraFollow::Rigid);
        p.step = uint8_t(p.step + 1u);
        return;
    }
    case KnockBackStep::Slide:
        if (p.body.key.frame > kKnockBackImmuneFrames) {
            p.immunity = 0;
            p.step = uint8_t(p.step + 1u);
        }
        [[fallthrough]];
    case KnockBackStep::Slowing:
        p.velocity.z = wrap16(p.velocity.z + (p.stepCounter == 0 ? -kKnockBackBrake : kKnockBackBrake));
        if (p.velocity.z == 0) p.step = uint8_t(p.step + 1u);
        return;
    case KnockBackStep::Recover:
        if (p.bodyAnimEnded()) {
            p.moveToSub(PlayerSub::RunningJump, RunningJumpStep::Landing);
            selectIdleTables(p, kIdleCalm, kIdleCalm);
        }
        return;
    default:
        return;
    }
}

void knockDownMotion(Player& p) {
    const int8_t step = int8_t(p.step);
    const bool behind = p.stepCounter != 0;
    auto slideToStop = [&]() { // steps Stop / StopAgain
        if (p.slippery == 0) decayToward0(p.velocity.z, kSlideBrake);
        if (p.velocity.z != 0 && p.slippery == 0) {
            skidSparks(p); // the slide's sparks
            return;
        }
        if (p.hitPoints < 0) { // dead
            if (p.velocity.z != 0) {
                glideDecay(p, kLieGlideDecay);
                return;
            }
            if (step == int8_t(KnockDownStep::Stop)) {
                p.state = uint8_t(PlayerState::Dead);
                p.subState = uint8_t(DeadStep::Enter);
            } else {
                p.step = uint8_t(step + 1);
            }
            return;
        }
        p.moveToSub(PlayerSub::RunningJump, RunningJumpStep::Landing);
        selectIdleTables(p, kIdleCalm, kIdleCalm);
        p.hurtSum = 0;
        p.hurtCount = 0;
        p.immunity = kImmuneBlinking;
        p.immunityTicks = kGetUpImmuneTicks;
    };
    auto getUp = [&](bool dead) { // steps Lie / LieAgain once the animation ended
        int8_t anim;
        if (dead)
            anim = step == int8_t(KnockDownStep::Lie)
                       ? int8_t(-1)
                       : int8_t(!behind ? PlayerAnim::KnockDownLandBehind : PlayerAnim::KnockDownLandFront);
        else if (step == int8_t(KnockDownStep::Lie))
            anim = int8_t(!behind ? PlayerAnim::GetUpFront : PlayerAnim::GetUpBehind);
        else
            anim = int8_t(!behind ? PlayerAnim::GetUpBehind : PlayerAnim::GetUpFront);
        if (anim >= 0) requestAnimId(p, anim, 0, true);
        p.step = uint8_t(p.step + 1u);
    };
    switch (KnockDownStep(step)) {
    case KnockDownStep::Thrown:
    case KnockDownStep::ThrownAgain:
        knockDownThrow(p, 0);
        soundPlayAtPlayer(uint16_t(PlayerSound::KnockedDown));
        padVibrate(kKnockVibration, kKnockVibrationTicks);
        p.step = uint8_t(p.step + 1u);
        return;
    case KnockDownStep::Airborne:
        if (p.airborne == 0) {
            requestAnim(p, !behind ? PlayerAnim::KnockDownLandFront : PlayerAnim::KnockDownLandBehind, 0, true);
            p.pushVelocity.x = 0;
            p.pushVelocity.y = 0;
            p.pushVelocity.z = 0;
            dustPuff(p, kDustAtHips);
            soundPlayAtPlayer(uint16_t(PlayerSound::FallLand));
            p.step = uint8_t(p.step + 1u);
            return;
        }
        if (int8_t(p.body.key.events) == kAnimEventLand) p.step = uint8_t(step + 3); // the bounce
        applyJumpGravity(p, kGravityRise);
        return;
    case KnockDownStep::Lie:
        glideDecay(p, kLieGlideDecay);
        if (!p.bodyAnimEnded()) return;
        getUp(p.hitPoints < 0);
        return;
    case KnockDownStep::Stop:
    case KnockDownStep::StopAgain:
        slideToStop();
        return;
    case KnockDownStep::Bounce:
        if (p.airborne == 0) {
            requestAnim(p, !behind ? PlayerAnim::BounceFront : PlayerAnim::BounceBehind, 0, true);
            if (uint8_t(p.helmetPart) == 0) hitPointsChange(p, kBounceDamage); // exe 0x5e5320 (game/hit_points)
            p.pushVelocity.x = 0;
            p.pushVelocity.y = 0;
            p.pushVelocity.z = 0;
            dustPuff(p, kDustAtHips);
            soundPlayAtPlayer(uint16_t(PlayerSound::Bounce));
            p.step = uint8_t(p.step + 1u);
            return;
        }
        applyJumpGravity(p, kGravityRise);
        return;
    case KnockDownStep::LieAgain:
        glideDecay(p, kLieGlideDecay);
        if (!p.bodyAnimEnded()) return;
        getUp(p.hitPoints < 0);
        return;
    case KnockDownStep::Dead:
        if (p.bodyAnimEnded()) {
            p.state = uint8_t(PlayerState::Dead);
            p.subState = uint8_t(DeadStep::Enter);
        }
        return;
    default:
        return;
    }
}

} // namespace od::port
