// The pirate base's carrier (ST3B sub 0, story 12: static type 0x49, exe 0x5b4000; hypothesis names), one routine per
// exe routine.
//
// Update: the tint neutral, the previous pose and contact bytes, the state [0x8d0864]; then, while live, a stand-on
// solid. The shadow (exe 0x5f20f0(a, 0x100, 0x80, 1)) is render output and left out.
// States: 0 bind 0x5b4080, 1 run 0x5b4110 -- variant 0 plays the scenes [0x8d086c] (0x5b4130), the others are the
// talk prop 0x5b4a00 (message 0x28).
// Scenes (variant 0; the script's step sub-id moves them on, each ends with the player kept at a point on the carrier,
// 0x5b4830):
//   0 0x5b4150 the scale pulses (0x201 on odd ticks of the script's step tick)
//   1 0x5b41c0 sinks while turning a full circle, then sinks faster
//   2 0x5b42e0 placed at (0x900, -0x600), turns, rises and slows, then turns back sinking; its height follows its roll
//   3 0x5b44f0 x = 0x600 on the ground, forward -0x300
//   4 0x5b4560 placed at (-0x700, -0xc00), yaw 0x800, forward -0x300
//   5 0x5b4610 z = -0x200, forward -0x300 (the player's point from the table 0x8d0860 by the step tick 10..0x17);
//     past z 0xc00 -> 6
//   6 0x5b46e0 4 blast rings (offsets 0x8d08c0), 6 smoke columns (stage static type 0x26 variant 2, 0x8d08e0), a camera
//     shake, freed
// Effects: 0x5b48a0 the hit spark (effect type 0 variant 3), 0x5b4950 the skid sparks (type 3 variant 0x17) at the
// rotated point k of 0x8d0910 (every 4th tick).
#include "content/saul_kada/st3b_pirate_carrier.hpp"

#include "engine/data_source.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/camera.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/explosions.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/skid_sparks.hpp"
#include "game/story.hpp"
#include "game/table_records.hpp"

namespace od::port {

namespace {

enum class CarrierState : uint8_t { Bind = 0, Run = 1 };
// [0x8d086c] (variant 0), moved on by the script's step sub-id
enum class CarrierScene : uint8_t { Wait = 0, Sink = 1, Swing = 2, Ashore = 3, Approach = 4, Leave = 5, Burst = 6 };
enum class SinkStep : uint8_t { Start = 0, Turn = 1, Dive = 2 };
// [0x5b44d8]
enum class SwingStep : uint8_t { Place = 0, Turn = 1, Rise = 2, Fall = 3 };
// the talk prop's sub-states (variant != 0)
enum class TalkState : uint8_t { Idle = 0, Talking = 1 };

// A point of the carrier's effects (exe 0x8d0910, 8 bytes: s16 x / y / z, >> 4)
const DataTable<RecordTable<ShortPoint, 4, 8>> effectPoints{TableId::St3bPirateCarrierEffectPoints};
// The player's place during the leave scene by the step tick (exe 0x8d0860: s16 x / z)
struct PlayerSpot : DataRecord {
    int16_t x = 0;
    int16_t z = 0;
    static PlayerSpot load(const TableReader& r) {
        PlayerSpot rec;
        rec.x = r.s16(0);
        rec.z = r.s16(2);
        return rec;
    }
};
const DataTable<RecordTable<PlayerSpot, 0x18, 4>> leaveSpots{TableId::St3bPirateCarrierLeaveSpots};
// A blast ring of the burst (exe 0x8d08c0, 8 bytes): its offset from the carrier and its size
struct BurstRing : DataRecord {
    uint16_t dx = 0;
    uint16_t dy = 0;
    uint16_t dz = 0;
    uint16_t size = 0;
    static BurstRing load(const TableReader& r) {
        BurstRing rec;
        rec.dx = r.u16(0);
        rec.dy = r.u16(2);
        rec.dz = r.u16(4);
        rec.size = r.u16(6);
        return rec;
    }
};
const DataTable<RecordTable<BurstRing, 4, 8>> burstRings{TableId::St3bPirateCarrierBurstRings};
// A smoke column of the burst (exe 0x8d08e0, 8 bytes): its velocity's words as the exe copies them; the yaw is the
// short after the velocity's z
struct BurstColumn : DataRecord {
    uint32_t velocityXY = 0;
    uint32_t velocityZYaw = 0;
    int16_t yaw = 0;
    static BurstColumn load(const TableReader& r) {
        BurstColumn rec;
        rec.velocityXY = r.u32(0);
        rec.velocityZYaw = r.u32(4);
        rec.yaw = r.s16(6);
        return rec;
    }
};
const DataTable<RecordTable<BurstColumn, 6, 8>> burstColumns{TableId::St3bPirateCarrierBurstColumns};
// The smoke column's life (stage static type 0x26: the short after its velocity)
struct SmokeColumnFields {
    explicit SmokeColumnFields(const StaticActor& actor) : SmokeColumnFields(const_cast<StaticActor&>(actor)) {}
    explicit SmokeColumnFields(StaticActor& actor) : life(actor.velocity.spare) {}
    int16_t& life;
};

constexpr uint8_t kHitSparkVariant = 3, kSkidSparksVariant = 0x17;
constexpr uint8_t kRingVariant = 0x10;
constexpr uint8_t kSmokeColumnType = 0x26, kSmokeColumnVariant = 2;
constexpr int16_t kSmokeColumnLife = 0x3c;
constexpr uint16_t kSparkSize = 0xc0;
constexpr int16_t kScaleOne = 0x200, kScalePulse = 0x201;
constexpr uint16_t kTalkMessage = 0x28;
// the player's place on the carrier
constexpr int kDeckY = -0x700, kDeckZ = -0x380;

bool everyFourthTick() { return (gameState().frame & 3) == 0; }

void clampMin(int16_t& value, int16_t lo) {
    if (value < lo) value = lo;
}

// exe 0x5b4830(a, x, y, z): the player at the carrier's position + Ry Rx Rz(rotation) * (x, y, z), facing its yaw
void keepPlayerOnDeck(const StaticActor& actor, int x, int y, int z) {
    int32_t offset[3];
    rotateVector(actor.rotation, x, y, z, offset); // exe 0x5e3970
    Player& p = player();
    p.pos.x = actor.pos.x + offset[0];
    p.pos.y = actor.pos.y + offset[1];
    p.pos.z = actor.pos.z + offset[2];
    p.rotation.y = actor.rotation.y;
}
// the effect of exe 0x5b48a0 / 0x5b4950: an effect at the point k rotated by the carrier's rotation (<< 4, >> 16) +
// its whole position; nullptr when the pool is full
EffectActor* effectAtPoint(const StaticActor& actor, uint32_t k) {
    const ShortPoint point = effectPoints[k];
    int32_t offset[3];
    rotateVector(actor.rotation, int(point.x) << 4, int(point.y) << 4, int(point.z) << 4, offset); // exe 0x5e3970
    const uint32_t slot = allocActorA0(0); // exe 0x5e13b0
    if (slot == 0) return nullptr;
    EffectActor& effect = effectActorAt(slot);
    effect.flags = kActorLive;
    effect.loopFlags |= kActorSweepGroup2;
    effect.pos.x = int16_t((offset[0] >> 16) + actor.pos.x.whole);
    effect.pos.y = int16_t((offset[1] >> 16) + actor.pos.y.whole);
    effect.pos.z = int16_t((offset[2] >> 16) + actor.pos.z.whole);
    return &effect;
}
// exe 0x5b48a0: a hit spark of the size at the point k
void hitSparkAt(const StaticActor& actor, uint32_t k, uint16_t size) {
    if (EffectActor* spark = effectAtPoint(actor, k)) {
        spark->type = typeByte(GlobalEffectType::HitSpark);
        spark->variant = kHitSparkVariant;
        classState<HitSparkFields>(effectActorAt(spark->at)).size = size;
    }
}
// exe 0x5b4950(a, k, 0x40): skid sparks at the point k, turned away from the carrier's yaw (the third argument unused)
void skidSparksAt(const StaticActor& actor, uint32_t k) {
    if (EffectActor* sparks = effectAtPoint(actor, k)) {
        sparks->type = typeByte(GlobalEffectType::SkidSparks);
        sparks->variant = kSkidSparksVariant;
        classState<SkidSparkFields>(*sparks).yaw = uint16_t(uint16_t(actor.rotation.y) + 0x800u);
    }
}
// the scenes' tails: the step sub-id `cue` moves the carrier on to that scene
void followCue(StaticActor& actor, CarrierScene cue) {
    if (scriptState().stepSub == uint8_t(cue)) {
        actor.subState = uint8_t(cue);
        actor.restartStep();
    }
}
void setScale(StaticActor& actor, int16_t s) {
    actor.scale.z = s;
    actor.scale.y = s;
    actor.scale.x = s;
}

// exe 0x5b4150
void waitScene(StaticActor& actor) {
    setScale(actor, (scriptState().stepTick & 1) ? kScalePulse : kScaleOne);
    if (scriptState().stepSub == uint8_t(CarrierScene::Sink)) {
        actor.subState = uint8_t(CarrierScene::Sink);
        actor.restartStep();
        setScale(actor, kScaleOne);
    }
}
// exe 0x5b41c0
void sinkScene(StaticActor& actor) {
    switch (SinkStep(actor.step)) {
    case SinkStep::Start:
        actor.rotation.y &= 0xfff;
        ++actor.step;
        actor.velocity.z = 0;
        [[fallthrough]];
    case SinkStep::Turn:
        actor.velocity.z -= 0x10;
        clampMin(actor.velocity.z, -0x180);
        actor.rotation.y += 0x14;
        if (actor.rotation.y >= 0x1000) {
            actor.rotation.y = 0;
            ++actor.step;
        }
        if (everyFourthTick()) {
            skidSparksAt(actor, 2);
            skidSparksAt(actor, 3);
        }
        break;
    case SinkStep::Dive:
        actor.velocity.z -= 0x20;
        clampMin(actor.velocity.z, -0x300);
        if (everyFourthTick()) {
            hitSparkAt(actor, 0, kSparkSize);
            hitSparkAt(actor, 1, kSparkSize);
            skidSparksAt(actor, 2);
            skidSparksAt(actor, 3);
        }
        break;
    default: break;
    }
    integrateVelocity(actor, 0, 0, actor.velocity.z); // exe 0x5e3690
    followCue(actor, CarrierScene::Swing);
    keepPlayerOnDeck(actor, 0, kDeckY, kDeckZ);
}
// exe 0x5b42e0 (the roll's acceleration in the spin's x lane, the turn speed in its y lane)
void swingScene(StaticActor& actor) {
    switch (SwingStep(actor.step)) {
    case SwingStep::Place: // 0x5b4308
        ++actor.step;
        actor.pos.x = 0x9000000;
        actor.pos.z = int32_t(0xfa000000u);
        actor.rotation.y = 0x200;
        actor.rotation.z = 0x100;
        actor.spin.x = 0;
        actor.spin.y = 0x20;
        [[fallthrough]];
    case SwingStep::Turn: // 0x5b4331
        moveAlongYaw(actor.pos, 0, 0, 0, actor.velocity.z); // exe 0x5e36f0
        actor.rotation.y += uint16_t(actor.spin.y);
        if (actor.rotation.y > 0x400) ++actor.step;
        if (everyFourthTick()) {
            hitSparkAt(actor, 1, kSparkSize);
            hitSparkAt(actor, 3, kSparkSize);
        }
        break;
    case SwingStep::Rise: { // 0x5b4385
        const uint8_t step = actor.step;
        actor.velocity.z += 0x30;
        if (actor.velocity.z >= 0) {
            actor.step = uint8_t(step + 1u);
            actor.velocity.z = 0;
        }
        actor.spin.y -= 2;
        clampMin(actor.spin.y, 0);
        const int speed = actor.velocity.z;
        actor.rotation.y += uint16_t(actor.spin.y);
        moveAlongYaw(actor.pos, 0, 0, 0, speed); // exe 0x5e36f0
        if (everyFourthTick()) {
            hitSparkAt(actor, 1, kSparkSize);
            hitSparkAt(actor, 3, kSparkSize);
        }
        break;
    }
    case SwingStep::Fall: // 0x5b43f2
        actor.velocity.z -= 0x40;
        clampMin(actor.velocity.z, -0x400);
        moveAlongYaw(actor.pos, actor.rotation.y, 0, 0, actor.velocity.z); // exe 0x5e36f0
        actor.rotation.y -= 0x10;
        clampMin(actor.rotation.y, 0x400);
        actor.spin.x += 4;
        actor.rotation.z -= uint16_t(actor.spin.x);
        clampMin(actor.rotation.z, 0);
        if (everyFourthTick()) {
            hitSparkAt(actor, 0, kSparkSize);
            hitSparkAt(actor, 1, kSparkSize);
            skidSparksAt(actor, 2);
            skidSparksAt(actor, 3);
        }
        break;
    default: break;
    }
    // 0x5b4482: height = (-0x68 * sin(roll)) >> 12 (the exe's ((x * 7 * 2 - x) * -8) >> 12)
    const int sine = fixedSin(actor.rotation.z);
    actor.pos.y.whole = int16_t((-(sine * 0xd) * 8) >> 12);
    followCue(actor, CarrierScene::Ashore);
    keepPlayerOnDeck(actor, 0, kDeckY, kDeckZ);
}
// exe 0x5b44f0
void ashoreScene(const PlayerEnv& env, StaticActor& actor) {
    if (actor.step == 0) {
        actor.pos.x = 0x6000000;
        actor.pos.y.whole = int16_t(actorGround(env, actor)); // exe 0x40f190
        ++actor.step;
    }
    moveAlongYaw(actor.pos, actor.rotation.y, 0, 0, -0x300); // exe 0x5e36f0
    followCue(actor, CarrierScene::Approach);
    keepPlayerOnDeck(actor, 0, kDeckY, kDeckZ);
}
// exe 0x5b4560
void approachScene(StaticActor& actor) {
    if (actor.step == 0) {
        actor.pos.x = int32_t(0xf9000000u);
        actor.pos.z = int32_t(0xf4000000u);
        actor.rotation.y = 0x800;
        actor.step = 1;
    }
    moveAlongYaw(actor.pos, actor.rotation.y, 0, 0, -0x300); // exe 0x5e36f0
    keepPlayerOnDeck(actor, 0, kDeckY, kDeckZ);
    followCue(actor, CarrierScene::Leave);
    if (everyFourthTick())
        for (uint32_t k = 0; k < 4; ++k) hitSparkAt(actor, k, kSparkSize);
}
// exe 0x5b4610
void leaveScene(StaticActor& actor) {
    if (actor.step == 0) {
        actor.pos.z = int32_t(0xfe000000u);
        actor.step = 1;
    }
    moveAlongYaw(actor.pos, actor.rotation.y, 0, 0, -0x300); // exe 0x5e36f0
    const int32_t tick = scriptState().stepTick;
    if (tick < 10) {
        keepPlayerOnDeck(actor, 0, kDeckY, kDeckZ);
    } else if (tick <= 0x17) {
        const PlayerSpot spot = leaveSpots[size_t(tick)];
        keepPlayerOnDeck(actor, -int(spot.x), kDeckY, int(spot.z) + kDeckZ);
    }
    if (everyFourthTick()) {
        hitSparkAt(actor, 0, kSparkSize);
        hitSparkAt(actor, 1, kSparkSize);
        skidSparksAt(actor, 2);
        skidSparksAt(actor, 3);
    }
    if (actor.pos.z.whole > 0xc00) {
        actor.subState = uint8_t(CarrierScene::Burst);
        actor.restartStep();
    }
}
// exe 0x5b46e0
void burstScene(StaticActor& actor) {
    switch (actor.step) {
    case 0: {
        for (uint32_t k = 0; k < burstRings.size(); ++k) {
            const BurstRing ring = burstRings[k];
            const uint32_t slot = allocActor60(0); // exe 0x5e1300
            if (slot == 0) continue;
            StaticActor& blast = staticActorAt(slot);
            blast.flags = kActorLive;
            blast.type = typeByte(GlobalStaticType::Blast);
            blast.subtype = 0;
            blast.variant = kRingVariant;
            blast.loopFlags |= kActorSweepGroup2;
            blast.pos.x.whole = int16_t(uint16_t(ring.dx) + uint16_t(actor.pos.x.whole));
            blast.pos.y.whole = int16_t(uint16_t(ring.dy) + uint16_t(actor.pos.y.whole));
            blast.pos.z.whole = int16_t(uint16_t(actor.pos.z.whole) + uint16_t(ring.dz));
            BlastParams params(actorHeaderAt(slot));
            params.size = ring.size;
            params.attack = 0;
        }
        for (uint32_t k = 0; k < burstColumns.size(); ++k) {
            const BurstColumn column = burstColumns[k];
            const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
            if (slot == 0) continue;
            StaticActor& smoke = staticActorAt(slot);
            smoke.flags = kActorLive;
            smoke.loopFlags |= kActorSweepGroup2;
            smoke.type = kSmokeColumnType;
            smoke.subtype = 0;
            smoke.variant = kSmokeColumnVariant;
            smoke.pos = actor.pos;
            smoke.velocity.setWords(column.velocityXY, column.velocityZYaw);
            smoke.rotation.y = column.yaw;
            SmokeColumnFields(smoke).life = kSmokeColumnLife;
            smoke.setParamsWord(0);
        }
        cameraShake(true, 0x800, 0xc0); // exe 0x40ea80
        actor.velocity.y = int16_t(0xf800);
        actor.velocity.z = 0;
        freeActor(actor); // exe 0x5e1540
        return;
    }
    case 1: // 0x5b46f8 (not reached: the step is 0 on entry and the carrier is freed above)
        actor.rotation.x += 0x80;
        actor.rotation.y += 0x80;
        actor.pos.y += uint32_t(int32_t(actor.velocity.y) << 12);
        actor.pos.x += 0x400000u;
        actor.velocity.y += 0x40;
        return;
    default: return;
    }
}
// exe 0x5b4a00: message 0x28 when the player talks to it (no event running)
void talkProp(StaticActor& actor) {
    switch (TalkState(actor.subState)) {
    case TalkState::Idle:
        if (player().talksTo(actor) && eventState().active == 0) { // exe 0x573fa0
            eventStartAt(0, nullptr, 0x12, 0, kTalkMessage);              // exe 0x543b40
            actor.subState = uint8_t(TalkState::Talking);
        }
        return;
    case TalkState::Talking:
        if (eventState().active == 0) actor.subState = uint8_t(TalkState::Idle);
        return;
    default: return;
    }
}

// exe 0x5b4080
void bind(const PlayerEnv& env, StaticActor& actor) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1540
        return;
    }
    actor.hitPoints = 1;
    actor.maxHitPoints = 1;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.velocity.clearWords();
    actor.collisionBox.pointAt(TableId::St3bPirateCarrierBox);
    if (actor.variant == 0) {
        actor.interactionBox = nullptr;
        actor.interaction = 0;
    } else {
        actor.interactionBox.pointAt(TableId::St3bPirateCarrierTalkBox);
        actor.interaction = kInteractTalk;
    }
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock();                    // exe 0x40dc40
    const int ground = actorGround(env, actor); // exe 0x40f190
    actor.enterState(uint8_t(CarrierState::Run));
    actor.pos.y.whole = int16_t(ground);
}
// exe 0x5b4110
void run(const PlayerEnv& env, StaticActor& actor) {
    if (actor.variant != 0) {
        talkProp(actor);
        return;
    }
    switch (CarrierScene(actor.subState)) { // 0x5b4130
    case CarrierScene::Wait: waitScene(actor); return;
    case CarrierScene::Sink: sinkScene(actor); return;
    case CarrierScene::Swing: swingScene(actor); return;
    case CarrierScene::Ashore: ashoreScene(env, actor); return;
    case CarrierScene::Approach: approachScene(actor); return;
    case CarrierScene::Leave: leaveScene(actor); return;
    case CarrierScene::Burst: burstScene(actor); return;
    default: notPortedAt(kStoryPort, "0x5b4130 sub-state", actor.subState);
    }
}

// exe 0x5b4000
void pirateCarrier(const PlayerEnv& env, StaticActor& actor) {
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (CarrierState(actor.state)) { // [0x8d0864]
    case CarrierState::Bind: bind(env, actor); break;
    case CarrierState::Run: run(env, actor); break;
    default: notPortedAt(kStoryPort, "0x5b4000 state", actor.state);
    }
    if (actor.flags & kActorLive) actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050
}

} // namespace

void pirateCarrierUpdate(const PlayerEnv& env, uint32_t actor) { pirateCarrier(env, staticActorAt(actor)); }

} // namespace od::port
