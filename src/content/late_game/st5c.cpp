// ST5C (the stage case 0x58cb10 of content/late_game/end_scripts: the record 0x8cd8c0 at the stage start, script 0x75
// on flag 0x5b0, script 0x6d on flag 0x680). Classes of the ST5C tables (stage setup 0x5dff60: pool 0x20 [0x9192bc],
// static [0x919514], pool 0x40 [0x9196b0], pool 0xa0 [0x919780]). Names are hypotheses from the observed behaviour.
//
// The boss (pool 0x20 type 0x83 sub 0, exe 0x50b9c0, record 0x8cd8c0). States ([0x898324]): 0 bind 0x50ba80 (hit points
// [row] << 7, box 0x898308, y -0x3c0, the boss gauge 0x415d30(1, 7, white)), 1 run 0x50bb30 (its attack (the sub-state)
// on [0x898330], then the hit list from bones 0 / 7 / 8 and capsules 2-3 / 5-6 while the attack left the hit flag),
// 2 dead 0x50bd30 (sound 0x2dc; at key 0x30 the flag 0x680 that starts script 0x6d). The attacks call 0x50d1a0 (the
// damage: at most 0x20 a tick, the rest queued; dead -> state 2) except 4 / 5:
//   0 0x50bd90 face the player then pick (half the hit points lost and no cooldown -> 10; near (< 0xc00) and 1 in 4 ->
//     4 / 5; else by the RNG & 7: 7 / 3 / 1 / 1 / 7 / 3 or 9 / 1 / 9), 1 0x50bf30 rise then dash forward (to the centre
//     or 0x80 ticks), maybe -> 8, else a second dash at the player -> 6, 2 0x50c130 dash out from the centre and land,
//     3 0x50c250 rise, dash at the player (speed by the difficulty), dash out, land, 4 / 5 0x50c420 the circling
//     attack (three after-images 0x50d3d0 trail its yaw / animation, the yaw around the room centre at radius 0xc00,
//     clockwise / counter-clockwise), 6 0x50c740 rise high and spin out 15 blades (0x5c16b0), 7 0x50c9f0 two beam
//     charges (0x5be190) then turn toward the player until they report, 8 0x50cbd0 jump and stomp (0x50d270: shake,
//     and when the ground is below two shock waves + 2 x 3 stomp shots), 9 0x50cd30 rocks (0x50ee90) around the
//     player, 10 0x50cfb0 two ball charges (0x5bb6e0) and a ball (0x5d1a10), cooldown 0x180.
// Its block from +0x14c: the hit box, two timers, the cooldown, the flags (1 the shadow, 2 the hit list), the queued
// damage, the three after-images.
// The after-image (type 0x83 sub 2, exe 0x50d3d0; [0x89835c]: 0 bind 0x50d420, 1 0x50d460 placed on the circle by its
// yaw; the boss copies its yaw / animation each tick).
// The intro cast (type 0x83 sub 1, exe 0x50d4a0, the cast 0x8ae5a0 of script 0x75; variant table [0x898368]): fades in
// bone by bone (0x50d8a0), sparks around it (0x50d9d0: spark bursts at rates by the step tick), face cells by the tick.
// The defeat cast (type 0x83 sub 3, exe 0x50dae0, the cast 0x8ad590 of script 0x6d): lifted 0x12c then dropped (shake
// / vibration on landing), face cells keyed to the step tick of step 1.
// The spark burst (static type 0x64, exe 0x5bfcf0): a burst of up to 16 sparks stored in the actor (s16 x / y / z /
// speed, a size byte each); its kind from its spawn byte: init 0x5bfd10 (0..4 -> states 1..5) or 0x5c0d40 (0..2 ->
// states 7..9, set by a spawner writing state 6); every state moves each spark out from the actor along its pitch /
// yaw and writes draw records (rings, billboards) to the draw buffer.
// The dust effect (pool 0xa0 type 0x24, exe 0x43ff80, [0x88d998] by its variant): variant 2 0x440940 = four dust puffs
// (billboards) drifting along a turning yaw; variants 0 / 1 (content/late_game/end_cast) and 3 / 4
// (content/late_game/st5c_fx2) elsewhere.
//
// Render output not ported: the shadow exe 0x5f20f0 (the boss) / 0x5f2220 (the intro cast). The models go through the
// standard +0xa4 path; the sparks / puffs through dc::emit.
#include "content/late_game/st5c.hpp"
#include "content/late_game/st5c_state.hpp"

#include "content/late_game/end_cast.hpp"
#include "content/late_game/st5c_fx2.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/draw_commands.hpp"
#include "game/field_alias.hpp"
#include "game/game_state.hpp"
#include "game/geometry_scratch.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/vectors.hpp"

namespace od::port {

using namespace st5c_state;

namespace {

// the room centre (16.16; the exe's local (0, 0, 0))
const int32_t kRoomCentre[3] = {0, 0, 0};
void positionOf(const Actor& actor, int32_t p[3]) {
    p[0] = actor.pos.x;
    p[1] = actor.pos.y;
    p[2] = actor.pos.z;
}
// exe 0x5e1810: bone n's point (a null result would crash the exe)
ShortVec4& bonePointOf(const AnimatedActor& actor, uint32_t n) {
    BoneBlock* block = boneBlockOf(actor, int(n >> 2));
    if (!block) notPortedAt(kStoryPort, "st5c: exe 0x5e1810 returned null, bone", n);
    return block->points[n & 3];
}
BoneColour boneColourOf(const AnimatedActor& actor, uint32_t n) {
    BoneBlock* block = boneBlockOf(actor, int(n >> 2));
    if (!block) notPortedAt(kStoryPort, "st5c: exe 0x5e1810 returned null, bone", n);
    return block->colour(n & 3);
}
void pointOf(const ShortVec4& p, int16_t o[3]) {
    o[0] = p.x;
    o[1] = p.y;
    o[2] = p.z;
}
// exe 0x5e1970 on the local vector (0, y, 0)
void bonePointUp(const AnimatedActor& actor, uint32_t bone, int y, int16_t out[3]) {
    const int16_t v[3] = {0, int16_t(y), 0};
    bonePointOffset(actor, bone, v, out);
}
// exe 0x5e1a00(actor, on, mode): the draw's semi-transparency bits
void blend(AnimatedActor& actor, int on, int mode) { actor.semiTransparency = semiTransparencyBits(uint32_t(on), uint32_t(mode)); }

// ---- the boss (pool 0x20 type 0x83 sub 0, exe 0x50b9c0) -------------------------------------------------------------
enum class BossState : uint8_t { Bind = 0, Run = 1, Dead = 2 };
enum class BossAttack : uint8_t {
    Pick = 0, DashForward = 1, DashOut = 2, DashAtPlayer = 3, CircleRight = 4, CircleLeft = 5, Blades = 6,
    BeamCharges = 7, Stomp = 8, Rocks = 9, BallCharge = 10, AfterImageBind = 11, AfterImageOrbit = 12,
};
constexpr uint8_t kBossType = 0x83, kAfterImageSub = 2;
constexpr uint8_t kBladeType = 0x66, kBeamChargeType = 0x60, kBallChargeType = 0x5b, kRockType = 0x86;
constexpr uint8_t kShockWaveType = 3, kStompShotType = 0x32, kChargeBallType = 0x33;
constexpr uint32_t kFlagBossDown = 0x680;
constexpr uint8_t kBossShadow = 0x01, kBossHitList = 0x02; // the flags' bits
constexpr uint8_t kAfterImageFlags = kActorLiveDrawn | kActorMeshModel;
constexpr uint8_t kRockFlags = kActorLiveDrawn | kActorMeshModel;


// exe 0x50d0e0(actor, y, speed): the height toward y by speed << 12 (snaps within a step)
void riseTo(AnimatedActor& actor, uint32_t y, uint32_t speed) {
    const uint32_t s = speed << 12;
    const uint32_t c = uint32_t(actor.pos.y);
    const int32_t d = int32_t(c - y);
    if (d == 0) return;
    const uint32_t ad = uint32_t(d < 0 ? -d : d);
    if (int32_t(ad) < int32_t(s)) actor.pos.y = int32_t(y);
    else if (d > 0) actor.pos.y = int32_t(c - s);
    else actor.pos.y = int32_t(c + s);
}
// exe 0x50d130(actor, step): the yaw toward the angle of the actor seen from the player (snap within step)
void turnFromPlayer(AnimatedActor& actor, int step) {
    const int ang = angleOf(player().pos, actor.pos);
    const int d = angleDiff(uint32_t(ang), uint32_t(int32_t(actor.rotation.y)));
    const int ad = d < 0 ? -d : d;
    if (step > ad) actor.rotation.y = int16_t(ang);
    else if (d > 0) actor.rotation.y = int16_t(uint16_t(actor.rotation.y) - uint32_t(step));
    else actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + uint32_t(step));
}
// exe 0x50d1a0(actor, block): a new hit (a player's attack): flash, sound 0x8d, at most 0x20 now and the rest queued;
// else the queue drained by 0x20 a tick. -1 = dead, 1 = damaged, 0 = nothing
int takeDamage(AnimatedActor& actor, BossFields& boss) {
    const uint32_t h = boss.hitBox.result;
    uint32_t d = h & kHitDamageMask;
    if ((h & kHitAttackByPlayer) && d != 0) {
        actor.tint = kTintFlash;
        soundPlayAtPos(0x8d, actor.pos);
        if (int32_t(d) > 0x20) {
            boss.queuedDamage = uint16_t(d - 0x20);
            d = 0x20;
        }
        return hitPointsAdd(actor, -int(d)) ? -1 : 1; // exe 0x5e52f0
    }
    const uint16_t q = boss.queuedDamage;
    if (q == 0) return 0;
    if (q > 0x20) {
        boss.queuedDamage = uint16_t(q - 0x20u);
        d = 0x20;
    } else {
        d = q;
        boss.queuedDamage = 0;
    }
    return hitPointsAdd(actor, -int(d)) ? -1 : 1;
}
// the common tail of the attacks (but 4 / 5): dead -> state 2 (the shadow), else the shadow and the hit list
void damageTail(AnimatedActor& actor, BossFields& boss) {
    if (takeDamage(actor, boss) < 0) {
        actor.enterState(uint8_t(BossState::Dead));
        boss.flagsLow |= kBossShadow;
        return;
    }
    boss.flagsLow |= uint8_t(kBossShadow | kBossHitList);
}
void setAnim(AnimatedActor& actor, uint8_t n) {
    actor.anim = n;
    actor.animKey.frame = 0;
}
void nextAttack(AnimatedActor& actor, BossAttack next) {
    actor.subState = uint8_t(next);
    actor.restartStep();
}
void nextStep(AnimatedActor& actor) { ++actor.step; }
bool animEnd(const AnimatedActor& actor) { return actor.animKey.flags == kKeyHold; }
void dashForward(AnimatedActor& actor) { integrateVelocity(actor, 0, 0, actor.velocity.z); } // exe 0x5e3690
uint32_t distanceToPlayer(const AnimatedActor& actor) { return uint32_t(planarDistance(actor.pos, player().pos)); }
uint32_t distanceToCentre(const AnimatedActor& actor) { return uint32_t(planarDistance(actor.pos, kRoomCentre)); }
bool halfHitPointsLost(const AnimatedActor& actor) { return actor.hitPoints < int16_t(actor.maxHitPoints >> 1); }

// exe 0x50bd90: face the player, then pick the next attack
void pickAttack(AnimatedActor& actor, BossFields& boss) {
    turnFromPlayer(actor, 0x20);
    switch (actor.step) {
    case 0:
        setAnim(actor, 0);
        nextStep(actor);
        break;
    case 1:
        if (actor.keyIndex == 0) nextStep(actor);
        break;
    case 2:
        if (int(actor.rotation.y) == angleOf(player().pos, actor.pos)) nextStep(actor);
        break;
    case 3: {
        if (halfHitPointsLost(actor) && boss.cooldown == 0) {
            actor.subState = uint8_t(BossAttack::BallCharge);
        } else {
            bool picked = false;
            if (planarDistance(actor.pos, player().pos) < 0xc00 && boss.cooldown == 0 && (rngStep() & 3) == 0) {
                actor.subState = uint8_t((rngStep() & 1) + 4u);
                picked = true;
            }
            if (!picked) {
                switch (rngStep() & 7) { // [0x50bf10]
                case 0: case 4: actor.subState = uint8_t(BossAttack::BeamCharges); break;
                case 1: actor.subState = uint8_t(BossAttack::DashAtPlayer); break;
                case 2: case 3: case 6: actor.subState = uint8_t(BossAttack::DashForward); break;
                case 5:
                    actor.subState = uint8_t(halfHitPointsLost(actor) ? BossAttack::Rocks : BossAttack::DashAtPlayer);
                    break;
                default: actor.subState = uint8_t(BossAttack::Rocks); break;
                }
            }
        }
        actor.restartStep();
        break;
    }
    default: break;
    }
    damageTail(actor, boss);
}
// exe 0x50bf30: rise, dash forward (to the centre or 0x80 ticks); facing the player within 0x140 (3 in 4) -> land
// (stomp), else a dash at the player (0x80 ticks or within 0x200) -> the blades
void dashForwardAttack(AnimatedActor& actor, BossFields& boss) {
    turnFromPlayer(actor, 0x30);
    switch (actor.step) {
    case 0:
        setAnim(actor, 0x19);
        nextStep(actor);
        break;
    case 1:
        riseTo(actor, 0xfcc00000u, 0x200);
        if (animEnd(actor)) {
            setAnim(actor, 0x1a);
            actor.velocity.z = int16_t(0xfd00);
            boss.timerA = 0x80;
            nextStep(actor);
        }
        break;
    case 2: {
        dashForward(actor);
        if (distanceToCentre(actor) >= 0x800) {
            boss.timerA -= 1u;
            if (boss.timerA != 0) break;
        }
        int32_t p[3];
        positionOf(actor, p);
        const int y1 = int16_t(angleOf(p[0], p[2], kRoomCentre[0], kRoomCentre[2]));
        const int y2 = int16_t(angleOf(actor.pos, player().pos));
        const int d = angleDiff(uint32_t(y1), uint32_t(y2));
        if ((d < 0 ? -d : d) < 0x140 && (rngStep() & 3) != 0) {
            setAnim(actor, 0x1b);
            nextStep(actor);
        } else {
            boss.timerA = 0x80;
            actor.step += 2u;
        }
        break;
    }
    case 3:
        if (animEnd(actor)) nextAttack(actor, BossAttack::Stomp);
        break;
    case 4:
        dashForward(actor);
        if (distanceToPlayer(actor) >= 0x200) {
            boss.timerA -= 1u;
            if (boss.timerA != 0) break;
        }
        setAnim(actor, 0x1b);
        nextStep(actor);
        break;
    case 5:
        if (animEnd(actor)) nextAttack(actor, BossAttack::Blades);
        break;
    default: break;
    }
    damageTail(actor, boss);
}
// exe 0x50c130: dash out past 0xc00 from the centre, then land
void dashOutAttack(AnimatedActor& actor, BossFields& boss) {
    turnFromPlayer(actor, 0x80);
    switch (actor.step) {
    case 0:
        setAnim(actor, 0x1c);
        nextStep(actor);
        break;
    case 1:
        if (animEnd(actor)) {
            setAnim(actor, 0x1d);
            actor.velocity.z = 0x600;
            nextStep(actor);
        }
        break;
    case 2:
        dashForward(actor);
        if (distanceToCentre(actor) > 0xc00) {
            setAnim(actor, 0x1e);
            nextStep(actor);
        }
        break;
    case 3:
        riseTo(actor, 0xfc400000u, 0x100);
        if (animEnd(actor)) nextAttack(actor, BossAttack::Pick);
        break;
    default: break;
    }
    damageTail(actor, boss);
}
// exe 0x50c250: rise, dash at the player (speed by the difficulty) for 0xc0 ticks or until within 0x100, dash out, land
void dashAtPlayerAttack(AnimatedActor& actor, BossFields& boss) {
    switch (actor.step) {
    case 0:
        setAnim(actor, 0x19);
        soundPlayAtPos(0x2d6, actor.pos);
        nextStep(actor);
        break;
    case 1:
        turnFromPlayer(actor, 0x20);
        riseTo(actor, 0xfcc00000u, 0x200);
        if (animEnd(actor)) {
            setAnim(actor, 0x1a);
            const uint8_t s = gameState().difficulty;
            actor.velocity.z = int16_t(s < 2 ? 0xfd00u : s < 3 ? 0xfc80u : 0xfc00u);
            boss.timerA = 0xc0;
            nextStep(actor);
        }
        break;
    case 2:
        turnFromPlayer(actor, 0x20);
        dashForward(actor);
        if (distanceToPlayer(actor) >= 0x100) {
            boss.timerA -= 1u;
            if (boss.timerA != 0) break;
        }
        nextStep(actor);
        break;
    case 3:
        dashForward(actor);
        if (distanceToCentre(actor) > 0xc00) {
            setAnim(actor, 0x1b);
            nextStep(actor);
        }
        break;
    case 4:
        turnFromPlayer(actor, 0x80);
        riseTo(actor, 0xfc400000u, 0x100);
        if (animEnd(actor)) {
            actor.pos.y = int32_t(0xfc400000u);
            nextAttack(actor, BossAttack::Pick);
        }
        break;
    default: break;
    }
    damageTail(actor, boss);
}
// the boss's position on the circle of radius 0xc00 at its yaw
void placeOnCircle(AnimatedActor& actor) {
    const uint32_t i = uint16_t(actor.rotation.y) & 0xfffu;
    actor.pos.x = int32_t(uint32_t(fixedSin(int(i)) * 3) << 14);
    actor.pos.z = int32_t(uint32_t(fixedCos(int(i)) * 3) << 14);
}
// exe 0x50c420 (attacks 4 / 5): around the room centre with three after-images; no damage (no 0x50d1a0)
void circleAttack(AnimatedActor& actor, BossFields& boss) {
    const uint32_t f = BossAttack(actor.subState) != BossAttack::CircleRight ? 1u : 0u;
    auto soundCheck = [&actor] { // the yaw against the player's: sound 0x2cf while within the turn speed
        const int c = actor.rotation.y, p = player().rotation.y;
        const int e = c - p - 0x800;
        const int v = (e >= 0 ? e : p - c + 0x800) & 0xfff;
        if (int16_t(v) < actor.spin.y) soundPlayAtPos(0x2cf, actor.pos);
    };
    switch (actor.step) {
    case 0: {
        setAnim(actor, uint8_t((f << 2) + 1));
        int32_t p[3];
        positionOf(actor, p);
        const int y = angleOf(kRoomCentre[0], kRoomCentre[2], p[0], p[2]);
        actor.spin.y = 0;
        actor.rotation.y = int16_t(y);
        for (uint32_t k = 0; k < 3; ++k) {
            const uint32_t slot = allocActor20(); // exe 0x5e1160
            if (!slot) continue;
            AnimatedActor& image = animatedActorAt(slot);
            image.flags = kAfterImageFlags;
            image.type = kBossType;
            image.variant = uint8_t((4 - k) | (f << 4));
            image.loopFlags |= kActorSweepGroup2;
            image.subtype = kAfterImageSub;
            image.pos = actor.pos;
            image.rotation = actor.rotation;
            image.anim = actor.anim;
            image.animKey.frame = actor.animKey.frame;
            pointAtWord(boss.afterImages[k], slot);
        }
        soundPlayAtPos(0x2cf, actor.pos);
        blend(actor, 1, 0);
        nextStep(actor);
        break;
    }
    case 1:
        actor.spin.y = int16_t(uint16_t(actor.spin.y) + 0x1cu);
        if (animEnd(actor)) {
            setAnim(actor, uint8_t((f << 2) + 2));
            nextStep(actor);
        }
        break;
    case 2:
        if (animEnd(actor)) {
            setAnim(actor, 3);
            boss.timerA = uint16_t((rngStep() & 0xf) + 0x20u);
            nextStep(actor);
        }
        soundCheck();
        break;
    case 3:
        boss.timerA -= 1u;
        if (boss.timerA == 0) {
            setAnim(actor, uint8_t((f + 1) << 2));
            nextStep(actor);
        }
        soundCheck();
        break;
    case 4:
        actor.spin.y = int16_t(uint16_t(actor.spin.y) - 0x18u);
        if (animEnd(actor)) {
            boss.timerA = 3;
            nextStep(actor);
        }
        soundCheck();
        break;
    case 5:
        boss.timerA -= 1u;
        if (boss.timerA != 0) break;
        blend(actor, 0, 0);
        for (uint32_t k = 0; k < 3; ++k) {
            AnimatedActor* image = boss.afterImages[k].get();
            if (!image) notPorted(kStoryPort, "0x50c420: null after-image (the exe faults)");
            freeActor(*image);
        }
        nextAttack(actor, BossAttack::Pick);
        break;
    default: break;
    }
    // the after-images trail one another (2 <- 1 <- 0 <- the boss): yaw, animation, key
    AnimatedActor* e[3];
    for (uint32_t k = 0; k < 3; ++k) {
        e[k] = boss.afterImages[k].get();
        if (!e[k]) notPorted(kStoryPort, "0x50c420: null after-image pointer (the exe faults)");
    }
    for (int k = 2; k >= 1; --k) {
        e[k]->rotation.y = e[k - 1]->rotation.y;
        e[k]->anim = e[k - 1]->anim;
        e[k]->animKey.frame = e[k - 1]->animKey.frame;
    }
    e[0]->rotation.y = actor.rotation.y;
    e[0]->anim = actor.anim;
    e[0]->animKey.frame = actor.animKey.frame;
    if (f) actor.rotation.y = int16_t((uint16_t(actor.spin.y) + uint16_t(actor.rotation.y)) & 0xfffu);
    else actor.rotation.y = int16_t((uint16_t(actor.rotation.y) - uint16_t(actor.spin.y)) & 0xfffu);
    placeOnCircle(actor);
}
// a blade (static type 0x66, exe 0x5c16b0): following this boss, its bone / height / radius in its position's words,
// its rotation
void spawnBlade(AnimatedActor& actor, int16_t bone, int16_t height, int16_t radius, uint16_t pitch, uint16_t yaw) {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return;
    StaticActor& blade = staticActorAt(slot);
    blade.flags = kActorLive;
    blade.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
    blade.type = kBladeType;
    blade.subtype = 0;
    blade.variant = 0;
    blade.setParamsWord(actor.at);
    blade.pos.x.whole = bone;
    blade.pos.y.whole = height;
    blade.pos.z.whole = radius;
    blade.rotation.x = int16_t(pitch);
    blade.rotation.y = int16_t(yaw);
    blade.rotation.z = 0;
}
// exe 0x50c740: rise high, the blades, a slow descent with sound every 6 ticks, then rise back -> dash out
void bladesAttack(AnimatedActor& actor, BossFields& boss) {
    int s = int32_t(distanceToPlayer(actor) * 3u) >> 8;
    if (int16_t(s) < 8) s = 8;
    else if (int16_t(s) > 0x80) s = 0x80;
    turnFromPlayer(actor, int16_t(s));
    switch (actor.step) {
    case 0:
        soundPlayAtPos(0x2d9, actor.pos);
        setAnim(actor, 9);
        nextStep(actor);
        break;
    case 1:
        riseTo(actor, 0xfb000000u, 0x400);
        if (!animEnd(actor)) break;
        spawnBlade(actor, 9, 0xc0, 0, 0xc00, 0);
        for (uint32_t k = 0; k < 6; ++k) spawnBlade(actor, 9, 0x98, 0xc8, 0xae0, uint16_t(k * 0x2aa));
        for (uint32_t k = 0; k < 8; ++k) spawnBlade(actor, 8, 0x88, 0x160, 0x9c0, uint16_t(k << 9));
        setAnim(actor, 0xa);
        soundPlayAtPos(0x2cb, actor.pos);
        actor.velocity.z = int16_t(0xfec0);
        boss.timerA = 0x180;
        boss.timerB = 9;
        nextStep(actor);
        ++actor.stepCounter;
        break;
    case 2:
        dashForward(actor);
        boss.timerA -= 1u;
        if (boss.timerA == 0) {
            boss.timerA = 8;
            nextStep(actor);
            actor.stepCounter = 0;
            break;
        }
        boss.timerB -= 1u;
        if (boss.timerB == 0) {
            boss.timerB = 6;
            soundPlayAtPos(0x2d5, actor.pos);
        }
        break;
    case 3:
        boss.timerA -= 1u;
        if (boss.timerA == 0) {
            setAnim(actor, 0xb);
            nextStep(actor);
        }
        break;
    case 4:
        riseTo(actor, 0xfcc00000u, 0x200);
        if (animEnd(actor)) nextAttack(actor, BossAttack::DashOut);
        break;
    default: break;
    }
    damageTail(actor, boss);
}
// two statics of type t (variants 0 / 1) following this boss
void spawnChargePair(AnimatedActor& actor, uint8_t type) {
    for (uint32_t k = 0; k < 2; ++k) {
        const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (!slot) continue;
        StaticActor& charge = staticActorAt(slot);
        charge.flags = kActorLive;
        charge.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
        charge.type = type;
        charge.subtype = 0;
        charge.variant = uint8_t(k);
        charge.setParamsWord(actor.at);
    }
}
// exe 0x50c9f0: two beam charges; turning toward the player (the turn speed eased by 2 up to 0x18) until they report
void beamChargesAttack(AnimatedActor& actor, BossFields& boss) {
    switch (actor.step) {
    case 0:
        setAnim(actor, 0x12);
        soundPlayAtPos(0x2dd, actor.pos);
        nextStep(actor);
        break;
    case 1:
        if (!animEnd(actor)) break;
        spawnChargePair(actor, kBeamChargeType);
        setAnim(actor, 0x13);
        nextStep(actor);
        ++actor.stepCounter;
        break;
    case 2:
        if (animEnd(actor)) {
            actor.spin.y = 0;
            setAnim(actor, 0x14);
            nextStep(actor);
        }
        break;
    case 3:
        if (actor.stepCounter == 2) nextStep(actor);
        break;
    case 4: {
        if (actor.stepCounter == 3) {
            nextStep(actor);
            break;
        }
        const uint16_t y = uint16_t((uint16_t(actor.spin.y) + uint16_t(actor.rotation.y)) & 0xfffu);
        actor.rotation.y = int16_t(y);
        const int16_t d = int16_t(angleDiff(uint32_t(angleOf(actor.pos, player().pos)), uint32_t(int16_t(y))));
        if (d == 0) break;
        if (d > 0) {
            actor.spin.y = int16_t(uint16_t(actor.spin.y) + 2u);
            if (actor.spin.y > 0x18) actor.spin.y = 0x18;
        } else {
            actor.spin.y = int16_t(uint16_t(actor.spin.y) - 2u);
            if (actor.spin.y < -0x18) actor.spin.y = int16_t(0xffe8);
        }
        break;
    }
    case 5:
        if (actor.stepCounter == 4) {
            setAnim(actor, 0x15);
            nextStep(actor);
            actor.stepCounter = 0;
        }
        break;
    case 6:
        if (animEnd(actor)) nextAttack(actor, BossAttack::Pick);
        break;
    default: break;
    }
    damageTail(actor, boss);
}
// exe 0x50d270(actor): the stomp: shake / vibration at the actor; for bones 3 and 6 (j = 0 / 1), when the ground probe
// at the actor finds a floor: a shock wave (pool 0x40 stage type 3, variant 0x84) at the bone and three stomp shots
// (variant j << 4 | k) with the yaw (rng & 0x3f) - 0x20
void stomp(const PlayerEnv& env, AnimatedActor& actor) {
    cameraShakeAt(0x2000, 0x100, 0x100, 0x2000, actor.pos);   // exe 0x40eab0
    padVibrateAt(0x401, 0x10220e0, 0x100, 0x2000, actor.pos); // exe 0x55cd80
    uint32_t j = 0;
    for (uint32_t b = 3; b < 9; b += 3, ++j) {
        const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        const uint32_t mask = collisionLayerMask(actor.slot);            // exe 0x413240
        if (probeGroundAt(env, p, CollisionBoxRef::record(stateAddress(TableId::St5cStompProbeBox)), 0, mask) <= 0) continue; // exe 0x40f1c0
        if (const uint32_t slot = allocActor40(kActorStageClass)) {       // exe 0x5e1280
            ProjectileActor& wave = projectileActorAt(slot);
            wave.loopFlags |= kActorSweepGroup2;
            wave.flags = kActorLive;
            wave.type = kShockWaveType;
            wave.variant = 0x84;
            wave.params[0] = 0;
            wave.params[1] = 0;
            const ShortVec4& bp = bonePointOf(actor, b);
            wave.pos.x = int32_t(uint32_t(int32_t(bp.x)) << 16);
            wave.pos.y = 0;
            wave.pos.z = int32_t(uint32_t(int32_t(bp.z)) << 16);
        }
        const uint32_t yaw = (rngStep() & 0x3f) - 0x20u;
        for (uint32_t k = 0; k < 3; ++k) {
            const uint32_t slot = allocActor40(kActorStageClass);
            if (!slot) continue;
            ProjectileActor& shot = projectileActorAt(slot);
            shot.flags = kActorLive;
            shot.type = kStompShotType;
            shot.variant = uint8_t((j << 4) | k);
            shot.loopFlags |= kActorSweepGroup2;
            shot.pos = actor.pos;
            shot.rotation.y = int16_t(yaw);
            shot.setParamsWord(actor.at);
        }
    }
}
// exe 0x50cbd0: jump (rise to -0x400), the stomp at key 0x24, land -> dash out
void stompAttack(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) {
    switch (actor.step) {
    case 0:
        turnFromPlayer(actor, 0x40);
        setAnim(actor, 0xc);
        soundPlayAtPos(0x2d7, actor.pos);
        nextStep(actor);
        break;
    case 1:
        riseTo(actor, 0xfc000000u, 0x200);
        if (int8_t(actor.keyIndex) >= 0x12) nextStep(actor);
        else turnFromPlayer(actor, 0x40);
        break;
    case 2:
        if (actor.keyIndex == 0x24) {
            stomp(env, actor);
            soundPlayAtPos(0x2d0, actor.pos);
            nextStep(actor);
        }
        break;
    case 3:
        if (animEnd(actor)) {
            setAnim(actor, 0xd);
            boss.timerA = 8;
            nextStep(actor);
        }
        break;
    case 4:
        boss.timerA -= 1u;
        if (boss.timerA == 0) {
            setAnim(actor, 0xe);
            nextStep(actor);
        }
        break;
    case 5:
        riseTo(actor, 0xfc400000u, 0x200);
        if (animEnd(actor)) nextAttack(actor, BossAttack::DashOut);
        break;
    default: break;
    }
    damageTail(actor, boss);
}
// exe 0x50cd30: 0x10 rocks dropped around the player at intervals by the difficulty
void rocksAttack(AnimatedActor& actor, BossFields& boss) {
    switch (actor.step) {
    case 0:
        setAnim(actor, 0xf);
        soundPlayAtPos(0x2d8, actor.pos);
        nextStep(actor);
        break;
    case 1:
        if (!animEnd(actor)) break;
        setAnim(actor, 0x10);
        boss.timerA = 0x10;
        boss.timerB = uint16_t((rngStep() & 0xf) + 0x10u);
        nextStep(actor);
        ++actor.stepCounter;
        break;
    case 2: {
        boss.timerB -= 1u;
        if (boss.timerB != 0) break;
        if (const uint32_t slot = allocActor20()) { // exe 0x5e1160
            AnimatedActor& rock = animatedActorAt(slot);
            const Player& p = player();
            rock.flags = kRockFlags;
            rock.loopFlags |= kActorSweepGroup2;
            rock.type = kRockType;
            rock.subtype = 0;
            rock.variant = 0;
            rock.pos.x = int32_t((((rngStep() & 0x1ffu) - 0x100u) << 16) + uint32_t(p.pos.x));
            rock.pos.z = int32_t((((rngStep() & 0x1ffu) - 0x100u) << 16) + uint32_t(p.pos.z));
            const uint8_t s = gameState().difficulty;
            rock.pos.y = int32_t(s < 2 ? 0xc0000000u : s < 3 ? 0xd0000000u : 0xe0000000u);
            rock.setParamsWord(actor.at);
        }
        boss.timerA -= 1u;
        if (boss.timerA == 0) {
            boss.timerA = 0x40;
            nextStep(actor);
            break;
        }
        const uint8_t s = gameState().difficulty;
        if (s < 2) boss.timerB = uint16_t((rngStep() & 0xf) + 0x10u);
        else if (s < 3) boss.timerB = uint16_t((rngStep() & 0xf) + 8u);
        else boss.timerB = uint16_t((rngStep() & 7) + 8u);
        break;
    }
    case 3:
        boss.timerA -= 1u;
        if (boss.timerA == 0) {
            setAnim(actor, 0x11);
            nextStep(actor);
        }
        break;
    case 4:
        if (animEnd(actor)) nextAttack(actor, BossAttack::Pick);
        break;
    default: break;
    }
    damageTail(actor, boss);
}
// exe 0x50cfb0: two ball charges at key 2, a ball, cooldown 0x180
void ballChargeAttack(AnimatedActor& actor, BossFields& boss) {
    switch (actor.step) {
    case 0:
        setAnim(actor, 0x16);
        soundPlayAtPos(0x2da, actor.pos);
        nextStep(actor);
        break;
    case 1:
        if (actor.keyIndex != 2) break;
        spawnChargePair(actor, kBallChargeType);
        nextStep(actor);
        ++actor.stepCounter;
        break;
    case 2:
        if (!animEnd(actor)) break;
        if (const uint32_t slot = allocActor40(kActorStageClass)) { // exe 0x5e1280
            ProjectileActor& ball = projectileActorAt(slot);
            ball.flags = kActorLive;
            ball.loopFlags |= kActorSweepGroup2;
            ball.type = kChargeBallType;
            ball.subtype = 0;
            ball.variant = 0;
            ball.setParamsWord(actor.at);
        }
        setAnim(actor, 0x17);
        nextStep(actor);
        break;
    case 3:
        if (animEnd(actor)) {
            boss.cooldown = 0x180;
            nextAttack(actor, BossAttack::Pick);
        }
        break;
    default: break;
    }
    damageTail(actor, boss);
}
// exe 0x50d420 (state 0 of the after-image, entry 11 of [0x898330]): bind (none: freed), blend (1, 0), state 1
void afterImageBind(AnimatedActor& actor) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    actor.prevAnim = 0xff;
    blend(actor, 1, 0);
    actor.enterStateKeepingStep(1);
    actor.restartStep();
}
// exe 0x50d460 (state 1 of the after-image, entry 12 of [0x898330]): on the circle of radius 0xc00 at its yaw
void afterImageOrbit(AnimatedActor& actor) { placeOnCircle(actor); }
void bossAttack(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss, uint32_t n) { // [0x898330]
    switch (BossAttack(n)) {
    case BossAttack::Pick: pickAttack(actor, boss); return;
    case BossAttack::DashForward: dashForwardAttack(actor, boss); return;
    case BossAttack::DashOut: dashOutAttack(actor, boss); return;
    case BossAttack::DashAtPlayer: dashAtPlayerAttack(actor, boss); return;
    case BossAttack::CircleRight:
    case BossAttack::CircleLeft: circleAttack(actor, boss); return;
    case BossAttack::Blades: bladesAttack(actor, boss); return;
    case BossAttack::BeamCharges: beamChargesAttack(actor, boss); return;
    case BossAttack::Stomp: stompAttack(env, actor, boss); return;
    case BossAttack::Rocks: rocksAttack(actor, boss); return;
    case BossAttack::BallCharge: ballChargeAttack(actor, boss); return;
    case BossAttack::AfterImageBind: afterImageBind(actor); return;
    case BossAttack::AfterImageOrbit: afterImageOrbit(actor); return;
    default: notPortedAt(kStoryPort, "0x50b9c0 sub-state past [0x898330]", n);
    }
}
// exe 0x50ba80: bind
void bossBind(AnimatedActor& actor, BossFields& boss) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    actor.pos.y = int32_t(0xfc400000u);
    actor.anim = 0;
    actor.prevAnim = 0xff;
    actor.animKey.frame = 0;
    const int16_t hp = hasAnimatedStats(actor.type) ? int16_t(uint16_t(animatedStats(actor.type).hitPoints << 7)) : 0;
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.collisionBox.pointAt(TableId::St5cBossBox);
    boss.hitBox.previousX = actor.prevPos.x.whole;
    boss.hitBox.previousZ = actor.prevPos.z.whole;
    boss.hitBox.result = 0;
    boss.timerA = 0;
    boss.timerB = 0;
    boss.cooldown = 0;
    boss.word = 0;
    boss.flags = 0;
    actor.enterStateKeepingStep(uint8_t(BossState::Run));
    actor.restartStep();
    hudAddBossGauge(actor, 1, 7, 0xffffff); // exe 0x415d30
}
// exe 0x50bb30: run (the flags cleared, the cooldown, the attack; the hit list when the attack asks)
void bossRun(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) {
    boss.flags = 0;
    if (boss.cooldown != 0) boss.cooldown -= 1u;
    bossAttack(env, actor, boss, actor.subState);
    if (!(actor.flags & kActorDrawn) || !(boss.flagsLow & kBossHitList)) return;
    boss.hitBox.previousX = actor.prevPos.x.whole;
    boss.hitBox.previousZ = actor.prevPos.z.whole;
    boss.hitBox.result = 0;
    const uint32_t g = actor.hitPoints < 0 ? kHitAttackBump : kHitAttackSolid;
    if (!hasAnimatedStats(actor.type)) notPorted(kStoryPort, "0x50bb30: null type row (the exe faults)");
    const AnimatedActorStats row = animatedStats(actor.type);
    const uint32_t base = hitOwner(actor.slot) | hitHeightLevel(row.hitHeight);
    const uint32_t r1 = row.attack;
    const uint32_t bodyEntry = hitGroup(kHitGroupBodies), noLock = kHitNoLockOn;
    const uint32_t limbAttack = r1 | hitImmunity(1) | kHitAttackDeflects;
    int16_t p[3], q[3];
    pointOf(bonePointOf(actor, 0), p);
    hitListSubmit(p, boss.hitBox, base | bodyEntry | hitRadius(0xb0), r1 | g); // exe 0x5e4120
    bonePointUp(actor, 7, 0x120, p);
    hitListSubmit(p, boss.hitBox, base | bodyEntry | noLock | hitRadius(0xe0), r1 | g);
    pointOf(bonePointOf(actor, 8), p);
    hitListSubmit(p, boss.hitBox, base | bodyEntry | noLock | hitRadius(0x140), r1 | g);
    for (uint32_t b = 3; b < 9; b += 3) {
        pointOf(bonePointOf(actor, b - 1), p);
        pointOf(bonePointOf(actor, b), q);
        hitListSubmitCapsule(p, 0xa, q, 0x28, boss.hitBox, base | bodyEntry | noLock | hitCapsule(2),
                             limbAttack); // exe 0x5e4190
        bonePointUp(actor, b, 0x100, p);
        hitListSubmit(p, boss.hitBox, base | bodyEntry | noLock | hitRadius(0xc0), limbAttack);
    }
}
// exe 0x50bd30: dead (sound 0x2dc; at key 0x30 the flag 0x680)
void bossDead(AnimatedActor& actor) {
    switch (actor.subState) {
    case 0:
        setAnim(actor, 0);
        soundPlayAtPos(0x2dc, actor.pos);
        ++actor.subState;
        break;
    case 1:
        if (actor.keyIndex == 0x30) {
            setStoryFlag(kFlagBossDown); // exe 0x510e10
            ++actor.subState;
        }
        break;
    default: break;
    }
}
void bossTick(const PlayerEnv& env, AnimatedActor& actor) {
    BossFields& boss = classState<BossFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    const uint32_t st = actor.state;
    switch (BossState(st)) { // [0x898324]
    case BossState::Bind: bossBind(actor, boss); break;
    case BossState::Run: bossRun(env, actor, boss); break;
    case BossState::Dead: bossDead(actor); break;
    default:
        if (st > 15) notPortedAt(kStoryPort, "0x50b9c0 state past [0x898324]", st);
        bossAttack(env, actor, boss, st - 3); // the table continues with the attacks
        break;
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, actor.animKey.frame); // exe 0x5e1c30
    else animStepPose(actor);                                                             // exe 0x5e1e80
    // exe 0x5f20f0(actor, 0xc0, 0x100, 0) while drawn and the shadow flag: the shadow, render output
}

// ---- the after-image (type 0x83 sub 2, exe 0x50d3d0) ----------------------------------------------------------------
enum class AfterImageState : uint8_t { Bind = 0, Orbit = 1 };
void afterImage(AnimatedActor& actor) {
    switch (AfterImageState(actor.state)) { // [0x89835c]
    case AfterImageState::Bind: afterImageBind(actor); break;
    case AfterImageState::Orbit: afterImageOrbit(actor); break;
    default: notPortedAt(kStoryPort, "0x50d3d0 state past [0x89835c] (null entry)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, actor.animKey.frame); // exe 0x5e1c30
    else animStepPose(actor);                                                             // exe 0x5e1e80
}

// ---- the spark bursts (static type 0x64, exe 0x5bfcf0; their spawn bytes: content/late_game/st5c.hpp) ---------------

// ---- the intro cast (type 0x83 sub 1, exe 0x50d4a0, the cast 0x8ae5a0 of script 0x75) ------------------------------
enum class IntroState : uint8_t { Bind = 0, FadeIn = 1, Glow = 2, Talk = 3 };
constexpr TableId kIntroFaceTables[2] = {TableId::St5cIntroEyes, TableId::St5cIntroMouth};
const DataTable<ScalarTable<int16_t, 16>> introFadeThresholds{TableId::St5cIntroFadeThresholds};

int32_t stepTick() { return scriptState().stepTick; }
uint8_t stepSub() { return scriptState().stepSub; }
uint8_t frameByte() { return uint8_t(gameState().frame); }

// a spark burst of the kind at a point (a stage static slot, exe 0x5e12f0); 0 when the pool is full
uint32_t allocSparkBurst() { return allocActor60(kActorStageClass); } // exe 0x5e12f0
// exe 0x50da70(actor, block): a spark burst (kind 1) at the actor's x / z, y 0; its colour and size by the step tick
// >> 4
void tickSparks(const AnimatedActor& actor) {
    const uint32_t slot = allocSparkBurst();
    if (!slot) return;
    StaticActor& burst = staticActorAt(slot);
    SparkBurstParams params(staticActorAt(slot));
    const int32_t t = stepTick() >> 4;
    burst.loopFlags |= kActorLateLoop;
    burst.flags = kActorLive;
    burst.type = kSparkBurstType;
    burst.spawnByte = 1;
    burst.variant = uint8_t(uint32_t(t) & 3);
    burst.pos.y = 0;
    burst.pos.x = actor.pos.x;
    burst.pos.z = actor.pos.z;
    params.phase = uint8_t(0x1eu - uint32_t(t));
    params.mask = 0x3f;
    params.life = 0x64;
    params.speed = int8_t(uint32_t(t % 3));
}
// exe 0x50d930(actor): a spark burst (kind 4) at the actor's x / z, y 0, life 0x78
void groundSparks(const AnimatedActor& actor) {
    const uint32_t slot = allocSparkBurst();
    if (!slot) return;
    StaticActor& burst = staticActorAt(slot);
    SparkBurstParams params(staticActorAt(slot));
    burst.spawnByte = 4;
    burst.flags = kActorLive;
    burst.type = kSparkBurstType;
    burst.loopFlags |= kActorLateLoop;
    burst.variant = 0;
    burst.pos.x = actor.pos.x;
    burst.pos.y = 0;
    burst.pos.z = actor.pos.z;
    params.phase = 1;
    params.speed = 0;
    params.mask = 0x1f;
    params.life = 0x78;
}
// exe 0x50d980: a spark burst (kind 4) at (-0x78, -0x400, -0x47e), life 0x3c
void fixedSparks() {
    const uint32_t slot = allocSparkBurst();
    if (!slot) return;
    StaticActor& burst = staticActorAt(slot);
    SparkBurstParams params(staticActorAt(slot));
    burst.flags = kActorLive;
    burst.type = kSparkBurstType;
    burst.loopFlags |= kActorLateLoop;
    burst.variant = 0;
    burst.spawnByte = 4;
    burst.pos.x.whole = int16_t(0xff88);
    burst.pos.y.whole = int16_t(0xfc00);
    burst.pos.z.whole = int16_t(0xfb82);
    params.phase = 1;
    params.speed = 0;
    params.mask = 0x1f;
    params.life = 0x3c;
}
// exe 0x50d9d0(actor, block): the sparks by the step tick (before 0x64 every 4 frames, 0xaa every 8, 0x15e every 16 +
// a ground burst every 8, then a ground burst + the fixed one every 32)
void introSparks(const AnimatedActor& actor) {
    const int32_t t = stepTick();
    const uint8_t fc = frameByte();
    if (t < 0x64) {
        if (!(fc & 3)) tickSparks(actor);
    } else if (t < 0xaa) {
        if (!(fc & 7)) tickSparks(actor);
    } else if (t < 0x15e) {
        if (!(fc & 0xf)) tickSparks(actor);
        if (!(frameByte() & 7)) groundSparks(actor);
    } else if (!(fc & 0x1f)) {
        groundSparks(actor);
        fixedSparks();
    }
}
// exe 0x50d880(actor, block): shown, the fade counter 0x10b
void fadeReset(AnimatedActor& actor, IntroFields& intro) {
    actor.setDrawn(true);
    intro.fadeCounter = 0x10b;
}
// exe 0x50d8a0(actor, block): the bones' brightness by the counter against their thresholds; the counter steps down;
// past 0: blend (0, 1), 1
int fadeBones(AnimatedActor& actor, IntroFields& intro) {
    int c = intro.fadeCounter;
    actor.tint = 0;
    for (uint32_t i = 0; i < 16; ++i) {
        const int thr = introFadeThresholds[i];
        BoneColour colour = boneColourOf(actor, i);
        if (c > thr) {
            colour = 0;
        } else {
            int v = thr - c;
            if (v > 0x10) v = 0x10;
            colour = uint16_t(((v << 5) | v) << 5 | v);
        }
    }
    --c;
    if (c >= 0) {
        intro.fadeCounter = int16_t(c);
        return 0;
    }
    blend(actor, 0, 1);
    return 1;
}
// exe 0x50d5f0 ([0x898368] variant 0)
void introCastState(AnimatedActor& actor, CastFields& cast, IntroFields& intro) {
    switch (IntroState(actor.state)) {
    case IntroState::Bind:
        if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
            freeActor(actor);            // exe 0x5e1490
            return;
        }
        actor.collisionBox.pointAt(TableId::St5cIntroBox);
        actor.clearClassBlock();
        actor.enterState(uint8_t(IntroState::FadeIn));
        actor.anim = 0x20;
        actor.prevAnim = 0xff;
        clearVelocity(actor);
        cast.animKey = 0;
        cast.faceTracks.init(stateAddress(kIntroFaceTables[0]), stateAddress(kIntroFaceTables[1])); // exe 0x5e33a0
        textureTrackStop(actor, cast.faceTracks, FaceTrack::Eyes, 2);    // exe 0x5e3630
        textureTrackStop(actor, cast.faceTracks, FaceTrack::Mouth, 0);   // exe 0x5e3660
        actor.subState = 1;
        [[fallthrough]];
    case IntroState::FadeIn:
        if (stepSub() == 1) actor.state = uint8_t(IntroState::Glow);
        introSparks(actor);
        fadeReset(actor, intro);
        fadeBones(actor, intro);
        return;
    case IntroState::Glow:
        if (uint32_t(stepTick()) == 0xc8) actor.anim = 0x21;
        introSparks(actor);
        if (actor.subState != 0 && fadeBones(actor, intro) != 0) actor.subState = 0;
        if (stepSub() == 2) actor.state = uint8_t(IntroState::Talk);
        return;
    case IntroState::Talk: {
        switch (uint32_t(stepTick())) {
        case 0x28: cast.faceTracks.first.select(1, 0); break;
        case 0x50:
            cast.faceTracks.first.select(2, 0);
            actor.anim = 0x22;
            break;
        case 0x82: actor.anim = 0x23; break;
        case 0x84:
            cast.faceTracks.first.select(3, 0);
            cast.faceTracks.second.select(1, 0);
            break;
        case 0xb6: cast.faceTracks.second.select(0, 0); break;
        default: break;
        }
        if (stepTick() < 0x96 && !(frameByte() & 0xf)) {
            groundSparks(actor);
            fixedSparks();
        }
        if (actor.anim == 0x23 && stepTick() > 0xca && actor.animEnded()) actor.anim = 0;
        return;
    }
    default: return;
    }
}
void introCast(const PlayerEnv& env, AnimatedActor& actor) {
    IntroFields& intro = classState<IntroFields>(actor); // the cast's block and its own bytes past it
    CastFields& cast = intro;
    castSavePose(actor);
    if (actor.variant != 0) notPortedAt(kStoryPort, "0x50d4a0 variant past [0x898368] (null entry)", actor.variant);
    introCastState(actor, cast, intro);
    if (!(actor.flags & kActorLive)) return;
    castDrift(actor);
    if (cast.castFlags & kCastOnGround) actor.pos.y.whole = int16_t(actorGround(env, actor)); // exe 0x40f190
    textureTracksTick(actor, cast.faceTracks);                                              // exe 0x5e34f0
    castAnimate(actor, cast.animKey, cast.castFlags, kCastHeld);
    // exe 0x5f2220(actor, ...) while the shadow flag: the shadow, render output
}

// ---- the defeat cast (type 0x83 sub 3, exe 0x50dae0, the cast 0x8ad590 of script 0x6d) -----------------------------
enum class DefeatState : uint8_t { Lift = 0, Talk = 1, Idle = 2 };
enum class DropPhase : uint8_t { None = 0, Start = 1, Falling = 2 };
constexpr TableId kDefeatFaceTables[2] = {TableId::St5cDefeatEyes, TableId::St5cDefeatMouth};

// exe 0x50db90: step 0 -- lifted 0x12c above its ground, then dropped (shake / vibration on landing)
void defeatLift(AnimatedActor& actor, CastFields& cast, DefeatFields& defeat) {
    if (actor.subState == 0) {
        if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
            freeActor(actor);            // exe 0x5e1490
            return;
        }
        actor.collisionBox.pointAt(TableId::St5cDefeatBox);
        actor.clearClassBlock();
        actor.subState = 1;
        actor.clearStepBytes();
        actor.anim = 0x31;
        actor.prevAnim = 0xff;
        clearVelocity(actor);
        cast.animKey = 0;
        cast.faceTracks.init(stateAddress(kDefeatFaceTables[0]), stateAddress(kDefeatFaceTables[1])); // exe 0x5e33a0
        textureTrackStop(actor, cast.faceTracks, FaceTrack::Eyes, 5);
        textureTrackStop(actor, cast.faceTracks, FaceTrack::Mouth, 0);
        defeat.groundY = actor.pos.y;
        actor.pos.y = int32_t(uint32_t(actor.pos.y) + 0xfed40000u);
        defeat.wordA = 0;
        defeat.wordB = 0x200;
    }
    if (stepSub() != 0) {
        actor.enterState(uint8_t(DefeatState::Talk));
        return;
    }
    switch (uint32_t(stepTick())) {
    case 1:
        actor.anim = 0x31;
        actor.pos.y = int32_t(uint32_t(defeat.groundY) - 0x12c0000u);
        cast.animKey = 0;
        break;
    case 0xfb:
        actor.anim = 0x32;
        cast.animKey = 0;
        actor.stepCounter = uint8_t(DropPhase::Start);
        textureTrackStop(actor, cast.faceTracks, FaceTrack::Eyes, 5);
        textureTrackStop(actor, cast.faceTracks, FaceTrack::Mouth, 0);
        break;
    case 0x191:
        actor.anim = 0x33;
        cast.animKey = 0;
        break;
    case 0x1ff:
        cameraShake(0, 0x600, 0x30);   // exe 0x40ea80
        padVibrate(0x401, 0x10430f8); // exe 0x55cd10
        break;
    default: break;
    }
    switch (DropPhase(actor.stepCounter)) {
    case DropPhase::Start:
        actor.acceleration.y = 1;
        actor.stepCounter = uint8_t(DropPhase::Falling);
        break;
    case DropPhase::Falling:
        if (actor.velocity.y >= 0x80) actor.acceleration.y = 0;
        if (actor.pos.y.whole >= defeat.groundY.whole) {
            cameraShake(0, 0x800, 0x40);
            padVibrate(0x401, 0x10450f8);
            actor.pos.y = defeat.groundY;
            actor.velocity.y = 0;
            actor.stepCounter = uint8_t(DropPhase::None);
        }
        break;
    default: break;
    }
    if (DropPhase(actor.stepCounter) == DropPhase::None) return;
    actor.velocity.y = int16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y));
    integrateVelocity(actor, 0, actor.velocity.y, 0); // exe 0x5e3690
}
// exe 0x50dd60: step 1, face cells by the step tick
void defeatTalk(AnimatedActor& actor, CastFields& cast) {
    if (stepSub() != 1) {
        actor.enterState(uint8_t(DefeatState::Idle));
        return;
    }
    switch (uint32_t(stepTick())) {
    case 1:
        textureTrackStop(actor, cast.faceTracks, FaceTrack::Eyes, 8);
        textureTrackStop(actor, cast.faceTracks, FaceTrack::Mouth, 3);
        return;
    case 0xd1: case 0x10f: case 0x14b: case 0x1d7: case 0x24f: case 0x2b3: case 0x303: case 0x35d: case 0x3d5:
    case 0x41b: case 0x4ed: case 0x521: case 0x583: case 0x5d3:
        textureTrackStop(actor, cast.faceTracks, FaceTrack::Mouth, 3);
        return;
    case 0xa1: case 0xfb: case 0x123: case 0x19b: case 0x1ff: case 0x277: case 0x2f9: case 0x335: case 0x38f:
    case 0x3fd: case 0x4e1: case 0x515: case 0x56f: case 0x5ab:
        cast.faceTracks.second.select(0, 0);
        return;
    case 0x17d: cast.faceTracks.first.select(1, 0); return;
    case 0x5dd:
        actor.anim = 0x34;
        cast.animKey = 0;
        cast.faceTracks.first.select(2, 0);
        return;
    default: return;
    }
}
void defeatCast(AnimatedActor& actor) {
    DefeatFields& defeat = classState<DefeatFields>(actor); // the cast's block and its own bytes past it
    CastFields& cast = defeat;
    castSavePose(actor);
    switch (DefeatState(actor.state)) { // [0x898418]
    case DefeatState::Lift: defeatLift(actor, cast, defeat); break;
    case DefeatState::Talk: defeatTalk(actor, cast); break;
    case DefeatState::Idle: break; // 0x52ef00
    default: notPortedAt(kStoryPort, "0x50dae0 state past [0x898418]", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    textureTracksTick(actor, cast.faceTracks); // exe 0x5e34f0
    castAnimate(actor, cast.animKey, cast.castFlags, kCastHeld);
}

// ---- the spark bursts (static type 0x64, exe 0x5bfcf0) --------------------------------------------------------------
enum class BurstState : uint8_t {
    Init = 0, Ring = 1, Billboards = 2, Bloom = 3, Spray = 4, Scatter = 5, InitB = 6, Star = 7, Swirl = 8, Halo = 9,
};

// exe 0x5c0c00 / 0x5c1590 (identical): the colour of level n (past 7: 4..7 by the low bits) in mode m
uint32_t sparkColour(uint32_t n, uint32_t mode) {
    if (n > 7) {
        const uint32_t e = n & 7;
        n = e < 4 ? 7 - e : e;
    }
    const uint32_t v = (((n << 8) - n) << 5) >> 8;
    switch (mode) {
    case 0: return (((v & 0xfe) << 8 | (v & 0xfe)) << 7) | v;
    case 1: return (((v & 0xff) << 9 | (v & 0xfe)) << 7) | (v >> 1);
    case 2: return (((v & 0xfe) << 7 | (v & 0xff)) << 8) | (v >> 1);
    case 3: return (((v & 0xfe) << 7 | (v & 0xff)) << 8) | v;
    case 4: return (((v & 0xff) << 8 | (v & 0xff)) << 8) | (v >> 1);
    case 5: return (((v & 0xfc) << 8 | (v & 0xfc)) << 6) | (v >> 2);
    default: notPortedAt(kStoryPort, "exe 0x5c0c00: colour mode past 5 (the exe leaves its local unset)", mode);
    }
}
// one spark moved out from the centre c: the pitch (+0x400) splits the speed into dy (>> dyShift) and h (>> hShift),
// the yaw (+ yawAdd) splits h into x / z
void stepOut(const int16_t c[3], BurstSpark& p, int dyShift, int hShift, int yawAdd) {
    const int16_t q[3] = {p.x, p.y, p.z};
    const int32_t sp = p.speed;
    const uint32_t pi = uint32_t(pitchOf(c, q) + 0x400) & 0xfff;
    const int32_t dy = (fixedSin(int(pi)) * sp) >> dyShift;
    const int32_t h = (fixedCos(int(pi)) * sp) >> hShift;
    const uint32_t ya = uint32_t(angleOf(c, q) + yawAdd) & 0xfff;
    p.x = int16_t(uint16_t(p.x) + uint32_t((fixedSin(int(ya)) * h) >> 12));
    p.y = int16_t(uint16_t(p.y) + uint32_t(dy));
    p.z = int16_t(uint16_t(p.z) + uint32_t((fixedCos(int(ya)) * h) >> 12));
}
uint32_t sparkXY(const BurstSpark& p) { return uint32_t(uint16_t(p.x)) | uint32_t(uint16_t(p.y)) << 16; }
// the two rings of a spark (sizes 0x40 / 0x20 << 16)
void sparkRings(const StaticActor& actor, const BurstSpark& p, uint32_t col, uint32_t zflag) {
    const uint32_t xy = sparkXY(p), z = uint16_t(p.z) | zflag;
    dc::emit({0x4000u, dc::ringColour(col), xy, dc::kSemiAdditive, z, 0x400000u}, actor.at);
    dc::emit({0x4000u, dc::ringColour(col), xy, dc::kSemiAdditive, z, 0x200000u}, actor.at);
}
// a billboard of a spark at its point << the stage shift, uv by the speed byte, size the spark's byte
void sparkBillboard(const StaticActor& actor, const BurstFields& burst, const BurstSpark& p, uint32_t i, uint32_t col) {
    const uint32_t sh = stage().coordinateShift & 0x1f;
    const uint16_t x = uint16_t(uint32_t(uint16_t(p.x)) << sh), y = uint16_t(uint32_t(uint16_t(p.y)) << sh),
                   z = uint16_t(uint32_t(uint16_t(p.z)) << sh);
    const uint32_t u = uint32_t(int32_t(burst.speed) << 5), u2 = u + 0x1f;
    dc::emit({0, dc::billboardColour(col), uint32_t(x) | uint32_t(y) << 16, u | 0x3c78a000u, z, u2 | 0x2ea000u,
              uint32_t(int32_t(burst.sizes[i])), u | 0xbf00u, 0, u2 | 0xbf00u},
             actor.at, false);
}
void centreOf(const StaticActor& actor, int16_t c[3]) {
    c[0] = actor.pos.x.whole;
    c[1] = actor.pos.y.whole;
    c[2] = actor.pos.z.whole;
}
// the life byte: 0 -> freed (false), else decremented
bool burstLife(StaticActor& actor, BurstFields& burst) {
    if (burst.life == 0) {
        freeActor(actor); // exe 0x5e1540
        return false;
    }
    burst.life -= 1u;
    return true;
}
// a new spark i on the ring of angle (phase & 0x3f) * 0x40 at radius rad (>> rs), y + (rng & 0xff)
void ringSpawn(const StaticActor& actor, BurstFields& burst, uint32_t i, int rad, int rs) {
    const uint32_t ang = (burst.phase & 0x3fu) * 0x40;
    BurstSpark& p = burst.sparks[i];
    p.x = int16_t(uint16_t(actor.pos.x.whole) + uint32_t((fixedCos(int(ang)) * rad) >> rs));
    p.y = int16_t(uint16_t(actor.pos.y.whole) + (rngStep() & 0xffu));
    p.z = int16_t(uint16_t(actor.pos.z.whole) + uint32_t((fixedSin(int(ang)) * rad) >> rs));
}
// exe 0x5bfd80 (kind 1): rings, speed (rng & 0xf) + speed * 2
void burstRing(StaticActor& actor, BurstFields& burst) {
    if (!burstLife(actor, burst)) return;
    const int8_t n0 = burst.count;
    if (n0 < 0x10) {
        const uint32_t i = uint32_t(int32_t(n0));
        BurstSpark& p = burst.sparks[i];
        ringSpawn(actor, burst, i, 0x300, 12);
        p.speed = int16_t((rngStep() & 0xf) + uint32_t(int32_t(burst.speed) * 2));
        burst.sizes[i] = int8_t((burst.mask & rngStep()) + 3u);
        burst.phase += 4u;
        ++burst.count;
    }
    int16_t c[3];
    centreOf(actor, c);
    const uint32_t col = sparkColour(burst.life, actor.variant);
    for (int i = burst.count - 1; i >= 0; --i) {
        BurstSpark& p = burst.sparks[size_t(i)];
        stepOut(c, p, 12, 12, 0x400);
        sparkRings(actor, p, col, 0x20000);
    }
}
// exe 0x5c0030 (kind 2): billboards (colour mode 5), speed (mask & rng) + phase; after the step tick 0xdc the sparks
// spread faster (h >> 10) and turn by -0x500
void burstBillboards(StaticActor& actor, BurstFields& burst) {
    if (!burstLife(actor, burst)) return;
    const int8_t n0 = burst.count;
    if (n0 < 0x10) {
        const uint32_t i = uint32_t(int32_t(n0));
        BurstSpark& p = burst.sparks[i];
        ringSpawn(actor, burst, i, 0x300, 12);
        p.speed = int16_t((uint32_t(int32_t(int8_t(burst.mask))) & rngStep()) + uint32_t(int32_t(int8_t(burst.phase))));
        burst.sizes[i] = int8_t((burst.mask & rngStep()) + 0x40u);
        burst.phase += 4u;
        ++burst.count;
    }
    int16_t c[3];
    centreOf(actor, c);
    const uint32_t col = sparkColour(burst.life, 5);
    for (int i = burst.count - 1; i >= 0; --i) {
        BurstSpark& p = burst.sparks[size_t(i)];
        if (stepTick() > 0xdc) stepOut(c, p, 12, 10, -0x500);
        else stepOut(c, p, 12, 12, -0x400);
        sparkBillboard(actor, burst, p, uint32_t(i), col);
    }
}
// exe 0x5c0360 (kind 3): 16 sparks at once (speed 0x10), 0x190 ticks; the last 0x12c rise out, the first 0x64 only
// spread
void burstBloom(StaticActor& actor, BurstFields& burst) {
    if (actor.step == 0) {
        burst.count = 0;
        uint32_t c = (rngStep() >> 6) & 0x3f;
        while (burst.count < 0x10) {
            const uint32_t i = uint32_t(int32_t(burst.count)), ang = (c & 0x3f) * 0x40;
            BurstSpark& p = burst.sparks[i];
            p.x = int16_t(uint16_t(actor.pos.x.whole) + uint32_t((fixedCos(int(ang)) * 0x300) >> 12));
            p.y = actor.pos.y.whole;
            p.z = int16_t(uint16_t(actor.pos.z.whole) + uint32_t((fixedSin(int(ang)) * 0x300) >> 12));
            p.speed = 0x10;
            burst.sizes[i] = int8_t(c);
            c += 4;
            ++burst.count;
        }
        ++actor.step;
        burst.timer = 0x190;
    }
    if (burst.timer == 0) {
        freeActor(actor); // exe 0x5e1540
        return;
    }
    burst.timer -= 1u;
    int16_t c[3];
    centreOf(actor, c);
    const uint32_t col = sparkColour(burst.timerLow, 4);
    for (int i = burst.count - 1; i >= 0; --i) {
        BurstSpark& p = burst.sparks[size_t(i)];
        if (int16_t(burst.timer) < 0x12c) {
            stepOut(c, p, 12, 12, 0x400);
        } else {
            const int16_t q[3] = {p.x, p.y, p.z};
            const uint32_t ya = uint32_t(angleOf(c, q) - 0x400) & 0xfff;
            const int32_t sp = p.speed;
            p.x = int16_t(uint16_t(p.x) + uint32_t((fixedSin(int(ya)) * sp) >> 12));
            p.z = int16_t(uint16_t(p.z) + uint32_t((fixedCos(int(ya)) * sp) >> 12));
        }
        sparkRings(actor, p, col, 0x40000);
    }
}
// exe 0x5c0650 (kind 4): sparks within 0x4b0 at angles 0x300..0x4ff, slow rise (dy >> 14), wide spread (h >> 9); two
// rings sized by the spark's byte (<< 16 / << 17)
void burstSpray(StaticActor& actor, BurstFields& burst) {
    if (!burstLife(actor, burst)) return;
    const int8_t n0 = burst.count;
    if (n0 < 0x10) {
        const uint32_t i = uint32_t(int32_t(n0));
        BurstSpark& p = burst.sparks[i];
        const uint32_t ang = ((rngStep() & 0x1ff) + 0x300) & 0xfff;
        p.x = int16_t(uint16_t(actor.pos.x.whole) + uint32_t((fixedCos(int(ang)) * 0x4b0) >> 12));
        p.y = int16_t(uint16_t(actor.pos.y.whole) + (rngStep() & 0xfu));
        p.z = int16_t(uint16_t(actor.pos.z.whole) + uint32_t((fixedSin(int(ang)) * 0x4b0) >> 12));
        p.speed = int16_t(uint32_t(int32_t(burst.speed)) + (rngStep() & 0x1fu));
        burst.sizes[i] = int8_t(((rngStep() >> 8) & burst.mask) + 3u);
        ++burst.count;
    }
    int16_t c[3];
    centreOf(actor, c);
    const uint32_t col = sparkColour(burst.life, actor.variant);
    for (int i = burst.count - 1; i >= 0; --i) {
        BurstSpark& p = burst.sparks[size_t(i)];
        stepOut(c, p, 14, 9, 0x800);
        const uint32_t xy = sparkXY(p), z = uint16_t(p.z) | 0x20000u;
        const int16_t sz = int16_t(burst.sizes[size_t(i)]);
        dc::emit({0x4000u, dc::ringColour(col), xy, dc::kSemiAdditive, z, uint32_t(int32_t(sz)) << 16}, actor.at);
        dc::emit({0x4000u, dc::ringColour(col), xy, dc::kSemiAdditive, z, uint32_t(int32_t(int16_t(sz * 2))) << 16},
                 actor.at);
    }
}
// exe 0x5c0940 (kind 5): sparks at a random radius (< 0x400) on the ring, one ring each sized by the byte
void burstScatter(StaticActor& actor, BurstFields& burst) {
    if (!burstLife(actor, burst)) return;
    const int8_t n0 = burst.count;
    if (n0 < 0x10) {
        const uint32_t i = uint32_t(int32_t(n0));
        BurstSpark& p = burst.sparks[i];
        const int r = int((rngStep() >> 8) & 0x3ff);
        ringSpawn(actor, burst, i, r, 12);
        p.speed = int16_t((uint32_t(int32_t(int8_t(burst.mask))) & rngStep()) + uint32_t(int32_t(int8_t(burst.phase))));
        burst.sizes[i] = int8_t((burst.mask & rngStep()) + 0xau);
        burst.phase += 4u;
        ++burst.count;
    }
    int16_t c[3];
    centreOf(actor, c);
    const uint32_t col = sparkColour(burst.life, actor.variant);
    for (int i = burst.count - 1; i >= 0; --i) {
        BurstSpark& p = burst.sparks[size_t(i)];
        stepOut(c, p, 13, 12, 0x400);
        const uint32_t xy = sparkXY(p), z = uint16_t(p.z) | 0x40000u;
        dc::emit({0x4000u, dc::ringColour(col), xy, dc::kSemiAdditive, z,
                  uint32_t(int32_t(burst.sizes[size_t(i)])) << 16},
                 actor.at);
    }
}
// exe 0x5c0d80 (kind 7): 8 sparks at angles i * 0x200, radius phase + 0xe0, speed speed * 2
void burstStar(StaticActor& actor, BurstFields& burst) {
    if (!burstLife(actor, burst)) return;
    const int8_t n0 = burst.countB;
    if (n0 < 8) {
        const uint32_t i = uint32_t(int32_t(n0));
        BurstSpark& p = burst.sparks[i];
        const int rad = int16_t(uint16_t(int32_t(int8_t(burst.phase)) + 0xe0));
        const uint32_t ang = (i << 9) & 0xfff;
        p.x = int16_t(uint16_t(actor.pos.x.whole) + uint32_t((fixedCos(int(ang)) * rad) >> 12));
        p.y = int16_t(uint16_t(actor.pos.y.whole) + (rngStep() & 0xffu));
        p.z = int16_t(uint16_t(actor.pos.z.whole) + uint32_t((fixedSin(int(ang)) * rad) >> 12));
        p.speed = int16_t(uint32_t(int32_t(burst.speed)) << 1);
        burst.sizes[i] = int8_t((burst.mask & rngStep()) + 3u);
        ++burst.countB;
    }
    int16_t c[3];
    centreOf(actor, c);
    const uint32_t col = sparkColour(burst.life, actor.variant);
    for (int i = burst.countB - 1; i >= 0; --i) {
        BurstSpark& p = burst.sparks[size_t(i)];
        stepOut(c, p, 12, 12, 0x400);
        sparkRings(actor, p, col, 0x30000);
    }
}
// exe 0x5c1010 (kind 8): billboards (colour mode 5) at radius 0xe0 + (rng >> 4 & 0x7f), turning by -0x400
void burstSwirl(StaticActor& actor, BurstFields& burst) {
    if (!burstLife(actor, burst)) return;
    const int8_t n0 = burst.countB;
    if (n0 < 0x10) {
        const uint32_t i = uint32_t(int32_t(n0));
        BurstSpark& p = burst.sparks[i];
        const int r = int(((rngStep() >> 4) & 0x7f) + 0xe0);
        ringSpawn(actor, burst, i, r, 12);
        p.speed = int16_t((uint32_t(int32_t(int8_t(burst.mask))) & rngStep()) + uint32_t(int32_t(int8_t(burst.phase))));
        burst.sizes[i] = int8_t((burst.mask & rngStep()) + 0x28u);
        burst.phase += 4u;
        ++burst.countB;
    }
    int16_t c[3];
    centreOf(actor, c);
    const uint32_t col = sparkColour(burst.life, 5);
    for (int i = burst.countB - 1; i >= 0; --i) {
        BurstSpark& p = burst.sparks[size_t(i)];
        stepOut(c, p, 12, 12, -0x400);
        sparkBillboard(actor, burst, p, uint32_t(i), col);
    }
}
// exe 0x5c1310 (kind 9): 16 sparks at once on a ring of radius 0x70 (speed 0x10), turning by -0x400
void burstHalo(StaticActor& actor, BurstFields& burst) {
    if (!burstLife(actor, burst)) return;
    if (actor.step == 0) {
        burst.countB = 0;
        uint32_t c = (rngStep() >> 6) & 0x3f;
        while (burst.countB < 0x10) {
            const uint32_t i = uint32_t(int32_t(burst.countB)), ang = (c & 0x3f) * 0x40;
            BurstSpark& p = burst.sparks[i];
            p.x = int16_t(uint16_t(actor.pos.x.whole) + uint32_t((fixedCos(int(ang)) * 0xe0) >> 13));
            p.y = actor.pos.y.whole;
            p.z = int16_t(uint16_t(actor.pos.z.whole) + uint32_t((fixedSin(int(ang)) * 0xe0) >> 13));
            p.speed = 0x10;
            burst.sizes[i] = int8_t(c);
            c += 4;
            ++burst.countB;
        }
        ++actor.step;
    }
    int16_t c[3];
    centreOf(actor, c);
    const uint32_t col = sparkColour(burst.life, actor.variant);
    for (int i = burst.countB - 1; i >= 0; --i) {
        BurstSpark& p = burst.sparks[size_t(i)];
        stepOut(c, p, 12, 12, -0x400);
        sparkRings(actor, p, col, 0x40000);
    }
}
void sparkBurst(StaticActor& actor) {
    BurstFields& burst = classState<BurstFields>(actor);
    switch (BurstState(actor.state)) { // [0x8d0e28]
    case BurstState::Init:                   // 0x5bfd10
        actor.setDrawn(false);
        burst.count = 0;
        burst.timer = 0;
        if (actor.spawnByte <= 4) actor.enterState(uint8_t(actor.spawnByte + 1u));
        return;
    case BurstState::Ring: burstRing(actor, burst); return;
    case BurstState::Billboards: burstBillboards(actor, burst); return;
    case BurstState::Bloom: burstBloom(actor, burst); return;
    case BurstState::Spray: burstSpray(actor, burst); return;
    case BurstState::Scatter: burstScatter(actor, burst); return;
    case BurstState::InitB: // 0x5c0d40
        actor.setDrawn(false);
        burst.countB = 0;
        if (actor.spawnByte <= 2) actor.enterState(uint8_t(actor.spawnByte + 7u));
        return;
    case BurstState::Star: burstStar(actor, burst); return;
    case BurstState::Swirl: burstSwirl(actor, burst); return;
    case BurstState::Halo: burstHalo(actor, burst); return;
    default: notPortedAt(kStoryPort, "0x5bfcf0 state past [0x8d0e28]", actor.state);
    }
}

// ---- the dust effect (pool 0xa0 type 0x24, exe 0x43ff80), variant 2 0x440940: four dust puffs ----------------------
// The puffs' own bytes: their points (the first over the position), their life, yaw, yaw step, size and drift
struct DriftingPuffsFields {
    explicit DriftingPuffsFields(const EffectActor& actor) : DriftingPuffsFields(const_cast<EffectActor&>(actor)) {}
    explicit DriftingPuffsFields(EffectActor& actor)
        : puffs{actor}, life(actor.subState), yaw(actor.step), yawStep(actor.stepCounter), size(actor.params[0]),
          drift(actor.params, 1), slot_(actor) {}
    EffectPoints puffs;
    uint8_t& life;
    uint8_t& yaw;
    uint8_t& yawStep;
    FieldAlias<int8_t, uint8_t> size;
    ArrayFields<int8_t, 3, uint8_t, 4> drift;
    // the bytes after the first puff the init clears (the trail)
    void clearPuffBytes() { slot_.trail = {}; }

private:
    EffectActor& slot_;
};
// The projection scratch the exe builds the puffs in (written as the exe does): a point, its spare lane and its two
// words, the drift
struct PuffScratch {
    LaneVec3 point = geometryScratch().vec3(0x0);
    Lane16 pointSpare = geometryScratch().lane(0x6);
    Lane32 pointXY = geometryScratch().word(0x0);
    Lane32 pointZ = geometryScratch().word(0x4);
    LaneVec3 drift = geometryScratch().vec3(0x8);
};
PuffScratch& puffScratch() {
    static PuffScratch s;
    return s;
}
void driftingPuffs(EffectActor& actor) {
    DriftingPuffsFields fx(actor);
    PuffScratch& v = puffScratch();
    if (actor.state == 0) {
        fx.clearPuffBytes(); // exe 0x40dc40
        v.point.x = actor.pos.x;
        v.point.y = actor.pos.y;
        v.point.z = actor.pos.z;
        for (uint32_t k = 0; k < 4; ++k) { // the puffs within 0x180 of the spawn point
            ShortTriple q = fx.puffs[k];
            const uint32_t ang = rngStep() & 0xfff;
            q.x = int16_t(uint16_t(v.point.x) + uint32_t((fixedSin(int(ang)) * 0x180) >> 12));
            q.y = int16_t((rngStep() & 0xffu) + uint16_t(v.point.y) - 0x80u);
            q.z = int16_t(uint16_t(v.point.z) + uint32_t((fixedCos(int(ang)) * 0x180) >> 12));
        }
        fx.life = uint8_t((rngStep() & 0xf) + 0x28u);
        fx.yaw = uint8_t(rngStep());
        fx.yawStep = uint8_t(rngStep());
        fx.size = int8_t((rngStep() & 0xf) + 0x10u);
        fx.drift[0] = int8_t(0x20u - (rngStep() & 0x1f));
        fx.drift[1] = int8_t(0xf0u - (rngStep() & 0xf));
        fx.drift[2] = int8_t(0x20u - (rngStep() & 0x1f));
    }
    const uint32_t total = fx.life, t = actor.state, half = total >> 1;
    if (half == 0) notPorted(kStoryPort, "0x440940: life < 2 (the exe divides by zero)");
    const uint32_t g = (t < half ? t * 0xff : total * 0xff - t * 0xff) / half;
    const uint32_t col = dc::billboardColour((((g & 0x1fe) << 7) | g) << 8);
    const uint32_t sh = stage().coordinateShift & 0x1f;
    for (uint32_t k = 0; k < 4; ++k) {
        const ShortTriple q = fx.puffs[k];
        v.point.x = int16_t(uint32_t(uint16_t(q.x)) << sh);
        v.point.y = int16_t(uint32_t(uint16_t(q.y)) << sh);
        v.pointSpare = 0;
        v.point.z = int16_t(uint32_t(uint16_t(q.z)) << sh);
        dc::emit({0, col, uint32_t(v.pointXY), 0x7c502020u, uint32_t(v.pointZ), 0x2f203fu, uint32_t(int32_t(fx.size)), 0x3f20u, 0, 0x3f3fu},
                 actor.at, false);
    }
    ++actor.state;
    if (actor.state == fx.life) {
        freeActor(actor); // exe 0x5e1580
        return;
    }
    // the drift: each puff moved by the drift << 4 along the yaw * 4 + k * the yaw step * 4
    const uint32_t step = uint32_t(fx.yawStep) << 2;
    uint32_t yaw = uint32_t(fx.yaw) << 2;
    v.drift.x = int16_t(uint32_t(int32_t(fx.drift[0])) << 4);
    v.drift.y = int16_t(uint32_t(int32_t(fx.drift[1])) << 4);
    v.drift.z = int16_t(uint32_t(int32_t(fx.drift[2])) << 4);
    for (uint32_t k = 0; k < 4; ++k, yaw += step) {
        ShortTriple q = fx.puffs[k];
        v.point.x = q.x;
        v.point.y = q.y;
        v.point.z = q.z;
        moveAlongYaw16(v.point, int(yaw), v.drift.x, v.drift.y, v.drift.z); // exe 0x5e3750
        q.x = v.point.x;
        q.y = v.point.y;
        q.z = v.point.z;
    }
    fx.yaw += 2u;
    ++fx.yawStep;
}

} // namespace

void st5cBossUpdate(const PlayerEnv& env, uint32_t actor) { bossTick(env, animatedActorAt(actor)); }

void bossIntroCastUpdate(const PlayerEnv& env, uint32_t actor) { introCast(env, animatedActorAt(actor)); }

void bossAfterImageUpdate(const PlayerEnv&, uint32_t actor) { afterImage(animatedActorAt(actor)); }

void bossDefeatCastUpdate(const PlayerEnv&, uint32_t actor) { defeatCast(animatedActorAt(actor)); }

void sparkBurstUpdate(const PlayerEnv&, uint32_t actor) { sparkBurst(staticActorAt(actor)); }

// exe 0x440940: the four dust puffs (exe 0x43ff80 [0x88d998] variant 2 = exe 0x43d030 [0x88d970] variant 12)
void driftingPuffsUpdate(const PlayerEnv&, uint32_t actor) { driftingPuffs(effectActorAt(actor)); }

void stageDustEffectUpdate(const PlayerEnv& env, uint32_t actor) {
    EffectActor& effect = effectActorAt(actor);
    switch (effect.variant) { // [0x88d998]
    case 0: lightPillarUpdate(env, actor); return;   // content/late_game/end_cast (the light pillar)
    case 1: risingStreaksUpdate(env, actor); return; // content/late_game/end_cast (its rising streaks)
    case 2: driftingPuffs(effect); return;
    case kWarningRingVariant: rockWarningRingUpdate(env, actor); return; // content/late_game/st5c_fx2
    case kLandingDustVariant: rockLandingDustUpdate(env, actor); return; // content/late_game/st5c_fx2
    default: notPortedAt(kStoryPort, "0x43ff80 variant past [0x88d998]", effect.variant);
    }
}

} // namespace od::port
