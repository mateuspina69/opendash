// The hovering gunner of the pirate base's gate rooms (ST41 / ST42 pool 0x20 type 0x7d, exe 0x50a5a0; hypothesis
// names). States [0x8982a0]: 0 bind 0x50a6d0, 1 active 0x50a790 (its sub-states [0x8982b0]), 2 dying 0x50b520, 3
// freed 0x468fa0.
// Sub-states: 0 pause 0x50a800 (then raised: 1, else 2 / 3), 1 the raised approach 0x50a8b0 (within 0x300 -> 6), 2
// the ground approach 0x50aae0 (within 0x500 -> 8), 3 the rise 0x50ac50 (to y -0x400; not once the gate is open), 4
// knocked down from up high 0x50ad60, 5 knocked down on the ground 0x50af80, 6 the single bomb 0x50b140, 7 the burst of
// 3 rising shots 0x50b260, 8 the stream of thrown shots 0x50b360, 9 turning away from a wall 0x50b480.
// The shadow (exe 0x5f20f0(actor, 0x3c, 0x100, 0)) is render output.
#include "content/saul_kada/st41_hover_gunner.hpp"
#include "content/saul_kada/st41_hover_gunner_state.hpp"

#include "content/saul_kada/st41_pirate_shot.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/vectors.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace st41_hover_gunner_state;

namespace {

enum class GunnerState : uint8_t { Bind = 0, Active = 1, Dying = 2, Free = 3 };
enum class GunnerSub : uint8_t {
    Pause = 0, RaisedApproach = 1, GroundApproach = 2, Rise = 3, KnockedHigh = 4, KnockedLow = 5, Bomb = 6, Burst = 7,
    Stream = 8, TurnAway = 9
};
enum class GunnerAnim : uint8_t {
    RaisedIdle = 0, Idle = 1, RaisedMove = 2, Move = 3, Turn = 4, BombAim = 6, BombFire = 7, BombEnd = 8, BurstAim = 9,
    BurstFire = 0xa, BurstEnd = 0xb, StreamAim = 0xc, StreamFire = 0xd, StreamEnd = 0xe, Knocked = 0xf, Falling = 0x10,
    Rise = 0x11, KnockedLow = 0x12, Hop = 0x14, Landed = 0x18, Down = 0x19, GetUp = 0x1a
};
// Its spawn parameters: the gate open at its bind, raised (up at y -0x400)
struct GunnerParams {
    explicit GunnerParams(const AnimatedActor& actor) : GunnerParams(const_cast<AnimatedActor&>(actor)) {}
    explicit GunnerParams(AnimatedActor& actor) : gateOpen(actor.params[0]), raised(actor.params[1]) {}
    uint8_t& gateOpen;
    uint8_t& raised;
};
constexpr uint8_t kGunnerNoHit = 0x01;
constexpr uint8_t kGunnerNoDamage = 0x02;
constexpr uint8_t kGunnerNoWallTurn = 0x04;
constexpr uint8_t kGunnerAttacking = 0x20; // (not read by the class)
constexpr uint8_t kGunnerReflect = 0x40;   // turned off the walls it meets
constexpr uint8_t kGunnerNoLockOn = 0x80;  // its entry never the lock-on target
constexpr uint8_t kRisingFlags = kGunnerNoDamage | kGunnerNoWallTurn;
constexpr uint8_t kAttackFlags = kGunnerNoWallTurn | kGunnerAttacking;
constexpr uint8_t kContactWallX = 0x01, kContactWallZ = 0x02, kContactFloor = 0x04, kContactCeiling = 0x08,
                  kContactReflected = 0x10;
constexpr uint32_t kFlagGateOpen = 0x155;
const DataTable<ScalarTable<uint16_t, 8>> wallYaws{TableId::St41HoverGunnerWallYaws}; // by the wall's side
constexpr uint16_t kMoveSound = 0x2e1, kAimSound = 0x2e2, kRaisedSound = 0x2e3, kLandSound = 0x2e4, kGetUpSound = 0x2e5;
constexpr uint16_t kStaggerMeter = 0x320;
constexpr uint16_t kExplosionTint = 0x4214;
constexpr int16_t kRaisedY = -0x400;

AnimatedActorStats gunnerStats(const AnimatedActor& actor) { return animatedStats(actor.type); }
void sound(uint16_t id, const AnimatedActor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380
void setAnim(AnimatedActor& actor, GunnerAnim anim) { actor.anim = uint8_t(anim); }
void enterSub(AnimatedActor& actor, GunnerSub sub) {
    actor.subState = uint8_t(sub);
    actor.restartStep();
}
bool isRaised(const AnimatedActor& actor) { return GunnerParams(actor).raised != 0; }
// its speed down by its gravity, the 16.16 y moved by it << 12
void fallStep(AnimatedActor& actor) {
    actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
    actor.pos.y = int32_t(uint32_t(actor.pos.y) + (uint32_t(int32_t(actor.velocity.y)) << 12));
}

// exe 0x50aa60(actor, add, step): turned (by step, exe 0x5e3df0) toward the player offset by its aim; the signed yaw
// difference to that point (exe 0x5e3d80) + add
uint32_t aimAt(AnimatedActor& actor, const GunnerFields& gunner, uint32_t add, int step) {
    const Player& p = player();
    const int32_t targetX =
        int32_t((uint32_t(p.pos.x) & 0xffffu) | uint32_t(uint16_t(uint16_t(p.pos.x.whole) + gunner.aimX)) << 16);
    const int32_t targetZ =
        int32_t((uint32_t(p.pos.z) & 0xffffu) | uint32_t(uint16_t(uint16_t(p.pos.z.whole) + gunner.aimZ)) << 16);
    const uint32_t angle = uint32_t(angleOf(targetX, targetZ, actor.pos.x, actor.pos.z)); // exe 0x5e3bc0
    turnYawTo(actor.rotation.y, angle, step);                                               // exe 0x5e3df0
    return uint32_t(angleDiff(uint32_t(int32_t(actor.rotation.y)), angle)) + add;
}
// exe 0x50b6a0: the walls (its contact bits by the push-out's, turned off them while kGunnerReflect), the wall's side
void collide(const PlayerEnv& env, AnimatedActor& actor, GunnerFields& gunner) {
    gunner.contact = 0;
    if (int32_t(actorTestMove(env, actor)) >= 0) return; // exe 0x40f570
    const uint32_t result = actorPushOut(env, actor);    // exe 0x410960
    actor.wallHit = result;
    if (int32_t(result) < 0) actor.pos = actor.prevPos;
    const uint32_t walls = (result >> 8) & 0xff;
    if (walls & 3) gunner.contact |= kContactWallX;
    if (walls & 0xc) gunner.contact |= kContactWallZ;
    if (walls & 0x20) gunner.contact |= kContactCeiling;
    if (walls & 0x10) gunner.contact |= kContactFloor;
    if ((gunner.flags & kGunnerReflect) && reflectRotationOffWalls(actor.rotation, result) != 0) // exe 0x410740
        gunner.contact |= kContactReflected;
    gunner.wallSide = uint16_t(result & 7);
}
// exe 0x50b630: a hit's damage: flashed, its hit points, its stagger meter; the meter or its hit points out ->
// knocked down
void takeDamage(AnimatedActor& actor, GunnerFields& gunner) {
    const uint32_t damage = gunner.hitBox.result & kHitDamageMask;
    if (damage == 0) return;
    actor.tint = kTintFlash;
    hitPointsAdd(actor, -int(damage)); // exe 0x5e52f0
    gunner.stagger = uint16_t(gunner.stagger - damage);
    if (int16_t(gunner.stagger) > 0 && actor.hitPoints >= 0) return;
    gunner.stagger = kStaggerMeter;
    actor.enterState(uint8_t(GunnerState::Active),
                     uint8_t(isRaised(actor) ? GunnerSub::KnockedHigh : GunnerSub::KnockedLow));
}

// ---- the sub-states [0x8982b0] -------------------------------------------------------------------------------------
// exe 0x50a800: a pause, then an approach (or the rise)
void pause(AnimatedActor& actor, GunnerFields& gunner) {
    switch (actor.step) {
    case 0:
        gunner.flags = 0;
        actor.enterStep(1);
        setAnim(actor, GunnerAnim::Idle);
        if (isRaised(actor)) setAnim(actor, GunnerAnim::RaisedIdle);
        gunner.timer = uint16_t((crtRand() & 0xc) + 0x18);
        [[fallthrough]];
    case 1:
        gunner.timer -= 1u;
        if (gunner.timer != 0) return;
        if (!isRaised(actor)) {
            actor.collisionBox.pointAt(TableId::St41HoverGunnerGroundBox);
            actor.subState = uint8_t(gunner.cooldown == 0 && GunnerParams(actor).gateOpen == 0 ? GunnerSub::Rise
                                                                                                  : GunnerSub::GroundApproach);
            actor.restartStep();
            return;
        }
        enterSub(actor, GunnerSub::RaisedApproach);
        actor.collisionBox.pointAt(TableId::St41HoverGunnerAirBox);
        gunner.cooldown = 0x5a;
        return;
    default: return;
    }
}
// the approaches' aim: random offsets around the player
void newAim(GunnerFields& gunner, bool spare) {
    gunner.aimX = uint16_t((crtRand() & 0x3ff) - 0x200u);
    gunner.aimZ = uint16_t((crtRand() & 0x3ff) - 0x200u);
    if (spare) gunner.aimSpare = uint16_t((crtRand() & 0x1ff) - 0x100u);
}
// exe 0x50a8b0 (its steps [0x50aa4c]): the raised approach
void raisedApproach(AnimatedActor& actor, GunnerFields& gunner) {
    switch (actor.step) {
    case 0:
        gunner.flags = 0;
        actor.enterStep(1);
        setAnim(actor, GunnerAnim::RaisedIdle);
        gunner.timer = 0xf;
        [[fallthrough]];
    case 1:
        gunner.timer -= 1u;
        if (gunner.timer != 0) break;
        newAim(gunner, true);
        if (aimAt(actor, gunner, 0x280, 0) < 0x500) {
            actor.enterStep(3);
            setAnim(actor, GunnerAnim::RaisedMove);
        } else {
            actor.enterStep(2);
            setAnim(actor, GunnerAnim::Turn);
        }
        break;
    case 2: // exe 0x50a978
        if (gunner.cooldown == 0) {
            enterSub(actor, GunnerSub::Burst);
            break;
        }
        if (aimAt(actor, gunner, 0x280, 0x30) < 0x500) {
            actor.enterStep(3);
            setAnim(actor, GunnerAnim::RaisedMove);
            break;
        }
        if (int32_t(actor.animKey.frame) % 6 == 0) sound(kMoveSound, actor);
        break;
    case 3: // exe 0x50a9cd
        if (gunner.cooldown == 0) {
            enterSub(actor, GunnerSub::Burst);
            break;
        }
        integrateVelocity(actor, 0, 0, -0x320); // exe 0x5e3690
        if (aimAt(actor, gunner, 0x400, 0x30) > 0x800) {
            actor.restartStep();
            break;
        }
        if (uint8_t(actor.animKey.frame) == 3 || uint8_t(actor.animKey.frame) == 7) sound(kMoveSound, actor);
        break;
    default: break;
    }
    if (planarDistance(actor.pos, player().pos) < 0x300) enterSub(actor, GunnerSub::Bomb); // exe 0x5e3b00
}
// exe 0x50aae0 (its steps [0x50ac40]): the ground approach
void groundApproach(AnimatedActor& actor, GunnerFields& gunner) {
    switch (actor.step) {
    case 0:
        gunner.flags = 0;
        actor.enterStep(1);
        setAnim(actor, GunnerAnim::Idle);
        gunner.timer = 0x1e;
        [[fallthrough]];
    case 1:
        gunner.timer -= 1u;
        if (gunner.timer != 0) break;
        newAim(gunner, false);
        if (aimAt(actor, gunner, 0x280, 0) < 0x500) {
            actor.enterStep(3);
            setAnim(actor, GunnerAnim::Move);
        } else {
            actor.enterStep(2);
            setAnim(actor, GunnerAnim::Turn);
        }
        break;
    case 2: // exe 0x50ab97
        if (aimAt(actor, gunner, 0x280, 0x18) < 0x500) {
            actor.enterStep(3);
            setAnim(actor, GunnerAnim::Move);
            break;
        }
        if (int32_t(actor.animKey.frame) % 6 == 0) sound(kMoveSound, actor);
        break;
    case 3: // exe 0x50abd3
        integrateVelocity(actor, 0, 0, -0x320);
        if (aimAt(actor, gunner, 0x400, 0x18) > 0x800) {
            actor.restartStep();
            break;
        }
        if (!(uint8_t(actor.animKey.frame) & 7)) sound(kMoveSound, actor);
        break;
    default: break;
    }
    if (planarDistance(actor.pos, player().pos) < 0x500) enterSub(actor, GunnerSub::Stream);
}
// exe 0x50ac50 (its steps [0x50ad4c]): the rise to y -0x400 (a flip on the way); the ceiling knocks it down
void rise(AnimatedActor& actor, GunnerFields& gunner) {
    GunnerParams params(actor);
    switch (actor.step) {
    case 0:
        if (params.gateOpen != 0) {
            enterSub(actor, GunnerSub::Pause);
            return;
        }
        gunner.flags = kRisingFlags;
        actor.enterStep(1);
        setAnim(actor, GunnerAnim::Rise);
        actor.velocity.y = 0;
        actor.acceleration.y = 0;
        gunner.timer = 4;
        [[fallthrough]];
    case 1:
        gunner.timer -= 1u;
        if (gunner.timer != 0) break;
        actor.enterStep(2);
        actor.velocity.y = int16_t(0xf900);
        actor.acceleration.y = 0x40;
        actor.rotation.z = 0;
        break;
    case 2: // exe 0x50accb
        actor.rotation.z = int16_t(uint16_t(uint16_t(actor.rotation.z) + 0x80u));
        if (uint16_t(actor.rotation.z) == 0x800) actor.enterStep(3);
        [[fallthrough]];
    case 3: // exe 0x50acdf
        if (gunner.contact & kContactCeiling) {
            actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) - 0x80u);
            actor.enterState(uint8_t(GunnerState::Active), uint8_t(GunnerSub::KnockedHigh));
            actor.collisionBox.pointAt(TableId::St41HoverGunnerAirBox);
            return;
        }
        if (actor.pos.y.whole <= kRaisedY) {
            actor.pos.y.whole = kRaisedY;
            enterSub(actor, GunnerSub::Pause);
            actor.rotation.z = 0x800;
            params.raised = 1;
            sound(kRaisedSound, actor);
        }
        break;
    default: break;
    }
    fallStep(actor); // exe 0x50ad30
}
// the knocked-down tail both share: the hop back up; landed -> the next step
void hopBack(AnimatedActor& actor, GunnerFields& gunner, uint8_t landedStep) {
    if (gunner.contact & kContactFloor) {
        actor.enterStep(landedStep);
        sound(kGetUpSound, actor);
        return;
    }
    if (uint8_t(actor.animKey.frame) == 0x11) {
        actor.prevAnim = 0xff;
        gunner.animEntry = 7;
    }
    if (actor.animKey.frame < 0xd) return;
    actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
    integrateVelocity(actor, 0, actor.velocity.y, -0x140);
}
// exe 0x50ad60 (its steps [0x50af54]): knocked down from up high
void knockedHigh(const PlayerEnv& env, AnimatedActor& actor, GunnerFields& gunner) {
    switch (actor.step) {
    case 0:
        gunner.flags = kRisingFlags;
        actor.enterStep(1);
        setAnim(actor, GunnerAnim::Knocked);
        actor.velocity.y = 0;
        actor.acceleration.y = 0x40;
        actor.collisionBox.pointAt(TableId::St41HoverGunnerAirBox);
        return;
    case 1:
        if (!actor.animEnded()) return;
        actor.enterStep(2);
        setAnim(actor, GunnerAnim::Falling);
        return;
    case 2: // exe 0x50adc4
        if (gunner.contact & kContactFloor) {
            actor.enterStep(3);
            actor.rotation.z = 0;
            setAnim(actor, GunnerAnim::Down);
            actor.collisionBox.pointAt(TableId::St41HoverGunnerGroundBox);
            actor.pos.y.whole = int16_t(actorGround(env, actor)); // exe 0x40f190
            GunnerParams(actor).raised = 0;
            actor.velocity.y = int16_t(0xfd00);
            sound(kLandSound, actor);
            return;
        }
        fallStep(actor);
        return;
    case 3: // exe 0x50ae27
        if ((gunner.contact & kContactFloor) && actor.velocity.y >= 0) {
            actor.enterStep(4);
            gunner.timer = 0x28;
            sound(kLandSound, actor);
            return;
        }
        fallStep(actor);
        return;
    case 4: // exe 0x50ae76
        if (actor.hitPoints < 0) {
            actor.enterState(uint8_t(GunnerState::Dying));
            return;
        }
        gunner.timer -= 1u;
        if (gunner.timer != 0) return;
        actor.enterStep(5);
        setAnim(actor, GunnerAnim::GetUp);
        return;
    case 5: // exe 0x50aeaa
        if (!actor.animEnded()) return;
        actor.enterStep(6);
        setAnim(actor, GunnerAnim::Hop);
        actor.velocity.y = int16_t(0xfd00);
        actor.acceleration.y = 0x80;
        return;
    case 6: hopBack(actor, gunner, 7); return; // exe 0x50aed3
    case 7: // exe 0x50af34
        if (!actor.animEnded()) return;
        enterSub(actor, GunnerSub::Pause);
        gunner.cooldown = 0x12c;
        return;
    default: return;
    }
}
// exe 0x50af80 (its steps [0x50b118]): knocked down on the ground
void knockedLow(AnimatedActor& actor, GunnerFields& gunner) {
    switch (actor.step) {
    case 0:
        gunner.flags = kRisingFlags;
        actor.enterStep(1);
        setAnim(actor, GunnerAnim::KnockedLow);
        actor.collisionBox.pointAt(TableId::St41HoverGunnerGroundBox);
        actor.velocity.y = int16_t(0xfd00);
        actor.acceleration.y = 0x80;
        return;
    case 1: // exe 0x50afc4
        if (gunner.contact & kContactFloor) {
            actor.enterStep(2);
            setAnim(actor, GunnerAnim::Landed);
            sound(kLandSound, actor);
            return;
        }
        actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
        if (uint8_t(actor.animKey.frame) == 6) {
            actor.prevAnim = 0xff;
            gunner.animEntry = 6;
        }
        integrateVelocity(actor, 0, actor.velocity.y, 0x140);
        return;
    case 2: // exe 0x50b021
        if (!actor.animEnded()) return;
        if (actor.hitPoints < 0) {
            actor.enterState(uint8_t(GunnerState::Dying));
            return;
        }
        actor.enterStep(3);
        setAnim(actor, GunnerAnim::Down);
        gunner.timer = 0x3c;
        return;
    case 3: // exe 0x50b055
        gunner.timer -= 1u;
        if (gunner.timer != 0) return;
        actor.enterStep(4);
        setAnim(actor, GunnerAnim::GetUp);
        return;
    case 4: // exe 0x50b078
        if (!actor.animEnded()) return;
        actor.enterStep(5);
        setAnim(actor, GunnerAnim::Hop);
        actor.velocity.y = int16_t(0xfd00);
        actor.acceleration.y = 0x80;
        return;
    case 5: hopBack(actor, gunner, 6); return; // exe 0x50b0a0
    case 6:                                 // exe 0x50b100
        if (!actor.animEnded()) return;
        enterSub(actor, GunnerSub::Pause);
        return;
    default: return;
    }
}
// the attacks' shot: a pirate shot of the variant at its bone 0 (exe 0x5e1280 / 0x5e1810)
uint32_t spawnShot(const AnimatedActor& actor, PirateShotVariant variant) {
    const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!slot) return 0;
    const ShortVec4* bone = bonePointOrNull(actor, 0); // exe 0x5e1810
    if (!bone) notPortedAt(kStoryPort, "exe 0x5e1810 returned null, bone", 0);
    ProjectileActor& shot = projectileActorAt(slot);
    const ShortVec4& point = *bone;
    shot.loopFlags |= kActorSweepGroup2;
    shot.flags = kActorLive;
    shot.type = kPirateShotType;
    shot.variant = uint8_t(variant);
    shot.spawnByte = 0;
    shot.pos.x.whole = point.x;
    shot.pos.y.whole = point.y;
    shot.pos.z.whole = point.z;
    return slot;
}
// the last step of the attacks: the animation over -> the pause
void endOnAnim(AnimatedActor& actor) {
    if (!actor.animEnded()) return;
    enterSub(actor, GunnerSub::Pause);
}
// the attacks' aim: its animation's key 5 sounds
void aimSound(const AnimatedActor& actor) {
    if (uint8_t(actor.animKey.frame) == 5) sound(kAimSound, actor);
}
// exe 0x50b140 (its steps [0x50b248]): the single bomb
void bombAttack(AnimatedActor& actor, GunnerFields& gunner) {
    switch (actor.step) {
    case 0:
        gunner.flags = kAttackFlags;
        actor.enterStep(1);
        setAnim(actor, GunnerAnim::BombAim);
        return;
    case 1: { // exe 0x50b174
        aimSound(actor);
        if (!actor.animEnded()) return;
        actor.enterStep(2);
        setAnim(actor, GunnerAnim::BombFire);
        gunner.timer = 0x1e;
        const uint32_t slot = spawnShot(actor, PirateShotVariant::Bomb);
        if (!slot) return;
        ProjectileActor& shot = projectileActorAt(slot);
        shot.rotation.x = 0;
        shot.rotation.z = 0;
        shot.rotation.y = actor.rotation.y;
        return;
    }
    case 2: // exe 0x50b20e
        gunner.timer -= 1u;
        if (gunner.timer != 0) return;
        actor.enterStep(3);
        setAnim(actor, GunnerAnim::BombEnd);
        return;
    case 3: endOnAnim(actor); return; // exe 0x50b230
    default: return;
    }
}
// exe 0x50b260 (its steps [0x50b348]): the burst of 3 rising shots
void burstAttack(AnimatedActor& actor, GunnerFields& gunner) {
    switch (actor.step) {
    case 0:
        gunner.flags = kAttackFlags;
        actor.enterStep(1);
        setAnim(actor, GunnerAnim::BurstAim);
        return;
    case 1: // exe 0x50b296
        aimSound(actor);
        if (!actor.animEnded()) return;
        actor.enterStep(2);
        setAnim(actor, GunnerAnim::BurstFire);
        return;
    case 2: // exe 0x50b2ce
        if (!actor.animEnded()) return;
        actor.enterStep(3);
        setAnim(actor, GunnerAnim::BurstEnd);
        for (int k = 0; k < 3; ++k) spawnShot(actor, PirateShotVariant::Rising);
        return;
    case 3: endOnAnim(actor); return; // exe 0x50b330
    default: return;
    }
}
// exe 0x50b360 (its steps [0x50b468]): the stream of thrown shots (one every 6 ticks while firing)
void streamAttack(AnimatedActor& actor, GunnerFields& gunner) {
    switch (actor.step) {
    case 0:
        gunner.flags = kAttackFlags;
        actor.enterStep(1);
        setAnim(actor, GunnerAnim::StreamAim);
        return;
    case 1: // exe 0x50b390
        aimSound(actor);
        if (!actor.animEnded()) return;
        actor.enterStep(2);
        setAnim(actor, GunnerAnim::StreamFire);
        gunner.timer = 0;
        return;
    case 2: { // exe 0x50b3d0
        const uint16_t left = gunner.timer;
        gunner.timer = uint16_t(left - 1u);
        if (left == 0) {
            gunner.timer = 5;
            spawnShot(actor, PirateShotVariant::Thrown);
        }
        if (!actor.animEnded()) return;
        actor.enterStep(3);
        setAnim(actor, GunnerAnim::StreamEnd);
        return;
    }
    case 3: endOnAnim(actor); return; // exe 0x50b44f
    default: return;
    }
}
// exe 0x50b480: turned away from the wall's side (a random spread around its yaw), then the pause
void turnAway(AnimatedActor& actor, GunnerFields& gunner) {
    switch (actor.step) {
    case 0:
        gunner.flags = kGunnerNoWallTurn;
        actor.enterStep(1);
        setAnim(actor, GunnerAnim::Turn); // the same raised or not
        gunner.turnYaw = int16_t(uint16_t((crtRand() & 0x3ff) + wallYaws[gunner.wallSide] - 0x200u));
        return;
    case 1: {
        const int step = isRaised(actor) ? 0x30 : 0x18;
        const uint32_t d = uint32_t(turnYawTo(actor.rotation.y, uint32_t(int32_t(gunner.turnYaw)), step)) +
                           0x200u; // exe 0x5e3df0
        if (d < 0x400) enterSub(actor, GunnerSub::Pause);
        return;
    }
    default: return;
    }
}

// ---- the states [0x8982a0] -----------------------------------------------------------------------------------------
// exe 0x50a6d0: its model, the gate's state, the hit points of its row x 16, its box; the rise first
void bind(AnimatedActor& actor, GunnerFields& gunner) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    GunnerParams params(actor);
    params.gateOpen = 0;
    if (storyFlag(kFlagGateOpen)) params.gateOpen = 1; // exe 0x510e50
    params.raised = 0;
    setAnim(actor, GunnerAnim::Idle);
    actor.prevAnim = 0xff;
    const uint16_t hp = hasAnimatedStats(actor.type) ? uint16_t(uint32_t(gunnerStats(actor).hitPoints) << 4) : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(TableId::St41HoverGunnerGroundBox);
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    gunner.hitBox.previousX = actor.pos.x.whole;
    gunner.hitBox.result = 0;
    gunner.hitBox.previousZ = actor.pos.z.whole;
    gunner.stagger = kStaggerMeter;
    actor.enterState(uint8_t(GunnerState::Active), uint8_t(GunnerSub::Rise));
}
// exe 0x50a790
void active(const PlayerEnv& env, AnimatedActor& actor, GunnerFields& gunner) {
    if (gunner.cooldown != 0) gunner.cooldown -= 1u;
    actor.tint = kTintNeutral;
    switch (GunnerSub(actor.subState)) { // [0x8982b0]
    case GunnerSub::Pause: pause(actor, gunner); break;
    case GunnerSub::RaisedApproach: raisedApproach(actor, gunner); break;
    case GunnerSub::GroundApproach: groundApproach(actor, gunner); break;
    case GunnerSub::Rise: rise(actor, gunner); break;
    case GunnerSub::KnockedHigh: knockedHigh(env, actor, gunner); break;
    case GunnerSub::KnockedLow: knockedLow(actor, gunner); break;
    case GunnerSub::Bomb: bombAttack(actor, gunner); break;
    case GunnerSub::Burst: burstAttack(actor, gunner); break;
    case GunnerSub::Stream: streamAttack(actor, gunner); break;
    case GunnerSub::TurnAway: turnAway(actor, gunner); break;
    default: notPortedAt(kStoryPort, "0x50a790 sub-state (table 0x8982b0 runs into other data)", actor.subState);
    }
    collide(env, actor, gunner); // exe 0x50b6a0
    if (!(gunner.flags & kGunnerNoWallTurn) && (gunner.contact & (kContactWallX | kContactWallZ)))
        actor.enterState(uint8_t(GunnerState::Active), uint8_t(GunnerSub::TurnAway));
    if (!(gunner.flags & kGunnerNoDamage)) takeDamage(actor, gunner); // exe 0x50b630
}
// exe 0x50b520: dying: an explosion 0x100 up, blinking 0x28 ticks (not lockable from 0x14), a defeat counted, its
// drops, freed
void dying(AnimatedActor& actor, GunnerFields& gunner) {
    actor.tint = kTintFlash;
    switch (actor.subState) {
    case 0: // exe 0x50b611
        gunner.flags = kGunnerNoHit;
        ++actor.subState;
        setAnim(actor, GunnerAnim::Down);
        return;
    case 1: {
        ++actor.subState;
        actor.velocity.x = 0;
        actor.velocity.y = 0;
        actor.velocity.z = 0;
        gunner.timer = 0x28;
        if (const uint32_t slot = allocActor60(0)) { // exe 0x5e1300
            StaticActor& explosion = staticActorAt(slot);
            explosion.flags = kActorLive;
            explosion.loopFlags |= kActorSweepGroup2;
            explosion.type = typeByte(GlobalStaticType::Explosion);
            explosion.variant = 0;
            explosion.pos.x.whole = actor.pos.x.whole;
            explosion.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) - 0x100u);
            explosion.pos.z.whole = actor.pos.z.whole;
            ExplosionParams params(actorHeaderAt(slot));
            params.spread = 0x80;
            params.flashes = gunner.timer;
        }
        [[fallthrough]];
    }
    case 2: { // exe 0x50b5b0
        if (gunner.timerLow & 1) actor.tint = kExplosionTint;
        const uint16_t left = gunner.timer;
        if (left == 0x14) gunner.flags |= kGunnerNoLockOn;
        gunner.timer = uint16_t(left - 1u);
        if (uint16_t(left - 1u) != 0) return;
        countDefeat();                                           // exe 0x5e53e0
        spawnDrops(actor.pos, gunnerStats(actor).dropTable); // exe 0x5f1270
        freeActor(actor);                                        // exe 0x5e1490
        return;
    }
    default: return;
    }
}
void hoverGunner(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x50a5a0
    GunnerFields& gunner = classState<GunnerFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (GunnerState(actor.state)) { // [0x8982a0]
    case GunnerState::Bind: bind(actor, gunner); break;
    case GunnerState::Active: active(env, actor, gunner); break;
    case GunnerState::Dying: dying(actor, gunner); break;
    case GunnerState::Free: freeActor(actor); break; // 0x468fa0
    default:
        notPortedAt(kStoryPort, "0x50a5a0 state (table 0x8982a0 runs into the sub-state table)", actor.state);
        return;
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) {
        animStepPose(actor); // exe 0x5e1e80
    } else {
        animSetPose(actor, actor.anim, gunner.animEntry); // exe 0x5e1c30
        gunner.animEntry = 0;
    }
    const AnimatedActorStats stats = gunnerStats(actor);
    gunner.hitBox.result = 0;
    gunner.hitBox.previousX = actor.prevPos.x.whole;
    gunner.hitBox.previousZ = actor.prevPos.z.whole;
    if (!(gunner.flags & kGunnerNoHit)) {
        const uint32_t lockOn = uint32_t(gunner.flags & kGunnerNoLockOn) << 12; // -> kHitNoLockOn
        const ShortVec4* bone = bonePointOrNull(actor, 0);                     // exe 0x5e1810
        if (!bone) notPortedAt(kStoryPort, "exe 0x5e1810 returned null, bone", 0);
        const ShortVec4& point = *bone;
        const int16_t pos[3] = {point.x, point.y, point.z};
        const uint32_t shape = hitOwner(actor.slot) | hitHeightLevel(stats.hitHeight) | hitGroup(kHitGroupBodies) |
                               kHitCompareHeights | (uint32_t(stats.bodyAttack) >> 2) | lockOn;
        hitListSubmit(pos, gunner.hitBox, shape, uint32_t(stats.attack) | kHitAttackSolid); // exe 0x5e4120
    }
    // the shadow (exe 0x5f20f0(actor, 0x3c, 0x100, 0)) is render output
}

} // namespace

void hoverGunnerUpdate(const PlayerEnv& env, uint32_t actor) { hoverGunner(env, animatedActorAt(actor)); }

} // namespace od::port
