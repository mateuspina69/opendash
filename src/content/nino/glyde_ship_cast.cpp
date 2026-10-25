// Glyde's ship in the cutscenes of his attack on Nino: the pool-0x20 type 0x54 classes driven by the script's timeline
// (its step tick). Models from the alternative type-1 bank the script loads (ST1701 / ST1800 ...). One routine per exe
// routine; the names are hypotheses (Glyde's ship(s), the stage cast of the Nino attack scripts).
//   0x4df510  ST17 (script 0x39, camera script 0x8a6c1c, records 0x8a6a74 / 0x8a6a88): a ship flying in along its
//             heading (variant 0 fast -0x100 / variant 1 slow -0x40 per tick); its animation from the byte table
//             0xa59e6c
//   0x4df6a0  ST18 (scripts 0x3a / 0x3b, camera scripts 0x8a6efc / 0x8a7208), variants [0x8958ec]: 0 still; 1 type-0x16
//             effects at its nose from the tick 0x32 (animation 1 at the tick 0xf); 2 hit by three blasts (ticks 0xa /
//             0x23 / 0x46: static types 7 + 3 at the offsets 0x895900.., a camera shake); 3 destroyed (ticks
//             0xf..0x16: explosions 0x895918, debris type 0x26 0x895930, a type-3 static riding it, then it falls and
//             pitches down); 4 a bobbing hover
#include "content/nino/glyde_ship_cast.hpp"
#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_state.hpp"
#include "game/stage_smoke.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"

namespace od::port {

namespace {

constexpr uint8_t kNoAnim = 0xff;
constexpr uint8_t kRidingSmokeVariant = 9;
constexpr uint8_t kNoseEffectType = 0x16; // the stage's pool-0xa0 screen ripple (content/nino/screen_ripple)
constexpr uint8_t kDebrisType = 0x26;     // the stage's smoke column / debris static
constexpr uint8_t kBlastVariant = 0x10;

// The smoke that rides an actor (the stage's type 3 variant 9): the actor
// the smoke's actor followed (its identity word, the hit box's first word)
struct RidingSmoke {
    explicit RidingSmoke(const StaticActor& actor) : RidingSmoke(const_cast<StaticActor&>(actor)) {}
    explicit RidingSmoke(StaticActor& actor) : followed(actor.hitBox.result) {}
    uint32_t& followed;
};
// The debris' lifetime (the velocity's spare lane of the stage's static type 0x26)
struct DebrisFields {
    explicit DebrisFields(const ProjectileActor& actor) : DebrisFields(const_cast<ProjectileActor&>(actor)) {}
    explicit DebrisFields(ProjectileActor& actor) : ticks(actor.velocity.spare) {}
    int16_t& ticks;
};

int32_t stepTick() { return scriptState().stepTick; }

// the common head of both updates (0x4df510 / 0x4df6a0)
void head(AnimatedActor& actor) {
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
}
// the common tail: live -> the animation stepped (the same) or started at key 0
void tailAnim(AnimatedActor& actor) {
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStepPose(actor); // exe 0x5e1e80
    else animSetPose(actor, actor.anim, 0);               // exe 0x5e1c30(a, anim, 0)
}

// ---- 0x4df510 (states [0x8958bc]) --------------------------------------------------------------------------------
enum class FlyInState : uint8_t { Init = 0, Fly = 1 };
const DataTable<ScalarTable<uint8_t, 4>> flyInAnims{TableId::GlydeShipCastFlyInAnims};
const DataTable<CollisionBoxRecord> flyInBox{TableId::GlydeShipCastFlyInBox};

// exe 0x4df5a0: the model (scenery-drawn), the variant's animation, the hit points of its row * 2, the box, the class
// block
void flyInInit(AnimatedActor& actor) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);           // exe 0x5e1490
        return;
    }
    actor.flags |= kActorScenery;
    actor.prevAnim = kNoAnim;
    actor.anim = flyInAnims[actor.variant];
    const int16_t hp = hasAnimatedStats(actor.type) ? int16_t(animatedStats(actor.type).hitPoints << 1) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.collisionBox.pointAt(flyInBox.stateAddress());
    actor.clearClassBlock(); // exe 0x40dc40
    actor.enterState(uint8_t(FlyInState::Fly));
}
// exe 0x4df640: by the variant [0x8958c4]
void flyInFly(AnimatedActor& actor) {
    switch (actor.variant) {
    case 0: integrateVelocity(actor, 0, 0, -0x100); break; // exe 0x4df660
    case 1: integrateVelocity(actor, 0, 0, -0x40); break;  // exe 0x4df680
    default: notPortedAt(kStoryPort, "0x4df640 variant", actor.variant);
    }
}
// exe 0x4df510
void flyInShip(AnimatedActor& actor) {
    head(actor);
    switch (FlyInState(actor.state)) {
    case FlyInState::Init: flyInInit(actor); break;
    case FlyInState::Fly: flyInFly(actor); break;
    default: notPortedAt(kStoryPort, "0x4df510 state", actor.state);
    }
    tailAnim(actor);
}

// ---- 0x4df6a0 (states [0x8958dc]) --------------------------------------------------------------------------------
enum class HoverShipState : uint8_t { Init = 0, Run = 1 };
// [0x8958ec]
enum class HoverShipVariant : uint8_t { Still = 0, NoseEffects = 1, Blasts = 2, Destroyed = 3, Bob = 4 };
const DataTable<ScalarTable<uint8_t, 5>> hoverShipAnims{TableId::GlydeShipCastHoverShipAnims}; // {0, 0, 1, 0, 0}
const DataTable<CollisionBoxRecord> hoverShipBox{TableId::GlydeShipCastHoverShipBox};
constexpr TableId kBlastOffsets[3] = {TableId::GlydeShipCastFirstBlastOffset, TableId::GlydeShipCastSecondBlastOffset, TableId::GlydeShipCastThirdBlastOffset};
// an explosion's place and size (exe data: s16 x, y, z, size)
struct ExplosionPlace : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    int16_t x = 0; // +0x0
    int16_t y = 0; // +0x2
    int16_t z = 0; // +0x4
    uint16_t size = 0; // +0x6
    template <class R> static ExplosionPlace load(const R& r) {
        ExplosionPlace v;
        v.x = r.template read<int16_t>(0x0);
        v.y = r.template read<int16_t>(0x2);
        v.z = r.template read<int16_t>(0x4);
        v.size = r.template read<uint16_t>(0x6);
        return v;
    }
};
// a debris' velocity and yaw (exe data, 8 bytes: x | y, z | yaw)
struct DebrisLaunch : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    uint32_t xy = 0; // +0x0
    uint32_t zYaw = 0; // +0x4
    uint16_t yaw = 0; // +0x6
    template <class R> static DebrisLaunch load(const R& r) {
        DebrisLaunch v;
        v.xy = r.template read<uint32_t>(0x0);
        v.zYaw = r.template read<uint32_t>(0x4);
        v.yaw = r.template read<uint16_t>(0x6);
        return v;
    }
};

// exe 0x4df730: the model (scenery-drawn), no motion, the variant's animation, one hit point, the box, the class block;
// variant 2 posed at its key 0x1f, variant 4 bobbing down
void hoverShipInit(AnimatedActor& actor) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);           // exe 0x5e1490
        return;
    }
    actor.flags |= kActorScenery;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.velocity.clearWords();
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.anim = hoverShipAnims[actor.variant];
    actor.prevAnim = kNoAnim;
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.collisionBox.pointAt(hoverShipBox.stateAddress());
    actor.clearClassBlock(); // exe 0x40dc40
    switch (HoverShipVariant(actor.variant)) {
    case HoverShipVariant::Blasts:
        animSetPose(actor, actor.anim, 0x1f); // exe 0x5e1c30
        actor.prevAnim = actor.anim;
        break;
    case HoverShipVariant::Bob: actor.acceleration.y = -1; break;
    default: break;
    }
    actor.enterState(uint8_t(HoverShipState::Run));
}
// a type-0x16 effect at the nose (radius 0x2c0, 0x40 up)
void noseEffect(const AnimatedActor& actor, int16_t dx, int16_t dz, uint8_t v) {
    const uint32_t e = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (e == 0) return;
    EffectActor& effect = effectActorAt(e);
    effect.flags = kActorLive;
    effect.type = kNoseEffectType;
    effect.variant = v;
    effect.pos.x = int16_t(uint16_t(actor.pos.x.whole) + uint16_t(dx));
    effect.pos.y = int16_t(uint16_t(actor.pos.y.whole) - 0x40);
    effect.pos.z = int16_t(uint16_t(actor.pos.z.whole) + uint16_t(dz));
}
// exe 0x4df820 (variant 1): animation 1 at the tick 0xf; a nose effect (variant 1) at the tick 0x32 and (variant 2)
// every tick after it
void noseEffects(AnimatedActor& actor) {
    int16_t dx = 0, dz = 0;
    angleVector(actor.rotation.y, 0x2c0, dx, dz); // exe 0x5e40c0
    const int32_t c = stepTick();
    if (c == 0xf) actor.anim = 1;
    else if (c == 0x32) noseEffect(actor, dx, dz, 1);
    if (stepTick() > 0x32) noseEffect(actor, dx, dz, 2);
}
// exe 0x4df920(actor, offset): a blast at the actor + Ry(yaw) * (offset << 4): the global static type 7 variant 0x10
// (+ a camera shake), the stage's static type 3 variant 0
void blastAt(const AnimatedActor& actor, uint32_t offset) {
    const ExplosionPlace ofs = dataRecord<ExplosionPlace>(offset, ExplosionPlace::kBytes);
    int32_t o[3];
    rotateByYaw(actor.rotation.y, int32_t(ofs.x) << 4, int32_t(ofs.y) << 4, int32_t(ofs.z) << 4,
                o); // exe 0x5e38d0
    uint32_t e = allocActor60(0); // exe 0x5e1300
    if (e != 0) {
        StaticActor& blast = staticActorAt(e);
        auto&& params = BlastParams(actorHeaderAt(e));
        blast.flags = kActorLive;
        blast.type = typeByte(GlobalStaticType::Blast);
        blast.subtype = 0;
        blast.variant = kBlastVariant;
        blast.loopFlags |= kActorSweepGroup2;
        blast.pos.x = int32_t(uint32_t(actor.pos.x) + uint32_t(o[0]));
        blast.pos.y = int32_t(uint32_t(actor.pos.y) + uint32_t(o[1]));
        blast.pos.z = int32_t(uint32_t(actor.pos.z) + uint32_t(o[2]));
        params.size = 0x80;
        params.attack = 0;
        cameraShake(1, 0x400, 0x20); // exe 0x40ea80
    }
    e = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (e != 0) {
        StaticActor& smoke = staticActorAt(e);
        auto&& params = SmokeParams(actorHeaderAt(e));
        smoke.flags = kActorLive;
        smoke.type = kSmokeType;
        smoke.variant = 0;
        smoke.loopFlags |= kActorSweepGroup2;
        smoke.pos.x = int32_t(uint32_t(actor.pos.x) + uint32_t(o[0]));
        smoke.pos.y = int32_t(uint32_t(actor.pos.y) + uint32_t(o[1]));
        smoke.pos.z = int32_t(uint32_t(actor.pos.z) + uint32_t(o[2]));
        params.driftX = 0;
        params.driftZ = 0;
        params.life = 0;
    }
}
// exe 0x4df8d0 (variant 2): the three blasts on their ticks
void blasts(const AnimatedActor& actor) {
    switch (stepTick()) {
    case 0xa: blastAt(actor, stateAddress(kBlastOffsets[0])); break;
    case 0x23: blastAt(actor, stateAddress(kBlastOffsets[1])); break;
    case 0x46: blastAt(actor, stateAddress(kBlastOffsets[2])); break;
    default: break;
    }
}
// exe 0x4dfa10 (variant 3): destroyed -- a shake and sound at 0xf; at 0xf / 0x12 / 0x15 three explosions and four debris
// pairs; at 0x16 a smoke riding it; after it the fall (vy += 0x10 up to 0x180, y down to 0x4000, the pitch down to
// -0x300)
void destroyed(AnimatedActor& actor) {
    const int32_t c = stepTick();
    if (c == 0xf) {
        cameraShake(1, 0x200, 0x20); // exe 0x40ea80
        soundPlayFlat(0x8b);         // exe 0x5d51f0
    }
    if (c == 0xf || c == 0x12 || c == 0x15) {
        for (uint32_t i = 0; i < 3; ++i) {
            const ExplosionPlace t = dataRecord<ExplosionPlace>(stateAddress(TableId::GlydeShipCastExplosions) + i * 8, ExplosionPlace::kBytes);
            const uint32_t e = allocActor60(0); // exe 0x5e1300
            if (e == 0) continue;
            StaticActor& blast = staticActorAt(e);
            auto&& params = BlastParams(actorHeaderAt(e));
            blast.flags = kActorLive;
            blast.loopFlags |= kActorSweepGroup2;
            blast.type = typeByte(GlobalStaticType::Blast);
            blast.subtype = 0;
            blast.variant = kBlastVariant;
            blast.pos.x = int32_t(uint32_t(actor.pos.x) + (uint32_t(int32_t(t.x)) << 16));
            blast.pos.y = int32_t(uint32_t(actor.pos.y) + (uint32_t(int32_t(t.y)) << 16));
            blast.pos.z = int32_t(uint32_t(actor.pos.z) + (uint32_t(int32_t(t.z)) << 16));
            params.size = t.size;
            params.attack = 0;
        }
        for (uint32_t i = 0; i < 4; ++i) { // the debris pairs
            const DebrisLaunch t = dataRecord<DebrisLaunch>(stateAddress(TableId::GlydeShipCastDebrisLaunches) + i * 8, DebrisLaunch::kBytes);
            uint32_t e = allocActor60(kActorStageClass); // exe 0x5e12f0
            if (e != 0) {
                StaticActor& debris = staticActorAt(e);
                debris.flags = kActorLive;
                debris.loopFlags |= kActorSweepGroup2;
                debris.type = kDebrisType;
                debris.subtype = 0;
                debris.variant = 2;
                debris.pos = actor.pos;
                debris.rotation.y = int16_t(t.yaw);
                debris.velocity.setWords(t.xy, t.zYaw);
                DebrisFields(projectileActorAt(e)).ticks = 0xf0;
            }
            e = allocActor60(kActorStageClass);
            if (e != 0) {
                StaticActor& debris = staticActorAt(e);
                debris.flags = kActorLive;
                debris.loopFlags |= kActorSweepGroup2;
                debris.type = kDebrisType;
                debris.subtype = 0;
                debris.variant = 1;
                debris.pos = actor.pos;
                debris.rotation.y = int16_t(uint16_t((uint32_t(actor.subState) << 8) + uint16_t(t.yaw)));
                debris.velocity.setWords(t.xy, t.zYaw);
            }
        }
        actor.subState += 1u;
    } else if (c == 0x16) {
        const uint32_t e = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (e != 0) {
            StaticActor& smoke = staticActorAt(e);
            smoke.flags = kActorLive;
            smoke.loopFlags |= kActorSweepGroup2;
            smoke.type = kSmokeType;
            smoke.subtype = 0;
            smoke.variant = kRidingSmokeVariant;
            RidingSmoke(staticActorAt(e)).followed = slotIdentityWord(actor);
            smoke.setParamsWord(0);
        }
    }
    if (stepTick() > 0x16) { // the fall
        actor.velocity.y += 0x10u;
        if (actor.velocity.y > 0x180) actor.velocity.y = 0x180;
        actor.pos.y = int32_t(uint32_t(actor.pos.y) + (uint32_t(int32_t(actor.velocity.y)) << 12));
        if (actor.pos.y.whole > 0x4000) actor.pos.y.whole = 0x4000;
        actor.rotation.x -= 6u;
        if (actor.rotation.x < -0x300) actor.rotation.x = -0x300;
    }
}
// exe 0x4dfc20 (variant 4): the bob, the vertical speed += its step flipped at +-0x20
void bob(AnimatedActor& actor) {
    actor.velocity.y = int16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y));
    if (actor.velocity.y > 0x20) {
        actor.velocity.y = 0x20;
        actor.acceleration.y = -1;
    }
    if (actor.velocity.y < -0x20) {
        actor.velocity.y = -0x20;
        actor.acceleration.y = 1;
    }
    actor.pos.y = int32_t(uint32_t(actor.pos.y) + (uint32_t(int32_t(actor.velocity.y)) << 12));
}
// exe 0x4df800: by the variant [0x8958ec]
void hoverShipRun(AnimatedActor& actor) {
    switch (HoverShipVariant(actor.variant)) {
    case HoverShipVariant::Still: break; // exe 0x52ef00 (ret)
    case HoverShipVariant::NoseEffects: noseEffects(actor); break;
    case HoverShipVariant::Blasts: blasts(actor); break;
    case HoverShipVariant::Destroyed: destroyed(actor); break;
    case HoverShipVariant::Bob: bob(actor); break;
    default: notPortedAt(kStoryPort, "0x4df800 variant", actor.variant);
    }
}
// exe 0x4df6a0
void hoverShip(AnimatedActor& actor) {
    head(actor);
    switch (HoverShipState(actor.state)) {
    case HoverShipState::Init: hoverShipInit(actor); break;
    case HoverShipState::Run: hoverShipRun(actor); break;
    default: notPortedAt(kStoryPort, "0x4df6a0 state", actor.state);
    }
    tailAnim(actor);
}

} // namespace

void glydeFlyInShipUpdate(const PlayerEnv&, uint32_t actor) { flyInShip(animatedActorAt(actor)); }
void glydeHoverShipUpdate(const PlayerEnv&, uint32_t actor) { hoverShip(animatedActorAt(actor)); }

} // namespace od::port
