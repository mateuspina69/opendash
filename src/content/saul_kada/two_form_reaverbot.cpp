// The two-form reaverbot of ST26 sub 1 (pool 0x20 type 0x29, exe 0x48d640; hypothesis names).
// States [0x892888]: 0 bind form A (model variant 2) 0x48d6d0, 1 form A 0x48d820 (its sub-states [0x89289c]), 2 bind
// form B (model variant 3) 0x48d950, 3 form B 0x48da10 (its sub-states [0x8928b8]), 4 freed 0x468fa0. The sub-state
// tables are windows of the same 23-entry table (bases 5 / 12; the variant table of exe 0x48dc90 is base 20), so they
// are dispatched here through one table of routines exactly as the exe indexes it.
// Form A rises out (its scale 0x200 -> 0x220 -> 0x200), then by its variant: 0 walks a fixed out-and-back beat, 1
// stands, 2 idles. A hit, a push or its enemy group's marks turn it into form B, which wanders around the player, gives
// way to neighbour enemies and dies into an explosion and the drops of its row. The shadow is render output.
#include "content/saul_kada/two_form_reaverbot.hpp"
#include "content/saul_kada/two_form_reaverbot_state.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/enemy_motion.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection_state.hpp"
#include "game/random.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace two_form_reaverbot_state;

namespace {

// the routines of the one table [0x892888] (the states 0..4, form A's sub-states from 5, form B's from 12, form A's
// idle variants from 20)
enum class FormRoutine : uint8_t {
    BindA, FormA, BindB, FormB, Free,                // 0..4 (0x468fa0 frees)
    Rise, Idle, Watch, FallA, Turn,                  // 5..9
    Shrink, Vanish, Grow, Walk, Pushed,              // 10..14
    FallB, Stagger, Death, ShrinkB,                  // 15..18 (0x48e410: 0x48dee0's code)
    TurnWalk, Beat, Stand, Nothing,                  // 19..22
};
constexpr size_t kFormRoutines = 23;
constexpr uint8_t kFormASubs = 5, kFormBSubs = 12, kIdleVariants = 20;
enum class FormState : uint8_t { BindA = 0, FormA = 1, BindB = 2, FormB = 3, Free = 4 };
// form A's sub-states (from 5) and form B's (from 12)
enum class FormASub : uint8_t { Rise = 0, Idle = 1, Watch = 2, Fall = 3, Turn = 4, Shrink = 5, Vanish = 6, Grow = 7 };
enum class FormBSub : uint8_t { Grow = 0, Walk = 1, Pushed = 2, Fall = 3, Stagger = 4, Death = 5, Shrink = 6, TurnWalk = 7 };
enum class CapsuleKind : uint8_t { FormA = 0, FormB = 1, Dead = 2 };
// Its spawn parameters: the bit of its enemy group's marks it sets (as a shift), a word its growth clears
struct FormParams {
    explicit FormParams(const AnimatedActor& actor) : FormParams(const_cast<AnimatedActor&>(actor)) {}
    explicit FormParams(AnimatedActor& actor) : markBit(actor.params[1]), grownWord(actor.params, 2) {}
    FieldAlias<int8_t, uint8_t> markBit;
    BytesField<uint16_t, 4> grownWord;
};
constexpr uint8_t kFormNoHits = 0x02, kFormNoDamage = 0x04, kFormStaggered = 0x08, kFormNoShadow = 0x10,
                  kFormFalling = 0x20, kFormNearPlayer = 0x40;
constexpr uint32_t kHitKinds = kHitAttackBump | kHitAttackByPlayer | kHitAttackThrown | kHitAttackStrong; // 0x1d0000
constexpr uint16_t kHurtSound = 0x8d, kStepSound = 0x166, kGrowlSound = 0x167, kTurnSound = 0x168,
                   kWalkSound = 0x169, kFormBSound = 0x16a;
constexpr uint8_t kDustVariant = 3;
constexpr uint8_t kModelFormA = 2, kModelFormB = 3;
constexpr int16_t kScaleOne = 0x200, kScaleFormB = 0x156;

AnimatedActorStats formStats(const AnimatedActor& actor) { return animatedStats(actor.type, actor.spawnByte); }
void sound(uint16_t id, const AnimatedActor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380
void countUp(FormFields& form) { form.timer += 1u; }
void enterASub(AnimatedActor& actor, FormASub sub) {
    actor.subState = uint8_t(sub);
    actor.step = 0;
}
void enterBSub(AnimatedActor& actor, FormBSub sub) {
    actor.subState = uint8_t(sub);
    actor.step = 0;
}

// ---- the class's helpers ---------------------------------------------------------------------------------------
// exe 0x48d2c0: a hit of the player's / thrown / strong / a bump with damage: flashed, sound 0x8d, its hit points
// (unless no damage; -1 dead), its stagger counter (8 hits or a 0x20 one: staggered); 1 hit, 0 none
int hitCheck(AnimatedActor& actor, FormFields& form) {
    form.flags &= uint8_t(~kFormStaggered);
    const uint32_t result = form.hitBox.result;
    const int damage = int(result & kHitDamageMask);
    if (!(result & kHitKinds) || damage == 0) return 0;
    actor.tint = kTintFlash;
    sound(kHurtSound, actor);
    if (!(form.flags & kFormNoDamage)) {
        if (hitPointsAdd(actor, -damage) != 0) return -1; // exe 0x5e52f0
        int8_t count = form.stagger;
        if (count >= 0) {
            count = int8_t(count + 1);
            form.stagger = count;
            if (damage >= 0x20 || count >= 8) {
                form.stagger = int8_t(0xf0);
                form.flags |= kFormStaggered;
            }
        }
    }
    return 1;
}
// a dust puff (the global hit spark, variant 3) at random around its feet; `ySpread` its height's spread
void dustPuff(const AnimatedActor& actor, uint32_t ySpread) {
    const uint32_t slot = allocActorA0(0); // exe 0x5e13b0
    if (!slot) return;
    EffectActor& dust = effectActorAt(slot);
    dust.flags = kActorLive;
    dust.type = typeByte(GlobalEffectType::HitSpark);
    dust.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    dust.variant = kDustVariant;
    dust.pos.x = int16_t(uint16_t(uint16_t(actor.pos.x.whole) - (crtRand() & 0xff) + 0x80u));
    dust.pos.y = int16_t(uint16_t(uint16_t(actor.pos.y.whole) - (crtRand() & ySpread)));
    dust.pos.z = int16_t(uint16_t(uint16_t(actor.pos.z.whole) - (crtRand() & 0xff) + 0x80u));
    classState<HitSparkFields>(effectActorAt(slot)).size = uint16_t((crtRand() & 0xf) + 0xa4);
}
// exe 0x48d3e0: on the frames with bit 4, a dust puff around its feet
void dust(const AnimatedActor& actor) {
    if (!(gameState().frame & 4)) return;
    dustPuff(actor, 0x3f);
}
// exe 0x48e540: the ground and the walls. On the ground: the ground within +-0x3c -> snapped onto it (unless on a
// platform above it), 0; below it -> pushed out (blocked: back), 2; above it -> falling, 1 (0 on a platform).
// Airborne: pushed out (blocked: back, 2); landed -> on the ground; 2
int ground(const PlayerEnv& env, AnimatedActor& actor, FormFields& form) {
    form.wallResult = 0;
    if (actor.airborne == 0) {
        const int g = actorGround(env, actor); // exe 0x40f190
        const int y = actor.pos.y.whole;
        if (uint32_t(g - y + 0x3c) <= 0x78) {
            if (actor.onPlatform == 0 || g < y) actor.pos.y = int32_t(uint32_t(g) << 16);
            return 0;
        }
        if (y > g) {
            const uint32_t result = actorPushOut(env, actor); // exe 0x410960
            form.wallResult = result;
            if (int32_t(result) < 0) actor.pos = actor.prevPos;
            return 2;
        }
        if (actor.onPlatform != 0) return 0;
        actor.airborne = 1;
        return 1;
    }
    const uint32_t result = actorPushOut(env, actor); // exe 0x410960
    form.wallResult = result;
    if (int32_t(result) < 0) {
        actor.pos = actor.prevPos;
        return 2;
    }
    if ((actor.actorHit | result) & kWallOnYPlus) actor.airborne = 0;
    return 2;
}
// exe 0x48e610(actor, kind): its hit box (cleared, its previous x / z) and, by kind, its body's capsule between
// (x, y - 0xc0 / 0xe4, z) and (x, y - 0x30 / 0x40, z)
void submitCapsule(const AnimatedActor& actor, FormFields& form, CapsuleKind kind) {
    form.hitBox.previousX = actor.prevPos.x.whole;
    form.hitBox.previousZ = actor.prevPos.z.whole;
    form.hitBox.result = 0;
    const int16_t x = actor.pos.x.whole, z = actor.pos.z.whole, y = actor.pos.y.whole;
    int16_t lo, hi, r;
    uint32_t attack;
    switch (kind) {
    case CapsuleKind::FormA:
        hi = int16_t(y - 0xc0);
        lo = int16_t(y - 0x30);
        r = 0x3c;
        attack = uint32_t(int32_t(int16_t(formStats(actor).attack))) | hitImmunity(kHitGroupPlayer) | kHitAttackSolid;
        break;
    case CapsuleKind::FormB:
        hi = int16_t(y - 0xe4);
        lo = int16_t(y - 0x40);
        r = 0x60;
        attack = uint32_t(int32_t(int16_t(formStats(actor).strongAttack))) | hitImmunity(kHitGroupPlayer) |
                 kHitAttackThrown | kHitAttackSolid;
        break;
    case CapsuleKind::Dead:
        hi = int16_t(y - 0xe4);
        lo = int16_t(y - 0x40);
        r = 0x60;
        attack = kHitImmunityMask | kHitAttackBump;
        break;
    default: return;
    }
    r = int16_t(r >> 2);
    const int16_t pa[3] = {x, hi, z}, pb[3] = {x, lo, z};
    hitListSubmitCapsule(pa, uint16_t(r), pb, uint16_t(r), form.hitBox,
                         hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | kHitCompareHeights | hitCapsule(1),
                         attack); // exe 0x5e4190
}
// the fall shared by the sub-states: falling (the anim back to 0)
void toFall(AnimatedActor& actor, FormFields& form) {
    actor.subState = 3; // form A's / form B's fall
    actor.step = 0;
    actor.anim = 0;
    form.flags |= kFormFalling;
}
// the gravity step (+0x30 up to 0x2000) with the move
void fallStep(AnimatedActor& actor) {
    actor.velocity.y += 0x30u;
    if (actor.velocity.y > 0x2000) actor.velocity.y = 0x2000;
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
}

// ---- form B's wander -------------------------------------------------------------------------------------------
// exe 0x48d470: its target yaw re-aimed once its timer runs out (near the player: kept until it leaves 0x280)
void wander(const AnimatedActor& actor, FormFields& form, uint32_t distance) {
    if (form.flags & kFormNearPlayer) {
        if (distance >= 0x280) form.flags &= uint8_t(~kFormNearPlayer);
        return;
    }
    const uint16_t left = form.timer;
    if (left != 0) {
        form.timer = uint16_t(left - 1u);
        return;
    }
    const uint32_t r = crtRand();
    if (r & 0x3f) {
        form.targetYaw = uint16_t(angleOf(player().pos, actor.pos)); // exe 0x5e3bc0
        return;
    }
    form.targetYaw = uint16_t(form.targetYaw + ((r & 0x40) ? 0x190u : 0xfe70u));
    form.timer = uint16_t(((r >> 7) & 7) + 0x10);
}
// exe 0x48d500: giving way for 0x78 ticks; the neighbour classified
void yieldStart(const AnimatedActor& actor, FormFields& form) {
    form.timer = 0x78;
    neighbourClassify(actor, form.neighbour); // exe 0x5e5f70
    form.neighbour = uint16_t(form.neighbour | (crtRand() & 1));
}
// exe 0x48d530: steered / slowed by the neighbour's movement; done once it left or the time ran out
void yield(AnimatedActor& actor, FormFields& form) {
    const Actor& other = neighbourActor(form.neighbour); // exe 0x5e5cb0
    const uint16_t bits = form.neighbour;
    bool gone = false;
    if (bits & kNeighbourOpposite)
        form.targetYaw = uint16_t(headingFromOther(actor, other, neighbourSide(form.neighbour), 0x100)); // 0x5e5d90
    if ((bits & kNeighbourCrossingSlow) && stepTowardLimit(actor.velocity.z, 0xfff0, 0x180) != 0)   // exe 0x5e5510
        actor.velocity.z = int16_t(0xfe80);
    if ((bits & kNeighbourCatching) && stepTowardLimit(actor.velocity.z, 0xfff0, 0x80) != 0)
        actor.velocity.z = int16_t(0xff80);
    if (other.flags == 0 || (uint32_t(int32_t(other.pos.x.whole) - actor.pos.x.whole + 0x100) > 0x200 &&
                             uint32_t(int32_t(other.pos.z.whole) - actor.pos.z.whole + 0x100) > 0x200))
        gone = true;
    form.timer -= 1u;
    if (form.timer == 0 || gone) {
        form.timer = 0;
        form.flags &= uint8_t(~kFormNearPlayer);
        actor.velocity.z = int16_t(0xff00);
        actor.step = 0;
    }
}

// ---- the routines ----------------------------------------------------------------------------------------------
void runRoutine(const PlayerEnv& env, AnimatedActor& actor, FormFields& form, size_t index);

// exe 0x48db60 (form A 0): its scale up to 0x220 and back, a wait of 30, then by its variant
void rise(AnimatedActor& actor, FormFields& form) {
    switch (actor.step) {
    case 0:
        if (actor.variant == 1) {
            actor.scale.y = kScaleOne;
            enterASub(actor, FormASub::Idle);
            actor.velocity.z = int16_t(0xffb8);
            form.animEntry = 6;
            actor.anim = 2;
            form.flags = 0;
            return;
        }
        actor.scale.y = int16_t(uint16_t(uint16_t(actor.scale.y) + 0x10u));
        if (actor.scale.y >= 0x220) ++actor.step;
        dust(actor); // exe 0x48d3e0
        return;
    case 1:
        actor.scale.y = int16_t(uint16_t(uint16_t(actor.scale.y) - 8u));
        if (actor.scale.y <= kScaleOne) {
            ++actor.step;
            actor.scale.y = kScaleOne;
            form.timer = 0;
        }
        dust(actor);
        return;
    case 2:
        countUp(form);
        if (form.timer == 0x1e) ++actor.step;
        return;
    case 3:
        enterASub(actor, FormASub::Idle);
        actor.velocity.z = int16_t(0xffb8);
        switch (actor.variant) {
        case 0:
            form.animEntry = uint8_t(crtRand() & 0x1f);
            actor.anim = 0;
            break;
        case 1:
            form.animEntry = 6;
            actor.anim = 2;
            break;
        case 2:
            form.animEntry = 0;
            actor.anim = 2;
            break;
        default: break;
        }
        form.flags = 0;
        return;
    default: return;
    }
}
// exe 0x48dcf0 (variant 0): walking 0x1c7 ticks, turning 0x40 x 0x20, walking back, back on its spawn point, turning
void beat(AnimatedActor& actor, FormFields& form) {
    bool next = false;
    switch (actor.step) {
    case 0:
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        countUp(form);
        next = form.timer == 0x1c7;
        break;
    case 1:
        actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + 0x20u));
        countUp(form);
        next = form.timer == 0x40;
        break;
    case 2:
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        countUp(form);
        if (form.timer == 0x1c7) { // back on its spawn point
            actor.pos.x = int32_t(uint32_t(int32_t(form.spawnX)) << 16);
            actor.pos.y = int32_t(uint32_t(int32_t(form.spawnY)) << 16);
            actor.pos.z = int32_t(uint32_t(int32_t(form.spawnZ)) << 16);
            next = true;
        }
        break;
    case 3:
        actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + 0x20u));
        countUp(form);
        if (form.timer == 0x40) {
            form.timer = 0;
            actor.step = 0;
        }
        break;
    default: break;
    }
    if (next) {
        form.timer = 0;
        ++actor.step;
    }
    if (actor.keyIndex == 0xf || actor.keyIndex == 0x25) sound(kStepSound, actor); // the steps
}
// exe 0x48de00 (variant 1): its animation's entry 6, the pose forced
void stand(AnimatedActor& actor, FormFields& form) {
    form.animEntry = 6;
    actor.prevAnim = 0xff;
}
// exe 0x48dc90 (form A 1): its variant's behaviour, then the ground
void idle(const PlayerEnv& env, AnimatedActor& actor, FormFields& form) {
    runRoutine(env, actor, form, kIdleVariants + actor.variant);
    const int r = ground(env, actor, form);
    if (r == 2) {
        enterASub(actor, FormASub::Vanish);
        form.flags = uint8_t(r);
    } else if (r == 1) {
        enterASub(actor, FormASub::Fall);
        form.flags |= kFormFalling;
        return;
    }
    planarDistance(actor.pos, player().pos); // exe 0x5e3b00 (its result unused)
}
// exe 0x48de10 (form A 2): the player beyond 0xcd -> idle
void watch(const PlayerEnv& env, AnimatedActor& actor, FormFields& form) {
    if (uint32_t(planarDistance(actor.pos, player().pos)) > 0xcd) {
        enterASub(actor, FormASub::Idle);
        actor.anim = 0;
    }
    if (ground(env, actor, form) == 1) toFall(actor, form);
}
// exe 0x48de70 (form A 3): the fall; landed -> the turn (turning) or idle
void fallA(const PlayerEnv& env, AnimatedActor& actor, FormFields& form) {
    fallStep(actor);
    ground(env, actor, form);
    if (actor.airborne != 0) return;
    actor.velocity.y = 0;
    if (actor.anim == 3) {
        enterASub(actor, FormASub::Turn);
        return;
    }
    enterASub(actor, FormASub::Idle);
    form.flags &= uint8_t(~kFormFalling);
}
// exe 0x48c8f0 (form A 4): the turn into form B (animation 4), then rebound as form B
void turn(AnimatedActor& actor, FormFields& form) {
    switch (actor.step) {
    case 0:
        form.flags = kFormNoHits;
        ++actor.step;
        break;
    case 1:
        if (actor.animEnded()) {
            actor.anim = 4;
            ++actor.step;
            form.timer = 0;
        }
        break;
    case 2:
        countUp(form);
        if (form.timer == 0x28) {
            sound(kTurnSound, actor);
            form.soundTimer = uint8_t((rngStep() & 0x1f) + 0x3c);
            actor.enterState(uint8_t(FormState::BindB));
        }
        break;
    default: break;
    }
    if (!(gameState().frame & 4)) return;
    dustPuff(actor, 0xff); // (its height spread 0xff here)
}
// exe 0x48dee0 / 0x48e410 (form A 5 / form B 6): its scale down by 0x10; at 0 freed
void shrink(AnimatedActor& actor) {
    actor.scale.y = int16_t(uint16_t(uint16_t(actor.scale.y) - 0x10u));
    if (actor.scale.y <= 0) {
        actor.scale.y = 0;
        actor.enterState(uint8_t(FormState::Free));
    }
    dust(actor);
}
// exe 0x48df20 (form A 6): faded out over 0x10 ticks (its tint greying), then hidden and freed
void vanish(AnimatedActor& actor, FormFields& form) {
    switch (actor.step) {
    case 0:
        actor.semiTransparency = semiTransparencyBits(1, 1); // exe 0x5e1a00(actor, 1, 1)
        form.timer = 0x10;
        form.flags |= kFormNoShadow;
        ++actor.step;
        break;
    case 1:
        form.timer -= 1u;
        if (form.timer == 0) {
            actor.enterState(uint8_t(FormState::Free));
            actor.setDrawn(false);
        }
        break;
    default: break;
    }
    const uint32_t v = form.timer;
    actor.tint = uint16_t(((v << 5 | v) << 5) | v);
}
// exe 0x48dfa0 (form A 7 / form B 0): its scale from 0x156 + 0xa a tick for 0x11 ticks, then walking
void grow(AnimatedActor& actor, FormFields& form) {
    actor.scale.x = int16_t(uint16_t(uint16_t(actor.scale.x) + 0xau));
    actor.scale.z = actor.scale.x;
    actor.scale.y = actor.scale.x;
    countUp(form);
    if (form.timer == 0x11) {
        actor.scale.z = kScaleOne;
        actor.scale.y = kScaleOne;
        actor.scale.x = kScaleOne;
        enterBSub(actor, FormBSub::Walk);
        form.animEntry = uint8_t(crtRand() & 0x1f);
        actor.anim = 0;
        actor.velocity.z = int16_t(0xff00);
        form.flags = 0;
        form.timer = 0;
        FormParams(actor).grownWord = 0;
    }
    dust(actor);
}
// exe 0x48e040 (form A 8 / form B 1): wandering (step 0) / giving way (1, 2); the walls, the pushes, its life out
void walk(const PlayerEnv& env, AnimatedActor& actor, FormFields& form) {
    const uint32_t distance = uint32_t(planarDistance(actor.pos, player().pos)); // exe 0x5e3b00
    switch (actor.step) {
    case 0: wander(actor, form, distance); break;
    case 1:
        yieldStart(actor, form);
        ++actor.step;
        yield(actor, form);
        break;
    case 2: yield(actor, form); break;
    default: break;
    }
    if (((uint32_t(actor.keyIndex) - 2u) & 7) == 0) sound(kWalkSound, actor);
    turnYawTo(actor.rotation.y, form.targetYaw, actor.spin.y); // exe 0x5e3df0
    integrateVelocity(actor, 0, 0, actor.velocity.z);
    const int r = ground(env, actor, form);
    if (r == 1) {
        toFall(actor, form);
    } else if (r == 2) {
        if (form.timer == 0) { // a wall: its yaw reflected (through the projection scratch's point), held 0x10..0x1f
            LaneVec4& wall = projectionScratch().point;
            wall.z = 0;
            wall.x = 0;
            wall.y = actor.rotation.y;
            reflectRotationOffWalls(wall, form.wallResult); // exe 0x410740
            form.targetYaw = uint16_t(wall.y);
            form.timer = uint16_t((crtRand() & 0xf) + 0x10);
        }
    } else if (form.hitBox.result & kHitAttackBump) { // pushed: facing the player's side, backing off
        enterBSub(actor, FormBSub::Pushed);
        actor.anim = 1;
        actor.velocity.z = 0x80;
        actor.rotation.y = int16_t(angleOf(player().pos, actor.pos)); // exe 0x5e3bc0
    }
    if (distance < 0x40) form.flags |= kFormNearPlayer;
    if (form.lifeCount == 0x258) {
        enterBSub(actor, FormBSub::Shrink);
        actor.anim = 2;
        form.flags = kFormNoHits;
    }
}
// exe 0x48e1c0 (form B 2): sliding back; at its animation's end the turn-walk
void pushed(const PlayerEnv& env, AnimatedActor& actor, FormFields& form) {
    integrateVelocity(actor, 0, 0, actor.velocity.z);
    if (actor.animEnded()) {
        enterBSub(actor, FormBSub::TurnWalk);
        actor.anim = 0;
        actor.velocity.z = int16_t(0xff00);
        form.targetYaw = uint16_t(actor.rotation.y);
        form.timer = 0;
    }
    if (ground(env, actor, form) == 1) toFall(actor, form);
}
// exe 0x48e240 (form B 3): the fall; landed -> walking
void fallB(const PlayerEnv& env, AnimatedActor& actor, FormFields& form) {
    fallStep(actor);
    ground(env, actor, form);
    if (actor.airborne != 0) return;
    enterBSub(actor, FormBSub::Walk);
    actor.anim = 0;
    actor.velocity.z = int16_t(0xff00);
    actor.velocity.y = 0;
    form.flags &= uint8_t(~kFormFalling);
}
// exe 0x48e2b0 (form B 4): the stagger animation, then walking
void stagger(const PlayerEnv& env, AnimatedActor& actor, FormFields& form) {
    if (actor.animEnded()) {
        actor.anim = 0;
        enterBSub(actor, FormBSub::Walk);
    }
    if (ground(env, actor, form) == 1) toFall(actor, form);
}
// exe 0x48e300 (form B 5): an explosion; no hits from 0xf, its drops and freed at 0x20
void death(const PlayerEnv& env, AnimatedActor& actor, FormFields& form) {
    if (actor.airborne != 0) {
        fallStep(actor);
        ground(env, actor, form);
    } else {
        form.flags &= uint8_t(~kFormFalling);
        actor.velocity.y = 0;
    }
    if (actor.step == 0) {
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
            params.flashes = 0x20;
        }
        form.timer = 0;
        ++actor.step;
        return;
    }
    countUp(form);
    const uint16_t ticks = form.timer;
    if (ticks == 0x20) {
        form.timer = 0;
        actor.enterState(uint8_t(FormState::Free));
        spawnDrops(actor.pos, formStats(actor).dropTable); // exe 0x5f1270
        return;
    }
    if (ticks == 0xf) form.flags |= kFormNoHits;
}
// exe 0x48e450 (form B 7): walking toward its target yaw (+-0x200 on a wall) up to 0x5a ticks within 0x280
void turnWalk(const PlayerEnv& env, AnimatedActor& actor, FormFields& form) {
    turnYawTo(actor.rotation.y, form.targetYaw, actor.spin.y); // exe 0x5e3df0
    integrateVelocity(actor, 0, 0, actor.velocity.z);
    const uint32_t distance = uint32_t(planarDistance(actor.pos, player().pos));
    const int r = ground(env, actor, form);
    if (r == 1) {
        toFall(actor, form);
    } else if (r == 2) {
        if (int32_t(actor.rotation.y) == int32_t(form.targetYaw))
            form.targetYaw = uint16_t(form.targetYaw + ((crtRand() & 1) ? 0xfe00u : 0x200u));
    } else if (form.hitBox.result & kHitAttackBump) {
        enterBSub(actor, FormBSub::Pushed);
        actor.anim = 1;
        actor.velocity.z = 0x80;
    }
    if (distance < 0x280) {
        countUp(form);
        if (int16_t(form.timer) <= 0x5a) return;
    }
    actor.anim = 0;
    enterBSub(actor, FormBSub::Walk);
    form.timer = 0;
}

// ---- the states ------------------------------------------------------------------------------------------------
// exe 0x48d6d0: form A's model, registered as an enemy, the hit points of its row, on the ground
void bindA(const PlayerEnv& env, AnimatedActor& actor, FormFields& form) {
    if (!bindActorModel(actor, kModelFormA)) { // exe 0x5e0bc0 on a local byte
        freeActor(actor);                      // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn;
    actor.scale.y = 0;
    actor.anim = 1;
    actor.prevAnim = 1;
    const uint16_t hp = hasAnimatedStats(actor.type) ? formStats(actor).hitPoints : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.velocity.setZSpare(0, 0);
    actor.velocity.setXY(0, 0);
    actor.spin.x = 0;
    actor.spin.y = 0x40;
    actor.collisionBox.pointAt(TableId::TwoFormReaverbotFormBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.actorHit = 0;
    actor.onPlatform = 0;
    actor.airborne = 0;
    actor.platform = nullptr;
    actor.clearClassBlock(); // exe 0x40dc40
    form.flags = kFormNoHits;
    form.targetYaw = uint16_t(actor.rotation.y);
    form.group = uint8_t(enemyGroupFindFor(actor, actor.variant != 0 ? 1u : 0u)); // exe 0x5e6cd0
    form.lifeCount = 0;
    registerEnemy(actor.slot, 0); // exe 0x5e5c10
    form.spawnX = actor.pos.x.whole;
    form.spawnY = actor.pos.y.whole;
    form.spawnZ = actor.pos.z.whole;
    actor.pos.y = int32_t(uint32_t(actorGround(env, actor)) << 16); // exe 0x40f190
    const uint32_t key = crtRand() & 0x1f;
    animSetPose(actor, actor.anim, int(key)); // exe 0x5e1c30
    actor.enterState(uint8_t(FormState::FormA));
}
// the animation of both forms: a new one at its entry key (then cleared), else stepped
void animate(AnimatedActor& actor, FormFields& form) {
    if (actor.anim == actor.prevAnim) {
        animStepPose(actor); // exe 0x5e1e80
    } else {
        animSetPose(actor, actor.anim, form.animEntry); // exe 0x5e1c30
        form.animEntry = 0;
    }
}
// exe 0x48d820: form A
void formA(const PlayerEnv& env, AnimatedActor& actor, FormFields& form) {
    if (form.groupMark == 0) {
        const bool marked = enemyGroupMarks(int32_t(form.group), EnemyGroupMarks::Either) != 0; // 0x5e6d60
        bool turnNow = false;
        if (!(form.flags & kFormNoHits) && (hitCheck(actor, form) != 0 || (form.hitBox.result & kHitAttackBump)))
            turnNow = true;
        if (marked) turnNow = true;
        if (turnNow) { // into form B; its group's mark set from now on
            actor.anim = 3;
            enterASub(actor, FormASub::Turn);
            form.groupMark = uint8_t(1u << (uint32_t(int32_t(FormParams(actor).markBit.get())) & 0x1f));
        }
    }
    runRoutine(env, actor, form, kFormASubs + actor.subState);
    if (form.groupMark == 0 && FormASub(actor.subState) != FormASub::Turn) {
        form.soundTimer -= 1u;
        if (form.soundTimer == 0) {
            form.soundTimer = 0x37;
            sound(kGrowlSound, actor);
        }
    }
    if (!(form.flags & kFormNoHits)) submitCapsule(actor, form, CapsuleKind(form.capsuleKind)); // 0x48e610
    animate(actor, form);
    // unless kFormNoShadow: the shadow (exe 0x5f20f0(actor, 0x40, 0x100, 0)) -- render output
}
// exe 0x48d950: rebound as form B (its bone blocks released first), scale 0x156
void bindB(AnimatedActor& actor, FormFields& form) {
    actorSystem().boneBlocksUsed &= ~uint32_t(actor.boneBlocks); // exe 0x5e1760
    if (!bindActorModel(actor, kModelFormB)) {                    // exe 0x5e0bc0 on a local byte
        freeActor(actor);                                         // exe 0x5e1490
        return;
    }
    actor.scale.z = kScaleFormB;
    actor.scale.y = kScaleFormB;
    actor.scale.x = kScaleFormB;
    actor.collisionBox.pointAt(TableId::TwoFormReaverbotFormBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.anim = 2;
    actor.prevAnim = 2;
    const uint32_t key = crtRand() & 0xf;
    animSetPose(actor, actor.anim, int(key)); // exe 0x5e1c30
    form.stagger = 0;
    form.capsuleKind = uint8_t(CapsuleKind::FormB);
    form.timer = 0;
    form.hitBox.result = 0;
    actor.enterState(uint8_t(FormState::FormB));
    // the shadow (exe 0x5f20f0(actor, 0x60, 0x100, 0)) -- render output
}
// exe 0x48da10: form B
void formB(const PlayerEnv& env, AnimatedActor& actor, FormFields& form) {
    if (!(form.flags & kFormNoHits)) {
        if (hitCheck(actor, form) == -1) { // dead: its dead capsule
            enterBSub(actor, FormBSub::Death);
            actor.anim = 4;
            actor.velocity.z = 0;
            form.flags = 0;
            form.timer = 0;
            form.capsuleKind += 1u;
        } else if ((form.flags & (kFormStaggered | kFormFalling)) == kFormStaggered) { // staggered on the ground
            enterBSub(actor, FormBSub::Stagger);
            actor.anim = 3;
        }
    }
    runRoutine(env, actor, form, kFormBSubs + actor.subState);
    if (FormBSub(actor.subState) != FormBSub::Death) {
        form.soundTimer -= 1u;
        if (form.soundTimer == 0) {
            form.soundTimer = uint8_t((rngStep() & 0x3f) + 0x6a);
            sound(kFormBSound, actor);
        }
    }
    if (!(form.flags & kFormNoHits)) submitCapsule(actor, form, CapsuleKind(form.capsuleKind)); // 0x48e610
    actorCollisionSubmitMover(actor); // exe 0x4140b0
    animate(actor, form);
    // the shadow (exe 0x5f20f0(actor, 0x60, 0x100, 0)) -- render output
}

// [0x892888]: the one table of routines
constexpr FormRoutine kRoutines[kFormRoutines] = {
    FormRoutine::BindA,  FormRoutine::FormA,   FormRoutine::BindB,  FormRoutine::FormB,    FormRoutine::Free,
    FormRoutine::Rise,   FormRoutine::Idle,    FormRoutine::Watch,  FormRoutine::FallA,    FormRoutine::Turn,
    FormRoutine::Shrink, FormRoutine::Vanish,  FormRoutine::Grow,   FormRoutine::Walk,     FormRoutine::Pushed,
    FormRoutine::FallB,  FormRoutine::Stagger, FormRoutine::Death,  FormRoutine::ShrinkB,  FormRoutine::TurnWalk,
    FormRoutine::Beat,   FormRoutine::Stand,   FormRoutine::Nothing,
};
void runRoutine(const PlayerEnv& env, AnimatedActor& actor, FormFields& form, size_t index) {
    if (index >= kFormRoutines) notPortedAt(kStoryPort, "0x48d640: table index past [0x892888]", uint32_t(index));
    switch (kRoutines[index]) {
    case FormRoutine::BindA: bindA(env, actor, form); return;
    case FormRoutine::FormA: formA(env, actor, form); return;
    case FormRoutine::BindB: bindB(actor, form); return;
    case FormRoutine::FormB: formB(env, actor, form); return;
    case FormRoutine::Free: freeActor(actor); return;
    case FormRoutine::Rise: rise(actor, form); return;
    case FormRoutine::Idle: idle(env, actor, form); return;
    case FormRoutine::Watch: watch(env, actor, form); return;
    case FormRoutine::FallA: fallA(env, actor, form); return;
    case FormRoutine::Turn: turn(actor, form); return;
    case FormRoutine::Shrink:
    case FormRoutine::ShrinkB: shrink(actor); return;
    case FormRoutine::Vanish: vanish(actor, form); return;
    case FormRoutine::Grow: grow(actor, form); return;
    case FormRoutine::Walk: walk(env, actor, form); return;
    case FormRoutine::Pushed: pushed(env, actor, form); return;
    case FormRoutine::FallB: fallB(env, actor, form); return;
    case FormRoutine::Stagger: stagger(env, actor, form); return;
    case FormRoutine::Death: death(env, actor, form); return;
    case FormRoutine::TurnWalk: turnWalk(env, actor, form); return;
    case FormRoutine::Beat: beat(actor, form); return;
    case FormRoutine::Stand: stand(actor, form); return;
    case FormRoutine::Nothing: return; // exe 0x52ef00
    default: return;
    }
}

void twoFormReaverbot(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x48d640
    FormFields& form = classState<FormFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    runRoutine(env, actor, form, actor.state);
    if (form.stagger < 0) form.stagger += 1u;
    enemyGroupMark(int32_t(form.group), form.groupMark); // exe 0x5e6d40
}

} // namespace

void twoFormReaverbotUpdate(const PlayerEnv& env, uint32_t actor) { twoFormReaverbot(env, animatedActorAt(actor)); }

} // namespace od::port
