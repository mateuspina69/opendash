// The guardian of the Manda ruin (ST14 sub 4, story 4) and its cast (see manda_boss.hpp): pool 0x20, the ST14 stage
// table type 0x2b (0x8e0710 -> 0x48fb90), spawned by the fight's controller of script 0x1a; bit-exact against
// tests/data/story_ST14_boss_*.bin / story_ST14_kill_*.bin. The guardian sits on one of four perches (exe table
// 0x892a08) and hops between them toward the quadrant of the room the player stands in, turning in steps of 0x200;
// its attacks are a debris spray (pool 0x40 type 0x12), a rain of falling shots (pool 0x40 type 0xb), a ground shock
// (pool 0x40 type 3 variant 10) and a beam (the static type 0x2c), aimed at the player or at the floating target the
// controller keeps. Its spawn byte 0 is the guardian (its death: flag 0x5b5, the room handler starts script 0xe); the
// others run the dying sub-state 10. Left out (render output): the shadows (exe 0x5f20f0) and the beam's projected
// corners (exe 0x5ab320's draw).
#include "content/manda/manda_boss.hpp"
#include "content/late_game/st43_roller_state.hpp"
#include "content/manda/manda_boss_state.hpp"

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
#include "game/distances.hpp"
#include "game/draw_commands.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/glow_trails.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/matrix.hpp"
#include "game/pickups.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/skid_sparks.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/vectors.hpp"

namespace od::port {

using namespace manda_boss_state;

namespace {

// the 64-step cosine table read at the exe's index, unmasked (an index past the table's 64 entries, which the exe's
// arithmetic allows, is a StateIndexError: the exe would read whatever follows the table)
int16_t cosineAt(int32_t index) { return trig64Tables().cosine[size_t(index)]; }
// the actor a pointer field holds (the exe reads through it unchecked: a null one is the port gap)
template <class V> V& pointee(const ActorRef<V>& p, const char* what) {
    if (!p) notPorted(kStoryPort, what);
    return *p;
}

constexpr uint8_t kFightFloaterAlive = 0x01, kFightGuardianHalf = 0x02, kFightFloaterInReach = 0x04;
constexpr uint8_t kFightVariantDied = 0x08;
constexpr uint8_t kFightRoller0 = 0x10;          // 0x10 << k: roller k is up
constexpr uint8_t kFightAlive = 0xf1;           // the floater and the four rollers
enum class FightState : uint8_t { SetUp = 0, Run = 1, End = 2 }; // exe 0x423190 / 0x4232f0 / 0x423370

// +0x160
constexpr uint32_t kGuardianOnGround = 0x0001;    // its height snapped to the ground every tick
constexpr uint32_t kGuardianFastHop = 0x0008;     // the spinning hop's speed 0x400
constexpr uint32_t kGuardianLanded = 0x0010;      // a hop ended (the player's quadrant changed since: kHopPending)
constexpr uint32_t kGuardianHopPending = 0x0020;  // a hop asked for (the debris first)
constexpr uint32_t kGuardianBodyArmed = 0x0040;   // its body takes damage, its mouth's entry is solid
constexpr uint32_t kGuardianStomping = 0x0080;    // ... its body's entry reports into the main box
constexpr uint32_t kGuardianVolley = 0x0100;      // the second attack is up (the falling shots / the beam's repeats)
constexpr uint32_t kGuardianBeamOut = 0x0200;
constexpr uint32_t kGuardianBeamLow = 0x0400;     // the beam's sway pattern 1 (pitch offset 0x80)
constexpr uint32_t kGuardianTargetAsked = 0x0800; // the controller asks for the beam at the floater
constexpr uint32_t kGuardianBeamAtTarget = 0x1000;
constexpr uint32_t kGuardianHit = 0x2000;
constexpr uint32_t kGuardianDead = 0x4000;
constexpr uint32_t kGuardianHitClears = 0xffffe7cf; // the hit's flags cleared: kHit, the target's, the body's
enum class GuardianState : uint8_t { SetUp = 0, Fight = 1, Gone = 2 }; // exe 0x48fbc0 / 0x48fcd0 / 0x490050
enum class GuardianSub : uint8_t {
    Choose = 0,    // exe 0x490080
    TurnRight = 1, // exe 0x4903d0
    TurnLeft = 2,  // exe 0x490650
    Hop = 3,       // exe 0x4908d0
    SpinHop = 4,   // exe 0x490aa0
    Debris = 5,    // exe 0x490dd0
    Volley = 6,    // exe 0x490f20
    Beam = 7,      // exe 0x491030
    TargetBeam = 8, // exe 0x4911c0
    Hit = 9,       // exe 0x4913d0
    Dying = 10,    // exe 0x491520
};

// A perch (exe data 0x892a08: a 16.16 point)
struct Perch : DataRecord {
    std::array<int32_t, 3> point{};
    static Perch load(const TableReader& r) { return {{}, {r.s32(0), r.s32(4), r.s32(8)}}; }
    int16_t pointY() const { return int16_t(uint32_t(point[1]) >> 16); } // the point's y's integer part
};
const DataTable<RecordTable<Perch, 4, 0xc>> perches{TableId::MandaBossPerches};
// the actor put at the perch (the exe's three dword copies)
void placeAt(Fixed16Vec3& pos, const Perch& p) {
    pos.x = p.point[0];
    pos.y = p.point[1];
    pos.z = p.point[2];
}
// the launch speed from the actor's position to the perch
int launchToPerch(const Fixed16Vec3& from, const Perch& p, int speed, int gravity) {
    const int32_t f[3] = {from.x, from.y, from.z};
    return launchSpeed(f, p.point.data(), speed, gravity, 0xffffffffu);
}
const DataTable<ScalarTable<int16_t, 16>> perchYaws{TableId::MandaBossPerchYaws}; // the yaw from perch p toward quadrant q: [q + p * 4]
constexpr int8_t kNoQuadrant = -1;
constexpr uint32_t kFlagGuardianBeaten = 0x5b5;
constexpr uint16_t kSoundTurn = 0x135, kSoundDebris = 0x136, kSoundBeam = 0x137, kSoundVolley = 0x138;
constexpr uint16_t kSoundTakeOff = 0x139, kSoundLand = 0x13a;
constexpr int16_t kTurnStep = 0x200;

GuardianFightBlock& workOf(GuardianFields& f) { return *f.work; }
const GuardianFightBlock& workOf(const GuardianFields& f) { return *f.work; }
// a guardian's sub-state (its step cleared: the exe's byte stores at +9 / +0xa)
void toSub(AnimatedActor& a, GuardianSub s) {
    a.subState = uint8_t(s);
    a.step = 0;
}
// the animation asked for, from its first key
void play(AnimatedActor& a, GuardianFields& f, uint8_t anim) {
    a.anim = anim;
    f.startKey = 0;
}
// the shared "hit" branch: its flags cleared, sub-state 9
void toHit(AnimatedActor& a, GuardianFields& f) {
    f.flags = f.flags & kGuardianHitClears;
    toSub(a, GuardianSub::Hit);
}
void toChoose(AnimatedActor& a, GuardianFields& f, int16_t timer) {
    play(a, f, 0);
    toSub(a, GuardianSub::Choose);
    f.timer = timer;
}
// the point (x, y, z) on its bone 0 (exe 0x5e1970 on the stack offset)
void mouthPoint(const AnimatedActor& a, int16_t x, int16_t y, int16_t z, int16_t out[3]) {
    const int16_t v[3] = {x, y, z};
    bonePointOffset(a, 0, v, out);
}

// ---- its spawns ----
// exe 0x491c20(actor, variant, yaw, v): a skid spark (the global effect type 3) at Ry(yaw) * (v << 4), on the ground
void dust(const PlayerEnv& env, const AnimatedActor& a, uint8_t variant, uint16_t yaw, int16_t x, int16_t y, int16_t z) {
    const uint32_t address = allocActorA0(0); // exe 0x5e13b0
    if (!address) return;
    const int16_t rot[3] = {0, int16_t(yaw), 0};
    int32_t o[3];
    rotateVector(rot, int(x) << 4, int(y) << 4, int(z) << 4, o); // exe 0x5e3970
    EffectActor& e = effectActorAt(address);
    classState<SkidSparkFields>(e).yaw = yaw;
    e.flags = kActorLiveDrawn;
    e.type = typeByte(GlobalEffectType::SkidSparks);
    e.variant = variant;
    e.loopFlags |= kActorSweepGroup2;
    e.pos.x = int16_t(uint32_t(int16_t(uint32_t(o[0]) >> 16)) + uint16_t(a.pos.x.whole));
    e.pos.y = int16_t(actorGround(env, a)); // exe 0x40f190
    e.pos.z = int16_t(uint32_t(int16_t(uint32_t(o[2]) >> 16)) + uint16_t(a.pos.z.whole));
}
// exe 0x491b70: the dust under its four feet
void dustFeet(const PlayerEnv& env, const AnimatedActor& a) {
    const uint16_t y = uint16_t(a.rotation.y);
    dust(env, a, 0x1b, y, -0x80, 0, -0x80);
    dust(env, a, 0x1b, y, 0x80, 0, -0x80);
    dust(env, a, 0x1b, y, -0xc0, 0, 0x80);
    dust(env, a, 0x1b, y, 0xc0, 0, 0x80);
}
// the ground shock's spawn parameters (pool 0x40 stage type 3; its class is not ported: the names are the spawner's)
struct ShockParams {
    explicit ShockParams(const ProjectileActor& actor) : ShockParams(const_cast<ProjectileActor&>(actor)) {}
    explicit ShockParams(ProjectileActor& actor) : rings(actor.params[0]), phase(actor.params[1]) {}
    uint8_t& rings;
    uint8_t& phase;
};
// exe 0x4916b0: the ground shock (pool 0x40 type 3 variant 10) under it
void shock(const PlayerEnv& env, const AnimatedActor& a) {
    const uint32_t address = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!address) return;
    ProjectileActor& e = projectileActorAt(address);
    ShockParams params(e);
    e.flags = kActorLive;
    e.type = 3;
    e.variant = 10;
    e.loopFlags |= kActorSweepGroup2;
    params.rings = 1;
    params.phase = 0;
    e.pos.x.whole = a.pos.x.whole;
    e.pos.y.whole = int16_t(actorGround(env, a));
    e.pos.z.whole = a.pos.z.whole;
}
// exe 0x491700: a falling shot (pool 0x40 type 0xb) 0x180 above it, a random yaw around the player's direction
void fallingShot(const AnimatedActor& a, const GuardianFields& f) {
    const uint32_t address = allocActor40(kActorStageClass);
    if (!address) return;
    ProjectileActor& e = projectileActorAt(address);
    e.flags = kActorLive;
    e.type = 0xb;
    e.loopFlags |= kActorSweepGroup2;
    e.variant = 0;
    e.pos.x.whole = a.pos.x.whole;
    e.pos.y.whole = int16_t(uint32_t(uint16_t(a.pos.y.whole)) - 0x180u);
    e.pos.z.whole = a.pos.z.whole;
    e.rotation.x = 0x100;
    const uint32_t r = rngStep();
    e.rotation.z = 0;
    e.setParamsWord(0x20);
    e.rotation.y = int16_t((r & 0x1ff) + uint32_t(uint16_t(f.playerYaw)) - 0x100u);
}
// exe 0x491790: a debris piece (pool 0x40 type 0x12) from the mouth (bone 0 + (0, 0x20, -0x80)), three RNG steps
void debris(const AnimatedActor& a) {
    const uint32_t address = allocActor40(kActorStageClass);
    if (!address) return;
    int16_t p[3];
    mouthPoint(a, 0, 0x20, -0x80, p);
    const uint32_t r1 = rngStep(), r2 = rngStep(), r3 = rngStep();
    ProjectileActor& e = projectileActorAt(address);
    e.loopFlags |= kActorSweepGroup2;
    e.flags = kActorLive;
    e.type = 0x12;
    e.variant = 0;
    e.pos.x.whole = p[0];
    e.pos.y.whole = p[1];
    e.pos.z.whole = p[2];
    e.rotation.x = int16_t(((r2 >> 6) & 0x1ff) - 0x180u);
    e.rotation.y = int16_t(uint32_t(uint16_t(a.rotation.y)) + (r1 & 0x1ff) * 2u - 0x200u);
    e.rotation.z = 0;
    e.velocity.x = 0;
    e.velocity.y = 0;
    e.velocity.z = int16_t(-int32_t(((r3 >> 4) & 0xff) + 0x80));
    e.acceleration.x = 0;
    e.acceleration.y = 0;
    e.acceleration.z = 0;
    e.setParamsWord(0);
}

// ---- the beam (static, ST14 type 0x2c, exe 0x5aad40) ---------------------------------------------------------------
constexpr uint16_t kBeamShortSegments = 0x0002; // the head: the short segments (pattern 1)
constexpr uint8_t kSegmentTip = 0x01, kSegmentShort = 0x02;
constexpr uint8_t kBeamType = 0x2c;
constexpr uint32_t kBeamSegments = 0x10;
constexpr uint32_t kHitGroupBeam = 6;
// by the segment's growth (the exe reads past the 16 widths for the longer beams: the words that follow)
const DataTable<ScalarTable<uint16_t, 0x80>> beamWidths{TableId::MandaBossBeamWidths};
const DataTable<ScalarTable<uint16_t, 0x80>> shortBeamWidths{TableId::MandaBossShortBeamWidths}; // (likewise)
enum class BeamState : uint8_t { Spawn = 0, Grow = 1, Hold = 2, Shrink = 3, Done = 4 };

constexpr uint32_t kFloaterRisen = 0x1, kFloaterOut = 0x2, kFloaterInReach = 0x4, kFloaterApproach = 0x8;

// exe 0x4918a0: the beam (static type 0x2c) aimed by the flags: at the floater (pattern 2), low (pattern 1), else at
// the player (its pitch offset by the perch and the quadrant)
void spawnBeam(AnimatedActor& a, GuardianFields& f) {
    const GuardianFightBlock& w = workOf(f);
    const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
    pointAtWord(f.beam, address);
    if (!address) return;
    StaticActor& e = staticActorAt(address);
    BeamHeadFields& b = classState<BeamHeadFields>(e);
    e.flags = kActorLive;
    e.type = kBeamType;
    e.variant = 0;
    e.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
    e.setParamsWord(0);
    e.enterState(0);
    b.target = f.target;
    if (f.flags & kGuardianBeamAtTarget) {
        b.pattern = 2;
        b.pitchOffset = 0;
    } else if (f.flags & kGuardianBeamLow) {
        b.pattern = 1;
        b.pitchOffset = 0x80;
    } else if (w.perch != 3 && w.playerQuadrant != 0) {
        b.pitchOffset = -0x60;
    } else {
        b.pitchOffset = 0x60;
    }
    f.flags = f.flags | kGuardianBeamOut;
}
// a blast (the global static type 7) of the size at the point
void blastAt(int16_t x, int16_t y, int16_t z, uint16_t size) {
    const uint32_t address = allocActor60(0); // exe 0x5e1300
    if (!address) return;
    StaticActor& e = staticActorAt(address);
    e.flags = kActorLive;
    e.type = typeByte(GlobalStaticType::Blast);
    e.variant = 0;
    e.loopFlags |= kActorSweepGroup2;
    e.pos.x.whole = x;
    e.pos.y.whole = y;
    e.pos.z.whole = z;
    BlastParams params(e);
    params.size = size;
    params.attack = 0;
}
// exe 0x491ef0: a small blast at a random point around it
void blastRandom(const AnimatedActor& a) {
    const uint32_t r1 = rngStep(), r2 = rngStep(), r3 = rngStep();
    const int16_t x = a.pos.x.whole, y = a.pos.y.whole, z = a.pos.z.whole;
    blastAt(int16_t(x - int16_t(r1 & 0x7f) + 0x40), int16_t(y - int16_t((r2 >> 8) & 0x7f) - 0x80),
            int16_t(z - int16_t((r3 >> 16) & 0x7f) + 0x40), 0x80);
}
// exe 0x491b20: the big blast of its end
void blastBig(const AnimatedActor& a) {
    blastAt(a.pos.x.whole, int16_t(a.pos.y.whole - 0xc0), a.pos.z.whole, 0x200);
}

// ---- its damage, the beam's aim ----
// exe 0x491600 (while kBodyArmed / kStomping): the main box's player hits hurt it
void bodyDamage(AnimatedActor& a, GuardianFields& f) {
    const uint32_t r = (*f.hits).main.result;
    if (!(r & kHitAttackByPlayer)) return;
    const uint32_t d = r & kHitDamageMask;
    a.tint = kTintFlash;
    if (d != 0 && hitPointsAdd(a, int(int16_t(-int32_t(d)))) != 0) f.flags = f.flags | kGuardianDead;
}
// exe 0x491650: a thrown body's hit on either box -> the hit (and its damage)
void headDamage(AnimatedActor& a, GuardianFields& f) {
    const GuardianHitBoxes& boxes = *f.hits;
    uint32_t r = boxes.body.result | boxes.main.result;
    if (!(r & kHitAttackThrown)) return;
    r &= kHitDamageMask;
    f.flags = f.flags | kGuardianHit;
    a.tint = kTintFlash;
    if (r != 0 && hitPointsAdd(a, int(int16_t(-int32_t(r)))) != 0) f.flags = f.flags | kGuardianDead;
}
// exe 0x4919b0: the beam at the mouth along its head's angles, aimed at the player (or at the floater)
void beamAim(const AnimatedActor& a, const GuardianFields& f) {
    StaticActor& c = pointee(f.beam, "exe 0x4919b0 with no beam (the exe writes through null)");
    BeamHeadFields& b = classState<BeamHeadFields>(c);
    int16_t p[3];
    mouthPoint(a, 0, 0x28, -0x60, p);
    c.pos.x.whole = p[0];
    c.pos.y.whole = p[1];
    c.pos.z.whole = p[2];
    LaneVec4 head = decodedFrame().entries[1]; // the decoded frame's part 0 angles
    c.rotation.x = int16_t(uint32_t(uint16_t(a.rotation.x)) + uint16_t(head.x) + 0x100u);
    c.rotation.y = int16_t(uint32_t(uint16_t(a.rotation.y)) + uint16_t(head.y));
    c.rotation.z = head.z;
    if (!(f.flags & kGuardianBeamAtTarget)) {
        b.aimPitch = f.playerPitch;
        b.aimYaw = f.playerYaw;
        return;
    }
    const AnimatedActor& t = pointee(f.target, "exe 0x4919b0 with no target (the exe reads address 0x12)");
    const int16_t me[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
    const int16_t tg[3] = {t.pos.x.whole, t.pos.y.whole, t.pos.z.whole};
    b.aimPitch = int16_t(0x800 - pitchOf(tg, me));
    b.aimYaw = int16_t(angleOf(tg, me));
    if (b.aimYaw > 0x7ff) b.aimYaw = int16_t(uint32_t(uint16_t(b.aimYaw)) - 0x1000u);
}

// ---- the sub-states [0x8929bc] ----
// exe 0x490080: waits the timer, then chooses: the floater exposed (face the opposite perch's yaw, then the beam at it;
// 8 while it stays exposed), a pending hop (the debris, or the beam when facing a quarter turn with kVolley), the player
// in this perch's quadrant (the falling shots with kVolley, else face the player: the turns, the beam), no quadrant
// (face the player), another quadrant (face it, then a hop or the debris first)
void choose(AnimatedActor& a, GuardianFields& f) {
    GuardianFightBlock& w = workOf(f);
    uint32_t fl = f.flags;
    if (fl & kGuardianHit) {
        toHit(a, f);
        return;
    }
    f.timer = int16_t(uint32_t(uint16_t(f.timer)) - 1u);
    if (f.timer != 0) return;
    if (fl & kGuardianBeamAtTarget) {
        FightFields& fight = classState<FightFields>(pointee(f.controller, "exe 0x490080 with no controller"));
        const uint8_t c = fight.bits;
        if (c & kFightFloaterAlive) {
            toSub(a, GuardianSub::TargetBeam);
            return;
        }
        fight.bits = uint8_t(c & 0xfb);
        f.flags = f.flags & 0xffffe7ff;
    }
    auto turnOrFacing = [&](int d) -> bool { // the turn right / left; false: facing
        if (d > 0) {
            toSub(a, GuardianSub::TurnRight);
            return true;
        }
        if (d < 0) {
            toSub(a, GuardianSub::TurnLeft);
            return true;
        }
        return false;
    };
    auto settle = [&]() {
        f.timer = 1;
        a.step = 0;
    };
    fl = f.flags;
    if (fl & kGuardianTargetAsked) {
        FightFields& fight = classState<FightFields>(pointee(f.controller, "exe 0x490080 with no controller"));
        const uint8_t c = fight.bits;
        if (c & kFightFloaterAlive) {
            f.flags = fl | kGuardianBeamAtTarget;
            const int8_t q = int8_t(3 - w.perch);
            w.nextPerch = q;
            const int16_t t = perchYaws[size_t(int32_t(q) + int32_t(w.perch) * 4)];
            w.targetYaw = t;
            if (!turnOrFacing(angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(int32_t(t))))) settle();
            return;
        }
        fight.bits = uint8_t(c & 0xfb);
        f.flags = fl & 0xfffff7ff;
    }
    fl = f.flags;
    if (fl & kGuardianHopPending) {
        fl &= ~kGuardianHopPending;
        f.flags = fl;
        const int8_t q = w.playerQuadrant;
        if (int32_t(w.perch) + int32_t(q) != 3 && q != kNoQuadrant && (int32_t(a.rotation.y) % 0x400) == 0 &&
            (fl & kGuardianVolley)) {
            f.counter = 3;
            f.flags = fl & 0xfffffadf;
            toSub(a, GuardianSub::Beam);
            return;
        }
        toSub(a, GuardianSub::Debris);
        return;
    }
    const int8_t cur = w.perch, q = w.playerQuadrant;
    auto facePlayer = [&]() {
        const uint16_t t = uint16_t((uint32_t(uint16_t(f.playerYaw)) + 0x100u) & 0xfffffe00u);
        w.targetYaw = int16_t(t);
        if (int16_t(t) >= 0x800) w.targetYaw = int16_t(uint32_t(t) - 0x1000u);
        return angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(int32_t(w.targetYaw)));
    };
    if (cur == q) {
        f.flags = fl & ~kGuardianLanded;
        if (fl & kGuardianVolley) {
            f.flags = fl & 0xfffffeef;
            toSub(a, GuardianSub::Volley);
            return;
        }
        const int d = facePlayer();
        if (turnOrFacing(d)) return;
        f.counter = 1;
        f.flags = f.flags | kGuardianVolley | kGuardianBeamLow;
        toSub(a, GuardianSub::Beam);
        return;
    }
    if (q == kNoQuadrant) {
        if (!turnOrFacing(facePlayer())) settle();
        return;
    }
    w.nextPerch = q;
    const int16_t t = perchYaws[size_t(int32_t(q) + int32_t(cur) * 4)];
    w.targetYaw = t;
    if (turnOrFacing(angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(int32_t(t))))) return;
    if (int32_t(q) + int32_t(cur) == 3) {
        const uint32_t r = rngStep();
        if (r & 1) {
            a.subState = uint8_t(GuardianSub::SpinHop);
            f.flags = f.flags | kGuardianFastHop;
            a.step = 0;
        } else {
            toSub(a, GuardianSub::Hop);
        }
        return;
    }
    if (rngStep() & 1) {
        toSub(a, (rngStep() & 2) ? GuardianSub::SpinHop : GuardianSub::Hop);
        return;
    }
    f.flags = f.flags | kGuardianHopPending;
    settle();
}
// exe 0x4903d0 / 0x490650: the quarter-step turns (+-0x200 over the animation: the yaw = the start +- (n << 9) / 18)
// until the target yaw; step 5: back to the choice (a hop pending when the player changed quadrant)
void turn(AnimatedActor& a, GuardianFields& f, bool right) {
    GuardianFightBlock& w = workOf(f);
    auto spin = [&](uint8_t next) {
        const int32_t n = int32_t(f.counter) << 9;
        const int16_t s = int16_t(n / 18);
        a.rotation.y = right ? int16_t(uint32_t(uint16_t(s)) + uint16_t(w.turnYaw))
                             : int16_t(uint32_t(uint16_t(w.turnYaw)) - uint16_t(s));
        f.counter = int16_t(uint32_t(uint16_t(f.counter)) + 1u);
        if (!a.animEnded()) return;
        soundPlayAtPos(kSoundTurn, a.pos);
        a.anim = next;
        f.startKey = 0;
        ++a.step;
        w.turnYaw = right ? int16_t(uint32_t(uint16_t(w.turnYaw)) + uint32_t(kTurnStep))
                          : int16_t(uint32_t(uint16_t(w.turnYaw)) - uint32_t(kTurnStep));
        f.counter = 0;
    };
    auto hold = [&](uint8_t again, bool loop) {
        a.rotation.y = w.turnYaw;
        if (f.flags & kGuardianHit) {
            toHit(a, f);
            return;
        }
        if (!a.animEnded()) return;
        soundPlayAtPos(kSoundTurn, a.pos);
        if (w.targetYaw == w.turnYaw) {
            play(a, f, 0x12);
            a.step = 5;
        } else {
            play(a, f, again);
            if (loop) a.step = 1;
            else ++a.step;
        }
    };
    switch (a.step) {
    case 0:
        ++a.step;
        play(a, f, right ? 0x13 : 0xd);
        w.turnYaw = a.rotation.y;
        f.counter = 0;
        break;
    case 1: spin(right ? 0x10 : 0xe); break;
    case 2: hold(right ? 0x11 : 0xf, false); break;
    case 3: spin(right ? 0xe : 0x10); break;
    case 4: hold(right ? 0xf : 0x11, true); break;
    case 5: {
        const uint32_t fl = f.flags;
        if (fl & kGuardianHit) {
            toHit(a, f);
            break;
        }
        if (!a.animEnded()) break;
        if (w.perch != w.playerQuadrant && (fl & kGuardianLanded))
            f.flags = (fl & ~kGuardianLanded) | kGuardianHopPending;
        play(a, f, 0);
        toSub(a, GuardianSub::Choose);
        f.timer = 1;
        break;
    }
    default: break;
    }
    if (w.turnYaw >= 0x800) w.turnYaw = int16_t(uint32_t(uint16_t(w.turnYaw)) - 0x1000u);
    if (w.turnYaw < -0x800) w.turnYaw = int16_t(uint32_t(uint16_t(w.turnYaw)) + 0x1000u);
}
// the motion cleared (the velocity's and the acceleration's four dwords)
void stop(AnimatedActor& a) {
    a.velocity.clearWords();
    a.acceleration.clearWords();
}
// exe 0x4908d0: the hop to the next perch (the launch speed for 0x400 / gravity 0x30), the landing (a shake, the dust)
void hop(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) {
    GuardianFightBlock& w = workOf(f);
    const Perch& next = perches[size_t(int32_t(w.nextPerch))];
    switch (a.step) {
    case 0:
        play(a, f, 7);
        ++a.step;
        return;
    case 1:
        if (!a.animEnded()) return;
        soundPlayAtPos(kSoundTakeOff, a.pos);
        dustFeet(env, a);
        a.velocity.x = 0;
        a.velocity.y = int16_t(launchToPerch(a.pos, next, 0x400, 0x30));
        a.velocity.z = -0x400;
        a.acceleration.x = 0;
        a.acceleration.y = 0x30;
        a.acceleration.z = 0;
        play(a, f, 8);
        ++a.step;
        f.flags = f.flags & ~kGuardianOnGround;
        return;
    case 2:
        if (a.velocity.y > 0) {
            play(a, f, 9);
            ++a.step;
        }
        return;
    case 3:
        if (next.pointY() > a.pos.y.whole) return;
        soundPlayAtPos(kSoundLand, a.pos);
        dustFeet(env, a);
        placeAt(a.pos, perches[size_t(int32_t(w.nextPerch))]);
        stop(a);
        play(a, f, 10);
        ++a.step;
        f.flags = f.flags | kGuardianOnGround;
        cameraShakeAt(0x1000, 0x200, 0x100, 0x1000, a.pos);
        return;
    case 4:
        if (!a.animEnded()) return;
        toChoose(a, f, 0x1e);
        w.perch = w.nextPerch;
        f.flags = (f.flags & ~kGuardianHit) | kGuardianLanded;
        return;
    default: return;
    }
}
// exe 0x490aa0: the spinning hop (speed 0x280, or 0x400 when fast; gravity 0x40; the yaw -0x1e0 a tick while it
// falls), the landing's shock, a stomp (animations 0x1c / 0x1d with kStomping, 0x1a x 8, 0x1b) and the reset on the
// perch
void spinHop(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) {
    GuardianFightBlock& w = workOf(f);
    const Perch& next = perches[size_t(int32_t(w.nextPerch))];
    switch (a.step) {
    case 0:
        play(a, f, 7);
        ++a.step;
        return;
    case 1: {
        if (!a.animEnded()) return;
        soundPlayAtPos(kSoundTakeOff, a.pos);
        dustFeet(env, a);
        int16_t speed = 0x280;
        if (f.flags & kGuardianFastHop) {
            speed = 0x400;
            f.flags = f.flags & ~kGuardianFastHop;
        }
        a.velocity.x = 0;
        a.velocity.y = int16_t(launchToPerch(a.pos, next, speed, 0x40));
        a.velocity.z = int16_t(int32_t(speed) * -2);
        a.acceleration.x = 0;
        a.acceleration.y = 0x40;
        a.acceleration.z = 0;
        play(a, f, 0x14);
        ++a.step;
        f.flags = f.flags & ~kGuardianOnGround;
        return;
    }
    case 2:
        if (a.velocity.y > 0) {
            a.velocity.z = 0;
            ++a.step;
            f.startKey = 0;
            w.turnYaw = a.rotation.y;
        }
        return;
    case 3:
        a.rotation.y = int16_t(uint32_t(uint16_t(a.rotation.y)) - 0x1e0u);
        if (next.pointY() > a.pos.y.whole) return;
        soundPlayAtPos(kSoundLand, a.pos);
        placeAt(a.pos, perches[size_t(int32_t(w.nextPerch))]);
        stop(a);
        f.startKey = 0;
        a.rotation.y = w.turnYaw;
        a.anim = 0x16;
        ++a.step;
        f.flags = f.flags | kGuardianOnGround;
        shock(env, a);
        cameraShakeAt(0x1000, 0x200, 0x100, 0x1000, a.pos);
        return;
    case 4:
        if (!a.animEnded()) return;
        a.velocity.z = 0x50;
        play(a, f, 0x1c);
        ++a.step;
        return;
    case 5:
        if (!a.animEnded()) return;
        play(a, f, 0x1d);
        ++a.step;
        f.flags = f.flags | kGuardianStomping;
        return;
    case 6:
        if (!a.animEnded()) return;
        a.velocity.z = 0;
        play(a, f, 0x1a);
        ++a.step;
        f.timer = 8;
        return;
    case 7:
        if (a.animEnded()) f.timer = int16_t(uint32_t(uint16_t(f.timer)) - 1u);
        if (f.timer != 0) return;
        a.velocity.z = -0xd0;
        play(a, f, 0x1b);
        ++a.step;
        f.flags = f.flags & ~kGuardianStomping;
        return;
    case 8: {
        if (!a.animEnded()) return;
        placeAt(a.pos, perches[size_t(int32_t(w.nextPerch))]);
        a.velocity.z = 0;
        toChoose(a, f, 0x1e);
        w.perch = w.nextPerch;
        const uint32_t fl = f.flags;
        f.flags = (fl & ~kGuardianHit) | kGuardianLanded;
        if (int8_t(a.spawnByte) != 0 && (fl & kGuardianDead)) toSub(a, GuardianSub::Dying);
        return;
    }
    default: return;
    }
}
// exe 0x490dd0: the debris spray (animation 1, the sound at its key 0x10, animation 3 with kBodyArmed for 0x5a ticks:
// debris every 6, animation 2)
void debrisSpray(AnimatedActor& a, GuardianFields& f) {
    switch (a.step) {
    case 0:
        f.flags = f.flags | kGuardianVolley;
        play(a, f, 1);
        ++a.step;
        break;
    case 1:
        if (int8_t(a.keyIndex) == 0x10) soundPlayAtPos(kSoundDebris, a.pos);
        if (!a.animEnded()) break;
        play(a, f, 3);
        ++a.step;
        f.timer = 0x5a;
        f.flags = f.flags | kGuardianBodyArmed;
        break;
    case 2:
        if (int32_t(f.timer) % 6 == 0) debris(a);
        f.timer = int16_t(uint32_t(uint16_t(f.timer)) - 1u);
        if (f.timer == 0) {
            play(a, f, 2);
            ++a.step;
        }
        break;
    case 3:
        if (!a.animEnded()) break;
        toChoose(a, f, 0x1e);
        f.flags = f.flags & ~kGuardianBodyArmed;
        break;
    default: break;
    }
    if (f.flags & kGuardianHit) toHit(a, f);
    if (int8_t(a.spawnByte) != 0 && (f.flags & kGuardianDead)) toSub(a, GuardianSub::Dying);
}
// exe 0x490f20: the falling shots (animation 0x17, 0x10 ticks, then a shot every 6 for 0x5a ticks, animation 0x18)
void volley(AnimatedActor& a, GuardianFields& f) {
    switch (a.step) {
    case 0:
        play(a, f, 0x17);
        ++a.step;
        f.timer = 0x10;
        break;
    case 1:
        f.timer = int16_t(uint32_t(uint16_t(f.timer)) - 1u);
        if (f.timer == 0) {
            ++a.step;
            f.timer = 0x5a;
        }
        break;
    case 2:
        if (int32_t(f.timer) % 6 == 0) {
            soundPlayAtPos(kSoundVolley, a.pos);
            fallingShot(a, f);
        }
        f.timer = int16_t(uint32_t(uint16_t(f.timer)) - 1u);
        if (f.timer == 0) {
            play(a, f, 0x18);
            ++a.step;
        }
        break;
    case 3:
        if (a.animEnded()) toChoose(a, f, 0x1e);
        break;
    default: break;
    }
    if (f.flags & kGuardianHit) toHit(a, f);
}
// the beam out and done: freed, animation 2
bool beamFinished(AnimatedActor& a, GuardianFields& f, uint32_t clears) {
    StaticActor& c = pointee(f.beam, "exe 0x491030 with no beam (the exe reads address 0xe)");
    if (classState<BeamHeadFields>(c).done != 1) return false;
    freeActor(c); // exe 0x5e1540
    play(a, f, 2);
    ++a.step;
    f.flags = f.flags & clears;
    return true;
}
// the beam's wind-up (animation 4 with kBodyArmed, 0x14 ticks)
void beamWindUp(AnimatedActor& a, GuardianFields& f) {
    GuardianFightBlock& w = workOf(f);
    soundPlayAtPos(kSoundDebris, a.pos);
    play(a, f, 4);
    ++a.step;
    f.timer = 0x14;
    f.flags = f.flags | kGuardianBodyArmed;
    w.beamQuadrant = w.playerQuadrant;
}
// the wind-up's tick; true: the beam out (animation 6)
bool beamCharge(AnimatedActor& a, GuardianFields& f) {
    f.timer = int16_t(uint32_t(uint16_t(f.timer)) - 1u);
    if (f.timer != 0) return false;
    soundPlayAtPos(kSoundBeam, a.pos);
    play(a, f, 6);
    spawnBeam(a, f);
    ++a.step;
    return true;
}
// exe 0x491030: the beam (repeated `counter` times while the player's quadrant holds)
void beamAttack(AnimatedActor& a, GuardianFields& f) {
    GuardianFightBlock& w = workOf(f);
    uint32_t fl;
    switch (a.step) {
    case 0: beamWindUp(a, f); return;
    case 1:
        if (beamCharge(a, f)) return;
        fl = f.flags;
        if (!(fl & kGuardianHit)) return;
        break;
    case 2: beamFinished(a, f, 0xfffffdbf); return;
    case 3:
        if (int8_t(a.spawnByte) != 0 && (f.flags & kGuardianDead)) toSub(a, GuardianSub::Dying);
        fl = f.flags;
        if (!(fl & kGuardianHit)) {
            if (!a.animEnded()) return;
            f.counter = int16_t(uint32_t(uint16_t(f.counter)) - 1u);
            if (w.beamQuadrant != w.playerQuadrant || f.counter == 0) {
                toChoose(a, f, 1);
                return;
            }
            a.step = 0;
            return;
        }
        break;
    default: return;
    }
    f.flags = fl & kGuardianHitClears;
    toSub(a, GuardianSub::Hit);
}
// exe 0x4911c0: the beam at the exposed floater; afterwards, the floater let go: it heals 1/8 (animation 5, 5 rounds)
void targetBeam(AnimatedActor& a, GuardianFields& f) {
    switch (a.step) {
    case 0: beamWindUp(a, f); return;
    case 1:
        if (beamCharge(a, f)) return;
        break;
    case 2:
        if (!beamFinished(a, f, 0xffffe58f)) return;
        {
            FightFields& fight = classState<FightFields>(pointee(f.controller, "exe 0x4911c0 with no controller"));
            fight.bits = uint8_t(fight.bits & 0xfb);
        }
        return;
    case 3: {
        if (int8_t(a.spawnByte) != 0 && (f.flags & kGuardianDead)) toSub(a, GuardianSub::Dying);
        if (!a.animEnded()) return;
        const AnimatedActor& t = pointee(f.target, "exe 0x4911c0 with no target (the exe reads address 0xe)");
        if (classState<FloaterFields>(t).released == 0) {
            toChoose(a, f, 1);
        } else {
            a.hitPoints = int16_t(uint32_t(uint16_t(a.hitPoints)) + uint16_t(a.maxHitPoints >> 3));
            play(a, f, 5);
            ++a.step;
            f.counter = 5;
        }
        break;
    }
    case 4:
        if (f.flags & kGuardianHit) {
            toHit(a, f);
            return;
        }
        if (a.animEnded()) {
            f.counter = int16_t(uint32_t(uint16_t(f.counter)) - 1u);
            if (f.counter == 0) toChoose(a, f, 1);
        }
        return;
    default: return;
    }
    if (f.flags & kGuardianHit) toHit(a, f);
}
// exe 0x4913d0: hit (animation 0x19 with kStomping, 0x1a x 8, 0x1b), back onto its perch
void hit(AnimatedActor& a, GuardianFields& f) {
    GuardianFightBlock& w = workOf(f);
    switch (a.step) {
    case 0:
        a.velocity.z = 0x50;
        play(a, f, 0x19);
        ++a.step;
        f.flags = (f.flags & ~kGuardianBodyArmed) | kGuardianStomping;
        w.nextPerch = w.perch;
        return;
    case 1:
        if (!a.animEnded()) return;
        a.velocity.z = 0;
        play(a, f, 0x1a);
        ++a.step;
        f.timer = 8;
        return;
    case 2:
        if (a.animEnded()) f.timer = int16_t(uint32_t(uint16_t(f.timer)) - 1u);
        if (f.timer != 0) return;
        f.startKey = 0;
        a.velocity.z = -0xd0;
        a.anim = 0x1b;
        ++a.step;
        f.flags = f.flags & ~kGuardianStomping;
        return;
    case 3: {
        if (!a.animEnded()) return;
        placeAt(a.pos, perches[size_t(int32_t(w.nextPerch))]);
        a.velocity.z = 0;
        toChoose(a, f, 0x1e);
        const uint32_t fl = f.flags;
        f.flags = fl & ~kGuardianHit;
        if (int8_t(a.spawnByte) != 0 && (fl & kGuardianDead)) toSub(a, GuardianSub::Dying);
        return;
    }
    default: return;
    }
}
// exe 0x491520 (the spawn bytes other than 0): animation 0xc, reddening, blasts for 0x82 ticks, then the big one, the
// controller told, gone
void dying(AnimatedActor& a, GuardianFields& f) {
    if (a.step == 0) {
        f.timer = 0x82;
        f.counter = 0x2d;
        play(a, f, 0xc);
        ++a.step;
        return;
    }
    if (a.step != 1) return;
    f.counter = int16_t(uint32_t(uint16_t(f.counter)) - 1u);
    if (f.counter == 0) {
        const int8_t red = int8_t(f.red + 1);
        f.red = red;
        if (red > 0x1f) f.red = 0x1f;
        if (f.timer < 0x1f) f.counter = 4;
        else f.counter = int16_t((rngStep() & 7) + 6u);
        blastRandom(a);
    }
    f.timer = int16_t(uint32_t(uint16_t(f.timer)) - 1u);
    if (f.timer == 0) {
        blastBig(a);
        FightFields& fight = classState<FightFields>(pointee(f.controller, "exe 0x491520 with no controller"));
        fight.bits = uint8_t(fight.bits | kFightVariantDied);
        a.enterState(uint8_t(GuardianState::Gone));
    }
}
void guardianSub(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) {
    switch (GuardianSub(a.subState)) {
    case GuardianSub::Choose: choose(a, f); break;
    case GuardianSub::TurnRight: turn(a, f, true); break;
    case GuardianSub::TurnLeft: turn(a, f, false); break;
    case GuardianSub::Hop: hop(env, a, f); break;
    case GuardianSub::SpinHop: spinHop(env, a, f); break;
    case GuardianSub::Debris: debrisSpray(a, f); break;
    case GuardianSub::Volley: volley(a, f); break;
    case GuardianSub::Beam: beamAttack(a, f); break;
    case GuardianSub::TargetBeam: targetBeam(a, f); break;
    case GuardianSub::Hit: hit(a, f); break;
    case GuardianSub::Dying: dying(a, f); break;
    default: notPortedAt(kStoryPort, "0x48fcd0 sub-state", a.subState);
    }
}

// exe 0x48fbc0: the model of its spawn byte, the motion cleared, its work area and hit boxes in its third bone block,
// on perch 2, its stats row (the hit points << 6), the boss gauge
void setUp(AnimatedActor& a, GuardianFields& f) {
    if (!bindActorModel(a, a.spawnByte)) { // exe 0x5e0bc0(actor, actor + 7)
        freeActor(a);                      // exe 0x5e1490
        return;
    }
    a.flags |= kActorScenery;
    a.clearMotion();
    BoneBlock* block = boneBlockOf(a, 2); // exe 0x5e1780
    if (block == nullptr) notPorted(kStoryPort, "exe 0x5e1780: no such bone block");
    f.work.pointAt(block->at + 0x40);
    f.hits.pointAt(block->at + 0x90);
    f.clearStateBytesBytes();
    GuardianFightBlock& w = workOf(f);
    w = {};
    *f.hits = {};
    f.timer = 0x3c;
    f.flags = f.flags | kGuardianOnGround;
    w.perch = 2;
    w.nextPerch = 2;
    const uint32_t row = statTableRow(actorStatTables().animated, a.type);
    const uint32_t entry = row ? row + uint32_t(a.spawnByte) * 0x10 : 0;
    w.statsRow = entry;
    const uint16_t hp = entry ? uint16_t(uint32_t(animatedStatsAt(entry).hitPoints) << 6) : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    hudAddBossGauge(a, 1, 4, 0x203050); // exe 0x415d30
    a.anim = 0;
    f.startKey = 0;
    f.red = 0x10;
    animSetPose(a, 0, 0); // exe 0x5e1c30
    a.subState = uint8_t(GuardianSub::Choose);
    a.state = uint8_t(GuardianState::Fight);
}
// the player's quadrant of the room (its chest's integer parts; -1: none)
int8_t playerQuadrant(int16_t x, int16_t y, int16_t z) {
    if (y >= -0x80) return kNoQuadrant;
    const bool front = z >= -0x620 && z <= -0x1e0, back = z >= 0x1e0 && z <= 0x620;
    if (x >= -0x420 && x <= 0x20) {
        if (front) return 0;
        if (back) return 1;
    } else if (x >= 0x3e0 && x <= 0x820) {
        if (front) return 2;
        if (back) return 3;
    }
    return kNoQuadrant;
}
// exe 0x48fcd0: the player's quadrant and direction, the controller's bits, the sub-state, the motion, the damage, the
// pose (with the beam's aim), the two hit entries; the guardian out of hit points -> gone
void fight(const PlayerEnv& env, AnimatedActor& a, GuardianFields& f) {
    const ShortVec4& bone0 = bonePoint(a, 0, kStoryPort); // exe 0x5e1810
    const Player& p = player();
    const int32_t chest[3] = {p.pos.x, int32_t(uint32_t(p.pos.y) - 0x800000u), p.pos.z};
    const int32_t from[3] = {int32_t(uint32_t(int32_t(bone0.x)) << 16), int32_t(uint32_t(int32_t(bone0.y)) << 16),
                             int32_t(uint32_t(int32_t(bone0.z)) << 16)};
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.tint = uint16_t(uint16_t(int16_t(f.red)) | 0x4200u);
    GuardianFightBlock& w = workOf(f);
    w.playerQuadrant = playerQuadrant(int16_t(uint32_t(chest[0]) >> 16), int16_t(uint32_t(chest[1]) >> 16),
                                      int16_t(uint32_t(chest[2]) >> 16));
    FightFields& fight = classState<FightFields>(pointee(f.controller, "exe 0x48fcd0 with no controller"));
    if (a.hitPoints < int16_t(a.maxHitPoints >> 1)) fight.bits = uint8_t(fight.bits | kFightGuardianHalf);
    else fight.bits = uint8_t(fight.bits & 0xfd);
    if (fight.bits & kFightFloaterInReach) f.flags = f.flags | kGuardianTargetAsked;
    f.playerYaw = int16_t(angleOf(chest[0], chest[2], from[0], from[2]));
    f.playerPitch = int16_t(0x800 - pitchOf(chest, from));
    guardianSub(env, a, f);
    if (f.playerYaw > 0x7ff) f.playerYaw = int16_t(uint32_t(uint16_t(f.playerYaw)) - 0x1000u);
    if (a.rotation.y > 0x7ff) a.rotation.y = int16_t(uint32_t(uint16_t(a.rotation.y)) - 0x1000u);
    if (a.rotation.y < -0x800) a.rotation.y = int16_t(uint32_t(uint16_t(a.rotation.y)) + 0x1000u);
    a.velocity.z = int16_t(uint32_t(uint16_t(a.velocity.z)) + uint16_t(a.acceleration.z));
    a.velocity.y = int16_t(uint32_t(uint16_t(a.velocity.y)) + uint16_t(a.acceleration.y));
    a.velocity.x = int16_t(uint32_t(uint16_t(a.velocity.x)) + uint16_t(a.acceleration.x));
    moveAlongYaw(a.pos, a.rotation.y, a.velocity.x, a.velocity.y, a.velocity.z);
    if (f.flags & kGuardianOnGround) a.pos.y.whole = int16_t(actorGround(env, a)); // exe 0x40f190
    if (f.flags & (kGuardianBodyArmed | kGuardianStomping)) bodyDamage(a, f);
    headDamage(a, f);
    if (a.anim != a.prevAnim) animSet(a, a.anim, int(f.startKey)); // exe 0x5e1c50
    else animStep(a);                                              // exe 0x5e1ea0
    if (f.flags & kGuardianBeamOut) beamAim(a, f);
    animPose(a); // exe 0x5e2d90
    GuardianHitBoxes& boxes = *f.hits;
    boxes.main.result = 0;
    boxes.main.previousX = a.prevPos.x.whole;
    boxes.main.previousZ = a.prevPos.z.whole;
    boxes.body.result = 0;
    boxes.body.previousX = a.prevPos.x.whole;
    boxes.body.previousZ = a.prevPos.z.whole;
    const uint32_t fl = f.flags;
    int16_t mouth[3];
    mouthPoint(a, 0, 0x20, -0x80, mouth);
    hitListSubmit(mouth, boxes.main, hitOwner(a.slot) | hitGroup(kHitGroupBodies) | hitRadius(0xa0),
                  ((fl & kGuardianBodyArmed) ? kHitAttackSolid : kHitAttackDeflects) | 0x10u);
    const bool stomping = (f.flags & kGuardianStomping) != 0;
    const ShortVec4& bone = bonePoint(a, 0, kStoryPort);
    const int16_t body[3] = {bone.x, bone.y, bone.z};
    hitListSubmit(body, stomping ? boxes.main : boxes.body,
                  hitOwner(a.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x100),
                  (stomping ? kHitAttackSolid : kHitAttackDeflects) | 8u);
    if (a.spawnByte == 0 && (f.flags & kGuardianDead)) a.enterState(uint8_t(GuardianState::Gone));
}
// exe 0x490050: the guardian's flag (spawn byte 0), freed
void gone(AnimatedActor& a) {
    if (a.spawnByte == 0) setStoryFlag(kFlagGuardianBeaten);
    freeActor(a); // exe 0x5e1490
}
// exe 0x48fb90
void guardianUpdate(const PlayerEnv& env, AnimatedActor& a) {
    auto&& f = classState<GuardianFields>(a);
    switch (GuardianState(a.state)) {
    case GuardianState::SetUp: setUp(a, f); break;
    case GuardianState::Fight: fight(env, a, f); break;
    case GuardianState::Gone: gone(a); break;
    default: notPortedAt(kStoryPort, "0x48fb90 state", a.state);
    }
    // exe 0x5f20f0(actor, 0x100, 0x100, 0): the shadow (render output)
}

// ---- the floater (exe 0x4b83e0; states [0x894828]: 0 0x4b8410 set up, 1 0x4b84d0 float (sub-states [0x894838]: 0
// 0x4b8830 wander, 1 0x4b8b30 approach), 2 0x4b8750 shot down (a blast and the drops at 0x13, freed at 0), 3 0x4b87e0
// caught by the beam (an animation, freed when the beam lets it go)) --------------------------------------------
enum class FloaterState : uint8_t { SetUp = 0, Float = 1, Down = 2, Caught = 3 };
enum class FloaterSub : uint8_t { Wander = 0, Approach = 1 };
const int16_t kArenaCentre[3] = {0x200, -0x400, 0};

// exe 0x4b8cb0: its hit entry (no lock-on beyond 0xc00 of the player)
void floaterHitBox(const AnimatedActor& a, FloaterFields& f) {
    f.hitBox.result = 0;
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    const int16_t p[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
    const uint32_t distant = f.playerDistance < 0xc00 ? 0u : kHitNoLockOn;
    hitListSubmit(p, f.hitBox, hitOwner(a.slot) | distant | hitGroup(kHitGroupBodies) | hitRadius(0x40),
                  kHitAttackThrown | kHitAttackSolid | 1u);
}
// exe 0x4b8d40: its glow trail (stage effect type 6, variant 1)
void floaterGlow(AnimatedActor& a) {
    const uint32_t address = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (!address) return;
    EffectActor& e = effectActorAt(address);
    GlowTrailFields& glow = classState<GlowTrailFields>(e);
    e.flags = kActorLiveDrawn;
    e.type = kGlowTrailStageType;
    e.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
    e.variant = uint8_t(GlowTrailVariant::Trail);
    e.enterStateKeepingStep(0);
    glow.size = 0x10;
    glow.mode = 0;
    glow.parent = &a;
    e.pos.x = 0;
    e.pos.y = 0;
    e.pos.z = -0x60;
    glow.bone = 0;
}
// exe 0x4b8410
void floaterSetUp(AnimatedActor& a, FloaterFields& f) {
    if (!bindActorModel(a, 0)) { // exe 0x5e0ba0
        freeActor(a);
        return;
    }
    a.clearMotion();
    f.clearStateBytesBytes();
    f.statsRow = statTableRow(actorStatTables().animated, a.type);
    a.velocity.y = 0x80;
    a.velocity.z = 0;
    const uint16_t hp = animatedStatsAt(f.statsRow).hitPoints;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.anim = 0;
    f.startKey = 0;
    animSetPose(a, 0, 0); // exe 0x5e1c30
    floaterGlow(a);
    a.state = uint8_t(FloaterState::Float);
    f.setModes(uint16_t(rngStep() & 0x707));
    f.timer = 0x3c;
}
// the floater's turn: toward a turn speed of s (to the left) ...
void turnLeft(FloaterFields& f, int speed, int s) { f.turnAccel = int16_t(speed < s ? 2 : 0); }
// ... to the right
void turnRight(FloaterFields& f, int speed, int s) { f.turnAccel = int16_t(speed <= -s ? 0 : -2); }
// the floater's bob (once it has risen to -0x400)
void floaterBob(AnimatedActor& a, FloaterFields& f) {
    if (!(f.flags & kFloaterRisen)) {
        if (a.pos.y.whole > -0x401) {
            f.bob = 1;
            f.flags = f.flags | kFloaterRisen;
            a.acceleration.y = -8;
        }
    } else {
        a.acceleration.y = int16_t(int16_t(f.bob) < 0x20 ? -8 : 8);
        f.bob = uint16_t(uint32_t(uint8_t(uint8_t(f.bob) + 1u)) & 0x3fu);
    }
}
// exe 0x4b8830: wanders in the arena by its random turn / speed modes (back toward the centre when out)
void floaterWander(AnimatedActor& a, FloaterFields& f) {
    const int16_t x = a.pos.x.whole, z = a.pos.z.whole;
    if (x < -0x400 || x > 0x800 || z < -0x600 || z > 0x600) {
        f.timer = 1;
        f.flags = f.flags | kFloaterOut;
        f.setModes(0x808);
    } else {
        f.timer = int16_t(uint32_t(uint16_t(f.timer)) - 1u);
        f.flags = f.flags & ~kFloaterOut;
        if (f.timer == 0) {
            f.timer = 0x3c;
            f.setModes(uint16_t(rngStep() & 0x707));
        }
    }
    if (a.pos.y.whole < -0x600) f.speedMode = 7;
    const int sp = a.spin.y;
    auto settle = [&] {
        if (sp - 2 < 0) {
            if (sp + 2 < 1) {
                f.turnAccel = 2;
            } else {
                a.spin.y = 0;
                f.turnAccel = 0;
            }
        } else {
            f.turnAccel = -2;
        }
    };
    const int8_t c = f.turnMode;
    switch (c) {
    case 2: case 3: case 4: turnLeft(f, sp, 1 << (c - 1)); break;
    case 5: case 6: case 7: turnRight(f, sp, 1 << (c - 4)); break;
    case 8: { // back toward the centre
        int16_t d = int16_t(a.rotation.y - f.centreAngle);
        if (d > 0x7ff) d = int16_t(d - 0x1000);
        if (d < -0x800) d = int16_t(d + 0x1000);
        if (d > 0x80) turnRight(f, sp, 0x20);
        else if (d < -0x80) turnLeft(f, sp, 0x20);
        else settle();
        break;
    }
    case 0: case 1: settle(); break;
    default: break;
    }
    const int v = a.velocity.z;
    auto toward = [&](int target) {
        if (v - 4 < target) {
            a.acceleration.z = 4;
        } else if (v + 4 > target) {
            a.acceleration.z = -4;
        } else {
            a.velocity.z = int16_t(target);
            a.acceleration.z = 0;
        }
    };
    switch (f.speedMode) {
    case 0: case 1: case 2:
        if (v - 4 < 0x200) {
            a.acceleration.z = 4;
        } else {
            a.velocity.z = 0x200;
            a.acceleration.z = 0;
        }
        break;
    case 3: case 4: toward(0x180); break;
    case 5: case 6: toward(0x100); break;
    case 7: toward(0x80); break;
    case 8:
        if (v + 8 > 0x80) {
            a.acceleration.z = -8;
        } else {
            a.velocity.z = 0x80;
            a.acceleration.z = 0;
        }
        break;
    default: break;
    }
    floaterBob(a, f);
    if (uint8_t(f.flags) & kFloaterApproach) {
        a.subState = uint8_t(FloaterSub::Approach);
        a.restartStep();
    }
}
// exe 0x4b8b30 (the guardian under half its hit points): turns toward the arena's centre and drifts in; within 0x200
// of it in reach (the controller tells the guardian), its speed down to 0; back to wandering once the ask is gone
void floaterApproach(AnimatedActor& a, FloaterFields& f) {
    const int sp = a.spin.y;
    auto settle = [&] {
        if (sp - 2 >= 0) {
            f.turnAccel = -2;
        } else if (sp <= -2) {
            f.turnAccel = 2;
        } else {
            a.spin.y = 0;
            f.turnAccel = 0;
        }
    };
    if (f.centreDistance < 0x201) {
        settle();
    } else {
        int16_t d = int16_t(a.rotation.y - f.centreAngle);
        if (d > 0x7ff) d = int16_t(d - 0x1000);
        if (d < -0x800) d = int16_t(d + 0x1000);
        if (d < 0x81) {
            if (d > -0x81) settle();
            else f.turnAccel = int16_t(sp < 0x20 ? 2 : 0);
        } else {
            f.turnAccel = int16_t(sp > -0x20 ? -2 : 0);
        }
    }
    if (f.centreDistance < 0x200) f.flags = f.flags | kFloaterInReach;
    const int t = (uint8_t(f.flags) & kFloaterInReach) ? 0 : 0x100;
    const int v = a.velocity.z;
    if (v - 4 < t) {
        a.acceleration.z = 4;
    } else if (t < v + 4) {
        a.acceleration.z = -4;
    } else {
        a.velocity.z = int16_t(t);
        a.acceleration.z = 0;
    }
    floaterBob(a, f);
    if (!(f.flags & kFloaterApproach)) {
        f.flags = f.flags & ~kFloaterInReach;
        a.subState = uint8_t(FloaterSub::Wander);
        a.restartStep();
    }
}
// exe 0x4b84d0: the distances, the controller's bits, the sub-state, the motion, the beam's catch / the damage
void floaterFloat(const PlayerEnv& env, AnimatedActor& a, FloaterFields& f) {
    a.prevAnim = a.anim;
    a.tint = kTintNeutral;
    a.savePreviousPose();
    const int16_t me[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
    const Player& p = player();
    const int16_t pl[3] = {p.pos.x.whole, p.pos.y.whole, p.pos.z.whole};
    f.playerDistance = distance3(me, pl);                           // exe 0x5e3ab0
    f.centreDistance = int16_t(planarDistance16(me, kArenaCentre)); // exe 0x5e3b40
    f.centreAngle = int16_t(angleOf(kArenaCentre, me));
    FightFields& fight = classState<FightFields>(pointee(f.controller, "exe 0x4b84d0 with no controller"));
    if ((fight.bits & kFightGuardianHalf) && (f.flags & kFloaterRisen)) f.flags = f.flags | kFloaterApproach;
    if (uint8_t(f.flags) & kFloaterInReach) fight.bits = uint8_t(fight.bits | kFightFloaterInReach);
    switch (FloaterSub(a.subState)) {
    case FloaterSub::Wander: floaterWander(a, f); break;
    case FloaterSub::Approach: floaterApproach(a, f); break;
    default: notPortedAt(kStoryPort, "0x4b84d0 sub-state", a.subState);
    }
    if (f.centreAngle > 0x7ff) f.centreAngle = int16_t(uint32_t(uint16_t(f.centreAngle)) - 0x1000u);
    if (a.rotation.y > 0x7ff) a.rotation.y = int16_t(uint32_t(uint16_t(a.rotation.y)) - 0x1000u);
    if (a.rotation.y < -0x800) a.rotation.y = int16_t(uint32_t(uint16_t(a.rotation.y)) + 0x1000u);
    a.velocity.z = int16_t(uint32_t(uint16_t(a.velocity.z)) + uint16_t(a.acceleration.z));
    a.spin.y = int16_t(uint32_t(uint16_t(a.spin.y)) + uint16_t(f.turnAccel));
    a.velocity.y = int16_t(uint32_t(uint16_t(a.velocity.y)) + uint16_t(a.acceleration.y));
    a.rotation.y = int16_t((uint32_t(uint16_t(a.spin.y)) + uint16_t(a.rotation.y)) & kAngleMask);
    integrateVelocity(a, 0, a.velocity.y, -int(a.velocity.z)); // exe 0x5e3690
    if (int32_t(actorTestMove(env, a)) < 0 && (actorPushOut(env, a) & 0xf00)) a.pos = a.prevPos; // 0x40f570 / 0x410960
    const uint32_t r = f.hitBox.result;
    if ((r & kHitAttackStrong) && a.subState != uint8_t(FloaterSub::Wander)) {
        f.caught = 1;
        a.state = uint8_t(FloaterState::Caught);
    } else if (r & (kHitAttackBump | kHitAttackByPlayer)) {
        a.tint = kTintFlash;
        if ((r & kHitDamageMask) && hitPointsAdd(a, int(int16_t(-int32_t(r & kHitDamageMask)))) != 0) {
            fight.bits = uint8_t(fight.bits & ~kFightFloaterAlive);
            a.state = uint8_t(FloaterState::Down);
            f.timer = 0x14;
        }
    }
    if (a.anim != a.prevAnim) animSetPose(a, a.anim, int(f.startKey)); // exe 0x5e1c30
    else animStepPose(a);                                              // exe 0x5e1e80
    floaterHitBox(a, f);
}
// exe 0x4b8750: shot down -- a blast and the drops at tick 0x13 (hidden), freed at 0
void floaterDown(AnimatedActor& a, FloaterFields& f) {
    f.timer = int16_t(uint32_t(uint16_t(f.timer)) - 1u);
    if (f.timer == 0) {
        freeActor(a);
        return;
    }
    if (f.timer == 0x13) {
        if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300
            StaticActor& e = staticActorAt(address);
            e.flags = kActorLive;
            e.type = typeByte(GlobalStaticType::Blast);
            e.variant = 0;
            e.loopFlags |= kActorSweepGroup2;
            e.pos = a.pos;
            BlastParams params(e);
            params.size = 0x80;
            params.attack = 0;
        }
        spawnDrops(a.pos, animatedStatsAt(f.statsRow).dropTable); // exe 0x5f1270
        a.flags &= uint8_t(~kActorDrawn);
    }
    floaterHitBox(a, f);
}
// exe 0x4b87e0: carried by the beam until it lets go: the controller told, freed
void floaterCaught(AnimatedActor& a, FloaterFields& f) {
    if (a.anim == a.prevAnim) animStepPose(a);
    else animSetPose(a, a.anim, int(f.startKey));
    if (f.released != 0) {
        FightFields& fight = classState<FightFields>(pointee(f.controller, "exe 0x4b87e0 with no controller"));
        fight.bits = uint8_t(fight.bits & ~kFightFloaterAlive);
        freeActor(a);
    }
}
// exe 0x4b83e0
void floaterUpdate(const PlayerEnv& env, AnimatedActor& a) {
    auto&& f = classState<FloaterFields>(a);
    switch (FloaterState(a.state)) {
    case FloaterState::SetUp: floaterSetUp(a, f); break;
    case FloaterState::Float: floaterFloat(env, a, f); break;
    case FloaterState::Down: floaterDown(a, f); break;
    case FloaterState::Caught: floaterCaught(a, f); break;
    default: notPortedAt(kStoryPort, "0x4b83e0 state", a.state);
    }
    // exe 0x5f20f0(actor, 0x40, 0x100, 0): the shadow (render output)
}

// ---- script 0x1a's puppets (pool 0x20; states 0 set up, 1 the act, 2 exe 0x468fa0: freed) ----------------------------
enum class PuppetState : uint8_t { SetUp = 0, Act = 1, Free = 2 };
const DataTable<ScalarTable<int8_t, 13>> puppetFadeThresholds{TableId::MandaBossPuppetFadeThresholds};
constexpr uint32_t kPuppetFadeBones = 13;
constexpr int8_t kPuppetFadeTicks = 0x1a;
constexpr uint8_t kPuppetDustVariant = 0xf;
constexpr uint16_t kSoundPuppetFade = 0x12a;
const DataTable<CollisionBoxRecord> risingPuppetBox{TableId::MandaBossRisingPuppetBox};
const DataTable<CollisionBoxRecord> droppingPuppetBox{TableId::MandaBossDroppingPuppetBox};

// the colour of bone k (exe 0x5e1810: a bone past the actor's blocks reads through null in the exe)
BoneColour boneColour(AnimatedActor& a, uint32_t k) {
    BoneBlock* b = boneBlockOf(a, int(k >> 2));
    if (b == nullptr) notPorted(kStoryPort, "exe 0x5e1810: bone past the bone-block mask");
    return b->colour(k & 3);
}
// the dust's ticks of the fade (bit 31 of 0x8212c0 << the counter)
bool fadeDustTick(int8_t counter) { return int32_t(0x8212c0u << (uint8_t(counter) & 0x1f)) < 0; }
// exe 0x488a10: four skid sparks of dust around the puppet (at its position << the stage shift)
void puppetDust(const AnimatedActor& a, uint16_t base, uint8_t variant) {
    for (int k = 3; k >= 0; --k) {
        const uint32_t address = allocActorA0(0); // exe 0x5e13b0
        if (!address) continue;
        EffectActor& e = effectActorAt(address);
        const uint32_t shift = stage().coordinateShift & 0x1f;
        e.flags = kActorLiveDrawn;
        e.type = typeByte(GlobalEffectType::SkidSparks);
        e.variant = variant;
        e.pos.x = int16_t(uint32_t(uint16_t(a.pos.x.whole)) << shift);
        e.pos.y = int16_t(uint32_t(uint16_t(a.pos.y.whole)) << shift);
        e.pos.z = int16_t(uint32_t(uint16_t(a.pos.z.whole)) << shift);
        classState<SkidSparkFields>(e).yaw = uint16_t(uint32_t(k << 10) + base);
    }
}
// exe 0x488990: a tick of the fade out (each bone from its threshold); true at its end (hidden)
bool puppetFade(AnimatedActor& a, PuppetFields& f) {
    const int n = f.fadeCounter;
    a.tint = 0;
    for (uint32_t k = 0; k < kPuppetFadeBones; ++k) {
        BoneColour colour = boneColour(a, k);
        const int t = puppetFadeThresholds[k];
        if (t < n) {
            colour = kTintNeutral;
        } else {
            int v = (n - t) + 0x10;
            if (v < 0) v = 0;
            colour = uint16_t(((v << 5 | v) << 5) | v);
        }
    }
    f.fadeCounter = int8_t(n - 1);
    if (n - 1 < 0) {
        a.flags &= uint8_t(~kActorDrawn);
        return true;
    }
    return false;
}
// the velocity += the acceleration, the move along the yaw (exe 0x5e36f0), the animation
void puppetMove(AnimatedActor& a, PuppetFields& f) {
    a.velocity.y = int16_t(uint32_t(uint16_t(a.velocity.y)) + uint16_t(a.acceleration.y));
    a.velocity.z = int16_t(uint32_t(uint16_t(a.velocity.z)) + uint16_t(a.acceleration.z));
    a.velocity.x = int16_t(uint32_t(uint16_t(a.velocity.x)) + uint16_t(a.acceleration.x));
    moveAlongYaw(a.pos, a.rotation.y, a.velocity.x, a.velocity.y, a.velocity.z);
    if (a.anim == a.prevAnim) animStepPose(a);    // exe 0x5e1e80
    else animSetPose(a, a.anim, int(f.startKey)); // exe 0x5e1c30
}
enum class RisingStep : uint8_t { Rise = 0, Slow = 1, WaitRoar = 2, Roar = 3, WaitTurn = 4, WaitFade = 5, Fade = 6 };
// exe 0x4886d0 (states [0x892310]): rises out of the floor, plays its animations on script 0x1a's ticks (step 3 tick
// 0x122, tick 0x2ee, step 4 tick 0x1e: the sound), then fades out bone by bone with dust, hidden and freed
void risingPuppetUpdate(AnimatedActor& a) {
    auto&& f = classState<PuppetFields>(a);
    switch (PuppetState(a.state)) {
    case PuppetState::SetUp: // exe 0x4886f0
        if (!bindActorModel(a, 0)) { // exe 0x5e0ba0
            freeActor(a);
            return;
        }
        a.clearClassBlock();
        a.collisionBox.pointAt(risingPuppetBox.stateAddress());
        a.velocity.clearWords();
        a.acceleration.clearWords();
        a.anim = 0x18;
        f.startKey = 0;
        animSetPose(a, 0x18, 0);
        a.subState = 0;
        ++a.state;
        return;
    case PuppetState::Act: { // exe 0x488760
        a.savePreviousPose();
        a.prevAnim = a.anim;
        const ScriptState& s = scriptState();
        const uint8_t st = a.subState;
        switch (RisingStep(st)) {
        case RisingStep::Rise:
            if (a.pos.y.whole < -0x128) {
                a.velocity.y = 0x40;
            } else {
                a.acceleration.y = -4;
                a.subState = uint8_t(st + 1u);
            }
            break;
        case RisingStep::Slow:
            if (a.velocity.y == 0) {
                a.acceleration.y = 0;
                a.subState = uint8_t(st + 1u);
            }
            break;
        case RisingStep::WaitRoar:
            if (s.stepSub == 3 && uint32_t(s.stepTick) == 0x122) {
                a.anim = 0x19;
                a.subState = uint8_t(st + 1u);
            }
            break;
        case RisingStep::Roar:
            if (a.animEnded()) {
                a.anim = 0x18;
                a.subState = uint8_t(st + 1u);
            }
            break;
        case RisingStep::WaitTurn:
            if (uint32_t(s.stepTick) == 0x2ee) {
                a.anim = 0x1a;
                a.subState = uint8_t(st + 1u);
            }
            break;
        case RisingStep::WaitFade:
            if (s.stepSub == 4 && uint32_t(s.stepTick) == 0x1e) {
                soundPlayAtPos(kSoundPuppetFade, a.pos);
                f.fadeCounter = kPuppetFadeTicks;                   // exe 0x488970
                a.semiTransparency = ((1 & 1) << 4 | (1 & 3)) << 1; // exe 0x5e1a00(actor, 1, 1)
                f.dustPhase = 0;
                ++a.subState;
            }
            break;
        case RisingStep::Fade:
            if (fadeDustTick(f.fadeCounter)) {
                puppetDust(a, uint16_t(uint32_t(f.dustPhase) << 8), kPuppetDustVariant);
                ++f.dustPhase;
            }
            if (puppetFade(a, f)) {
                a.subState = 0;
                ++a.state;
            }
            break;
        default: break;
        }
        puppetMove(a, f);
        return;
    }
    case PuppetState::Free: freeActor(a); return; // exe 0x468fa0
    default: notPortedAt(kStoryPort, "0x4886d0 state", a.state);
    }
}
enum class DroppingStep : uint8_t { Fall = 0, Shake = 1, WaitTurn = 2, Turn = 3 };
// exe 0x492050 (the ST14 table type 0x2b sub 1, states [0x892a68]): the guardian of script 0x1a drops onto its ground
// (a shake and the pad's vibration, animation 10); at the script's step 6 animation 0x1e
void droppingPuppetUpdate(const PlayerEnv& env, AnimatedActor& a) {
    auto&& f = classState<PuppetFields>(a);
    switch (PuppetState(a.state)) {
    case PuppetState::SetUp: // exe 0x492070
        if (!bindActorModel(a, 0)) {
            freeActor(a);
            return;
        }
        a.clearClassBlock();
        a.velocity.clearWords();
        a.acceleration.clearWords();
        a.collisionBox.pointAt(droppingPuppetBox.stateAddress());
        a.airborne = 1;
        f.ground = int16_t(actorGround(env, a)); // exe 0x40f190
        a.anim = 9;
        f.startKey = 0;
        animSetPose(a, 9, 0);
        a.subState = 0;
        ++a.state;
        return;
    case PuppetState::Act: { // exe 0x4920f0
        a.savePreviousPose();
        const uint8_t st = a.subState;
        a.prevAnim = a.anim;
        bool next = false;
        switch (DroppingStep(st)) {
        case DroppingStep::Fall:
            a.velocity.y = int16_t(uint32_t(uint16_t(a.velocity.y)) + 0x30u);
            if (a.pos.y.whole >= f.ground) {
                cameraShake(0, 0x1000, 0x80); // exe 0x40ea80
                padVibrate(0, 0x10210bd);     // exe 0x55cd10
                a.velocity.y = 0;
                a.pos.y.whole = f.ground;
                a.airborne = 0;
                a.anim = 10;
                f.timer = 0x30;
                next = true;
            }
            break;
        case DroppingStep::Shake:
            f.timer = int16_t(uint32_t(uint16_t(f.timer)) - 1u);
            if (f.timer == 0) {
                padVibrate(0, 0x1000000);
                next = true;
            }
            break;
        case DroppingStep::WaitTurn:
            if (scriptState().stepSub == 6) {
                a.anim = 0x1e;
                next = true;
            }
            break;
        case DroppingStep::Turn: next = a.animEnded(); break;
        default: break;
        }
        if (next) a.subState = uint8_t(st + 1u);
        puppetMove(a, f);
        return;
    }
    case PuppetState::Free: freeActor(a); return; // exe 0x468fa0
    default: notPortedAt(kStoryPort, "0x492050 state", a.state);
    }
}

constexpr uint8_t kFloaterType = 0x3e, kRollerType = 0x35, kGuardianType = 0x2b;
constexpr uint8_t kRespawnTicks = 0xf0;
constexpr uint32_t kFlagVariantGuardianBeaten = 0x170;

// the floater (pool 0x20 type 0x3e) of exe 0x423190 / 0x423390
void spawnFloater(ControllerActor& c, FightFields& fight) {
    const uint32_t address = allocActor20(); // exe 0x5e1160
    pointAtWord(fight.floater, address);
    if (!address) return;
    AnimatedActor& f = animatedActorAt(address);
    f.flags = kActorLiveDrawn;
    f.type = kFloaterType;
    f.subtype = 0;
    f.variant = 0;
    f.spawnByte = c.spawnByte;
    f.loopFlags |= kActorSweepGroup2;
    f.setParamsWord(0);
    f.pos.x.whole = 0x200;
    f.pos.y.whole = -0x1000;
    f.pos.z.whole = 0;
    f.rotation.y = 0;
    classState<FloaterFields>(f).controller = &c;
}
// a roller k of exe 0x423190 (the first ones, stacked) / exe 0x423420 (a respawn)
void spawnRoller(ControllerActor& c, int k, bool again) {
    const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!address) return;
    StaticActor& e = staticActorAt(address);
    e.flags = kActorLiveDrawn;
    e.type = kRollerType;
    e.subtype = 0;
    e.variant = uint8_t(k);
    e.spawnByte = c.spawnByte;
    e.loopFlags |= kActorSweepGroup2;
    e.setParamsWord(again ? 1u : 0u);
    e.pos.x.whole = 0x200;
    e.pos.y.whole = again ? int16_t(0x40) : int16_t(0x20 - (k << 5));
    e.pos.z.whole = 0;
    e.rotation.y = int16_t(k << 10);
    classState<st43_roller_state::RollerFields>(e).spawner = &c; // the roller class's own state
}
// exe 0x423190: the four rollers, the floater, the guardian
void fightSetUp(ControllerActor& c, FightFields& fight) {
    for (int k = 0; k < 4; ++k) spawnRoller(c, k, false);
    spawnFloater(c, fight);
    const uint32_t address = allocActor20();
    pointAtWord(fight.guardian, address);
    if (address) {
        AnimatedActor& g = animatedActorAt(address);
        auto&& gf = classState<GuardianFields>(g);
        g.flags = kActorLiveDrawn;
        g.type = kGuardianType;
        g.variant = 0;
        g.spawnByte = c.spawnByte;
        g.loopFlags |= kActorSweepGroup2;
        g.setParamsWord(0);
        g.pos.x.whole = 0x600;
        g.pos.y.whole = -0x180;
        g.pos.z.whole = -0x400;
        g.rotation.y = 0x800;
        gf.controller = &c;
        gf.target = fight.floater;
    }
    fight.bits = kFightAlive;
    fight.floaterTimer = kRespawnTicks;
    fight.rollerTimer = kRespawnTicks;
    ++c.state;
}
// exe 0x4232f0: the floater / the first missing roller respawned after 0xf0 ticks; a variant guardian beaten: the
// flag, the end
void fightTick(ControllerActor& c, FightFields& fight) {
    if (!(fight.bits & kFightFloaterAlive)) {
        const uint8_t t = uint8_t(fight.floaterTimer - 1u);
        fight.floaterTimer = t;
        if (t == 0) { // exe 0x423390
            spawnFloater(c, fight);
            if (fight.floater) classState<GuardianFields>(animatedPool().of(*fight.guardian)).target = fight.floater;
            fight.floaterTimer = kRespawnTicks;
            fight.bits = uint8_t(fight.bits | kFightFloaterAlive);
        }
    }
    for (int k = 0; k < 4; ++k) {
        if (int32_t(int8_t(fight.bits)) & (1 << (k + 4))) continue;
        const uint8_t t = uint8_t(fight.rollerTimer - 1u);
        fight.rollerTimer = t;
        if (t == 0) { // exe 0x423420
            spawnRoller(c, k, true);
            fight.rollerTimer = kRespawnTicks;
            fight.bits = uint8_t(fight.bits | uint8_t(kFightRoller0 << k));
        }
        break;
    }
    if (c.spawnByte != 0 && (fight.bits & kFightVariantDied)) {
        setStoryFlag(kFlagVariantGuardianBeaten);
        ++c.state;
    }
}
// exe 0x423170
void fightControllerUpdate(ControllerActor& c) {
    auto&& fight = classState<FightFields>(c);
    switch (FightState(c.state)) {
    case FightState::SetUp: fightSetUp(c, fight); break;
    case FightState::Run: fightTick(c, fight); break;
    case FightState::End: // exe 0x423370
        if ((fight.bits & kFightAlive) == 0) freeActor(c); // exe 0x5e15c0
        break;
    default: notPortedAt(kStoryPort, "0x423170 state", c.state);
    }
}

// ---- the beam (exe 0x5aad40) -------------------------------------------------------------------------------------
StaticActor& segmentAt(const BeamHeadFields& head, int k) {
    return pointee(head.segmentSlots[size_t(k)], "the beam's segment not spawned (the exe reads address 0)");
}
// exe 0x5ab820 (exe 0x5ab780 also marks this one the tip and the previous one not): the segment at the end of the
// previous one -- its previous position + its matrix * (0, 0, -length), its previous angles
void segmentFollow(StaticActor& s) {
    BeamSegmentFields& f = classState<BeamSegmentFields>(s);
    StaticActor& p = pointee(f.previous, "exe 0x5ab820 with no previous segment");
    const int z = -int(f.length);
    const Matrix3 m = p.rootMatrix;
    int16_t o[3];
    for (uint32_t i = 0; i < 3; ++i) o[i] = int16_t((m.e[i * 3 + 2] * z) >> 12); // exe 0x40bb30 on (0, 0, z)
    s.pos.x.whole = int16_t(uint32_t(uint16_t(p.prevPos.x.whole)) + uint16_t(o[0]));
    s.pos.y.whole = int16_t(uint32_t(uint16_t(p.prevPos.y.whole)) + uint16_t(o[1]));
    s.pos.z.whole = int16_t(uint32_t(uint16_t(p.prevPos.z.whole)) + uint16_t(o[2]));
    s.rotation.x = p.prevRotation.x;
    s.rotation.y = p.prevRotation.y;
    s.rotation.z = p.prevRotation.z;
}
// exe 0x5ab780: the new tip
void segmentTip(StaticActor& s) {
    segmentFollow(s);
    BeamSegmentFields& f = classState<BeamSegmentFields>(s);
    StaticActor& p = *f.previous;
    f.segmentFlagsByte = uint8_t(f.segmentFlagsByte | kSegmentTip);
    f.cornersLink = &p; // (the exe: the address of its projected corners, its +0xa4)
    BeamSegmentFields& pf = classState<BeamSegmentFields>(p);
    pf.segmentFlagsByte = uint8_t(pf.segmentFlagsByte & ~kSegmentTip);
}
// exe 0x5ab210: a segment's hit entry (the floater carried at the tip), its matrix (the rest is the draw)
void beamSegment(StaticActor& s) {
    if (s.state == 0) return;
    BeamSegmentFields& f = classState<BeamSegmentFields>(s);
    s.savePreviousPose();
    uint32_t r;
    if (!(f.segmentFlagsByte & kSegmentShort)) {
        r = 0x48;
        f.width = int16_t(beamWidths[size_t(int32_t(f.segments))]);
    } else {
        r = 0x3c;
        f.width = int16_t(shortBeamWidths[size_t(int32_t(f.segments))]);
    }
    f.hitBox.result = 0;
    f.hitBox.previousX = s.prevPos.x.whole;
    f.hitBox.previousZ = s.prevPos.z.whole;
    const int16_t p[3] = {s.pos.x.whole, s.pos.y.whole, s.pos.z.whole};
    hitListSubmit(p, f.hitBox, hitOwner(s.slot) | hitGroup(kHitGroupBeam) | kHitNoLockOn | hitRadius(r),
                  kHitAttackStrong | kHitAttackBump | 8u);
    AnimatedActor& floater = pointee(f.floater, "exe 0x5ab210 with no floater (the exe reads address 0xd)");
    if (classState<FloaterFields>(floater).caught != 0 && (f.segmentFlagsByte & kSegmentTip)) floater.pos = s.pos;
    storeRotationYXZ(s.rotation.x, s.rotation.y, s.rotation.z, s.rootMatrix, f.matrixPad); // exe 0x5ab320's matrix
}
// exe 0x5aad60: the head -- the first segment at it along its sway pattern (states 1..3); the segments spawned, grown,
// held, shrunk; done
void beamHead(StaticActor& h) {
    BeamHeadFields& b = classState<BeamHeadFields>(h);
    const uint8_t st = h.state;
    if (st != 0 && st <= 3) {
        StaticActor& s0 = segmentAt(b, 0);
        s0.pos = h.pos;
        const int16_t yaw = h.rotation.y;
        int d = int(b.aimYaw) - yaw;
        if (d > 0x7ff) d -= 0x1000;
        if (d < -0x800) d += 0x1000;
        if (d > 0x100) b.aimYaw = int16_t(yaw + 0x100);
        else if (d < -0x100) b.aimYaw = int16_t(yaw - 0x100);
        if (int(b.aimPitch) > int(b.pitchOffset) + 0x84) b.aimPitch = int16_t(uint32_t(uint16_t(b.pitchOffset)) + 0x84u);
        if (b.aimPitch < -0x180) b.aimPitch = -0x180;
        const int c = int8_t(b.tick);
        const uint8_t dd = b.tick;
        switch (b.pattern) {
        case 0: // swaying right
            s0.rotation.x = b.aimPitch;
            s0.rotation.y = int16_t(uint16_t(int16_t(cosineAt(8 + c) >> 4) + yaw));
            s0.rotation.z = h.rotation.z;
            break;
        case 1: // swaying left
            s0.rotation.x = b.aimPitch;
            s0.rotation.y = int16_t(uint16_t(yaw - int16_t(cosineAt(8 + c) >> 4)));
            s0.rotation.z = h.rotation.z;
            break;
        case 2: // nodding at the aim
            s0.rotation.x = int16_t(uint16_t(int16_t(cosineAt((dd & 0xf) * 4) >> 6) + b.aimPitch));
            s0.rotation.y = b.aimYaw;
            s0.rotation.z = h.rotation.z;
            break;
        case 3: // shaking at the aim
            s0.rotation.x = b.aimPitch;
            s0.rotation.y = int16_t(uint16_t(int16_t(cosineAt((dd & 3) * 0x10) >> 7) + b.aimYaw));
            s0.rotation.z = h.rotation.z;
            break;
        case 4: // a wide sweep right
            s0.rotation.x = b.aimPitch;
            s0.rotation.y = int16_t(uint16_t(int16_t(cosineAt((dd * 2 + 8) & 0x3f) >> 3) + yaw));
            s0.rotation.z = h.rotation.z;
            break;
        case 5: // a wide sweep left
            s0.rotation.x = b.aimPitch;
            s0.rotation.y = int16_t(uint16_t(yaw - int16_t(cosineAt((dd * 2 + 8) & 0x3f) >> 3)));
            s0.rotation.z = h.rotation.z;
            break;
        default: break;
        }
        ++b.tick;
    }
    switch (BeamState(st)) {
    case BeamState::Spawn: {
        b.headFlags = 0;
        for (uint32_t k = 0; k < kBeamSegments; ++k) {
            const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
            if (!address) continue;
            StaticActor& e = staticActorAt(address);
            b.segmentSlots[k] = &e;
            BeamSegmentFields& sf = classState<BeamSegmentFields>(e);
            e.flags = kActorLive;
            e.type = kBeamType;
            e.variant = uint8_t(k + 1);
            e.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
            e.setParamsWord(0);
            e.enterState(0);
            sf.segmentFlags = 0;
            sf.floater = b.target;
            if (b.pattern == 1) sf.segmentFlags = kSegmentShort;
        }
        if (b.pattern == 0) {
            b.pattern = int8_t(rngStep() & 3);
        } else if (b.pattern == 1) {
            b.headFlagsByte = uint8_t(b.headFlagsByte | kBeamShortSegments);
            b.pattern = int8_t((rngStep() & 1) + 4u);
        }
        b.tick = 0;
        b.segments = 0;
        b.done = 0;
        h.state = uint8_t(st + 1u);
        return;
    }
    case BeamState::Grow: {
        const int f = b.segments;
        if (f > 0xf) {
            b.holdTicks = 0x10;
            h.state = uint8_t(st + 1u);
            return;
        }
        StaticActor& s = segmentAt(b, f);
        BeamSegmentFields& sf = classState<BeamSegmentFields>(s);
        if (!(b.headFlags & kBeamShortSegments)) sf.length = int16_t(b.pattern > 1 ? 0x90 : 0xa4);
        else sf.length = 0x40;
        ++s.state;
        if (f == 0) {
            sf.segmentFlagsByte = uint8_t(sf.segmentFlagsByte | kSegmentTip);
        } else {
            sf.previous = &segmentAt(b, f - 1);
            segmentTip(s);
        }
        const int n = int8_t(f + 1);
        b.segments = int8_t(n);
        for (int i = 1; i < n - 2; ++i) segmentFollow(segmentAt(b, i));
        for (int i = 0; i < n; ++i) ++classState<BeamSegmentFields>(segmentAt(b, i)).segments;
        return;
    }
    case BeamState::Hold:
        b.holdTicks = int16_t(uint32_t(uint16_t(b.holdTicks)) - 1u);
        if (b.holdTicks == 0) h.state = uint8_t(st + 1u);
        for (int i = 1; i < int(kBeamSegments); ++i) segmentFollow(segmentAt(b, i));
        return;
    case BeamState::Shrink: {
        const int f = int8_t(b.segments - 1);
        b.segments = int8_t(f);
        freeActor(segmentAt(b, f)); // exe 0x5e1540
        if (f == 0) {
            h.state = uint8_t(st + 1u);
            return;
        }
        for (int i = 1; i < f - 1; ++i) segmentFollow(segmentAt(b, i));
        BeamSegmentFields& last = classState<BeamSegmentFields>(segmentAt(b, f - 1));
        last.segmentFlagsByte = uint8_t(last.segmentFlagsByte | kSegmentTip);
        for (int i = 0; i < f; ++i) --classState<BeamSegmentFields>(segmentAt(b, i)).segments;
        return;
    }
    case BeamState::Done: {
        b.done = 1;
        FloaterFields& floater =
            classState<FloaterFields>(pointee(b.target, "exe 0x5aad60 with no floater (the exe reads address 0xd)"));
        if (floater.caught != 0) floater.released = 1;
        return;
    }
    default: return;
    }
}
// exe 0x5aad40
void beamUpdate(StaticActor& a) {
    if (a.variant == 0) beamHead(a);
    else beamSegment(a);
}

// ---- the debris (pool 0x40, ST14 type 0x12, exe 0x5c7020; states [0x8d1308]) -----------------------------------------
// Flies along its rotation, homes on the player's chest after 0x1e ticks (yaw / pitch steps of 0x10), 400 ticks; a hit,
// a wall or the player's shield ends it (state 2: the burst, sound 0x89, 6 ticks)
enum class DebrisState : uint8_t { Launch = 0, Fly = 1, Burst = 2 }; // exe 0x5c7060 / 0x5c70f0 / 0x5c7280
constexpr uint32_t kDebrisHoming = 0x1;
constexpr int16_t kDebrisHomeAfter = 0x172; // it homes once its timer is below this
constexpr uint32_t kDebrisHitGroup = 5;
const DataTable<CollisionBoxRecord> debrisBox{TableId::MandaBossDebrisBox};
constexpr uint16_t kSoundDebrisBurst = 0x89;

// exe 0x5c7320: its sprite (a billboard), brighter when the player is near
void debrisSprite(const ProjectileActor& a, const DebrisFields& f) {
    const int d = f.playerDistance;
    const uint32_t v = d < 0x600 ? 0x80u : d < 0xe00 ? uint32_t(0x80 - ((d - 0x600) >> 4)) : 0u;
    const uint32_t c = f.column;
    dc::Billboard bb;
    bb.colour = dc::billboardColour(v | v << 8 | v << 16);
    bb.xy = dc::packXY(a.pos.x.whole, a.pos.y.whole);
    bb.z = dc::zWord(a.pos.z.whole);
    bb.clut = 0x7dd3;
    bb.tpage = 0x2e;
    bb.uv[0] = dc::texel(c, 0xc0);
    bb.uv[1] = dc::texel(c + 0x1f, 0xc0);
    bb.uv[2] = dc::texel(c, 0xdf);
    bb.uv[3] = dc::texel(c + 0x1f, 0xdf);
    bb.sizeRotation = 0x30;
    dc::emit(bb, a.at);
}
// the player's chest (a 16.16 point 0x80 above its position)
void playerChest(int32_t out[3]) {
    const Player& p = player();
    out[0] = p.pos.x;
    out[1] = int32_t(uint32_t(p.pos.y) - 0x800000u);
    out[2] = p.pos.z;
}
// exe 0x5c7020
void debrisUpdate(const PlayerEnv& env, ProjectileActor& a) {
    auto&& f = classState<DebrisFields>(a);
    switch (DebrisState(a.state)) {
    case DebrisState::Launch: { // exe 0x5c7060
        int32_t chest[3];
        playerChest(chest);
        f.playerDistance = planarDistance(a.pos, chest);
        f.hitBox.result = 0;
        f.timer = 400;
        f.burstTicks = 0;
        f.launchSpeed = a.velocity.z;
        f.column = 0;
        a.collisionBox.pointAt(debrisBox.stateAddress());
        debrisSprite(a, f);
        a.state = uint8_t(DebrisState::Fly);
        break;
    }
    case DebrisState::Fly: { // exe 0x5c70f0
        int32_t chest[3];
        playerChest(chest);
        a.savePreviousPose();
        f.playerDistance = planarDistance(a.pos, chest);
        f.playerYaw = int16_t(angleOf(chest[0], chest[2], a.pos.x, a.pos.z));
        const int32_t at[3] = {a.pos.x, a.pos.y, a.pos.z};
        f.playerPitch = int16_t(0x800 - pitchOf(chest, at));
        if (f.timer < kDebrisHomeAfter) f.flags = f.flags | kDebrisHoming;
        else f.flags = f.flags & ~kDebrisHoming;
        if (f.flags & kDebrisHoming) { // exe 0x5c72e0
            const int d = angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(int32_t(f.playerYaw)));
            if (0x10 <= d) a.rotation.y = int16_t(a.rotation.y + 0x10); // exe 0x5e3db0(actor, yaw, 0x10)
            if (d <= -0x10) a.rotation.y = int16_t(a.rotation.y - 0x10);
            const int p = a.rotation.x, t = f.playerPitch;
            a.rotation.x = int16_t(t != p && t - p >= 0 ? p + 0x10 : p - 0x10);
        }
        moveAlongRotation(a.pos, a.rotation, 0, a.velocity.y, a.velocity.z); // exe 0x5e37b0
        f.timer = int16_t(uint32_t(uint16_t(f.timer)) - 1u);
        if (f.timer == 0) a.state = uint8_t(DebrisState::Burst);
        debrisSprite(a, f);
        if (playerShieldBlocks(a, 0x30)) { // exe 0x5737b0
            a.state = uint8_t(DebrisState::Burst);
            break;
        }
        const uint32_t r = f.hitBox.result;
        if (r & (f.timer > kDebrisHomeAfter ? (kHitAttackBump | kHitAttackByPlayer)
                                            : (kHitAttackBump | kHitAttackByPlayer | kHitAttackDeflects)))
            a.state = uint8_t(DebrisState::Burst);
        if (int32_t(actorTestMove(env, a)) < 0) a.state = uint8_t(DebrisState::Burst); // exe 0x40f570
        f.hitBox.result = 0;
        f.hitBox.previousX = a.prevPos.x.whole;
        f.hitBox.previousZ = a.prevPos.z.whole;
        const int16_t p[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
        hitListSubmit(p, f.hitBox, hitOwner(a.slot) | hitGroup(kDebrisHitGroup) | kHitNoLockOn | hitRadius(0x30),
                      (1u << (31 - kHitGroupBodies)) | kHitAttackStrong | 8u); // immune to the bodies' group
        break;
    }
    case DebrisState::Burst: // exe 0x5c7280
        if (f.burstTicks == 0) soundPlayAtPos(kSoundDebrisBurst, a.pos);
        f.burstTicks = int16_t(uint32_t(uint16_t(f.burstTicks)) + 1u);
        f.column = uint8_t(f.column + 0x20u);
        debrisSprite(a, f);
        if (f.burstTicks > 6) freeActor(a); // exe 0x5e1500
        break;
    default: notPortedAt(kStoryPort, "0x5c7020 state", a.state);
    }
    // the shadow exe 0x5f20f0 at its height - 0x100 (render output)
}

} // namespace

void st14GuardianUpdate(const PlayerEnv& env, uint32_t actor) { guardianUpdate(env, animatedActorAt(actor)); }
void st14FloaterUpdate(const PlayerEnv& env, uint32_t actor) { floaterUpdate(env, animatedActorAt(actor)); }
void st14RisingPuppetUpdate(const PlayerEnv&, uint32_t actor) { risingPuppetUpdate(animatedActorAt(actor)); }
void st14DroppingPuppetUpdate(const PlayerEnv& env, uint32_t actor) {
    droppingPuppetUpdate(env, animatedActorAt(actor));
}
void st14ControllerUpdate(const PlayerEnv&, uint32_t actor) { fightControllerUpdate(controllerActorAt(actor)); }
void st14BeamUpdate(const PlayerEnv&, uint32_t actor) { beamUpdate(staticActorAt(actor)); }
void st14DebrisUpdate(const PlayerEnv& env, uint32_t actor) { debrisUpdate(env, projectileActorAt(actor)); }

} // namespace od::port
