// The guardian of the Manda ruin and its cast (see manda_guardian.hpp): the fight's guardian (exe 0x484e00: pool 0x20
// type 0x24, ST13 sub 1 after script 9, variant 0; the ST12 sub 5 arena's variants 1 / 2 after script 0xb), its shot,
// the shot's trail, the hit mute of its death, the cloud of its spin, the cutscene guardians of scripts 9 / 10 / 0xb /
// 0xc, its hoppers, the arena's blades, the falling blade and the opened chest of script 0xb. Bit-exact against
// tests/data/story_ST13_g*.bin.
//
// The fight (variant 0): it fades in (the 13 bones' colours by their thresholds) and spawns the hopper spawner (pool
// 0xe0 type 0xa, content/manda/ruin_spawners); waits hidden (600 ticks or until its hoppers are gone); re-appears at
// the centre and spins (the clouds); shoots volleys of three shots from bone 5, then fades out (back to the wait).
// Damage through its capsule box (an 8-tick invulnerability after a strong push); its death: the kill counter, the hit
// mute (pool 0xe0 type 0x18), the fade out, flag 0x5b1 (the room handler starts script 10). Variants 1 / 2 fight in
// the arena of the static blades (death: flag 0x5b3 -> script 0xc). Render output (the shadows, exe 0x5f20f0; the
// trail's quads; the cloud's strips) is left out.
#include <cstdio>
#include <stdexcept>
#include <string>

#include "content/manda/manda_guardian.hpp"
#include "content/manda/manda_guardian_state.hpp"

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/actors.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/carry.hpp"
#include "game/collision.hpp"
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/skid_sparks.hpp"
#include "game/spawn.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"
#include "game/table_records.hpp"
#include "game/territory.hpp"

namespace od::port {

using namespace manda_guardian_state;

namespace {

constexpr const char* kMandaGuardianPort = "manda guardian port"; // the area of its port-gap errors

int16_t wrap16(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }

// the colour of bone k (exe 0x5e1810: a bone past the actor's blocks reads through null in the exe)
BoneColour boneColour(AnimatedActor& a, uint32_t k) {
    BoneBlock* b = boneBlockOf(a, int(k >> 2));
    if (b == nullptr) notPorted(kMandaGuardianPort, "0x5e1810: bone past the bone-block mask");
    return b->colour(k & 3);
}
// the point of bone k as an s16 triple (exe 0x5e1810)
void bonePosition(const AnimatedActor& a, uint32_t k, int16_t out[3]) {
    const ShortVec4& p = bonePoint(a, k, kMandaGuardianPort);
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
}
// the player's chest (0x96 above its position), the guardians' shots' aim
void playerChest(int16_t out[3]) {
    const Player& p = player();
    out[0] = p.pos.x.whole;
    out[1] = wrap16(int(p.pos.y.whole) - 0x96);
    out[2] = p.pos.z.whole;
}
// a 16.16 point with only its integer parts set (the exe's stack points of x / z)
void wholePoint(int16_t x, int16_t z, int32_t out[3]) {
    out[0] = int32_t(uint32_t(uint16_t(x)) << 16);
    out[1] = 0;
    out[2] = int32_t(uint32_t(uint16_t(z)) << 16);
}
// the 64-step sine / cosine tables read at the exe's index, unmasked (an index past the 64 entries is a
// StateIndexError: the exe would read whatever follows the table)
int16_t sineAt(int32_t index) { return trig64Tables().sine[size_t(index)]; }
int16_t cosineAt(int32_t index) { return trig64Tables().cosine[size_t(index)]; }

// ---- the guardian (pool 0x20 type 0x24) ---------------------------------------------------------------------------
constexpr uint8_t kGuardianType = 0x24;
// +0x15e
constexpr uint8_t kReflect = 0x01;    // the collision turns its rotation off the walls
constexpr uint8_t kNoLean = 0x02;     // the pitch / roll eased back to 0
constexpr uint8_t kFading = 0x04;     // no hit entry, no damage
constexpr uint8_t kDeflects = 0x08;   // its body deflects the player's shots (no damage)
constexpr uint8_t kNoHeadTurn = 0x10; // the head turned back to 0
constexpr uint8_t kLanding = 0x20;    // the landing's hit entry (the jump of variants 1 / 2)
constexpr uint8_t kNoLockOn = 0x40;   // never the player's lock-on target
constexpr uint8_t kHarmless = 0x80;   // its body only bumps (no damage)
constexpr uint8_t kNoDamage = kFading | kDeflects | kHarmless;
constexpr uint8_t kHiddenFlags = kNoLean | kFading | kDeflects | kNoLockOn;
// +0x160
constexpr uint8_t kHitWall = 0x01, kHitReflected = 0x02, kHitNoCell = 0x08, kHitFloor = 0x10;
// the enemy group's marks the guardian, its hoppers, its spawner and the blades share
constexpr uint16_t kMarkSpin = 0x01;          // the guardian spins: its hoppers heal and gather round it
constexpr uint16_t kMarkRelease = 0x02;       // it fades out after its volleys: its hoppers go free
constexpr uint16_t kMarkRecall = 0x04;        // its spin is coming: its hoppers jump to their corners
constexpr uint16_t kMarkHoppersCalled = 0x20; // hidden: its hoppers are called (the spawner refills them)
constexpr uint16_t kMarkHoppersGone = 0x40;   // marked by the spawner: none is left
constexpr uint16_t kMarkBladesBack = 0x100;   // marked by a blade: its slide round ended
enum class GuardianState : uint8_t { Init = 0, Fight = 1, Dying = 2 }; // [0x892128]: 0x485000 / 0x4850f0 / 0x486500
// variant 0's sub-states [0x892140]
enum class ArenaSub : uint8_t {
    Appear = 0,  // exe 0x4851b0
    Hidden = 1,  // exe 0x485280
    Spin = 2,    // exe 0x485300
    Volleys = 3, // exe 0x485620
};
// variants 1 / 2's sub-states [0x892170]
enum class BladeArenaSub : uint8_t {
    Vanish = 0,      // exe 0x485930
    Reappear = 1,    // exe 0x4859d0
    HandVolleys = 2, // exe 0x485b90
    Jump = 3,        // exe 0x485ee0
    Spin = 4,        // exe 0x486230
};
// the death's sub-states (exe 0x486520)
enum class DeathSub : uint8_t { Start = 0, SpinEnds = 1, Collapse = 2, FadeStart = 3, Fading = 4, Gone = 5 };
constexpr uint8_t kVariantArena = 0, kVariantBladeArena = 1, kVariantBladeArenaB = 2;
constexpr uint32_t kFlagGuardianBeaten = 0x5b1, kFlagBladeGuardianBeaten = 0x5b3;
constexpr uint16_t kSoundFade = 0x12a, kSoundShotHit = 0x12d, kSoundSpinStart = 0x12e, kSoundSpinLoop = 0x12f;
constexpr uint16_t kSoundJump = 0x130, kSoundLand = 0x131, kSoundHurt = 0x8d, kSoundShot = 300;
constexpr uint8_t kDustVariant = 0xf, kJumpDustVariant = 0x10;
constexpr uint32_t kFadeBones = 13;
constexpr int8_t kFadeTicks = 0x1a;
constexpr uint32_t kShotBone = 5, kHeadBone = 1, kFootBone = 3;
constexpr size_t kHeadEntry = 2; // the decoded frame's entry the head's turn is added to
// the parts shown / hidden with the shots' flash
constexpr size_t kHandPart = 5, kFlashPart = 13;
// the fade's thresholds by bone (exe data, s8 x 13): the fight's, script 9's, script 10's, scripts 0xb / 0xc's
using FadeThresholds = DataTable<ScalarTable<int8_t, kFadeBones>>;
const FadeThresholds fightFade{TableId::MandaGuardianFightFade}, castFade{TableId::MandaGuardianCastFade}, outroFade{TableId::MandaGuardianOutroFade},
    cast2Fade{TableId::MandaGuardianCast2Fade};
const DataTable<CollisionBoxRecord> fightBox{TableId::MandaGuardianFightBox}, castBox{TableId::MandaGuardianCastBox},
    outroBox{TableId::MandaGuardianOutroBox}, cast2Box{TableId::MandaGuardianCast2Box};
const DataTable<ScalarTable<uint8_t, 0x10>> cloudShades{TableId::MandaGuardianCloudShades}; // the clouds' second byte, by RNG
const DataTable<ScalarTable<uint8_t, 0x10>> volleyCounts{TableId::MandaGuardianVolleyCounts}; // the shots of a volley (variant 0), by RNG
const DataTable<RecordTable<ShortPoint, 3, 8>> handOffsets{TableId::MandaGuardianHandOffsets}; // the hand shots' offsets from bone 5 (variants 1 / 2)

// the hopper spawner's spawn parameter: the guardian's slot (content/manda/ruin_spawners)
constexpr uint8_t kHopperSpawnerType = 0xa;
struct HopperSpawnerParams {
    explicit HopperSpawnerParams(const ControllerActor& actor) : HopperSpawnerParams(const_cast<ControllerActor&>(actor)) {}
    explicit HopperSpawnerParams(ControllerActor& actor) : guardian(actor.params[0]) {}
    uint8_t& guardian;
};
// the cloud of its spin (pool 0xa0, ST13 stage type 0xc): its spawn parameters, the guardian's step counter and a shade
// (their reader is the cloud's draw), its ring index / size / shrink ticks (the header's step bytes), its wobble's
// phase and step, the hold timer and the wobble ring
constexpr uint8_t kCloudType = 0xc;
// the hit mute (pool 0xe0 type 0x18): the groups it mutes (a bit per group, the variant byte) and its ticks
constexpr uint8_t kHitMuteType = 0x18;
constexpr uint8_t kMutedGroups = 0x2c; // the groups 2, 3 and 5 (bodies, thrown boxes, liftables)
struct HitMuteFields {
    explicit HitMuteFields(const ControllerActor& actor) : HitMuteFields(const_cast<ControllerActor&>(actor)) {}
    explicit HitMuteFields(ControllerActor& actor) : ticks(actor.params, 0) {}
    BytesField<int16_t, 4> ticks;
};
// the ground shock (pool 0x40 stage type 3; its class is not ported: the names are the spawner's)
constexpr uint8_t kShockType = 3, kShockVariant = 9;
struct ShockParams {
    explicit ShockParams(const ProjectileActor& actor) : ShockParams(const_cast<ProjectileActor&>(actor)) {}
    explicit ShockParams(ProjectileActor& actor) : rings(actor.params[0]), phase(actor.params[1]) {}
    uint8_t& rings;
    uint8_t& phase;
};
constexpr uint8_t kShotType = 0xe; // its shot (pool 0x40, ST13 / ST12 stage type 0xe)

void toSub(AnimatedActor& a, uint8_t sub) {
    a.subState = sub;
    a.restartStep();
}
void toSub(AnimatedActor& a, ArenaSub s) { toSub(a, uint8_t(s)); }
void toSub(AnimatedActor& a, BladeArenaSub s) { toSub(a, uint8_t(s)); }

// ---- the helpers --------------------------------------------------------------------------------------------------
// exe 0x4869b0(actor, yaw, variant): four skid sparks (the global effect type 3) at the actor, yaws k << 10 + yaw
void dustRing(const AnimatedActor& a, uint16_t yaw, uint8_t variant) {
    for (int k = 3; k >= 0; --k) {
        const uint32_t address = allocActorA0(0); // exe 0x5e13b0
        if (!address) continue;
        EffectActor& e = effectActorAt(address);
        e.flags = kActorLiveDrawn;
        e.type = typeByte(GlobalEffectType::SkidSparks);
        e.variant = variant;
        e.pos.x = a.pos.x.whole;
        e.pos.y = a.pos.y.whole;
        e.pos.z = a.pos.z.whole;
        classState<SkidSparkFields>(e).yaw = uint16_t(uint32_t(k << 10) + yaw);
    }
}
// exe 0x488a10: the same at the position << the stage's coordinate shift (the cutscenes')
void dustRingShifted(const AnimatedActor& a, uint16_t yaw, uint8_t variant) {
    const uint32_t shift = stage().coordinateShift & 0x1f;
    for (int k = 3; k >= 0; --k) {
        const uint32_t address = allocActorA0(0); // exe 0x5e13b0
        if (!address) continue;
        EffectActor& e = effectActorAt(address);
        e.flags = kActorLiveDrawn;
        e.type = typeByte(GlobalEffectType::SkidSparks);
        e.variant = variant;
        e.pos.x = int16_t(uint32_t(uint16_t(a.pos.x.whole)) << shift);
        e.pos.y = int16_t(uint32_t(uint16_t(a.pos.y.whole)) << shift);
        e.pos.z = int16_t(uint32_t(uint16_t(a.pos.z.whole)) << shift);
        classState<SkidSparkFields>(e).yaw = uint16_t(uint32_t(k << 10) + yaw);
    }
}
// the dust's ticks of the fade (bit 31 of 0x8212c0 << counter)
bool dustTick(const GuardianFields& f) { return int32_t(0x8212c0u << (uint8_t(f.fadeCounter) & 0x1f)) < 0; }
void dustStep(const AnimatedActor& a, GuardianFields& f) {
    if (!dustTick(f)) return;
    dustRing(a, uint16_t(uint32_t(f.count) << 8), kDustVariant);
    ++f.count;
}
void dustStepShifted(const AnimatedActor& a, GuardianFields& f) {
    if (!dustTick(f)) return;
    dustRingShifted(a, uint16_t(uint32_t(f.count) << 8), kDustVariant);
    ++f.count;
}
// exe 0x4881f0: the fade started (drawn, semi-transparent, no hit entry while it fades)
void fadeStartSilent(AnimatedActor& a, GuardianFields& f) {
    a.flags |= kActorDrawn;
    f.fadeCounter = kFadeTicks;
    a.semiTransparency = ((1 & 1) << 4 | (1 & 3)) << 1; // exe 0x5e1a00(actor, 1, 1)
    f.flags |= kFading;
}
// exe 0x487a80: the same with its sound
void fadeStart(AnimatedActor& a, GuardianFields& f) {
    fadeStartSilent(a, f);
    soundPlayAtPos(kSoundFade, a.pos);
}
// exe 0x4867d0: the fade started at the height -0x100, no vertical speed
void fadeStartLow(AnimatedActor& a, GuardianFields& f) {
    fadeStart(a, f);
    a.pos.y.whole = -0x100;
    a.velocity.y = 0;
}
// exe 0x486800 (the fight's table) / 0x487140 (the cutscenes'): a tick of the fade out (each bone from its threshold);
// true at its end (hidden)
bool fadeOut(AnimatedActor& a, GuardianFields& f, const FadeThresholds& table = fightFade) {
    const int c = f.fadeCounter;
    a.tint = 0;
    for (uint32_t k = 0; k < kFadeBones; ++k) {
        BoneColour colour = boneColour(a, k);
        const int t = table[k];
        if (t < c) {
            colour = kTintNeutral;
        } else {
            int v = (c - t) + 0x10;
            if (v < 0) v = 0;
            colour = uint16_t(v << 10 | v << 5 | v);
        }
    }
    f.fadeCounter = int8_t(c - 1);
    if (c - 1 < 0) {
        a.flags &= uint8_t(~kActorDrawn);
        return true;
    }
    return false;
}
// exe 0x486880 / 0x4871c0: a tick of the fade in; true at its end (opaque)
bool fadeIn(AnimatedActor& a, GuardianFields& f, const FadeThresholds& table = fightFade) {
    const int c = f.fadeCounter;
    a.tint = 0;
    for (uint32_t k = 0; k < kFadeBones; ++k) {
        BoneColour colour = boneColour(a, k);
        const int t = table[k];
        if (t < c) {
            colour = 0;
        } else {
            int v = t - c;
            if (v > 0x10) v = 0x10;
            colour = uint16_t(v << 10 | v << 5 | v);
        }
    }
    f.fadeCounter = int8_t(c - 1);
    if (c - 1 < 0) {
        a.semiTransparency = ((0 & 1) << 4 | (1 & 3)) << 1; // exe 0x5e1a00(actor, 0, 1)
        return true;
    }
    return false;
}
// exe 0x486670(actor, player, 0x80)
void facePlayer(AnimatedActor& a) { turnYawTo(a.rotation.y, uint32_t(angleOf(player().pos, a.pos)), 0x80); }
// the pitch from the point to the player's chest, clamped to +-limit (the shots')
int16_t pitchToChest(const int16_t from[3], uint32_t limit) {
    int16_t chest[3];
    playerChest(chest);
    const uint32_t u = (uint32_t(pitchOf(from, chest)) - 0x800u) & 0xfff;
    uint32_t v = u;
    if (u < 0x800) {
        if (u > limit) v = limit;
    } else if (u < 0x1000 - limit) {
        v = 0x10000 - limit;
    }
    return int16_t(uint16_t(v));
}
// exe 0x486b10(actor, yaw offset, variant): a shot (pool 0x40 stage type 0xe) from bone 5, its pitch toward the
// player's chest clamped to +-0x200
void shoot(const AnimatedActor& a, int16_t dyaw, uint8_t variant) {
    int16_t hand[3];
    bonePosition(a, kShotBone, hand);
    const uint32_t address = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!address) return;
    ProjectileActor& e = projectileActorAt(address);
    e.flags = kActorLiveDrawn;
    e.type = kShotType;
    e.variant = variant;
    e.pos.x.whole = hand[0];
    e.pos.y.whole = hand[1];
    e.pos.z.whole = hand[2];
    e.rotation.y = int16_t(uint16_t(a.rotation.y) + uint16_t(dyaw));
    e.rotation.x = pitchToChest(hand, 0x200);
}
// exe 0x486be0(actor, yaw offset, offset, variant): a shot at the offset (<< 4, turned by the yaw) from bone 5, its
// pitch toward the player's chest clamped to +-0x100
void shootFrom(const AnimatedActor& a, int16_t dyaw, const ShortPoint& offset, uint8_t variant) {
    int16_t hand[3];
    bonePosition(a, kShotBone, hand);
    const uint32_t address = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!address) return;
    ProjectileActor& e = projectileActorAt(address);
    e.flags = kActorLiveDrawn;
    e.type = kShotType;
    e.variant = variant;
    const int16_t yaw = wrap16(int(a.rotation.y) + dyaw);
    e.rotation.y = yaw;
    int32_t o[3];
    rotateByYaw(yaw, int(offset.x) << 4, int(offset.y) << 4, int(offset.z) << 4, o); // exe 0x5e38d0
    e.pos.x.whole = int16_t(uint32_t(o[0] >> 16) + uint16_t(hand[0]));
    e.pos.y.whole = int16_t(uint32_t(o[1] >> 16) + uint16_t(hand[1]));
    e.pos.z.whole = int16_t(uint32_t(o[2] >> 16) + uint16_t(hand[2]));
    e.rotation.x = pitchToChest(hand, 0x100);
}
// the shots' flash shown (the hand part hidden) / hidden
void showFlash(AnimatedActor& a, bool on) {
    if (on) {
        a.parts[kHandPart].flags |= kPartHidden;
        a.parts[kFlashPart].flags &= uint8_t(~kPartHidden);
    } else {
        a.parts[kHandPart].flags &= uint8_t(~kPartHidden);
        a.parts[kFlashPart].flags |= kPartHidden;
    }
}
// exe 0x4866a0(actor, angle): the pitch and the roll leaned toward the turn to `angle` (their speeds eased, capped at
// +-0x60)
void lean(AnimatedActor& a, GuardianFields& f, uint32_t angle) {
    const int roll = a.rotation.z, pitch = a.rotation.x;
    int pitchSpeed = a.spin.x, rollSpeed = f.rollSpeed;
    const int d = angleDiff(uint32_t(int32_t(a.rotation.y)), angle);
    int e = angleDiff(uint32_t(pitch), (uint32_t(d + 0x400) < 0x800u ? 0x60u : uint32_t(-0x60)));
    if (e > 0 && (pitchSpeed += 4) > 0x180) pitchSpeed = 0x180;
    if (e < 0 && (pitchSpeed -= 4) < -0x180) pitchSpeed = -0x180;
    int newPitch = pitch + (pitchSpeed >> 4);
    if (newPitch >= 0x61) newPitch = 0x60;
    else if (newPitch < -0x60) newPitch = -0x60;
    e = angleDiff(uint32_t(roll), d < 1 ? 0x60u : uint32_t(-0x60));
    if (e > 0 && (rollSpeed += 4) > 0x180) rollSpeed = 0x180;
    if (e < 0 && (rollSpeed -= 4) < -0x180) rollSpeed = -0x180;
    int newRoll = roll + (rollSpeed >> 4);
    if (newRoll >= 0x61) newRoll = 0x60;
    else if (newRoll < -0x60) newRoll = -0x60;
    a.rotation.x = int16_t(newPitch);
    a.spin.x = int16_t(pitchSpeed);
    f.rollSpeed = int16_t(rollSpeed);
    a.rotation.z = int16_t(newRoll);
}
// exe 0x488170: the pitch and the roll back to 0 by 8
void unlean(AnimatedActor& a) {
    uint32_t p = uint32_t(int32_t(a.rotation.x)), r = uint32_t(int32_t(a.rotation.z));
    int d = angleDiff(p, 0);
    if (d > 0) p += 8;
    if (d < 0) p -= 8;
    if ((p + 8) * 0x10 < 0x100) p = 0;
    d = angleDiff(r, 0);
    if (d > 0) r += 8;
    if (d < 0) r -= 8;
    a.rotation.x = int16_t(p);
    a.rotation.z = int16_t((r + 8) * 0x10 < 0x100 ? 0u : r);
}
void leanOrUnlean(AnimatedActor& a, GuardianFields& f) {
    if (!(f.flags & kNoLean)) lean(a, f, uint32_t(int32_t(f.leanTarget)));
    else unlean(a);
}
// exe 0x486610: the hover's bob (its vertical speed swings between -0x10 and 0x10)
void bob(AnimatedActor& a, GuardianFields& f) {
    if (f.bobs != 1) return;
    a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
    if (a.velocity.y > 0x10) {
        a.velocity.y = 0x10;
        a.acceleration.y = -1;
    }
    if (a.velocity.y < -0x10) {
        a.velocity.y = -0x10;
        a.acceleration.y = 1;
    }
    a.pos.y = int32_t(uint32_t(a.pos.y) + uint32_t(int32_t(a.velocity.y) * 0x1000));
}
// exe 0x487bf0: the room collision (pushed out; back to the previous position when stuck), its results
void collide(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) {
    f.hits = 0;
    uint32_t r = actorTestMove(env, a); // exe 0x40f570
    if (r & kMoveNoCell) f.hits |= kHitNoCell;
    if (int32_t(r) >= 0) return;
    r = actorPushOut(env, a); // exe 0x410960
    if (int32_t(r) < 0) a.pos = a.prevPos;
    if (r & kSolidOnSides) f.hits |= kHitWall;
    if (r & kSolidAboveOrBelow) f.hits |= kHitFloor;
    if ((f.flags & kReflect) && reflectRotationOffWalls(a.rotation, r) != 0) f.hits |= kHitReflected;
}
// exe 0x486a60: the damage of its body's box (none while invulnerable or kNoDamage); 1 a player's / strong hit, 2
// dead
int takeDamage(AnimatedActor& a, GuardianFields& f) {
    const uint32_t v = f.bodyBox.result, d = v & kHitDamageMask;
    if (f.invulnerable != 0) return 0;
    int r = 0;
    if (d != 0 && (f.flags & kNoDamage) == 0) {
        r = (v & (kHitAttackByPlayer | kHitAttackStrong)) != 0;
        a.tint = kTintFlash;
        soundPlayAtPos(kSoundHurt, a.pos);
        if (hitPointsAdd(a, int(wrap16(-int32_t(d)))) != 0) r = 2;
        if (int(f.damageTaken) + int(d) < 0x1001) f.damageTaken = int16_t(uint16_t(f.damageTaken) + d);
        else f.damageTaken = 0x1000;
        if (v & kHitAttackStrongPush) f.invulnerable = 8;
    }
    return r;
}
// exe 0x486910: the head turned toward the player's bone 1 (from its bone 1), at most 0x80 a tick, within -0x80..0x200
void turnHead(const AnimatedActor& a, GuardianFields& f) {
    const int s = f.headTurn;
    uint32_t u = 0;
    if (!(f.flags & kNoHeadTurn)) {
        const ShortVec3& target = player().bonePoints[1];
        const int16_t to[3] = {target.x, target.y, target.z};
        int16_t from[3];
        bonePosition(a, kHeadBone, from);
        u = uint32_t(pitchOf(from, to)) + 0x800u;
    }
    int d = angleDiff(uint32_t(s), u);
    if (uint32_t(d + 0x80) > 0xff) u = uint32_t(s + (d < 1 ? -0x80 : 0x80));
    d = angleDiff(0, u);
    if (d >= 0x201) d = 0x200;
    else if (d < -0x80) d = -0x80;
    f.headTurn = int16_t(d);
}
// the animation stepped / set (a fresh animation 7 from key 0x1f), the head's turn added to its decoded angle, the pose
void animateWithHead(AnimatedActor& a, const GuardianFields& f) {
    const uint8_t anim = a.anim;
    if (anim == a.prevAnim) animStep(a);          // exe 0x5e1ea0
    else animSet(a, anim, anim == 7 ? 0x1f : 0); // exe 0x5e1c50
    LaneVec4 headAngles = decodedFrame().entries[kHeadEntry];
    headAngles.x = int16_t(uint16_t(f.headTurn) + uint16_t(headAngles.x));
    animPose(a); // exe 0x5e2d90
}
// the per-tick bookkeeping of the guardians: the animation of the last tick, the tint, the previous pose / contacts
void beginTick(AnimatedActor& a) {
    a.prevAnim = a.anim;
    a.tint = kTintNeutral;
    a.savePreviousPose();
    a.savePreviousContact();
}
// the model, the animation, the hit points (the stat row's << 4), still, the box, the class block cleared, the bob's
// direction up (the inits of the fight and of the cutscenes); false when the model failed (the actor freed)
bool setUpGuardian(AnimatedActor& a, const DataTable<CollisionBoxRecord>& box, uint8_t anim) {
    if (!bindActorModel(a, 0)) { // exe 0x5e0ba0
        freeActor(a);            // exe 0x5e1490
        return false;
    }
    a.anim = anim;
    a.prevAnim = kNoAnimShown;
    const uint16_t hp = hasAnimatedStats(a.type) ? uint16_t(uint32_t(animatedStats(a.type).hitPoints) << 4) : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.velocity.clearWords();
    a.spin.setXY(0, 0);
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.collisionBox.pointAt(box.stateAddress());
    a.interactionBox = nullptr;
    a.interaction = 0;
    a.airborne = 0;
    a.onPlatform = 0;
    a.platform = nullptr;
    a.actorHit = 0;
    a.clearClassBlock(); // exe 0x40dc40(+0x14c, +0x168)
    a.acceleration.y = -1;
    return true;
}
// the cloud of the spin (pool 0xa0 stage type 0xc) at the guardian, every fourth tick of the timer
void spinCloud(const AnimatedActor& a, const GuardianFields& f) {
    if ((f.timer & 3) != 0) return;
    const uint32_t address = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (!address) return;
    EffectActor& e = effectActorAt(address);
    CloudFields& c = classState<CloudFields>(e);
    e.flags = kActorLive;
    e.type = kCloudType;
    e.variant = 0;
    e.loopFlags |= kActorLateLoop;
    e.pos.x = a.pos.x.whole;
    e.pos.y = int16_t(uint16_t(a.pos.y.whole) - 0x20u);
    e.pos.z = a.pos.z.whole;
    c.spin = a.stepCounter;
    c.shade = cloudShades[rngStep() & 0xf];
}
// the spin's counter (the step counter) advanced by a random step
void spinStep(AnimatedActor& a) { a.stepCounter = uint8_t(((rngStep() & 0xf) + a.stepCounter) & 0x7f); }
// the spin's sound: started (0x18 ticks to the loop), or the loop every 0x11 ticks
void spinSoundStart(AnimatedActor& a, GuardianFields& f) {
    soundPlayAtPos(kSoundSpinStart, a.pos);
    f.soundTimer = 0x18;
    f.flags |= kNoHeadTurn;
}
bool spinSoundTick(GuardianFields& f) {
    --f.soundTimer;
    return f.soundTimer == 0;
}
void spinSoundLoop(const AnimatedActor& a, GuardianFields& f) {
    soundPlayAtPos(kSoundSpinLoop, a.pos);
    f.soundTimer = 0x11;
}
// the direction of a volley's side shots: the side the player is on
int playerSide(const AnimatedActor& a) {
    return angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(angleOf(player().pos, a.pos)));
}

// ---- variant 0: sub-states [0x892140] ------------------------------------------------------------------------------
// exe 0x4851b0: appear (fade in at the spawn point), then the hopper spawner
void arenaAppear(AnimatedActor& a, GuardianFields& f) {
    switch (a.step) {
    case 0:
        f.flags = kHiddenFlags;
        a.anim = 8;
        f.bobs = 1;
        a.step = 1;
        return;
    case 1:
        if (!a.animEnded()) return;
        a.anim = 4;
        fadeStart(a, f);
        f.count = 0;
        a.enterStep(2);
        [[fallthrough]];
    case 2: {
        dustStep(a, f);
        if (!fadeOut(a, f)) return;
        toSub(a, ArenaSub::Hidden);
        const uint32_t address = allocActorE0(); // exe 0x5e1440
        if (!address) return;
        ControllerActor& e = controllerActorAt(address);
        e.flags = kActorLive;
        e.type = kHopperSpawnerType;
        e.variant = 0;
        e.loopFlags |= kActorSweepGroup2;
        HopperSpawnerParams(e).guardian = a.slot;
        return;
    }
    default: return;
    }
}
// exe 0x485280: hidden for 600 ticks or until its hoppers are gone
void arenaHidden(AnimatedActor& a, GuardianFields& f) {
    if (a.step == 0) {
        f.flags = kHiddenFlags;
        f.bobs = 1;
        f.timer = 600;
        a.anim = 4;
        enemyGroupMark(f.group, kMarkHoppersCalled);
        a.enterStep(1);
        return;
    }
    --f.timer;
    if (f.timer != 0 && (enemyGroupMarks(f.group, EnemyGroupMarks::ThisTick) & kMarkHoppersGone) == 0) return;
    toSub(a, ArenaSub::Spin);
}
// exe 0x485300: re-appear at the centre, spin (the clouds), then the volleys
void arenaSpin(AnimatedActor& a, GuardianFields& f) {
    bool toVolleys = false, loop = false;
    switch (a.step) {
    case 0:
        f.flags = kHiddenFlags;
        f.bobs = 1;
        a.anim = 4;
        fadeStartLow(a, f);
        a.pos.x.whole = -0x200;
        a.pos.z.whole = -0x200;
        a.rotation.y = int16_t(angleOf(player().pos, a.pos));
        f.count = 0;
        a.enterStep(1);
        [[fallthrough]];
    case 1:
        dustStep(a, f);
        if (fadeIn(a, f)) {
            a.enterStep(2);
            f.timer = 0x2d;
            f.flags &= uint8_t(~kFading);
        }
        break;
    case 2:
        --f.timer;
        if (f.timer == 0) a.enterStep(3);
        break;
    case 3:
        if (a.stepCounter == 0) {
            a.stepCounter = 1;
            f.timer = 0x2d;
            if (enemyGroupMarks(f.group, EnemyGroupMarks::Either) & kMarkHoppersGone) f.timer = 1;
        }
        enemyGroupMark(f.group, kMarkRecall);
        --f.timer;
        if (f.timer == 0) a.enterStep(4);
        break;
    case 4:
        if (a.stepCounter == 0) {
            a.anim = 0;
            a.stepCounter = 1;
            break;
        }
        if (!a.animEnded()) break;
        if ((enemyGroupMarks(f.group, EnemyGroupMarks::Either) & kMarkHoppersGone) == 0) {
            a.enterStep(5);
            break;
        }
        toVolleys = true;
        break;
    case 5:
        enemyGroupMark(f.group, kMarkRecall);
        if (a.stepCounter == 0) {
            a.stepCounter = 1;
            a.anim = 10;
            spinSoundStart(a, f);
            break;
        }
        if (a.animEnded()) a.enterStep(6);
        loop = spinSoundTick(f);
        break;
    case 6:
        a.anim = 9;
        f.timer = 0x2d;
        a.enterStep(7);
        f.damageTaken = 0;
        [[fallthrough]];
    case 7:
        enemyGroupMark(f.group, kMarkSpin);
        spinCloud(a, f);
        spinStep(a);
        --f.timer;
        if (f.timer == 0) {
            a.anim = 0xb;
            a.enterStep(8);
        }
        loop = spinSoundTick(f);
        break;
    case 8:
        if (a.animEnded()) toVolleys = true;
        break;
    default: break;
    }
    if (loop) spinSoundLoop(a, f);
    if (toVolleys) toSub(a, ArenaSub::Volleys);
    facePlayer(a);
}
// exe 0x485620: volleys of three shots (their count by RNG), then fade out back to the wait
void arenaVolleys(AnimatedActor& a, GuardianFields& f) {
    switch (a.step) {
    case 0:
        f.flags = kNoLean;
        f.bobs = 1;
        a.anim = 0xd;
        f.timer = 0x2d0;
        f.count = 3;
        a.enterStep(2);
        break;
    case 1:
        a.anim = 0xd;
        f.count = volleyCounts[rngStep() & 0xf];
        a.enterStep(2);
        [[fallthrough]];
    case 2:
        if (a.animKey.events & 1) {
            const int d = playerSide(a);
            shoot(a, 0, 0);
            int16_t s;
            if (d < 0) {
                shoot(a, -0x80, 0);
                s = -0x100;
            } else {
                shoot(a, 0x80, 0);
                s = 0x100;
            }
            shoot(a, s, 0);
            showFlash(a, true);
            --f.count;
        }
        if (a.animKey.events & 2) showFlash(a, false);
        if (f.timer != 0) --f.timer;
        if (f.count == 0) a.enterStep(3);
        break;
    case 3:
        if (a.stepCounter == 0) {
            if (a.animKey.frame == 0x13) {
                a.anim = 0xe;
                showFlash(a, false);
                a.stepCounter = 1;
            }
        } else if (a.animEnded()) {
            a.enterStep(f.timer != 0 ? 4 : 5);
        }
        break;
    case 4:
        if (a.stepCounter == 0) {
            a.anim = 7;
            f.count = 0x3c;
            a.stepCounter = 1;
        }
        --f.count;
        if (f.count == 0) a.enterStep(1);
        break;
    case 5:
        f.bobs = 1;
        a.anim = 7;
        enemyGroupMark(f.group, kMarkRelease);
        fadeStart(a, f);
        f.count = 0;
        a.enterStep(6);
        [[fallthrough]];
    case 6:
        dustStep(a, f);
        if (fadeOut(a, f)) toSub(a, ArenaSub::Hidden);
        break;
    default: break;
    }
    facePlayer(a);
}

// ---- variants 1 / 2 (ST12 sub 5, script 0xb's records 0x89fc7c): exe 0x4858b0, sub-states [0x892170] --------------
// The arena of the five static blades. It vanishes where it stands; re-appears beside the player (0x2000 toward / away
// from the centre (0, -0x200) by the player's distance), or at the centre once its retreats (1 at init, 3 after a
// spin, one less when its hit points fall under half) are spent; shoots volleys of three shots from the hands (three
// volleys) and fades out -- or jumps at a close player, landing with the ground shock; spins (the clouds) until the
// blades end a slide round. Death: flag 0x5b3 every tick the damage reports it (the actor stays; script 0xc sweeps it).
// exe 0x486a10(actor, yaw, variant): one skid spark 0x40 above the actor
void dustAbove(const AnimatedActor& a, uint16_t yaw, uint8_t variant) {
    const uint32_t address = allocActorA0(0); // exe 0x5e13b0
    if (!address) return;
    EffectActor& e = effectActorAt(address);
    e.flags = kActorLiveDrawn;
    e.variant = variant;
    e.type = typeByte(GlobalEffectType::SkidSparks);
    e.pos.x = a.pos.x.whole;
    e.pos.y = int16_t(uint16_t(a.pos.y.whole) - 0x40u);
    e.pos.z = a.pos.z.whole;
    classState<SkidSparkFields>(e).yaw = yaw;
}
// the arena's centre (0, -0x200) as the exe's stack point
constexpr int32_t kCentreX = 0, kCentreZ = int32_t(0xfe000000u);
// exe 0x485930: fade out where it stands, 60 ticks, then re-appear
void bladeArenaVanish(AnimatedActor& a, GuardianFields& f) {
    switch (a.step) {
    case 0:
        f.flags = kHiddenFlags;
        f.bobs = 1;
        f.count = 0;
        fadeStart(a, f); // exe 0x487a80
        f.flags = uint8_t((f.flags & ~kFading) | kHarmless);
        a.enterStep(1);
        break;
    case 1: break;
    case 2:
        --f.timer;
        if (f.timer != 0) return;
        toSub(a, BladeArenaSub::Reappear);
        return;
    default: return;
    }
    dustStep(a, f);
    if (fadeOut(a, f)) {
        f.timer = 0x3c;
        a.enterStep(2);
    }
}
// exe 0x4859d0: re-appear beside the player (or at the centre), then attack
void bladeArenaReappear(AnimatedActor& a, GuardianFields& f) {
    if (a.step == 0) {
        f.bobs = 1;
        a.anim = 4;
        if (a.hitPoints < (a.maxHitPoints >> 1) && f.retreats != 0) --f.retreats;
        fadeStartLow(a, f); // exe 0x4867d0
        if (f.retreats != 0) {
            const Player& p = player();
            const int ang = angleOf(p.pos.x, p.pos.z, kCentreX, kCentreZ); // exe 0x5e3bc0
            const int32_t centre[3] = {kCentreX, 0, kCentreZ};
            const int d = planarDistance(p.pos, centre); // exe 0x5e3b00
            int32_t o[3];
            rotateByYaw(ang, 0, 0, d > 0x300 ? -0x2000 : 0x2000, o); // exe 0x5e38d0
            a.pos.x = o[0] + kCentreX;
            a.pos.y = int32_t(0xff000000u);
            a.pos.z = o[2] + kCentreZ;
        } else {
            a.pos.y = int32_t(0xfe740000u);
            a.pos.z = kCentreZ;
            a.pos.x = 0;
        }
        a.rotation.y = int16_t(angleOf(player().pos, a.pos));
        f.flags = kHarmless | kNoHeadTurn | kNoLean;
        f.count = 0;
        a.enterStep(1);
    } else if (a.step != 1) {
        facePlayer(a);
        return;
    }
    dustStep(a, f);
    if (fadeIn(a, f)) {
        f.flags &= uint8_t(~kHarmless);
        const int d = planarDistance(a.pos, player().pos);
        a.subState = uint8_t((d > 0x300 && f.volleys < 3) ? BladeArenaSub::HandVolleys : BladeArenaSub::Jump);
        if (f.retreats == 0) a.subState = uint8_t(BladeArenaSub::Spin);
        a.restartStep();
    }
    facePlayer(a);
}
// the fade out back to the re-appearance (the end of the volleys / the spin)
void bladeArenaFadeStart(AnimatedActor& a, GuardianFields& f) {
    fadeStart(a, f);
    f.flags = uint8_t((f.flags & ~kFading) | kHarmless);
}
// exe 0x485b90: volleys of three shots from the hands (three volleys), then fade out (or the jump at a close player)
void bladeArenaHandVolleys(AnimatedActor& a, GuardianFields& f) {
    switch (a.step) {
    case 0:
        f.flags = kDeflects | kNoLean;
        f.bobs = 1;
        f.timer = 0x3c;
        ++f.volleys;
        a.anim = 4;
        a.enterStep(1);
        [[fallthrough]];
    case 1:
        --f.timer;
        if (f.timer == 0) a.enterStep(2);
        break;
    case 2:
        if (a.stepCounter == 0) {
            a.anim = 0;
            a.stepCounter = 1;
        } else if (a.animEnded()) {
            a.enterStep(3);
            f.flags &= uint8_t(~kDeflects);
        }
        break;
    case 3:
        f.timer = 3;
        [[fallthrough]];
    case 4:
        a.anim = 0xd;
        f.count = 3;
        a.enterStep(5);
        [[fallthrough]];
    case 5:
        if (a.animKey.events & 1) {
            const int d = playerSide(a);
            const int16_t s = uint32_t(d + 0x80) < 0x100 ? 0 : (d < 0 ? -0x100 : 0x100);
            shootFrom(a, s, handOffsets[0], 1);
            shootFrom(a, s, handOffsets[1], 1);
            shootFrom(a, s, handOffsets[2], 1);
            showFlash(a, true);
            --f.count;
        }
        if (a.animKey.events & 2) showFlash(a, false);
        if (f.count == 0) a.enterStep(6);
        break;
    case 6:
        if (a.stepCounter == 0) {
            if (a.animKey.frame == 0x13) {
                a.anim = 0xe;
                showFlash(a, false);
                a.stepCounter = 1;
            }
        } else if (a.animEnded()) {
            --f.timer;
            a.enterStep(f.timer != 0 ? 7 : 8);
        }
        break;
    case 7:
        if (a.stepCounter == 0) {
            a.anim = 7;
            f.count = 0x1e;
            a.stepCounter = 1;
        }
        --f.count;
        if (f.count == 0) a.enterStep(4);
        if (planarDistance(a.pos, player().pos) < 0x100) {
            a.subState = uint8_t(BladeArenaSub::Jump);
            a.enterStep(3);
            f.flags = kNoHeadTurn | kNoLean | kReflect;
        }
        break;
    case 8:
        f.bobs = 1;
        a.anim = 7;
        enemyGroupMark(f.group, kMarkRelease);
        fadeStart(a, f);
        f.count = 0;
        f.flags = uint8_t((f.flags & ~kFading) | kHarmless);
        a.enterStep(9);
        [[fallthrough]];
    case 9:
        dustStep(a, f);
        if (fadeOut(a, f)) toSub(a, BladeArenaSub::Reappear);
        break;
    default: break;
    }
    facePlayer(a);
}
// the ground shock (pool 0x40 stage type 3 variant 9) at the guardian
void groundShock(const AnimatedActor& a) {
    const uint32_t address = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!address) return;
    ProjectileActor& e = projectileActorAt(address);
    ShockParams params(e);
    e.flags = kActorLive;
    e.loopFlags |= kActorSweepGroup2;
    e.type = kShockType;
    e.variant = kShockVariant;
    params.rings = 1;
    params.phase = 0;
    e.pos = a.pos;
}
// exe 0x485ee0: a jump at the player, the ground shock on landing, fade out
void bladeArenaJump(AnimatedActor& a, GuardianFields& f) {
    switch (a.step) {
    case 0:
        f.flags = kNoHeadTurn | kNoLean | kReflect;
        f.bobs = 1;
        a.anim = 0;
        a.enterStep(1);
        return;
    case 1:
        facePlayer(a);
        if (a.animEnded()) a.enterStep(2);
        return;
    case 2:
        facePlayer(a);
        if (a.stepCounter == 0) {
            a.anim = 7;
            f.timer = 0x4b;
            a.stepCounter = 1;
            return;
        }
        if (uint16_t(f.timer) != 0) --f.timer;
        if (a.animEnded() && uint16_t(f.timer) == 0) a.enterStep(3);
        return;
    case 3: {
        a.anim = 1;
        f.volleys = 0;
        f.bobs = 0;
        const int d = planarDistance(a.pos, player().pos);
        int v;
        if (d > 0x200) {
            a.velocity.z = -0x200;
            v = launchSpeed(a.pos, player().pos, 0x200, 0x40, 0x400); // exe 0x5e3ee0
        } else if (d < 0x100) {
            a.velocity.z = 0;
            a.velocity.y = -0x400;
            a.enterStep(4);
            return;
        } else {
            a.velocity.z = -0xa0;
            v = launchSpeed(a.pos, player().pos, 0xa0, 0x40, 0x200);
        }
        a.velocity.y = int16_t(v);
        a.enterStep(4);
        return;
    }
    case 4:
        if (a.animEnded()) {
            a.anim = 2;
            soundPlayAtPos(kSoundJump, a.pos);
            a.enterStep(5);
        }
        return;
    case 5:
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z); // exe 0x5e3690
        a.velocity.y = int16_t(uint16_t(a.velocity.y) + 0x40u);
        if (a.velocity.y >= 0) {
            a.anim = 3;
            a.enterStep(6);
            f.flags |= kLanding;
        }
        return;
    case 6:
        if (f.hits & kHitReflected) {
            a.enterStep(7);
            a.anim = 5;
            cameraShakeAt(0x1e00, 0x300, 0x200, 0x1000, a.pos); // exe 0x40eab0
            groundShock(a);
            soundPlayAtPos(kSoundLand, a.pos);
            return;
        }
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        a.velocity.y = int16_t(uint16_t(a.velocity.y) + 0x40u);
        if (a.stepCounter == 0 && a.pos.y.whole >= -0x1c0) {
            a.anim = 0xc;
            a.stepCounter = 1;
        }
        return;
    case 7:
        if (a.animEnded()) a.enterStep(8);
        return;
    case 8:
        a.anim = 6;
        a.velocity.y = -0x200;
        a.velocity.z = 0x100;
        fadeStart(a, f);
        f.count = 0;
        f.flags = uint8_t((f.flags & ~kFading) | kHarmless);
        a.enterStep(9);
        [[fallthrough]];
    case 9:
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        a.velocity.y = int16_t(uint16_t(a.velocity.y) + 0x20u);
        if (a.velocity.y >= 0) a.velocity.z = int16_t(uint16_t(a.velocity.z) + 0x40u);
        if (int32_t(0x1115c0u << (uint8_t(f.fadeCounter) & 0x1f)) < 0) {
            dustAbove(a, uint16_t(uint32_t(uint16_t(a.rotation.y)) + 0x800u), kJumpDustVariant);
            ++f.count;
        }
        if (fadeOut(a, f)) toSub(a, BladeArenaSub::Reappear);
        return;
    default: return;
    }
}
// exe 0x486230: spin at the centre (the clouds) until the blades end a slide round, then fade out
void bladeArenaSpin(AnimatedActor& a, GuardianFields& f) {
    bool loop = false;
    switch (a.step) {
    case 0:
        f.flags = kDeflects | kNoLean;
        f.bobs = 1;
        a.anim = 4;
        f.timer = 0x3c;
        a.enterStep(1);
        [[fallthrough]];
    case 1:
        --f.timer;
        if (f.timer == 0) a.enterStep(2);
        break;
    case 2:
        if (a.stepCounter == 0) {
            a.anim = 0;
            a.stepCounter = 1;
        } else if (a.animEnded()) {
            a.enterStep(3);
        }
        break;
    case 3:
        if (a.stepCounter == 0) {
            a.stepCounter = 1;
            a.anim = 10;
            spinSoundStart(a, f);
            break;
        }
        if (a.animEnded()) a.enterStep(4);
        loop = spinSoundTick(f);
        break;
    case 4:
        a.anim = 9;
        f.timer = 0;
        f.count = 1;
        a.enterStep(5);
        f.damageTaken = 0;
        [[fallthrough]];
    case 5:
        enemyGroupMark(f.group, kMarkSpin);
        spinCloud(a, f);
        spinStep(a);
        --f.timer;
        if (enemyGroupMarks(f.group, EnemyGroupMarks::LastTick) & kMarkBladesBack) {
            --f.count;
            if (f.count == 0) {
                a.anim = 0xb;
                a.enterStep(6);
            }
        }
        loop = spinSoundTick(f);
        break;
    case 6:
        if (a.animEnded()) {
            f.bobs = 1;
            a.anim = 8;
            a.enterStep(7);
        }
        break;
    case 7:
        if (a.stepCounter == 0) {
            if (a.animEnded()) {
                a.anim = 4;
                f.timer = 0x3c;
                a.stepCounter = 1;
            }
        } else {
            --f.timer;
            if (f.timer == 0) {
                f.flags = kHiddenFlags;
                f.bobs = 1;
                bladeArenaFadeStart(a, f);
                a.enterStep(8);
            }
        }
        break;
    case 8:
        if (fadeOut(a, f)) {
            f.retreats = 3;
            toSub(a, BladeArenaSub::Reappear);
        }
        break;
    default: break;
    }
    if (loop) spinSoundLoop(a, f);
    facePlayer(a);
}
// exe 0x4858b0 (variants 1 and 2)
void bladeArenaFight(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) {
    if (takeDamage(a, f) == 2) {
        setStoryFlag(kFlagBladeGuardianBeaten);
        return;
    }
    switch (BladeArenaSub(a.subState)) {
    case BladeArenaSub::Vanish: bladeArenaVanish(a, f); break;
    case BladeArenaSub::Reappear: bladeArenaReappear(a, f); break;
    case BladeArenaSub::HandVolleys: bladeArenaHandVolleys(a, f); break;
    case BladeArenaSub::Jump: bladeArenaJump(a, f); break;
    case BladeArenaSub::Spin: bladeArenaSpin(a, f); break;
    default: notPortedAt(kMandaGuardianPort, "0x4858b0 sub-state", a.subState);
    }
    leanOrUnlean(a, f);
    bob(a, f);
    collide(env, a, f);
}

// [0x8921a0 + variant]: variant 0's death (exe 0x486520): the kill counter, the hit mute, the spin's end, the fade out,
// flag 0x5b1
void dying(AnimatedActor& a, GuardianFields& f) {
    if (a.variant != kVariantArena) notPortedAt(kMandaGuardianPort, "0x486500 death of variant", a.variant);
    switch (DeathSub(a.subState)) {
    case DeathSub::Start: {
        countDefeat(); // exe 0x5e53e0
        const uint32_t address = allocActorE0(); // exe 0x5e1440
        if (address) {
            ControllerActor& e = controllerActorAt(address);
            e.flags = kActorLive;
            e.type = kHitMuteType;
            e.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
            e.variant = kMutedGroups;
            HitMuteFields(e).ticks = 300;
        }
        const uint8_t anim = a.anim;
        if (anim == 10) {
            a.subState = uint8_t(DeathSub::SpinEnds);
        } else if (anim == 9) {
            a.anim = 0xb;
            a.subState = uint8_t(DeathSub::Collapse);
        } else {
            a.subState = uint8_t((anim != 0xb) + uint8_t(DeathSub::Collapse));
        }
        return;
    }
    case DeathSub::SpinEnds:
        if (a.animEnded()) {
            a.anim = 0xb;
            a.subState = uint8_t(DeathSub::Collapse);
        }
        return;
    case DeathSub::Collapse:
        if (a.animEnded()) {
            a.anim = 7;
            a.subState = uint8_t(DeathSub::FadeStart);
        }
        return;
    case DeathSub::FadeStart:
        f.bobs = 1;
        fadeStart(a, f);
        a.subState = uint8_t(DeathSub::Fading);
        [[fallthrough]];
    case DeathSub::Fading:
        if (fadeOut(a, f)) {
            setStoryFlag(kFlagGuardianBeaten);
            a.subState = uint8_t(DeathSub::Gone);
        }
        return;
    default: return;
    }
}
// exe 0x485130
void arenaFight(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) {
    if (takeDamage(a, f) == 2) {
        a.enterState(uint8_t(GuardianState::Dying));
        dying(a, f);
        return;
    }
    switch (ArenaSub(a.subState)) {
    case ArenaSub::Appear: arenaAppear(a, f); break;
    case ArenaSub::Hidden: arenaHidden(a, f); break;
    case ArenaSub::Spin: arenaSpin(a, f); break;
    case ArenaSub::Volleys: arenaVolleys(a, f); break;
    default: notPortedAt(kMandaGuardianPort, "0x485130 sub-state", a.subState);
    }
    leanOrUnlean(a, f);
    bob(a, f);
    collide(env, a, f);
}
// exe 0x485000
void guardianInit(AnimatedActor& a, GuardianFields& f) {
    if (!setUpGuardian(a, fightBox, 4)) return;
    if (a.variant == kVariantBladeArena) f.retreats = 1;
    f.group = int8_t(enemyGroupFindFor(a, 0));
    hudAddBossGauge(a, 3, 3, 0x20a0ff);
    showFlash(a, false);
    a.enterState(uint8_t(GuardianState::Fight));
}
// exe 0x4850f0: [0x892134 + variant]: 0 exe 0x485130, 1 / 2 exe 0x4858b0
void guardianFight(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) {
    switch (a.variant) {
    case kVariantArena: arenaFight(env, a, f); break;
    case kVariantBladeArena:
    case kVariantBladeArenaB: bladeArenaFight(env, a, f); break;
    default: notPortedAt(kMandaGuardianPort, "0x4850f0 variant", a.variant);
    }
    if (f.invulnerable != 0) --f.invulnerable;
    turnHead(a, f);
}
// its hit entries: the body's capsule (solid, deflecting while kDeflects, a bump while kHarmless) and, while it lands,
// the landing's box at its foot
void submitHits(AnimatedActor& a, GuardianFields& f) {
    f.bodyBox.result = 0;
    f.bodyBox.previousX = a.prevPos.x.whole;
    f.bodyBox.previousZ = a.prevPos.z.whole;
    const uint8_t b = f.flags;
    if (!(a.flags & kActorDrawn) || (b & kFading)) return;
    const int16_t top[3] = {a.pos.x.whole, wrap16(int(a.pos.y.whole) - 0xb4), a.pos.z.whole};
    const int16_t low[3] = {a.pos.x.whole, wrap16(int(a.pos.y.whole) - 0x40), a.pos.z.whole};
    const AnimatedActorStats stats = animatedStats(a.type);
    uint32_t kind = (b & kDeflects) ? kHitAttackDeflects : kHitAttackSolid, damage = stats.attack;
    if (b & kHarmless) {
        kind = kHitAttackBump;
        damage = 0;
    }
    hitListSubmitCapsule(top, 8, low, 8, f.bodyBox, // exe 0x5e4190
                         hitOwner(a.slot) | ((b & kNoLockOn) ? kHitNoLockOn : 0u) | hitGroup(kHitGroupBodies) |
                             hitCapsule(1) | hitHeightLevel(7),
                         damage | kind);
    if (b & kLanding) {
        const int16_t foot[3] = {0, 0x10, -0x30};
        int16_t p[3];
        bonePointOffset(a, kFootBone, foot, p); // exe 0x5e1970
        hitListSubmit(p, f.landingBox, // exe 0x5e4120
                      hitOwner(a.slot) | hitGroup(kHitGroupHeavyThrown) | kHitNoLockOn | hitRadius(0x20),
                      stats.strongAttack | kHitAttackHarmless | kHitAttackStrong | kHitAttackThrown);
    }
}
// exe 0x484e00
void guardianUpdate(const PlayerEnv& env, AnimatedActor& a) {
    GuardianFields& f = classState<GuardianFields>(a);
    beginTick(a);
    switch (GuardianState(a.state)) { // [0x892128]
    case GuardianState::Init: guardianInit(a, f); break;
    case GuardianState::Fight: guardianFight(env, a, f); break;
    case GuardianState::Dying: dying(a, f); break;
    default: notPortedAt(kMandaGuardianPort, "0x484e00 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    animateWithHead(a, f);
    submitHits(a, f);
    // exe 0x5f20f0(actor, 0x60, 0x100, 0): the shadow (render output)
}

enum class ShotState : uint8_t { Init = 0, Fly = 1 }; // exe 0x5c6000 / 0x5c6090
enum class ShotSub : uint8_t { Flying = 0, Falling = 1 };
const DataTable<CollisionBoxRecord> shotBox{TableId::MandaGuardianShotBox};
constexpr uint8_t kShotWall = 0x01, kShotGround = 0x02;
// the trail segment (pool 0xe0 type 0x10, exe 0x424800): its angles (roll / yaw of the shot's rotation and of its
// previous one, >> 6), the shot's current and previous points (its draw's), its ticks
constexpr uint8_t kShotTrailType = 0x10;

// exe 0x5c6280: the room collision (pushed out; back to the previous position when stuck): kShotWall / kShotGround
uint32_t shotCollide(const PlayerEnv& env, ProjectileActor& s) {
    uint32_t b = 0;
    if (int32_t(actorTestMove(env, s)) < 0) {   // exe 0x40f570
        const uint32_t r = actorPushOut(env, s); // exe 0x410960
        if (int32_t(r) < 0) s.pos = s.prevPos;
        b = (r & kSolidOnSides) != 0 ? kShotWall : 0u;
        if (r & kSolidAboveOrBelow) b |= kShotGround;
    }
    return b;
}
// exe 0x5c6000
void shotInit(ProjectileActor& s, ShotFields& f) {
    if (!bindActorModel(s, 0)) { // exe 0x5e0ba0
        freeActor(s);            // exe 0x5e1500
        return;
    }
    s.maxHitPoints = 0xff;
    s.hitPoints = 0xff;
    s.velocity.clearWords();
    s.rotation.z = 0;
    s.interactionBox = nullptr;
    s.interaction = 0;
    s.onPlatform = 0;
    s.flags |= kActorDrawn;
    s.collisionBox.pointAt(shotBox.stateAddress());
    s.airborne = 1;
    f.clearClassBytesBytes(); // exe 0x40dc40(+0xbc, +0xd8)
    f.heading = s.rotation;
    soundPlayAtPos(kSoundShot, s.pos);
    s.enterState(uint8_t(ShotState::Fly));
}
// the trail segment between the shot's current and previous points
void spawnShotTrail(const ProjectileActor& s) {
    const uint32_t address = allocActorE0(); // exe 0x5e1440
    if (!address) return;
    ControllerActor& e = controllerActorAt(address);
    ShotTrailFields& t = classState<ShotTrailFields>(e);
    e.flags = kActorLiveDrawn;
    e.type = kShotTrailType;
    e.variant = 0;
    e.loopFlags |= kActorLateLoop;
    t.roll = uint8_t((uint32_t(uint16_t(s.rotation.z)) >> 6) & 0x3f);
    t.yaw = uint8_t((uint32_t(uint16_t(-int32_t(uint16_t(s.rotation.y)))) >> 6) & 0x3f);
    t.prevRoll = uint8_t((uint32_t(uint16_t(s.prevRotation.z)) >> 6) & 0x3f);
    t.prevYaw = uint8_t((uint32_t(uint16_t(-int32_t(uint16_t(s.prevRotation.y)))) >> 6) & 0x3f);
    t.head.x = s.pos.x.whole;
    t.head.y = s.pos.y.whole;
    t.head.z = s.pos.z.whole;
    t.tail.x = s.prevPos.x.whole;
    t.tail.y = s.prevPos.y.whole;
    t.tail.z = s.prevPos.z.whole;
}
// exe 0x5c6090
void shotFly(const PlayerEnv& env, ProjectileActor& s, ShotFields& f) {
    s.airborne = 1;
    if (ShotSub(s.subState) != ShotSub::Flying) {
        moveAlongYaw(s.pos, f.heading.y, 0, s.velocity.y, 0x90); // exe 0x5e36f0
        s.velocity.y = int16_t(uint16_t(s.velocity.y) + 0x40u);
        s.rotation.y = int16_t(uint16_t(s.rotation.y) - 0x200u);
        s.rotation.z = int16_t(uint16_t(s.rotation.z) + 0x200u);
        if (shotCollide(env, s) & kShotGround) freeActor(s); // exe 0x5e1500
        return;
    }
    moveAlongRotation(s.pos, f.heading, 0, 0, -0x480); // exe 0x5e3810
    s.rotation.y = int16_t(uint16_t(s.rotation.y) - 0x200u);
    if ((shotCollide(env, s) & (kShotWall | kShotGround)) || (f.hitBox.result & kHitAttackBump)) {
        soundPlayAtPos(kSoundShotHit, s.pos);
        s.velocity.y = -0x280;
        s.subState = uint8_t(ShotSub::Falling);
        return;
    }
    const int16_t cur[3] = {s.pos.x.whole, s.pos.y.whole, s.pos.z.whole};
    const int16_t prev[3] = {s.prevPos.x.whole, s.prevPos.y.whole, s.prevPos.z.whole};
    const AnimatedActorStats stats = animatedStats(kGuardianType);
    f.hitBox.result = 0;
    f.hitBox.previousX = s.prevPos.x.whole;
    f.hitBox.previousZ = s.prevPos.z.whole;
    hitListSubmitCapsule(cur, 8, prev, 8, f.hitBox, hitOwner(s.slot) | hitGroup(kHitGroupHeavyThrown) | hitCapsule(1),
                         (s.variant != 0 ? kHitAttackThrown : 0u) | stats.shotAttack | kHitAttackStrong);
    spawnShotTrail(s);
}
void shotUpdate(const PlayerEnv& env, ProjectileActor& s) {
    ShotFields& f = classState<ShotFields>(s);
    s.savePreviousPose();
    s.savePreviousContact();
    switch (ShotState(s.state)) {
    case ShotState::Init: shotInit(s, f); return;
    case ShotState::Fly: shotFly(env, s, f); return;
    default: notPortedAt(kMandaGuardianPort, "0x5c5fb0 state", s.state);
    }
}

// ---- the shot's trail segment (exe 0x424800: pool 0xe0 type 0x10, late) ----------------------------------------------
// States [0x88bdb4]: started, then 7 ticks, then freed. Its quads (two gouraud bands between the shot's current /
// previous points, its angles, the fading colour exe 0x424ca0) are OT packets: render output, left out.
enum class TrailState : uint8_t { Start = 0, Run = 1 };
enum class TrailStep : uint8_t { Start = 0, Fading = 1, Done = 2 };
void shotTrailTick(ControllerActor& e) {
    ShotTrailFields& t = classState<ShotTrailFields>(e);
    switch (TrailState(e.state)) {
    case TrailState::Start:
        e.state = uint8_t(TrailState::Run);
        e.step = uint8_t(TrailStep::Start);
        break;
    case TrailState::Run: break;
    default: notPortedAt(kMandaGuardianPort, "0x424800 state", e.state);
    }
    switch (TrailStep(e.step)) { // exe 0x424840
    case TrailStep::Start:
        e.step = uint8_t(TrailStep::Fading);
        t.ticks = 7;
        [[fallthrough]];
    case TrailStep::Fading:
        --t.ticks;
        if (t.ticks == 0) ++e.step;
        break;
    case TrailStep::Done: freeActor(e); return; // exe 0x5e15c0
    default: break;
    }
    // the trail's quads (OT packets): render output
}

// ---- the hit mute (exe 0x425aa0: pool 0xe0 type 0x18, late) -----------------------------------------------------------
// For its ticks the hit entries of the groups in its mask (the variant byte) are submitted in every group (harmless to
// the pairs' tests); the guardian's death spawns it for the bodies / thrown boxes / liftables, 300 ticks.
void hitMuteTick(ControllerActor& e) {
    HitMuteFields m(e);
    const uint8_t mask = e.variant;
    ActorSystemState& sys = actorSystem();
    const uint32_t n = sys.hitCount;
    if (n > 1)
        for (uint32_t i = 0; i < n; ++i) {
            HitEntry& h = sys.hitList[i];
            if (mask & (1u << ((h.owner >> kHitGroupShift) & kHitGroupMask))) h.owner = h.owner | kHitGroupsAll;
        }
    --m.ticks;
    if (m.ticks == 0) freeActor(e); // exe 0x5e15c0
}

// ---- the cloud of the guardian's spin (exe 0x4331a0: pool 0xa0, ST13 stage type 0xc, late) ---------------------------
// [0x88cb48 + variant]: variant 0 (exe 0x4331c0) grows (size 3 -> 0x10), holds 0x10 ticks, shrinks (freed under 3).
// Only its game state is ported: in the view's depth range ([0xc0, the stage's depth range * 4 - 0x152)) it writes
// its wobble ring from the sine table (the phase stepped by the table 0x88cb28); its strips (OT packets) are render
// output.
enum class CloudState : uint8_t { Spawn = 0, Grow = 1, Hold = 2, Shrink = 3 };
const DataTable<ScalarTable<uint8_t, 0x20>> cloudPhaseSteps{TableId::MandaGuardianCloudPhaseSteps};
void cloudTick(EffectActor& e) {
    if (e.variant != 0) notPortedAt(kMandaGuardianPort, "0x4331a0 variant", e.variant);
    CloudFields& c = classState<CloudFields>(e);
    switch (CloudState(e.state)) {
    case CloudState::Spawn:
        for (size_t i = 0; i < c.ring.size(); ++i) c.ring[i] = 0; // exe 0x40dc40(+0x18, +0x24)
        e.state = uint8_t(CloudState::Grow);
        c.ringIndex = 0;
        c.size = 3;
        c.shrinkTicks = 0;
        c.phase = 0;
        c.phaseStep = int8_t(crtRand() & 0x1f);
        [[fallthrough]];
    case CloudState::Grow:
        ++c.size;
        if (c.size > 0xf) {
            c.size = 0x10;
            c.holdTicks = 0x10;
            e.state = uint8_t(CloudState::Hold);
        }
        break;
    case CloudState::Hold:
        --c.holdTicks;
        if (c.holdTicks == 0) e.state = uint8_t(CloudState::Shrink);
        break;
    case CloudState::Shrink:
        --c.size;
        ++c.shrinkTicks;
        if (c.size < 3) {
            freeActor(e); // exe 0x5e1580
            return;
        }
        break;
    default: break;
    }
    const int16_t centre[3] = {e.pos.x, e.pos.y, e.pos.z};
    const CameraPoint cp = cameraPoint(centre);
    if (!(cp.view[2] + 0x152 < int32_t(uint32_t(stage().depthRange) * 4) && cp.view[2] >= 0xc0)) return;
    c.ring[c.ringIndex] = uint8_t((int32_t(sineAt(int32_t(c.phase))) * 0x18) >> 0xb);
    c.ringIndex = uint8_t((c.ringIndex + 1u) & 0xf);
    c.phase = int8_t((uint32_t(cloudPhaseSteps[size_t(int32_t(c.phaseStep))]) + uint8_t(c.phase)) & 0x3f);
    c.phaseStep = int8_t((uint8_t(c.phaseStep) + 1u) & 0x1f);
}

// ---- the guardian of script 9's cutscene (exe 0x486d00: pool 0x20 type 0x24 sub 1) -----------------------------------
// States [0x8921d0]: its set-up (exe 0x486dd0; its first phase starts hidden), then [0x8921d8 + variant]: each phase,
// keyed on the script's step sub / tick, moves the variant on to the next: it fades in at tick 0x40; in the step sub 3
// the animations 0x12 / 0x13 at 0xf / 0x78, the fade out at 200; in the step sub 4 it re-appears at (-0x200, -0x100,
// -0x180) at 0x1e; in the step sub 5 it stops bobbing. Then the fight's lean / bob / collision / head. The script frees
// it (content/manda/manda_ruin_scripts).
enum class CastState : uint8_t { Init = 0, Act = 1 }; // exe 0x486dd0 / 0x486eb0
enum class CastPhase : uint8_t {
    FadeIn = 0,   // exe 0x486f10
    Speech = 1,   // exe 0x486fa0
    Reappear = 2, // exe 0x487080
    Settle = 3,   // exe 0x487100
};
// exe 0x486dd0
void castInit(AnimatedActor& a) {
    if (!setUpGuardian(a, castBox, 4)) return;
    if (CastPhase(a.variant) == CastPhase::FadeIn) {
        a.anim = 7;
        a.flags &= uint8_t(~kActorDrawn);
    }
    showFlash(a, false);
    a.enterState(uint8_t(CastState::Act));
}
void nextPhase(AnimatedActor& a, CastPhase p) {
    a.variant = uint8_t(p);
    a.subState = 0;
}
// [0x8921d8 + variant]
void castStep(AnimatedActor& a, GuardianFields& f) {
    const ScriptState& s = scriptState();
    const uint8_t sub = s.stepSub;
    const int32_t t = s.stepTick;
    switch (CastPhase(a.variant)) {
    case CastPhase::FadeIn:
        if (a.subState == 0) {
            if (t != 0x40) return;
            f.count = 0;
            f.flags = kHiddenFlags;
            f.bobs = 1;
            fadeStart(a, f); // exe 0x487120 = 0x487a80 + the vertical speed 0
            a.velocity.y = 0;
            fadeIn(a, f, castFade);
            a.subState = 1;
        } else if (a.subState == 1) {
            dustStepShifted(a, f);
            if (fadeIn(a, f, castFade)) nextPhase(a, CastPhase::Speech);
        }
        return;
    case CastPhase::Speech:
        if (sub != 3) return;
        switch (a.subState) {
        case 0:
            if (t == 0xf) {
                a.anim = 0x12;
                a.subState = 1;
            }
            return;
        case 1:
            if (t == 0x78) {
                a.anim = 0x13;
                a.subState = 2;
            }
            return;
        case 2:
            if (t != 200) return;
            f.bobs = 1;
            a.anim = 7;
            fadeStart(a, f);
            f.count = 0;
            a.subState = 3;
            [[fallthrough]];
        case 3:
            dustStepShifted(a, f);
            if (fadeOut(a, f, castFade)) nextPhase(a, CastPhase::Reappear);
            return;
        default: return;
        }
    case CastPhase::Reappear:
        if (sub != 4) return;
        if (a.subState == 0) {
            if (t != 0x1e) return;
            a.anim = 0x1c;
            f.bobs = 1;
            a.pos.x = int32_t(0xfe000000u);
            a.pos.y = int32_t(0xff000000u);
            a.pos.z = int32_t(0xfe800000u);
            fadeStart(a, f);
            a.velocity.y = 0;
            a.subState = 1;
        } else if (a.subState != 1) {
            return;
        }
        if (fadeIn(a, f, castFade)) nextPhase(a, CastPhase::Settle);
        return;
    case CastPhase::Settle:
        if (sub == 5) f.bobs = 0;
        return;
    default: notPortedAt(kMandaGuardianPort, "0x486eb0 variant", a.variant);
    }
}
// exe 0x486d00
void castGuardianUpdate(const PlayerEnv& env, AnimatedActor& a) {
    GuardianFields& f = classState<GuardianFields>(a);
    beginTick(a);
    switch (CastState(a.state)) {
    case CastState::Init: castInit(a); break;
    case CastState::Act: // exe 0x486eb0
        castStep(a, f);
        leanOrUnlean(a, f);
        bob(a, f);
        collide(env, a, f);
        turnHead(a, f);
        break;
    default: notPortedAt(kMandaGuardianPort, "0x486d00 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    animateWithHead(a, f);
    // exe 0x5f20f0(actor, 0x60, 0x100, 0) while drawn: the shadow (render output)
}

// ---- the guardian of script 10's cutscene (exe 0x487250: pool 0x20 type 0x24 sub 2) ----------------------------------
// States [0x892210]: its set-up (exe 0x487320: animation 7, its first variant 0x1c), then the sub-states [0x892218]:
// it starts bobbing; in the step sub 1 the animations 0x12 at tick 0xf, 0x13, 0x1c; in the step sub 2 it fades out from
// tick 0xeb. Then the fight's bob / collision / head.
enum class OutroSub : uint8_t {
    Start = 0,   // exe 0x487420
    Speech = 1,  // exe 0x487440
    FadeOut = 2, // exe 0x4874d0
};
// exe 0x487320
void outroInit(AnimatedActor& a) {
    if (!setUpGuardian(a, outroBox, 7)) return;
    if (a.variant == 0) a.anim = 0x1c;
    showFlash(a, false);
    a.enterState(uint8_t(CastState::Act));
}
// exe 0x4873f0
void outroStep(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) {
    const ScriptState& s = scriptState();
    const uint8_t sub = s.stepSub;
    const int32_t t = s.stepTick;
    switch (OutroSub(a.subState)) {
    case OutroSub::Start:
        f.bobs = 1;
        toSub(a, uint8_t(OutroSub::Speech));
        break;
    case OutroSub::Speech:
        if (sub != 1) break;
        switch (a.step) {
        case 0:
            f.bobs = 1;
            a.enterStep(1);
            [[fallthrough]];
        case 1:
            if (t == 0xf) {
                a.anim = 0x12;
                a.enterStep(2);
            }
            break;
        case 2:
            if (a.animEnded()) {
                a.anim = 0x13;
                a.enterStep(3);
            }
            break;
        case 3:
            if (a.animEnded()) {
                a.anim = 0x1c;
                toSub(a, uint8_t(OutroSub::FadeOut));
            }
            break;
        default: break;
        }
        break;
    case OutroSub::FadeOut:
        if (sub != 2) break;
        if (a.step == 0) {
            f.bobs = 1;
            if (t != 0xeb) break;
            fadeStartSilent(a, f); // exe 0x4881f0
            f.count = 0;
            a.step = 1;
            a.stepCounter = 0;
        } else if (a.step != 1) {
            break;
        }
        dustStepShifted(a, f);
        if (fadeOut(a, f, outroFade)) {
            a.step = 2;
            a.stepCounter = 0;
        }
        break;
    default: notPortedAt(kMandaGuardianPort, "0x4873f0 sub-state", a.subState);
    }
    bob(a, f);
    collide(env, a, f);
    turnHead(a, f);
}
// exe 0x487250
void outroGuardianUpdate(const PlayerEnv& env, AnimatedActor& a) {
    GuardianFields& f = classState<GuardianFields>(a);
    beginTick(a);
    switch (CastState(a.state)) {
    case CastState::Init: outroInit(a); break;
    case CastState::Act: outroStep(env, a, f); break;
    default: notPortedAt(kMandaGuardianPort, "0x487250 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    animateWithHead(a, f);
    // exe 0x5f20f0(actor, 0x60, 0x100, 0) while drawn: the shadow (render output)
}

// +0x161
constexpr uint8_t kHopperHeld = 0x01;     // held (no hit entry; a hit while thrown bounces it back)
constexpr uint8_t kHopperNoDamage = 0x02;
constexpr uint8_t kHopperFlag04 = 0x04;   // set with its hops (no reader in its class)
constexpr uint8_t kHopperNoFall = 0x08;   // the ground lost does not make it fall
constexpr uint8_t kHopperThrown = 0x10;   // its hit entry the thrown one (it hurts enemies)
constexpr uint8_t kHopperLunging = 0x20;  // its hit entry its strong attack
constexpr uint8_t kHopperBounces = 0x40;  // the walls turn it, it stays airborne on the floor
constexpr uint8_t kHopperNoLockOn = 0x80; // never the player's lock-on target (its last ticks)
constexpr uint8_t kHopperHops = kHopperBounces | kHopperNoFall | kHopperFlag04;
constexpr uint8_t kHopperJumps = kHopperHops | kHopperLunging;
constexpr uint8_t kHopperRolls = kHopperBounces | kHopperLunging | kHopperFlag04;
constexpr uint8_t kHopperFlies = kHopperHops | kHopperNoDamage;
// +0x160
constexpr uint8_t kHopperWallZ = 0x01, kHopperWallX = 0x02, kHopperBounced = 0x04, kHopperGroundLost = 0x08;
// +0x167
constexpr uint8_t kHopperGathered = 0x01, kHopperToCorner = 0x04;
enum class HopperState : uint8_t { Init = 0, Act = 1, Carried = 2, Dying = 3 }; // exe 0x49e370 / 0x49e460 / 0x49f040
                                                                               // / 0x49f2e0
enum class HopperSub : uint8_t {
    Reset = 0,      // exe 0x49e4f0
    Idle = 1,       // exe 0x49e530
    Chase = 2,      // exe 0x49e610
    HighJump = 3,   // exe 0x49e850
    Orbit = 4,      // exe 0x49e980
    ToCorner = 5,   // exe 0x49eab0
    Fall = 6,       // exe 0x49ec80
    KnockedOut = 7, // exe 0x49ece0
    GetUp = 8,      // exe 0x49ed40
    Knocked = 9,    // exe 0x49ee30
    KnockedAir = 10, // exe 0x49ef20
};
enum class CarriedSub : uint8_t { Held = 0, Thrown = 1 }; // [0x893a60]: exe 0x49f070 / 0x49f170
const DataTable<CollisionBoxRecord> hopperBox{TableId::MandaGuardianHopperBox};
const DataTable<InteractionBoxRecord> hopperLiftBox{TableId::MandaGuardianHopperLiftBox};
constexpr uint16_t kSoundChirp = 0x11b, kSoundHop = 0x119, kSoundBigHop = 0x11a, kSoundRoll = 0x132;
constexpr uint8_t kGuardianClass = kAnimatedClass;

void toSub(AnimatedActor& a, HopperSub s) { toSub(a, uint8_t(s)); }
// the guardian it follows (its pool index from the spawn parameter)
AnimatedActor& guardianOf(const HopperFields& f) {
    const int i = f.guardian;
    if (i < 0 || i >= int(AnimatedPool::size()))
        notPortedAt(kMandaGuardianPort, "0x49e160: guardian index outside the pool", uint32_t(i));
    return animatedPool()[size_t(i)];
}
// the player in the air: airborne in a jump (standing, running) or a skate fall
bool playerInTheAir() {
    const Player& p = player();
    const PlayerSub s = p.sub();
    return p.airborne != 0 && (s == PlayerSub::Jump || s == PlayerSub::RunningJump || s == PlayerSub::SkateFall);
}
// exe 0x49f280: landed (bounced): the speeds halved back, true; else the move along the yaw and the gravity
bool hopAir(AnimatedActor& a, const HopperFields& f, int yaw) {
    if (f.hits & kHopperBounced) {
        a.velocity.z = int16_t(int32_t(a.velocity.z) >> 1);
        a.velocity.y = int16_t(-int32_t(a.velocity.y) >> 1);
        a.rotation.x = 0;
        a.airborne = 1;
        return true;
    }
    moveAlongYaw(a.pos, yaw, 0, a.velocity.y, a.velocity.z); // exe 0x5e36f0
    a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
    return false;
}
// the wall results of a push-out / a territory push
void wallHits(HopperFields& f, uint32_t r) {
    if (r & (kSolidOnZPlus | kSolidOnZMinus)) f.hits |= kHopperWallZ;
    if (r & (kSolidOnXPlus | kSolidOnXMinus)) f.hits |= kHopperWallX;
}
// exe 0x49f530: the walls (bouncing it while kHopperBounces), the territory, the ground; true: the ground lost (more
// than 0x10 below)
bool hopCollide(const PlayerEnv& env, AnimatedActor& a, HopperFields& f) {
    f.hits = 0;
    uint32_t r = 0;
    if (int32_t(actorTestMove(env, a)) < 0) { // exe 0x40f570
        r = actorPushOut(env, a);             // exe 0x410960
        if (int32_t(r) < 0) a.pos = a.prevPos;
        wallHits(f, r);
        if ((f.flags & kHopperBounces) && reflectRotationOffWalls(a.rotation, r) != 0) f.hits |= kHopperBounced;
    } else {
        const int rect = territoryViolated(a.pos); // exe 0x5e6ab0
        if (rect != 0) {
            r = territoryPushBack(a, rect); // exe 0x5e6b40
            wallHits(f, r);
        }
    }
    const int g = actorGround(env, a); // exe 0x40f190
    if (a.airborne != 0) {
        if (!(f.flags & kHopperBounces) && (r & kSolidAboveOrBelow)) {
            a.airborne = 0;
            a.pos.y.whole = int16_t(g);
            f.hits |= kHopperBounced;
        }
        return false;
    }
    if (g - a.pos.y.whole > 0x10) {
        f.hits |= kHopperGroundLost;
        return true;
    }
    a.pos.y.whole = int16_t(g);
    return false;
}
// the knock-back of a hit: in the air, on the ground, or (knocked out) dying
void knockBack(AnimatedActor& a, const HopperFields& f, bool dieWhenKnockedOut) {
    if (a.airborne != 0) toSub(a, HopperSub::KnockedAir);
    else if (f.knockedOut == 0) toSub(a, HopperSub::Knocked);
    else if (dieWhenKnockedOut) a.enterState(uint8_t(HopperState::Dying));
}
// exe 0x49f420: the damage of its box (a player's / thrown / strong hit: the flash, the regeneration of half of it);
// dead -> knocked back (or dying); gathered under a quarter of its hit points -> knocked back
void hopDamage(AnimatedActor& a, HopperFields& f) {
    const uint32_t v = f.hitBox.result, d = v & kHitDamageMask;
    if (d == 0) return;
    uint32_t hit = 0;
    if (v & (kHitAttackByPlayer | kHitAttackThrown | kHitAttackStrong)) {
        f.heading = f.hitBox.contact.x;
        a.tint = kTintFlash;
        const uint16_t h = uint16_t(int32_t(d) >> 1);
        f.regenTicks = 8;
        f.regen = int16_t(h);
        hit = d;
        if (h > 0xff) f.regen = 0xff;
    }
    if (hitPointsAdd(a, int(wrap16(-int32_t(hit)))) != 0) {
        f.regen = 0;
        f.heading = f.hitBox.contact.x;
        knockBack(a, f, true);
        return;
    }
    if ((f.mode & kHopperGathered) && a.hitPoints < int16_t(a.maxHitPoints >> 2)) {
        f.heading = f.hitBox.contact.x;
        knockBack(a, f, false);
    }
}
// exe 0x49e510: still, the animation 0, the model's scale reset
void hopReset(AnimatedActor& a) {
    a.anim = 0;
    a.velocity.setXY(0, 0);
    a.velocity.z = 0;
    a.acceleration.z = 0;
    resetModelScale(a); // exe 0x4e5cd0
}
// exe 0x5e3df0: turned toward the player, minus r
void hopToward(AnimatedActor& a, const HopperFields& f, uint32_t r, uint32_t extra) {
    turnYawTo(a.rotation.y, uint32_t(angleOf(player().pos, a.pos)) - r, int(f.turnRate) + int(extra));
}
void chirp(const AnimatedActor& a) {
    if ((rngStep() & 0x1f) == 0) soundPlayAtPos(kSoundChirp, a.pos);
}
// exe 0x49e530: idle; the player close (ahead 0xc00, else 0x600) -> the chase; the recall -> its corner
void hopIdle(AnimatedActor& a, HopperFields& f) {
    if (a.step == 0) {
        f.flags = 0;
        a.anim = 0;
        a.enterStep(1);
    }
    chirp(a);
    const int ang = angleOf(player().pos, a.pos);
    const int d = angleDiff(uint32_t(ang), uint32_t(uint16_t(a.rotation.y)));
    const int dist = planarDistance(player().pos, a.pos);
    if (uint32_t(d + 0x400) < 0x800 ? dist < 0xc00 : dist < 0x600) toSub(a, HopperSub::Chase);
    if ((f.marks & kMarkRecall) && a.airborne == 0) toSub(a, HopperSub::ToCorner);
}
// exe 0x49e610: hops at the player (short ones, then a long one)
void hopChase(AnimatedActor& a, HopperFields& f) {
    const uint8_t c = a.step;
    switch (c) {
    case 0:
        f.flags = kHopperHops;
        a.velocity.z = 0;
        a.velocity.y = 0;
        a.acceleration.y = 0;
        f.hopsLeft = 5;
        a.anim = 0;
        a.enterStep(1);
        break;
    case 1:
        chirp(a);
        if (a.animEnded()) ++a.step;
        break;
    case 2:
        a.anim = 1;
        a.step = uint8_t(c + 1u);
        if ((rngStep() & 1) == 0) {
            if (f.hopsLeft == 0) {
                f.hopsLeft = 5;
            } else {
                a.anim = 0;
                a.step = 1;
                --f.hopsLeft;
            }
        }
        break;
    case 3:
        if (!a.animEnded()) break;
        a.step = uint8_t(c + 1u);
        [[fallthrough]];
    case 4: {
        soundPlayAtPos(kSoundHop, a.pos);
        const uint32_t r = rngStep() & 0x7f;
        hopToward(a, f, r, 0x200);
        a.velocity.z = -0x180;
        a.velocity.y = int16_t(-0x140 - int32_t(r));
        a.acceleration.y = 0x40;
        a.airborne = 1;
        ++a.step;
        a.anim = 2;
        break;
    }
    case 5:
        if (hopAir(a, f, a.rotation.y)) {
            a.airborne = 0;
            a.velocity.z = 0;
            a.velocity.y = 0;
            ++a.step;
            a.anim = 3;
        }
        break;
    case 6:
        if (a.animEnded()) {
            a.anim = 0;
            a.step = 2;
        }
        break;
    default: break;
    }
    if (playerInTheAir() && planarDistance(player().pos, a.pos) < 0x400 && a.airborne == 0)
        toSub(a, HopperSub::HighJump);
    if (((f.mode & kHopperToCorner) || (f.marks & kMarkSpin)) && a.airborne == 0) toSub(a, HopperSub::ToCorner);
}
// exe 0x49e850: a high jump (the player in the air close by)
void hopHighJump(AnimatedActor& a, HopperFields& f) {
    switch (a.step) {
    case 0:
        f.flags = kHopperJumps;
        a.velocity.z = 0;
        a.velocity.y = 0;
        a.acceleration.y = 0;
        a.anim = 4;
        ++a.step;
        return;
    case 1:
        if (!a.animEnded()) return;
        ++a.step;
        [[fallthrough]];
    case 2: {
        soundPlayAtPos(kSoundBigHop, a.pos);
        const uint32_t r = rngStep() & 0x3f;
        hopToward(a, f, r, 0x200);
        a.velocity.z = -0x18;
        a.velocity.y = -0x4b0;
        a.acceleration.y = 0x40;
        a.airborne = 1;
        ++a.step;
        a.anim = 5;
        return;
    }
    case 3:
        if (hopAir(a, f, a.rotation.y)) {
            a.anim = 6;
            a.airborne = 0;
            a.velocity.z = 0;
            a.velocity.y = 0;
            ++a.step;
        }
        return;
    case 4:
        if (a.animEnded()) toSub(a, HopperSub::Chase);
        return;
    default: return;
    }
}
// exe 0x49e980: the orbit (rolling, facing the player -- or the guardian while the player is in the air), its roll
// wobbling; the gathering over -> the chase
void hopOrbit(AnimatedActor& a, HopperFields& f) {
    if (a.step == 0) {
        a.enterStep(1);
        f.flags = kHopperRolls;
        a.velocity.z = -0x280;
        a.velocity.y = 0;
        a.acceleration.y = 0;
        f.rollSide = 0;
        a.anim = 5;
        ++a.step;
    }
    if ((gameState().frame & 7) == 0) soundPlayAtPos(kSoundRoll, a.pos);
    uint32_t ang, step;
    if (!playerInTheAir()) {
        ang = uint32_t(angleOf(player().pos, a.pos));
        step = (uint32_t(f.turnRate) >> 3) + 0x20;
    } else {
        ang = uint32_t(angleOf(guardianOf(f).pos, a.pos));
        step = uint32_t(f.turnRate) >> 3;
    }
    turnYawTo(a.rotation.y, ang, int(step));
    integrateVelocity(a, 0, 0, a.velocity.z); // exe 0x5e3690
    if (f.rollSide == 0) {
        a.rotation.z = int16_t(uint16_t(a.rotation.z) + 0x40u);
        if (a.rotation.z > 0x80) {
            f.rollSide = 1;
            a.rotation.z = 0x80;
        }
    } else {
        a.rotation.z = int16_t(uint16_t(a.rotation.z) - 0x40u);
        if (a.rotation.z < -0x80) {
            f.rollSide = 0;
            a.rotation.z = -0x80;
        }
    }
    if (!(f.mode & kHopperGathered)) {
        a.rotation.z = 0;
        toSub(a, HopperSub::Chase);
    }
}
// exe 0x49eab0: the jump to its corner of the ring round the guardian, then the orbit once the gathering starts
void hopToCorner(AnimatedActor& a, HopperFields& f) {
    const uint8_t c = a.step;
    switch (c) {
    case 0:
        f.flags = kHopperFlies;
        a.velocity.z = 0;
        a.velocity.y = 0;
        a.acceleration.y = 0;
        a.anim = 1;
        a.enterStep(1);
        return;
    case 1:
        if (!a.animEnded()) return;
        a.step = uint8_t(c + 1u);
        [[fallthrough]];
    case 2: {
        soundPlayAtPos(kSoundBigHop, a.pos);
        const AnimatedActor& g = guardianOf(f);
        int32_t corner[3] = {g.pos.x, g.pos.y, g.pos.z};
        const int32_t k = int32_t(f.corner) * 0x10;
        const auto addWhole = [](int32_t& v, int16_t d) {
            v = int32_t((uint32_t(v) & 0xffff) | uint32_t(uint16_t(uint16_t(uint32_t(v) >> 16) + uint16_t(d))) << 16);
        };
        addWhole(corner[0], int16_t(sineAt(k) >> 5));
        addWhole(corner[2], int16_t(cosineAt(k) >> 5));
        f.heading = int16_t(angleOf(corner[0], corner[2], a.pos.x, a.pos.z));
        const int dist = planarDistance(a.pos, corner);
        a.velocity.z = -0x180;
        a.velocity.y = int16_t(-(((dist / 0x18) * 0x20) >> 1));
        a.acceleration.y = 0x20;
        ++a.step;
        a.airborne = 1;
        a.anim = 2;
        return;
    }
    case 3:
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + 0x200u);
        if (hopAir(a, f, f.heading)) {
            a.airborne = 0;
            ++a.step;
            a.velocity.z = 0;
            a.velocity.y = 0;
            a.anim = 3;
            a.rotation.y = int16_t((int32_t(f.corner) + 2) * 0x400);
        }
        return;
    case 4:
        if (a.animEnded()) {
            a.anim = 0xe;
            a.step = uint8_t(c + 1u);
        }
        return;
    case 5:
        if (!(f.mode & kHopperToCorner) && !(f.marks & kMarkSpin)) toSub(a, HopperSub::Orbit);
        return;
    default: return;
    }
}
// exe 0x49ec80: falling (the ground lost) until it lands -> idle
void hopFall(AnimatedActor& a, HopperFields& f) {
    if (a.step == 0) {
        f.flags = kHopperHops;
        a.step = 1;
        a.anim = 0;
        a.acceleration.y = 0x40;
        resetModelScale(a);
    } else if (a.step != 1) {
        return;
    }
    if (hopAir(a, f, a.rotation.y)) {
        a.airborne = 0;
        a.velocity.y = 0;
        toSub(a, HopperSub::Idle);
    }
}
// exe 0x49ece0: knocked out up to 900 ticks (while gathered) -> up again
void hopKnockedOut(AnimatedActor& a, HopperFields& f) {
    if (a.step == 0) {
        f.flags = 0;
        f.knockedOut = 1;
        a.anim = 8;
        a.enterStep(1);
        f.timer = 900;
    }
    --f.timer;
    if (!(f.mode & kHopperGathered) || f.timer == 0) {
        toSub(a, HopperSub::GetUp);
        f.knockedOut = 0;
    }
}
// exe 0x49ed40: back up (a hop) -> the chase, or the orbit while gathered
void hopGetUp(AnimatedActor& a, HopperFields& f) {
    switch (a.step) {
    case 0:
        f.flags = kHopperFlies;
        a.anim = 0xb;
        a.enterStep(1);
        a.velocity.z = 0;
        a.velocity.y = -0x200;
        a.acceleration.y = 0x40;
        a.rotation.x = 0;
        a.airborne = 1;
        return;
    case 1:
        if (a.animEnded()) {
            a.anim = 0xc;
            ++a.step;
            soundPlayAtPos(kSoundHop, a.pos);
        }
        [[fallthrough]];
    case 2:
        if (hopAir(a, f, a.rotation.y)) {
            a.anim = 0xd;
            a.airborne = 0;
            a.velocity.y = 0;
            a.velocity.z = 0;
            ++a.step;
        }
        return;
    case 3:
        if (a.animEnded()) toSub(a, (f.mode & kHopperGathered) ? HopperSub::Orbit : HopperSub::Chase);
        return;
    default: return;
    }
}
// landed from a knock-back: dead -> dying, else knocked out
void knockLanded(AnimatedActor& a) {
    if (a.hitPoints < 0) a.enterState(uint8_t(HopperState::Dying));
    else toSub(a, HopperSub::KnockedOut);
}
// exe 0x49ee30 (on the ground) / 0x49ef20 (in the air): knocked back by a hit along the hit's yaw
void hopKnocked(AnimatedActor& a, HopperFields& f, bool air) {
    const uint8_t c = a.step;
    if (c == 0) {
        a.enterStep(1);
        f.flags = kHopperFlies;
        a.anim = air ? 9 : 7;
        a.velocity.z = 0x100;
        const uint32_t r = rngStep();
        a.acceleration.y = 0x40;
        a.velocity.y = int16_t((air ? -0x180 : -0x100) - int32_t(r & 0x3f));
        a.rotation.y = int16_t(uint16_t(f.heading) + 0x800u);
        a.rotation.x = 0;
        a.rotation.z = 0;
        a.airborne = 1;
        soundPlayAtPos(kSoundHurt, a.pos);
        resetModelScale(a);
        if (!air) return;
    } else if (!air) {
        if (c != 1) return;
        hopAir(a, f, a.rotation.y);
        if (!a.animEnded()) return;
        a.airborne = 0;
        a.velocity.y = 0;
        a.velocity.z = 0;
        a.anim = 8;
        knockLanded(a);
        return;
    } else if (c == 1) {
        if (a.animEnded()) {
            a.anim = 10;
            a.step = 2;
        }
    } else if (c == 2 && a.animEnded()) {
        a.anim = 8;
        a.step = 3;
    }
    if (!hopAir(a, f, a.rotation.y)) return;
    if (a.stepCounter == 0) {
        a.stepCounter = 1;
        return;
    }
    a.airborne = 0;
    a.velocity.y = 0;
    a.velocity.z = 0;
    knockLanded(a);
}
// exe 0x49e370
void hopperInit(AnimatedActor& a, HopperFields& f) {
    a.flags |= kActorDrawn;
    if (!bindActorModel(a, 0)) {
        freeActor(a); // exe 0x5e1490
        return;
    }
    a.rotation.x = 0;
    a.rotation.z = 0;
    a.anim = 0;
    a.prevAnim = kNoAnimShown;
    a.runFlags |= kActorRunMarked;
    const uint16_t hp = hasAnimatedStats(a.type) ? uint16_t(animatedStats(a.type).hitPoints) : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.collisionBox.pointAt(hopperBox.stateAddress());
    f.guardian = int8_t(uint8_t(f.guardian) - kAnimatedClass);
    a.interactionBox.pointAt(hopperLiftBox.stateAddress());
    a.interaction = kInteractLift;
    a.airborne = 0;
    a.onPlatform = 0;
    const int div = int(f.corner) + 1;
    if (div == 0) notPorted(kMandaGuardianPort, "0x49e370: 0xff / 0 (the corner -1)");
    f.turnRate = uint8_t(0xff / div);
    a.clearClassBlock(); // exe 0x40dc40(+0x14c, +0x168)
    f.hitBox.previousX = a.pos.x.whole;
    f.hitBox.previousZ = a.pos.z.whole;
    f.hitBox.result = 0;
    const int idx = enemyGroupFind(kGuardianClass, kGuardianType, 0); // exe 0x5e6d00
    f.mode = 0;
    f.hopsLeft = 0;
    f.regen = 0;
    f.group = uint8_t(idx);
    f.glow = 0x20;
    a.enterState(uint8_t(HopperState::Act));
}
// the glow's tint
void glowTint(AnimatedActor& a, const HopperFields& f) { a.tint = uint16_t((uint32_t(f.glow) >> 1) | 0x4200u); }
// exe 0x49e460
void hopperAct(const PlayerEnv& env, AnimatedActor& a, HopperFields& f) {
    glowTint(a, f);
    if (!(f.flags & kHopperNoDamage)) hopDamage(a, f);
    switch (HopperSub(a.subState)) { // [0x893a34]
    case HopperSub::Reset:
        hopReset(a);
        f.flags = 0;
        toSub(a, HopperSub::Idle);
        break;
    case HopperSub::Idle: hopIdle(a, f); break;
    case HopperSub::Chase: hopChase(a, f); break;
    case HopperSub::HighJump: hopHighJump(a, f); break;
    case HopperSub::Orbit: hopOrbit(a, f); break;
    case HopperSub::ToCorner: hopToCorner(a, f); break;
    case HopperSub::Fall: hopFall(a, f); break;
    case HopperSub::KnockedOut: hopKnockedOut(a, f); break;
    case HopperSub::GetUp: hopGetUp(a, f); break;
    case HopperSub::Knocked: hopKnocked(a, f, false); break;
    case HopperSub::KnockedAir: hopKnocked(a, f, true); break;
    default: notPortedAt(kMandaGuardianPort, "0x49e460 sub-state", a.subState);
    }
    if (hopCollide(env, a, f)) {
        a.airborne = 1;
        if (!(f.flags & kHopperNoFall)) toSub(a, HopperSub::Fall);
    }
    if (a.hitPoints >= 0 && playerLiftTargetCheck(a, 0)) { // exe 0x573a60
        a.enterState(uint8_t(HopperState::Carried));
        f.flags |= kHopperHeld;
    }
}
// the death explosion (the global static type 8) at the hopper
void hopperExplosion(const AnimatedActor& a, const HopperFields& f) {
    const uint32_t address = allocActor60(0); // exe 0x5e1300
    if (!address) return;
    StaticActor& e = staticActorAt(address);
    ExplosionParams params(e);
    e.flags = kActorLive;
    e.type = typeByte(GlobalStaticType::Explosion);
    e.variant = 0;
    e.loopFlags |= kActorSweepGroup2;
    e.pos.x.whole = a.pos.x.whole;
    e.pos.y.whole = int16_t(uint16_t(a.pos.y.whole) - 0x60u);
    e.pos.z.whole = a.pos.z.whole;
    params.spread = 0x30;
    params.flashes = uint16_t(f.timer);
}
// exe 0x49f2e0: the flash, the explosion, it falls, freed
void hopperDying(const PlayerEnv& env, AnimatedActor& a, HopperFields& f) {
    a.tint = kTintFlash;
    if (a.step == 0) {
        a.step = 1;
        f.timer = 0x28;
        if (a.airborne == 0) {
            a.velocity.x = 0;
            a.velocity.y = 0;
            a.velocity.z = 0;
        } else {
            f.timer = 0x14;
        }
        a.acceleration.y = 0x40;
        a.anim = 8;
        countDefeat(); // exe 0x5e53e0
        hopperExplosion(a, f);
    }
    if (f.timer & 1) glowTint(a, f);
    if (f.timer < 0x19) f.flags |= kHopperNoLockOn;
    --f.timer;
    if (f.timer == 0) {
        freeActor(a); // exe 0x5e1490
        return;
    }
    integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z);
    if (a.airborne != 0) a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
    hopCollide(env, a, f);
    if (a.wasAirborne != 0 && a.airborne == 0) {
        a.velocity.z = 0;
        a.velocity.x = 0;
    }
}
// ---- state 2 (exe 0x49f040): carried by the player ----
void hopperThrown(const PlayerEnv& env, AnimatedActor& a, HopperFields& f);
// exe 0x49f070: at the player's hands (0x18 up) until released (thrown / dropped)
void hopperHeld(const PlayerEnv& env, AnimatedActor& a, HopperFields& f) {
    if (a.step == 0) {
        f.flags = kHopperBounces | kHopperHeld;
        a.step = 1;
        a.anim = 8;
        a.rotation.z = 0;
        resetModelScale(a); // exe 0x4e5cd0
    }
    carryAtHands(a, 0, -0x18, 0);           // exe 0x573ab0
    const CarryRelease r = carryReleased(); // exe 0x573950
    if (r == CarryRelease::Held) return;
    f.flags &= uint8_t(~kHopperHeld);
    f.heading = player().rotation.y;
    if (r != CarryRelease::Thrown) {
        a.velocity.y = 0x300;
        a.velocity.z = -0x40;
    } else {
        int32_t v[3];
        playerAimVector(0, -0x100, -0x380, v); // exe 0x5e3e50
        a.velocity.x = 0;
        a.velocity.y = int16_t(v[1]);
        a.velocity.z = int16_t(v[2]);
        f.flags |= kHopperThrown;
    }
    a.acceleration.y = 0x40;
    a.rotation.y = f.heading;
    a.airborne = 1;
    carryDropFromPlayer(a); // exe 0x573990
    toSub(a, uint8_t(CarriedSub::Thrown));
    hopperThrown(env, a, f);
}
// exe 0x49f170: flying (spinning after the first bounce); a solid hit while alive bounces it back (its hit points
// -1); its third landing -> knocked out (or dying)
void hopperThrown(const PlayerEnv& env, AnimatedActor& a, HopperFields& f) {
    if (a.step == 0) {
        f.heading = a.rotation.y;
        ++a.step;
    }
    if ((f.hitBox.result & (kHitAttackDeflects | kHitAttackSolid)) && a.hitPoints >= 0) {
        f.regen = 0;
        a.hitPoints = -1;
        a.velocity.z = int16_t(-int32_t(a.velocity.z) >> 1);
        spawnHitSpark(kActorSweepGroup2, a.pos.x.whole, int16_t(uint16_t(a.pos.y.whole - 0x40)), a.pos.z.whole);
        f.flags |= kHopperHeld;
    }
    if (a.stepCounter != 0) a.rotation.y = int16_t(uint16_t(a.rotation.y) + 0x200u);
    if (hopAir(a, f, f.heading)) { // exe 0x49f280
        if (a.stepCounter >= 2) {
            if (a.hitPoints >= 0) a.enterState(uint8_t(HopperState::Act), uint8_t(HopperSub::KnockedOut));
            else a.enterState(uint8_t(HopperState::Dying));
            a.velocity.y = 0;
            a.airborne = 0;
        } else {
            ++a.stepCounter;
            integrateVelocity(a, 0, a.velocity.y, a.velocity.z); // exe 0x5e3690
            f.heading = int16_t(uint16_t(f.heading) + 0x800u);
        }
    }
    hopCollide(env, a, f); // exe 0x49f530
    if (f.hits & (kHopperWallZ | kHopperWallX)) f.heading = int16_t(uint16_t(f.heading) + 0x800u);
}
// exe 0x49f040
void hopperCarried(const PlayerEnv& env, AnimatedActor& a, HopperFields& f) {
    glowTint(a, f);
    switch (CarriedSub(a.subState)) {
    case CarriedSub::Held: hopperHeld(env, a, f); break;
    case CarriedSub::Thrown: hopperThrown(env, a, f); break;
    default: notPortedAt(kMandaGuardianPort, "0x49f040 sub-state (table 0x893a60 has 2)", a.subState);
    }
}
// the guardian's marks taken in (while alive): the recall, the spin (healed, gathered), the release
void followGuardian(AnimatedActor& a, HopperFields& f) {
    if (a.hitPoints >= 0) {
        const uint8_t b = f.marks;
        if (b != 0) {
            if (b & kMarkRecall) f.mode = kHopperToCorner;
            if (b & kMarkSpin) {
                a.hitPoints = a.maxHitPoints;
                f.mode = kHopperGathered;
            }
            if (f.marks & kMarkRelease) f.mode = 0;
        }
    }
    f.marks = uint8_t(enemyGroupMarks(int(f.group), EnemyGroupMarks::Either) & 0xff);
    if (!(f.mode & kHopperGathered)) {
        const uint8_t v = uint8_t(f.glow - 1u);
        f.glow = v < 0x20 ? uint8_t(0x20) : v;
    } else {
        const uint8_t v = uint8_t(f.glow + 1u);
        f.glow = v > 0x3e ? uint8_t(0x3e) : v;
    }
    if (f.regen != 0) {
        if (f.regenTicks == 0) {
            f.regenTicks = 8;
            a.hitPoints = int16_t(uint16_t(a.hitPoints) + 1u);
            --f.regen;
        } else {
            --f.regenTicks;
        }
    }
}
// its hit entry at bone 0: dying a bump; carried and thrown the thrown attack; else its lunge's strong attack or a solid
// body (no lock-on while gathered, unless the guardian spins)
void hopperHits(AnimatedActor& a, HopperFields& f) {
    const uint8_t b = f.flags;
    const AnimatedActorStats stats = animatedStats(a.type);
    int16_t root[3];
    bonePosition(a, 0, root);
    f.hitBox.result = 0;
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    if (b & kHopperHeld) return;
    const uint32_t lockOn = (!(f.mode & kHopperGathered) || (f.marks & kMarkSpin))
                                ? ((b & kHopperNoLockOn) ? kHitNoLockOn : 0u)
                                : kHitNoLockOn;
    uint32_t owner, attack;
    if (HopperState(a.state) == HopperState::Dying) {
        attack = kHitAttackBump;
        owner = hitOwner(a.slot) | lockOn | hitGroup(kHitGroupBodies) | hitRadius(0x58);
    } else if (!(b & kHopperThrown)) {
        attack = (b & kHopperLunging) ? (kHitAttackStrong | kHitAttackThrown | stats.strongAttack)
                                      : (kHitAttackSolid | stats.attack);
        owner = hitOwner(a.slot) | lockOn | hitGroup(kHitGroupBodies) | kHitContactMotion | hitRadius(0x50);
    } else {
        attack = stats.bodyAttack | kHitAttackByPlayer;
        owner = hitOwner(a.slot) | hitGroup(kHitGroupPlayerAttacks) | kHitContactMotion | hitRadius(0x48);
    }
    hitListSubmit(root, f.hitBox, owner, attack); // exe 0x5e4120
}
// exe 0x49e160
void hopperUpdate(const PlayerEnv& env, AnimatedActor& a) {
    HopperFields& f = classState<HopperFields>(a);
    followGuardian(a, f);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (HopperState(a.state)) { // [0x893a24]
    case HopperState::Init: hopperInit(a, f); break;
    case HopperState::Act: hopperAct(env, a, f); break;
    case HopperState::Carried: hopperCarried(env, a, f); break;
    case HopperState::Dying: hopperDying(env, a, f); break;
    default: notPortedAt(kMandaGuardianPort, "0x49e160 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim == a.prevAnim) animStep(a); // exe 0x5e1e80
    else animSet(a, a.anim, 0);            // exe 0x5e1c30
    animPose(a);
    hopperHits(a, f);
    // exe 0x5f20f0(actor, 0x30, 0x160, 0) unless carried: the shadow (render output)
}

// ---- the guardian of scripts 0xb / 0xc (exe 0x4875e0: ST12 pool 0x20 type 0x24 sub 1) ---------------------------------
// States [0x892244]: its set-up (exe 0x4876b0: animation 0x1c), then [0x89224c + variant] and the bob / collision /
// head (no lean). Variant 0 (script 0xb's sequence, exe 0x4877a0, sub-states [0x892254]) fades in, then in the step sub
// 3 the animations 0x14 / 0x1c; variant 1 (script 0xc's record 0x89fe80, exe 0x487890, sub-states [0x892260]) keyed on
// the script's step sub / tick: the animations 0x12 / 0x15 / 0x16, in the step sub 1 0x17 / 0x1b / 0x1c, in the step
// sub 2 0x14 / 0x1c and the fade out at tick 0x28. The script frees it.
constexpr uint8_t kCast2Script0b = 0, kCast2Script0c = 1;
// exe 0x4876b0
void cast2Init(AnimatedActor& a) {
    if (!setUpGuardian(a, cast2Box, 0x1c)) return;
    showFlash(a, false);
    a.enterState(uint8_t(CastState::Act));
}
// [0x89224c + variant]
void cast2Step(AnimatedActor& a, GuardianFields& f) {
    const ScriptState& s = scriptState();
    const uint8_t sub = s.stepSub;
    const int32_t t = s.stepTick;
    switch (a.variant) {
    case kCast2Script0c: // exe 0x487890 -> [0x892260 + sub-state]
        switch (a.subState) {
        case 0: // exe 0x4878b0
            switch (a.step) {
            case 0:
                f.flags = kHiddenFlags | kNoHeadTurn;
                f.bobs = 1;
                a.step = 1;
                [[fallthrough]];
            case 1:
                if (t == 0xf) {
                    a.anim = 0x12;
                    ++a.step;
                }
                return;
            case 2:
                if (a.animEnded()) {
                    a.anim = 0x15;
                    ++a.step;
                }
                return;
            case 3:
                if (a.animEnded()) {
                    a.anim = 0x16;
                    toSub(a, uint8_t(1));
                }
                return;
            default: return;
            }
        case 1: // exe 0x487940
            if (sub != 1) return;
            if (a.step == 0) {
                a.anim = 0x17;
                a.step = 1;
            } else if (a.step == 1) {
                if (t == 0x1e) {
                    a.anim = 0x1b;
                    a.enterStep(2);
                }
            } else if (a.step == 2 && a.animEnded()) {
                a.anim = 0x1c;
                toSub(a, uint8_t(2));
            }
            return;
        case 2: // exe 0x4879b0
            if (sub != 2) return;
            switch (a.step) {
            case 0:
                if (t == 0) {
                    a.anim = 0x14;
                    a.step = 1;
                }
                return;
            case 1:
                if (a.animEnded()) {
                    a.anim = 0x1c;
                    a.step = 2;
                }
                return;
            case 2:
                if (t != 0x28) return;
                fadeStart(a, f); // exe 0x487a80
                f.count = 0;
                a.step = 3;
                [[fallthrough]];
            case 3:
                dustStepShifted(a, f);
                if (fadeOut(a, f, cast2Fade)) ++a.step; // exe 0x487ae0
                return;
            default: return;
            }
        default: notPortedAt(kMandaGuardianPort, "0x487890 sub-state", a.subState);
        }
    case kCast2Script0b: // exe 0x4877a0 -> [0x892254 + sub-state]
        switch (a.subState) {
        case 0: // exe 0x4877c0: the fade in
            if (a.step == 0) {
                f.count = 0;
                f.flags = kHiddenFlags | kNoHeadTurn;
                f.bobs = 1;
                fadeStart(a, f); // exe 0x487ac0 = 0x487a80 + the vertical speed 0
                a.velocity.y = 0;
                fadeIn(a, f, cast2Fade); // exe 0x487b60
                a.step = 1;
            } else if (a.step == 1) {
                dustStepShifted(a, f);
                if (fadeIn(a, f, cast2Fade)) toSub(a, uint8_t(1));
            }
            return;
        case 1: // exe 0x487840
            toSub(a, uint8_t(2));
            return;
        case 2: // exe 0x487850: in the step sub 3 the animation 0x14 at tick 0, then 0x1c
            if (sub != 3) return;
            if (t == 0) a.anim = 0x14;
            if (a.anim == 0x14 && a.animEnded()) a.anim = 0x1c;
            return;
        default: notPortedAt(kMandaGuardianPort, "0x4877a0 sub-state", a.subState);
        }
    default: notPortedAt(kMandaGuardianPort, "0x487770 variant", a.variant);
    }
}
// exe 0x4875e0
void cast2GuardianUpdate(const PlayerEnv& env, AnimatedActor& a) {
    GuardianFields& f = classState<GuardianFields>(a);
    beginTick(a);
    switch (CastState(a.state)) { // [0x892244]
    case CastState::Init: cast2Init(a); break;
    case CastState::Act: // exe 0x487770
        cast2Step(a, f);
        bob(a, f);
        collide(env, a, f);
        turnHead(a, f);
        break;
    default: notPortedAt(kMandaGuardianPort, "0x4875e0 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    animateWithHead(a, f);
    // exe 0x5f20f0(actor, 0x60, 0x100, 0) while drawn: the shadow (render output)
}

enum class BladeState : uint8_t { Init = 0, Run = 1, Idle = 2 }; // exe 0x59bf30 / 0x59c0f0 / 0x52ef00
enum class BladeSub : uint8_t {
    Wait = 0,      // exe 0x59c200
    Wobble = 1,    // exe 0x59c240
    AimNear = 2,   // exe 0x59c280
    SlideNear = 3, // exe 0x59c300
    AimFar = 4,    // exe 0x59c390
    SlideFar = 5,  // exe 0x59c300
    Return = 6,    // exe 0x59c410
};
enum class ShuttleSub : uint8_t { Start = 0, Travel = 1, Turn = 2 };
constexpr uint8_t kBladeSpinner = 0, kBladeInPlace = 1, kBladeShuttle = 2, kBladeScenery = 0x80;
// A corner of the arena (exe data 0x8ce790, 8 bytes each: x, -, z, yaw)
struct BladeCorner : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    int16_t x = 0; // +0x0
    int16_t z = 0; // +0x4
    int16_t yaw = 0; // +0x6
    template <class R> static BladeCorner load(const R& r) {
        BladeCorner v;
        v.x = r.template read<int16_t>(0x0);
        v.z = r.template read<int16_t>(0x4);
        v.yaw = r.template read<int16_t>(0x6);
        return v;
    }
};
// the corner k (the exe's arithmetic on the signed index)
BladeCorner bladeCorner(int8_t k) { return dataRecord<BladeCorner>(stateAddress(TableId::MandaGuardianBladeCorners) + uint32_t(int32_t(k) * 8), BladeCorner::kBytes); }
// the model variant of a blade variant (exe data 0x8ce7bc, indexed by the variant unbounded)
uint8_t bladeModel(uint8_t variant) { return dataScalar<uint8_t>(stateAddress(TableId::MandaGuardianBladeModels) + variant); }
const DataTable<CollisionBoxRecord> bladeBox{TableId::MandaGuardianBladeBox};
constexpr uint16_t kSoundSlide = 0x133, kSoundSlideStop = 0x134;
constexpr int16_t kBladeSpin = 0x80; // its yaw's turn a tick (backwards)
void spinBlade(StaticActor& s) { s.rotation.y = int16_t(uint16_t(s.rotation.y) - uint16_t(kBladeSpin)); }

// exe 0x59c160: its hit entry (the static row's variant-1 damage), then the shadow (render output)
void bladeHit(StaticActor& s, BladeFields& f) {
    const StaticActorStats stats = staticStats(s.type, 1);
    const int16_t p[3] = {s.pos.x.whole, wrap16(int(s.pos.y.whole) - 0x20), s.pos.z.whole};
    f.hitBox.result = 0;
    f.hitBox.previousX = s.prevPos.x.whole;
    f.hitBox.previousZ = s.prevPos.z.whole;
    hitListSubmit(p, f.hitBox, hitOwner(s.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0xc0),
                  stats.damage | kHitAttackHarmless | kHitAttackDeflects | kHitAttackThrown);
}
// exe 0x59c530: the slide (the step counter its phase: accelerating to the centre, coasting to the braking distance,
// braking); true once stopped at the target
bool bladeSlide(StaticActor& s, BladeFields& f) {
    int32_t target[3];
    switch (s.stepCounter) {
    case 0: {
        s.velocity.z = int16_t(uint16_t(s.velocity.z) - 0x80u);
        if (s.velocity.z <= 0x400) s.velocity.z = -0x400;
        int32_t centre[3];
        wholePoint(0, -0x200, centre);
        if (planarDistance(s.pos, centre) <= 0x100) {
            wholePoint(f.targetX, f.targetZ, target);
            f.travelYaw = int16_t(angleOf(target[0], target[2], s.pos.x, s.pos.z));
            soundPlayAtPos(kSoundSlide, s.pos);
            cameraShakeAt(0x1e00, 0x300, 0x200, 0x1000, s.pos);
            s.stepCounter = 1;
        }
        moveAlongYaw(s.pos, f.travelYaw, 0, 0, s.velocity.z);
        return false;
    }
    case 1: {
        wholePoint(f.targetX, f.targetZ, target);
        const int d = planarDistance(s.pos, target);
        int32_t p1 = s.velocity.z, p2 = 0x80; // exe 0x5e4040(v, 0x80): the braking distance (16.16)
        if (p1 < p2) {
            p1 = -p1;
            p2 = -p2;
        }
        const int32_t r =
            -((int32_t(uint32_t(p1) << 12) / int32_t(uint32_t(p2) << 12)) * (int32_t(uint32_t(p1) << 12) >> 1));
        if (r >= int32_t(uint32_t(d) << 16)) s.stepCounter = 2;
        moveAlongYaw(s.pos, f.travelYaw, 0, 0, s.velocity.z);
        return false;
    }
    case 2: {
        wholePoint(f.targetX, f.targetZ, target);
        const int d = planarDistance(s.pos, target);
        s.velocity.z = int16_t(uint16_t(s.velocity.z) + 0x80u);
        const int v = s.velocity.z;
        if (v < 0 && int32_t(uint32_t(d) << 4) > (v < 0 ? -v : v)) {
            moveAlongYaw(s.pos, f.travelYaw, 0, 0, v);
            return false;
        }
        soundPlayAtPos(kSoundSlideStop, s.pos);
        cameraShakeAt(0x1e00, 0x300, 0x200, 0x1000, s.pos);
        s.velocity.z = 0;
        return true;
    }
    default: return false;
    }
}
// exe 0x59c280 / 0x59c390: the target ahead of its corner, then the slide
void bladeAim(StaticActor& s, BladeFields& f, int z, BladeSub next) {
    const BladeCorner c = bladeCorner(f.corner);
    int32_t o[3];
    rotateByYaw(c.yaw, -0x1500, 0, z, o); // exe 0x5e38d0
    f.targetX = int16_t(uint32_t(o[0] >> 16) + uint16_t(c.x));
    f.targetZ = int16_t(uint32_t(o[2] >> 16) + uint16_t(c.z));
    s.subState = uint8_t(next);
    s.restartStep();
}
// the wobble: the roll flipped every tick
void wobble(StaticActor& s) { s.rotation.z = int16_t(-int32_t(s.rotation.z)); }
// exe 0x59c300: the slide to the target, 45 ticks of rest, the next sub-state
void bladeGo(StaticActor& s, BladeFields& f) {
    wobble(s);
    const uint8_t c = s.step;
    if (c == 0) {
        s.velocity.z = 0;
        f.travelYaw = int16_t(angleOf(kCentreX, kCentreZ, s.pos.x, s.pos.z));
        s.enterStep(1);
    } else if (c != 1) {
        if (c != 2) return;
        --f.timer;
        if (f.timer != 0) return;
        s.restartStep();
        ++s.subState;
        return;
    }
    if (bladeSlide(s, f)) {
        f.timer = 0x2d;
        s.enterStep(2);
    }
}
// exe 0x59c410: the slide to the next corner, the mark kMarkBladesBack, then the wait / the next round
void bladeReturn(StaticActor& s, BladeFields& f) {
    wobble(s);
    const uint8_t c = s.step;
    if (c == 0) {
        const uint8_t k = uint8_t((uint8_t(f.corner) + 1u) & 3);
        s.velocity.z = 0;
        f.corner = int8_t(k);
        const BladeCorner e = bladeCorner(int8_t(k));
        f.travelYaw = int16_t(angleOf(kCentreX, kCentreZ, s.pos.x, s.pos.z));
        f.targetX = e.x;
        f.targetZ = e.z;
        s.enterStep(1);
    } else if (c != 1) {
        if (c != 2) return;
        --f.timer;
        if (f.timer != 0) return;
        s.subState = uint8_t((enemyGroupMarks(f.group, EnemyGroupMarks::Either) & kMarkSpin) ? BladeSub::AimNear
                                                                                              : BladeSub::Wait);
        s.restartStep();
        return;
    }
    if (bladeSlide(s, f)) {
        const BladeCorner e = bladeCorner(f.corner);
        s.pos.x.whole = e.x;
        s.pos.z.whole = e.z;
        f.travelYaw = e.yaw;
        f.timer = 0x2d;
        s.enterStep(2);
        enemyGroupMark(f.group, kMarkBladesBack);
    }
}
// exe 0x59bf30
void bladeInit(const PlayerEnv& env, StaticActor& s, BladeFields& f) {
    if (!bindActorModel(s, bladeModel(s.variant))) {
        freeActor(s); // exe 0x5e1540
        return;
    }
    s.maxHitPoints = 1;
    s.hitPoints = 1;
    s.velocity.clearWords();
    s.rotation.z = 0;
    s.rotation.x = 0;
    s.airborne = 0;
    s.onPlatform = 0;
    s.collisionBox.pointAt(bladeBox.stateAddress());
    s.interactionBox = nullptr;
    s.interaction = 0;
    s.clearClassBlock(); // exe 0x40dc40(+0xbc, +0xc8)
    switch (s.variant) {
    case kBladeSpinner: {
        s.flags |= kActorDrawn;
        const BladeCorner c = bladeCorner(f.corner);
        s.pos.x.whole = c.x;
        s.pos.z.whole = c.z;
        f.travelYaw = c.yaw;
        [[fallthrough]];
    }
    case kBladeInPlace:
        f.group = int8_t(enemyGroupFind(kGuardianClass, kGuardianType, 0));
        s.pos.y.whole = int16_t(actorGround(env, s)); // exe 0x40f190
        break;
    case kBladeShuttle:
        f.travelYaw = s.rotation.y;
        s.velocity.z = -0x400;
        break;
    case kBladeScenery:
        s.scale.x = int16_t(uint16_t(s.scale.x) << 1);
        s.scale.y = int16_t(uint16_t(s.scale.y) << 1);
        s.scale.z = int16_t(uint16_t(s.scale.z) << 1);
        s.flags |= kActorDrawn;
        break;
    default: break;
    }
    s.enterState(uint8_t(BladeState::Run));
}
// exe 0x59c120: the spinner
void bladeSpinner(const PlayerEnv& env, StaticActor& s, BladeFields& f) {
    switch (BladeSub(s.subState)) { // [0x8ce7cc]
    case BladeSub::Wait:
        s.rotation.z = 0;
        if (enemyGroupMarks(f.group, EnemyGroupMarks::Either) & kMarkSpin) {
            s.subState = uint8_t(BladeSub::Wobble);
            s.restartStep();
            s.rotation.z = 0x20;
        }
        break;
    case BladeSub::Wobble:
        wobble(s);
        if (s.step == 0) {
            f.timer = 0x5a;
            s.enterStep(1);
        }
        --f.timer;
        if (f.timer == 0) {
            s.subState = uint8_t(BladeSub::AimNear);
            s.restartStep();
        }
        break;
    case BladeSub::AimNear:
        bladeAim(s, f, -0x2000, BladeSub::SlideNear);
        bladeGo(s, f);
        break;
    case BladeSub::SlideNear:
    case BladeSub::SlideFar: bladeGo(s, f); break;
    case BladeSub::AimFar:
        bladeAim(s, f, -0x5000, BladeSub::SlideFar);
        bladeGo(s, f);
        break;
    case BladeSub::Return: bladeReturn(s, f); break;
    default: notPortedAt(kMandaGuardianPort, "0x59c120 sub-state", s.subState);
    }
    spinBlade(s);
    if (int32_t(actorTestMove(env, s)) < 0) actorPushOut(env, s); // exe 0x40f570 / 0x410960
    bladeHit(s, f);
}
// exe 0x59c700: spinning in place, wobbling while the guardian spins
void bladeInPlace(StaticActor& s, BladeFields& f) {
    ++f.timer;
    spinBlade(s);
    if (enemyGroupMarks(f.group, EnemyGroupMarks::Either) & kMarkSpin) s.rotation.z = (f.timer & 1) ? 0x20 : -0x20;
    else s.rotation.z = 0;
    bladeHit(s, f);
}
// exe 0x59c760: shuttles along its travel's yaw at its speed for 0x1000, waits 15 ticks, turns back
void bladeShuttle(StaticActor& s, BladeFields& f) {
    switch (ShuttleSub(s.subState)) {
    case ShuttleSub::Start:
        f.travel = 0x1000;
        s.subState = uint8_t(ShuttleSub::Travel);
        [[fallthrough]];
    case ShuttleSub::Travel:
        moveAlongYaw(s.pos, f.travelYaw, 0, 0, s.velocity.z); // exe 0x5e36f0
        f.travel = int16_t(uint16_t(f.travel) + uint32_t(int32_t(s.velocity.z) >> 4));
        if (f.travel <= 0) {
            s.subState = uint8_t(ShuttleSub::Turn);
            f.timer = 0xf;
        }
        break;
    case ShuttleSub::Turn:
        --f.timer;
        if (f.timer == 0) {
            f.travelYaw = int16_t(uint16_t(f.travelYaw) ^ 0x800u);
            spinBlade(s);
            s.subState = uint8_t(ShuttleSub::Start);
            bladeHit(s, f);
            return;
        }
        break;
    default: break;
    }
    spinBlade(s);
    bladeHit(s, f);
}
// exe 0x59bee0
void bladeUpdate(const PlayerEnv& env, StaticActor& s) {
    BladeFields& f = classState<BladeFields>(s);
    s.savePreviousPose();
    s.savePreviousContact();
    switch (BladeState(s.state)) { // [0x8ce7b0]
    case BladeState::Init: bladeInit(env, s, f); return;
    case BladeState::Run: break; // exe 0x59c0f0
    case BladeState::Idle: return;
    default: notPortedAt(kMandaGuardianPort, "0x59bee0 state", s.state);
    }
    const uint8_t v = s.variant;
    if (v >= kBladeScenery) return;
    switch (v) {
    case kBladeSpinner: bladeSpinner(env, s, f); break;
    case kBladeInPlace: bladeInPlace(s, f); break;
    case kBladeShuttle: bladeShuttle(s, f); break;
    default:
        notPortedAt(kMandaGuardianPort, "0x59c0f0 variant", v); // [0x8ce7c0] holds variants 0..2
    }
}

enum class FallingBladeSub : uint8_t { Start = 0, Falling = 1, Landed = 2 };
constexpr uint8_t kFallHitWall = 0x01, kFallHitNoCell = 0x08, kFallHitGround = 0x10, kFallHitCeiling = 0x20;
const DataTable<ScalarTable<uint8_t, 1>> fallingBladeModel{TableId::MandaGuardianFallingBladeModel}; // exe data 0x8ce820
const DataTable<CollisionBoxRecord> fallingBladeBox{TableId::MandaGuardianFallingBladeBox};
constexpr uint32_t kFlagBladeLanded = 0x680;
// exe 0x59c930: the room collision (pushed out; back to the previous position when stuck), its results
void fallingBladeCollide(const PlayerEnv& env, StaticActor& s, FallingBladeFields& f) {
    f.hits = 0;
    uint32_t r = actorTestMove(env, s);
    if (r & kMoveNoCell) f.hits |= kFallHitNoCell;
    if (int32_t(r) < 0) {
        r = actorPushOut(env, s);
        if (int32_t(r) < 0) s.pos = s.prevPos;
        if (r & kSolidOnSides) f.hits |= kFallHitWall;
        if (r & kSolidOnYPlus) f.hits |= kFallHitGround;
        if (r & kSolidOnYMinus) f.hits |= kFallHitCeiling;
    }
}
void fallingBladeUpdate(const PlayerEnv& env, StaticActor& s) {
    FallingBladeFields& f = classState<FallingBladeFields>(s);
    s.savePreviousPose();
    s.savePreviousContact();
    if (BladeState(s.state) == BladeState::Init) { // exe 0x59c850
        if (!bindActorModel(s, fallingBladeModel[0])) {
            freeActor(s); // exe 0x5e1540
            return;
        }
        s.velocity.clearWords();
        s.rotation.z = 0;
        s.rotation.x = 0;
        s.onPlatform = 0;
        s.interactionBox = nullptr;
        s.interaction = 0;
        s.maxHitPoints = 1;
        s.hitPoints = 1;
        s.airborne = 1;
        s.collisionBox.pointAt(fallingBladeBox.stateAddress());
        s.clearClassBlock(); // exe 0x40dc40(+0xbc, +0xc8)
        s.enterState(uint8_t(BladeState::Run));
        return;
    }
    if (BladeState(s.state) != BladeState::Run) notPortedAt(kMandaGuardianPort, "0x59c800 state", s.state);
    if (FallingBladeSub(s.subState) == FallingBladeSub::Start) {
        s.velocity.y = 0;
        s.subState = uint8_t(FallingBladeSub::Falling);
    } else if (FallingBladeSub(s.subState) != FallingBladeSub::Falling) {
        spinBlade(s);
        return;
    }
    s.pos.y = int32_t(uint32_t(s.pos.y) + uint32_t(int32_t(s.velocity.y) * 0x1000));
    s.velocity.y = int16_t(uint16_t(s.velocity.y) + 0x40u);
    fallingBladeCollide(env, s, f);
    if (f.hits & kFallHitGround) {
        s.subState = uint8_t(FallingBladeSub::Landed);
        s.airborne = 0;
        setStoryFlag(kFlagBladeLanded);
    }
    spinBlade(s);
}

enum class ChestState : uint8_t { Init = 0, Wait = 1 };
const DataTable<CollisionBoxRecord> chestBox{TableId::MandaGuardianChestBox};
void chestBlast(const AnimatedActor& a) {
    const uint32_t address = allocActor60(0); // exe 0x5e1300
    if (!address) return;
    StaticActor& e = staticActorAt(address);
    BlastParams params(e);
    e.flags = kActorLive;
    e.type = typeByte(GlobalStaticType::Blast);
    e.variant = 0;
    e.loopFlags |= kActorSweepGroup2;
    e.pos = a.pos;
    params.size = 0xc0;
    params.attack = 0;
}
void openedChestUpdate(const PlayerEnv& env, AnimatedActor& a) {
    ChestFields& f = classState<ChestFields>(a);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (ChestState(a.state)) {
    case ChestState::Init: // exe 0x475b80
        if (!bindActorModel(a, 0)) {
            freeActor(a); // exe 0x5e1490
            return;
        }
        a.clearClassBlock(); // exe 0x40dc40(+0x14c, +0x168)
        a.hitPoints = 1;
        a.maxHitPoints = 1;
        a.velocity.clearWords();
        a.rotation.z = 0;
        a.rotation.x = 0;
        a.collisionBox.pointAt(chestBox.stateAddress());
        a.interactionBox = nullptr;
        a.interaction = 0;
        a.airborne = 0;
        a.onPlatform = 0;
        a.platform = nullptr;
        a.lastMover = nullptr;
        a.pos.y.whole = int16_t(actorGround(env, a)); // exe 0x40f190
        a.anim = 2;
        f.startKey = 0x10;
        a.enterState(uint8_t(ChestState::Wait));
        break;
    case ChestState::Wait: // exe 0x475c10
        if (!storyFlag(kFlagBladeLanded)) break;
        chestBlast(a);
        freeActor(a); // exe 0x5e1490
        break;
    default: notPortedAt(kMandaGuardianPort, "0x475ae0 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim == a.prevAnim) {
        animStep(a); // exe 0x5e1e80
    } else {
        animSet(a, a.anim, f.startKey); // exe 0x5e1c30
        f.startKey = 0;
    }
    animPose(a);
}

} // namespace

void mandaGuardianUpdate(const PlayerEnv& env, uint32_t actor) { guardianUpdate(env, animatedActorAt(actor)); }
void mandaShotUpdate(const PlayerEnv& env, uint32_t actor) { shotUpdate(env, projectileActorAt(actor)); }
void shotTrailUpdate(const PlayerEnv&, uint32_t actor) { shotTrailTick(controllerActorAt(actor)); }
void hitMuteUpdate(const PlayerEnv&, uint32_t actor) { hitMuteTick(controllerActorAt(actor)); }
void mandaCloudUpdate(const PlayerEnv&, uint32_t actor) { cloudTick(effectActorAt(actor)); }
void mandaCastGuardianUpdate(const PlayerEnv& env, uint32_t actor) { castGuardianUpdate(env, animatedActorAt(actor)); }
void mandaOutroGuardianUpdate(const PlayerEnv& env, uint32_t actor) {
    outroGuardianUpdate(env, animatedActorAt(actor));
}
void mandaHopperUpdate(const PlayerEnv& env, uint32_t actor) { hopperUpdate(env, animatedActorAt(actor)); }
void mandaCast2GuardianUpdate(const PlayerEnv& env, uint32_t actor) {
    cast2GuardianUpdate(env, animatedActorAt(actor));
}
void mandaBladeUpdate(const PlayerEnv& env, uint32_t actor) { bladeUpdate(env, staticActorAt(actor)); }
void mandaFallingBladeUpdate(const PlayerEnv& env, uint32_t actor) { fallingBladeUpdate(env, staticActorAt(actor)); }
void mandaOpenedChestUpdate(const PlayerEnv& env, uint32_t actor) { openedChestUpdate(env, animatedActorAt(actor)); }

} // namespace od::port
