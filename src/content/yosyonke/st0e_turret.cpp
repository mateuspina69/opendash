// ST0E: the boss vehicle's turret and what it fires (see st0e_turret.hpp).
//
// The turret (exe 0x503bd0, spawned by the vehicle's first sub-state). States (the exe's table 0x897d38): 0 bind
// 0x504290, 1 run 0x5044a0 (the sub-states 0x897d44: 0 0x5044e0 wait for flag 0x581, 1 0x504520 three bomb volleys per
// object-state signal, 2 0x5046b0 the cannons + bombs while flag 0x582, 3 0x5047c0 the end: event 0x12 message 1, the
// defended vehicle's HP carried), 2 free 0x468fa0 (the table repeats the sub-states as states 3..6). It keeps its
// block in a bone block of its own (block 2, 0xa0 bytes): the part fire emitters, the seven e0 watchers (the hit
// owners), eight hit boxes (the body's capsules, the two arms, the three parts, the core, the beam head), eight sub-HPs
// (the row's +8.. << 4), the beam sweep and its speed, the cannon / volley timers, the arms' death timers, the dying
// timer, the end delay, the cannon burst count, the bomb slot. Its flags: kTurret*.
// The bomb (exe 0x5cc850, its states 0x8d1678: 0x5cc880 / 0x5cc900 / 0x5cca80) hurts the defended vehicle on contact.
// The cannon shot (exe 0x5d0430, its states 0x8d17fc: 0x5d04d0 / 0x5d0560; its outcomes as the enemy shot 0x5c52f0).
// The glow billboard (exe 0x43a760, the variants 0x88d788: 0 exe 0x43a780 (states 0x43a7a0 / 0x43a7c0), 1 exe 0x43a810
// (states 0x43a830 / 0x43a850 / 0x4358c0)).
// The exe writes through null pointers when an allocation fails (the watchers of the bind, the beam / glow pointers,
// the bomb read by its muzzle flash): port gaps there. The Servbots flung out of a broken part are
// content/yosyonke/st0e_servbots (pool 0x20 type 0x2d variant 0x41); the beam (static 0x56 exe 0x5b9910) is
// content/nino/nino_ruin_st35; the fire emitters (static stage 3 exe 0x592050) content/first_ruin; the blasts (static
// global 7) game/explosions; the hit sparks (effect global 0) game/hit_effects; the flash (effect stage 7)
// content/intro/crash_landing.
#include "content/yosyonke/st0e_turret.hpp"
#include "content/yosyonke/st0e_turret_state.hpp"

#include "content/yosyonke/st0e_vehicles.hpp"
#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/camera.hpp"
#include "game/draw_commands.hpp"
#include "game/encounter.hpp"
#include "game/event.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story_flags.hpp"

using namespace od::port::st0e_turret_state;

namespace od::port {

using namespace st0e_turret_state;

namespace {

int16_t t16(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }

constexpr size_t kBodyBox = 0, kArmBoxA = 1, kArmBoxB = 2, kPartBoxA = 3, kCoreBox = 6, kBeamBox = 7;
constexpr size_t kArmHpA = 0, kArmHpB = 1, kPartHpA = 2, kCoreHp = 5;
constexpr size_t kTurretBoxes = 8;
// the flags (+0x168; the high byte's bits by the byte stores of +0x169)
constexpr uint8_t kTurretSignal = 0x01;
constexpr uint8_t kTurretCoreBlast = 0x02;
constexpr uint8_t kTurretPartADown = 0x04, kTurretPartBDown = 0x08, kTurretPartCDown = 0x10;
constexpr uint8_t kTurretBeam = 0x20;
constexpr uint8_t kTurretAimBombs = 0x40;
constexpr uint8_t kTurretCannons = 0x80;
constexpr uint8_t kTurretBombPending = 0x01; // +0x169 (0x100)
constexpr uint16_t kTurretAllPartsDown = 0x200;
constexpr uint16_t kTurretFire3of4 = 0x400, kTurretFireHalf = 0x800, kTurretFire1of4 = 0x1000;
constexpr uint16_t kTurretArmADead = 0x2000, kTurretArmBDead = 0x4000;
constexpr uint8_t kTurretArmABlast = 0x20, kTurretArmBBlast = 0x40, kTurretDying = 0x80; // +0x169
constexpr uint8_t kVoiceVolleySaid = 0x01, kVoiceRoundSaid = 0x02;
enum class TurretState : uint8_t { Bind = 0, Run = 1, Free = 2 };
enum class TurretSub : uint8_t { Wait = 0, Volleys = 1, Cannons = 2, End = 3 };
enum class VolleyStep : uint8_t { Delay = 0, Fire = 1, WaitLanded = 2, NextRound = 3 };
enum class BeamStep : uint8_t { Aim = 0, Charge = 1, Fire = 2, SweepRight = 3, SlowRight = 4, SweepLeft = 5, SlowLeft = 6 };
constexpr uint32_t kFlagFightStarted = 0x581, kFlagFrontBroken = 0x582;
constexpr uint32_t kHitByPlayer = kHitAttackByPlayer;
constexpr uint8_t kShotType = 0x2e, kBeamType = 0x56, kGlowType = 0x1b, kFlungServbotType = 0x2d;
constexpr uint8_t kFlashEffectType = 7; // the stage effect 7 (the flash, content/intro/crash_landing)
constexpr uint32_t kHarmlessSolid = kHitAttackHarmless | kHitAttackSolid;
// the voices / sounds of its tables
const DataTable<ScalarTable<uint32_t, 8>> hurtVoices{TableId::St0eTurretHurtVoices};
const DataTable<ScalarTable<uint32_t, 4>> sweepVoices{TableId::St0eTurretSweepVoices};
const DataTable<ScalarTable<uint32_t, 4>> coreBlastSounds{TableId::St0eTurretCoreBlastSounds};
const DataTable<ScalarTable<uint32_t, 4>> armBlastSounds{TableId::St0eTurretArmBlastSounds};
// the Servbots flung out of a blast: their yaws, offsets and velocities (by blast)
struct WreckPair : DataRecord {
    int16_t first = 0, second = 0;
    static WreckPair load(const TableReader& r) { return {{}, r.s16(0), r.s16(2)}; }
};
using WreckPairs = DataTable<RecordTable<WreckPair, 4, 4>>;
struct WreckTable {
    const DataTable<ScalarTable<uint16_t, 4>>& yaws;
    const WreckPairs& offsets;
    const WreckPairs& velocities;
};
// (exe data: the core's 0x897dbc / 0x897dac / 0x897dd4, the arms' 0x897df4 / 0x897de4 / 0x897e0c, the dying's 0x897e2c /
// 0x897e1c / 0x897e34)
const DataTable<ScalarTable<uint16_t, 4>> coreWreckYaws{TableId::St0eTurretCoreWreckYaws},
    armWreckYaws{TableId::St0eTurretArmWreckYaws}, dyingWreckYaws{TableId::St0eTurretDyingWreckYaws};
const WreckPairs coreWreckOffsets{TableId::St0eTurretCoreWreckOffsets}, coreWreckVelocities{TableId::St0eTurretCoreWreckVelocities},
    armWreckOffsets{TableId::St0eTurretArmWreckOffsets}, armWreckVelocities{TableId::St0eTurretArmWreckVelocities},
    dyingWreckOffsets{TableId::St0eTurretDyingWreckOffsets}, dyingWreckVelocities{TableId::St0eTurretDyingWreckVelocities};
const WreckTable coreWrecks{coreWreckYaws, coreWreckOffsets, coreWreckVelocities};
const WreckTable armWrecks{armWreckYaws, armWreckOffsets, armWreckVelocities};
const WreckTable dyingWrecks{dyingWreckYaws, dyingWreckOffsets, dyingWreckVelocities};
// the stat row: the parts' sub-HPs after the generic fields (+8..+0xf)
struct TurretStats : AnimatedActorStats {
    static constexpr uint32_t kBytes = 0x10;
    std::array<uint8_t, 8> partHitPoints{}; // +0x8 (over the generic fields there)
    template <class R> static TurretStats load(const R& r) {
        TurretStats v;
        static_cast<AnimatedActorStats&>(v) = AnimatedActorStats::load(r);
        for (size_t i = 0; i < v.partHitPoints.size(); ++i) v.partHitPoints[i] = r.u8(8 + i);
        return v;
    }
};
TurretStats statsOf(const AnimatedActor& a) { return dataRecord<TurretStats>(statTableRow(actorStatTables().animated, a.type), TurretStats::kBytes); }
uint32_t ownerWord(uint8_t slot, const AnimatedActorStats& row, uint32_t shape) {
    return hitOwner(slot) | hitHeightLevel(row.hitHeight) | shape;
}
void submit(const BonePoint& p, HitBoxHead& box, uint32_t owner, uint32_t attack) { hitListSubmit(p.v, box, owner, attack); }
StaticActor& staticAt(uint32_t address) { return staticActorAt(address); }

// the Servbot spawned flung (pool 0x20 type 0x2d variant 0x41, its mode 6; content/yosyonke/st0e_servbots) from the bone
// point (0, -0x70, -0x70): its yaw, the x / z offsets, the negated velocities
struct FlungSpawnParams {
    explicit FlungSpawnParams(const AnimatedActor& actor) : FlungSpawnParams(const_cast<AnimatedActor&>(actor)) {}
    explicit FlungSpawnParams(AnimatedActor& actor) : model(actor.params[0]), unusedD(actor.params[1]), unusedE(actor.params, 2) {}
    uint8_t& model;
    uint8_t& unusedD;
    BytesField<uint16_t, 4> unusedE;
};
void spawnWreck(AnimatedActor& a, uint32_t bone, uint16_t yaw, const WreckPair& offs, const WreckPair& vel) {
    const uint32_t at = allocActor20(); // exe 0x5e1160
    if (!at) return;
    AnimatedActor& e = animatedActorAt(at);
    e.flags = kActorLive;
    e.type = kFlungServbotType;
    e.subtype = 0;
    e.variant = 0x41;
    e.spawnByte = 6;
    e.rotation.y = int16_t(yaw);
    e.loopFlags |= kActorSweepGroup2;
    const BonePoint p = bonePointAt(a, bone, 0, -0x70, -0x70);
    e.pos.x = int32_t(uint32_t(int32_t(offs.first) + p.v[0]) * 0x10000u);
    e.pos.y = int32_t(uint32_t(int32_t(p.v[1])) << 16);
    e.pos.z = int32_t(uint32_t(int32_t(offs.second) + p.v[2]) * 0x10000u);
    e.velocity.y = int16_t(-vel.first);
    e.velocity.z = int16_t(-vel.second);
    FlungSpawnParams s(e);
    s.model = 0;
    s.unusedD = 0;
    s.unusedE = 0;
}
void spawnWrecks(AnimatedActor& a, uint32_t bone, const WreckTable& t) {
    for (size_t k = 0; k < 4; ++k) spawnWreck(a, bone, t.yaws[k], t.offsets[k], t.velocities[k]);
}

// exe 0x504810(actor, k): a volley's bomb k (unless part k is down): pool 0x40 type 0x24 at bone 6 ((k 0 / 1 / else:
// 0xc0 / 0 / -0xc0), -0xb8, 0xb0), the yaw + the bombs' aim, lobbed (speed 0x680, gravity 0x10) at the player +- 0x3f
// in x / z; its damage the row's attack; sound 0x262; a muzzle flash (global effect 0 variant 3, size 0xc0) at it
void lobBomb(AnimatedActor& a, TurretFields& f, int8_t k) {
    const TurretStats row = statsOf(a);
    if (uint32_t(f.flags) & (1u << ((uint32_t(int32_t(k)) + 2u) & 0x1f))) return;
    const uint32_t at = allocActor40(kActorStageClass); // exe 0x5e1280
    ProjectileActor* bomb = nullptr;
    if (at) {
        ProjectileActor& e = projectileActorAt(at);
        bomb = &e;
        e.flags = kActorLive;
        e.type = kBombType;
        e.variant = 0;
        e.loopFlags |= kActorSweepGroup2;
        const int x = k == 0 ? 0xc0 : (k == 1 ? 0 : -0xc0);
        const BonePoint p = bonePointAt(a, 6, x, -0xb8, 0xb0);
        e.pos.x.whole = p.v[0];
        e.pos.y.whole = p.v[1];
        e.pos.z.whole = p.v[2];
        // the player's 16.16 position, its integer x / z jittered by (rng & 0x7f) - 0x3f
        const Player& pl = player();
        int32_t to[3];
        const uint32_t r1 = rngStep();
        to[0] = int32_t((uint32_t(uint16_t(pl.pos.x.whole + int((r1 & 0x7f)) - 0x3f)) << 16) | pl.pos.x.frac);
        to[1] = pl.pos.y;
        const uint32_t r2 = rngStep();
        to[2] = int32_t((uint32_t(uint16_t(pl.pos.z.whole + int((r2 & 0x7f)) - 0x3f)) << 16) | pl.pos.z.frac);
        e.rotation.x = 0;
        e.rotation.z = 0;
        e.rotation.y = int16_t(a.rotation.y + f.bombAim);
        e.velocity.x = 0;
        const int32_t from[3] = {e.pos.x, e.pos.y, e.pos.z};
        e.velocity.y = int16_t(launchSpeed(from, to, 0x680, 0x10, 0xffffffffu)); // exe 0x5e3ee0
        e.velocity.z = -0x680;
        e.acceleration.y = 0x10;
        BombSpawnParams b(e);
        b.damage = int8_t(row.attack);
        b.held = 0;
        soundPlayAtPos(0x262, e.pos);
    }
    const uint32_t flashAt = allocActorA0(0); // exe 0x5e13b0
    if (!flashAt) return;
    if (!bomb) notPorted(kSt0ePort, "0x504810: the muzzle flash reads the bomb the exe failed to allocate");
    EffectActor& flash = effectActorAt(flashAt);
    flash.flags = kActorLiveDrawn;
    flash.type = typeByte(GlobalEffectType::HitSpark);
    flash.variant = 3;
    flash.loopFlags |= kActorSweepGroup2;
    flash.pos.x = bomb->pos.x.whole;
    flash.pos.y = bomb->pos.y.whole;
    flash.pos.z = bomb->pos.z.whole;
    classState<HitSparkFields>(flash).size = 0xc0;
}
// exe 0x504a10 (kTurretAimBombs): the bombs' aim turns bone 6 (-0xc0, -0xb8, 0xb0) toward the player
void aimBombs(AnimatedActor& a, TurretFields& f) {
    aimStep(a, f.bombAim, angleFromPlayer(bonePointAt(a, 6, -0xc0, -0xb8, 0xb0)));
}
// exe 0x505c00: the beam's aim turns bone 5 (0x100, 0, 0) toward the player; the beam's yaw = its aim
void aimBeam(AnimatedActor& a, TurretFields& f) {
    aimStep(a, f.beamAim, angleFromPlayer(bonePointAt(a, 5, 0x100, 0, 0)));
    f.beamYaw = f.beamAim;
}
// exe 0x505ec0(actor, k): cannon k (bone 2 / 4): its yaw at 0x400 left / right of the player (along the player's yaw),
// its pitch = 0x800 - the pitch to the player's centre (y - 0x78)
void aimCannon(AnimatedActor& a, TurretFields& f, uint32_t k) {
    const uint32_t bone = k ? 4 : 2;
    const BonePoint p = bonePointAt(a, bone, 0, 0, 0x93);
    int32_t o[3];
    rotateByYaw(player().rotation.y, k ? 0x400 : -0x400, 0, 0, o); // exe 0x5e36f0 on a copy of the player's position
    const uint32_t qx = uint32_t(player().pos.x) + uint32_t(o[0]);
    const uint32_t qz = uint32_t(player().pos.z) + uint32_t(o[2]);
    int16_t& yaw = f.cannonYaw[k];
    aimStep(a, yaw,
            angleOf(int32_t(qx), int32_t(qz), int32_t(uint32_t(int32_t(p.v[0])) << 16),
                    int32_t(uint32_t(int32_t(p.v[2])) << 16)));
    const BonePoint q = bonePointAt(a, bone, 0, 0, 0x93);
    const int16_t t[3] = {player().pos.x.whole, t16(player().pos.y.whole - 0x78), player().pos.z.whole};
    f.cannonPitch[k] = t16(0x800 - pitchOf(q.v, t));
}
// exe 0x505d80(actor, k): a cannon shot (pool 0x40 type 0x2e, variant 0x81, life 0x50, damage the row's strong attack)
// from bone k (2 / 4) (0, 0, 0xee) along the cannon's yaw + 0x80 and the pitch 0x1000 - its pitch. The exe's debug
// counter (the second pad's bits 0x10 / 0x40) is kept.
struct ShotParams {
    explicit ShotParams(const ProjectileActor& actor) : ShotParams(const_cast<ProjectileActor&>(actor)) {}
    explicit ShotParams(ProjectileActor& actor) : life(actor.params, 0), damage(actor.params, 2) {}
    BytesField<int16_t, 4> life;
    BytesField<uint16_t, 4> damage;
};
void fireCannon(AnimatedActor& a, TurretFields& f, uint32_t k) {
    if (pad1().held & 0x10) ++turretDebug().shotCounter;
    if (pad1().held & 0x40) --turretDebug().shotCounter;
    const uint32_t at = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!at) return;
    ProjectileActor& e = projectileActorAt(at);
    e.flags = kActorLive;
    e.loopFlags |= kActorSweepGroup2;
    e.type = kShotType;
    e.subtype = 0;
    e.variant = 0x80;
    uint32_t bone;
    if (k != 2) {
        e.rotation.y = int16_t(a.rotation.y + f.cannonYaw[1] + 0x80);
        e.rotation.z = 0;
        e.rotation.x = int16_t(0x1000 - f.cannonPitch[1]);
        bone = 4;
    } else {
        e.rotation.y = int16_t(a.rotation.y + f.cannonYaw[0] + 0x80);
        e.rotation.z = 0;
        e.rotation.x = int16_t(0x1000 - f.cannonPitch[0]);
        bone = 2;
    }
    int32_t pos[3];
    fixedOfPoint(bonePointAt(a, bone, 0, 0, 0xee), pos);
    e.pos.x = pos[0];
    e.pos.y = pos[1];
    e.pos.z = pos[2];
    e.variant = 0x81;
    ShotParams s(e);
    s.life = 0x50;
    s.damage = statsOf(a).strongAttack;
}
// exe 0x505ce0 (kTurretCannons): both cannons aim; bursts of 3 shots 2 ticks apart from the living cannons, 0x1e ticks
// between the bursts
void cannons(AnimatedActor& a, TurretFields& f) {
    TurretBlock& blk = *f.block;
    aimCannon(a, f, 0);
    aimCannon(a, f, 1);
    if (blk.burstCount >= 3) {
        blk.cannonTimer = 0x1e;
        blk.burstCount = 0;
        return;
    }
    if (blk.cannonTimer == 0) {
        if (!(f.flagsHigh & kTurretArmABlast)) fireCannon(a, f, 2);
        if (!(f.flagsHigh & kTurretArmBBlast)) fireCannon(a, f, 4);
        blk.cannonTimer = 2;
        ++blk.burstCount;
        return;
    }
    --blk.cannonTimer;
}
// exe 0x505b40: the beam (static stage 0x56, its damage the row's shot attack) at bone 5 (0x100, 0, 0), the pitch
// -0x38, the yaw the beam's aim + the turret's
void spawnBeam(AnimatedActor& a, TurretFields& f) {
    const TurretStats row = statsOf(a);
    const uint32_t at = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!at) return;
    StaticActor& e = staticActorAt(at);
    e.loopFlags |= kActorSweepGroup2;
    e.flags = kActorLive;
    e.type = kBeamType;
    e.subtype = 0;
    e.variant = 0;
    e.spawnByte = 0;
    e.runFlags |= kActorWarpKept;
    int32_t pos[3];
    fixedOfPoint(bonePointAt(a, 5, 0x100, 0, 0), pos);
    e.pos.x = pos[0];
    e.pos.y = pos[1];
    e.pos.z = pos[2];
    e.rotation.x = -0x38;
    e.rotation.z = 0;
    e.rotation.y = int16_t(f.beamAim + a.rotation.y);
    e.stepCounter = row.shotAttack;
    f.beam = e.at;
}
// the beam's head at bone 5 (0x100, 0, 0): the glow (an s16 point) and the beam (16.16)
void placeGlow(AnimatedActor& a, TurretFields& f) {
    if (f.glow == kNoActor) notPorted(kSt0ePort, "0x505330: no glow (+0x154)");
    const BonePoint p = bonePointAt(a, 5, 0x100, 0, 0);
    EffectActor& g = effectActorAt(f.glow);
    g.pos.x = p.v[0];
    g.pos.y = p.v[1];
    g.pos.z = p.v[2];
}
void placeBeam(AnimatedActor& a, TurretFields& f) {
    if (f.beam == kNoActor) notPorted(kSt0ePort, "0x505330: no beam (+0x150)");
    int32_t pos[3];
    fixedOfPoint(bonePointAt(a, 5, 0x100, 0, 0), pos);
    StaticActor& b = staticAt(f.beam);
    b.pos.x = pos[0];
    b.pos.y = pos[1];
    b.pos.z = pos[2];
}
// the beam's yaw = its aim + the turret's yaw + the sweep; the beam yaw = that - the turret's yaw
void beamYawWithSweep(AnimatedActor& a, TurretFields& f, TurretBlock& blk) {
    if (f.beam == kNoActor) notPorted(kSt0ePort, "0x505330: no beam (+0x150)");
    StaticActor& b = staticAt(f.beam);
    b.rotation.y = int16_t(f.beamAim + a.rotation.y + blk.sweep);
    f.beamYaw = int16_t(b.rotation.y - a.rotation.y);
}
void beamHum(TurretFields& f) { // the hum 0x264 at the beam while it lives (not dying)
    if (!(f.flagsHigh & kTurretDying) && staticAt(f.beam).state != 2) soundPlayAtPos(0x264, staticAt(f.beam).pos);
}
// the sweep's turn voice: the roll kept; one time in 4 a voice of 0x897d9c
void sweepVoice() {
    const uint32_t r = rngStep();
    turretDebug().sweepRoll = r;
    if (!(r & 3)) vehicleVoice(uint16_t(sweepVoices[(r >> 2) & 3]));
}
// exe 0x505330 (kTurretBeam): the beam by its step: Aim until the player is within 0x100 of straight behind (the glow,
// effect 0x1b variant 1, sound 0x263); Charge 0x96 ticks, the beam; Fire 0x5a ticks; the sweeps (the sweep swings
// +-0x20 with its speed, the voices); the beam / the glow follow bone 5
void beam(AnimatedActor& a, TurretFields& f) {
    const uint8_t s = f.beamStep;
    TurretBlock& blk = *f.block;
    const bool dying = (f.flagsHigh & kTurretDying) != 0;
    auto sweepTail = [&](bool aim) { // the shared tail (exe 0x50588f / 0x5058c9)
        if (aim) {
            aimBeam(a, f);
            beamYawWithSweep(a, f, blk);
        }
        placeGlow(a, f);
        placeBeam(a, f);
        beamHum(f);
        f.flags = uint16_t(f.flags & ~uint16_t(kTurretCannons));
    };
    auto slowTail = [&]() { // the steps SlowRight / SlowLeft (exe 0x505b05): the cannons resume
        placeGlow(a, f);
        placeBeam(a, f);
        beamHum(f);
        if (!(f.flagsLow & kTurretCannons)) blk.burstCount = 0;
        f.flags |= uint16_t(kTurretCannons);
    };
    switch (BeamStep(s)) {
    case BeamStep::Aim: {
        if (!dying) aimBeam(a, f);
        const uint16_t yaw = uint16_t(a.rotation.y);
        a.rotation.y = int16_t(yaw + f.beamAim);
        f.beamYaw = f.beamAim;
        if (facesAwayFrom(a, player().pos, 0x100)) {
            ++f.beamStep;
            blk.sweep = 0;
            blk.sweepSpeed = 0;
            const uint32_t at = allocActorA0(kActorStageClass); // exe 0x5e13a0
            if (at) {
                EffectActor& e = effectActorAt(at);
                e.loopFlags |= kActorLateLoop;
                e.flags = kActorLive;
                e.type = kGlowType;
                e.variant = 1;
                e.subtype = 0;
                e.spawnByte = 0;
                e.runFlags |= kActorWarpKept;
                const BonePoint p = bonePointAt(a, 5, 0x100, 0, 0);
                e.pos.x = p.v[0];
                e.pos.y = p.v[1];
                e.pos.z = p.v[2];
                e.params[0] = 0xa0;
                e.params[2] = 2;
                f.glow = e.at;
                f.beamTimer = 0x96;
                if (!dying) soundPlayAtPosS16(0x263, e.pos); // exe 0x5d5420
            }
        }
        a.rotation.y = int16_t(yaw);
        return;
    }
    case BeamStep::Charge:
        if (!dying) {
            aimBeam(a, f);
            if (f.beamTimer != 0) {
                --f.beamTimer;
            } else {
                spawnBeam(a, f);
                f.beamTimer = 0x5a;
                ++f.beamStep;
            }
        }
        f.beamYaw = f.beamAim;
        placeGlow(a, f);
        return;
    case BeamStep::Fire:
        if (f.beamTimer != 0) {
            if (f.beam == kNoActor) notPorted(kSt0ePort, "0x505330: no beam (+0x150)");
            StaticActor& b = staticAt(f.beam);
            b.rotation.y = int16_t(f.beamAim + a.rotation.y);
            f.beamYaw = int16_t(b.rotation.y - a.rotation.y);
            placeGlow(a, f);
            placeBeam(a, f);
            --f.beamTimer;
            return;
        }
        f.beamStep = uint8_t(s + 1);
        return;
    case BeamStep::SweepRight:
        if (dying) {
            sweepTail(false);
            return;
        }
        if (blk.sweep >= 0x20) {
            f.beamStep = uint8_t(s + 1);
            sweepVoice();
        } else {
            if (blk.sweepSpeed >= 0x1800) blk.sweepSpeed = 0x1800;
            else blk.sweepSpeed = int16_t(blk.sweepSpeed + 0x380);
            blk.sweep = int16_t(t16(blk.sweepSpeed >> 8) + blk.sweep);
        }
        sweepTail(true);
        return;
    case BeamStep::SlowRight:
    case BeamStep::SlowLeft:
        if (!dying) {
            if (blk.sweepSpeed >= 1) {
                blk.sweepSpeed = int16_t(blk.sweepSpeed - 0xc0);
            } else {
                f.beamStep = uint8_t(BeamStep(s) == BeamStep::SlowRight ? BeamStep::SweepLeft : BeamStep::SweepRight);
                blk.sweepSpeed = 0;
            }
            const int16_t d = t16(blk.sweepSpeed >> 8);
            blk.sweep = int16_t(BeamStep(s) == BeamStep::SlowRight ? blk.sweep + d : blk.sweep - d);
            aimBeam(a, f);
            beamYawWithSweep(a, f, blk);
        }
        slowTail();
        return;
    case BeamStep::SweepLeft:
        if (dying) {
            sweepTail(false);
            return;
        }
        if (blk.sweep <= -0x20) {
            f.beamTimer = 0x14;
            f.beamStep = uint8_t(s + 1);
            sweepVoice();
        } else {
            if (blk.sweepSpeed >= 0x1800) blk.sweepSpeed = 0x1800;
            else blk.sweepSpeed = int16_t(blk.sweepSpeed + 0x380);
            blk.sweep = int16_t(blk.sweep - t16(blk.sweepSpeed >> 8));
        }
        sweepTail(true);
        return;
    default: return;
    }
}
// exe 0x506090 (kTurretCoreBlast): the core's blast after its ticks (sparks on odd ticks): the part emitters end, a
// blast (0xff), voice 0x26a, the remaining parts' HPs taken from the main HP, a fire emitter on bone 6, four wrecks, a
// sound; the flag cleared
void coreBlast(AnimatedActor& a, TurretFields& f) {
    if (a.parts[6].flags & kPartHidden) return;
    --f.coreTimer;
    TurretBlock& blk = *f.block;
    if (uint16_t(f.coreTimer) & 1) spawnBoneSpark(a, 6, 0, -0x70, -0x70, true);
    if (f.coreTimer != 0) return;
    a.parts[6].flags |= kPartHidden;
    for (size_t k = 0; k < 3; ++k)
        if (blk.emitters(k) != kNoActor) {
            staticAt(blk.emitters(k)).subState = kFireEmitterEnd;
            blk.emitters(k) = kNoActor;
        }
    spawnBoneBlast(a, 6, 0, -0x70, -0x70, 0xff);
    vehicleVoice(0x26a);
    f.blastMark = 3;
    if (blk.partHp[kCoreHp] >= 0) {
        a.hitPoints = int16_t(a.hitPoints - blk.partHp[kCoreHp]);
    } else {
        for (size_t k = kPartHpA; k < kPartHpA + 3; ++k)
            if (blk.partHp[k] >= 0) {
                a.hitPoints = int16_t(a.hitPoints - blk.partHp[k]);
                blk.partHp[k] = -1;
            }
    }
    spawnFireEmitter(a, 0x15, 6, 0, 0xffc0, 0, 0);
    spawnWrecks(a, 6, coreWrecks);
    soundPlayAtPos(uint16_t(coreBlastSounds[rngStep() & 3]), a.pos);
    f.flags = uint16_t(f.flags & ~uint16_t(kTurretCoreBlast));
}
// exe 0x506470(actor, bone, k) (kTurretArmABlast / kTurretArmBBlast): arm k's blast after its delay (sparks on odd
// ticks): its parts hidden, a blast (0x7f), a fire emitter, a wreck set, a sound
void armBlast(AnimatedActor& a, TurretFields& f, uint32_t bone, uint32_t k) {
    TurretBlock& blk = *f.block;
    if (a.parts[bone].flags & kPartHidden) return;
    int16_t& t = blk.armTimer[k];
    --t;
    if (uint16_t(t) & 1) spawnBoneSpark(a, bone, 0, 0, 0, false);
    if (t != 0) return;
    a.parts[bone].flags |= kPartHidden;
    a.parts[bone + 1].flags |= kPartHidden;
    spawnBoneBlast(a, bone, 0, 0, 0, 0x7f);
    if (StaticActor* e = spawnFireEmitter(a, 0x15, 6, 0, 0xffc0, 0, 0)) blk.armEmitter(k) = e->at;
    spawnWreck(a, bone, armWrecks.yaws[k], armWrecks.offsets[k], armWrecks.velocities[k]);
    soundPlayAtPos(uint16_t(armBlastSounds[rngStep() & 3]), a.pos);
}
// exe 0x5067b0 (kTurretDying): sparks on bone 7 every other tick; after the dying timer the big blast, a fire emitter
// (mode 7), four wrecks; then a blast on bone 7 (0, -0x2ae, 0) +- 0x7f every 4 ticks
void dying(AnimatedActor& a, TurretFields& f) {
    TurretBlock& blk = *f.block;
    if (gameState().frame & 1) spawnBoneSpark(a, 7, 0, 0, 0, false);
    if (a.parts[7].flags & kPartHidden) {
        if (gameState().frame & 3) return;
        const uint32_t at = allocActor60(0); // exe 0x5e1300
        if (!at) return;
        StaticActor& e = staticActorAt(at);
        e.flags = kActorLive;
        e.loopFlags |= kActorSweepGroup2;
        e.type = typeByte(GlobalStaticType::Blast);
        e.subtype = 0;
        e.variant = 0;
        const BonePoint p = bonePointAt(a, 7, 0, -0x2ae, 0);
        e.pos.x.whole = t16(sparkJitter(rngStep(), false) + p.v[0]);
        e.pos.y.whole = t16(sparkJitter(rngStep(), false) + p.v[1]);
        const uint32_t r = rngStep();
        BlastParams b(e);
        b.size = 0x7f;
        b.attack = 0;
        e.pos.z.whole = t16(sparkJitter(r, false) + p.v[2]);
        return;
    }
    --blk.dyingTimer;
    if (blk.dyingTimer != 0) return;
    a.parts[7].flags |= kPartHidden;
    spawnBoneBlast(a, 7, 0, 0, 0, 0x7f);
    spawnFireEmitter(a, 0x15, 7, 0, 0, 0, 0);
    spawnWrecks(a, 7, dyingWrecks);
}
// a part's own HP hit (exe 0x504ad0): the main HP swapped with the part's; dead -> onDead(); swapped back
template <class F> void partHit(AnimatedActor& a, HitBoxHead& box, int16_t& hp, F onDead) {
    if (!(box.result & kHitByPlayer)) return;
    const uint16_t keep = uint16_t(a.hitPoints);
    a.hitPoints = hp;
    if (hitPointsAdd(a, -int(box.result & kHitDamageMask)) != 0) onDead();
    hp = a.hitPoints;
    a.hitPoints = int16_t(keep);
}
// the hurt voice: not beaming and its timer out: 0x78 ticks; one time in 2 a voice of 0x897d7c
void hurtVoice(TurretFields& f) {
    if ((f.flagsLow & kTurretBeam) || f.hurtVoiceTimer != 0) return;
    f.hurtVoiceTimer = 0x78;
    const uint32_t r = rngStep();
    if (r & 1) return;
    vehicleVoice(uint16_t(hurtVoices[rngStep() & 7]));
}
// exe 0x504ad0 (any state but the bind): the hits
void turretHits(AnimatedActor& a, TurretFields& f) {
    TurretBlock& blk = *f.block;
    if (f.hurtVoiceTimer != 0) --f.hurtVoiceTimer;
    if (f.damageCoolDown != 0) --f.damageCoolDown;
    size_t boxIndex = kTurretBoxes; // the body / arm box hit, if any
    if (blk.boxes[kBodyBox].result & kHitByPlayer) boxIndex = kBodyBox;
    else if (blk.boxes[kArmBoxA].result & kHitByPlayer) boxIndex = kArmBoxA;
    else if (blk.boxes[kArmBoxB].result & kHitByPlayer) boxIndex = kArmBoxB;
    bool skipThresholds = boxIndex == kTurretBoxes;
    if (boxIndex != kTurretBoxes) {
        flashBone(a, 7);
        if (f.damageCoolDown == 0) {
            const int dead = hitPointsAdd(a, -int(blk.boxes[boxIndex].result & kHitDamageMask));
            f.damageCoolDown = 4;
            if (!(f.flagsLow & kTurretBeam) && f.hurtVoiceTimer == 0) {
                f.hurtVoiceTimer = 0x78;
                const uint32_t r = rngStep();
                if (!(r & 1)) vehicleVoice(uint16_t(hurtVoices[rngStep() & 7]));
            }
            if (dead) {
                f.flagsHigh |= kTurretDying;
                blk.dyingTimer = 0x3c;
                a.subState = uint8_t(TurretSub::End);
                a.step = 0;
                blk.endDelay = 0x96;
                if (!(f.flags & kTurretArmADead)) {
                    f.flags |= kTurretArmADead;
                    blk.armTimer[0] = 0x2d;
                    blk.partHp[kArmHpA] = -1;
                }
                if (!(f.flags & kTurretArmBDead)) {
                    f.flags |= kTurretArmBDead;
                    blk.armTimer[1] = 0x2d;
                    blk.partHp[kArmHpB] = -1;
                }
                if (f.beam != kNoActor) {
                    staticAt(f.beam).state = 2;
                    soundStopAllLoops(); // exe 0x5d5760
                    soundPlayAtPos(0x265, staticAt(f.beam).pos);
                }
                if (f.glow != kNoActor) effectActorAt(f.glow).step = 1; // the glow released
                skipThresholds = true;
            }
        }
    }
    if (!skipThresholds) {
        const int16_t q = t16(a.maxHitPoints / 4);
        if (!(f.flags & kTurretFire3of4) && a.hitPoints < q * 3) {
            f.flags |= kTurretFire3of4;
            spawnFireEmitter(a, 0x14, 7, 0xee0000, 0xfd520000u, 0xff7b0000u, 0);
        }
        if (!(f.flags & kTurretFireHalf) && a.hitPoints < q * 2) {
            f.flags |= kTurretFireHalf;
            spawnFireEmitter(a, 0x14, 7, 0x1d0000, 0xfd520000u, 0x1030000, 0);
        }
        if (!(f.flags & kTurretFire1of4) && a.hitPoints < q) {
            f.flags |= kTurretFire1of4;
            spawnFireEmitter(a, 0x14, 7, 0x1d0000, 0xfd520000u, 0, 0);
        }
    }
    // the arms (their own HPs): dead -> the arm's blast flag, its delay 0x2d, voice 0x269
    if (blk.boxes[kArmBoxA].result & kHitByPlayer) {
        partHit(a, blk.boxes[kArmBoxA], blk.partHp[kArmHpA], [&] {
            f.flagsHigh |= kTurretArmABlast;
            blk.armTimer[0] = 0x2d;
            vehicleVoice(0x269);
            f.blastMark = 3;
        });
        hurtVoice(f);
        flashBone(a, 1);
    }
    if (blk.boxes[kArmBoxB].result & kHitByPlayer) {
        partHit(a, blk.boxes[kArmBoxB], blk.partHp[kArmHpB], [&] {
            f.flagsHigh |= kTurretArmBBlast;
            blk.armTimer[1] = 0x2d;
            vehicleVoice(0x269);
            f.blastMark = 3;
        });
        hurtVoice(f);
        flashBone(a, 3);
    }
    // the three parts: dead -> a fire emitter (mode 6) into their emitter slot, their down flag
    struct Part {
        uint32_t x, y;
        uint8_t bit;
    };
    const Part parts[3] = {{0xc00000, 0xff480000u, kTurretPartADown},
                           {0, 0xff480000u, kTurretPartBDown},
                           {0xff400000u, 0xff400000u, kTurretPartCDown}};
    for (size_t k = 0; k < 3; ++k) {
        const Part& p = parts[k];
        if (!(blk.boxes[kPartBoxA + k].result & kHitByPlayer)) continue;
        partHit(a, blk.boxes[kPartBoxA + k], blk.partHp[kPartHpA + k], [&] {
            if (StaticActor* e = spawnFireEmitter(a, 0x14, 6, p.x, p.y, 0xf00000, 0)) blk.emitters(k) = e->at;
            f.flagsLow |= p.bit;
        });
        hurtVoice(f);
        flashBone(a, 6);
    }
    // the core: dead -> the beam, the core blast and the parts down, its timer 0x5a, the parts' HPs gone
    if (blk.boxes[kCoreBox].result & kHitByPlayer) {
        partHit(a, blk.boxes[kCoreBox], blk.partHp[kCoreHp], [&] {
            f.flags |= uint16_t(kTurretAllPartsDown | kTurretPartCDown | kTurretPartBDown | kTurretPartADown |
                                kTurretCoreBlast);
            f.coreTimer = 0x5a;
            blk.clearMiddlePartsWord();
            blk.partHp[kPartHpA + 2] = -1;
        });
        flashBone(a, 6);
        return;
    }
    const uint16_t flags = f.flags;
    constexpr uint8_t kPartsDown = kTurretPartADown | kTurretPartBDown | kTurretPartCDown;
    if (!(flags & kTurretAllPartsDown) && (uint8_t(flags) & kPartsDown) == kPartsDown) {
        f.coreTimer = 0x5a;
        f.flags = uint16_t(flags | kTurretAllPartsDown | kTurretCoreBlast);
    }
}
// exe 0x504290
void turretBind(AnimatedActor& a, TurretFields& f) {
    if (!bindActorModel(a, 0)) { // exe 0x5e0ba0
        freeActor(a);            // exe 0x5e1490
        return;
    }
    TurretDebug& debug = turretDebug();
    debug.counterA = 0;
    debug.counterB = 0;
    debug.shotCounter = 0;
    a.velocity.setXY(0, 0);
    a.flags |= uint8_t(kActorDrawn | kActorScenery);
    a.runFlags |= kActorWarpKept;
    a.anim = 0;
    a.prevAnim = 0xff;
    a.collisionBox = nullptr;
    a.interactionBox = nullptr;
    a.interaction = kInteractLift;
    a.velocity.setZSpare(0, 0);
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.velocity.y = 0;
    a.acceleration.y = 0x40;
    a.airborne = 0;
    a.onPlatform = 0;
    a.platform = nullptr;
    a.actorHit = 0;
    a.state = uint8_t(TurretState::Run);
    a.subState = uint8_t(TurretSub::Wait);
    a.step = 0;
    allocBoneBlocks(a, 1); // exe 0x5e16c0
    f.block.pointAt(boneBlockAddress(a, 2));
    TurretBlock& blk = *f.block;
    for (size_t k = 0; k < kTurretBoxes; ++k) blk.boxes[k].result = 0;
    const TurretStats row = statsOf(a);
    a.maxHitPoints = int16_t(hasAnimatedStats(a.type) ? uint32_t(row.hitPoints) << 6 : 0u);
    for (size_t k = 0; k < 8; ++k) blk.partHp[k] = int16_t(uint32_t(row.partHitPoints[k]) << 4);
    a.hitPoints = a.maxHitPoints;
    hudAddBossGauge(a, 0, 6, 0x2222aa); // exe 0x415d30
    const int32_t idx = enemyGroupFindFor(a, 0); // exe 0x5e6cd0(a, 0)
    f.objectState = uint8_t(idx);
    f.flagsLow = kTurretSignal;
    f.flagsHigh = 0;
    enemyGroupMark(int32_t(uint32_t(idx) & 0xff), 1); // exe 0x5e6d40 (the exe passes the index & 0xff)
    f.bombAim = kHalfTurn;
    f.cannonYaw[0] = kHalfTurn;
    f.cannonYaw[1] = kHalfTurn;
    f.beamAim = int16_t(3 * kQuarterTurn);
    f.beamYaw = int16_t(3 * kQuarterTurn);
    f.clearPitchesWord();
    blk.cannonTimer = 0;
    blk.burstCount = 0;
    f.damageCoolDown = 0;
    f.beam = kNoActor;
    f.glow = kNoActor;
    a.spin.setXY(0, 0);
    for (size_t k = 0; k < 5; ++k) blk.emitters(k) = kNoActor;
    for (size_t k = 0; k < 7; ++k) blk.watchers(k) = spawnHitWatcher(a).at;
}
// exe 0x5044e0: flag 0x581 -> Volleys, the volley delay 0x5a, three rounds
void waitSub(AnimatedActor& a, TurretFields& f) {
    TurretBlock& blk = *f.block;
    if (!storyFlag(kFlagFightStarted)) return;
    a.step = 0;
    ++a.subState;
    blk.volleyTimer = 0x5a;
    f.rounds = 3;
}
// exe 0x504520: volleys of 0x12 bombs (the slot cycling 0..2, 10 ticks apart, voice 0x28d once), then the wait for the
// bombs to land (no projectile live), then per object-state signal another round (voice 0x28f at round 2); after the
// last, Cannons (voice 0x28e)
void volleysSub(AnimatedActor& a, TurretFields& f) {
    f.flagsLow |= kTurretAimBombs;
    TurretBlock& blk = *f.block;
    switch (VolleyStep(a.step)) {
    case VolleyStep::Delay:
        if (blk.volleyTimer == 0) {
            a.step = uint8_t(VolleyStep::Fire);
            blk.volleyTimer = 0;
            f.volleySlot = 0;
            f.bombsLeft = 0x12;
            return;
        }
        --blk.volleyTimer;
        return;
    case VolleyStep::Fire:
        if (blk.volleyTimer == 0) {
            if (f.bombsLeft == 0) {
                a.step = uint8_t(VolleyStep::WaitLanded);
                return;
            }
            if (f.volleySlot >= 3) {
                f.volleySlot = 0;
                return;
            }
            if (!(f.voicesSaid & kVoiceVolleySaid)) {
                vehicleVoice(0x28d);
                f.voicesSaid |= kVoiceVolleySaid;
            }
            f.flagsHigh |= kTurretBombPending;
            blk.bombSlot = f.volleySlot;
            --f.bombsLeft;
            blk.volleyTimer = 10;
            ++f.volleySlot;
            return;
        }
        --blk.volleyTimer;
        return;
    case VolleyStep::WaitLanded:
        if (actorSystem().projectilesLive == 0) {
            f.flags = uint16_t(f.flags & ~uint16_t(kTurretSignal));
            a.step = uint8_t(VolleyStep::NextRound);
        }
        return;
    case VolleyStep::NextRound:
        if (f.rounds != 0) {
            if (enemyGroupMarks(int32_t(f.objectState), EnemyGroupMarks::Either) != 0) { // exe 0x5e6d60(index, 0)
                blk.volleyTimer = 0x78;
                a.step = uint8_t(VolleyStep::Delay);
                f.flagsLow |= kTurretSignal;
                const uint8_t n = uint8_t(f.rounds - 1);
                f.rounds = n;
                if (n == 2 && !(f.voicesSaid & kVoiceRoundSaid)) {
                    vehicleVoice(0x28f);
                    f.voicesSaid |= kVoiceRoundSaid;
                }
            }
        } else {
            ++a.subState;
            a.step = 0;
            vehicleVoice(0x28e);
        }
        return;
    default: return;
    }
}
// exe 0x5046b0: the cannons on (unless the beam is on; the beam once the 1/2 HP fire burns); while flag 0x582: rounds
// of 0xf bombs 0x14 ticks apart, 0x28 ticks between the rounds
void cannonsSub(AnimatedActor& a, TurretFields& f) {
    f.flagsLow |= kTurretAimBombs;
    TurretBlock& blk = *f.block;
    if (!(f.flags & kTurretBeam)) f.flags |= uint16_t(kTurretCannons);
    if (f.flags & kTurretFireHalf) f.flags |= uint16_t(kTurretBeam);
    if (!storyFlag(kFlagFrontBroken)) {
        a.step = 0;
        blk.volleyTimer = 0;
        return;
    }
    if (a.step == 0) {
        if (blk.volleyTimer == 0) {
            a.step = 1;
            blk.volleyTimer = 0;
            f.volleySlot = 0;
            f.bombsLeft = 0xf;
            return;
        }
        --blk.volleyTimer;
        return;
    }
    if (a.step != 1) return;
    if (blk.volleyTimer != 0) {
        --blk.volleyTimer;
        return;
    }
    if (f.bombsLeft == 0) {
        a.step = 0;
        blk.volleyTimer = 0x28;
        return;
    }
    if (f.volleySlot >= 3) {
        f.volleySlot = 0;
        return;
    }
    f.flagsHigh |= kTurretBombPending;
    blk.bombSlot = f.volleySlot;
    blk.volleyTimer = 0x14;
    ++f.volleySlot;
    --f.bombsLeft;
}
// exe 0x5047c0: the end delay, then the event 0x12 message 1 and the defended vehicle's HP carried in the stage block
void endSub(AnimatedActor& a, TurretFields& f) {
    const uint32_t target = defendedVehicleAddress();
    if (a.step != 0) return;
    TurretBlock& blk = *f.block;
    if (blk.endDelay != 0) {
        --blk.endDelay;
        return;
    }
    a.step = 1;
    eventStartAtActor(a, 0x12, 0, 1); // exe 0x543af0(actor, 0x12, 0, 1)
    stageBlock().part<VehicleChaseBlock>().carriedHp = int32_t(actorAt(target).hitPoints);
}
void turretSub(AnimatedActor& a, TurretFields& f, uint8_t s) {
    switch (TurretSub(s)) {
    case TurretSub::Wait: waitSub(a, f); return;
    case TurretSub::Volleys: volleysSub(a, f); return;
    case TurretSub::Cannons: cannonsSub(a, f); return;
    case TurretSub::End: endSub(a, f); return;
    default: notPortedAt(kSt0ePort, "0x503bd0 sub-state", s);
    }
}
// the decoded frame's angles a turret lane turns (its parts' joints)
void addAngle(Lane16 lane, int v) { lane = int16_t(v + lane); }
// exe 0x503bd0
void bossTurret(AnimatedActor& a) {
    TurretFields& f = classState<TurretFields>(a);
    const TurretStats row = statsOf(a);
    a.prevAnim = a.anim;
    a.tint = kTintNeutral;
    a.savePreviousPose();
    a.savePreviousContact();
    if (a.state != uint8_t(TurretState::Bind)) turretHits(a, f);
    switch (a.state) {
    case uint8_t(TurretState::Bind):
        turretBind(a, f);
        if (!(a.flags & kActorLive)) return; // freed by the bind (the exe goes on with the freed actor's tail)
        break;
    case uint8_t(TurretState::Run): // exe 0x5044a0
        turretSub(a, f, a.subState);
        if (f.flagsLow & kTurretSignal) enemyGroupMark(int32_t(f.objectState), 1);
        break;
    case uint8_t(TurretState::Free): freeActor(a); break; // exe 0x468fa0
    case 3: waitSub(a, f); break;                         // the table repeats the sub-states as states 3..6
    case 4: volleysSub(a, f); break;
    case 5: cannonsSub(a, f); break;
    case 6: endSub(a, f); break;
    default: notPortedAt(kSt0ePort, "0x503bd0 state", a.state);
    }
    if (a.anim != a.prevAnim) animSet(a, a.anim, 0); // exe 0x5e1c50
    else animStep(a);                                // exe 0x5e1ea0
    DecodedFrame& frame = decodedFrame();
    addAngle(frame.entries[7].y, f.bombAim + kHalfTurn);
    addAngle(frame.entries[6].y, f.beamYaw + kQuarterTurn);
    addAngle(frame.entries[2].y, f.cannonYaw[0] + kHalfTurn);
    addAngle(frame.entries[4].y, f.cannonYaw[1] + kHalfTurn);
    addAngle(frame.entries[3].x, f.cannonPitch[0]);
    addAngle(frame.entries[5].x, f.cannonPitch[1]);
    animPose(a); // exe 0x5e2d90
    TurretBlock& blk = *f.block;
    actorCollisionSubmitSolid(a, SolidType::StandOn, 0, 0); // exe 0x414050
    if (f.flagsLow & kTurretAimBombs) aimBombs(a, f);
    if (f.flagsHigh & kTurretBombPending) {
        lobBomb(a, f, blk.bombSlot);
        f.flags = uint16_t(f.flags & ~uint16_t(kTurretBombPending << 8));
    }
    if (f.flagsLow & kTurretCannons) cannons(a, f);
    if (f.flagsLow & kTurretBeam) beam(a, f);
    if (f.flagsLow & kTurretCoreBlast) coreBlast(a, f);
    if (f.flagsHigh & kTurretArmABlast) armBlast(a, f, 1, 0);
    if (f.flagsHigh & kTurretArmBBlast) armBlast(a, f, 3, 1);
    if (f.flagsHigh & kTurretDying) dying(a, f);
    for (size_t k = 0; k < kTurretBoxes; ++k) {
        HitBoxHead& box = blk.boxes[k];
        box.result = 0;
        box.previousX = a.prevPos.x.whole;
        box.previousZ = a.prevPos.z.whole;
    }
    const uint8_t own = a.slot;
    const uint32_t attack = row.attack | kHarmlessSolid;
    if (!(f.flagsHigh & kTurretDying)) {
        HitBoxHead& body = blk.boxes[kBodyBox];
        const uint32_t cap = ownerWord(own, row, hitGroup(kHitGroupBodies) | kHitNoLockOn | hitCapsule(1));
        BonePoint p = bonePointAt(a, 7, 0xee, -0x2ae, -0x49), q = bonePointAt(a, 7, 0xee, -0x2ae, 0x10b);
        hitListSubmitCapsule(p.v, 0x1d, q.v, 0x1d, body, cap, attack); // exe 0x5e4190
        p = bonePointAt(a, 7, 0x1d, -0x2ae, 0x103);
        q = bonePointAt(a, 7, -0xe8, -0x2ae, 0x103);
        hitListSubmitCapsule(p.v, 0x1d, q.v, 0x1d, body, cap, attack);
        submit(bonePointAt(a, 7, 0, -0x2ae, 0), body, ownerWord(own, row, hitGroup(kHitGroupBodies) | hitRadius(0x74)),
               attack);
    }
    auto part = [&](size_t hp, uint32_t bone, int x, int y, int z, size_t box, size_t watcher, uint32_t shape) {
        if (blk.partHp[hp] < 0) return;
        submit(bonePointAt(a, bone, x, y, z), blk.boxes[box],
               ownerWord(controllerActorAt(blk.watchers(watcher)).slot, row, shape), attack);
    };
    const uint32_t arm = hitGroup(kHitGroupBodies) | hitRadius(0x88);
    const uint32_t gun = hitGroup(kHitGroupBodies) | hitRadius(0x54);
    part(kArmHpA, 1, 0, -0x5c, 0, kArmBoxA, 0, arm);
    part(kArmHpB, 3, 0, -0x5c, 0, kArmBoxB, 1, arm);
    part(kPartHpA, 6, 0xc0, -0xb8, 0xb0, kPartBoxA, 2, gun);
    part(kPartHpA + 1, 6, 0, -0xb8, 0xb0, kPartBoxA + 1, 3, gun);
    part(kPartHpA + 2, 6, -0xc0, -0xb8, 0xb0, kPartBoxA + 2, 4, gun);
    if (blk.partHp[kCoreHp] >= 0) {
        const uint32_t core = hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0xf0);
        submit(bonePointAt(a, 6, 0x71, -0x70, -0x70), blk.boxes[kCoreBox], ownerWord(own, row, core), attack);
        submit(bonePointAt(a, 6, -0x71, -0x70, -0x70), blk.boxes[kCoreBox], ownerWord(own, row, core), attack);
    }
    submit(bonePointAt(a, 5, 0xe0, 0x13, 0), blk.boxes[kBeamBox],
           ownerWord(own, row, hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x8c)), attack);
}

enum class BombState : uint8_t { Init = 0, Fly = 1, Blast = kBombBlast };
constexpr uint8_t kBombSpark = 2; // the blast's variant: a spark (else the flash of that variant)
const DataTable<CollisionBoxRecord> heldBombBox{TableId::St0eTurretHeldBombBox}, bombBox{TableId::St0eTurretBombBox};
constexpr uint32_t kFlagVehicleDead = 0x587;

// exe 0x5ccb60: its billboard (0x2c, its grey, its size, the u cell stepping by 0x20 every 5 ticks up to 0x80)
void bombSprite(ProjectileActor& a, BombFields& f) {
    const uint32_t c = f.grey;
    const int32_t u = f.frameU;
    dc::emit({0, 0x2c000000u | c << 16 | c << 8 | c,
              uint32_t(uint16_t(a.pos.x.whole)) | uint32_t(uint16_t(a.pos.y.whole)) << 16, uint32_t(u) | 0x7c936000u,
              uint16_t(a.pos.z.whole), uint32_t(u + 0x1f) | 0x2f6000u, uint32_t(int32_t(f.size)),
              uint32_t(u) | 0x7f00u, 0, uint32_t(u + 0x1f) | 0x7f00u},
             a.at, false);
    if (f.frameU >= 0x81) {
        f.frameU = 0;
        return;
    }
    if (f.frameTimer == 0) {
        f.frameU = int16_t(f.frameU + 0x20);
        f.frameTimer = 4;
        return;
    }
    --f.frameTimer;
}
// exe 0x5cc900: the flight -- it moves unless held, the shield stops it, a hit / a wall / an actor contact ends it (the
// spark / the flash); an actor contact hurts the defended vehicle by its damage (unless flag 0x587); its hit entry
// while it moves
void bombFly(const PlayerEnv& env, ProjectileActor& a, BombFields& f) {
    const uint32_t target = defendedVehicleAddress();
    if (f.held == 0 || f.flying != 0) {
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        a.velocity.y = int16_t(a.velocity.y + a.acceleration.y);
    }
    bombSprite(a, f);
    if (playerShieldBlocks(a, 0x60)) {
        if (a.velocity.y != 0) {
            a.state = uint8_t(BombState::Blast);
            a.variant = kBombSpark;
        }
        return;
    }
    if ((f.box.result & (kHitAttackByPlayer | kHitAttackBump)) && a.velocity.y != 0) {
        a.state = uint8_t(BombState::Blast);
        a.variant = kBombSpark;
    }
    if (int32_t(actorTestMove(env, a)) <= -1 && a.velocity.y != 0) { // exe 0x40f570
        a.state = uint8_t(BombState::Blast);
        a.variant = 0;
    }
    if (a.actorHit != 0) {
        if (a.velocity.y != 0) {
            a.state = uint8_t(BombState::Blast);
            a.variant = kBombSpark;
        }
        if (!storyFlag(kFlagVehicleDead)) hitPointsAdd(actorAt(target), -int(f.damage));
    }
    f.box.result = 0;
    f.box.previousX = a.prevPos.x.whole;
    f.box.previousZ = a.prevPos.z.whole;
    if (a.velocity.y != 0) {
        const int16_t p[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
        const uint32_t owner =
            hitOwner(a.slot) | hitGroup(kHitGroupLiftable) | kHitNoLockOn | hitRadius(f.held ? 0x30 : 0x60);
        hitListSubmit(p, f.box, owner, uint32_t(int32_t(f.damage)) | (0x80000000u >> kHitGroupBodies) |
                                           kHitAttackStrong);
    }
}
// exe 0x5cc850
void turretBomb(const PlayerEnv& env, ProjectileActor& a) {
    BombFields& f = classState<BombFields>(a);
    switch (BombState(a.state)) {
    case BombState::Init: // exe 0x5cc880
        f.box.result = 0;
        f.unusedC8 = 10;
        f.blastDelay = 0;
        f.unusedD6 = 0;
        f.grey = 0x80;
        if (f.held != 0) {
            f.size = 0x40;
            a.collisionBox.pointAt(heldBombBox.stateAddress());
        } else {
            f.size = 0x80;
            a.collisionBox.pointAt(bombBox.stateAddress());
        }
        f.frameU = 0;
        a.state = uint8_t(BombState::Fly);
        f.flying = 0;
        a.step = 0;
        a.subState = 0;
        a.runFlags |= kActorWarpKept;
        bombFly(env, a, f);
        break;
    case BombState::Fly: bombFly(env, a, f); break;
    case BombState::Blast: { // exe 0x5cca80: the explosion
        if (f.blastDelay == 0) soundPlayAtPos(0x89, a.pos);
        if (a.variant & kBombSpark) {
            if (const uint32_t at = allocActorA0(0)) { // exe 0x5e13b0
                EffectActor& e = effectActorAt(at);
                e.flags = kActorLiveDrawn;
                e.type = typeByte(GlobalEffectType::HitSpark);
                e.variant = 0;
                e.pos.x = a.pos.x.whole;
                e.pos.y = a.pos.y.whole;
                e.pos.z = a.pos.z.whole;
                HitSparkFields& s = classState<HitSparkFields>(e);
                s.size = 0x90;
                s.frameTicks = 1;
            }
        } else if (const uint32_t at = allocActorA0(kActorStageClass)) { // exe 0x5e13a0
            EffectActor& e = effectActorAt(at);
            e.flags = kActorLiveDrawn;
            e.type = kFlashEffectType;
            e.loopFlags |= kActorLateLoop;
            e.variant = a.variant;
            e.pos.x = a.pos.x.whole;
            e.pos.y = a.pos.y.whole;
            e.pos.z = a.pos.z.whole;
            classState<HitSparkFields>(e).size = 0xc0;
        }
        cameraShakeAt(0x2000, 0x300, 0x100, 0x1c00, a.pos); // exe 0x40eab0
        padVibrateAt(0x201, 0x208b0, 0x100, 0x1c00, a.pos); // exe 0x55cd80
        freeActor(a);                                       // exe 0x5e1500
        break;
    }
    default: notPortedAt(kSt0ePort, "0x5cc850 state", a.state);
    }
    if (f.held != 0 && f.flying == 0) return;
    actorCollisionSubmitMover(a); // exe 0x4140b0
}

// ==== the cannon shot (exe 0x5d0430, pool 0x40 stage type 0x2e) =======================================================
struct CannonShotFields {
    explicit CannonShotFields(const ProjectileActor& actor) : CannonShotFields(const_cast<ProjectileActor&>(actor)) {}
    explicit CannonShotFields(ProjectileActor& actor) : life(actor.params, 0), damage(actor.params, 2), box(actor.hitBox) {}
    BytesField<int16_t, 4> life;
    BytesField<uint16_t, 4> damage;
    HitBoxHead& box;
};
enum class ShotState : uint8_t { Init = 0, Fly = 1 };
enum class ShotOutcome : int { Flying = 0, Hit = 1, Spent = 2, Shielded = 3, Contact = 4 };
constexpr uint8_t kShotNoMover = 0x80; // the variant's bit: not queued as a mover
const DataTable<CollisionBoxRecord> cannonShotBox{TableId::St0eTurretCannonShotBox};
// the stray texel words the exe stores 0x28 bytes past the record (its words 7 / 9 of the next one): +0x44 / +0x4c
constexpr uint32_t kStrayFirst = 0x44, kStraySecond = 0x4c;
// exe 0x5d0710(shot, v): a hit spark (global effect 0, its size ((variant & 3) + 8) * 0x10), the shot freed
void shotSpark(ProjectileActor& a, uint8_t v) {
    if (const uint32_t at = allocActorA0(0)) { // exe 0x5e13b0
        EffectActor& e = effectActorAt(at);
        e.flags = kActorLiveDrawn;
        e.type = typeByte(GlobalEffectType::HitSpark);
        e.variant = v;
        e.pos.x = a.pos.x.whole;
        e.pos.y = a.pos.y.whole;
        e.pos.z = a.pos.z.whole;
        HitSparkFields& s = classState<HitSparkFields>(e);
        s.frameTicks = 1;
        s.size = uint16_t(((a.variant & 3) + 8) * 0x10);
    }
    freeActor(a); // exe 0x5e1500
}
// exe 0x5c55f0(shot, v): a flash (stage effect 7, late, variant v, its size ((variant & 3) + 2) * 0x40), the shot freed
void shotPop(ProjectileActor& a, uint8_t v) {
    if (const uint32_t at = allocActorA0(kActorStageClass)) { // exe 0x5e13a0
        EffectActor& e = effectActorAt(at);
        e.flags = kActorLiveDrawn;
        e.type = kFlashEffectType;
        e.loopFlags |= kActorLateLoop;
        e.variant = v;
        e.pos.x = a.pos.x.whole;
        e.pos.y = a.pos.y.whole;
        e.pos.z = a.pos.z.whole;
        classState<HitSparkFields>(e).size = uint16_t(((a.variant & 3) + 2) * 0x40);
    }
    freeActor(a); // exe 0x5e1500
}
// exe 0x5c6fc0: the shot's glow (a 0xf0 disc, its radius (variant & 3) * 0x20 + 0x30)
void shotGlow(ProjectileActor& a) {
    dc::emit({0x4000u, 0xf0008080u, uint32_t(uint16_t(a.pos.x.whole)) | uint32_t(uint16_t(a.pos.y.whole)) << 16,
              0x22000000u, uint32_t(uint16_t(a.pos.z.whole)) | 0x20000u, (a.variant & 3u) * 0x200000u + 0x300000u},
             a.at);
}
// exe 0x5d04d0: the init -- the speed -0x1000 along its rotation, the previous position 0x2000 behind, the box, sound
// 0xb1
ShotOutcome shotInit(ProjectileActor& a, CannonShotFields& f) {
    a.runFlags |= kActorWarpKept;
    a.setDrawn(false);
    f.box.result = 0;
    a.velocity.setXY(0, 0);
    a.velocity.z = -0x1000;
    a.prevPos = a.pos;
    moveAlongRotation(a.prevPos, a.rotation, 0, 0, 0x2000); // exe 0x5e3810
    a.collisionBox.pointAt(cannonShotBox.stateAddress());
    a.actorHit = 0;
    soundPlayAtPos(0xb1, a.pos);
    ++a.state;
    shotGlow(a);
    return ShotOutcome::Flying;
}
// exe 0x5d0560: the flight -- the outcomes; else the billboard (0x2c; the exe also writes 0x7fc0 / 0x7fdf 0x28 bytes
// past the record's words 7 / 9) and the hit entry
ShotOutcome shotFly(ProjectileActor& a, CannonShotFields& f) {
    a.savePreviousPose();
    if (a.actorHit != 0) return ShotOutcome::Contact;
    if (f.box.result & kHitAttackBump) return ShotOutcome::Hit;
    --f.life;
    if (f.life == 0) return ShotOutcome::Spent;
    moveAlongRotation(a.pos, a.rotation, 0, 0, a.velocity.z); // exe 0x5e3810
    const uint32_t rad = (a.variant & 3u) * 8 + 0x20;
    if (playerShieldBlocks(a, rad)) return ShotOutcome::Shielded;
    dc::DrawCommandState& dcs = dc::drawCommands();
    if (dc::DrawCommandState::holds(dcs.cursor + kStrayFirst, 3)) {
        dcs.word(dcs.cursor + kStrayFirst) = 0x7fc0;
        dcs.word(dcs.cursor + kStraySecond) = 0x7fdf;
    }
    dc::emit({0, 0x2c808080u, uint32_t(uint16_t(a.pos.x.whole)) | uint32_t(int32_t(a.pos.y.whole)) << 16,
              0x7cd060c0u, uint32_t(int32_t(a.pos.z.whole)), 0x2f60dfu,
              ((a.variant & 3u) * 0x10 + 0x18) | ((uint32_t(uint16_t(f.life)) << 24) & 0x0fff0000u), 0x7fc0u, 0,
              0x7fdfu},
             a.at, false);
    const int16_t p[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
    f.box.result = 0;
    f.box.previousX = a.prevPos.x.whole;
    f.box.previousZ = a.prevPos.z.whole;
    hitListSubmit(p, f.box, hitOwner(a.slot) | ((rad | 0x1800000u) >> 2), uint32_t(f.damage) | kHitAttackStrong);
    a.airborne = kAirborneFalling;
    a.onPlatform = 0;
    a.platform = nullptr;
    return ShotOutcome::Flying;
}
// exe 0x5d0430
void turretShot(const PlayerEnv& env, ProjectileActor& a) {
    CannonShotFields f(a);
    ShotOutcome r;
    switch (ShotState(a.state)) {
    case ShotState::Init: r = shotInit(a, f); break;
    case ShotState::Fly: r = shotFly(a, f); break;
    default: notPortedAt(kSt0ePort, "0x5d0430 state", a.state);
    }
    switch (r) {
    case ShotOutcome::Flying: {
        const uint32_t t = actorTestMove(env, a); // exe 0x40f570
        if (int32_t(t) <= -1) {
            queueBreakableWallHit(t, int(uint32_t(f.damage) << 4)); // exe 0x58d4e0
            actorPushOut(env, a);                                   // exe 0x410960
            shotPop(a, 0);
            return;
        }
        if (a.variant & kShotNoMover) return;
        actorCollisionSubmitMover(a); // exe 0x4140b0
        return;
    }
    case ShotOutcome::Hit:
    case ShotOutcome::Contact: shotSpark(a, 0); return;
    case ShotOutcome::Spent: shotPop(a, 1); return;
    case ShotOutcome::Shielded:
        freeActor(a); // exe 0x5e1500
        shotSpark(a, 0);
        return;
    default: return;
    }
}

enum class GlowState : uint8_t { Init = 0, Shine = 1, Free = 2 };
enum class GlowSub : uint8_t { Grow = 0, Pulse = 1, Shrink = 2, Done = 3 };
constexpr uint16_t kRestartGlow = 0x2000; // the second pad's pressed bit that restarts a done glow (a debug test)

// exe 0x43a9c0: the billboard (0x2c, its colour word, its size)
void glowSprite(EffectActor& a, GlowFields& f) {
    dc::emit({0, f.colour, a.positionWord(0), 0x7dd12020u, uint16_t(a.pos.z), 0x2f203fu,
              uint32_t(int32_t(f.size)), 0x3f20u, 0, 0x3f3fu},
             a.at, false);
}
// the pulse of the variant 1's Pulse: the counter + 1 indexes the 64-step sine: the size target + (target * t) >> 14,
// the grey t >> 5
void glowPulse(GlowFields& f) {
    const uint8_t n = uint8_t(f.pulse + 1);
    f.pulse = n;
    const int32_t t = trig64Tables().sine[n & 0x1fu];
    const uint32_t g = uint32_t(t >> 5);
    f.size = t16((int32_t(f.targetSize) * t >> 14) + f.targetSize);
    f.colour = ((g << 8 | g) << 8 | g) + 0x2e7f7f7fu;
}
// exe 0x43a780 (its states 0x88d790)
void glowVariant0(EffectActor& a, GlowFields& f) {
    switch (GlowState(a.state)) {
    case GlowState::Init: // exe 0x43a7a0
        a.state = uint8_t(GlowState::Shine);
        f.size = 0x20;
        f.colour = 0x2e808080u;
        return;
    case GlowState::Shine: { // exe 0x43a7c0: the size / the grey from the 64-step sine by the frame counter
        const int32_t t = trig64Tables().sine[(gameState().frame & 0xfu) * 2];
        f.size = int16_t((t >> 8) + 0x20);
        const uint32_t c = uint32_t(t) & 0x1fc0;
        f.colour = (((c << 8 | c) << 3) | uint32_t(t >> 5)) + 0x2e404040u;
        glowSprite(a, f);
        return;
    }
    default: notPortedAt(kSt0ePort, "0x43a780 state", a.state);
    }
}
// exe 0x43a810 (its states 0x88d79c)
void glowVariant1(EffectActor& a, GlowFields& f) {
    switch (GlowState(a.state)) {
    case GlowState::Init: // exe 0x43a830
        a.state = uint8_t(GlowState::Shine);
        a.subState = uint8_t(GlowSub::Grow);
        f.size = 0;
        f.colour = 0x2e808080u;
        f.growth = 1;
        return;
    case GlowState::Shine: // exe 0x43a850: Grow by the growth to the target; Pulse 16 ticks, then while released to the
                           // end of a 32-tick period; Shrink 8 ticks by target >> 3; Done (the debug pad restarts it)
        switch (GlowSub(a.subState)) {
        case GlowSub::Grow:
            if (f.targetSize <= f.size) {
                f.size = f.targetSize;
                ++a.subState;
            } else {
                f.size = int16_t(f.growth + f.size);
            }
            break;
        case GlowSub::Pulse:
            if (int8_t(f.pulse) <= 0xf) {
                glowPulse(f);
            } else if (f.released != 0) {
                glowPulse(f);
                if ((f.pulse & 0x1f) == 0x1f) {
                    f.pulse = 0;
                    ++a.subState;
                }
            }
            break;
        case GlowSub::Shrink: {
            f.size = int16_t(f.size - t16(f.targetSize >> 3));
            const uint8_t n = uint8_t(f.pulse + 1);
            f.pulse = n;
            if (n == 8) {
                f.size = 0;
                a.subState = uint8_t(GlowSub::Done);
            }
            break;
        }
        case GlowSub::Done:
            if (pad1().pressed & kRestartGlow) a.state = uint8_t(GlowState::Init);
            break;
        default: break;
        }
        glowSprite(a, f);
        return;
    case GlowState::Free: freeActor(a); return; // exe 0x4358c0 -> 0x5e1580
    default: notPortedAt(kSt0ePort, "0x43a810 state", a.state);
    }
}
// exe 0x43a760: the variant's routine (the exe's table 0x88d788)
void beamGlow(EffectActor& a) {
    GlowFields& f = classState<GlowFields>(a);
    switch (a.variant) {
    case 0: glowVariant0(a, f); return;
    case 1: glowVariant1(a, f); return;
    default: notPortedAt(kSt0ePort, "0x43a760 variant", a.variant);
    }
}

} // namespace

void bossTurretUpdate(const PlayerEnv&, uint32_t actor) { bossTurret(animatedActorAt(actor)); }
void turretBombUpdate(const PlayerEnv& env, uint32_t actor) { turretBomb(env, projectileActorAt(actor)); }
void turretShotUpdate(const PlayerEnv& env, uint32_t actor) { turretShot(env, projectileActorAt(actor)); }
void beamGlowUpdate(const PlayerEnv&, uint32_t actor) { beamGlow(effectActorAt(actor)); }

} // namespace od::port
