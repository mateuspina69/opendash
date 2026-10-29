// ST12 / ST13 / ST14 actor classes (see st12_14.hpp): the rolling boulder, the tumbling block, the walker that drops
// in and the gun turret. Left out (render output): the shadows (exe 0x5f20f0 / the shadow list 0x5f2220).
#include "content/manda/st12_14.hpp"
#include "content/manda/st12_14_state.hpp"
#include "content/nino/platform_boss_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/vectors.hpp"
#include "game/global_class_types.hpp"
#include "game/glow_trails.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/territory.hpp"

namespace od::port {

using namespace st12_14_state;
using platform_boss_state::BoltFields;

namespace {

constexpr uint16_t kTintHurtBlink = 0x4214; // the dying blink's tint

// ---- 0x59c9b0 (ST14 static type 0x18): the rolling boulder ------------------------------------------------------
// Rolls back and forth between its spawn point and a point (+0xc, +0xd) * 0x200 away: accelerates by its variant's
// row [0x8ce834] (the top speed, the acceleration), brakes in time (sound 0x13b), then turns round (exe 0x59cde0); a
// wall hit shakes the camera and turns it round. Spins, drags its companion (another type 0x18 that stays put) along,
// and is a 0x20 capsule hit entry. A boulder with a model variant (+0xf) stays put.
enum class BoulderState : uint8_t { Bind = 0, Roll = 1 }; // exe 0x59cab0 / 0x59cc00
enum class BoulderSub : uint8_t { Accelerate = 0, Cruise = 1, Brake = 2 };

// a boulder's row (exe data, by variant)
struct BoulderRow : DataRecord {
    int16_t topSpeed = 0;
    int16_t acceleration = 0;
    static BoulderRow load(const TableReader& r) {
        BoulderRow rec;
        rec.topSpeed = r.s16(0);
        rec.acceleration = r.s16(2);
        return rec;
    }
};
const DataTable<RecordTable<BoulderRow, 4, 4>> boulderRows{TableId::St1214BoulderRows};
const DataTable<CollisionBoxRecord> boulderBox{TableId::St1214BoulderBox};
constexpr uint8_t kBoulderType = 0x18;
constexpr uint8_t kBoulderAttackType = 0x20; // the animated actors' type whose row's attack the boulder deals
constexpr uint16_t kSoundBoulderBrake = 0x13b;

// the point the boulder rolls toward (x, z)
void boulderTarget(const BoulderFields& f, int16_t& x, int16_t& z) {
    if (f.returning == 0) {
        x = f.endX;
        z = f.endZ;
    } else {
        x = f.startX;
        z = f.startZ;
    }
}
// exe 0x59cde0: turned round toward the other point, stopped
void boulderTurn(StaticActor& a, BoulderFields& f) {
    f.returning = uint16_t(f.returning ^ 1u);
    a.velocity.z = 0;
    int16_t tx, tz;
    boulderTarget(f, tx, tz);
    const int dz = a.pos.z.whole - tz;
    const int dx = a.pos.x.whole - tx;
    f.pathYaw = int16_t(uint32_t(atanAngle(float(dx), float(dz))) & kAngleMask); // exe 0x40b960
}
// exe 0x59cab0: the model, the box, its two points and heading; its companion (unless it stays put)
void boulderBind(StaticActor& a, BoulderFields& f) {
    a.enterState(uint8_t(BoulderState::Roll));
    if (!bindActorModel(a, f.modelVariant)) { // exe 0x5e0bc0
        freeActor(a);
        return;
    }
    a.maxHitPoints = 1;
    a.hitPoints = 1;
    a.flags |= kActorScenery | kActorDrawn;
    a.velocity.setXY(0, 0);
    a.velocity.setZSpare(0, 0);
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.airborne = 1;
    a.onPlatform = 0;
    a.collisionBox.pointAt(boulderBox.stateAddress());
    a.interactionBox = nullptr;
    a.interaction = 0;
    a.clearClassBlock(); // exe 0x40dc40
    f.startX = a.pos.x.whole;
    f.startZ = a.pos.z.whole;
    f.endX = int16_t(uint32_t(int32_t(f.endCellX) << 9) + uint16_t(a.pos.x.whole));
    f.endZ = int16_t(uint32_t(int32_t(f.endCellZ) << 9) + uint16_t(a.pos.z.whole));
    const BoulderRow row = boulderRows[a.variant];
    const int dz = f.startZ - f.endZ;
    const int dx = f.startX - f.endX;
    f.pathYaw = int16_t(uint32_t(atanAngle(float(dx), float(dz))) & kAngleMask);
    a.acceleration.z = row.acceleration;
    if (f.modelVariant != 0) return;
    const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!address) return;
    StaticActor& companion = staticActorAt(address);
    companion.flags = kActorLive;
    companion.type = kBoulderType;
    companion.subtype = 0;
    companion.loopFlags |= kActorSweepGroup2;
    companion.variant = a.variant;
    classState<BoulderFields>(companion).modelVariant = 1;
    companion.pos = a.pos;
    f.companionSlot = companion.slot;
}
// exe 0x59cc00: the speed toward the point (braking in time), the roll, a wall's turn, the spin, the companion along
void boulderRoll(const PlayerEnv& env, StaticActor& a, BoulderFields& f) {
    bool turned = false;
    const BoulderRow row = boulderRows[a.variant];
    int16_t tx, tz;
    boulderTarget(f, tx, tz);
    const int32_t dx = a.pos.x.whole - tx;
    const int32_t dz = a.pos.z.whole - tz;
    const int32_t d = isqrt(int32_t(uint32_t(dz) * uint32_t(dz) + uint32_t(dx) * uint32_t(dx))); // exe 0x40b980
    if (f.modelVariant != 0) return;
    switch (BoulderSub(a.subState)) {
    case BoulderSub::Accelerate:
        a.velocity.z = int16_t(uint16_t(a.velocity.z) + uint16_t(a.acceleration.z));
        if (a.velocity.z >= row.topSpeed) {
            a.velocity.z = row.topSpeed;
            a.subState = uint8_t(BoulderSub::Cruise);
        }
        break;
    case BoulderSub::Cruise:
        if (stoppingDistance(-int32_t(a.velocity.z), a.acceleration.z) >= int32_t(uint32_t(d) << 16)) {
            a.velocity.z = int16_t(uint16_t(a.velocity.z) - uint16_t(a.acceleration.z));
            a.subState = uint8_t(BoulderSub::Brake);
            soundPlayAtPos(kSoundBoulderBrake, a.pos); // exe 0x5d5380
        }
        break;
    case BoulderSub::Brake:
        a.velocity.z = int16_t(uint16_t(a.velocity.z) - uint16_t(a.acceleration.z));
        if (a.velocity.z <= 0 || int32_t(uint32_t(d) << 4) < a.velocity.z) {
            a.pos.x.whole = tx;
            a.pos.z.whole = tz;
            boulderTurn(a, f);
            turned = true;
            a.subState = uint8_t(BoulderSub::Accelerate);
        }
        break;
    default: break;
    }
    moveAlongYaw(a.pos, f.pathYaw, 0, 0, -int32_t(a.velocity.z)); // exe 0x5e36f0
    if (int32_t(actorTestMove(env, a)) < 0 && (actorPushOut(env, a) & 0xf00) && !turned) { // exe 0x40f570 / 0x410960
        cameraShakeAt(0xa00, 0x200, 0x100, 0x800, a.pos); // exe 0x40eab0
        boulderTurn(a, f);
        a.subState = uint8_t(BoulderSub::Accelerate);
    }
    a.rotation.y = int16_t(uint16_t(a.rotation.y) - 0x80u);
    if (f.companionSlot == 0) return;
    Actor& companion = actorAt(actorBySlot(f.companionSlot)); // exe 0x5e0fa0
    if (!(companion.flags & kActorLive) || companion.type != kBoulderType) return;
    companion.pos = a.pos;
    companion.rotation = a.rotation;
}
// exe 0x59c9b0
void boulderUpdate(const PlayerEnv& env, StaticActor& a) {
    auto&& f = classState<BoulderFields>(a);
    a.savePreviousPose();
    a.savePreviousContact();
    switch (BoulderState(a.state)) {
    case BoulderState::Bind: boulderBind(a, f); break;
    case BoulderState::Roll: boulderRoll(env, a, f); break;
    default: notPortedAt(kStoryPort, "0x59c9b0 state (table 0x8ce838)", a.state); return;
    }
    if (!(a.flags & kActorLive)) return;
    const int16_t low[3] = {a.pos.x.whole, int16_t(a.pos.y.whole + 0x300), a.pos.z.whole};
    const int16_t high[3] = {a.pos.x.whole, int16_t(a.pos.y.whole + 0x100), a.pos.z.whole};
    const uint32_t attack =
        uint32_t(animatedStats(kBoulderAttackType).attack) | kHitAttackHarmless | kHitAttackDeflects | kHitAttackThrown;
    hitListSubmitCapsule(high, 0x20, low, 0x20, f.hitBox, // exe 0x5e4190
                         hitOwner(a.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitCapsule(1), attack);
    f.hitBox.previousX = low[0];
    f.hitBox.previousZ = low[2];
    // exe 0x5f2220(actor, high, 0x60, 0x100, 0): the shadow list entry (render output)
}

// ---- 0x59a7a0 (ST12 static type 0x13): the tumbling block -------------------------------------------------------
// Waits for its variant's flag ([0x8ce4d4]: 0x5b6 / 0x680), then repeats: 0x1e ticks still, slides 0x1000 forward
// (speed -0x80 a tick down to -0x200, sound 0x13b), lands (a shake, sound 0x13c) and rocks 0x10 ticks (pitch / roll
// by its facing, which then turns 0x800). While active, 1 tick in 8 a lightning bolt (controller type 0xf, exe
// 0x423f40: content/nino/platform_boss; sound 0x13d).
// Its facing from +0xc (bit 0: 0x800, bit 1: 0x400); a solid and a 0x20 capsule hit entry
enum class TumbleState : uint8_t { Bind = 0, Run = 1 }; // exe 0x59a840 / 0x59a8d0
enum class TumbleSub : uint8_t { Rest = 0, Still = 1, SlideStart = 2, Slide = 3, Rock = 4 }; // table 0x59ad8c

const DataTable<ScalarTable<uint16_t, 2>> tumbleStartFlags{TableId::St1214TumbleStartFlags}; // by variant
const DataTable<CollisionBoxRecord> tumbleBox{TableId::St1214TumbleBox};
constexpr uint8_t kBoltType = 0xf, kBoltOnStatic = 1;
constexpr uint16_t kSoundSlide = 0x13b, kSoundLand = 0x13c, kSoundBolt = 0x13d;
constexpr uint8_t kStillTicks = 0x1e, kRockTicks = 0x10;
constexpr int16_t kSlideLength = 0x1000;

// a lightning bolt on the block (a random point 0x50 around it, length 0x200, amplitude 0x20), wide or narrow: its
// width, start angle, angle step (the header's step) and fade (its step counter)
void blockBolt(const StaticActor& a, bool wide) {
    const uint32_t address = allocActorE0(); // exe 0x5e1440
    if (!address) return;
    ControllerActor& bolt = controllerActorAt(address);
    BoltFields& fields = classState<BoltFields>(bolt);
    bolt.flags = kActorLiveDrawn;
    bolt.loopFlags |= kActorLateLoop;
    bolt.type = kBoltType;
    bolt.variant = kBoltOnStatic;
    const uint32_t i = uint32_t(uint8_t((rngStep() & 7) << 3));
    fields.pos.x = int16_t(uint32_t((int32_t(sin64(i)) * 0x50) >> 12) + uint16_t(a.pos.x.whole));
    fields.pos.z = int16_t(uint32_t((int32_t(cos64(i)) * 0x50) >> 12) + uint16_t(a.pos.z.whole));
    fields.pos.y = int16_t(uint16_t(a.pos.y.whole) + 0x90u);
    fields.parent = int16_t(a.slot);
    fields.length = 0x200;
    fields.amplitudeParam = 0x20;
    fields.widthParam = int8_t((rngStep() & 0xf) + (wide ? 0x10u : 0x20u));
    if (wide) {
        bolt.step = 8;
        bolt.subState = uint8_t(rngStep() & 0x3f);
        bolt.stepCounter = uint8_t(0x10u - (rngStep() & 7));
    } else {
        bolt.subState = uint8_t(rngStep() & 0x3f);
        bolt.step = uint8_t((rngStep() & 7) + 7u);
        bolt.stepCounter = uint8_t(0x17u - (rngStep() & 7));
    }
    soundPlayAtPos(kSoundBolt, a.pos); // exe 0x5d5380
}
// exe 0x59a8d0: its flag awaited, then the slides and the rocking; the capsule entry and the bolts
void tumbleRun(StaticActor& a, TumbleFields& f) {
    if (f.active == 0) {
        if (storyFlag(tumbleStartFlags[a.variant])) f.active = 1; // exe 0x510e50
        return;
    }
    switch (TumbleSub(a.subState)) {
    case TumbleSub::Rest:
        f.timer = kStillTicks;
        a.velocity.z = 0;
        a.spin.x = 0;
        f.rockAmplitude = 0;
        a.subState = uint8_t(TumbleSub::Still);
        [[fallthrough]];
    case TumbleSub::Still:
        --f.timer;
        if (f.timer == 0) a.subState = uint8_t(TumbleSub::SlideStart);
        break;
    case TumbleSub::SlideStart:
        f.slideLeft = kSlideLength;
        soundPlayAtPos(kSoundSlide, a.pos);
        a.subState = uint8_t(TumbleSub::Slide);
        [[fallthrough]];
    case TumbleSub::Slide:
        a.velocity.z = int16_t(uint16_t(a.velocity.z) - 0x80u);
        if (a.velocity.z < -0x200) a.velocity.z = -0x200;
        f.slideLeft = int16_t(uint16_t(f.slideLeft) + uint16_t(a.velocity.z));
        if (f.slideLeft > 0) break;
        a.velocity.z = 0;
        cameraShakeAt(0x800, 0x300, 0x200, 0x800, a.pos); // exe 0x40eab0
        f.timer = kRockTicks;
        f.rockAmplitude = 8;
        a.subState = uint8_t(TumbleSub::Rock);
        soundPlayAtPos(kSoundLand, a.pos);
        [[fallthrough]];
    case TumbleSub::Rock: {
        int32_t o[3];
        rotateByYaw(int16_t(f.facing) + 0xc00, 0, 0, int32_t(f.rockAmplitude) * int8_t(f.timer), o); // exe 0x5e38d0
        a.rotation.x = int16_t(o[0] >> 16);
        a.rotation.z = int16_t(o[2] >> 16);
        f.rockAmplitude = int16_t(0u - uint16_t(f.rockAmplitude));
        --f.timer;
        if (f.timer == 0) {
            a.subState = uint8_t(TumbleSub::Rest);
            f.facing = uint16_t((uint16_t(f.facing) - 0x800u) & kAngleMask);
            a.rotation.x = 0;
            a.rotation.z = 0;
        }
        break;
    }
    default: break;
    }
    moveAlongYaw(a.pos, int16_t(f.facing), 0, 0, a.velocity.z); // exe 0x5e36f0
    const StaticActorStats stats = staticStats(a.type, 0);
    const int16_t y = int16_t(a.pos.y.whole + 0x80);
    const int16_t top[3] = {a.pos.x.whole, y, a.pos.z.whole};
    const int16_t bottom[3] = {a.pos.x.whole, int16_t(y + 0x280), a.pos.z.whole};
    hitListSubmitCapsule(top, 0x20, bottom, 0x20, f.hitBox, // exe 0x5e4190
                         hitOwner(a.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitCapsule(1),
                         uint32_t(stats.damage) | kHitAttackHarmless | kHitAttackSolid);
    if (rngStep() & 7) return;
    blockBolt(a, (rngStep() & 1) != 0);
}
// exe 0x59a7a0
void tumbleUpdate(StaticActor& a) {
    auto&& f = classState<TumbleFields>(a);
    a.savePreviousPose();
    a.savePreviousContact();
    switch (TumbleState(a.state)) {
    case TumbleState::Bind: // exe 0x59a840
        a.enterState(uint8_t(TumbleState::Run));
        if (bindActorModel(a, 0)) { // exe 0x5e0ba0
            a.velocity.setXY(0, 0);
            a.velocity.setZSpare(0, 0);
            a.rotation.z = 0;
            a.rotation.x = 0;
            a.airborne = 0;
            a.onPlatform = 0;
            a.interactionBox = nullptr;
            a.interaction = 0;
            a.flags |= kActorScenery;
            a.maxHitPoints = 1;
            a.hitPoints = 1;
            a.collisionBox.pointAt(tumbleBox.stateAddress());
            a.clearClassBlock(); // exe 0x40dc40
            const uint8_t c = f.facingBits;
            f.facing = uint16_t(uint32_t((c & 2) + (c & 1) * 4) << 9);
        }
        break;
    case TumbleState::Run: tumbleRun(a, f); break;
    default: notPortedAt(kStoryPort, "0x59a7a0 state (table 0x8ce4d8)", a.state); return;
    }
    if (!(a.flags & kActorLive)) return;
    // exe 0x5f2220(actor, {x, y + 0x300, z}, 0x60, 0x100, 0): the shadow list entry (render output)
    actorCollisionSubmitSolid(a, SolidType::StandOn, kNoBone, 0); // exe 0x414050(actor, 1, -1, 0)
}

// ---- 0x47e430 (pool 0x20 type 0x1b: ST12): the walker that drops in --------------------------------------------
// States [0x891334]: 0 bind 0x47e560 (a glow trail 0x46 above), 1 run 0x47e650 (the damage unless kWalkerNoDamage,
// the modes [0x891340], the footstep sound at frames 0 / 10), 2 die 0x47e920 (the defeat, an explosion, 0x14 / 8 ticks
// blinking, the drops, freed), 3..7 the modes themselves (the state table overlaps the mode table).
// Modes: 0 0x47e6b0 set up (speed -0x200), 1 0x47e700 walks until the move test passes, 2 0x47e750 walks until blocked,
// 3 0x47e7c0 walks 10 more ticks and vanishes, 4 0x47e820 falls (gravity 0x40) bouncing off the walls (the speeds
// halved) until it lands -> mode 2
enum class WalkerState : uint8_t { Bind = 0, Run = 1, Die = 2, FirstMode = 3, LastMode = 7 };
enum class WalkerMode : uint8_t {
    Start = 0,  // exe 0x47e6b0
    Emerge = 1, // exe 0x47e700
    Walk = 2,   // exe 0x47e750
    Vanish = 3, // exe 0x47e7c0
    Fall = 4,   // exe 0x47e820
};
constexpr uint8_t kWalkerModes = 5;

// +0x163
constexpr uint8_t kWalkerBounced = 0x01, kWalkerBlocked = 0x04;
// +0x164
constexpr uint8_t kWalkerNoHitEntry = 0x01, kWalkerNoDamage = 0x02, kWalkerKeepsFalling = 0x08;
constexpr uint8_t kWalkerNotTarget = 0x80;
constexpr uint8_t kWalkerHidden = kWalkerNotTarget | kWalkerKeepsFalling | kWalkerNoDamage | 0x04; // 0x8e

const DataTable<CollisionBoxRecord> walkerBox{TableId::St1214WalkerBox};
const DataTable<InteractionBoxRecord> walkerLiftBox{TableId::St1214WalkerLiftBox};
constexpr uint16_t kSoundWalkerHurt = 0x8d, kSoundWalkerStep = 0x11d;
constexpr int16_t kWalkerGlowHeight = -0x46;
constexpr uint8_t kWalkerGlowSize = 0x10;

// exe 0x47eae0: the move test and the territory -> kWalkerBlocked (its result, always 0, is not read here)
void walkerCollide(const PlayerEnv& env, AnimatedActor& a, WalkerFields& f) {
    f.contact = 0;
    if (int32_t(actorTestMove(env, a)) < 0) f.contact |= kWalkerBlocked; // exe 0x40f570
    const int rect = territoryViolated(a.pos);                             // exe 0x5e6ab0
    if (rect && (territoryPushBack(a, rect) & 0xf00)) f.contact |= kWalkerBlocked; // exe 0x5e6b40
}
// exe 0x47ea70: the hit box's damage (the player's attacks): the knock direction, the sound, the flash; dead -> die
void walkerDamage(AnimatedActor& a, WalkerFields& f) {
    const uint32_t v = f.hitBox.result;
    const uint32_t d = v & kHitDamageMask;
    if (d == 0) return;
    uint32_t hit = 0;
    if (v & (kHitAttackByPlayer | kHitAttackThrown | kHitAttackStrong)) {
        f.knockYaw = uint16_t(f.hitBox.contact.x);
        soundPlayAtPos(kSoundWalkerHurt, a.pos); // exe 0x5d5380
        a.tint = kTintFlash;
        hit = d;
    }
    if (hitPointsAdd(a, -int(hit))) a.enterState(uint8_t(WalkerState::Die)); // exe 0x5e52f0
}
// exe 0x47e8c0(actor, yaw): bounced: the speeds halved, 1; else the move along the yaw and the gravity
bool walkerFallStep(AnimatedActor& a, WalkerFields& f, int yaw) {
    if (f.contact & kWalkerBounced) {
        a.velocity.y = int16_t((-int32_t(a.velocity.y)) >> 1);
        a.velocity.z = int16_t(int32_t(a.velocity.z) >> 1);
        a.rotation.x = 0;
        a.airborne = 1;
        return true;
    }
    moveAlongYaw(a.pos, yaw, 0, a.velocity.y, a.velocity.z); // exe 0x5e36f0
    a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
    return false;
}
// the mode's first tick: its behaviour bits, the walk animation
bool walkerModeStart(AnimatedActor& a, WalkerFields& f, uint8_t mode) {
    if (a.step != 0) return false;
    f.mode = mode;
    a.step = 1;
    a.anim = 1;
    return true;
}
void toMode(AnimatedActor& a, WalkerMode m) {
    a.subState = uint8_t(m);
    a.restartStep();
}
void walkerMode(const PlayerEnv& env, AnimatedActor& a, WalkerFields& f, WalkerMode m) {
    switch (m) {
    case WalkerMode::Start: // exe 0x47e6d0
        a.anim = 1;
        a.velocity.setXY(0, 0);
        a.velocity.z = -0x200;
        a.acceleration.z = 0;
        resetModelScale(a); // exe 0x4e5cd0
        f.mode = 0;
        toMode(a, WalkerMode::Emerge);
        return;
    case WalkerMode::Emerge:
        walkerModeStart(a, f, kWalkerHidden);
        integrateVelocity(a, 0, 0, a.velocity.z); // exe 0x5e3690
        if (int32_t(actorTestMove(env, a)) >= 0) toMode(a, WalkerMode::Walk);
        return;
    case WalkerMode::Walk:
        walkerModeStart(a, f, 0x04);
        integrateVelocity(a, 0, 0, a.velocity.z);
        walkerCollide(env, a, f); // (its result is 0: the exe's ground-lost fall never starts here)
        if (f.contact & kWalkerBlocked) toMode(a, WalkerMode::Vanish);
        return;
    case WalkerMode::Vanish:
        if (walkerModeStart(a, f, kWalkerHidden)) f.vanishTicks = 0xa;
        integrateVelocity(a, 0, 0, a.velocity.z);
        --f.vanishTicks;
        if (f.vanishTicks == 0) freeActor(a); // exe 0x5e1490
        return;
    case WalkerMode::Fall: {
        if (a.step == 0) {
            f.mode = 0xc;
            a.step = 1;
            a.anim = 1;
            a.acceleration.y = 0x40;
            resetModelScale(a);
        } else if (a.step != 1) {
            return;
        }
        const uint32_t r = actorPushOut(env, a); // exe 0x410960
        if (int32_t(r) < 0) a.pos = a.prevPos;
        if (reflectRotationOffWalls(a.rotation, r)) f.contact |= kWalkerBounced; // exe 0x410740
        if (walkerFallStep(a, f, a.rotation.y)) {
            a.airborne = 0;
            a.velocity.y = 0;
            toMode(a, WalkerMode::Walk);
        }
        return;
    }
    default: notPortedAt(kStoryPort, "0x47e430 mode", uint32_t(m));
    }
}
// exe 0x47e920: the defeat and its explosion at the first tick, the blink, the drops when it ends
void walkerDie(const PlayerEnv& env, AnimatedActor& a, WalkerFields& f) {
    a.tint = kTintFlash;
    if (a.step == 0) {
        a.step = 1;
        if (a.airborne == 0) {
            a.velocity.x = 0;
            a.velocity.y = 0;
            a.velocity.z = 0;
            f.blinkTicks = 0x14;
        } else {
            f.blinkTicks = 8;
        }
        a.acceleration.y = 0x40;
        a.anim = 2;
        countDefeat(); // exe 0x5e53e0
        if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300
            StaticActor& x = staticActorAt(address);
            x.flags = kActorLive;
            x.loopFlags |= kActorSweepGroup2;
            x.type = typeByte(GlobalStaticType::Explosion);
            x.variant = 0;
            x.pos.x.whole = a.pos.x.whole;
            x.pos.y.whole = int16_t(uint16_t(a.pos.y.whole) - 0x60u);
            x.pos.z.whole = a.pos.z.whole;
            ExplosionParams params(x);
            params.spread = 0x30;
            params.flashes = f.blinkTicks;
        }
    }
    if (f.blinkTicks & 1) a.tint = kTintHurtBlink;
    if (int16_t(f.blinkTicks) < 5) f.mode |= kWalkerNotTarget;
    --f.blinkTicks;
    if (f.blinkTicks == 0) {
        spawnDrops(a.pos, animatedStats(a.type).dropTable); // exe 0x5f1270
        freeActor(a);
        return;
    }
    integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z);
    if (a.airborne) a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
    walkerCollide(env, a, f);
    if (a.wasAirborne && !a.airborne) {
        a.velocity.z = 0;
        a.velocity.x = 0;
    }
}
// exe 0x47e560: the model, the stats, the boxes; the glow trail riding it
void walkerBind(AnimatedActor& a, WalkerFields& f) {
    if (!bindActorModel(a, 0)) { // exe 0x5e0ba0
        freeActor(a);
        return;
    }
    a.runFlags |= kActorRunMarked;
    a.anim = 0;
    a.prevAnim = 0xff;
    a.rotation.x = 0;
    a.rotation.z = 0;
    const uint16_t hp = hasAnimatedStats(a.type) ? animatedStats(a.type).hitPoints : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.collisionBox.pointAt(walkerBox.stateAddress());
    a.interactionBox.pointAt(walkerLiftBox.stateAddress());
    a.interaction = 0;
    a.airborne = 0;
    a.onPlatform = 0;
    a.clearClassBlock(); // exe 0x40dc40
    f.hitBox.previousX = a.pos.x.whole;
    f.hitBox.previousZ = a.pos.z.whole;
    f.hitBox.result = 0;
    a.enterState(uint8_t(WalkerState::Run));
    if (const uint32_t address = allocActorA0(kActorStageClass)) { // exe 0x5e13a0
        EffectActor& e = effectActorAt(address);
        GlowTrailFields& glow = classState<GlowTrailFields>(e);
        e.flags = kActorLive;
        e.type = kGlowTrailStageType;
        e.variant = uint8_t(GlowTrailVariant::Trail);
        e.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
        glow.size = kWalkerGlowSize;
        glow.mode = 0;
        e.pos.x = 0;
        e.pos.y = 0;
        e.pos.z = kWalkerGlowHeight;
        glow.bone = 0;
        glow.parent = &a;
    }
}
// exe 0x47e430
void walkerUpdate(const PlayerEnv& env, AnimatedActor& a) {
    auto&& f = classState<WalkerFields>(a);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    const uint8_t s = a.state;
    if (s == uint8_t(WalkerState::Bind)) {
        walkerBind(a, f);
    } else if (s == uint8_t(WalkerState::Run)) { // exe 0x47e650
        a.tint = kTintNeutral;
        if (!(f.mode & kWalkerNoDamage)) walkerDamage(a, f);
        if (a.subState >= kWalkerModes) {
            notPortedAt(kStoryPort, "0x47e650 mode (table 0x891340)", a.subState);
            return;
        }
        walkerMode(env, a, f, WalkerMode(a.subState));
        const uint8_t frame = uint8_t(a.animKey.frame);
        if (frame == 0 || frame == 0xa) soundPlayAtPos(kSoundWalkerStep, a.pos);
    } else if (s == uint8_t(WalkerState::Die)) {
        walkerDie(env, a, f);
    } else if (s <= uint8_t(WalkerState::LastMode)) {
        walkerMode(env, a, f, WalkerMode(s - uint8_t(WalkerState::FirstMode)));
    } else {
        notPortedAt(kStoryPort, "0x47e430 state (table 0x891334)", s);
        return;
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim == a.prevAnim) animStepPose(a); // exe 0x5e1e80
    else animSetPose(a, a.anim, 0);            // exe 0x5e1c30
    const int8_t mode = int8_t(f.mode);
    f.hitBox.result = 0;
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    if (mode & kWalkerNoHitEntry) return;
    const ShortVec4& bone = bonePoint(a, 0, kStoryPort); // exe 0x5e1810
    const int16_t pos[3] = {bone.x, bone.y, bone.z};
    const uint32_t owner = hitOwner(a.slot) | ((uint32_t(int32_t(mode)) & kWalkerNotTarget) << 12) |
                           hitGroup(kHitGroupBodies) | kHitContactMotion | hitRadius(0x48);
    const uint32_t attack = a.state == uint8_t(WalkerState::Die)
                                ? kHitAttackBump
                                : (uint32_t(animatedStats(a.type).attack) | kHitAttackThrown | kHitAttackSolid);
    hitListSubmit(pos, f.hitBox, owner, attack); // exe 0x5e4120
    // exe 0x5f20f0(actor, 0x30, 0x160, 0): the shadow (render output)
}

// ---- 0x4892c0 (pool 0x20 type 0x26: ST13): the gun turret ---------------------------------------------------
// Armed (kTurretArmed) once flag 0x5b6 is set. Sub-states [0x8923b4]: 0 / 1 idle (animation 7 / 1, a 0xf0-tick timer);
// the player within 0x400 (planar, from its bone 1): it aims (animation 2 or 6, sound 0x122 / 0x123), 2 fires a burst
// of shots (stage pool-0x40 type 0xd) from frame 0xe (exe 0x489eb0: the yaw spread +-0x100 around its turned head,
// sound 0x124), 3 keeps firing for 4 more ticks, then reloads (4: 0x20 ticks or the player beyond 0xa00) or (5)
// re-aims; the damage (exe 0x489cc0): the body box and the two barrel boxes (bones 2 / 3); below 1/2, 1/4, 1/8 of its
// hit points it staggers (6); the special weapons' pushes count double; armed and out of hit points -> 8: sparks every
// 8 ticks for 0x3c ticks, then dies (state 2: a blast at bone 1, the drops). The head follows the player while
// kTurretTracking (its yaw / pitch added to the decoded frame's part 1)
enum class TurretState : uint8_t { Init = 0, Run = 1, Die = 2, FirstSub = 3, LastSub = 11 };
enum class TurretSub : uint8_t {
    Idle = 0,    // exe 0x4898b0
    Wait = 1,    // exe 0x489960
    Aim = 2,     // exe 0x489a00
    Burst = 3,   // exe 0x489a70
    Reload = 4,  // exe 0x489b00
    Reaim = 5,   // exe 0x489b60
    Stagger = 6, // exe 0x489bd0
    Recover = 7, // exe 0x489c20
    Dying = 8,   // exe 0x489c80
};

// +0x14c
constexpr uint16_t kTurretArmed = 0x001;
constexpr uint16_t kTurretAiming = 0x002;
constexpr uint16_t kTurretTracking = 0x004;   // the head follows the player
constexpr uint16_t kTurretGuarded = 0x008;    // the body deflects (its animation shields it)
constexpr uint16_t kTurretHurt = 0x010;       // no damage taken (until the stagger ends)
constexpr uint16_t kTurretHalfDown = 0x020, kTurretQuarterDown = 0x040, kTurretEighthDown = 0x080;
constexpr uint16_t kTurretRecovering = 0x100; // the hits only re-arm the recovery's timer
constexpr uint32_t kFlagTurretsArmed = 0x5b6;

constexpr uint8_t kTurretShotType = 0xd;
constexpr uint16_t kSoundAim = 0x122, kSoundAimAlt = 0x123, kSoundBurst = 0x124;
constexpr uint32_t kAimRange = 0x400, kLoseRange = 0xa00, kNearShotRange = 0x200;
constexpr uint16_t kTintDying = 0x3198;
constexpr int16_t kHeadStep = 0x40, kHeadLimit = 0x280;
constexpr int32_t kAimAbovePlayer = int32_t(0xff800000u); // the point aimed at: 0x80 above the player
constexpr int16_t kBarrelOffsets[2][3] = {{0, -0x20, -0x90}, {0, 0x20, -0x90}}; // on bones 2 / 3

AnimatedActorStats turretStats(const AnimatedActor& a) {
    if (!hasAnimatedStats(a.type))
        notPortedAt(kStoryPort, "0x4892c0: null type row (the exe reads near address 0)", a.type);
    return animatedStats(a.type);
}
void toSub(AnimatedActor& a, TurretSub s) { a.subState = uint8_t(s); }
// the aim: animation 2 (rng & mask == 0) or 6
void turretAim(AnimatedActor& a, TurretFields& f, uint32_t mask) {
    f.flags |= kTurretAiming;
    if ((rngStep() & mask) == 0) {
        a.anim = 2;
        soundPlayAtPos(kSoundAim, a.pos); // exe 0x5d5380
    } else {
        a.anim = 6;
        soundPlayAtPos(kSoundAimAlt, a.pos);
    }
    f.startKey = 0;
    f.bursts = 0;
    toSub(a, TurretSub::Aim);
}
// exe 0x489eb0: a shot from 0x10 ahead of bone 1, along the head's yaw (spread) and pitch
void turretShot(AnimatedActor& a, TurretFields& f, uint8_t variant) {
    const uint32_t address = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!address) return;
    int16_t p[3];
    bonePointAhead(a, 1, 0x10, p); // exe 0x5e18d0
    const uint32_t r1 = rngStep();
    const uint32_t r2 = rngStep();
    ProjectileActor& s = projectileActorAt(address);
    s.variant = variant;
    s.loopFlags |= kActorSweepGroup2;
    s.flags = kActorLive;
    s.type = kTurretShotType;
    s.pos.x.whole = p[0];
    s.pos.y.whole = p[1];
    s.pos.z.whole = p[2];
    s.rotation.x = 0;
    s.rotation.z = 0;
    s.velocity.x = 0;
    s.rotation.y = int16_t((((r2 >> 6) & 0x1fe) - 0x100u + uint16_t(f.headYaw) + uint16_t(a.rotation.y)) & kAngleMask);
    const int pitch = int(uint16_t(f.headPitch) & kAngleMask);
    s.velocity.y = int16_t(uint32_t(((int32_t(fixedSin(pitch)) * 7) << 7) >> 12) + (r1 & 0x1ff) - 0x100u);
    s.velocity.z = int16_t(uint32_t((-(int32_t(fixedCos(pitch)) * 7) << 7) >> 12));
    if (f.bursts == 0) s.velocity.y = int16_t(uint16_t(s.velocity.y) + 0x200u);
    else if (f.playerDistance > kNearShotRange) s.velocity.y = int16_t(uint16_t(s.velocity.y) - 0x80u);
    s.acceleration.x = 0;
    s.acceleration.y = 0x10;
    s.acceleration.z = 0;
    s.setParamsWord(0);
}
// exe 0x48a010: a small blast at a random point 0x80 ahead of bone 1
void turretSparks(AnimatedActor& a) {
    int16_t p[3];
    bonePointAhead(a, 1, 0x80, p);
    p[0] = int16_t(p[0] + int16_t((rngStep() & 0x7f) - 0x40));
    p[1] = int16_t(p[1] + int16_t(((rngStep() >> 4) & 0x7f) - 0x40));
    p[2] = int16_t(p[2] + int16_t(((rngStep() >> 8) & 0x7f) - 0x40));
    const uint32_t address = allocActor60(0); // exe 0x5e1300
    if (!address) return;
    StaticActor& b = staticActorAt(address);
    b.flags = kActorLive;
    b.loopFlags |= kActorSweepGroup2;
    b.type = typeByte(GlobalStaticType::Blast);
    b.variant = 0;
    b.pos.x.whole = p[0];
    b.pos.y.whole = p[1];
    b.pos.z.whole = p[2];
    BlastParams params(b);
    params.size = 0x40;
    params.attack = 0;
}
void turretStagger(AnimatedActor& a, TurretFields& f) {
    toSub(a, TurretSub::Stagger);
    a.anim = 3;
    f.startKey = 0;
}
// exe 0x489cc0: the body's and the barrels' damage (the special weapons' pushes double); dead -> the dying sparks
// (aiming) or state 2; the staggers below 1/2, 1/4, 1/8 of its hit points, else the recovery
void turretDamage(AnimatedActor& a, TurretFields& f) {
    const uint32_t body = f.bodyBox.result, barrels = f.barrelBox.result;
    int32_t d = 0;
    if (!(f.flags & kTurretRecovering)) {
        if ((body & (kHitAttackByPlayer | kHitAttackThrown)) && !(f.flagByte() & kTurretGuarded)) {
            a.tint = kTintFlash;
            d = int32_t(body & kHitDamageMask);
        }
        if ((barrels & (kHitAttackByPlayer | kHitAttackThrown)) && !(f.flagByte() & kTurretGuarded)) {
            a.tint = kTintFlash;
            d += int32_t(barrels & kHitDamageMask);
        }
    } else if ((body & kHitAttackByPlayer) || (barrels & kHitAttackByPlayer)) {
        f.timer = 0x96;
    }
    if (body & kHitAttackStrongPush) {
        a.tint = kTintFlash;
        d += int32_t(body & kHitDamageMask) * 2;
    }
    if (barrels & kHitAttackStrongPush) {
        a.tint = kTintFlash;
        d += int32_t(barrels & kHitDamageMask) * 2;
    }
    if (d == 0) return;
    f.flags &= uint16_t(~kTurretRecovering);
    if (hitPointsAdd(a, -d)) { // exe 0x5e52f0
        f.setFlagBits(kTurretHurt);
        if (f.flags & kTurretAiming) {
            toSub(a, TurretSub::Dying);
            a.anim = 4;
            f.startKey = 0;
            f.timer = 0x3c;
        } else {
            a.state = uint8_t(TurretState::Die);
        }
        return;
    }
    if (!(f.flags & kTurretAiming)) return;
    f.flags |= kTurretHurt;
    bool staggered = false;
    if (!(f.flagByte() & kTurretHalfDown) && a.hitPoints < int16_t(a.maxHitPoints >> 1)) {
        turretStagger(a, f);
        f.setFlagBits(kTurretHalfDown);
        staggered = true;
    }
    if (!(f.flagByte() & kTurretQuarterDown) && a.hitPoints < int16_t(a.maxHitPoints >> 2)) {
        turretStagger(a, f);
        f.flags |= kTurretQuarterDown;
        staggered = true;
    }
    if (!(f.flagByte() & kTurretEighthDown) && a.hitPoints < int16_t(a.maxHitPoints >> 3)) {
        turretStagger(a, f);
        f.flags |= kTurretEighthDown;
        return;
    }
    if (staggered) return;
    toSub(a, TurretSub::Recover);
    a.anim = 5;
    f.startKey = 0;
    f.timer = 0x96;
    f.flags = uint16_t((f.flags & 0xffeb) | kTurretRecovering);
}
void turretSub(AnimatedActor& a, TurretFields& f, uint8_t sub) {
    switch (TurretSub(sub)) { // table 0x8923b4
    case TurretSub::Idle:
        if (!a.animEnded()) return;
        ++f.timer;
        if (f.timer == 2) {
            f.timer = 0xf0;
            a.anim = 7;
            toSub(a, TurretSub::Wait);
        }
        if (f.playerDistance <= kAimRange && (f.flagByte() & kTurretArmed)) turretAim(a, f, 7);
        return;
    case TurretSub::Wait:
        --f.timer;
        if (f.timer == 0) {
            a.anim = 1;
            f.startKey = 0;
            toSub(a, TurretSub::Idle);
            return;
        }
        if (f.playerDistance <= kAimRange && (f.flagByte() & kTurretArmed)) turretAim(a, f, 3);
        return;
    case TurretSub::Aim:
        if (a.keyIndex == 0xe) soundPlayAtPos(kSoundAimAlt, a.pos);
        if (a.animKey.frame >= 0xe) turretShot(a, f, uint8_t(0x10 - int16_t(f.timer)));
        if (a.animEnded()) {
            f.setFlagBits(kTurretTracking);
            f.timer = 0x10;
            toSub(a, TurretSub::Burst);
            soundPlayAtPos(kSoundBurst, a.pos);
        }
        return;
    case TurretSub::Burst:
        if (int16_t(f.timer) > 0xb) turretShot(a, f, uint8_t(0x10 - int16_t(f.timer)));
        --f.timer;
        if (f.timer != 0) return;
        if (rngStep() & 1) {
            f.timer = 0x20;
            a.anim = 0;
            f.startKey = 0;
            ++f.bursts;
            toSub(a, TurretSub::Reload);
        } else {
            f.startKey = 0;
            ++f.bursts;
            a.anim = 5;
            toSub(a, TurretSub::Reaim);
        }
        return;
    case TurretSub::Reload:
        if (f.playerDistance > kLoseRange) {
            f.flags &= uint16_t(~(kTurretAiming | kTurretTracking));
            f.timer = 0xf0;
            a.anim = 5;
            f.startKey = 0;
            toSub(a, TurretSub::Wait);
            return;
        }
        --f.timer;
        if (f.timer == 0) {
            f.startKey = 0;
            a.anim = 5;
            toSub(a, TurretSub::Reaim);
        }
        return;
    case TurretSub::Reaim:
        if (!a.animEnded()) return;
        if ((rngStep() & 3) == 0) {
            a.anim = 2;
            soundPlayAtPos(kSoundAim, a.pos);
        } else {
            a.anim = 6;
            soundPlayAtPos(kSoundAimAlt, a.pos);
        }
        f.startKey = 0;
        toSub(a, TurretSub::Aim);
        return;
    case TurretSub::Stagger:
        if (!a.animEnded()) return;
        f.timer = 0x20;
        f.flags = uint16_t((f.flags & uint16_t(~kTurretHurt)) | kTurretTracking);
        a.anim = 0;
        f.startKey = 0;
        toSub(a, TurretSub::Reload);
        ++f.bursts;
        return;
    case TurretSub::Recover:
        --f.timer;
        if (f.timer != 0) return;
        f.flags = uint16_t((f.flags & uint16_t(~(kTurretRecovering | kTurretHurt))) | kTurretTracking);
        a.anim = 2;
        f.startKey = 0;
        ++f.bursts;
        toSub(a, TurretSub::Aim);
        soundPlayAtPos(kSoundAim, a.pos);
        return;
    case TurretSub::Dying:
        a.tint = kTintDying;
        if (!(uint8_t(f.timer) & 7)) turretSparks(a);
        --f.timer;
        if (f.timer == 0) a.state = uint8_t(TurretState::Die);
        return;
    default: notPortedAt(kStoryPort, "0x489390 sub-state (table 0x8923b4)", sub);
    }
}
// the head toward the player's direction while tracking (within +-0x280), else back to straight
void turnHead(TurretFields& f) {
    const int16_t yaw = f.headYaw;
    if (f.flags & kTurretTracking) {
        const int d = f.playerYaw - yaw;
        if (d > kHeadStep) f.headYaw = yaw < kHeadLimit ? int16_t(yaw + kHeadStep) : kHeadLimit;
        else if (d < -kHeadStep) f.headYaw = yaw > -kHeadLimit ? int16_t(yaw - kHeadStep) : int16_t(-kHeadLimit);
        else if (yaw < kHeadLimit && yaw > -kHeadLimit) f.headYaw = f.playerYaw;
        f.headPitch = f.playerPitch;
        return;
    }
    for (int16_t* angle : {&f.headYaw, &f.headPitch}) {
        const int16_t v = *angle;
        *angle = v > kHeadStep ? int16_t(v - kHeadStep) : v < -kHeadStep ? int16_t(v + kHeadStep) : int16_t(0);
    }
}
// exe 0x489390: the player measured from bone 1, the sub-state, the head, the damage, the pose with the head's turn,
// the hit entries (the body; the two barrels)
void turretRun(AnimatedActor& a, TurretFields& f) {
    const AnimatedActorStats stats = turretStats(a);
    const ShortVec4& bone = bonePoint(a, 1, kStoryPort); // exe 0x5e1810
    const Player& p = player();
    const int32_t aimed[3] = {p.pos.x, int32_t(uint32_t(p.pos.y) + uint32_t(kAimAbovePlayer)), p.pos.z};
    const int32_t from[3] = {int32_t(uint32_t(int32_t(bone.x)) << 16), int32_t(uint32_t(int32_t(bone.y)) << 16),
                             int32_t(uint32_t(int32_t(bone.z)) << 16)};
    a.tint = kTintNeutral;
    a.prevAnim = a.anim;
    a.savePreviousPose();
    f.playerDistance = uint32_t(planarDistance(p.pos, from));                          // exe 0x5e3b00
    f.playerYaw = int16_t(uint32_t(angleOf(aimed[0], aimed[2], from[0], from[2])) - uint16_t(a.rotation.y)); // 0x5e3bc0
    f.playerPitch = int16_t(0x800u - uint32_t(pitchOf(aimed, from)));                 // exe 0x5e3ca0
    if (f.playerYaw < -kHalfTurn) f.playerYaw = int16_t(uint16_t(f.playerYaw) + 0x1000u);
    if (f.playerYaw >= kHalfTurn) f.playerYaw = int16_t(uint16_t(f.playerYaw) - 0x1000u);
    turretSub(a, f, a.subState);
    const uint16_t flags = f.flags;
    turnHead(f);
    if (!(flags & kTurretHurt)) turretDamage(a, f);
    if (a.anim != a.prevAnim) animSet(a, a.anim, f.startKey); // exe 0x5e1c50
    else animStep(a);                                         // exe 0x5e1ea0
    LaneVec4 head = decodedFrame().entries[2];               // part 1's angles
    head.y = int16_t(uint16_t(f.headYaw) + uint16_t(head.y));
    head.x = int16_t(uint16_t(f.headPitch >> 1) + uint16_t(head.x));
    animPose(a); // exe 0x5e2d90
    f.bodyBox.result = 0;
    f.bodyBox.previousX = a.prevPos.x.whole;
    f.bodyBox.previousZ = a.prevPos.z.whole;
    f.barrelBox.result = 0;
    f.barrelBox.previousX = a.prevPos.x.whole;
    f.barrelBox.previousZ = a.prevPos.z.whole;
    uint32_t bodyAttack;
    const uint8_t anim = a.anim;
    const bool openAnim = anim == 0 || anim == 2 || anim == 6;
    if (openAnim || a.subState == uint8_t(TurretSub::Dying)) {
        bodyAttack = openAnim ? kHitAttackSolid : kHitAttackBump;
        f.flags &= uint16_t(~kTurretGuarded);
    } else {
        f.setFlagBits(kTurretGuarded);
        bodyAttack = kHitAttackDeflects;
    }
    uint32_t unarmed = 0;
    if (!(f.flagByte() & kTurretArmed)) {
        unarmed = kHitNoLockOn;
        bodyAttack |= kHitAttackHarmless;
    }
    const ShortVec4& centre = bonePoint(a, 1, kStoryPort);
    const int16_t pos[3] = {centre.x, centre.y, centre.z};
    hitListSubmit(pos, f.bodyBox, hitOwner(a.slot) | unarmed | hitGroup(kHitGroupBodies) | hitRadius(0x38),
                  uint32_t(stats.attack) | bodyAttack); // exe 0x5e4120
    const uint32_t barrelAttack = (f.flagByte() & kTurretGuarded) ? (kHitAttackHarmless | kHitAttackDeflects)
                                  : a.subState != uint8_t(TurretSub::Dying) ? (kHitAttackHarmless | kHitAttackSolid)
                                                                            : (kHitAttackHarmless | kHitAttackBump);
    for (uint32_t k = 0; k < 2; ++k) {
        int16_t q[3];
        bonePointOffset(a, 2 + k, kBarrelOffsets[k], q); // exe 0x5e1970 (the stack offsets)
        hitListSubmit(q, f.barrelBox, hitOwner(a.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x38),
                      uint32_t(stats.attack) | barrelAttack);
    }
}
// exe 0x489800: a blast 0x80 ahead of bone 1, the drops there, freed
void turretDie(AnimatedActor& a) {
    const AnimatedActorStats stats = turretStats(a);
    int16_t p[3];
    bonePointAhead(a, 1, 0x80, p);            // exe 0x5e18d0
    if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300
        StaticActor& b = staticActorAt(address);
        b.flags = kActorLive;
        b.loopFlags |= kActorSweepGroup2;
        b.type = typeByte(GlobalStaticType::Blast);
        b.variant = 0;
        b.pos.x.whole = p[0];
        b.pos.y.whole = p[1];
        b.pos.z.whole = p[2];
        BlastParams params(b);
        params.size = 0x100;
        params.attack = 0x20;
    }
    // the stack position: only its integer halves written
    const int32_t at[3] = {int32_t(uint32_t(uint16_t(p[0])) << 16), int32_t(uint32_t(uint16_t(p[1])) << 16),
                           int32_t(uint32_t(uint16_t(p[2])) << 16)};
    spawnDrops(at, stats.dropTable); // exe 0x5f1270
    freeActor(a);                    // exe 0x5e1490
}
// exe 0x4892c0
void turretUpdate(AnimatedActor& a) {
    TurretFields& f = classState<TurretFields>(a);
    switch (TurretState(a.state)) { // table 0x8923a8
    case TurretState::Init: {             // exe 0x4892e0
        if (!bindActorModel(a, 0)) { // exe 0x5e0ba0
            freeActor(a);
            return;
        }
        const AnimatedActorStats stats = turretStats(a);
        f.flags = 0;
        f.clearHead();
        f.timer = uint16_t((a.rotation.y >> 4) + 1);
        f.bodyBox.result = 0;
        f.barrelBox.result = 0;
        a.hitPoints = int16_t(stats.hitPoints);
        a.maxHitPoints = int16_t(stats.hitPoints);
        a.anim = 7;
        f.startKey = 0;
        if (storyFlag(kFlagTurretsArmed)) f.setFlagBits(kTurretArmed); // exe 0x510e50
        animSetPose(a, a.anim, f.startKey);                             // exe 0x5e1c30
        f.unused = 0;
        a.state = uint8_t(TurretState::Run);
        toSub(a, TurretSub::Wait);
        return;
    }
    case TurretState::Run: turretRun(a, f); return;
    case TurretState::Die: turretDie(a); return;
    default:
        if (a.state <= uint8_t(TurretState::LastSub)) { // states 3..11: the sub-state routines themselves (table
            turretSub(a, f, uint8_t(a.state - uint8_t(TurretState::FirstSub))); // 0x8923a8 overlaps 0x8923b4)
            return;
        }
        notPortedAt(kStoryPort, "0x4892c0 state (table 0x8923a8)", a.state);
    }
}

} // namespace

void st13GunTurretUpdate(const PlayerEnv&, uint32_t actor) { turretUpdate(animatedActorAt(actor)); }
void dropInWalkerUpdate(const PlayerEnv& env, uint32_t actor) { walkerUpdate(env, animatedActorAt(actor)); }
void tumblingBlockUpdate(const PlayerEnv&, uint32_t actor) { tumbleUpdate(staticActorAt(actor)); }
void rollingBoulderUpdate(const PlayerEnv& env, uint32_t actor) { boulderUpdate(env, staticActorAt(actor)); }

} // namespace od::port
