// The guardian of the ruin under Nino (ST35) and its attacks, one routine per exe routine; the names are hypotheses from
// the observed behaviour.
//   0x4b1180  pool 0x20 type 0x3a sub 0: the guardian (states [0x894484]: 0x4b12f0 init, 0x4b13b0 the fight by its
//             sub-state [0x894494], 0x4b23a0 beaten, none). It picks its attacks: a volley of shots (pool 0x40 type 9
//             from its head), homing orbs (and at low life a jump with the big one), the ring of orbs, mines it sets off
//             later, the twin beams. Variant 0 = ST35 sub 5 (beaten -> flag 0x94), variant 1 = sub 0 (-> flag 0x95).
//   0x5b9910  pool 0x60 type 0x56: a beam (its variant's row [0x8d0ba0 + (variant & 0xf) * 0x1c]: the ring colours, the
//             beam colours, the length, the speed, the life, the width, no hit, the radius; variant 0 a looping sound,
//             bit 0x80 a life, 0xf0 it slides back at full length); states [0x8d0b90]
//   0x5cd240  pool 0x40 type 0x26: the guardian's orbs (states [0x8d170c]; by the variant [0x8d171c]: 1 the 4 drifting
//             / homing orbs, 2 the big one (trail rings), 3 the ring of 8, 4 the floating mine (a model))
// Render output left out as in the other ports: the shadows (exe 0x5f20f0).
#include "content/nino/ruin_guardian.hpp"
#include "content/nino/ruin_guardian_state.hpp"

#include <algorithm>
#include <cstdlib>

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
#include "game/draw_commands.hpp"
#include "game/encounter.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/vectors.hpp"

namespace od::port {

using namespace ruin_guardian_state;

namespace {

int16_t t16(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }
// the camera-input point (s16 << the stage shift) of a game-unit point
void camPoint(const int16_t p[3], int16_t out[3]) {
    const int sh = stage().coordinateShift & 0x1f;
    for (int k = 0; k < 3; ++k) out[k] = t16(p[k] << sh);
}
// a 16.16 point moved along a yaw (exe 0x5e36f0 on a stack copy)
void moveAlongYaw32(int32_t p[3], int yaw, int x, int y, int z) {
    int32_t d[3];
    rotateByYaw(yaw, x, y, z, d);
    for (int k = 0; k < 3; ++k) p[k] = int32_t(uint32_t(p[k]) + uint32_t(d[k]));
}
bool blockedOut(const PlayerEnv& env, Actor& actor) { // blocked, and its push-out met a wall / a floor
    return int32_t(actorTestMove(env, actor)) < 0 && (actorPushOut(env, actor) & (kWallOnSides | kWallOnY));
}

// ==== 0x4b1180: the guardian ==========================================================================================
enum class GuardianState : uint8_t { Init = 0, Fight = 1, Beaten = 2, Done = 3 };
// the fight's sub-states [0x894494]
enum class GuardianSub : uint8_t {
    Choose = 0, Volley = 1, Orbs = 2, OrbsB = 3, Ring = 4, Mines = 5, Detonate = 6, Beams = 7,
};
constexpr uint8_t kGuardianNoHitEntry = 0x01, kGuardianNoDamage = 0x02, kGuardianBusy = 0x20, kGuardianDying = 0x80;
// The guardian's stats row (by type and variant): its attacks' damages, its hit radius
struct GuardianStats : AnimatedActorStats {
    static constexpr uint32_t kBytes = 0xf;
    uint8_t radiusBytes = 0; // +0x8
    uint8_t volleyDamage = 0; // +0xc
    uint8_t orbDamage = 0; // +0xd
    uint8_t bigOrbDamage = 0; // +0xe
    template <class R> static GuardianStats load(const R& r) {
        GuardianStats v;
        static_cast<AnimatedActorStats&>(v) = AnimatedActorStats::load(r);
        v.radiusBytes = r.template read<uint8_t>(0x8);
        v.volleyDamage = r.template read<uint8_t>(0xc);
        v.orbDamage = r.template read<uint8_t>(0xd);
        v.bigOrbDamage = r.template read<uint8_t>(0xe);
        return v;
    }
};
GuardianStats guardianStats(const AnimatedActor& g) { return dataRecord<GuardianStats>(animatedStatRow(g.type, g.variant), GuardianStats::kBytes); }
// The volley shot's spawn parameters (the stage's pool-0x40 type 9): its life and its damage
struct VolleyShotParams {
    explicit VolleyShotParams(const ProjectileActor& actor) : VolleyShotParams(const_cast<ProjectileActor&>(actor)) {}
    explicit VolleyShotParams(ProjectileActor& actor) : life(actor.params, 0), damage(actor.params, 2) {}
    BytesField<uint16_t, 4> life;
    BytesField<uint16_t, 4> damage;
};
// The orb's spawn parameters: its launch point's index, its sound countdown; its guardian
struct OrbParams {
    explicit OrbParams(const ProjectileActor& actor) : OrbParams(const_cast<ProjectileActor&>(actor)) {}
    explicit OrbParams(ProjectileActor& actor) : launch(actor.params[0]), soundTicks(actor.params[1]), guardian(actor.hitBox.result) {}
    FieldAlias<int8_t, uint8_t> launch;
    uint8_t& soundTicks;
    uint32_t& guardian;
};
constexpr uint8_t kGuardianType = 0x3a;
constexpr uint8_t kVolleyShotType = 9, kVolleyShotVariant = 0x8d, kOrbType = 0x26, kBeamType = 0x56;
enum class OrbVariant : uint8_t { None = 0, Drift = 1, Big = 2, Ring = 3, Mine = 4 };
const DataTable<ScalarTable<uint8_t, 16>> choiceA{TableId::RuinGuardianChoiceA}; // variant 0's: 0 -> the volley
const DataTable<ScalarTable<uint8_t, 8>> choiceB{TableId::RuinGuardianChoiceB}; // variant 1's attack kinds
const DataTable<ScalarTable<uint8_t, 8>> volleyCounts{TableId::RuinGuardianVolleyCounts};
const DataTable<ScalarTable<uint16_t, 8>> turnTicks{TableId::RuinGuardianTurnTicks};
const DataTable<ScalarTable<uint16_t, 2>> orbVoices{TableId::RuinGuardianOrbVoices};
const DataTable<ScalarTable<uint16_t, 4>> hurtVoices{TableId::RuinGuardianHurtVoices};
const DataTable<ScalarTable<int16_t, 8>> leadFactors{TableId::RuinGuardianLeadFactors}; // the aim's lead by the distance
const DataTable<CollisionBoxRecord> guardianBox{TableId::RuinGuardianBox};
constexpr uint32_t kFlagFirstBeaten = 0x94, kFlagSecondBeaten = 0x95;
constexpr uint32_t kGaugeColour = 0xff63de;
constexpr uint8_t kSlowedStatus = uint8_t(kStatusSlowed | kStatusSlowedFloor);

// exe 0x4b25b0 / 0x4b2600(actor, d, step): the head's turn / pitch toward d by at most step; 0 = reached
int ease(int16_t& field, int d, int step) {
    if (uint32_t(d + step) <= uint32_t(step * 2)) {
        field = int16_t(uint16_t(field) + uint16_t(d));
        return 0;
    }
    if (d > step) field = int16_t(uint16_t(field) + uint16_t(step));
    else field = int16_t(uint16_t(field) - uint16_t(step));
    return 1;
}
// exe 0x4b2450: the head aims at the player (the pitch toward its chest, the yaw toward where it moves to); 0 = on target
int aimHead(const AnimatedActor& g, GuardianFields& fields) {
    const Player& p = player();
    int32_t target[3] = {p.pos.x, p.pos.y, p.pos.z};
    const ShortVec4& head = bonePoint(g, 7, kStoryPort); // exe 0x5e1810
    // the head as a 16.16 point (its fractions: the exe's stale stack, 0 here)
    const int32_t from[3] = {int32_t(uint32_t(uint16_t(head.x)) << 16), int32_t(uint32_t(uint16_t(head.y)) << 16),
                             int32_t(uint32_t(uint16_t(head.z)) << 16)};
    const int16_t chestY = t16(uint16_t(p.pos.y.whole) - 0x64);
    const int16_t dist = t16(planarDistance(p.pos, from));
    int d = angleDiff(uint32_t(int32_t(fields.headPitch)), uint32_t(angleOf(head.y, 0, chestY, dist)));
    if (d < 0) d = 0;
    const int pitchLeft = ease(fields.headPitch, d, 0x80);
    int k = int(dist) >> 8;
    if (k > 7) k = 7;
    const int f = leadFactors[size_t(k)];
    moveAlongYaw32(target, p.rotation.y,
                   int(uint32_t((p.pos.x.whole - p.prevPos.x.whole) * f) << 4), 0,
                   int(uint32_t((p.pos.z.whole - p.prevPos.z.whole) * f) << 4));
    const int angle = angleOf(target[0], target[2], from[0], from[2]);
    const int d2 = angleDiff(uint32_t(fields.headYaw + g.rotation.y), uint32_t(angle)) + fields.aimOffset;
    return ease(fields.headYaw, d2, 0x80) | pitchLeft;
}
// exe 0x4b23d0: the damage taken: the hurt voice (a cooldown), the flash, the hit points; -1 = beaten
int guardianDamage(AnimatedActor& g, GuardianFields& fields) {
    const uint32_t dmg = fields.hit.result & kHitDamageMask;
    if (dmg == 0) return 0;
    if (fields.hurtVoiceTicks == 0) {
        fields.hurtVoiceTicks = uint16_t((crtRand() & 0x7f) + 0x12c);
        soundPlayAtPos(hurtVoices[crtRand() & 3], g.pos);
    }
    g.tint = kTintFlash;
    if (hitPointsAdd(g, -int(dmg)) != 0) return -1; // exe 0x5e52f0
    return 0;
}
// facing the player (the yaw within 0x600 of the angle from the player)
bool facingPlayer(const AnimatedActor& g) {
    return uint32_t(angleDiff(uint32_t(int32_t(g.rotation.y)), uint32_t(angleOf(player().pos, g.pos))) + 0x600) <
           0xc00u;
}
void attack(AnimatedActor& g, GuardianSub sub) { g.enterState(uint8_t(GuardianState::Fight), uint8_t(sub)); }
// exe 0x4b14b0: a pause, then the choice of the next attack
void choose(AnimatedActor& g, GuardianFields& fields) {
    switch (g.step) {
    case 0:
        fields.mode = 0;
        g.enterStep(1);
        g.anim = 0;
        fields.ticks = uint16_t((crtRand() & 7) + 0xa);
        return;
    case 1:
        fields.ticks -= 1u;
        if (fields.ticks == 0) g.enterStep(2);
        return;
    case 2:
        if (g.variant == 0) {
            if ((uint8_t(player().status) & kSlowedStatus) && g.hitPoints >= g.maxHitPoints / 3) {
                attack(g, GuardianSub::Volley);
                return;
            }
            if (choiceA[crtRand() & 0xf] == 0) {
                attack(g, GuardianSub::Volley);
                return;
            }
            attack(g, facingPlayer(g) ? GuardianSub::Orbs : GuardianSub::Volley);
            return;
        }
        switch (choiceB[crtRand() & 7]) {
        case 0: attack(g, GuardianSub::Volley); return;
        case 1: attack(g, facingPlayer(g) ? GuardianSub::Ring : GuardianSub::Volley); return;
        case 2:
            if (enemyGroupMarks(int32_t(fields.mineMarks), EnemyGroupMarks::LastTick) != 0) // exe 0x5e6d60
                attack(g, GuardianSub::Detonate);
            else attack(g, fields.minesLive > 8 ? GuardianSub::Volley : GuardianSub::Mines);
            return;
        case 3: attack(g, GuardianSub::Beams); return;
        default: return;
        }
    default: return;
    }
}
// exe 0x4b1650: the head aims, then a volley of 3 shots (pool 0x40 type 9 variant 0x8d from the head), the volleys
// counted down; then the head back
void volley(AnimatedActor& g, GuardianFields& fields) {
    switch (g.step) {
    case 0:
        fields.mode = kGuardianBusy;
        g.enterStep(1);
        g.anim = 2;
        fields.volleys = volleyCounts[crtRand() & 7];
        fields.aimOffset = int16_t(0x20 - int(crtRand() & 0x3f));
        fields.ticks = 0;
        soundPlayAtPos(0x1f1, g.pos);
        [[fallthrough]];
    case 1:
        if (fields.ticks != 0) {
            fields.ticks -= 1u;
            return;
        }
        if (aimHead(g, fields) != 0) return;
        g.step += 1u;
        fields.shotsLeft = 3;
        fields.ticks = 0;
        return;
    case 2: {
        if (!g.animEnded()) return;
        const uint16_t t = fields.ticks;
        fields.ticks = uint16_t(t - 1u);
        if (t != 0) return;
        fields.ticks = 2;
        fields.shotsLeft -= 1u;
        if (fields.shotsLeft == 0) {
            g.anim = 3;
            g.prevAnim = 0xff;
            g.step += 1u;
        }
        const uint32_t s = allocActor40(kActorStageClass); // exe 0x5e1280
        if (!s) return;
        const int16_t muzzle[3] = {0, 0x85, 0};
        int16_t out[3];
        bonePointOffset(g, 7, muzzle, out);
        ProjectileActor& shot = projectileActorAt(s);
        VolleyShotParams params(projectileActorAt(s));
        shot.flags = kActorLive;
        shot.loopFlags |= kActorSweepGroup2;
        shot.type = kVolleyShotType;
        shot.variant = kVolleyShotVariant;
        shot.pos.x.whole = out[0];
        shot.pos.y.whole = out[1];
        shot.pos.z.whole = out[2];
        shot.rotation.x = fields.headPitch;
        shot.rotation.z = 0;
        params.life = 0x32;
        shot.rotation.y = int16_t(uint16_t(g.rotation.y) + uint16_t(fields.headYaw));
        params.damage = guardianStats(g).volleyDamage;
        return;
    }
    case 3:
        if (!g.animEnded()) return;
        fields.volleys -= 1u;
        if (fields.volleys == 0) {
            g.anim = 4;
            g.step += 1u;
            return;
        }
        g.step = 1;
        fields.ticks = 2;
        fields.aimOffset = int16_t(0x20 - int(crtRand() & 0x3f));
        return;
    case 4: {
        const int yawLeft = ease(fields.headYaw, angleDiff(uint32_t(int32_t(fields.headYaw)), 0), 0x80);
        const int pitchLeft = ease(fields.headPitch, angleDiff(uint32_t(int32_t(fields.headPitch)), 0), 0x80);
        if (g.animEnded() && (yawLeft | pitchLeft) == 0) g.enterState(uint8_t(GuardianState::Fight));
        return;
    }
    default: return;
    }
}
// an orb (pool 0x40 type 0x26) of the variant (its start puts it at its launch point on the guardian)
uint32_t spawnOrb(OrbVariant variant) {
    const uint32_t s = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!s) return 0;
    ProjectileActor& orb = projectileActorAt(s);
    orb.flags = kActorLive;
    orb.loopFlags |= kActorSweepGroup2;
    orb.type = kOrbType;
    orb.variant = uint8_t(variant);
    return s;
}
// exe 0x4b18f0: it turns to the player, then 4 homing orbs; at low life (the player slowed, a coin toss) a jump and
// the big one on landing
void orbs(const PlayerEnv& env, AnimatedActor& g, GuardianFields& fields) {
    switch (g.step) {
    case 0:
        fields.mode = kGuardianBusy;
        g.step += 1u;
        g.anim = 1;
        fields.ticks = turnTicks[crtRand() & 7];
        soundPlayAtPos(orbVoices[crtRand() & 1], g.pos);
        [[fallthrough]];
    case 1: {
        const int angle = angleOf(player().pos, g.pos);
        if (uint32_t(angleDiff(uint32_t(int32_t(g.rotation.y)), uint32_t(angle)) + 0x200) < 0x400u) {
            g.anim = 5;
            g.step += 1u;
            return;
        }
        fields.ticks -= 1u;
        if (fields.ticks == 0) attack(g, GuardianSub::Volley);
        turnYawTo(g.rotation.y, uint32_t(angle), 0x40);
        return;
    }
    case 2:
        if (!g.animEnded()) return;
        g.step += 1u;
        if (g.hitPoints < g.maxHitPoints / 3 && (crtRand() & 1) && (uint8_t(player().status) & kSlowedStatus)) {
            g.anim = 7;
            g.velocity.y = -0x100;
            g.acceleration.y = 0x15;
            fields.aimOffset = int16_t(0x40 - int(crtRand() & 0x7f));
        } else {
            g.anim = 6;
        }
        for (uint32_t i = 0; i < 4; ++i) {
            const uint32_t s = spawnOrb(OrbVariant::Drift);
            if (!s) continue;
            OrbParams params(projectileActorAt(s));
            ProjectileActor& orb = projectileActorAt(s);
            orb.params[0] = uint8_t(i);
            orb.params[1] = 0;
            params.guardian = slotIdentityWord(g);
            soundPlayAtPos(0x1e6, orb.pos);
        }
        return;
    case 3: {
        if (g.anim != 7) {
            if (g.animEnded()) g.enterState(uint8_t(GuardianState::Fight));
            return;
        }
        if (g.animEnded()) {
            g.step += 1u;
            g.anim = 8;
            fields.groundY = int16_t(actorGround(env, g)); // exe 0x40f190
        }
        const uint16_t vy = uint16_t(g.velocity.y);
        g.velocity.y = int16_t(uint16_t(vy + uint16_t(g.acceleration.y)));
        g.pos.y.whole = int16_t(uint16_t(g.pos.y.whole) + uint16_t(int16_t(vy) >> 4));
        turnYawTo(g.rotation.y, uint32_t(angleOf(player().pos, g.pos) + fields.aimOffset), 0x20);
        return;
    }
    case 4: {
        const uint16_t vy = uint16_t(g.velocity.y);
        g.pos.y.whole = int16_t(uint16_t(g.pos.y.whole) + uint16_t(int16_t(vy) >> 4));
        g.velocity.y = int16_t(uint16_t(vy + uint16_t(g.acceleration.y)));
        turnYawTo(g.rotation.y, uint32_t(angleOf(player().pos, g.pos)), 0x20);
        if (fields.groundY > g.pos.y.whole) return;
        g.step += 1u;
        g.anim = 9;
        g.pos.y.whole = fields.groundY;
        return;
    }
    case 5: {
        if (!g.animEnded()) return;
        if (const uint32_t s = spawnOrb(OrbVariant::Big)) {
            ProjectileActor& orb = projectileActorAt(s);
            orb.params[0] = 4;
            OrbParams(projectileActorAt(s)).guardian = slotIdentityWord(g);
            soundPlayAtPos(0x1eb, orb.pos);
        }
        g.step += 1u;
        fields.ticks = 0xa;
        soundPlayAtPos(0x1f4, g.pos);
        return;
    }
    case 6:
        fields.ticks -= 1u;
        if (fields.ticks != 0) return;
        g.anim = 0xa;
        g.step += 1u;
        return;
    case 7:
        if (g.animEnded()) g.enterState(uint8_t(GuardianState::Fight));
        return;
    default: return;
    }
}
// exe 0x4b1c30: it turns to the player, one ring of orbs (type 0x26 variant 3)
void ring(AnimatedActor& g, GuardianFields& fields) {
    switch (g.step) {
    case 0:
        fields.mode = kGuardianBusy;
        g.step += 1u;
        g.anim = 1;
        fields.ticks = turnTicks[crtRand() & 7];
        [[fallthrough]];
    case 1: {
        const int angle = angleOf(player().pos, g.pos);
        if (uint32_t(angleDiff(uint32_t(int32_t(g.rotation.y)), uint32_t(angle)) + 0x200) < 0x400u) {
            g.step += 1u;
            g.anim = 0xb;
            soundPlayAtPos(0x1f5, g.pos);
            return;
        }
        fields.ticks -= 1u;
        if (fields.ticks == 0) attack(g, GuardianSub::Volley);
        turnYawTo(g.rotation.y, uint32_t(angle), 0x40);
        return;
    }
    case 2: {
        if (!g.animEnded()) return;
        g.step += 1u;
        g.anim = 0xc;
        fields.ticks = 0x1e;
        const uint32_t s = spawnOrb(OrbVariant::Ring);
        if (!s) return;
        ProjectileActor& orb = projectileActorAt(s);
        orb.setParamsWord(5);
        OrbParams(projectileActorAt(s)).guardian = slotIdentityWord(g);
        soundPlayAtPos(0x1e8, orb.pos);
        return;
    }
    case 3:
        fields.ticks -= 1u;
        if (fields.ticks != 0) return;
        g.step += 1u;
        g.anim = 0xd;
        soundPlayAtPos(0x1f6, g.pos);
        return;
    case 4:
        if (g.animEnded()) g.enterState(uint8_t(GuardianState::Fight));
        return;
    default: return;
    }
}
// exe 0x4b1dc0: the head sweeps, dropping mines (type 0x26 variant 4) at random yaws, 8 times
void mines(AnimatedActor& g, GuardianFields& fields) {
    switch (g.step) {
    case 0:
        fields.mode = kGuardianBusy;
        g.enterStep(1);
        g.anim = 2;
        fields.ticks = 8;
        soundPlayAtPos(0x1f8, g.pos);
        return;
    case 1: {
        g.step += 1u;
        const uint32_t k = g.stepCounter;
        g.stepCounter = uint8_t(k + 1u);
        fields.sweepYaw = int16_t(uint16_t((k << 9) - (crtRand() & 0x1ff) - 1u));
        [[fallthrough]];
    }
    case 2: {
        if (ease(fields.headYaw, angleDiff(uint32_t(int32_t(fields.headYaw)),
                                           uint32_t(int32_t(fields.sweepYaw))), 0x80) != 0)
            return;
        if (!g.animEnded()) return;
        g.anim = 3;
        g.step += 1u;
        const uint32_t s = allocActor40(kActorStageClass);
        if (!s) return;
        fields.minesLive += 1u;
        ProjectileActor& mine = projectileActorAt(s);
        mine.flags = kActorLive;
        mine.type = kOrbType;
        mine.variant = uint8_t(OrbVariant::Mine);
        mine.loopFlags |= kActorSweepGroup2;
        mine.params[0] = 6;
        mine.rotation.y = int16_t(uint16_t(g.rotation.y) + uint16_t(fields.headYaw));
        OrbParams(projectileActorAt(s)).guardian = slotIdentityWord(g);
        soundPlayAtPos(0x1ea, mine.pos);
        return;
    }
    case 3:
        if (!g.animEnded()) return;
        fields.ticks -= 1u;
        if (fields.ticks == 0) {
            g.anim = 4;
            g.step += 1u;
        } else {
            g.step = 1;
            g.anim = 2;
        }
        return;
    case 4:
        if (ease(fields.headYaw, angleDiff(uint32_t(int32_t(fields.headYaw)), 0), 0x80) == 0 && g.animEnded())
            g.enterState(uint8_t(GuardianState::Fight));
        return;
    default: return;
    }
}
// exe 0x4b1f60: a pause, then its enemy-group entry marked (the mines go off)
void detonate(AnimatedActor& g, GuardianFields& fields) {
    switch (g.step) {
    case 0:
        fields.mode = kGuardianBusy;
        g.enterStep(1);
        g.anim = 0xe;
        fields.ticks = uint16_t((crtRand() & 0x1f) + 5);
        soundPlayAtPos(0x1f7, g.pos);
        return;
    case 1:
        if (!g.animEnded()) return;
        fields.ticks -= 1u;
        if (fields.ticks != 0) return;
        g.anim = 0xf;
        g.step += 1u;
        enemyGroupMark(int32_t(fields.mineGroup), 1);
        return;
    case 2:
        if (g.animEnded()) g.enterState(uint8_t(GuardianState::Fight));
        return;
    default: return;
    }
}

// A beam's row (exe data, 0x1c bytes)
struct BeamRow : DataRecord {
    static constexpr uint32_t kBytes = 0x1b;
    uint32_t ringColour = 0; // +0x0
    uint32_t ringCentre = 0; // +0x4
    uint32_t colourA = 0; // +0x8
    uint32_t colourB = 0; // +0xc
    uint16_t longest = 0; // +0x10
    uint16_t speed = 0; // +0x14
    uint16_t life = 0; // +0x16
    uint8_t width = 0; // +0x18
    uint8_t noHit = 0; // +0x19
    uint8_t radius = 0; // +0x1a
    template <class R> static BeamRow load(const R& r) {
        BeamRow v;
        v.ringColour = r.template read<uint32_t>(0x0);
        v.ringCentre = r.template read<uint32_t>(0x4);
        v.colourA = r.template read<uint32_t>(0x8);
        v.colourB = r.template read<uint32_t>(0xc);
        v.longest = r.template read<uint16_t>(0x10);
        v.speed = r.template read<uint16_t>(0x14);
        v.life = r.template read<uint16_t>(0x16);
        v.width = r.template read<uint8_t>(0x18);
        v.noHit = r.template read<uint8_t>(0x19);
        v.radius = r.template read<uint8_t>(0x1a);
        return v;
    }
};
BeamRow beamRow(const StaticActor& beam) { return dataRecord<BeamRow>(stateAddress(TableId::RuinGuardianBeamRows) + (beam.variant & 0xfu) * 0x1c, BeamRow::kBytes); }
enum class BeamState : uint8_t { Start = 0, Shine = 1, Fade = 2 };
constexpr uint16_t kBeamLoopSound = 0x264;

// exe 0x5b9c40: two rings (draw command 0xf0) along the beam's axis at its start and one length further; the last point
// one length further, the length backwards kept
void beamRings(StaticActor& beam, BeamFields& fields) {
    const BeamRow row = beamRow(beam);
    const int32_t x = beam.pos.x, y = beam.pos.y, z = beam.pos.z;
    const int32_t back = -fields.length;
    const uint32_t ring = (row.ringColour & 0xffffffu) | 0x23000000u;
    fields.lastPoint.x = beam.pos.x.whole;
    fields.lastPoint.y = beam.pos.y.whole;
    fields.lastPoint.z = beam.pos.z.whole;
    for (int i = 0; i < 2; ++i) {
        dc::emit({0x4000u, row.ringCentre | 0xf0000000u, dc::packXY(beam.pos.x.whole, beam.pos.y.whole), ring,
                  uint32_t(uint16_t(beam.pos.z.whole)), uint32_t(int32_t(fields.width)) << 16},
                 beam.at);
        fields.lastPoint.x = beam.pos.x.whole;
        fields.lastPoint.y = beam.pos.y.whole;
        fields.lastPoint.z = beam.pos.z.whole;
        moveAlongRotation(beam.pos, beam.rotation, 0, 0, int(uint32_t(back) << 4)); // exe 0x5e37b0
    }
    beam.pos.x = x;
    beam.pos.y = y;
    beam.pos.z = z;
    fields.backLength = back;
}
// exe 0x5b9d90: the beam in 0x80-unit segments, each as 4 additive POLY_G4 (the centre colour A, the edge colour B,
// up / down / right / left of the projected axis, the projected width). Port: dc::otQuadG4, the offsets = the width <<
// the stage shift
void beamBody(StaticActor& beam, BeamFields& fields) {
    const BeamRow row = beamRow(beam);
    int32_t len = -fields.backLength;
    int n = len / 0x80;
    if (len % 0x80 != 0) ++n;
    n = int8_t(n);
    const int32_t x = beam.pos.x, y = beam.pos.y, z = beam.pos.z;
    const int width = fields.width;
    const int sh = stage().coordinateShift & 0x1f;
    const float u = float(width << sh);
    const uint32_t cA = (row.colourA & 0xffffffu) | 0x3a000000u, cB = (row.colourB & 0xffffffu) | 0x3a000000u;
    for (int k = 0; k < n; ++k) {
        int16_t p0[3] = {beam.pos.x.whole, beam.pos.y.whole, beam.pos.z.whole}, p1[3];
        int16_t s0[2], s1[2], d0 = 0, d1 = 0;
        projectOnScreen(p0, s0);
        const int w0 = projectedWidth(p0, int16_t(width), &d0);
        if (len > 0x80) {
            moveAlongRotation(beam.pos, beam.rotation, 0, 0, -0x800);
            len -= 0x80;
            p1[0] = beam.pos.x.whole;
            p1[1] = beam.pos.y.whole;
            p1[2] = beam.pos.z.whole;
        } else {
            p1[0] = fields.lastPoint.x;
            p1[1] = fields.lastPoint.y;
            p1[2] = fields.lastPoint.z;
        }
        projectOnScreen(p1, s1);
        const int w1 = projectedWidth(p1, int16_t(width), &d1);
        const bool clip0 = uint16_t(s0[0]) == 0x8000 && uint16_t(s0[1]) == 0x8000;
        const bool clip1 = uint16_t(s1[0]) == 0x8000 && uint16_t(s1[1]) == 0x8000;
        if (w1 == 0 || clip1 || w0 == 0 || clip0) continue;
        int16_t v0[3], v1[3];
        camPoint(p0, v0);
        camPoint(p1, v1);
        // {dx, dy} of the edge corners: up, down, right, left; the centre corners carry colour A
        static constexpr float kDir[4][2] = {{0, -1}, {0, 1}, {1, 0}, {-1, 0}};
        for (const auto& d : kDir) {
            const bool edgeFirst = d[1] > 0 || d[0] > 0; // quads 2 / 3 start with the edge corner
            int16_t v[4][3];
            float off[4][2];
            uint32_t c[4];
            for (int j = 0; j < 4; ++j) {
                const bool edge = (j & 1) ? !edgeFirst : edgeFirst;
                std::copy(j < 2 ? v0 : v1, (j < 2 ? v0 : v1) + 3, v[j]);
                off[j][0] = edge ? d[0] * u : 0.0f;
                off[j][1] = edge ? d[1] * u : 0.0f;
                c[j] = edge ? cB : cA;
            }
            c[0] |= 0x3a000000u;
            dc::otQuadG4(beam.at, c, 1, v, off);
        }
    }
    beam.pos.x = x;
    beam.pos.y = y;
    beam.pos.z = z;
}
// exe 0x5b9910
void guardianBeam(StaticActor& beam) {
    BeamFields& fields = classState<BeamFields>(beam);
    const BeamRow row = beamRow(beam);
    Player& p = player();
    switch (BeamState(beam.state)) {
    case BeamState::Start: // exe 0x5b9930
        beam.loopFlags |= kActorLateLoop;
        beam.velocity.z = 8;
        fields.width = int8_t(row.width);
        fields.life = row.life;
        beam.velocity.z = int16_t(row.speed);
        fields.longest = row.longest;
        fields.hit.result = 0;
        fields.hit.previousX = p.pos.x.whole;
        fields.hit.previousZ = p.pos.z.whole;
        if (beam.variant == 0) fields.loopSound = soundStartLoop(kBeamLoopSound, beam.pos, 0); // exe 0x5d5480
        beam.state = uint8_t(BeamState::Shine);
        return;
    case BeamState::Shine: { // exe 0x5b99f0
        const int32_t speed = beam.velocity.z;
        const int32_t l = fields.length + speed;
        fields.length = l;
        if (l > int32_t(fields.longest)) {
            fields.length = int32_t(fields.longest);
            if (beam.variant & 0xf0) moveAlongRotation(beam.pos, beam.rotation, 0, 0, -(speed << 4));
        }
        beamRings(beam, fields);
        beamBody(beam, fields);
        fields.hit.result = 0;
        fields.hit.previousX = p.pos.x.whole;
        fields.hit.previousZ = p.pos.z.whole;
        if (row.noHit == 0) {
            const int16_t origin[3] = {beam.pos.x.whole, beam.pos.y.whole, beam.pos.z.whole};
            const int16_t chest[3] = {p.pos.x.whole, t16(uint16_t(p.pos.y.whole) - 0x58), p.pos.z.whole};
            const Matrix3 m = rotationYXZ(beam.rotation.x, beam.rotation.y, beam.rotation.z); // exe 0x40bfd0
            if (pointInBeam(origin, chest, m, uint16_t(fields.length), row.radius))
                hitListSubmit(chest, fields.hit, hitRadius(row.radius) | hitOwner(beam.slot) | hitGroup(kHitGroupHeavyThrown),
                              uint32_t(beam.stepCounter) | kHitAttackStrong | kHitAttackThrown);
        }
        if (beam.variant & 0x80) {
            if (fields.life != 0) fields.life -= 1u;
            else beam.state = uint8_t(BeamState::Fade);
        }
        if (beam.variant == 0) {
            const int32_t x = beam.pos.x, y = beam.pos.y, z = beam.pos.z;
            moveAlongRotation(beam.pos, beam.rotation, 0, 0, 0xc000);
            soundUpdateLoop(fields.loopSound, beam.pos, 0); // exe 0x5d55c0
            beam.pos.x = x;
            beam.pos.y = y;
            beam.pos.z = z;
        }
        return;
    }
    case BeamState::Fade: // exe 0x5b9bf0
        if (fields.width > 0) fields.width = int8_t(uint8_t(fields.width) - 2u);
        else freeActor(beam);
        beamRings(beam, fields);
        beamBody(beam, fields);
        return;
    default: notPortedAt(kStoryPort, "0x5b9910 state", beam.state);
    }
}

// exe 0x4b21b0: the two beams: each one's length to the first wall along it, its sway, its pitch toward the player,
// its origin 0x300 in front of the guardian
void steerBeams(const PlayerEnv& env, const AnimatedActor& g, const GuardianFields& fields) {
    const Player& p = player();
    for (size_t i = 0; i < 2; ++i) {
        const int8_t slot = fields.beams[i];
        if (slot < 0) return;
        StaticActor& beam = staticActorAt(actorBySlot(uint32_t(int32_t(slot))));
        BeamFields& bf = classState<BeamFields>(beam);
        const uint32_t mask = collisionLayerMask(beam.slot); // exe 0x413240
        auto probe = [&](int32_t z) {
            int32_t tip[3] = {beam.pos.x, beam.pos.y, beam.pos.z};
            const int c = fixedCos(beam.rotation.x);
            if (c == 0) notPorted(kStoryPort, "0x4b21b0: division by a zero cosine");
            const int y = (fixedSin(beam.rotation.x) / c) >> 8;
            moveAlongYaw32(tip, beam.rotation.y, 0, y, z);
            const int16_t a[3] = {int16_t(uint32_t(tip[0]) >> 16), int16_t(uint32_t(tip[1]) >> 16),
                                  int16_t(uint32_t(tip[2]) >> 16)};
            const int16_t b[3] = {beam.pos.x.whole, beam.pos.y.whole, beam.pos.z.whole};
            return int32_t(testMoveBlocked(env, a, b, CollisionBoxRef::record(stateAddress(TableId::RuinGuardianBeamProbeBox)), mask)); // exe 0x40f5a0
        };
        int32_t z = 0;
        do z += 0x1000;
        while (probe(z) >= 0);
        do z -= 0x100;
        while (probe(z) < 0);
        const int16_t sway = bf.swaySpeed;
        if (uint32_t(int32_t(sway) + 0x40) <= 0x80u) bf.swaySpeed = int16_t(uint16_t(bf.swayStep) + uint16_t(sway));
        beam.rotation.y = int16_t(uint16_t(beam.rotation.y) + uint16_t(bf.swaySpeed));
        bf.swayPhase = int16_t(uint16_t(bf.swayPhase) + uint16_t(bf.phaseStep));
        beam.rotation.x = int16_t((fixedCos(int(uint16_t(bf.swayPhase))) << 5) >> 12);
        bf.longest = uint16_t((z >> 4) + 0x10);
        const int16_t chestY = t16(uint16_t(p.pos.y.whole) - 0x78);
        const int16_t d = t16(planarDistance(p.pos, beam.pos));
        beam.rotation.x = int16_t(uint16_t(beam.rotation.x) + uint16_t(angleOf(beam.pos.y.whole, 0, chestY, d)));
        beam.pos.x = g.pos.x;
        beam.pos.z = g.pos.z;
        moveAlongYaw(beam.pos, beam.rotation.y, 0, 0, -0x300);
    }
}
// exe 0x4b2010: the twin beams (static type 0x56 variant 1 from its bone 0, swaying apart)
void twinBeams(const PlayerEnv& env, AnimatedActor& g, GuardianFields& fields) {
    switch (g.step) {
    case 0:
        fields.mode = kGuardianBusy;
        g.enterStep(1);
        g.anim = 0x10;
        return;
    case 1: {
        if (g.animKey.frame == 0x14) soundPlayAtPos(0x1f9, g.pos);
        if (!g.animEnded()) return;
        g.step += 1u;
        fields.ticks = 0x30;
        const ShortVec4& base = bonePoint(g, 0, kStoryPort); // exe 0x5e1810
        for (uint32_t i = 0; i < 2; ++i) {
            fields.beams[i] = -1;
            const uint32_t s = allocActor60(kActorStageClass); // exe 0x5e12f0
            if (!s) return;
            StaticActor& beam = staticActorAt(s);
            BeamFields& bf = classState<BeamFields>(beam);
            fields.beams[i] = int8_t(beam.slot);
            beam.flags = kActorLive;
            beam.type = kBeamType;
            beam.stepCounter = 0x1e; // its damage
            beam.variant = 1;
            beam.pos.x.whole = base.x;
            beam.pos.y.whole = base.y;
            beam.pos.z.whole = base.z;
            beam.rotation.x = 0;
            beam.rotation.z = 0;
            bf.phaseStep = 0x80;
            bf.swaySpeed = 0;
            beam.rotation.y = int16_t(uint16_t((i << 11) + uint16_t(g.rotation.y) + 0x400u));
            bf.swayPhase = int16_t(i << 10);
            bf.swayStep = 8;
            if (i != 0) {
                bf.phaseStep = -0x80;
                bf.swayStep = -8;
            }
            soundPlayAtPos(0x1ec, beam.pos);
        }
        return;
    }
    case 2:
        steerBeams(env, g, fields);
        fields.ticks -= 1u;
        if (fields.ticks != 0) return;
        g.enterStep(3);
        g.anim = 0x11;
        for (size_t i = 0; i < 2; ++i) {
            const int8_t slot = fields.beams[i];
            if (slot < 0) return;
            actorHeaderAt(actorBySlot(uint32_t(int32_t(slot)))).state = uint8_t(BeamState::Fade);
        }
        return;
    case 3:
        if (g.animEnded()) g.enterState(uint8_t(GuardianState::Fight));
        return;
    default: return;
    }
}
// exe 0x4b12f0: the model, its row's hit points * 16, the box, the enemy-group entries, the boss gauge
void guardianInit(AnimatedActor& g, GuardianFields& fields) {
    if (!bindActorModel(g, 0)) { // exe 0x5e0ba0
        freeActor(g);           // exe 0x5e1490
        return;
    }
    g.anim = 0;
    g.prevAnim = 0xff;
    const int16_t hp =
        hasAnimatedStats(g.type) ? int16_t(uint16_t(animatedStats(g.type).hitPoints << 4)) : int16_t(0);
    g.maxHitPoints = hp;
    g.hitPoints = hp;
    g.collisionBox.pointAt(guardianBox.stateAddress());
    g.airborne = 0;
    g.onPlatform = 0;
    g.clearClassBlock(); // exe 0x40dc40
    fields.hit.previousX = g.pos.x.whole;
    fields.hit.previousZ = g.pos.z.whole;
    fields.hit.result = 0;
    fields.mineGroup = uint8_t(enemyGroupFindFor(g, g.variant));                 // exe 0x5e6cd0
    fields.mineMarks = uint8_t(enemyGroupFind(kProjectileClass, kOrbType, uint8_t(OrbVariant::Mine))); // exe 0x5e6d00
    g.enterState(uint8_t(GuardianState::Fight));
    hudAddBossGauge(g, 3, 5, kGaugeColour); // exe 0x415d10 -> 0x415d30
}
// exe 0x4b13b0: the sub-state, the damage, the hurt voice's cooldown, the damage stage's voices
void guardianFight(const PlayerEnv& env, AnimatedActor& g, GuardianFields& fields) {
    g.tint = kTintNeutral;
    switch (GuardianSub(g.subState)) { // [0x894494]
    case GuardianSub::Choose: choose(g, fields); break;
    case GuardianSub::Volley: volley(g, fields); break;
    case GuardianSub::Orbs:
    case GuardianSub::OrbsB: orbs(env, g, fields); break;
    case GuardianSub::Ring: ring(g, fields); break;
    case GuardianSub::Mines: mines(g, fields); break;
    case GuardianSub::Detonate: detonate(g, fields); break;
    case GuardianSub::Beams: twinBeams(env, g, fields); break;
    default: notPortedAt(kStoryPort, "0x4b1180 sub-state", g.subState);
    }
    if (!(fields.mode & kGuardianNoDamage) && guardianDamage(g, fields) < 0) g.enterState(uint8_t(GuardianState::Beaten));
    if (fields.hurtVoiceTicks != 0) fields.hurtVoiceTicks -= 1u;
    const int hp = g.hitPoints, mx = g.maxHitPoints;
    switch (fields.damageStage) {
    case 0:
        if (hp < (mx * 3) / 4) {
            fields.damageStage += 1u;
            soundPlayAtPos(0x1fa, g.pos);
        }
        return;
    case 1: // (the exe compares with max << 1)
        if (hp < (mx << 1)) {
            fields.damageStage += 1u;
            soundPlayAtPos(0x1fb, g.pos);
        }
        return;
    case 2: // (max << 2)
        if (hp < (mx << 2)) {
            fields.damageStage += 1u;
            soundPlayAtPos(0x1fc, g.pos);
        }
        return;
    default: return;
    }
}
// exe 0x4b1180
void ruinGuardian(const PlayerEnv& env, AnimatedActor& g) {
    GuardianFields& fields = classState<GuardianFields>(g);
    g.prevAnim = g.anim;
    g.savePreviousPose();
    g.savePreviousContact();
    switch (GuardianState(g.state)) {
    case GuardianState::Init: guardianInit(g, fields); break;
    case GuardianState::Fight: guardianFight(env, g, fields); break;
    case GuardianState::Beaten: // exe 0x4b23a0
        g.enterState(uint8_t(GuardianState::Done));
        setStoryFlag(g.variant == 0 ? kFlagFirstBeaten : kFlagSecondBeaten);
        break;
    case GuardianState::Done: break; // exe 0x52ef00
    default: notPortedAt(kStoryPort, "0x4b1180 state", g.state);
    }
    if (!(g.flags & kActorLive)) return;
    if (g.anim == g.prevAnim) animStep(g); // exe 0x5e1ea0
    else animSet(g, g.anim, 0);            // exe 0x5e1c50
    // the head's pitch and turn added to the decoded frame (parts 3 / 6's pitch, part 2's yaw)
    DecodedFrame& frame = decodedFrame();
    frame.entries[4].x += uint16_t(fields.headPitch);
    frame.entries[7].x += uint16_t(fields.headPitch);
    frame.entries[3].y += uint16_t(fields.headYaw);
    animPose(g); // exe 0x5e2d90
    const GuardianStats row = guardianStats(g);
    const uint8_t mode = fields.mode;
    fields.hit.result = 0;
    fields.hit.previousX = g.prevPos.x.whole;
    fields.hit.previousZ = g.prevPos.z.whole;
    if (mode & kGuardianNoHitEntry) return;
    const ShortVec4& body = bonePoint(g, 2, kStoryPort);
    const int16_t pos[3] = {body.x, body.y, body.z};
    hitListSubmit(pos, fields.hit,
                  hitOwner(g.slot) | hitHeightLevel(row.hitHeight) | hitGroup(kHitGroupBodies) |
                      hitRadius(row.radiusBytes) | ((mode & kGuardianDying) ? kHitNoLockOn : 0u),
                  uint32_t(row.attack) | kHitAttackSolid); // exe 0x5e4120
}

// ==== 0x5cd240: the orbs ==============================================================================================
enum class OrbState : uint8_t { Start = 0, Fly = 1, End = 2, Free = 3 };
// A launch point of the guardian (exe data, 8 bytes): the offset and the bone
struct LaunchPoint : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    int16_t x = 0; // +0x0
    int16_t y = 0; // +0x2
    int16_t z = 0; // +0x4
    int16_t bone = 0; // +0x6
    template <class R> static LaunchPoint load(const R& r) {
        LaunchPoint v;
        v.x = r.template read<int16_t>(0x0);
        v.y = r.template read<int16_t>(0x2);
        v.z = r.template read<int16_t>(0x4);
        v.bone = r.template read<int16_t>(0x6);
        return v;
    }
};
const DataTable<CollisionBoxRecord> orbBox{TableId::RuinGuardianOrbBox};
const DataTable<ScalarTable<int16_t, 8>> chaseSpeeds{TableId::RuinGuardianChaseSpeeds};
const DataTable<ScalarTable<int16_t, 8>> chaseTurns{TableId::RuinGuardianChaseTurns};
constexpr uint32_t kOrbAttackFlag = 0x4000; // an attack flag of the orbs' own (its reader is not traced)

int moveToward(int cur, int target, int step) {
    if (cur < target) return target - cur < step ? target : cur + step;
    if (cur > target) return cur - target < step ? target : cur - step;
    return cur;
}
// the launch point of the orb's index on the guardian
void launchPoint(const ProjectileActor& orb, const AnimatedActor& g, int16_t out[3]) {
    const LaunchPoint lp = dataRecord<LaunchPoint>(stateAddress(TableId::RuinGuardianLaunchPoints) + uint32_t(int32_t(OrbParams(orb).launch.get())) * 8, LaunchPoint::kBytes);
    const int16_t offset[3] = {lp.x, lp.y, lp.z};
    bonePointOffset(g, uint32_t(int32_t(lp.bone)), offset, out);
}
// exe 0x5ce150: the bob up and down around a random height below the start
void bob(ProjectileActor& orb, OrbFields& fields) {
    switch (fields.bobPhase) {
    case 0:
        fields.bobPhase = 1;
        fields.bobY = int16_t(uint16_t(uint16_t(orb.pos.y.whole) - (crtRand() & 0xff) - 0x20u));
        [[fallthrough]];
    case 1: {
        int v = orb.velocity.y - orb.acceleration.y;
        if (v < -0x40) v = -0x40;
        orb.velocity.y = int16_t(v);
        orb.pos.y.whole = int16_t(uint16_t(orb.pos.y.whole) + uint16_t(v >> 4));
        if (orb.pos.y.whole <= fields.bobY) fields.bobPhase += 1u;
        return;
    }
    case 2: {
        int v = orb.acceleration.y + orb.velocity.y;
        if (v > 0x40) v = 0x40;
        orb.velocity.y = int16_t(v);
        orb.pos.y.whole = int16_t(uint16_t(orb.pos.y.whole) + uint16_t(v >> 4));
        if (orb.pos.y.whole >= fields.bobY) fields.bobPhase = 1;
        return;
    }
    default: return;
    }
}
// the draw commands: exe 0x5ce350 (an orb: a 0x2c billboard + an 0xf0 glow), exe 0x5ce400 (the trail rings between
// the trails' points), exe 0x5ce5b0 (the 8 rings around the ring of points)
void drawOrb(ProjectileActor& orb) {
    orb.rotation.z = int16_t(uint16_t(orb.rotation.z) + 0x100u);
    const uint32_t xy = dc::packXY(orb.pos.x.whole, orb.pos.y.whole);
    dc::emit({0u, 0x2e808080u, xy, 0x3cf8a0c0u, uint32_t(int32_t(orb.pos.z.whole)), 0x2ea0dfu,
              (uint32_t(int32_t(orb.rotation.z)) << 16) | 0x2du, 0xbfc0u, 0u, 0xbfdfu},
             orb.at, false);
    dc::emit({0x4000u, 0xf0101000u, xy, 0x2240c020u, uint32_t(uint16_t(orb.pos.z.whole)) | 0x10000u, 0x28u}, orb.at);
}
void ringsBetween(uint32_t owner, const ShortVec3& p0, const ShortVec3& p1, uint32_t w1, uint32_t w3, uint32_t zBits,
                  uint32_t w5) {
    int32_t x = int32_t(uint32_t(int32_t(p0.x)) << 16), y = int32_t(uint32_t(int32_t(p0.y)) << 16),
            z = int32_t(uint32_t(int32_t(p0.z)) << 16);
    const int32_t dx = int32_t(uint32_t(int32_t(p1.x)) << 16) - x,
                  dy = int32_t(uint32_t(int32_t(p1.y)) << 16) - y,
                  dz = int32_t(uint32_t(int32_t(p1.z)) << 16) - z;
    for (int i = 0; i < 4; ++i) {
        dc::emit({0x4000u, w1, (uint32_t(y) & 0xffff0000u) | ((uint32_t(x) >> 16) & 0xffffu), w3,
                  ((uint32_t(z) >> 16) & 0xffffu) | zBits, w5},
                 owner);
        x += dx >> 2;
        y += dy >> 2;
        z += dz >> 2;
    }
}
void drawTrails(const ProjectileActor& orb, const OrbFields& fields) {
    for (size_t i = 0; i < 4; ++i) {
        ringsBetween(orb.at, fields.trailA[i], fields.trailA[i + 1], 0xf0000000u, 0x22f080f0u, 0x20000u, 0x16);
        ringsBetween(orb.at, fields.trailB[i], fields.trailB[i + 1], 0xf0000000u, 0x2280f080u, 0x20000u, 0x16);
    }
}
void drawRing(const ProjectileActor& orb, const OrbFields& fields) {
    for (size_t i = 0; i < 8; ++i)
        ringsBetween(orb.at, fields.points[i], fields.points[i + 1], 0xf0100010u, 0x22f080f0u, 0x30000u, 0xc);
}
// a trail's points moved one back (entries 4..1 = 3..0)
void shiftTrail(const OrbFields::Rows& trail) {
    for (size_t k = 4; k >= 1; --k) {
        trail[k] = trail[k - 1];
    }
}
// exe 0x5cd500 (variant 1): it drifts ahead a moment, homes on the player 0x96 ticks, then flies on and ends
void driftOrb(const PlayerEnv& env, ProjectileActor& orb, OrbFields& fields) {
    const AnimatedActor& g = *slotByIdentityWord<AnimatedActor>(fields.guardian);
    OrbParams params(orb);
    const uint8_t c = params.soundTicks;
    params.soundTicks = uint8_t(c - 1u);
    if (c == 0) {
        params.soundTicks = 5;
        soundPlayAtPos(0x1e7, orb.pos);
    }
    switch (orb.subState) { // [0x8d1730]
    case 0: // exe 0x5cd5e0
        if (orb.step == 0) {
            orb.step = 1;
            orb.velocity.y = 0;
            fields.ticks = uint16_t((crtRand() & 0x1f) + 3);
        }
        fields.ticks -= 1u;
        if (fields.ticks == 0) {
            orb.step = 0;
            orb.subState += 1u;
        }
        integrateVelocity(orb, 0, 0, -0x190);
        break;
    case 1: { // exe 0x5cd640
        if (orb.step == 0) {
            orb.step = 1;
            orb.velocity.y = 0;
            fields.ticks = 0x96;
        }
        fields.ticks -= 1u;
        if (fields.ticks == 0) {
            orb.subState += 1u;
            fields.ticks = 0x1e;
        }
        const int y = (player().pos.y.whole - orb.pos.y.whole - 0x64) >> 2;
        turnYawTo(orb.rotation.y, uint32_t(angleOf(player().pos, orb.pos)), 0x20);
        integrateVelocity(orb, 0, y, -0x190);
        break;
    }
    case 2: // exe 0x5cd6c0
        fields.ticks -= 1u;
        if (fields.ticks == 0) orb.state += 1u;
        integrateVelocity(orb, 0, 0, -0x190);
        break;
    default: notPortedAt(kStoryPort, "0x5cd500 sub-state", orb.subState);
    }
    if (blockedOut(env, orb)) {
        orb.enterState(uint8_t(OrbState::End));
        return;
    }
    const int16_t pos[3] = {orb.pos.x.whole, orb.pos.y.whole, orb.pos.z.whole};
    hitListSubmit(pos, fields.hit, hitOwner(orb.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x1c),
                  uint32_t(guardianStats(g).orbDamage) | kHitAttackHarmless | kHitAttackStrong | kOrbAttackFlag);
    drawOrb(orb);
}
// exe 0x5cd6f0 (variant 2): straight ahead, the trails' points weaving around its path
void bigOrb(const PlayerEnv& env, ProjectileActor& orb, OrbFields& fields) {
    const AnimatedActor& g = *slotByIdentityWord<AnimatedActor>(fields.guardian);
    switch (orb.subState) { // [0x8d173c]
    case 0: { // exe 0x5cd7a0
        integrateVelocity(orb, 0, 0, -0x320);
        fields.phase += 0x200u;
        const uint32_t t = fields.phase;
        const int r = t16((fixedCos(int(t)) * 30) >> 12);
        const int yo = (-(fixedSin(int(t)) * 30)) >> 12;
        const int xo = (fixedCos(int(uint16_t(orb.rotation.y))) * r) >> 12;
        const int zo = (-(fixedSin(int(uint16_t(orb.rotation.y))) * r)) >> 12;
        shiftTrail(fields.trailA);
        shiftTrail(fields.trailB);
        ShortVec3& a = fields.trailA[0];
        ShortVec3& b = fields.trailB[0];
        a.x = int16_t(uint16_t(orb.pos.x.whole) + xo);
        a.y = int16_t(uint16_t(orb.pos.y.whole) + yo);
        a.z = int16_t(uint16_t(orb.pos.z.whole) + zo);
        b.x = int16_t(uint16_t(orb.pos.x.whole) - xo);
        b.y = int16_t(uint16_t(orb.pos.y.whole) - yo);
        b.z = int16_t(uint16_t(orb.pos.z.whole) - zo);
        break;
    }
    default: notPortedAt(kStoryPort, "0x5cd6f0 sub-state", orb.subState);
    }
    const int16_t prev[3] = {orb.prevPos.x.whole, orb.prevPos.y.whole, orb.prevPos.z.whole};
    hitListSubmit(prev, fields.hit, hitOwner(orb.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x50),
                  uint32_t(guardianStats(g).bigOrbDamage) | kHitAttackHarmless | kHitAttackStrong | kHitAttackThrown);
    if (blockedOut(env, orb)) orb.enterState(uint8_t(OrbState::End));
    drawTrails(orb, fields);
}
// exe 0x5cd8d0 (variant 3): held at the guardian's hand until its animation 0xd reaches key 0x19; launched it rises,
// then turns after the player (its turns slowing), then a last chase; the ring of 8 points around it
void ringOrb(const PlayerEnv& env, ProjectileActor& orb, OrbFields& fields) {
    const AnimatedActor& g = *slotByIdentityWord<AnimatedActor>(fields.guardian);
    switch (orb.subState) { // [0x8d1740]
    case 0: // exe 0x5cdaa0
        if (g.animKey.frame == 0x19) {
            if (g.anim != 0xd) break;
            orb.velocity.y = 0x180;
            orb.subState += 1u;
            orb.acceleration.y = 0x18;
            soundPlayAtPos(0x1e9, orb.pos);
        }
        if (g.anim == 0xd) {
            int16_t out[3];
            launchPoint(orb, g, out);
            orb.pos.x.whole = out[0];
            orb.pos.y.whole = int16_t(uint16_t(uint16_t(out[1]) - 0x20u));
            orb.pos.z.whole = out[2];
        }
        break;
    case 1: { // exe 0x5cdb70
        int vy = orb.velocity.y - orb.acceleration.y;
        if (vy < 0) vy = 0;
        orb.velocity.y = int16_t(vy);
        bool chase = true;
        switch (orb.step) {
        case 0:
            orb.step = 1;
            fields.ticks = 0xa;
            [[fallthrough]];
        case 1:
            fields.ticks -= 1u;
            if (fields.ticks == 0) {
                orb.step += 1u;
                orb.stepCounter = 0xf0;
                fields.turnLevel = 0;
            }
            integrateVelocity(orb, 0, orb.velocity.y, -0x320);
            chase = false;
            break;
        case 2: {
            orb.stepCounter -= 1u;
            bool done = orb.stepCounter == 0;
            if (!done) {
                const uint16_t t = fields.ticks;
                fields.ticks = uint16_t(t - 1u);
                if (t == 0) {
                    fields.ticks = 6;
                    int d = angleDiff(uint32_t(int32_t(orb.rotation.y)), uint32_t(angleOf(player().pos, orb.pos)));
                    if (d < 0) d = -d;
                    d >>= 8;
                    int c = fields.turnLevel;
                    if (c > d) --c;
                    else if (c < d) ++c;
                    fields.turnLevel = int16_t(c);
                    done = t16(c) == 0;
                }
            }
            if (done) {
                orb.step += 1u;
                orb.stepCounter = 0;
                fields.ticks = 0x3c;
                fields.bobPhase = 0x32; // the ring's radius target
            }
            break;
        }
        case 3:
            fields.ticks -= 1u;
            if (fields.ticks == 0) {
                orb.enterState(uint8_t(OrbState::End));
                fields.turnLevel = 0;
            }
            turnYawTo(orb.rotation.y, uint32_t(angleOf(player().pos, orb.pos)), 0x18);
            integrateVelocity(orb, 0, orb.velocity.y, -0x320);
            chase = false;
            break;
        default: chase = false; break;
        }
        if (chase) {
            const size_t k = size_t(int32_t(fields.turnLevel));
            turnYawTo(orb.rotation.y, uint32_t(angleOf(player().pos, orb.pos)), chaseTurns[k]);
            integrateVelocity(orb, 0, orb.velocity.y, chaseSpeeds[k]);
        }
        break;
    }
    default: notPortedAt(kStoryPort, "0x5cd8d0 sub-state", orb.subState);
    }
    for (size_t k = 0; k < 4; ++k) {
        const ShortVec3& point = fields.points[k * 2];
        const int16_t pos[3] = {point.x, point.y, point.z};
        hitListSubmit(pos, fields.hit, hitOwner(orb.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x10),
                      kHitAttackHarmless | kHitAttackStrong | kHitAttackThrown | 0x1bu);
    }
    if (blockedOut(env, orb)) orb.enterState(uint8_t(OrbState::End));
    orb.rotation.z = int16_t(moveToward(orb.rotation.z, (orb.prevRotation.y - orb.rotation.y) << 3, 0x20));
    int r = fields.ringRadius;
    if (fields.bobPhase > r) ++r;
    else if (fields.bobPhase < r) --r;
    fields.ringRadius = uint8_t(r);
    fields.phase += 0x80u;
    uint32_t angle = fields.phase;
    const int16_t rot[3] = {orb.rotation.x, orb.rotation.y, orb.rotation.z};
    for (size_t k = 0; k < 8; ++k, angle += 0x200) {
        int32_t o[3];
        rotateVector(rot, (fixedSin(int(angle)) * r) >> 8, 0, (-(fixedCos(int(angle)) * r)) >> 8, o); // exe 0x5e3970
        ShortVec3& point = fields.points[k];
        point.x = int16_t(uint16_t((uint32_t(orb.pos.x) + uint32_t(o[0])) >> 16));
        point.y = int16_t(uint16_t((uint32_t(orb.pos.y) + uint32_t(o[1])) >> 16));
        point.z = int16_t(uint16_t((uint32_t(orb.pos.z) + uint32_t(o[2])) >> 16));
    }
    fields.points[8] = fields.points[0];
    drawRing(orb, fields);
}
// exe 0x5cdd20 (variant 4): the mine -- it drops and slows, drifts to a random point ahead and bobs; near the player it
// marks its group entry; once the guardian's entry is marked it jumps at the player, then runs on
void mine(const PlayerEnv& env, ProjectileActor& orb, OrbFields& fields) {
    const Player& p = player();
    switch (orb.subState) { // [0x8d1768]
    case 0: // exe 0x5cddb0: the drop, slowing
        switch (orb.step) {
        case 0:
            orb.step = 1;
            orb.velocity.y = 0;
            orb.velocity.z = -0x140;
            orb.acceleration.y = 2;
            orb.acceleration.z = 0x14;
            fields.ticks = 0x10;
            [[fallthrough]];
        case 1:
            fields.ticks -= 1u;
            if (fields.ticks == 0) orb.step += 1u;
            integrateVelocity(orb, 0, 0, orb.velocity.z);
            break;
        case 2:
            orb.velocity.z = int16_t(uint16_t(orb.velocity.z) + uint16_t(orb.acceleration.z));
            if (orb.velocity.z >= 0) {
                orb.subState += 1u;
                orb.restartStep();
                orb.velocity.z = 0;
            }
            bob(orb, fields);
            integrateVelocity(orb, 0, 0, orb.velocity.z);
            break;
        default: integrateVelocity(orb, 0, 0, orb.velocity.z); break;
        }
        break;
    case 1: { // exe 0x5cde60: the drift to a random point ahead, the wait near the player
        const uint8_t step = orb.step;
        if (step == 0) {
            orb.step = 1;
            const int r = int(crtRand() & 0x3ff) + 0x300;
            int32_t ahead[3] = {orb.pos.x, orb.pos.y, orb.pos.z};
            moveAlongYaw32(ahead, orb.rotation.y, 0, 0, -(r << 4));
            fields.driftX = int16_t(uint32_t(ahead[0]) >> 16);
            fields.driftZ = int16_t(uint32_t(ahead[2]) >> 16);
        }
        if (step <= 1) {
            // the drift's target (the exe's fraction words are stale stack; 0 here)
            const int32_t target[3] = {int32_t(uint32_t(uint16_t(fields.driftX)) << 16), 0,
                                       int32_t(uint32_t(uint16_t(fields.driftZ)) << 16)};
            const int angle = angleOf(target[0], target[2], orb.pos.x, orb.pos.z);
            const int d = planarDistance(orb.pos, target);
            int k = d >> 5;
            if (k > 0x60) k = 0x60;
            else if (k < 0x10) k = 0x10;
            int16_t dx = 0, dz = 0;
            angleVector(angle, -k, dx, dz); // exe 0x5e40c0
            integrateVelocity(orb, dx, 0, dz);
            if (d < 0x32) orb.step += 1u;
        }
        if (planarDistance(orb.pos, p.pos) <= 0x500) {
            enemyGroupMark(int32_t(fields.ownGroup), 1);
            if (enemyGroupMarks(int32_t(fields.guardianGroup), EnemyGroupMarks::LastTick) != 0) {
                orb.subState += 1u;
                orb.restartStep();
            }
        }
        bob(orb, fields);
        break;
    }
    case 2: // exe 0x5cdfc0: the jump at the player
        switch (orb.step) {
        case 0:
            orb.step = 1;
            fields.ticks = uint16_t((crtRand() & 7) + 2);
            break;
        case 1: {
            fields.ticks -= 1u;
            if (fields.ticks != 0) break;
            orb.step += 1u;
            orb.velocity.x = 0;
            orb.velocity.z = 0;
            const int16_t bx = t16(int(crtRand() & 0xff) + uint16_t(p.pos.x.whole) - 0x80);
            const int16_t ty = t16(uint16_t(p.pos.y.whole) - 0x64);
            const int16_t bz = t16(int(crtRand() & 0xff) + uint16_t(p.pos.z.whole) - 0x80);
            const int n = int(crtRand() & 7) + 0xa;
            fields.ticks = uint16_t((crtRand() & 3) + 0xc);
            orb.acceleration.x = int16_t(((int32_t(bx - orb.pos.x.whole) << 5) / n) / n);
            orb.acceleration.y = int16_t(((int32_t(ty - orb.pos.y.whole) << 5) / n - orb.velocity.y) / n);
            orb.acceleration.z = int16_t(((int32_t(bz - orb.pos.z.whole) << 5) / n) / n);
            break;
        }
        case 2:
            fields.ticks -= 1u;
            if (fields.ticks == 0) orb.enterState(uint8_t(OrbState::End));
            orb.velocity.x = int16_t(uint16_t(orb.velocity.x) + uint16_t(orb.acceleration.x));
            orb.velocity.y = int16_t(uint16_t(orb.velocity.y) + uint16_t(orb.acceleration.y));
            orb.velocity.z = int16_t(uint16_t(orb.velocity.z) + uint16_t(orb.acceleration.z));
            orb.pos.x = int32_t(uint32_t(orb.pos.x) + (uint32_t(int32_t(orb.velocity.x)) << 12));
            orb.pos.y = int32_t(uint32_t(orb.pos.y) + (uint32_t(int32_t(orb.velocity.y)) << 12));
            orb.pos.z = int32_t(uint32_t(orb.pos.z) + (uint32_t(int32_t(orb.velocity.z)) << 12));
            break;
        default: break;
        }
        break;
    case 3: // exe 0x5ce200: it runs on (the points: x / z pairs)
        integrateVelocity(orb, 0, 0, -0x320);
        for (size_t k = 7; k >= 1; --k) {
            ShortVec3& to = fields.points[k];
            const ShortVec3& from = fields.points[k - 1];
            to.x = from.x;
            to.y = from.y;
        }
        fields.points[0].x = orb.pos.x.whole;
        fields.points[0].y = orb.pos.z.whole;
        break;
    default: notPortedAt(kStoryPort, "0x5cdd20 sub-state", orb.subState);
    }
    if (fields.hit.result & 0xff0000u) {
        orb.enterState(uint8_t(OrbState::End));
        return;
    }
    const int16_t pos[3] = {orb.pos.x.whole, orb.pos.y.whole, orb.pos.z.whole};
    hitListSubmit(pos, fields.hit,
                  hitOwner(orb.slot) | hitGroup(kHitGroupBodies) | kHitCompareHeights | kHitNoLockOn | hitRadius(0x14),
                  kHitAttackSolid);
    if (blockedOut(env, orb)) orb.enterState(uint8_t(OrbState::End));
}
// exe 0x5cd280: at its launch point; by the variant its rotation from the guardian's, the trails / the ring filled, the
// mine's model
void orbStart(ProjectileActor& orb, OrbFields& fields) {
    const AnimatedActor& g = *slotByIdentityWord<AnimatedActor>(fields.guardian);
    orb.onPlatform = 0;
    orb.platform = nullptr;
    orb.collisionBox.pointAt(orbBox.stateAddress());
    orb.actorHit = 0;
    fields.clearBlockBytes(); // exe 0x40dc40(+0xc0, +0xd8)
    fields.hit.previousX = orb.pos.x.whole;
    fields.hit.previousZ = orb.pos.z.whole;
    int16_t out[3];
    launchPoint(orb, g, out);
    orb.pos.x.whole = out[0];
    orb.pos.y.whole = out[1];
    orb.pos.z.whole = out[2];
    switch (OrbVariant(orb.variant)) {
    case OrbVariant::Drift:
        orb.rotation = g.rotation;
        orb.rotation.y = int16_t(uint16_t(uint32_t(uint16_t(orb.rotation.y)) + 0x180u -
                                          uint32_t(int32_t(OrbParams(orb).launch.get()) << 8)));
        break;
    case OrbVariant::Big:
        orb.rotation = g.rotation;
        for (size_t k = 0; k < 5; ++k) {
            for (const auto* trail : {&fields.trailA, &fields.trailB}) {
                ShortVec3& point = (*trail)[k];
                point.x = orb.pos.x.whole;
                point.y = orb.pos.y.whole;
                point.z = orb.pos.z.whole;
            }
        }
        break;
    case OrbVariant::Ring:
        orb.rotation = g.rotation;
        orb.pos.y.whole = int16_t(uint16_t(uint16_t(out[1]) - 0x20u));
        fields.ringRadius = 0;
        fields.bobPhase = 0x23; // the ring's radius target
        break;
    case OrbVariant::Mine:
        if (!bindActorModel(orb, 0)) { // exe 0x5e0ba0
            freeActor(orb);           // exe 0x5e1500
            return;
        }
        orb.flags |= kActorDrawn;
        orb.scale.x = 0x180;
        orb.scale.y = 0x180;
        orb.scale.z = 0x180;
        fields.ownGroup = uint8_t(enemyGroupFindFor(orb, orb.variant));
        fields.guardianGroup = uint8_t(enemyGroupFind(kAnimatedClass, kGuardianType, 1));
        break;
    default: orb.rotation = g.rotation; break;
    }
    fields.phase = 0;
    orb.enterState(uint8_t(OrbState::Fly));
}
// exe 0x5cd240
void guardianOrb(const PlayerEnv& env, ProjectileActor& orb) {
    OrbFields& fields = classState<OrbFields>(orb);
    orb.savePreviousPose();
    switch (OrbState(orb.state)) {
    case OrbState::Start: orbStart(orb, fields); return;
    case OrbState::Fly: // exe 0x5cd4c0: by the variant [0x8d171c]
        switch (OrbVariant(orb.variant)) {
        case OrbVariant::None: break; // exe 0x52ef00
        case OrbVariant::Drift: driftOrb(env, orb, fields); break;
        case OrbVariant::Big: bigOrb(env, orb, fields); break;
        case OrbVariant::Ring: ringOrb(env, orb, fields); break;
        case OrbVariant::Mine: mine(env, orb, fields); break;
        default: notPortedAt(kStoryPort, "0x5cd240 variant", orb.variant);
        }
        fields.hit.result = 0;
        fields.hit.previousX = orb.prevPos.x.whole;
        fields.hit.previousZ = orb.prevPos.z.whole;
        return;
    case OrbState::End: // exe 0x5ce250
        if (OrbVariant(orb.variant) == OrbVariant::Big) {
            if (orb.subState == 0) {
                orb.subState = 1;
                fields.ticks = 0xf;
            }
            fields.ticks -= 1u;
            if (fields.ticks == 0) orb.state += 1u;
            shiftTrail(fields.trailA);
            shiftTrail(fields.trailB);
            drawTrails(orb, fields);
        } else if (OrbVariant(orb.variant) == OrbVariant::Mine) {
            orb.state += 1u;
            classState<GuardianFields>(*slotByIdentityWord<AnimatedActor>(fields.guardian)).minesLive -= 1u;
            const uint32_t e = allocActor60(0); // exe 0x5e1300: the blast
            if (!e) return;
            StaticActor& blast = staticActorAt(e);
            auto&& params = BlastParams(actorHeaderAt(e));
            blast.flags = kActorLive;
            blast.loopFlags |= kActorSweepGroup2;
            blast.type = typeByte(GlobalStaticType::Blast);
            blast.variant = 0;
            blast.pos = orb.pos;
            params.size = 0x32;
            params.attack = 8;
        } else {
            orb.state += 1u;
        }
        return;
    case OrbState::Free: freeActor(orb); return; // exe 0x5d0bc0 -> 0x5e1500
    default: notPortedAt(kStoryPort, "0x5cd240 state", orb.state);
    }
}

} // namespace

void ruinGuardianUpdate(const PlayerEnv& env, uint32_t actor) { ruinGuardian(env, animatedActorAt(actor)); }
void guardianBeamUpdate(const PlayerEnv&, uint32_t actor) { guardianBeam(staticActorAt(actor)); }
void guardianOrbUpdate(const PlayerEnv& env, uint32_t actor) { guardianOrb(env, projectileActorAt(actor)); }

} // namespace od::port
