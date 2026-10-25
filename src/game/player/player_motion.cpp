// The player's motion helpers (see player_motion.hpp).
#include "game/player/player_motion.hpp"

#include <cstdlib>

#include "game/actor_movement.hpp"
#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/matrix.hpp"
#include "game/player/player_animation.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"

namespace od::port {

namespace {

constexpr int kStickDeadZone = 0x37;  // |stick| above this turns
constexpr int kAnalogDeadZone = 0x38; // the analog curve's zero
constexpr int kDigitalTilt = 0x7f;    // a digital stick's |byte|: the given step / limit
constexpr int kAimPitchLimit = 0x200; // the aim pitch's range (+-)
constexpr int16_t kAimRecentreStep = 0x40;
constexpr int16_t kLeanLimit = 0x400;  // the lean target's range (+-)
constexpr int16_t kLeanEase = 0x100;   // the lean's ease per tick (a difference below it: set at once)
constexpr int16_t kHeadEase = 0x80;    // the head turn's
constexpr int kHeadEaseWindow = 0x7f;
constexpr int16_t kRunSpeed = -0x200;  // the run's forward speed (z < 0)
constexpr int16_t kRunRamp = 0x10;     // the run's acceleration on a slippery floor / in water
constexpr int16_t kSteerStep = 0x100;  // the move direction's turn toward the stick per tick
constexpr int kSteerSnap = 0x100;      // within this: the move direction = the stick's
constexpr int kHeadFacingWindow = 0x200; // the body within this of the head's direction: the arm may fire
constexpr int kGravity = 0x30;
constexpr int kGravityJumpCut = 0x240;
constexpr int kGravityFloat = 3;

} // namespace

void stopMotion(Player& p) {
    p.velocity.x = 0;
    p.velocity.y = 0;
    p.velocity.z = 0;
    p.pushVelocity.x = 0;
    p.pushVelocity.y = 0;
    p.pushVelocity.z = 0;
}

void stopUnlessSlippery(Player& p) {
    if (p.slippery != 0) return;
    p.velocity.x = 0;
    p.velocity.y = 0;
    p.velocity.z = 0;
    p.pushVelocity.x = 0;
    p.pushVelocity.y = 0;
    p.pushVelocity.z = 0;
}

uint32_t turnTowardStick(Player& p, int16_t step, int limit, int stick) {
    uint32_t side;
    bool reversed = false;
    if (stick < 1) {
        side = 2;
        if (p.spin.y > 0) {
            p.spin.y = 0;
            reversed = true;
        } else {
            const int16_t v = wrap16(p.spin.y - step);
            p.spin.y = v;
            if (int(v) < -limit) p.spin.y = wrap16(-limit);
            step = wrap16(-step);
        }
    } else {
        side = 1;
        if (p.spin.y < 0) {
            p.spin.y = 0;
            reversed = true;
        } else {
            const int16_t v = wrap16(p.spin.y + step);
            p.spin.y = v;
            if (limit < v) p.spin.y = wrap16(limit);
        }
    }
    if (!reversed) p.lastTurnStep = step;
    p.rotation.y = int16_t(uint32_t(p.spin.y + p.rotation.y) & kAngleMask);
    if (weaponBusy(p) != 0) {
        p.leanTarget = 0;
        return side;
    }
    p.leanTarget = wrap16(p.spin.y << 2);
    return side;
}

uint32_t turnByStick(Player& p, int16_t step, int limit) {
    const int stick = p.turnAxis;
    if (std::abs(stick) > kStickDeadZone && p.lockedActor == nullptr) return turnTowardStick(p, step, limit, stick);
    p.spin.y = 0;
    p.leanTarget = 0;
    return 0;
}

uint32_t turnByAnalogStick(Player& p, TurnRate rate, int8_t shift) {
    const int16_t step = rate.step;
    const int limit = rate.limit;
    int v = std::abs(p.turnAxis);
    if (v != kDigitalTilt) {
        v -= kAnalogDeadZone;
        if (v >= 0) {
            v = ((v * v) >> ((shift + 6) & 0x1f)) + 1;
            if (limit < v) v = limit;
            return turnByStick(p, wrap16(v), v);
        }
        return turnByStick(p, 0, 0);
    }
    return turnByStick(p, step, limit);
}

int approachAngle(int a, int b, int step) {
    int ua = a & kAngleMask;
    if (ua > 0x7ff) ua -= 0x1000;
    int ub = b & kAngleMask;
    if (ub > 0x7ff) ub -= 0x1000;
    if (ub < ua) {
        ub += step;
        if (ua > kAimPitchLimit && ub > kAimPitchLimit) return kAimPitchLimit;
        if (ua <= ub) return ua;
    } else {
        ub -= step;
        if (ua < -kAimPitchLimit && ub < -kAimPitchLimit) return -kAimPitchLimit;
        if (ub <= ua) ub = ua;
    }
    return ub;
}

void leanTowardMove(Player& p) {
    if (p.moveDir() == MoveDir::Back) return;
    int16_t f = signedAngle(p.directionOffset.y);
    int16_t v = wrap16(signedAngle(p.leanTarget) + f);
    bool settled = false; // the target already past the move direction: the lean keeps v
    if (v < 1) {
        if (v < -kLeanLimit) {
            v = -kLeanLimit;
            p.leanTarget = 0;
            f = v;
        } else if (f <= v) {
            settled = true;
        }
    } else if (v <= kLeanLimit) {
        if (v <= f) settled = true;
    } else {
        v = kLeanLimit;
        p.leanTarget = 0;
        f = v;
    }
    if (!settled) v = f;
    if (uint8_t(p.channelMix) != 0) v = int16_t(v >> 1);
    p.leanTarget = wrap16(p.leanTarget + v);
}

void easeLeanAndHead(Player& p) {
    if (p.leanLocked != 0) {
        p.lean = 0;
        p.headTurn = 0;
        return;
    }
    int16_t lean = p.lean;
    int16_t leanNow = signedAngle(lean);
    int16_t target = signedAngle(p.leanTarget);
    if (uint8_t(p.channelMix) == 0 && p.airborne != 0) target = wrap16((target * 3) / 4);
    if (std::abs(int(leanNow) - int(target)) < kLeanEase)
        p.lean = target;
    else
        p.lean = wrap16(target < leanNow ? lean - kLeanEase : lean + kLeanEase);
    int16_t head;
    if (uint8_t(p.channelMix) == 0 || p.moveDir() == MoveDir::Back)
        head = 0;
    else
        head = wrap16(signedAngle(p.directionOffset.y) - p.lean);
    const int16_t turn = p.headTurn;
    const int16_t turnNow = signedAngle(turn);
    if (std::abs(int(turnNow) - int(head)) > kHeadEaseWindow) {
        p.headTurnTarget = head;
        p.headTurn = wrap16(turnNow <= head ? turn + kHeadEase : turn - kHeadEase);
        return;
    }
    p.headTurn = head;
    p.headTurnTarget = head;
}

void turnOrStopVelocities(Player& p, int angle, uint8_t flags) {
    if (p.slippery != 0) {
        const int i = int(uint32_t(angle - p.directionOffset.y) & kAngleMask);
        const int s = fixedSin(i), c = fixedCos(i);
        if (flags & kAlsoLocalVelocity) {
            const int x = p.velocity.x;
            p.velocity.x = wrap16((c * x + s * p.velocity.z) >> 12);
            p.velocity.z = wrap16((c * p.velocity.z + s * x) >> 12);
            p.velocity.y = 0;
        }
        const int x = p.pushVelocity.x;
        p.pushVelocity.x = wrap16((c * x + s * p.pushVelocity.z) >> 12);
        p.pushVelocity.y = 0;
        p.pushVelocity.z = wrap16((c * p.pushVelocity.z + s * x) >> 12);
        return;
    }
    if (flags & kAlsoLocalVelocity) p.velocity.clearWords();
    p.pushVelocity.clearWords();
}

void decayToward0(int16_t& v, int16_t d) {
    const int16_t now = v;
    if (now == 0) return;
    if (now < 1) {
        v = wrap16(now + d);
        if (wrap16(now + d) > 0) v = 0;
    } else {
        v = wrap16(now - d);
        if (wrap16(now - d) < 0) v = 0;
    }
}

void glideDecay(Player& p, int16_t d) {
    if (p.slippery == 0) return;
    decayToward0(p.velocity.x, d);
    decayToward0(p.velocity.z, d);
    decayToward0(p.pushVelocity.x, d);
    decayToward0(p.pushVelocity.z, d);
}

void recentreAim(Player& p) {
    const int16_t pitch = p.rotation.x;
    if (pitch < 1) {
        p.rotation.x = wrap16(pitch + kAimRecentreStep);
        if (wrap16(pitch + kAimRecentreStep) > 0) p.rotation.x = 0;
    } else {
        p.rotation.x = wrap16(pitch - kAimRecentreStep);
        if (wrap16(pitch - kAimRecentreStep) < 0) p.rotation.x = 0;
    }
    CameraState& camera = cameraState();
    int16_t look = camera.lookPitch;
    bool level;
    if (look < 1) {
        look = wrap16(look + kAimRecentreStep);
        camera.lookPitch = look;
        level = look >= 1; // went past level
    } else {
        look = wrap16(look - kAimRecentreStep);
        camera.lookPitch = look;
        level = look < 0;
    }
    if (level) camera.lookPitch = 0;
    if (camera.lookPitch == 0 && p.rotation.x == 0) p.recentreAim = 0;
}

int turnAimPitch(Player& p, int16_t step, int limit, int dir) {
    const int16_t v = p.spin.x;
    p.recentreAim = 0;
    const CameraState& camera = cameraState();
    if (dir < 1) {
        if (v > 0) {
            p.spin.x = 0;
            return 0;
        }
        p.spin.x = wrap16(v - step);
        if (int(wrap16(v - step)) < -limit) p.spin.x = wrap16(-limit);
        p.rotation.x = wrap16(p.rotation.x + p.spin.x);
        if (p.rotation.x < -kAimPitchLimit) p.rotation.x = int16_t(-kAimPitchLimit);
        return p.rotation.x < camera.lookPitch ? 1 : 0;
    }
    if (v < 0) {
        p.spin.x = 0;
        return 0;
    }
    p.spin.x = wrap16(v + step);
    if (limit < wrap16(v + step)) p.spin.x = wrap16(limit);
    p.rotation.x = wrap16(p.rotation.x + p.spin.x);
    if (p.rotation.x > kAimPitchLimit) p.rotation.x = int16_t(kAimPitchLimit);
    return camera.lookPitch < p.rotation.x ? 1 : 0;
}

int turnAimByLookStick(Player& p, int16_t step, int limit) {
    const int stick = p.lookAxis;
    if (std::abs(stick) > kStickDeadZone) return turnAimPitch(p, step, limit, gameOptions().lookDirection != 0 ? stick : -stick);
    p.spin.x = 0;
    return 0;
}

uint32_t turnInPlace(Player& p, int16_t step, int limit, bool anim) {
    const int stick = p.turnAxis;
    if (std::abs(stick) > kStickDeadZone) {
        const uint32_t side = turnTowardStick(p, step, limit, stick);
        if (anim) requestAnim(p, PlayerAnim::Shuffle, 0, false);
        return side;
    }
    if (anim) requestAnim(p, PlayerAnim::Idle, 0, false);
    p.spin.y = 0;
    p.leanTarget = 0;
    return 0;
}

void moveInFacing(Player& p, int16_t x, int16_t y, int16_t z) {
    p.rotation.y = wrap16(p.rotation.y - p.directionOffset.y);
    integrateVelocity(p, x, y, z);
    p.rotation.y = wrap16(p.rotation.y + p.directionOffset.y);
}

void runSpeed(Player& p, int mode) {
    if (mode != 0) {
        turnOrStopVelocities(p, 0, kAlsoLocalVelocity);
        p.leanTarget = 0;
        return;
    }
    bool ramped = false;
    if (p.inWater != 0 || p.slippery != 0) {
        const MoveDir dir = p.moveDir();
        if (dir == MoveDir::Left && -int(p.velocity.x) < int(p.velocity.z)) p.velocity.z = wrap16(-p.velocity.x);
        if (dir == MoveDir::Right && p.velocity.x < p.velocity.z) p.velocity.z = p.velocity.x;
        if (p.velocity.z > 0) {
            if (dir == MoveDir::Back)
                p.velocity.z = wrap16(-p.velocity.z);
            else
                p.velocity.z = 0;
        }
        p.velocity.z = wrap16(p.velocity.z - kRunRamp);
        ramped = kRunSpeed - 1 < p.velocity.z;
    }
    if (!ramped) p.velocity.z = kRunSpeed;
    if (p.burden != 0) p.velocity.z = wrap16(p.velocity.z - wrap16((p.burden * p.velocity.z) >> 6));
    p.velocity.x = 0;
    p.velocity.y = 0;
    turnByAnalogStick(p, kRunTurn, kAnalogGentle);
    leanTowardMove(p);
}

void steerMoveDirection(Player& p) {
    const int a = atanAngle(float(-int(p.moveX)), float(-int(p.moveY)));
    const uint16_t current = uint16_t(p.directionOffset.y);
    const uint16_t want = uint16_t(a) & kAngleMask, have = current & kAngleMask;
    int diff = int(int16_t(have)) - int(int16_t(want));
    if (diff < 0) diff = int(int16_t(want)) - int(int16_t(have));
    uint16_t d = uint16_t(diff) & kAngleMask;
    const bool shortWay = d <= kHalfTurn;
    if (!shortWay) d = uint16_t(0x1000 - d);
    if (int16_t(d) <= kSteerSnap) {
        p.directionOffset.y = int16_t(want);
    } else if ((shortWay && want < have) || (!shortWay && have < want)) {
        p.directionOffset.y = int16_t(uint16_t(current - kSteerStep));
    } else {
        p.directionOffset.y = int16_t(uint16_t(current + kSteerStep));
    }
    if (p.moveDir() != MoveDir::Back) {
        const int dir = signedAngle(p.directionOffset.y);
        const int head = int16_t(wrap16((p.headTurn + p.lean) * 0x10) >> 4);
        uint32_t apart = uint32_t(dir - head);
        if (int32_t(apart) < 0) apart = uint32_t(head - dir);
        if ((apart & kAngleMask) <= uint32_t(kHeadFacingWindow)) {
            if (p.armFlags & kArmReady) return;
            enableFiring(p);
            return;
        }
    }
    putArmDown(p);
}

void applyJumpGravity(Player& p, int mode) {
    int g;
    if (mode == kGravityRise) {
        g = kGravity;
    } else if (mode == kGravityCut) {
        g = kGravityJumpCut;
    } else if (mode != kGravityFall) {
        g = 0;
    } else if ((p.liftMode & kLiftFloats) && (p.carryFlags & kCarryHolding) && p.holds(PlayerButton::Jump) &&
               !inputCaptured()) {
        g = kGravityFloat;
    } else {
        g = kGravity;
    }
    if (p.inWater != 0) g >>= 1;
    if (p.burden != 0) {
        p.velocity.y = wrap16(p.velocity.y + wrap16(((p.burden + 0x10) * g) >> 4));
        return;
    }
    p.velocity.y = wrap16(p.velocity.y + wrap16(g));
}

void playerAimVector(int x, int y, int z, int32_t out[3]) {
    const int32_t v[3] = {x, y, z};
    transformVectorFloat(mulMatrix3x3(rotationX(player().rotation.x), kIdentityMatrix), v, out);
}

} // namespace od::port
