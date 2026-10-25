// ST10 pool-0x20 class exe 0x48c160 (sub 0), one routine per exe routine (addresses in the comments); the names are
// hypotheses from the observed behaviour.
//
// A two-form enemy. Form 1 (state 1, hit mode 0): pops up (y scale 0 -> 0x220 -> 0x200, sound 0xa3), then rolls along
// its heading with a +-0x200 wobble of the yaw (exe 0x48d370), falls off ledges, shrinks away when the player stays
// > 0xc00 away for 0x78 ticks, fades out on a lost ground; variant 2 starts hidden until the player is within 0x400.
// Any hit / contact (or its encounter group's marks when variant 0) -> the morph (animation 4, then state 2 rebinds
// the model variant 1 at scale 0x156). Form 2 (state 3, hit mode 1): grows to 0x200, chases / wanders (turning to its
// heading, giving way to the neighbour enemies), flinches on strong hits, dies at 0 hit points (a death explosion,
// the drops after 0x20 ticks), gives up after 0x258 ticks or far away.
//
// States [0x892820]: bind 0x48c1f0, form 1 0x48c330, morph rebind 0x48c460, form 2 0x48c520, free 0x468fa0. Form-1
// sub-states [0x892834]: pop up 0x48c670, roll 0x48c770, idle 0x48c820, fall 0x48c880, morph 0x48c8f0, shrink away
// 0x48ca20, fade out 0x48ca90, hidden 0x48cb20. Form-2 sub-states [0x892854]: grow 0x48cb50, chase 0x48cbf0, hit back
// 0x48cdc0, fall 0x48ce40, flinch 0x48cea0, die 0x48cef0, shrink away 0x48d000, wander 0x48d070. Spawns: the death
// explosion (global static type 8) and dust sparks (global effect type 0 variant 3). Render output skipped: the shadow
// exe 0x5f20f0.
#include "content/forbidden_island/st10_cls.hpp"
#include "content/forbidden_island/st10_cls_state.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/collision.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/enemy_motion.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/explosions.hpp"
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
#include "game/room_grid.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st10_cls_state;

namespace {

constexpr uint8_t kRollerNoHits = 0x02;
constexpr uint8_t kRollerNoDamage = 0x04;
constexpr uint8_t kRollerFlinch = 0x08;
constexpr uint8_t kRollerNoShadow = 0x10;
constexpr uint8_t kRollerFalling = 0x20;
constexpr uint8_t kRollerClose = 0x40;
constexpr int8_t kRollerHitCooldown = -0x10;
constexpr uint16_t kRollerLifetime = 0x258;

enum class RollerState : uint8_t { Bind = 0, Form1 = 1, MorphBind = 2, Form2 = 3, Free = 4 };
enum class Form1Sub : uint8_t { PopUp = 0, Roll = 1, Idle = 2, Fall = 3, Morph = 4, ShrinkAway = 5, FadeOut = 6, Hidden = 7 };
enum class Form2Sub : uint8_t { Grow = 0, Chase = 1, HitBack = 2, Fall = 3, Flinch = 4, Die = 5, ShrinkAway = 6, Wander = 7 };
enum class HitMode : uint8_t { Small = 0, Large = 1, Harmless = 2 };

constexpr uint8_t kRollerDustSparkVariant = 3;
constexpr uint16_t kSoundPopUp = 0xa3, kSoundShrink = 0xa4, kSoundRoll = 0x166, kSoundHum = 0x167, kSoundMorph = 0x168,
                   kSoundChase = 0x169, kSoundGrowl = 0x16a, kSoundHit = 0x8d;
constexpr int16_t kFullScale = 0x200, kMorphScale = 0x156;

AnimatedActorStats row(const AnimatedActor& a) { return animatedStats(a.type, a.spawnByte); }
void sound(uint16_t id, const AnimatedActor& a) { soundPlayAtPos(id, a.pos); } // exe 0x5d5380
template <class Sub> void enterSub(AnimatedActor& a, Sub s) {
    a.subState = uint8_t(s);
    a.step = 0;
}
uint32_t playerDistance(const AnimatedActor& a) { return uint32_t(planarDistance(a.pos, player().pos)); } // exe 0x5e3b00
bool dustFrame() { return (gameState().frame & 4) != 0; }

// ---- the class's routines ---------------------------------------------------------------------------------------

// exe 0x48d1b0: the ground and the walls -- 0 fine, 1 the ground lost (airborne: falling), 2 blocked (back to the
// previous position for a push-out < 0 and for a step onto a cell that is not a floor); the push-out's result kept
int groundAndWalls(const PlayerEnv& env, AnimatedActor& a, RollerFields& fields) {
    fields.pushResult = 0;
    if (a.airborne == 0) {
        const int g = actorGround(env, a); // exe 0x40f190
        const int y = a.pos.y.whole;
        if (uint32_t(g - y + 0x3c) <= 0x78u) {
            const uint32_t cell = gridCellUnder(a.pos); // exe 0x40f030
            if (cell == 0) notPortedAt(kStoryPort, "0x48d1b0: exe 0x40f030 returned null (the exe reads [0 + 1])", 0);
            if (GridCell(cell).word & (kCellCollides | kCellFloor)) {
                a.pos.y = int32_t(uint32_t(g) << 16);
                return 0;
            }
            a.pos = a.prevPos;
            return 2;
        }
        if (y <= g) {
            a.airborne = kAirborneFalling;
            return 1;
        }
        const uint32_t r = actorPushOut(env, a); // exe 0x410960
        fields.pushResult = r;
        if (int32_t(r) < 0) a.pos = a.prevPos;
        return 2;
    }
    const uint32_t r = actorPushOut(env, a); // exe 0x410960
    fields.pushResult = r;
    if (int32_t(r) < 0) {
        a.pos = a.prevPos;
        return 2;
    }
    if (r & kWallOnYPlus) {
        a.airborne = 0;
        return 0;
    }
    return r == 0 ? 0 : 2;
}
// exe 0x48d2c0: the hit box's result -- a hit (a touch or an attack kind) with damage: the flash and its sound, unless
// it takes no damage the hit points (-1: destroyed); the hits counted: 8 hits or a damage >= 0x20 -> the flinch and the
// cooldown. 1 hit, 0 none
int takeHit(AnimatedActor& a, RollerFields& fields) {
    fields.flags &= uint8_t(~kRollerFlinch);
    const uint32_t r = fields.hit.result;
    const uint32_t d = r & kHitDamageMask;
    if (!(r & (kHitAttackBump | kHitAttackByPlayer | kHitAttackThrown | kHitAttackStrong)) || d == 0) return 0;
    a.tint = kTintFlash;
    sound(kSoundHit, a);
    if (fields.flags & kRollerNoDamage) return 1;
    if (hitPointsAdd(a, -int(d)) != 0) return -1; // exe 0x5e52f0
    int8_t c = fields.hitCount;
    if (c < 0) return 1;
    c = int8_t(c + 1);
    fields.hitCount = c;
    if (int(d) >= 0x20 || c >= 8) {
        fields.hitCount = kRollerHitCooldown;
        fields.flags |= kRollerFlinch;
    }
    return 1;
}
// exe 0x48d370: the roll with a wobble: the counter 0..0x7f, the yaw a triangle wave of +-0x200 around the heading;
// forward by the velocity
void rollWobbling(AnimatedActor& a, RollerFields& fields) {
    const uint8_t c = uint8_t((fields.wobble + 1u) & 0x7f);
    fields.wobble = c;
    const uint16_t v = uint16_t((c & 0x1f) << 4);
    a.rotation.y = int16_t(v);
    if (c & 0x20) a.rotation.y = int16_t(uint16_t(0x200 - v));
    if (c & 0x40) a.rotation.y = int16_t(uint16_t(-uint16_t(a.rotation.y)));
    a.rotation.y = int16_t((uint16_t(fields.heading) + uint16_t(a.rotation.y)) & 0xfff);
    integrateVelocity(a, 0, 0, a.velocity.z); // exe 0x5e3690
}
// the dust spark (global effect type 0 variant 3) around the actor, its height spread `ySpread`; the spark slot's
// loop flags read before its stores (`loopFirst`: the inlined copy of the morph) or as the stores go
void spawnDustSpark(const AnimatedActor& a, uint32_t ySpread, bool loopFirst) {
    const uint32_t address = allocActorA0(0); // exe 0x5e13b0
    if (!address) return;
    EffectActor& e = effectActorAt(address);
    HitSparkFields& spark = classState<HitSparkFields>(e);
    const uint8_t loop = loopFirst ? uint8_t(e.loopFlags) : uint8_t(0);
    e.flags = kActorLive;
    e.type = typeByte(GlobalEffectType::HitSpark);
    if (loopFirst) e.loopFlags = uint8_t(loop | kActorSweepGroup1 | kActorLateLoop);
    else e.loopFlags |= uint8_t(kActorSweepGroup1 | kActorLateLoop);
    e.variant = kRollerDustSparkVariant;
    e.pos.x = int16_t(uint16_t(a.pos.x.whole) - (crtRand() & 0xff) + 0x80u);
    e.pos.y = int16_t(uint16_t(a.pos.y.whole) - (crtRand() & ySpread));
    e.pos.z = int16_t(uint16_t(a.pos.z.whole) - (crtRand() & 0xff) + 0x80u);
    spark.size = uint16_t((crtRand() & 0xf) + 0xa4u);
}
// exe 0x48d3e0: a dust spark around the actor on the frames with bit 2
void dustPuff(const AnimatedActor& a) {
    if (!dustFrame()) return;
    spawnDustSpark(a, 0x3f, false);
}
// exe 0x48d470(actor, dist): the heading -- while close wait until 0x280 away; the timer; then mostly toward the
// player, 1 in 64 a +-0x190 turn held 0x10..0x17 ticks
void chooseHeading(const AnimatedActor& a, RollerFields& fields, uint32_t dist) {
    if (fields.flags & kRollerClose) {
        if (dist >= 0x280) fields.flags &= uint8_t(~kRollerClose);
        return;
    }
    if (fields.timer != 0) {
        --fields.timer;
        return;
    }
    const uint32_t r = crtRand();
    if (r & 0x3f) {
        fields.heading = uint16_t(angleOf(player().pos, a.pos)); // exe 0x5e3bc0
        return;
    }
    fields.heading += (r & 0x40) ? 0x190u : 0xfe70u;
    fields.timer = uint16_t(((r >> 7) & 7) + 0x10u);
}
// exe 0x48d500(actor, dist): start giving way: the timer 0x78, the neighbour classified (exe 0x5e5f70), a random side
void startGivingWay(const AnimatedActor& a, RollerFields& fields) {
    fields.timer = 0x78;
    neighbourClassify(a, fields.neighbour);
    fields.neighbour |= uint16_t(crtRand() & 1);
}
// exe 0x48d530(actor, dist): give way to the neighbour by its classification (heading away, slowing down); done when it
// is gone / 0x100 away on both axes or the timer runs out (the speed back to -0x100)
void giveWay(AnimatedActor& a, RollerFields& fields) {
    const Actor& other = neighbourActor(fields.neighbour); // exe 0x5e5cb0
    const uint32_t b = fields.neighbour;
    bool done = false;
    if (b & kNeighbourOpposite)
        fields.heading = uint16_t(headingFromOther(a, other, neighbourSide(fields.neighbour), 0x100)); // exe 0x5e5d90
    if (b & kNeighbourCrossingSlow)
        if (stepTowardLimit(a.velocity.z, 0xfff0, 0x180) != 0) a.velocity.z = -0x180; // exe 0x5e5510
    if (b & kNeighbourCatching)
        if (stepTowardLimit(a.velocity.z, 0xfff0, 0x80) != 0) a.velocity.z = -0x80;
    if (other.flags == 0)
        done = true;
    else if (uint32_t(int(other.pos.x.whole) - a.pos.x.whole + 0x100) > 0x200u &&
             uint32_t(int(other.pos.z.whole) - a.pos.z.whole + 0x100) > 0x200u)
        done = true;
    --fields.timer;
    if (fields.timer == 0 || done) {
        fields.timer = 0;
        fields.flags &= uint8_t(~kRollerClose);
        a.velocity.z = -0x100;
        a.step = 0;
    }
}
// exe 0x48e610(actor, mode): the hit capsule (its box, the previous x / z): small radius 0xf at y - 0x30 .. y - 0xc0
// (the row's attack), large radius 0x18 at y - 0x40 .. y - 0xe4 (the row's strong attack), harmless the large shape
void submitHitCapsule(const AnimatedActor& a, RollerFields& fields, uint32_t mode) {
    fields.hit.previousX = a.prevPos.x.whole;
    fields.hit.previousZ = a.prevPos.z.whole;
    fields.hit.result = 0;
    const int16_t x = a.pos.x.whole, z = a.pos.z.whole, y = a.pos.y.whole;
    int16_t top, bottom;
    int r;
    uint32_t attack, immunity, kind;
    switch (HitMode(mode)) {
    case HitMode::Small:
        bottom = int16_t(y - 0x30);
        top = int16_t(y - 0xc0);
        r = 0x3c;
        kind = kHitAttackSolid;
        attack = row(a).attack;
        immunity = 0x80000000u;
        break;
    case HitMode::Large:
        bottom = int16_t(y - 0x40);
        top = int16_t(y - 0xe4);
        r = 0x60;
        kind = kHitAttackSolid | kHitAttackThrown;
        attack = row(a).strongAttack;
        immunity = 0x80000000u;
        break;
    case HitMode::Harmless:
        bottom = int16_t(y - 0x40);
        top = int16_t(y - 0xe4);
        r = 0x60;
        kind = kHitAttackBump;
        attack = 0;
        immunity = 0xff000000u;
        break;
    default: return;
    }
    const int16_t low[3] = {x, bottom, z}, high[3] = {x, top, z};
    const uint16_t rad = uint16_t(int16_t(r) >> 2);
    hitListSubmitCapsule(high, rad, low, rad, fields.hit, uint32_t(a.slot) << 24 | 0x501000u,
                         attack | immunity | kind); // exe 0x5e4190
}
// the fall of the falling sub-states and of the dying: gravity 0x30 up to 0x2000
void fall(const PlayerEnv& env, AnimatedActor& a, RollerFields& fields) {
    a.velocity.y += 0x30u;
    if (a.velocity.y > 0x2000) a.velocity.y = 0x2000;
    integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
    groundAndWalls(env, a, fields);
}
// the "ground lost -> fall" switch of the sub-states (the fall, animation 0, falling)
template <class Sub> bool toFall(AnimatedActor& a, RollerFields& fields, int r, Sub fallSub) {
    if (r != 1) return false;
    enterSub(a, fallSub);
    a.anim = 0;
    fields.flags |= kRollerFalling;
    return true;
}
void giveUp(AnimatedActor& a, RollerFields& fields) {
    enterSub(a, Form2Sub::ShrinkAway);
    a.anim = 2;
    fields.flags = kRollerNoHits;
}

// ---- form 1 (state 1) sub-states [0x892834] ---------------------------------------------------------------------
// exe 0x48c670 (switch table 0x48c754): pops up (its sound, the y scale up to 0x220 and back to 0x200), waits 0x1e
// ticks, then rolls (a random start key, speed -0x48)
void popUp(AnimatedActor& a, RollerFields& fields) {
    const uint8_t s = a.step;
    switch (s) {
    case 0:
        sound(kSoundPopUp, a);
        a.step = uint8_t(s + 1);
        [[fallthrough]];
    case 1:
        a.scale.y += 0x10u;
        if (a.scale.y >= 0x220) ++a.step;
        break;
    case 2:
        a.scale.y -= 8u;
        if (a.scale.y <= kFullScale) {
            a.scale.y = kFullScale;
            a.step = uint8_t(s + 1);
            fields.timer = 0;
        }
        break;
    case 3:
        ++fields.timer;
        if (fields.timer == 0x1e) a.step = uint8_t(s + 1);
        return;
    case 4:
        enterSub(a, Form1Sub::Roll);
        a.anim = 0;
        a.stepCounter = uint8_t(crtRand() & 0x1f);
        a.velocity.z = -0x48;
        fields.flags = 0;
        fields.humTicks = 0x37;
        return;
    default: return;
    }
    if (dustFrame()) dustPuff(a);
}
// exe 0x48c770: the wobbling roll (its sound on two keys); blocked -> fade out, the ground lost -> fall; far from the
// player for 0x78 ticks -> shrink away
void roll(const PlayerEnv& env, AnimatedActor& a, RollerFields& fields) {
    rollWobbling(a, fields);
    if (a.keyIndex == 0xf || a.keyIndex == 0x25) sound(kSoundRoll, a);
    const int r = groundAndWalls(env, a, fields);
    if (r == 2) {
        enterSub(a, Form1Sub::FadeOut);
        fields.flags = kRollerNoHits;
    } else if (r == 1) {
        enterSub(a, Form1Sub::Fall);
        fields.flags |= kRollerFalling;
        return;
    }
    if (playerDistance(a) >= 0xc00) {
        ++fields.farTicks;
        if (fields.farTicks > 0x78) {
            enterSub(a, Form1Sub::ShrinkAway);
            fields.flags = kRollerNoHits;
        }
        return;
    }
    fields.farTicks = 0;
}
// exe 0x48c820: idle until the player is 0xcd away, then rolls
void idle(const PlayerEnv& env, AnimatedActor& a, RollerFields& fields) {
    if (playerDistance(a) > 0xcd) {
        enterSub(a, Form1Sub::Roll);
        a.anim = 0;
    }
    toFall(a, fields, groundAndWalls(env, a, fields), Form1Sub::Fall);
}
// exe 0x48c880: the fall; landed, the morph (animation 3) or the roll
void fallForm1(const PlayerEnv& env, AnimatedActor& a, RollerFields& fields) {
    fall(env, a, fields);
    if (a.airborne != 0) return;
    a.velocity.y = 0;
    if (a.anim == 3) {
        enterSub(a, Form1Sub::Morph);
        return;
    }
    enterSub(a, Form1Sub::Roll);
    fields.flags &= uint8_t(~kRollerFalling);
}
// exe 0x48c8f0: the morph -- no hits, animation 4 once the current one ended, 0x28 ticks later its sound and the
// rebind (state 2); dust sparks every frame with bit 2 (the spark of 0x48d3e0 inlined, y spread 0xff)
void morph(AnimatedActor& a, RollerFields& fields) {
    switch (a.step) {
    case 0:
        fields.flags = kRollerNoHits;
        ++a.step;
        break;
    case 1:
        if (a.animEnded()) {
            a.anim = 4;
            a.step = 2;
            fields.timer = 0;
        }
        break;
    case 2:
        ++fields.timer;
        if (fields.timer == 0x28) {
            sound(kSoundMorph, a);
            fields.humTicks = uint8_t((rngStep() & 0x1f) + 0x3cu);
            a.enterState(uint8_t(RollerState::MorphBind));
        }
        break;
    default: break;
    }
    if (!dustFrame()) return;
    spawnDustSpark(a, 0xff, true);
}
// exe 0x48ca20 / 0x48d000 (the same code): its sound, the y scale down to 0, then freed
void shrinkAway(AnimatedActor& a) {
    switch (a.step) {
    case 0:
        sound(kSoundShrink, a);
        ++a.step;
        [[fallthrough]];
    case 1:
        a.scale.y -= 0x10u;
        if (a.scale.y <= 0) {
            a.scale.y = 0;
            a.enterState(uint8_t(RollerState::Free));
        }
        if (dustFrame()) dustPuff(a);
        return;
    default: return;
    }
}
// exe 0x48ca90: rolling on, additive and without its shadow, its tint fading over 0x10 ticks, then hidden and freed
void fadeOut(AnimatedActor& a, RollerFields& fields) {
    rollWobbling(a, fields);
    switch (a.step) {
    case 0:
        a.semiTransparency = semiTransparencyBits(1, 1); // exe 0x5e1a00(a, 1, 1)
        fields.flags |= kRollerNoShadow;
        fields.timer = 0x10;
        ++a.step;
        break;
    case 1:
        --fields.timer;
        if (fields.timer == 0) {
            a.enterState(uint8_t(RollerState::Free));
            a.flags &= uint8_t(~kActorDrawn);
        }
        break;
    default: break;
    }
    const uint32_t v = fields.timer;
    a.tint = uint16_t((((v << 5) | v) << 5) | v);
}
// exe 0x48cb20: hidden until the player is within 0x400, then pops up
void hidden(AnimatedActor& a) {
    a.flags &= uint8_t(~kActorDrawn);
    if (playerDistance(a) <= 0x400) {
        a.subState = uint8_t(Form1Sub::PopUp);
        a.flags |= kActorDrawn;
    }
}
void form1Sub(const PlayerEnv& env, AnimatedActor& a, RollerFields& fields) {
    switch (Form1Sub(a.subState)) { // table 0x892834
    case Form1Sub::PopUp: popUp(a, fields); return;
    case Form1Sub::Roll: roll(env, a, fields); return;
    case Form1Sub::Idle: idle(env, a, fields); return;
    case Form1Sub::Fall: fallForm1(env, a, fields); return;
    case Form1Sub::Morph: morph(a, fields); return;
    case Form1Sub::ShrinkAway: shrinkAway(a); return;
    case Form1Sub::FadeOut: fadeOut(a, fields); return;
    case Form1Sub::Hidden: hidden(a); return;
    default: notPortedAt(kStoryPort, "0x48c330 sub-state (table 0x892834 runs into 0x892854)", a.subState);
    }
}

// ---- form 2 (state 3) sub-states [0x892854] ---------------------------------------------------------------------
// exe 0x48cb50: grows from the morph's scale over 0x11 ticks to 0x200, then chases (a random start key, speed -0x100)
void grow(AnimatedActor& a, RollerFields& fields) {
    a.scale.x += 0xau;
    a.scale.z = a.scale.x;
    a.scale.y = a.scale.x;
    ++fields.timer;
    if (fields.timer == 0x11) {
        a.scale.z = kFullScale;
        a.scale.y = kFullScale;
        a.scale.x = kFullScale;
        enterSub(a, Form2Sub::Chase);
        const uint32_t r = crtRand();
        a.anim = 0;
        a.stepCounter = uint8_t(r & 0x1f);
        a.velocity.z = -0x100;
        fields.flags = 0;
        fields.timer = 0;
        fields.farTicks = 0;
    }
    if (dustFrame()) dustPuff(a);
}
// the far-away count of the chase and the wander: 0x78 ticks beyond 0xc00 -> give up
void countFarTicks(AnimatedActor& a, RollerFields& fields, uint32_t dist) {
    if (dist >= 0xc00) {
        ++fields.farTicks;
        if (fields.farTicks > 0x78) giveUp(a, fields);
    } else {
        fields.farTicks = 0;
    }
}
// exe 0x48cbf0: the chase -- the heading (giving way to a neighbour), turned to and moved along; a wall bounces the
// heading (exe 0x410740 on the scratch rotation), a touch -> hit back; close: the close flag; the lifetime -> give up
void chase(const PlayerEnv& env, AnimatedActor& a, RollerFields& fields) {
    const uint32_t dist = playerDistance(a);
    switch (a.step) {
    case 0:
        chooseHeading(a, fields, dist);
        if (neighbourFind(a, fields.neighbour, 0) != 0) ++a.step; // exe 0x5e5e00
        break;
    case 1:
        startGivingWay(a, fields);
        ++a.step;
        [[fallthrough]];
    case 2: giveWay(a, fields); break;
    default: break;
    }
    if (((a.keyIndex - 2u) & 7) == 0) sound(kSoundChase, a);
    turnYawTo(a.rotation.y, fields.heading, a.spin.y); // exe 0x5e3df0
    integrateVelocity(a, 0, 0, a.velocity.z);
    const int r = groundAndWalls(env, a, fields);
    if (toFall(a, fields, r, Form2Sub::Fall)) {
    } else if (r == 2) {
        if (fields.timer == 0) {
            LaneVec4& wallRotation = projectionScratch().point;
            wallRotation.z = 0;
            wallRotation.x = 0;
            wallRotation.y = a.rotation.y;
            reflectRotationOffWalls(wallRotation, fields.pushResult); // exe 0x410740
            fields.heading = uint16_t(wallRotation.y);
            fields.timer = uint16_t((crtRand() & 0xf) + 0x10u);
        }
    } else {
        if (fields.hit.result & kHitAttackBump) {
            enterSub(a, Form2Sub::HitBack);
            a.anim = 1;
            a.velocity.z = 0x80;
            a.rotation.y = int16_t(uint32_t(angleOf(player().pos, a.pos))); // exe 0x5e3bc0
        }
        countFarTicks(a, fields, dist);
    }
    if (dist < 0x40) fields.flags |= kRollerClose;
    if (fields.lifetime == kRollerLifetime) giveUp(a, fields);
}
// exe 0x48cdc0: pushed back by the touch; at the animation's end it wanders along its yaw
void hitBack(const PlayerEnv& env, AnimatedActor& a, RollerFields& fields) {
    integrateVelocity(a, 0, 0, a.velocity.z);
    if (a.animEnded()) {
        enterSub(a, Form2Sub::Wander);
        a.anim = 0;
        a.velocity.z = -0x100;
        fields.heading = uint16_t(a.rotation.y);
        fields.timer = 0;
    }
    toFall(a, fields, groundAndWalls(env, a, fields), Form2Sub::Fall);
}
// exe 0x48ce40: the fall; landed, the chase
void fallForm2(const PlayerEnv& env, AnimatedActor& a, RollerFields& fields) {
    fall(env, a, fields);
    if (a.airborne != 0) return;
    enterSub(a, Form2Sub::Chase);
    a.velocity.y = 0;
    fields.flags &= uint8_t(~kRollerFalling);
}
// exe 0x48cea0: the flinch's animation, then the chase
void flinch(const PlayerEnv& env, AnimatedActor& a, RollerFields& fields) {
    if (a.animEnded()) {
        a.anim = 0;
        enterSub(a, Form2Sub::Chase);
    }
    toFall(a, fields, groundAndWalls(env, a, fields), Form2Sub::Fall);
}
// exe 0x48cef0: the death explosion (global static type 8, 0x100 above it); no hits from 0xf ticks, freed with its
// drops at 0x20
void die(const PlayerEnv& env, AnimatedActor& a, RollerFields& fields) {
    if (a.airborne != 0) {
        fall(env, a, fields);
    } else {
        fields.flags &= uint8_t(~kRollerFalling);
        a.velocity.y = 0;
    }
    if (a.step == 0) {
        if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300
            StaticActor& e = staticActorAt(address);
            ExplosionParams params(e);
            e.flags = kActorLive;
            e.loopFlags |= kActorSweepGroup2;
            e.type = typeByte(GlobalStaticType::Explosion);
            e.variant = 0;
            e.pos.x.whole = a.pos.x.whole;
            e.pos.y.whole = int16_t(uint16_t(a.pos.y.whole) - 0x100u);
            e.pos.z.whole = a.pos.z.whole;
            params.spread = 0x80;
            params.flashes = 0x20;
        }
        fields.timer = 0;
        ++a.step;
        return;
    }
    ++fields.timer;
    const uint16_t t = fields.timer;
    if (t == 0x20) {
        fields.timer = 0;
        a.enterState(uint8_t(RollerState::Free));
        spawnDrops(a.pos, row(a).dropTable); // exe 0x5f1270
        return;
    }
    if (t == 0xf) fields.flags |= kRollerNoHits;
}
// exe 0x48d070: wanders along its heading (a wall: a quarter turn aside once faced), a touch -> hit back; near the
// player for 0x5a ticks (or at once when far) it mostly chases again, else gives up
void wander(const PlayerEnv& env, AnimatedActor& a, RollerFields& fields) {
    turnYawTo(a.rotation.y, fields.heading, a.spin.y); // exe 0x5e3df0
    integrateVelocity(a, 0, 0, a.velocity.z);
    const uint32_t dist = playerDistance(a);
    const int r = groundAndWalls(env, a, fields);
    if (toFall(a, fields, r, Form2Sub::Fall)) {
    } else if (r == 2) {
        if (int32_t(a.rotation.y) == int32_t(uint16_t(fields.heading)))
            fields.heading += (crtRand() & 1) ? 0xfe00u : 0x200u;
    } else {
        if (fields.hit.result & kHitAttackBump) {
            enterSub(a, Form2Sub::HitBack);
            a.anim = 1;
            a.velocity.z = 0x80;
        }
        countFarTicks(a, fields, dist);
    }
    if (dist < 0x280) {
        ++fields.timer;
        if (int16_t(fields.timer) <= 0x5a) return;
    }
    if (crtRand() & 3) {
        a.anim = 0;
        enterSub(a, Form2Sub::Chase);
        fields.timer = 0;
        return;
    }
    giveUp(a, fields);
}
void form2Sub(const PlayerEnv& env, AnimatedActor& a, RollerFields& fields) {
    switch (Form2Sub(a.subState)) { // table 0x892854
    case Form2Sub::Grow: grow(a, fields); return;
    case Form2Sub::Chase: chase(env, a, fields); return;
    case Form2Sub::HitBack: hitBack(env, a, fields); return;
    case Form2Sub::Fall: fallForm2(env, a, fields); return;
    case Form2Sub::Flinch: flinch(env, a, fields); return;
    case Form2Sub::Die: die(env, a, fields); return;
    case Form2Sub::ShrinkAway: shrinkAway(a); return; // exe 0x48d000
    case Form2Sub::Wander: wander(env, a, fields); return;
    default: notPortedAt(kStoryPort, "0x48c520 sub-state (table 0x892854 runs into data 0x892874)", a.subState);
    }
}

// the animation at the end of the forms: a new one started at the start key (then cleared), else stepped
void animate(AnimatedActor& a) {
    if (a.anim == a.prevAnim) {
        animStepPose(a); // exe 0x5e1e80
        return;
    }
    animSetPose(a, a.anim, a.stepCounter); // exe 0x5e1c30
    a.stepCounter = 0;
}
// the hum of the forms: a countdown, at 0 its sound and a new count
void hum(AnimatedActor& a, RollerFields& fields, uint16_t id, uint8_t next) {
    --fields.humTicks;
    if (fields.humTicks == 0) {
        fields.humTicks = next;
        sound(id, a);
    }
}

// ---- states [0x892820] ------------------------------------------------------------------------------------------
// exe 0x48c1f0: model variant 0 (none: freed), shown at y scale 0, animation 1 at a random key, the hit points of the
// class row, the box 0x892878, the class block cleared, no hits; variant 0 joins its encounter group; registered as an
// enemy, on the ground; variant 2 starts hidden; kept through a warp
void bind(const PlayerEnv& env, AnimatedActor& a, RollerFields& fields) {
    if (!bindActorModel(a, 0)) { // exe 0x5e0ba0
        freeActor(a);            // exe 0x5e1490
        return;
    }
    a.flags |= kActorDrawn;
    a.scale.y = 0;
    a.anim = 1;
    a.prevAnim = 1;
    animSetPose(a, a.anim, int(crtRand() & 0x1f)); // exe 0x5e1c30
    const int16_t hp = hasAnimatedStats(a.type) ? int16_t(animatedStats(a.type, a.spawnByte).hitPoints) : 0;
    a.maxHitPoints = hp;
    a.hitPoints = hp;
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.velocity.setZSpare(0, 0);
    a.velocity.setXY(0, 0);
    a.spin.x = 0;
    a.spin.y = 0x40;
    a.collisionBox.pointAt(TableId::St10ClsBox);
    a.interactionBox = nullptr;
    a.interaction = 0;
    a.actorHit = 0;
    a.onPlatform = 0;
    a.airborne = 0;
    a.platform = nullptr;
    a.clearClassBlock(); // exe 0x40dc40
    fields.heading = uint16_t(a.rotation.y);
    fields.flags = kRollerNoHits;
    if (a.variant == 0) fields.groupIndex = uint8_t(enemyGroupFindFor(a, 0)); // exe 0x5e6cd0(a, 0)
    fields.lifetime = 0;
    registerEnemy(a.slot, 0); // exe 0x5e5c10
    a.pos.y = int32_t(uint32_t(actorGround(env, a)) << 16); // exe 0x40f190
    a.enterState(uint8_t(RollerState::Form1));
    if (a.variant == 2) {
        a.subState = uint8_t(Form1Sub::Hidden);
        fields.flags |= kRollerNoShadow;
    }
    a.runFlags |= kActorWarpKept;
}
// exe 0x48c330: form 1 -- before the morph a hit, a touch or its group's marks (variant 0) start the morph (animation
// 3) and give it its group bit; the sub-state; its hum; the hit capsule; the animation
void form1(const PlayerEnv& env, AnimatedActor& a, RollerFields& fields) {
    if (fields.groupBits == 0) {
        bool triggered = false;
        if (a.variant == 0 && enemyGroupMarks(int32_t(fields.groupIndex), EnemyGroupMarks::Either) != 0)
            triggered = true; // exe 0x5e6d60
        bool morphs;
        if (fields.flags & kRollerNoHits)
            morphs = triggered;
        else
            morphs = takeHit(a, fields) != 0 || (fields.hit.result & kHitAttackBump) != 0 || triggered;
        if (morphs) {
            a.anim = 3;
            enterSub(a, Form1Sub::Morph);
            fields.groupBits = uint8_t(1u << (uint32_t(int32_t(fields.groupBit.get())) & 0x1f));
        }
    }
    form1Sub(env, a, fields);
    if (fields.groupBits == 0 && Form1Sub(a.subState) != Form1Sub::Morph) hum(a, fields, kSoundHum, 0x37);
    if (!(fields.flags & kRollerNoHits)) submitHitCapsule(a, fields, fields.hitMode);
    animate(a);
    // exe 0x5f20f0(a, 0x40, 0x100, 0) unless kRollerNoShadow: the shadow (render output)
}
// exe 0x48c460: the bone blocks the exe reads it as owning released (exe 0x5e1760), model variant 1 at scale 0x156,
// the box 0x892878, animation 2 at a random key, the hits reset, hit mode large, form 2
void morphBind(AnimatedActor& a, RollerFields& fields) {
    actorSystem().boneBlocksUsed &= ~uint32_t(a.boneBlocks); // exe 0x5e1760
    if (!bindActorModel(a, 1)) { // exe 0x5e0bc0(a, &local byte 1)
        freeActor(a);            // exe 0x5e1490
        return;
    }
    a.scale.z = kMorphScale;
    a.scale.y = kMorphScale;
    a.scale.x = kMorphScale;
    a.collisionBox.pointAt(TableId::St10ClsBox);
    a.interactionBox = nullptr;
    a.interaction = 0;
    a.anim = 2;
    a.prevAnim = 2;
    animSetPose(a, a.anim, int(crtRand() & 0xf)); // exe 0x5e1c30
    fields.hitCount = 0;
    fields.hitMode = uint8_t(HitMode::Large);
    fields.timer = 0;
    fields.hit.result = 0;
    a.enterState(uint8_t(RollerState::Form2));
    // exe 0x5f20f0(a, 0x60, 0x100, 0): the shadow (render output)
}
// exe 0x48c520: form 2 -- its lifetime, the hits (destroyed -> die, harmless; a flinch on the ground), the sub-state,
// its growl, the hit capsule, the animation
void form2(const PlayerEnv& env, AnimatedActor& a, RollerFields& fields) {
    if (fields.lifetime != kRollerLifetime) ++fields.lifetime;
    if (!(fields.flags & kRollerNoHits)) {
        if (takeHit(a, fields) == -1) {
            enterSub(a, Form2Sub::Die);
            a.anim = 4;
            a.velocity.z = 0;
            fields.flags = 0;
            fields.timer = 0;
            ++fields.hitMode;
        } else if ((fields.flags & (kRollerFalling | kRollerFlinch)) == kRollerFlinch) {
            enterSub(a, Form2Sub::Flinch);
            a.anim = 3;
        }
    }
    form2Sub(env, a, fields);
    if (Form2Sub(a.subState) != Form2Sub::Die) {
        --fields.humTicks;
        if (fields.humTicks == 0) {
            fields.humTicks = uint8_t((rngStep() & 0x3f) + 0x6au);
            sound(kSoundGrowl, a);
        }
    }
    if (!(fields.flags & kRollerNoHits)) submitHitCapsule(a, fields, fields.hitMode);
    animate(a);
    // exe 0x5f20f0(a, 0x60, 0x100, 0): the shadow (render output)
}

// exe 0x48c160
void roller(const PlayerEnv& env, AnimatedActor& a) {
    RollerFields& fields = classState<RollerFields>(a);
    a.prevAnim = a.anim;
    a.tint = kTintNeutral;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (RollerState(a.state)) { // table 0x892820
    case RollerState::Bind: bind(env, a, fields); break;
    case RollerState::Form1: form1(env, a, fields); break;
    case RollerState::MorphBind: morphBind(a, fields); break;
    case RollerState::Form2: form2(env, a, fields); break;
    case RollerState::Free: freeActor(a); break; // exe 0x468fa0: 0x5e1490
    default: notPortedAt(kStoryPort, "0x48c160 state (table 0x892820 runs into the sub table 0x892834)", a.state);
    }
    if (fields.hitCount < 0) ++fields.hitCount;
    if (a.variant == 0) enemyGroupMark(int32_t(fields.groupIndex), fields.groupBits); // exe 0x5e6d40
}

} // namespace

void st10RollerUpdate(const PlayerEnv& env, uint32_t actor) { roller(env, animatedActorAt(actor)); }

} // namespace od::port
