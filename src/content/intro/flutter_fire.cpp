// The fires of the Flutter's fire mission (ST1E): a fire burns with its flame tongues, crackles, warms the player and
// throws embers; the extinguisher's water (or a shot) lowers its strength until it is out -- the flames go, smoke
// puffs rise, then a smoke column that dies out with it. Putting out the fire in front of the kitchen wall makes the
// wall burst (the wall blast: a flash, debris, the wall's segment broken open, a fire that swings embers). The
// mission's effects (the heat shimmer, the smoke puffs, embers, debris, the blast's flash, the flame on Data) are one
// stage effect class.
#include "content/intro/fire_mission.hpp"

#include "engine/data_source.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/camera.hpp"
#include "game/distances.hpp"
#include "game/field_alias.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/geometry_scratch.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/lock_on_targets.hpp"
#include "game/net_world.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/smoke_column.hpp"
#include "game/table_records.hpp"

namespace od::port {

namespace {

inline constexpr const char* kFireArea = "flutter fire";

// ---- the flame tongues ---------------------------------------------------------------------------------------------
const DataTable<ScalarTable<uint16_t, 4>> tongueHeights{TableId::FlutterFireTongueHeights}; // a new tongue's height by variant
const DataTable<ScalarTable<int16_t, 4>> tongueRise{TableId::FlutterFireTongueRise}; // the tongues' rise a tick by variant
const DataTable<ScalarTable<uint16_t, 4>> emitPeriods{TableId::FlutterFireEmitPeriods}; // the ticks between two tongues by variant
constexpr uint8_t kFlickerFlips = 0x1f;              // the tall flame's flicker phase flips after this many ticks

// ---- the fire and the wall blast -----------------------------------------------------------------------------------
// the fire's spawn parameters: its index, its attack, its flags
struct FireParams {
    explicit FireParams(const StaticActor& actor) : FireParams(const_cast<StaticActor&>(actor)) {}
    explicit FireParams(StaticActor& actor) : index(actor.params[0]), attack(actor.params[1]), flags(actor.params[2]) {}
    uint8_t& index;
    uint8_t& attack;
    uint8_t& flags;
};
constexpr uint8_t kFireTallFlame = 0x01; // its flame is the tall variant (1, flickering by its index), else 3
constexpr uint8_t kFireSpecial = 0x02;   // the fire in front of the wall (its end cues the wall blast)
constexpr uint8_t kFireEmbers = 0x04;    // throws embers
// the wall blast's: its attack and its debris thrown so far
struct WallBlastParams {
    explicit WallBlastParams(const StaticActor& actor) : WallBlastParams(const_cast<StaticActor&>(actor)) {}
    explicit WallBlastParams(StaticActor& actor) : attack(actor.params[1]), debrisThrown(actor.params[2]) {}
    uint8_t& attack;
    FieldAlias<int8_t, uint8_t> debrisThrown;
};

// the put-out steps (+0xa)
enum class PutOutStep : uint8_t { Start = 0, Smoke = 1, Column = 2 };

// the point the embers fly toward (near the camera's side of the kitchen)
const DataTable<ShortPoint> emberTarget{TableId::FlutterFireEmberTarget}; // exe data 0x831998

constexpr uint8_t kFlameType = 0x3e;
constexpr uint8_t kFlameShortVariant = 3, kFlameTallVariant = 1, kFlickerVariant = 0;
constexpr int32_t kOutStrength = 0x200; // put out at this strength or below
constexpr int32_t kRegrowth = 0x10;     // the strength back a tick while untouched
constexpr uint16_t kFlickerTicks = 8;
constexpr int32_t kEmberStrength = 0xc00; // embers only above this strength
constexpr uint8_t kCrackleTicks = 0x21, kBlastCrackleTicks = 0x1c;
constexpr uint16_t kCrackleSound = 0x152, kPutOutSound = 0x153, kBlastSound = 0x8b, kBlastCrackleSound = 0x158;
constexpr uint16_t kColumnTicks = 0x1d0; // the smoke column lives this long after the fire is out
constexpr uint8_t kSmokePuffs = 3;
constexpr int32_t kBlastStrength = 0x1000;
constexpr int kBlastDebris = 16;
constexpr int8_t kBlastExtraDebris = 9;
constexpr uint32_t kBlastShake = 0x6000;
constexpr uint16_t kBlastShakeDecay = 0x800;
constexpr int kBlastHitReach = 0x20;
constexpr uint16_t kEmberTicks = 4;
constexpr uint32_t kFireAttackFlags = kHitAttackHarmless | kHitAttackThrown | kHitAttackSolid;

// The lanes of the geometry scratch the exe leaves its hit points and offsets in (+0x60.., kept written; +0xa0..: the
// fire's own)
struct FireScratch {
    LaneVec3 hitPoint = geometryScratch().vec3(0x60);
    Lane16 offsetX = geometryScratch().lane(0x68);
    Lane16 offsetZ = geometryScratch().lane(0x6c);
    std::array<Lane32, 3> launch = geometryScratch().words<3>(0xa0);
};
// the scratch's one instance
FireScratch& scratch() {
    static FireScratch s;
    return s;
}

// the previous pose and contact bytes kept (the top of both classes)
void savePrevious(StaticActor& actor) {
    actor.savePreviousPose();
    actor.savePreviousContact();
}

// exe 0x5aea40: the hit's damage on the strength -- the water does more the closer the player stands; 1 = hit, 0 =
// untouched, -1 = put out. Netplay: the fire is the host's -- a client sends its hit and takes the host's strength;
// the host adds the clients' hits and sends the strength each hit leaves
int fireDamage(StaticActor& actor, BurningFields& fire) {
    const uint32_t v = fire.hitBox.result;
    int d = 0;
    if (v & (kHitAttackByPlayer | kHitAttackWater)) {
        d = int(v & kHitDamageMask) * 0x20;
        if (v & kHitAttackWater) {
            int dist = distance3(player().pos, actor.pos);
            if (dist > 0x100)
                dist = 0x100;
            else if (dist < 0x80)
                dist = 0x80;
            d += ((((0x100 - dist) * 0x340) >> 7) + 0x3c0) >> 3;
        }
    }
    const NetWorldHooks& net = netWorldHooks();
    const int16_t x = actor.pos.x.whole, z = actor.pos.z.whole;
    if (net.mirror) {
        if (d != 0 && net.fireHit) net.fireHit(x, z, d);
        const std::optional<int32_t> host = takeFireStep(x, z);
        if (!host) return 0;
        fire.strength = *host;
        if (*host > kOutStrength) return 1;
        fire.strength = 0;
        return -1;
    }
    d += takeFireHits(x, z);
    if (d == 0) return 0;
    const int32_t s = fire.strength - d;
    fire.strength = s;
    int r = 1;
    if (s <= kOutStrength) {
        fire.strength = 0;
        r = -1;
    }
    if (net.fireChanged) net.fireChanged({x, z, fire.strength});
    return r;
}
// exe 0x5aeb90: the sprite's frame (the sprite: a draw command, render output)
void stepFrame(const StaticActor& actor, BurningFields& fire) {
    const uint8_t t = uint8_t(fire.frameTicks - 1);
    fire.frameTicks = t;
    if (t != 0) return;
    const uint32_t v = actor.variant;
    if (fireFrames[v * kFireFramesPerSize + fire.frame].next == -1)
        fire.frame = 0;
    else
        ++fire.frame;
    fire.frameTicks = fireFrames[v * kFireFramesPerSize + fire.frame].ticks;
}
// exe 0x5aeae0: the hit entry, raised by the strength (r = strength >> 5, or >> 6 for the big sizes; at least 0x40)
void submitFireHit(const StaticActor& actor, BurningFields& fire, const FireParams& params) {
    fire.hitBox.previousX = actor.prevPos.x.whole;
    fire.hitBox.previousZ = actor.prevPos.z.whole;
    fire.hitBox.result = 0;
    const int sh = (actor.variant != 0 ? 1 : 0) + 5;
    const uint32_t r = fire.strength < (0x40 << sh) ? 0x40u : uint32_t(fire.strength >> sh);
    auto&& point = scratch().hitPoint;
    point.x = actor.pos.x.whole;
    point.y = wrap16(actor.pos.y.whole - int16_t(uint16_t(r)));
    point.z = actor.pos.z.whole;
    const int16_t pos[3] = {point.x, point.y, point.z};
    hitListSubmit(pos, fire.hitBox, hitOwner(actor.slot) | hitRadius(r & 0xffff) | hitGroup(kHitGroupBodies),
                  uint32_t(params.attack) | kFireAttackFlags);
}
// exe 0x5aed20: the heat the player feels -- the mission's heat += (strength >> 2) - the distance when closer
void addHeat(const StaticActor& actor, const BurningFields& fire) {
    const uint32_t d = uint32_t(distance3(player().pos, actor.pos));
    const uint32_t q = uint32_t(fire.strength >> 2);
    if (d < q) fireMission().heat = fireMission().heat + (q - d);
}
// a flame (the stage's static type 0x3e) at the burning thing
uint32_t spawnFlame(const StaticActor& actor, uint8_t variant, uint8_t loopFlags) {
    const uint32_t f = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (f == 0) return 0;
    StaticActor& flame = staticActorAt(f);
    flame.flags = kActorLive;
    flame.type = kFlameType;
    flame.variant = variant;
    flame.loopFlags |= loopFlags;
    flame.pos = actor.pos;
    return f;
}
// the flame stops emitting (set by its fire)
void stopFlame(uint32_t flame) { classState<FlameFields>(staticActorAt(flame)).stopped = 1; }
// a hit lowers the strength (put out: back to the caller), an untouched fire grows back, the water makes the flicker
// flame; the flicker ends after kFlickerTicks
int burnTick(StaticActor& actor, BurningFields& fire) {
    const int r = fireDamage(actor, fire);
    if (r == 0) {
        const int32_t full = fire.fullStrength;
        if (full > fire.strength) {
            const int32_t n = fire.strength + kRegrowth;
            fire.strength = n;
            if (full < n) fire.strength = full;
        }
    } else if (r == 1 && (fire.hitBox.result & kHitAttackWater)) { // sprayed: a flicker flame
        fire.flickerTicks = 0;
        if (fire.flicker == 0) fire.flicker = spawnFlame(actor, kFlickerVariant, kActorLateLoop);
    }
    if (const uint32_t f = fire.flicker; f != 0) {
        ++fire.flickerTicks;
        if (fire.flickerTicks >= kFlickerTicks) {
            stopFlame(f);
            fire.flicker = 0;
        }
    }
    return r;
}

// exe 0x5ae8d0 / 0x5af1e0: put out -- the flames stop, smoke puffs rise, then a smoke column that ends kColumnTicks
// later with the fire
void putOut(StaticActor& actor, BurningFields& fire, bool withSound) {
    const PutOutStep step = PutOutStep(actor.step);
    if (step == PutOutStep::Column) {
        ++fire.timer;
        if (fire.timer < kColumnTicks) return;
        const uint32_t s = fire.flame;
        if (s == 0) notPorted(kFireArea, "a fire put out without its smoke column");
        ++classState<SmokeColumnFields>(staticActorAt(s)).ended;
        freeActor(actor);
        return;
    }
    if (actor.step > uint8_t(PutOutStep::Column)) return;
    if (step == PutOutStep::Start) {
        if (fire.flame) stopFlame(fire.flame);
        if (fire.flicker) stopFlame(fire.flicker);
        if (withSound) soundPlayAtPos(kPutOutSound, actor.pos);
        releaseLockOnTarget(actor.slot); // no hit entry from here on: the lock goes to the next fire
        fire.timer = 0;
        ++actor.step;
    }
    ++fire.timer;
    if (uint8_t(fire.timer) & 1) {
        if (const uint32_t e = allocActorA0(kActorStageClass)) { // a smoke puff
            EffectActor& puff = effectActorAt(e);
            puff.flags = kActorLive;
            puff.type = kFireEffectType;
            puff.subtype = uint8_t(FireEffectKind::SmokePuff);
            puff.variant = 2;
            const uint32_t r = rngStep();
            puff.pos.x = wrap16(actor.pos.x.whole - int(r & 0x3f) + 0x20);
            puff.pos.y = wrap16(actor.pos.y.whole - int((r >> 6) & 7));
            puff.pos.z = wrap16(actor.pos.z.whole - int((r >> 9) & 0x3f) + 0x20);
        }
        ++actor.stepCounter;
    }
    if (actor.stepCounter >= kSmokePuffs) {
        const uint32_t s = allocActor60(0); // exe 0x5e1300: the smoke column
        fire.flame = s;
        if (s) {
            StaticActor& column = staticActorAt(s);
            column.flags = kActorLive;
            column.type = typeByte(GlobalStaticType::SmokeColumn);
            column.loopFlags |= kActorLateLoop;
            column.pos = actor.pos;
        }
        ++actor.step;
        fire.timer = 0;
    }
}

// an ember thrown from the fire toward the camera's side of the kitchen (its launch speed lands it there)
void throwEmber(const StaticActor& actor) {
    const uint32_t e = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (e == 0) return;
    EffectActor& ember = effectActorAt(e);
    FireEffectFields& fx = classState<FireEffectFields>(effectActorAt(e));
    ember.flags = kActorLive;
    ember.type = kFireEffectType;
    ember.subtype = uint8_t(FireEffectKind::Ember);
    const uint32_t r1 = rngStep();
    const ShortPoint& et = *emberTarget;
    const int32_t target[3] = {int32_t(uint32_t(et.x - int32_t(r1 & 0xff) + 0x80) << 16),
                               int32_t(uint32_t(int32_t(et.y)) << 16),
                               int32_t(uint32_t(et.z - int32_t((r1 >> 8) & 0x7f) + 0x40) << 16)};
    const int ang = atanAngle(float(int32_t(uint32_t(actor.pos.x) - uint32_t(target[0]))),
                              float(int32_t(uint32_t(actor.pos.z) - uint32_t(target[2])))) &
                    kAngleMask; // exe 0x5e3bc0
    EmberParams(effectActorAt(e)).yaw = uint16_t(ang);
    int32_t o[3];
    angleRadiusVector(ang, 0x300, o); // exe 0x5e4070
    auto&& launch = scratch().launch;
    launch[0] = o[0];
    launch[1] = 0;
    launch[2] = o[2];
    fx.pos.x = int16_t(uint32_t(actor.pos.x + o[0]) >> 16);
    const uint32_t r2 = rngStep();
    fx.pos.y = wrap16(actor.pos.y.whole - int(r2 & 0xf) - 0x40);
    fx.pos.z = int16_t(uint32_t(actor.pos.z + o[2]) >> 16);
    const int32_t from[3] = {int32_t(uint32_t(int32_t(fx.pos.x)) << 16), int32_t(uint32_t(int32_t(fx.pos.y)) << 16),
                             int32_t(uint32_t(int32_t(fx.pos.z)) << 16)};
    fx.riseSpeed = wrap16(launchSpeed(from, target, 0x100, 0xc, 0xffffffffu)); // exe 0x5e3ee0
}

void fireSpawn(StaticActor& actor, BurningFields& fire, const FireParams& params) { // exe 0x5ae4e0
    actor.flags &= uint8_t(~kActorDrawn);
    fire.clear(actor);
    const uint32_t v = actor.variant;
    fire.fullStrength = fireSizes[v].strength;
    fire.strength = fireSizes[v].strength;
    fire.frameTicks = fireFrames[v * kFireFramesPerSize + fire.frame].ticks;
    fire.emberTimer = uint16_t((rngStep() & 0x3f) + 0x90);
    fire.crackleTimer = uint8_t((actor.slot & 0xf) << 1);
    const uint32_t f = allocActor60(kActorStageClass);
    fire.flame = f;
    if (f) {
        StaticActor& flame = staticActorAt(f);
        flame.flags = kActorLive;
        flame.type = kFlameType;
        if ((params.flags & kFireTallFlame) == 0) {
            flame.variant = kFlameShortVariant;
        } else {
            flame.variant = kFlameTallVariant;
            classState<FlameFields>(staticActorAt(f)).flickerPhase = uint8_t(params.index & 1);
        }
        flame.loopFlags |= kActorLateLoop;
        flame.pos = actor.pos;
    }
    actor.enterState(uint8_t(FireState::Burn));
}

void fireBurn(StaticActor& actor, BurningFields& fire, const FireParams& params) { // exe 0x5ae5d0
    FireMissionState& mission = fireMission();
    if (burnTick(actor, fire) == -1) {
        if (params.flags & kFireSpecial) mission.specialFireOut = 1;
        --mission.firesLeft;
        ++mission.firesOut;
        actor.enterState(uint8_t(FireState::PutOut));
    }
    if ((params.flags & kFireEmbers) && fire.strength >= kEmberStrength && !mission.dataPanicking()) {
        --fire.emberTimer;
        if (fire.emberTimer == 0) {
            fire.emberTimer = uint16_t((rngStep() & 0x7f) + 0x90);
            throwEmber(actor);
        }
    }
    if (fire.crackleTimer == 0) {
        fire.crackleTimer = kCrackleTicks;
        soundPlayAtPos(kCrackleSound, actor.pos);
    } else {
        --fire.crackleTimer;
    }
    stepFrame(actor, fire);
    submitFireHit(actor, fire, params);
    addHeat(actor, fire);
}

// a debris piece (fire effect kind 4) thrown from the blast
void spawnDebris(const StaticActor& actor, uint8_t variant, bool randomVariant) {
    const uint32_t e = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (!e) return;
    EffectActor& debris = effectActorAt(e);
    debris.flags = kActorLive;
    debris.type = kFireEffectType;
    debris.subtype = uint8_t(FireEffectKind::Debris);
    debris.loopFlags |= kActorLateLoop;
    debris.variant = randomVariant ? uint8_t(rngStep() % 3) : variant;
    debris.pos.x = actor.pos.x.whole;
    debris.pos.y = wrap16(actor.pos.y.whole - int(rngStep() & 0x7f) + 0x40);
    debris.pos.z = actor.pos.z.whole;
    const uint32_t r = rngLastValue();
    auto&& params = DebrisParams(effectActorAt(e));
    params.direction = int8_t(uint8_t(((uint16_t(actor.rotation.y) >> 4) & 0xff) - ((r >> 7) & 0x7f) + 0x40));
    params.reach = int8_t(uint8_t(((r >> 0xe) & 0xf) + 0x18));
}

void wallBlastSpawn(StaticActor& actor, BurningFields& fire) { // exe 0x5aeda0
    actor.flags &= uint8_t(~kActorDrawn);
    fire.clear(actor);
    fire.fullStrength = kBlastStrength;
    fire.strength = kBlastStrength;
    if (const uint32_t e = allocActorA0(kActorStageClass)) { // the flash
        EffectActor& flash = effectActorAt(e);
        flash.flags = kActorLive;
        flash.type = kFireEffectType;
        flash.subtype = uint8_t(FireEffectKind::BlastFlash);
    }
    uint8_t v = 0;
    for (int k = 0; k < kBlastDebris; ++k, ++v) {
        if (v == 3) v = 0;
        spawnDebris(actor, v, false);
    }
    // the wall's cell broken open (its segment's variant 1)
    const uint32_t row = uint32_t(int(int16_t(actor.pos.z.whole >> 9)) + kCellBias);
    const uint32_t column = uint32_t(int(int16_t(actor.pos.x.whole >> 9)) + kCellBias);
    switchCellSegmentVariant(GridCell(gridCellAtIndex(column, row)).head, 1);
    cameraShake(true, kBlastShake, kBlastShakeDecay);
    soundPlayAtPos(kBlastSound, actor.pos);
    actor.enterState(uint8_t(FireState::Burn));
}

void wallBlastBurn(StaticActor& actor, BurningFields& fire, WallBlastParams& params) { // exe 0x5aef10
    FireMissionState& mission = fireMission();
    if (burnTick(actor, fire) == -1) {
        --mission.firesLeft;
        ++mission.firesOut;
        actor.enterState(uint8_t(FireState::PutOut));
    }
    ++fire.swing;
    ++fire.timer;
    if (fire.timer == kEmberTicks) {
        fire.timer = 0;
        if (const uint32_t e = allocActorA0(kActorStageClass)) { // an ember swinging around the blast's direction
            EffectActor& ember = effectActorAt(e);
            ember.flags = kActorLive;
            ember.type = kFireEffectType;
            ember.subtype = uint8_t(FireEffectKind::BlastEmber);
            const uint32_t q = rngStep();
            ember.pos.x = wrap16(actor.pos.x.whole - int(q & 0x1f) + 0xf);
            ember.pos.y = wrap16(actor.pos.y.whole - int((q >> 5) & 0x1f) + 0xf);
            ember.pos.z = wrap16(actor.pos.z.whole - int((q >> 10) & 0x1f) + 0xf);
            const uint32_t c = fire.swing;
            const uint32_t d = (c & 0x20) ? 0x10 - (c & 0x1f) : (c & 0x1f) - 0x10;
            auto&& emberParams = BlastEmberParams(effectActorAt(e));
            emberParams.direction = int8_t(uint8_t((uint32_t(uint16_t(actor.rotation.y) >> 4) + d) & 0xff));
            emberParams.attack = params.attack;
            emberParams.reach = int8_t(uint8_t(fire.strength >> 7));
        }
    }
    if (params.debrisThrown < kBlastExtraDebris) {
        params.debrisThrown = int8_t(params.debrisThrown + 1);
        spawnDebris(actor, 0, true);
    }
    if (fire.crackleTimer == 0) {
        fire.crackleTimer = kBlastCrackleTicks;
        soundPlayAtPos(kBlastCrackleSound, actor.pos);
    } else {
        --fire.crackleTimer;
    }
    // exe 0x5af340: the hit entry kBlastHitReach ahead along the yaw
    fire.hitBox.previousX = actor.prevPos.x.whole;
    fire.hitBox.previousZ = actor.prevPos.z.whole;
    fire.hitBox.result = 0;
    int16_t dx, dz;
    angleVector(uint16_t(actor.rotation.y), kBlastHitReach, dx, dz);
    FireScratch& s = scratch();
    s.offsetX = dx;
    s.hitPoint.x = wrap16(actor.pos.x.whole + dx);
    s.hitPoint.y = actor.pos.y.whole;
    s.offsetZ = dz;
    s.hitPoint.z = wrap16(actor.pos.z.whole + dz);
    const int16_t pos[3] = {s.hitPoint.x, s.hitPoint.y, s.hitPoint.z};
    hitListSubmit(pos, fire.hitBox, hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | hitRadius(0x40),
                  uint32_t(params.attack) | kFireAttackFlags);
    addHeat(actor, fire);
}

// exe 0x5b1010: a new tongue every period while emitting, every tongue stepped through its frames; the live tongues
int flameTongues(StaticActor& actor, FlameFields& flame) {
    int live = 0;
    const uint32_t v = actor.variant;
    if (flame.stopped == 0) {
        if (v == kFlameTallVariant && flame.flickerPhase != 0) flame.emitTimer = 0;
        if (int(emitPeriods[v]) <= int(flame.emitTimer)) {
            flame.emitTimer = 0;
            for (uint32_t k = 0; k < kTongues; ++k) {
                auto&& t = flame.tongues[k];
                if (int8_t(t.frame) != -1) continue;
                if (v == 0 || v == 1 || v == 3) {
                    const uint32_t r = rngStep();
                    t.dx = int8_t(uint8_t(0x10 - (r & 0x1f)));
                    t.rise = int16_t(-int((r >> 5) & 7));
                    t.dz = int8_t(uint8_t(0x10 - ((r >> 8) & 0x1f)));
                }
                t.height = int16_t(tongueHeights[actor.variant]);
                t.frame = 0;
                t.spin = uint8_t(rngStep() & 0xff);
                break;
            }
        }
    }
    for (uint32_t k = 0; k < kTongues; ++k) {
        auto&& t = flame.tongues[k];
        if (t.frame == kFreeTongue) continue;
        const TongueFrame f = tongueFrames[uint32_t(actor.variant) * kTongueFramesPerVariant + t.frame];
        ++live;
        t.height = wrap16(t.height + f.heightStep);
        t.spin = uint8_t(t.spin + f.spinStep);
        t.rise = wrap16(t.rise - tongueRise[actor.variant]);
        // exe 0x5b11a0: the tongue's sprite (a draw command: render output)
        if (f.next == -1)
            t.frame = kFreeTongue;
        else
            ++t.frame;
    }
    return live;
}

// ---- the mission's effects -----------------------------------------------------------------------------------------
// each kind's sizes (by variant) and the smoke's rise
const DataTable<ScalarTable<uint16_t, 8>> smokeSizes{TableId::FlutterFireSmokeSizes};
const DataTable<ScalarTable<int16_t, 8>> smokeRise{TableId::FlutterFireSmokeRise};
const DataTable<ScalarTable<uint16_t, 8>> blastEmberSizes{TableId::FlutterFireBlastEmberSizes};
const DataTable<ScalarTable<uint16_t, 8>> debrisSizes{TableId::FlutterFireDebrisSizes};
const DataTable<ScalarTable<uint16_t, 8>> emberSizes{TableId::FlutterFireEmberSizes};
const DataTable<CollisionBoxRecord> blastEmberBox{TableId::FlutterFireBlastEmberBox};
const DataTable<CollisionBoxRecord> emberBox{TableId::FlutterFireEmberBox};
constexpr uint32_t kEmberGroundMask = 2, kPieceGroundMask = 0x10;
constexpr uint8_t kEmberSparkVariant = 3, kBlastEmberSparkVariant = 4;
constexpr uint8_t kFlashStep = 0x20;
constexpr int16_t kBlastEmberRise = 0x80, kBlastEmberLift = 0x20, kDebrisFall = 0x18, kEmberFall = 0xc;
constexpr uint8_t kDebrisSettle = 0x10, kDebrisLife = 0x40, kEmberLife = 0x80;
constexpr uint16_t kEmberFadeSize = 8;
// the ember's step (+0xa): flying until it lands, then fading
enum class EmberStep : uint8_t { Fly = 0, Fade = 1 };
constexpr uint32_t kEmberQuench = kHitAttackByPlayer | kHitAttackBump | kHitAttackWater; // what puts an ember out
constexpr uint32_t kBlastEmberAttack = kHitAttackHarmless | kHitAttackStrong | kHitAttackThrown;
constexpr uint32_t kEmberAttack = kHitAttackHarmless | kHitAttackStrong | 4;

// exe 0x437d50(effect, keys): the sprite's key -- its size and colour stepped, the next key when its ticks run out (the
// last one ends the effect, a loop key restarts); the sprite itself is a draw command (render output)
void stepKeys(EffectActor& effect, FireEffectFields& fx, const KeyScript& keys) {
    const SpriteKey k = keys[fx.key];
    fx.size = wrap16(fx.size + k.sizeStep);
    fx.spin = uint8_t(fx.spin + k.spinStep);
    --fx.keyTicks;
    if (fx.keyTicks != 0) return;
    if (k.next == kKeyDone) {
        effect.enterState(uint8_t(FireEffectState::Done));
        return;
    }
    if (k.next == kKeyLoop) {
        fx.key = 0;
        fx.keyTicks = keys[0].ticks;
        return;
    }
    const uint8_t n = uint8_t(fx.key + 1);
    fx.key = n;
    fx.keyTicks = keys[n].ticks;
}
// the sprite started: hidden from the actor draw, its block cleared, its size and first key's ticks
void startSprite(EffectActor& effect, FireEffectFields& fx) {
    effect.flags &= uint8_t(~kActorDrawn);
    fx.clearSprite();
}
// exe 0x437ea0 / 0x437f20: the effect's hit entry (it hurts the player)
void submitEffectHit(const EffectActor& effect, FireEffectFields& fx, uint32_t size, uint32_t attack) {
    fx.hitBox.previousX = fx.pos.x;
    fx.hitBox.previousZ = fx.pos.z;
    fx.hitBox.result = 0;
    auto&& point = scratch().hitPoint;
    point.x = fx.pos.x;
    point.y = fx.pos.y;
    point.z = fx.pos.z;
    const int16_t pos[3] = {fx.pos.x, fx.pos.y, fx.pos.z};
    hitListSubmit(pos, fx.hitBox,
                  hitRadius(size >> 1) | hitOwner(effect.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn, attack);
}
// a spark (effect type 0) at the effect, its size given or (randomSize) drawn from the RNG
void sparkAt(const EffectActor& from, uint8_t variant, uint16_t size, bool randomSize = false) {
    const uint32_t e = allocActorA0(0); // exe 0x5e13b0
    if (!e) return;
    EffectActor& spark = effectActorAt(e);
    spark.flags = kActorLive;
    spark.type = typeByte(GlobalEffectType::HitSpark);
    spark.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    spark.variant = variant;
    spark.copyPositionWords(from); // its rise speed lands in the spark's +0x16 (no reader)
    auto&& sparkFields = classState<HitSparkFields>(effectActorAt(e));
    sparkFields.size = randomSize ? uint16_t((rngStep() & 0xf) + 0x30) : size;
}
// the planar step of a thrown piece (its direction byte << 4, its reach), left in the scratch as the exe does
void stepAlong(FireEffectFields& fx, int8_t direction, int8_t reach) {
    int16_t dx, dz;
    angleVector(int(direction) << 4, reach, dx, dz);
    FireScratch& s = scratch();
    s.hitPoint.x = dx;
    s.hitPoint.z = dz;
    fx.pos.x = wrap16(fx.pos.x + dx);
    fx.pos.z = wrap16(fx.pos.z + dz);
}

} // namespace

void fireUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    BurningFields& fire = classState<BurningFields>(actor);
    const FireParams params(actor);
    savePrevious(actor);
    switch (FireState(actor.state)) {
    case FireState::Spawn: fireSpawn(actor, fire, params); return;
    case FireState::Burn: fireBurn(actor, fire, params); return;
    case FireState::PutOut: putOut(actor, fire, true); return;
    default: notPortedAt(kFireArea, "0x5ae490 state", actor.state);
    }
}

void wallBlastUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    BurningFields& fire = classState<BurningFields>(actor);
    auto&& params = WallBlastParams(actor);
    savePrevious(actor);
    switch (FireState(actor.state)) {
    case FireState::Spawn: wallBlastSpawn(actor, fire); return;
    case FireState::Burn: wallBlastBurn(actor, fire, params); return;
    case FireState::PutOut: putOut(actor, fire, false); return;
    default: notPortedAt(kFireArea, "0x5aed50 state", actor.state);
    }
}

void flameUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    FlameFields& flame = classState<FlameFields>(actor);
    switch (FlameState(actor.state)) {
    case FlameState::Start: {
        actor.flags &= uint8_t(~kActorDrawn);
        flame.freeTongues();
        const uint32_t r = rngStep();
        flame.emitTimer = int8_t(uint8_t(r % emitPeriods[actor.variant]));
        flame.flickerTimer = 0;
        actor.enterState(uint8_t(FlameState::Emit));
        return;
    }
    case FlameState::Emit:
        flame.emitTimer = int8_t(flame.emitTimer + 1);
        ++flame.flickerTimer;
        if (flame.flickerTimer > kFlickerFlips) {
            flame.flickerTimer = 0;
            flame.flickerPhase ^= 1u;
        }
        flameTongues(actor, flame);
        if (flame.stopped != 0) actor.enterState(uint8_t(FlameState::Die));
        return;
    case FlameState::Die:
        if (flameTongues(actor, flame) == 0) freeActor(actor);
        return;
    default: notPortedAt(kFireArea, "0x5b0f20 state", actor.state);
    }
}

void fireEffectUpdate(const PlayerEnv& env, uint32_t address) {
    EffectActor& effect = effectActorAt(address);
    FireEffectFields& fx = classState<FireEffectFields>(effect);
    const FireEffectKind kind = FireEffectKind(effect.subtype);
    const FireEffectState st = FireEffectState(effect.state);
    FireMissionState& mission = fireMission();
    if (st == FireEffectState::Done) {
        freeActor(effect);
        return;
    }
    if (st != FireEffectState::Start && st != FireEffectState::Run)
        notPortedAt(kFireArea, "0x437520 state", effect.state);
    const bool start = st == FireEffectState::Start;
    switch (kind) {
    case FireEffectKind::Shimmer:
        if (start) { // exe 0x437540
            fx.clearSprite();
            effect.state = uint8_t(FireEffectState::Run);
            effect.flags &= uint8_t(~kActorDrawn);
            mission.heat = 0;
            return;
        }
        // exe 0x437570: the screen tint (exe 0x437cc0: a full-screen primitive, render output)
        if (mission.heat != 0) mission.heat = mission.heat + (rngStep() & 0x1ff);
        mission.heat = 0;
        return;
    case FireEffectKind::SmokePuff:
        if (start) { // exe 0x4375e0
            startSprite(effect, fx);
            fx.size = int16_t(smokeSizes[effect.variant]);
            fx.keyTicks = smokeKeys[effect.variant][0].ticks;
            effect.state = uint8_t(FireEffectState::Run);
            return;
        }
        fx.pos.y = wrap16(fx.pos.y - smokeRise[effect.variant]); // exe 0x437630: rises
        stepKeys(effect, fx, smokeKeys[effect.variant]);
        return;
    case FireEffectKind::BlastEmber: {
        if (start) { // exe 0x437670
            startSprite(effect, fx);
            fx.size = int16_t(blastEmberSizes[effect.variant]);
            fx.keyTicks = blastEmberKeys[effect.variant][0].ticks;
            fx.riseSpeed = kBlastEmberRise;
            effect.state = uint8_t(FireEffectState::Run);
            return;
        }
        // exe 0x4376d0: rises along its direction, a spark where it hits the room; hurts
        const BlastEmberParams params(effect);
        fx.riseSpeed = wrap16(fx.riseSpeed - kBlastEmberLift);
        fx.pos.y = wrap16(fx.pos.y + (fx.riseSpeed >> 4));
        stepAlong(fx, params.direction, params.reach);
        stepKeys(effect, fx, blastEmberKeys[effect.variant]);
        if (fx.key != 0) {
            const int16_t p[3] = {fx.pos.x, fx.pos.y, fx.pos.z};
            if (testMoveBlocked(env, p, p, CollisionBoxRef::record(blastEmberBox.stateAddress()), kPieceGroundMask) != 0) {
                sparkAt(effect, kBlastEmberSparkVariant, uint16_t(fx.size));
                effect.enterState(uint8_t(FireEffectState::Done));
            }
        }
        submitEffectHit(effect, fx, uint16_t(fx.size), uint32_t(params.attack) | kBlastEmberAttack);
        return;
    }
    case FireEffectKind::BlastFlash: {
        auto&& params = BlastFlashParams(effect);
        if (start) { // exe 0x4377d0 (the flash: exe 0x437cc0, render output)
            params.level = 0xff;
            effect.flags &= uint8_t(~kActorDrawn);
            effect.state = uint8_t(FireEffectState::Run);
            return;
        }
        if (params.level < kFlashStep) { // exe 0x437800: fades
            effect.enterState(uint8_t(FireEffectState::Done));
            return;
        }
        params.level = uint8_t(params.level - kFlashStep);
        return;
    }
    case FireEffectKind::Debris: {
        auto&& params = DebrisParams(effect);
        if (start) { // exe 0x437840: thrown up
            startSprite(effect, fx);
            fx.size = int16_t(debrisSizes[effect.variant]);
            fx.keyTicks = debrisKeys[effect.variant][0].ticks;
            const uint32_t r = rngStep();
            effect.state = uint8_t(FireEffectState::Run);
            fx.riseSpeed = wrap16(-0xa8 - int(r & 0x3f));
            return;
        }
        // exe 0x4378c0: falls, ends on the floor (after kDebrisSettle ticks) or at kDebrisLife
        fx.riseSpeed = wrap16(fx.riseSpeed + kDebrisFall);
        fx.pos.y = wrap16(fx.pos.y + (fx.riseSpeed >> 4));
        stepAlong(fx, params.direction, params.reach);
        stepKeys(effect, fx, debrisKeys[effect.variant]);
        ++params.age;
        if (params.age > kDebrisSettle) {
            const int16_t p[3] = {fx.pos.x, fx.pos.y, fx.pos.z};
            if (testMoveBlocked(env, p, p, CollisionBoxRef::record(0), kPieceGroundMask) != 0) effect.enterState(uint8_t(FireEffectState::Done));
        }
        if (params.age == kDebrisLife) effect.enterState(uint8_t(FireEffectState::Done));
        return;
    }
    case FireEffectKind::Ember: {
        auto&& params = EmberParams(effect);
        if (start) { // exe 0x4379a0
            startSprite(effect, fx);
            fx.size = int16_t(emberSizes[effect.variant]);
            fx.keyTicks = emberKeys[effect.variant][0].ticks;
            effect.state = uint8_t(FireEffectState::Run);
            return;
        }
        // exe 0x4379f0: flies until it lands, fades there; the water / a shot puts it out; hurts
        auto out = [&] { // a small spark (its size from the RNG when it gets a slot), the ember done
            sparkAt(effect, kEmberSparkVariant, 0, true);
            effect.enterState(uint8_t(FireEffectState::Done));
        };
        if (EmberStep(effect.step) == EmberStep::Fly) {
            const int16_t old[3] = {fx.pos.x, fx.pos.y, fx.pos.z};
            fx.riseSpeed = wrap16(fx.riseSpeed + kEmberFall);
            int16_t dx, dz;
            angleVector(params.yaw, 0x100, dx, dz);
            FireScratch& s = scratch();
            s.hitPoint.x = dx;
            s.hitPoint.z = dz;
            fx.pos.x = wrap16(fx.pos.x + (dx >> 4));
            fx.pos.z = wrap16(fx.pos.z + (dz >> 4));
            fx.pos.y = wrap16(fx.pos.y + (fx.riseSpeed >> 4));
            const int16_t now[3] = {fx.pos.x, fx.pos.y, fx.pos.z};
            if (testMoveBlocked(env, now, old, CollisionBoxRef::record(emberBox.stateAddress()), kEmberGroundMask) != 0) {
                const int g = probeGroundAt(env, now, CollisionBoxRef::record(emberBox.stateAddress()), fx.riseSpeed, kEmberGroundMask);
                if (uint32_t(int32_t(fx.pos.y) - g) < 0x20) fx.pos.y = int16_t(g);
                ++effect.step;
            }
        } else if (EmberStep(effect.step) == EmberStep::Fade) {
            --fx.size;
            if (uint16_t(fx.size) <= kEmberFadeSize) out();
        }
        stepKeys(effect, fx, emberKeys[effect.variant]);
        if (fx.hitBox.result & kEmberQuench) out();
        ++params.age;
        if (params.age == kEmberLife) effect.enterState(uint8_t(FireEffectState::Done));
        submitEffectHit(effect, fx, emberSizes[effect.variant], kEmberAttack);
        return;
    }
    case FireEffectKind::DataFlame:
        if (start) { // exe 0x437bf0: kept on the burning Data's head (content/intro/burning_data)
            startSprite(effect, fx);
            fx.keyTicks = emberKeys[effect.variant][0].ticks;
            effect.enterState(uint8_t(FireEffectState::Run));
            return;
        }
        // exe 0x437c30: its size from Data, smoke puffs (variant 3 every other frame, variant 4 every frame)
        fx.size = int16_t(DataFlameParams(effect).size);
        stepKeys(effect, fx, emberKeys[effect.variant]);
        for (const uint8_t v : {uint8_t(3), uint8_t(4)}) {
            if (v == 3 && (gameState().frame & 1)) continue;
            if (const uint32_t e = allocActorA0(kActorStageClass)) { // exe 0x5e13a0
                EffectActor& puff = effectActorAt(e);
                puff.flags = kActorLive;
                puff.type = kFireEffectType;
                puff.subtype = uint8_t(FireEffectKind::SmokePuff);
                puff.variant = v;
                puff.copyPositionWords(effect);
            }
        }
        return;
    default: break;
    }
    notPortedAt(kFireArea, "0x437520 kind", effect.subtype);
}

} // namespace od::port
