// The patrolling blade trap of the Nino ruin (static type 0x45, ST36..ST38; exe 0x5b2ba0), one routine per exe
// routine; the names are hypotheses from the observed behaviour. Closed (model variant 1) it bobs and walks its legs
// (0x800 long, a quarter turn at each end); the player in the strip ahead -> its blade comes out (model variant 0), it
// turns to the player's side and charges along the leg spinning, then retracts and patrols again. Its body is hurt
// only by the strong pushes; the blade's hits by the player flash it and start the charge.
#include "content/nino/blade_trap.hpp"
#include "content/nino/blade_trap_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/collision.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"
#include "game/territory.hpp"

namespace od::port {

using namespace blade_trap_state;

namespace {

enum class TrapState : uint8_t { Init = 0, Run = 1, Dying = 2 };
enum class TrapSub : uint8_t { Start = 0, Patrol = 1, Attack = 2, Retract = 3 };
enum class PatrolPhase : uint8_t { Start = 0, Walk = 1, Turn = 2 };
enum class AttackPhase : uint8_t { Aim = 0, Unfold = 1, Charge = 2, TurnLeft = 3, TurnRight = 4 };
enum class RetractPhase : uint8_t { Aim = 0, Fold = 1 };

// The trap's spawn parameters: its home height (the init's store over the parameters)
struct TrapParams {
    explicit TrapParams(const StaticActor& actor) : TrapParams(const_cast<StaticActor&>(actor)) {}
    explicit TrapParams(StaticActor& actor) : level(actor.params[0]), homeY(actor.params, 0) {}
    uint8_t& level;
    BytesField<int16_t, 4> homeY;
};
constexpr uint8_t kTouchedXWall = 0x01, kTouchedZWall = 0x02, kTouchedFloor = 0x04;
constexpr uint8_t kTrapNoHitEntry = 0x01, kTrapNoDamage = 0x02, kTrapTurnLeft = 0x08, kTrapArmed = 0x20,
                  kTrapDying = 0x80;
const DataTable<CollisionBoxRecord> trapClosedBox{TableId::BladeTrapClosedBox};
const DataTable<CollisionBoxRecord> trapArmedBox{TableId::BladeTrapArmedBox};
constexpr uint8_t kClosedModel = 1, kArmedModel = 0;
constexpr uint16_t kArmSound = 0x1d7, kChargeSound = 0x1d8;
constexpr int16_t kClosedScale = int16_t(0xfe26), kClosedRadius = 0x40, kArmedRadius = 0x140;
constexpr uint16_t kLegLength = 0x800;
constexpr uint16_t kTintDeathBlink = 0x4214;

// exe 0x5b3730: the scale from the offset (0x280 + offset on each axis)
void scaleFromOffset(StaticActor& actor, const TrapFields& fields) {
    const int16_t s = int16_t(uint16_t(fields.scaleOffset) + 0x280u);
    actor.scale.x = s;
    actor.scale.y = s;
    actor.scale.z = s;
}
// exe 0x5b3960: the actor collision's result -> the contact bits (a floor / ceiling: on the ground)
void contactFromHits(StaticActor& actor, TrapFields& fields) {
    const uint32_t v = actor.actorHit;
    if (v == 0) return;
    if (v & (kWallOnXPlus | kWallOnXMinus)) fields.contact |= kTouchedXWall;
    if (v & (kWallOnZPlus | kWallOnZMinus)) fields.contact |= kTouchedZWall;
    if (v & kWallOnY) {
        actor.airborne = 0;
        fields.contact |= kTouchedFloor;
    }
}
// exe 0x5b33f0 / 0x5b35c0: the model of the variant (the bone blocks the exe reads the static as owning released
// first: exe 0x5e1760 reads the +0xcc dword of the slot as an animated actor's), its box, the scale
void rebind(StaticActor& actor, const TrapFields& fields, uint8_t variant) {
    // exe 0x5e1760: the +0xcc dword past the static's slot: the next static slot's first four bytes
    actorSystem().boneBlocksUsed &= ~staticPool().following(actor, "the blade trap's bone blocks word").headWord();
    if (!bindActorModel(actor, variant)) { // exe 0x5e0bc0 on a local byte
        freeActor(actor);                  // exe 0x5e1540
        return;
    }
    actor.collisionBox.pointAt(variant == kArmedModel ? trapArmedBox.stateAddress() : trapClosedBox.stateAddress());
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    scaleFromOffset(actor, fields);
}
// exe 0x5b38d0: the walls / the territory, queued as a mover; a floor / ceiling push-out puts it back (1)
int collide(const PlayerEnv& env, StaticActor& actor, TrapFields& fields) {
    fields.contact = 0;
    if (int32_t(actorTestMove(env, actor)) < 0) {   // exe 0x40f570
        const uint32_t r = actorPushOut(env, actor); // exe 0x410960
        if (int32_t(r) < 0) actor.pos = actor.prevPos;
        if (r & kWallOnY) {
            actor.pos = actor.prevPos;
            return 1;
        }
    } else {
        const int rect = territoryViolated(actor.pos);    // exe 0x5e6ab0
        if (rect != 0) territoryPushBack(actor, rect);    // exe 0x5e6b40
    }
    actorCollisionSubmitMover(actor); // exe 0x4140b0
    return 0;
}
// the player inside [x0, x1) x [z0, z1) (unsigned compares of the 16-bit corners, as the exe)
bool playerIn(int16_t x0, int16_t x1, int16_t z0, int16_t z1) {
    const Player& p = player();
    return uint32_t(int32_t(p.pos.x.whole) - x0) < uint32_t(int32_t(x1) - x0) &&
           uint32_t(int32_t(p.pos.z.whole) - z0) < uint32_t(int32_t(z1) - z0);
}
// exe 0x5b3770: the strong pushes on the armed body cost hit points; the player's hits on the blade flash it and, the
// player in the 0xb00 square ahead, start the charge; destroyed -> dying (-1). 0 nothing, 1 hit
int damage(StaticActor& actor, TrapFields& fields) {
    const uint32_t body = fields.armedHit.result;
    if ((body & kHitDamageMask) != 0 && (body & kHitAttackStrongPush)) {
        if (hitPointsAdd(actor, -int(body & kHitDamageMask)) != 0) { // exe 0x5e52f0
            actor.enterState(uint8_t(TrapState::Dying));
            return -1;
        }
    }
    const uint32_t v = fields.hit.result;
    const uint32_t d = v & kHitDamageMask;
    if (d == 0) return 0;
    int delta = 0;
    if (v & (kHitAttackThrown | kHitAttackByPlayer)) {
        actor.tint = kTintFlash;
        if (TrapSub(actor.subState) != TrapSub::Attack) {
            const uint16_t x = uint16_t(actor.pos.x.whole), z = uint16_t(actor.pos.z.whole), leg = fields.legLeft;
            int16_t ax = 0, az = 0;
            switch ((uint16_t(fields.travelYaw) >> 10) & 3) {
            case 0: ax = int16_t(x - 0x980); az = int16_t(z - leg - 0x180); break;
            case 1: ax = int16_t(x - leg - 0x180); az = int16_t(z - 0x180); break;
            case 2: ax = int16_t(x - 0x180); az = int16_t(z + leg - 0x980); break;
            default: ax = int16_t(x + leg - 0x980); az = int16_t(z - 0x980); break;
            }
            const Player& p = player();
            if (uint32_t(int32_t(p.pos.x.whole) - ax) < 0xb00u &&
                uint32_t(int32_t(p.pos.z.whole) - az) < 0xb00u) {
                actor.subState = uint8_t(TrapSub::Attack);
                actor.restartStep();
            }
        }
        delta = -int(d);
    }
    if (hitPointsAdd(actor, delta) != 0) {
        actor.enterState(uint8_t(TrapState::Dying));
        return -1;
    }
    return 1;
}
// exe 0x5b2ea0 (+ exe 0x5b2ec0): still, closed, then the patrol
void trapStart(StaticActor& actor, TrapFields& fields) {
    actor.velocity.setXY(0, 0);
    fields.travelYaw = actor.rotation.y;
    fields.bladeRadius = kClosedRadius;
    fields.scaleOffset = kClosedScale;
    scaleFromOffset(actor, fields);
    fields.mode = 0;
    actor.subState = uint8_t(TrapSub::Patrol);
    actor.restartStep();
}
// the leg's move: the leg left down by `step` (its rest the last part's speed), along the travel yaw
void legStep(StaticActor& actor, TrapFields& fields, uint16_t step, int16_t speed) {
    const uint16_t leg = fields.legLeft;
    if (leg < step) {
        fields.speed = int16_t(uint16_t(0u - (uint32_t(leg) << 4)));
        fields.legLeft = 0;
    } else {
        fields.legLeft = uint16_t(leg - step);
        fields.speed = speed;
    }
    moveAlongYaw(actor.pos, fields.travelYaw, 0, 0, fields.speed); // exe 0x5e36f0
}
// exe 0x5b2ef0: it bobs, walks its leg and turns a quarter at its end; the player in the strip ahead -> the attack
void patrol(StaticActor& actor, TrapFields& fields) {
    switch (PatrolPhase(actor.step)) {
    case PatrolPhase::Start:
        fields.mode = 0;
        fields.stepSize = 1;
        fields.bob = 0;
        fields.speed = -0x40;
        actor.step = uint8_t((fields.travelYaw != actor.rotation.y ? 1u : 0u) + 1u);
        break;
    case PatrolPhase::Walk:
        if (fields.legLeft <= 0x200) {
            actor.step = uint8_t(PatrolPhase::Turn);
            fields.timer = 0x80;
        }
        break;
    case PatrolPhase::Turn:
        actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + 8u);
        fields.timer = int16_t(uint16_t(fields.timer) - 1u);
        if (fields.timer == 0) {
            actor.step = uint8_t(PatrolPhase::Walk);
            const int16_t yaw = int16_t((uint32_t(uint16_t(fields.travelYaw)) + 0x400u) & 0xfff);
            fields.travelYaw = yaw;
            actor.rotation.y = yaw;
            fields.legLeft = kLegLength;
        }
        break;
    default: break;
    }
    int v = fields.bob;
    if (v < 0) v = -v;
    if (v > 8) fields.stepSize = int16_t(0u - uint16_t(fields.stepSize));
    fields.bob = int16_t(uint16_t(fields.bob) + uint16_t(fields.stepSize));
    actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) - uint16_t(fields.bob));
    legStep(actor, fields, 4, -0x40);
    const uint16_t x = uint16_t(actor.pos.x.whole), z = uint16_t(actor.pos.z.whole), leg = fields.legLeft;
    int16_t x0 = 0, x1 = 0, z0 = 0, z1 = 0;
    switch ((uint16_t(fields.travelYaw) >> 10) & 3) {
    case 0: x0 = int16_t(x - 0x180); x1 = int16_t(x + 0x180); z0 = int16_t(z - leg - 0x180); z1 = int16_t(z); break;
    case 1: x0 = int16_t(x - leg - 0x180); x1 = int16_t(x); z0 = int16_t(z - 0x180); z1 = int16_t(z + 0x180); break;
    case 2: x0 = int16_t(x - 0x180); x1 = int16_t(x + 0x180); z0 = int16_t(z); z1 = int16_t(z + leg + 0x180); break;
    default: x0 = int16_t(x); x1 = int16_t(x + leg + 0x180); z0 = int16_t(z - 0x180); z1 = int16_t(z + 0x180); break;
    }
    if (playerIn(x0, x1, z0, z1)) {
        actor.subState = uint8_t(TrapSub::Attack);
        actor.restartStep();
    }
}
// leg / 48 (the exe's signed division by 0x30 through 0x2aaaaaab)
int32_t div48(uint32_t leg) {
    const int32_t q = int32_t((int64_t(int32_t(leg)) * 0x2aaaaaab) >> 32) >> 3;
    return q + int32_t(uint32_t(q) >> 31);
}
// the aim at the end of the leg on the player's side: the player ahead keeps the leg (a left turn), else the leg is
// reversed; the yaw aimed at the leg's end (a sixteenth of a turn per 0x30 of the leg left, at most a quarter)
void aimLeg(StaticActor& actor, TrapFields& fields) {
    const int angle = angleOf(player().pos, actor.pos) & kAngleMask;
    const int side = angleDiff(uint32_t(angle), uint16_t(fields.travelYaw));
    if (uint32_t(side + 0x400) < 0x800u) {
        fields.timer = int16_t(uint16_t(uint32_t(div48(fields.legLeft)) << 6));
        if (fields.timer > 0x400) fields.timer = 0x400;
        fields.timer = int16_t(uint16_t(uint32_t(uint16_t(fields.travelYaw)) - uint16_t(fields.timer) + 0x400u));
        fields.mode |= kTrapTurnLeft;
    } else {
        fields.legLeft = uint16_t(0x800u - fields.legLeft);
        fields.travelYaw = int16_t((uint32_t(uint16_t(fields.travelYaw)) - 0x800u) & 0xfff);
        fields.timer = int16_t(uint16_t(uint32_t(div48(fields.legLeft)) << 6));
        if (fields.timer > 0x400) fields.timer = 0x400;
        fields.timer = int16_t(uint16_t(uint16_t(fields.timer) + (uint32_t(uint16_t(fields.travelYaw)) - 0x400u)));
    }
}
// exe 0x5b30c0: the blade out, it turns to the player's side, unfolds, and charges spinning along the leg
void attack(StaticActor& actor, TrapFields& fields) {
    switch (AttackPhase(actor.step)) {
    case AttackPhase::Aim:
        fields.mode = kTrapArmed;
        rebind(actor, fields, kArmedModel); // exe 0x5b33f0
        fields.speed = 0;
        soundPlayAtPos(kArmSound, actor.pos);
        aimLeg(actor, fields);
        actor.step = uint8_t(AttackPhase::Unfold);
        fields.stepSize = int16_t(actor.pos.y.whole > TrapParams(actor).homeY ? -2 : 2);
        [[fallthrough]];
    case AttackPhase::Unfold: {
        fields.partsLeft = 4;
        actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + uint16_t(fields.stepSize));
        const int16_t y = actor.pos.y.whole, home = TrapParams(actor).homeY;
        if (fields.stepSize > 0 ? y > home : y < home) {
            actor.pos.y.whole = home;
            fields.partsLeft -= 1u;
        }
        if (turnYawTo(actor.rotation.y, uint32_t(int32_t(fields.timer)), 0x40) == 0) // exe 0x5e3df0
            fields.partsLeft -= 1u;
        if (uint16_t(fields.bladeRadius) < uint16_t(kArmedRadius)) {
            fields.bladeRadius = int16_t(uint16_t(fields.bladeRadius) + 8u);
        } else {
            fields.partsLeft -= 1u;
            fields.bladeRadius = kArmedRadius;
        }
        if (fields.scaleOffset < 0) {
            fields.scaleOffset = int16_t(uint16_t(fields.scaleOffset) + 8u);
        } else {
            fields.partsLeft -= 1u;
            fields.scaleOffset = 0;
        }
        scaleFromOffset(actor, fields);
        if (fields.partsLeft == 0) {
            if (((uint16_t(fields.travelYaw) ^ uint16_t(actor.rotation.y)) & 0xfff) != 0) {
                fields.timer = int16_t(div48(fields.legLeft) + 1);
                actor.step = uint8_t((fields.mode & kTrapTurnLeft) ? AttackPhase::TurnLeft : AttackPhase::TurnRight);
            } else {
                actor.step = uint8_t(AttackPhase::Charge);
            }
            fields.stepSize = 0x20;
            soundPlayAtPos(kChargeSound, actor.pos);
            fields.partsLeft = 0x40;
        }
        break;
    }
    case AttackPhase::Charge:
        if (fields.legLeft <= 0x300) {
            actor.step = uint8_t((fields.mode & kTrapTurnLeft) ? AttackPhase::TurnLeft : AttackPhase::TurnRight);
            fields.timer = 0x10;
        }
        break;
    case AttackPhase::TurnLeft:
    case AttackPhase::TurnRight: {
        const bool left = AttackPhase(actor.step) == AttackPhase::TurnLeft;
        actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + (left ? 0x40u : 0xffc0u));
        fields.timer = int16_t(uint16_t(fields.timer) - 1u);
        if (fields.timer == 0) {
            actor.step = uint8_t(AttackPhase::Charge);
            const int16_t yaw =
                int16_t((uint32_t(uint16_t(fields.travelYaw)) + (left ? 0x400u : 0xfffffc00u)) & 0xfff);
            fields.travelYaw = yaw;
            actor.rotation.y = yaw;
            fields.legLeft = kLegLength;
        }
        break;
    }
    default: break;
    }
    if (fields.spinSpeed < 0x180) fields.spinSpeed = int16_t(uint16_t(fields.stepSize) + uint16_t(fields.spinSpeed));
    actor.rotation.z = int16_t(uint16_t(actor.rotation.z) - uint16_t(fields.spinSpeed));
    if (actor.step <= uint8_t(AttackPhase::Unfold)) return;
    legStep(actor, fields, 0x30, -0x300);
    fields.partsLeft -= 1u;
    if (fields.partsLeft == 0) {
        actor.subState = uint8_t(TrapSub::Retract);
        actor.restartStep();
    }
}
// exe 0x5b3440: the blade in, it turns to the next leg, folds, back to the patrol (model variant 1)
void retract(StaticActor& actor, TrapFields& fields) {
    switch (RetractPhase(actor.step)) {
    case RetractPhase::Aim: {
        fields.speed = 0;
        const uint8_t m = fields.mode;
        if (m & kTrapTurnLeft) {
            fields.timer = int16_t(uint16_t(uint32_t(fields.legLeft >> 2) << 3));
            if (fields.timer > 0x400) fields.timer = 0x400;
            fields.timer = int16_t(uint16_t(uint32_t(uint16_t(fields.travelYaw)) - uint16_t(fields.timer) + 0x400u));
            fields.mode = uint8_t(m & ~kTrapTurnLeft);
        } else {
            fields.legLeft = uint16_t(0x800u - fields.legLeft);
            fields.travelYaw = int16_t((uint32_t(uint16_t(fields.travelYaw)) - 0x800u) & 0xfff);
            fields.timer = int16_t(uint16_t(uint32_t(fields.legLeft >> 2) << 3));
            if (fields.timer > 0x400) fields.timer = 0x400;
            fields.timer = int16_t(uint16_t(uint32_t(uint16_t(fields.travelYaw)) - uint16_t(fields.timer) + 0x400u));
        }
        actor.step = uint8_t(RetractPhase::Fold);
        [[fallthrough]];
    }
    case RetractPhase::Fold: {
        fields.partsLeft = 4;
        if (turnYawTo(actor.rotation.y, uint32_t(int32_t(fields.timer)), 0x40) == 0) fields.partsLeft -= 1u;
        if (fields.spinSpeed > 0) {
            const uint16_t v = uint16_t(uint16_t(fields.spinSpeed) - uint16_t(fields.stepSize));
            actor.rotation.z = int16_t(uint16_t(actor.rotation.z) + v);
            fields.spinSpeed = int16_t(v);
        } else {
            const int16_t st = fields.stepSize;
            actor.rotation.z = int16_t(uint16_t(actor.rotation.z) + uint16_t(st));
            if (int32_t(uint16_t(actor.rotation.z) & 0xfff) - int32_t(st) < 0) {
                actor.rotation.z = 0;
                fields.partsLeft -= 1u;
            }
        }
        if (uint16_t(fields.bladeRadius) > uint16_t(kClosedRadius)) {
            fields.bladeRadius = int16_t(uint16_t(fields.bladeRadius) - 8u);
        } else {
            fields.partsLeft -= 1u;
            fields.bladeRadius = kClosedRadius;
        }
        if (fields.scaleOffset > kClosedScale) {
            fields.scaleOffset = int16_t(uint16_t(fields.scaleOffset) - 8u);
        } else {
            fields.partsLeft -= 1u;
            fields.scaleOffset = kClosedScale;
        }
        scaleFromOffset(actor, fields);
        if (fields.partsLeft != 0) return;
        if (((uint16_t(fields.travelYaw) ^ uint16_t(actor.rotation.y)) & 0xfff) != 0)
            fields.timer = int16_t(uint16_t((fields.legLeft >> 2) + 1u));
        rebind(actor, fields, kClosedModel); // exe 0x5b35c0
        actor.subState = uint8_t(TrapSub::Patrol);
        actor.restartStep();
        return;
    }
    default: return;
    }
}
// exe 0x5b2d70: model variant 1 (closed), the row's hit points, the boxes' previous places, a full leg, its home height
void trapInit(StaticActor& actor, TrapFields& fields) {
    actor.variant = kClosedModel;
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0(a, variant)
        freeActor(actor);                        // exe 0x5e1540
        return;
    }
    actor.rotation.x = 0;
    actor.rotation.z = 0;
    actor.runFlags |= kActorRunMarked;
    const int16_t hp = hasStaticStats(actor.type) ? int16_t(staticStats(actor.type, 0).hitPoints) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.collisionBox.pointAt(trapClosedBox.stateAddress());
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    TrapParams params(actor);
    params.homeY = int16_t(uint16_t((uint32_t(params.level) << 9) - 0x85u)); // overwritten below (the exe's dead store)
    fields.clearBlockBytes(); // exe 0x40dc40(+0xbc, +0xc8)
    fields.clearLanesBytes(); // exe 0x40dc40(+0x40, +0x4c)
    fields.hit.previousX = actor.pos.x.whole;
    fields.hit.previousZ = actor.pos.z.whole;
    fields.hit.result = 0;
    fields.armedHit.previousX = actor.pos.x.whole;
    fields.armedHit.previousZ = actor.pos.z.whole;
    fields.armedHit.result = 0;
    fields.legLeft = kLegLength;
    params.homeY = actor.pos.y.whole;
    actor.enterState(uint8_t(TrapState::Run));
}
// exe 0x5b2e60
void trapRun(const PlayerEnv& env, StaticActor& actor, TrapFields& fields) {
    actor.tint = kTintNeutral;
    if (!(fields.mode & kTrapNoDamage) && damage(actor, fields) < 0) return;
    switch (TrapSub(actor.subState)) { // [0x8d07f0]
    case TrapSub::Start: trapStart(actor, fields); break;
    case TrapSub::Patrol: patrol(actor, fields); break;
    case TrapSub::Attack: attack(actor, fields); break;
    case TrapSub::Retract: retract(actor, fields); break;
    default: notPortedAt(kStoryPort, "0x5b2ba0 sub (past the table)", actor.subState);
    }
    collide(env, actor, fields);
}
// exe 0x5b3610: a death explosion (the global static type 8), the blade spinning down, sinking 0x28 ticks blinking,
// the drops, freed
void trapDying(const PlayerEnv& env, StaticActor& actor, TrapFields& fields) {
    actor.tint = kTintFlash;
    if (actor.step == 0) {
        actor.step = 1;
        fields.bob = 0;
        fields.speed = 0;
        fields.timer = 0x28;
        countDefeat();
        if (const uint32_t e = allocActor60(0)) { // exe 0x5e1300
            StaticActor& explosion = staticActorAt(e);
            auto&& params = ExplosionParams(actorHeaderAt(e));
            explosion.flags = kActorLive;
            explosion.loopFlags |= kActorSweepGroup2;
            explosion.type = typeByte(GlobalStaticType::Explosion);
            explosion.variant = 0;
            explosion.pos.x.whole = actor.pos.x.whole;
            explosion.pos.y.whole = actor.pos.y.whole;
            explosion.pos.z.whole = actor.pos.z.whole;
            params.spread = uint16_t(fields.bladeRadius);
            params.flashes = uint16_t(fields.timer);
        }
        fields.stepSize = int16_t(fields.stepSize >> 1);
    }
    if (fields.mode & kTrapArmed) {
        if (fields.spinSpeed > 0) {
            const uint16_t v = uint16_t(uint16_t(fields.spinSpeed) - uint16_t(fields.stepSize));
            actor.rotation.z = int16_t(uint16_t(actor.rotation.z) + v);
            fields.spinSpeed = int16_t(v);
        } else {
            actor.rotation.z = int16_t(uint16_t(actor.rotation.z) + uint16_t(fields.stepSize));
        }
    }
    if (uint16_t(fields.timer) & 1) actor.tint = kTintDeathBlink;
    if (fields.timer < 0x19) fields.mode |= kTrapDying;
    fields.timer = int16_t(uint16_t(fields.timer) - 1u);
    if (fields.timer == 0) {
        spawnDrops(actor.pos, staticStats(actor.type, 0).dropTable); // exe 0x5f1270
        freeActor(actor);                                           // exe 0x5e1540
        return;
    }
    moveAlongYaw(actor.pos, fields.travelYaw, 0, fields.bob, fields.speed); // exe 0x5e36f0
    collide(env, actor, fields);
}
// exe 0x5b2ba0
void bladeTrap(const PlayerEnv& env, StaticActor& actor) {
    TrapFields& fields = classState<TrapFields>(actor);
    contactFromHits(actor, fields);
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (TrapState(actor.state)) { // [0x8d07e4]
    case TrapState::Init: trapInit(actor, fields); break;
    case TrapState::Run: trapRun(env, actor, fields); break;
    case TrapState::Dying: trapDying(env, actor, fields); break;
    default: notPortedAt(kStoryPort, "0x5b2ba0 state (past the table)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    const StaticActorStats row = staticStats(actor.type, 0);
    int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const uint8_t mode = fields.mode;
    fields.hit.result = 0;
    fields.hit.previousX = actor.prevPos.x.whole;
    fields.hit.previousZ = actor.prevPos.z.whole;
    fields.armedHit.result = 0;
    fields.armedHit.previousX = actor.prevPos.x.whole;
    fields.armedHit.previousZ = actor.prevPos.z.whole;
    if (mode & kTrapNoHitEntry) return;
    const uint32_t owner = hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | ((mode & kTrapDying) ? kHitNoLockOn : 0u);
    const uint32_t r = uint16_t(fields.bladeRadius);
    if (!(mode & kTrapArmed)) {
        hitListSubmit(pos, fields.hit, owner | hitRadius(r), uint32_t(row.damage) | kHitAttackSolid);
    } else {
        hitListSubmit(pos, fields.armedHit, owner | hitRadius(r),
                      uint32_t(row.power) | kHitAttackDeflects | kHitAttackThrown);
        // the blade's edge: the radius back along the yaw (z 1.5 times)
        const int32_t sx = (int32_t(fixedSin(actor.rotation.y)) * int32_t(r)) >> 12;
        const int32_t cz = (int32_t(fixedCos(actor.rotation.y)) * int32_t(r)) >> 12;
        pos[0] = int16_t(uint16_t(uint16_t(pos[0]) - uint16_t(sx)));
        pos[2] = int16_t(uint16_t(uint16_t(pos[2]) - uint16_t((cz >> 1) + cz)));
        hitListSubmit(pos, fields.hit,
                      hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(r >> 1),
                      uint32_t(row.deathAttack) | kHitAttackThrown | kHitAttackSolid);
    }
    // exe 0x5f20f0(a, radius, 0x200, 0): the shadow (render output)
}

} // namespace

void bladeTrapUpdate(const PlayerEnv& env, uint32_t actor) { bladeTrap(env, staticActorAt(actor)); }

} // namespace od::port
