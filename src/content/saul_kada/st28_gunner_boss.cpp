// The fire ruin's gunner boss (ST28 sub 0's boss room; pool 0x20 type 0x51 sub 0, exe 0x4daf40; hypothesis names),
// spawned at the end of script 0x42 (record 0x8a8850); the room handler reads its hit points and its velocity's spare
// lane. States [0x895794]: 0 bind 0x4daff0 (the hit points of its row x 16, the boss gauge, its two helpers, sound
// 0x212), 1 run 0x4db0e0 (its damage while vulnerable, its sub-state [0x8957a0], the wall test 0x4dbd00), 2 dead
// 0x4db990 (story flag 0x4e, freed); the table goes on with the nine sub-states as states 3..11.
// Sub-states: 0 face 0x4db120 (turned to the player 0x1e ticks, then: a quarter of its hit points taken since the last
// time -> retreat, nearer than 0x400 -> spray, 0x600 -> volley, else advance), 1 advance 0x4db1d0 (at up to 0x200 a tick
// until within 0x400, then braking), 2 retreat 0x4db2d0 (back to its home (0x200, -0x140, 0x200), then the bombs (the
// player within 0x600) or the beam), 3 volley 0x4db440 (0x78 ticks aiming bone 6; each animation loop an enemy shot from
// one of three points), 4 spray 0x4db540 (0x3c ticks of shots from bone 4 at a random spread, on odd frames), 5 beam
// 0x4db670 (0x96 ticks of beams from bone 2 following the player; the shot catching him: its signal to the helpers),
// 6 bombs 0x4db800 (three from bone 6), 7 nothing, 8 death 0x4db940 (a defeat counted, then dead). The shadow is render
// output.
#include "content/saul_kada/st28_gunner_boss.hpp"
#include "content/saul_kada/st28_gunner_boss_state.hpp"
#include "content/saul_kada/st28_gunner_helpers_state.hpp"

#include "content/saul_kada/breath_puff.hpp"
#include "content/saul_kada/st28_gunner_beam.hpp"
#include "content/saul_kada/st28_gunner_bomb.hpp"
#include "content/saul_kada/st28_gunner_helpers.hpp"

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
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
#include "game/enemy_shot.hpp"
#include "game/game_state.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace st28_gunner_boss_state;

namespace {

enum class BossState : uint8_t { Bind = 0, Run = 1, Dead = 2 };
enum class BossSub : uint8_t { Face = 0, Advance = 1, Retreat = 2, Volley = 3, Spray = 4, Beam = 5, Bombs = 6, Rest = 7, Death = 8 };
enum class BossAnim : uint8_t {
    Idle = 0, WalkStart = 1, Walk = 2, WalkEnd = 3, BackStart = 4, Back = 5, BackEnd = 6, VolleyAim = 9, VolleyFire = 0xa,
    VolleyEnd = 0xb, SprayStart = 0xc, Spray = 0xd, SprayEnd = 0xe, BeamStart = 0xf, Beam = 0x10, BeamFire = 0x11,
    BeamEnd = 0x12, BombStart = 0x13, Bomb = 0x14, BombEnd = 0x15
};
constexpr uint8_t kBossDying = 0x01, kBossVulnerable = 0x04, kBossRetreatDue = 0x20;
constexpr uint8_t kBossBindSet = 0x1c; // its flags set at the bind
// its home point (exe data 0x895780: 0x200, -0x140, 0x200)
const DataTable<ScalarTable<int32_t, 3>> homePoint{TableId::St28GunnerBossHome};
const DataTable<ScalarTable<int8_t, 4>> spraySpread{TableId::St28GunnerBossSpraySpread};
// the helpers' offsets from it (x / z; table 0x89578c, by side)
struct HelperOffset : DataRecord {
    static constexpr uint32_t kBytes = 0x4;
    int16_t x = 0; // +0x0
    int16_t z = 0; // +0x2
    template <class R> static HelperOffset load(const R& r) {
        HelperOffset v;
        v.x = r.template read<int16_t>(0x0);
        v.z = r.template read<int16_t>(0x2);
        return v;
    }
};
constexpr uint32_t kFlagBossBeaten = 0x4e;
constexpr uint16_t kHurtSound = 0x8d, kGruntSound = 0x218, kBindSound = 0x212, kVolleySound = 0x214, kBeamSound = 0x211,
                   kBeamVoice = 0x215, kBombSound = 0x213, kBombDropSound = 0x210;
constexpr uint8_t kEnemyShotVariant = 1;

AnimatedActorStats bossStats(const AnimatedActor& actor, const char* who) {
    if (!hasAnimatedStats(actor.type)) notPorted(kStoryPort, who);
    return animatedStats(actor.type, actor.variant);
}
void sound(uint16_t id, const AnimatedActor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380
void enterSub(AnimatedActor& actor, BossSub sub) {
    actor.subState = uint8_t(sub);
    actor.restartStep();
}
void setAnim(AnimatedActor& actor, BossAnim anim) { actor.anim = uint8_t(anim); }
// exe 0x5e1970 on a vector given by value
void boneOffsetPoint(const AnimatedActor& actor, uint32_t bone, int x, int y, int z, int16_t out[3]) {
    const int16_t in[3] = {int16_t(x), int16_t(y), int16_t(z)};
    bonePointOffset(actor, bone, in, out);
}
// the "turn by at most step" of exe 0x4dbd50 / 0x4dbe20
void stepToward(int16_t& angle, int d, int step) {
    if (d >= step) angle = int16_t(uint16_t(uint16_t(angle) + uint32_t(step)));
    else if (d <= -step) angle = int16_t(uint16_t(uint16_t(angle) - uint32_t(step)));
    else angle = int16_t(uint16_t(uint16_t(angle) + uint32_t(d)));
}
void playerValues(int32_t out[3], int dy) {
    const Player& p = player();
    out[0] = p.pos.x;
    out[1] = int32_t((uint32_t(p.pos.y) & 0xffffu) | uint32_t(uint16_t(uint16_t(p.pos.y.whole) + uint16_t(dy))) << 16);
    out[2] = p.pos.z;
}
void positionValues(const AnimatedActor& actor, int32_t out[3]) {
    out[0] = actor.pos.x;
    out[1] = actor.pos.y;
    out[2] = actor.pos.z;
}

// exe 0x4dbd50(actor, pitchStep, yawStep): its pitch toward the player's (0x800 - the pitch to him), its yaw toward his
// angle, each by at most its step (0: not turned)
void facePlayer(AnimatedActor& actor, int pitchStep, int yawStep) {
    int32_t target[3], at[3];
    playerValues(target, 0);
    positionValues(actor, at);
    if (pitchStep != 0) {
        const int pitch = int16_t(uint16_t(0x800 - pitchOf(target, at)));
        stepToward(actor.rotation.x, angleDiff(uint32_t(int32_t(actor.rotation.x)), uint32_t(pitch)), pitchStep);
    }
    if (yawStep != 0)
        stepToward(actor.rotation.y,
                   angleDiff(uint32_t(int32_t(actor.rotation.y)),
                             uint32_t(int32_t(int16_t(angleOf(target[0], target[2], actor.pos.x, actor.pos.z))))),
                   yawStep);
}
// exe 0x4dbe20(actor, vector, bone, mode): the bone's point aimed at the player 0x5a above his feet: mode != 1 turns
// its pitch by at most 0x10; its yaw by at most 0x18 (mode 2) or 0x40
void aimBone(AnimatedActor& actor, int x, int y, int z, uint32_t bone, int mode) {
    int32_t target[3];
    playerValues(target, -0x5a);
    int16_t point[3];
    boneOffsetPoint(actor, bone, x, y, z, point);
    const int32_t from[3] = {int32_t(uint32_t(int32_t(point[0])) << 16), int32_t(uint32_t(int32_t(point[1])) << 16),
                             int32_t(uint32_t(int32_t(point[2])) << 16)};
    if (mode != 1) {
        const int pitch = int16_t(uint16_t(0x800 - pitchOf(target, from)));
        stepToward(actor.rotation.x, angleDiff(uint32_t(int32_t(actor.rotation.x)), uint32_t(pitch)), 0x10);
    }
    const int step = mode != 2 ? 0x40 : 0x18;
    stepToward(actor.rotation.y,
               angleDiff(uint32_t(int32_t(actor.rotation.y)),
                         uint32_t(int32_t(int16_t(angleOf(target[0], target[2], from[0], from[2]))))),
               step);
}
// exe 0x4dbc10: a hit of the player's: flashed, sound 0x8d (1 in 16 also 0x218), its hit points; dead -> its death
// once; else the damage adds up and past a quarter of its maximum the next pick retreats
void takeHit(AnimatedActor& actor, BossFields& boss) {
    const uint32_t hit = boss.bodyBox.result;
    const uint32_t damage = hit & kHitDamageMask;
    if (!(hit & kHitAttackByPlayer)) return;
    actor.tint = kTintFlash;
    sound(kHurtSound, actor);
    if ((rngStep() & 0xf) == 0) sound(kGruntSound, actor);
    if (hitPointsAdd(actor, -int(damage))) { // exe 0x5e52f0
        if (boss.flags & kBossDying) return;
        enterSub(actor, BossSub::Death);
        boss.flags |= kBossDying;
        return;
    }
    boss.damageTaken = uint16_t(boss.damageTaken + damage);
    const int16_t taken = int16_t(boss.damageTaken), quarter = int16_t(actor.maxHitPoints >> 2);
    if (taken <= quarter) return;
    boss.flags |= kBossRetreatDue;
    boss.damageTaken = uint16_t(taken - quarter);
}
// exe 0x4dbd00: the wall test; blocked -> pushed out (< 0: back), a wall noted
void wallTest(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) {
    boss.wallContact = 0;
    if (int32_t(actorTestMove(env, actor)) >= 0) return; // exe 0x40f570
    const uint32_t result = actorPushOut(env, actor);    // exe 0x410960
    if (int32_t(result) < 0) actor.pos = actor.prevPos;
    if (result & kWallOnSides) boss.wallContact = 1;
}
// exe 0x4dbf30(actor, k): an enemy shot (variant 1) from bone 6 at point k of three, its rotation, 0x40 ticks
void volleyShot(const AnimatedActor& actor, int k) {
    static constexpr int16_t kPoints[3][3] = {{0x20, 0xd0, -0x20}, {-0x20, 0xd0, -0x20}, {0, 0xd0, 0}};
    const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!slot) return;
    ProjectileActor& shot = projectileActorAt(slot);
    EnemyShotFields fields(projectileActorAt(slot));
    shot.loopFlags |= kActorSweepGroup2;
    shot.flags = kActorLiveDrawn;
    shot.type = kEnemyShotType;
    shot.variant = kEnemyShotVariant;
    fields.ticks = 0x40;
    fields.attack = bossStats(actor, "0x4dbf30: null type row (the exe faults)").strongAttack;
    if (k < 0 || k > 2)
        notPortedAt(kStoryPort, "0x4dbf30: point index outside the table (the exe reads its stack)", uint32_t(k));
    int16_t p[3];
    boneOffsetPoint(actor, 6, kPoints[k][0], kPoints[k][1], kPoints[k][2], p);
    shot.pos.x = int32_t(uint32_t(int32_t(p[0])) << 16);
    shot.pos.y = int32_t(uint32_t(int32_t(p[1])) << 16);
    shot.pos.z = int32_t(uint32_t(int32_t(p[2])) << 16);
    shot.rotation.x = actor.rotation.x;
    shot.rotation.y = actor.rotation.y;
    shot.rotation.z = actor.rotation.z;
}
// exe 0x4dc050(actor, dPitch, dYaw, flag): a spray shot at bone 4's point (0, 0xf0, 0), its rotation + (dPitch, dYaw)
void sprayShot(const AnimatedActor& actor, int dPitch, int dYaw, int flag) {
    int16_t p[3];
    boneOffsetPoint(actor, 4, 0, 0xf0, 0, p);
    const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!slot) return;
    ProjectileActor& shot = projectileActorAt(slot);
    BreathPuffParams fields(projectileActorAt(slot));
    shot.flags = kActorLive;
    shot.loopFlags |= kActorSweepGroup2;
    shot.type = kBreathPuffType;
    shot.variant = 0;
    shot.pos.x.whole = p[0];
    shot.pos.y.whole = p[1];
    shot.pos.z.whole = p[2];
    shot.rotation.x = int16_t(uint16_t(uint16_t(actor.rotation.x) + uint32_t(dPitch)));
    shot.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + uint32_t(dYaw)));
    shot.rotation.z = actor.rotation.z;
    fields.damage = bossStats(actor, "0x4dc050: null type row (the exe faults)").shotAttack;
    fields.frame = uint8_t(flag);
    fields.life = 0x18;
    shot.velocity.z = int16_t(0xfb80);
    fields.spin = 4;
    fields.size = 0x30;
}
// exe 0x4dc130: a bomb from bone 6's point (0, 0xd0, -0x10), (0, 0, -0x300), gravity 0x30
void dropBomb(const AnimatedActor& actor) {
    const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!slot) return;
    ProjectileActor& bomb = projectileActorAt(slot);
    bomb.loopFlags |= kActorSweepGroup2;
    bomb.flags = kActorLive;
    bomb.type = kGunnerBombType;
    bomb.variant = 0;
    GunnerBombParams(projectileActorAt(slot)).attack = int16_t(bossStats(actor, "0x4dc130: null type row (the exe faults)").bodyAttack);
    int16_t p[3];
    boneOffsetPoint(actor, 6, 0, 0xd0, -0x10, p);
    bomb.pos.x = int32_t(uint32_t(int32_t(p[0])) << 16);
    bomb.pos.y = int32_t(uint32_t(int32_t(p[1])) << 16);
    bomb.pos.z = int32_t(uint32_t(int32_t(p[2])) << 16);
    bomb.velocity.x = 0;
    bomb.velocity.y = 0;
    bomb.velocity.z = int16_t(0xfd00);
    bomb.rotation.x = actor.rotation.x;
    bomb.rotation.y = actor.rotation.y;
    bomb.rotation.z = actor.rotation.z;
    bomb.acceleration.y = 0x30;
}
// exe 0x4dc200(actor, variant): a beam at bone 2's point (0, 0, -0x40), its rotation; the beam's boss, its last beam
void fireBeam(const AnimatedActor& actor, BossFields& boss, GunnerBeamVariant variant) {
    const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!slot) return;
    ProjectileActor& beam = projectileActorAt(slot);
    beam.flags = kActorLive;
    beam.variant = uint8_t(variant);
    beam.type = kGunnerBeamType;
    beam.loopFlags |= kActorSweepGroup2;
    int16_t p[3];
    boneOffsetPoint(actor, 2, 0, 0, -0x40, p);
    beam.pos.x = int32_t(uint32_t(int32_t(p[0])) << 16);
    beam.pos.y = int32_t(uint32_t(int32_t(p[1])) << 16);
    beam.pos.z = int32_t(uint32_t(int32_t(p[2])) << 16);
    beam.rotation.x = actor.rotation.x;
    beam.rotation.y = actor.rotation.y;
    beam.rotation.z = actor.rotation.z;
    GunnerBeamFields(projectileActorAt(slot)).boss = actor.at;
    boss.beam = slot;
}
// exe 0x4dc2b0: its two helpers (variant 1, sides 0 / 1) at its point + their offsets, y 0x10, their boss
void spawnHelpers(const AnimatedActor& actor) {
    for (uint32_t i = 0; i < 2; ++i) {
        const uint32_t slot = allocActor20(); // exe 0x5e1160
        if (!slot) continue;
        const HelperOffset offset = dataRecord<HelperOffset>(stateAddress(TableId::St28GunnerBossHelperOffsets) + i * 4, HelperOffset::kBytes);
        AnimatedActor& helper = animatedActorAt(slot);
        helper.flags = kActorLiveDrawn;
        helper.loopFlags |= kActorSweepGroup2;
        helper.type = kGunnerHelperType;
        helper.subtype = kGunnerHelperSubtype;
        helper.variant = 1;
        GunnerHelperParams(animatedActorAt(slot)).side = int8_t(i);
        helper.pos.y = 0x100000;
        helper.pos.x = int32_t((uint32_t(int32_t(offset.x)) << 16) + uint32_t(actor.pos.x));
        helper.pos.z = int32_t((uint32_t(int32_t(offset.z)) << 16) + uint32_t(actor.pos.z));
        helper.rotation.x = actor.rotation.x;
        helper.rotation.y = actor.rotation.y;
        helper.rotation.z = actor.rotation.z;
        classState<st28_gunner_helpers_state::HelperFields>(animatedActorAt(slot)).boss = slotIdentityWord(actor);
    }
}
// exe 0x4db9b0: its hit spheres (the body's box: bone 0, bone 4 (0, 0x80, 0), bone 7 (-+0x60, 0x10, 0), bone 8; the
// arm's box: bone 6 (0x50, 0x60, 0))
void submitHits(const AnimatedActor& actor, BossFields& boss) {
    const AnimatedActorStats stats = bossStats(actor, "0x4db9b0: null type row (the exe faults)");
    boss.bodyBox.previousX = actor.prevPos.x.whole;
    boss.bodyBox.result = 0;
    boss.bodyBox.previousZ = actor.prevPos.z.whole;
    boss.armBox.previousX = actor.prevPos.x.whole;
    boss.armBox.previousZ = actor.prevPos.z.whole;
    boss.armBox.result = 0;
    const uint32_t owner = hitOwner(actor.slot) | hitHeightLevel(stats.hitHeight) | hitGroup(kHitGroupBodies);
    const uint32_t attack = uint32_t(stats.attack) | kHitAttackSolid;
    int16_t p[3];
    boneOffsetPoint(actor, 0, 0, 0, 0, p);
    hitListSubmit(p, boss.bodyBox, owner | hitRadius(0xa0), attack); // exe 0x5e4120
    boneOffsetPoint(actor, 4, 0, 0x80, 0, p);
    hitListSubmit(p, boss.bodyBox, owner | kHitNoLockOn | hitRadius(0x60), attack);
    boneOffsetPoint(actor, 6, 0x50, 0x60, 0, p);
    hitListSubmit(p, boss.armBox, owner | kHitNoLockOn | hitRadius(0x80), uint32_t(stats.attack) | kHitAttackDeflects);
    boneOffsetPoint(actor, 7, -0x60, 0x10, 0, p);
    hitListSubmit(p, boss.bodyBox, owner | kHitNoLockOn | hitRadius(0x60), attack);
    boneOffsetPoint(actor, 7, 0x60, 0x10, 0, p);
    hitListSubmit(p, boss.bodyBox, owner | kHitNoLockOn | hitRadius(0x60), attack);
    boneOffsetPoint(actor, 8, 0, 0, 0, p);
    hitListSubmit(p, boss.bodyBox, owner | kHitNoLockOn | hitRadius(0x60), attack);
}

// ---- the sub-states [0x8957a0] -------------------------------------------------------------------------------------
// exe 0x4db120: turned to the player 0x1e ticks, then the next sub-state picked
void face(AnimatedActor& actor, BossFields& boss) {
    facePlayer(actor, 0, 0x10);
    if (actor.stepCounter == 0) {
        boss.timer = 0;
        ++actor.stepCounter;
    } else {
        boss.timer += 1u;
        if (int8_t(boss.timer) < 0x1e) return;
    }
    const uint32_t d = uint32_t(planarDistance(actor.pos, player().pos));
    BossSub next;
    if (boss.flags & kBossRetreatDue) next = BossSub::Retreat;
    else if (d < 0x400) next = BossSub::Spray;
    else if (d < 0x600) next = BossSub::Volley;
    else next = BossSub::Advance;
    enterSub(actor, next);
    boss.timer = 0;
}
// its speed toward the walk's limit (accelerating), braking in the last step
void walkSpeed(AnimatedActor& actor, bool braking, int16_t limit) {
    const int16_t v = actor.velocity.z;
    if (!braking) {
        if (v <= limit) actor.velocity.z = limit;
        else actor.velocity.z = int16_t(uint16_t(v - actor.acceleration.z));
    } else {
        if (v < 0) actor.velocity.z = int16_t(uint16_t(v - actor.acceleration.z));
        else actor.velocity.z = 0;
    }
}
// exe 0x4db1d0: advancing on the player
void advance(AnimatedActor& actor) {
    switch (actor.step) {
    case 0:
        actor.velocity.z = 0;
        setAnim(actor, BossAnim::WalkStart);
        actor.acceleration.z = 0x20;
        actor.step = 1;
        break;
    case 1:
        if (actor.animEnded()) {
            setAnim(actor, BossAnim::Walk);
            actor.step = 2;
        }
        break;
    case 2:
        if (uint32_t(planarDistance(actor.pos, player().pos)) < 0x400) {
            setAnim(actor, BossAnim::WalkEnd);
            actor.acceleration.z = int16_t(0xffe0);
            actor.step = 3;
        }
        break;
    case 3:
        if (actor.animEnded() && actor.velocity.z == 0) {
            setAnim(actor, BossAnim::Idle);
            enterSub(actor, BossSub::Face);
            return;
        }
        break;
    default: break;
    }
    facePlayer(actor, 0, 0x10);
    walkSpeed(actor, actor.step == 3, -0x200);
    moveAlongYaw(actor.pos, actor.rotation.y, 0, 0, actor.velocity.z); // exe 0x5e36f0
}
int distanceHome(const AnimatedActor& actor) {
    const int32_t home[3] = {homePoint[0], 0, homePoint[2]};
    return planarDistance(actor.pos, home);
}
// the retreat's move (once started): its back to its home, walking (0x200; braking in its last step)
void retreatMove(AnimatedActor& actor) {
    if (actor.stepCounter == 0) return;
    const int angle = int16_t(angleOf(homePoint[0], homePoint[2], actor.pos.x, actor.pos.z));
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angle));
    actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + uint32_t(d + 0x800)));
    if (actor.step != 3) {
        actor.velocity.z = 0x200;
    } else {
        const int16_t v = actor.velocity.z;
        if (v < 0) actor.velocity.z = int16_t(uint16_t(v - actor.acceleration.z));
        else actor.velocity.z = 0;
    }
    moveAlongYaw(actor.pos, actor.rotation.y, 0, 0, actor.velocity.z); // exe 0x5e36f0
}
// exe 0x4db2d0: the retreat to its home
void retreat(AnimatedActor& actor) {
    switch (actor.step) {
    case 0:
        if (uint32_t(distanceHome(actor)) < 0x40) {
            actor.step = 3;
            actor.stepCounter = 0;
            return;
        }
        setAnim(actor, BossAnim::BackStart);
        actor.velocity.z = 0;
        actor.rotation.x = 0;
        actor.acceleration.z = 0x20;
        actor.step = 1;
        actor.stepCounter = 1;
        break;
    case 1:
        if (actor.animEnded()) {
            setAnim(actor, BossAnim::Back);
            actor.step = 2;
        }
        break;
    case 2:
        if (uint32_t(distanceHome(actor)) < 0x40) {
            setAnim(actor, BossAnim::BackEnd);
            actor.acceleration.z = int16_t(0xffe0);
            actor.step = 3;
        }
        break;
    case 3:
        if (actor.animEnded() && actor.velocity.z == 0) {
            enterSub(actor, uint32_t(planarDistance(actor.pos, player().pos)) < 0x600 ? BossSub::Bombs : BossSub::Beam);
            return;
        }
        break;
    default: break;
    }
    retreatMove(actor);
}
// exe 0x4db440: the volley (0x78 ticks; a shot each animation loop)
void volley(AnimatedActor& actor, BossFields& boss) {
    aimBone(actor, 0, 0xd0, 0, 6, 0);
    switch (actor.step) {
    case 0:
        setAnim(actor, BossAnim::VolleyAim);
        boss.timer = 0x78;
        boss.count = 0;
        sound(kVolleySound, actor);
        actor.step = 1;
        break;
    case 1:
        if (actor.animEnded()) {
            boss.count += 1u;
            volleyShot(actor, int(int8_t(boss.count)) % 3);
            setAnim(actor, BossAnim::VolleyFire);
            actor.prevAnim = 0xff;
        }
        boss.timer -= 1u;
        if (boss.timer != 0) break;
        setAnim(actor, BossAnim::VolleyEnd);
        actor.step = 2;
        break;
    case 2:
        if (actor.animEnded()) {
            enterSub(actor, BossSub::Face);
            setAnim(actor, BossAnim::Idle);
            boss.count = 0;
        }
        break;
    default: break;
    }
}
// exe 0x4db540: the spray (0x3c ticks; shots at a random spread on odd frames)
void spray(AnimatedActor& actor, BossFields& boss) {
    aimBone(actor, 0, 0xf0, 0, 4, 0);
    switch (actor.step) {
    case 0:
        setAnim(actor, BossAnim::SprayStart);
        boss.timer = 0x3c;
        actor.step = 1;
        break;
    case 1:
        if (actor.animEnded()) {
            setAnim(actor, BossAnim::Spray);
            actor.step = 2;
        }
        break;
    case 2: {
        const uint32_t r1 = rngStep();
        const int dPitch = spraySpread[r1 & 3];
        const uint32_t r2 = rngStep();
        const uint16_t frame = gameState().frame;
        if (frame & 1) sprayShot(actor, dPitch, spraySpread[r2 & 3], (frame >> 1) & 1);
        boss.timer -= 1u;
        if (boss.timer != 0) break;
        setAnim(actor, BossAnim::SprayEnd);
        actor.step = 3;
        break;
    }
    case 3:
        if (actor.animEnded()) {
            enterSub(actor, BossSub::Face);
            setAnim(actor, BossAnim::Idle);
        }
        break;
    default: break;
    }
}
// its last beam (the exe dereferences it without a test)
GunnerBeamFields lastBeam(const BossFields& boss) {
    if (boss.beam == 0) notPorted(kStoryPort, "0x4db670: no beam actor (the exe dereferences address 0)");
    return GunnerBeamFields(projectileActorAt(boss.beam));
}
// exe 0x4db670: the beam (0x96 ticks aiming bone 2; a shot each time the ray reports the player in it)
void beam(AnimatedActor& actor, BossFields& boss) {
    switch (actor.step) {
    case 0:
        setAnim(actor, BossAnim::BeamStart);
        boss.timer = 0x96;
        actor.step = 1;
        break;
    case 1:
        if (actor.animEnded()) {
            setAnim(actor, BossAnim::Beam);
            fireBeam(actor, boss, GunnerBeamVariant::Ray);
            actor.step = 2;
        }
        break;
    case 2:
        aimBone(actor, 0, -0x80, 0, 2, 2);
        boss.timer -= 1u;
        if (boss.timer == 0) {
            setAnim(actor, BossAnim::BeamEnd);
            actorHeaderAt(boss.beam).state = 2; // the ray freed
            actor.step = 5;
            break;
        }
        if (lastBeam(boss).signal == 0) break;
        fireBeam(actor, boss, GunnerBeamVariant::Shot);
        sound(kBeamSound, actor);
        sound(uint16_t((boss.timer & 1) + kBeamVoice), actor);
        setAnim(actor, BossAnim::BeamFire);
        actor.step = 3;
        break;
    case 3: {
        const GunnerBeamSignal signal = GunnerBeamSignal(lastBeam(boss).signal);
        if (signal == GunnerBeamSignal::Fire) {
            actor.step = 4;
        } else if (signal == GunnerBeamSignal::Caught) {
            GunnerBossParams(actor).caught = 1;
            actor.step = 4;
        }
        break;
    }
    case 4:
        if (actor.animEnded()) {
            GunnerBossParams(actor).caught = 0;
            setAnim(actor, BossAnim::BeamEnd);
            actor.step = 5;
        }
        break;
    case 5:
        if (actor.animEnded()) {
            boss.flags &= uint8_t(~kBossRetreatDue);
            enterSub(actor, BossSub::Face);
            setAnim(actor, BossAnim::Idle);
        }
        break;
    default: break;
    }
}
// exe 0x4db800: three bombs from bone 6
void bombs(AnimatedActor& actor, BossFields& boss) {
    aimBone(actor, 0, 0xd0, 0, 6, 1);
    switch (actor.step) {
    case 0:
        setAnim(actor, BossAnim::BombStart);
        actor.step = 1;
        boss.timer = 0;
        break;
    case 1:
        boss.timer += 1u;
        if (int8_t(boss.timer) <= 0x30) break;
        sound(kBombSound, actor);
        actor.step = 2;
        boss.timer = 0;
        break;
    case 2:
        if (boss.timer == 3) {
            if (actor.animEnded()) {
                setAnim(actor, BossAnim::BombEnd);
                actor.step = 3;
            }
            break;
        }
        if (!actor.animEnded()) break;
        dropBomb(actor);
        sound(kBombDropSound, actor);
        boss.timer += 1u;
        setAnim(actor, BossAnim::Bomb);
        actor.prevAnim = 0xff;
        break;
    case 3:
        if (actor.animEnded()) {
            boss.flags &= uint8_t(~kBossRetreatDue);
            enterSub(actor, BossSub::Face);
            setAnim(actor, BossAnim::Idle);
        }
        break;
    default: break;
    }
}
// exe 0x4db940: its death (a defeat counted; a tick later dead)
void death(AnimatedActor& actor, BossFields& boss) {
    switch (actor.step) {
    case 0:
        countDefeat(); // exe 0x5e53e0
        boss.dead = 1;
        boss.timer = 1;
        ++actor.step;
        break;
    case 1:
        boss.timer -= 1u;
        if (boss.timer != 0) break;
        actor.state = uint8_t(BossState::Dead);
        enterSub(actor, BossSub::Face);
        break;
    default: break;
    }
}
void runSub(AnimatedActor& actor, BossFields& boss, uint32_t n) { // [0x8957a0]
    switch (BossSub(n)) {
    case BossSub::Face: face(actor, boss); return;
    case BossSub::Advance: advance(actor); return;
    case BossSub::Retreat: retreat(actor); return;
    case BossSub::Volley: volley(actor, boss); return;
    case BossSub::Spray: spray(actor, boss); return;
    case BossSub::Beam: beam(actor, boss); return;
    case BossSub::Bombs: bombs(actor, boss); return;
    case BossSub::Rest: return; // exe 0x52ef00
    case BossSub::Death: death(actor, boss); return;
    default: notPortedAt(kStoryPort, "0x4daf40 sub-state past [0x8957a0]", n);
    }
}
// exe 0x4daff0: its model, the hit points of its row x 16, its box, the boss gauge, its helpers
void bind(AnimatedActor& actor, BossFields& boss) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn;
    setAnim(actor, BossAnim::Idle);
    actor.prevAnim = 0xff;
    const uint16_t hp =
        hasAnimatedStats(actor.type) ? uint16_t(animatedStats(actor.type, actor.variant).hitPoints << 4) : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.velocity.setXY(0, 0);
    actor.velocity.setZSpare(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.collisionBox.pointAt(TableId::St28GunnerBossBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.setParamsWord(0);
    actor.clearClassBlock(); // exe 0x40dc40
    boss.dead = 0;
    boss.damageTaken = 0;
    boss.flags = uint8_t((boss.flags & ~kBossRetreatDue) | kBossBindSet);
    hudAddBossGauge(actor, 0, 4, 0x2222aa); // exe 0x415d30
    spawnHelpers(actor);
    sound(kBindSound, actor);
    actor.restartStep();
    actor.state = uint8_t(BossState::Run);
    actor.subState = uint8_t(BossSub::Advance);
}
// exe 0x4db0e0
void run(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) {
    if (actor.hitPoints >= 0 && (boss.flags & kBossVulnerable)) takeHit(actor, boss);
    runSub(actor, boss, actor.subState);
    wallTest(env, actor, boss);
}
void gunnerBoss(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x4daf40
    BossFields& boss = classState<BossFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    const uint32_t state = actor.state;
    switch (state) { // [0x895794]
    case 0: bind(actor, boss); break;
    case 1: run(env, actor, boss); break;
    case 2: // exe 0x4db990
        setStoryFlag(kFlagBossBeaten); // exe 0x510e10
        freeActor(actor);
        break;
    default:
        if (state > 11) notPortedAt(kStoryPort, "0x4daf40 state past [0x895794]", state);
        runSub(actor, boss, state - 3); // the table continues with the sub-states
        break;
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStepPose(actor); // exe 0x5e1e80
    else animSetPose(actor, actor.anim, 0);                // exe 0x5e1c30
    submitHits(actor, boss);
    // drawn: the shadow (exe 0x5f20f0(actor, 0x100, 0xc0, 0)) -- render output
}

} // namespace

void gunnerBossUpdate(const PlayerEnv& env, uint32_t actor) { gunnerBoss(env, animatedActorAt(actor)); }

} // namespace od::port
