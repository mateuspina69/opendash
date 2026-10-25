// ST28 sub 3, the fire ruin's lava pit (story 13; hypothesis names), one routine per exe routine.
//
// The controller (pool e0 type 0x1e, exe 0x426a40; states [0x88bf04]): 0 start 0x426a60, 1 the flames 0x426a70 (two
// flames at fixed points every 4th tick of ticks 16..31 of 32), 2 / 5 freed 0x427cf0, 3 the creature spawned 0x426b40
// (at (-0x1100, 0x80, 0x1100)), 4 the creature moved 0x426ba0: once it hides it surfaces at one of 8 spots (0x88bf28,
// picked by 0x88bf1c; a spot within 0x600 of the player, or the last one, takes the next).
// The creature's mover (pool e0 type 0x1f, exe 0x426b20; states [0x88bf10]: 0 0x426b40, 1 0x426ba0, 2 freed) runs the
// same two routines.
// A flame (pool 0x60 type 0x44, exe 0x5b29b0) rises out of the pit (pitched 0xc00), growing 6 a tick; it hurts for
// 15 ticks, then fades 0x10 a tick and is freed; drawn as a billboard (draw command 0x2e, 4 frames).
// The creature (pool 0x60 type 0x40 sub 3, exe 0x5b18c0; states [0x8d06f4]: 0 bind, 1 the sub-states [0x8d0700] with
// its hit entry while surfaced, 2 / 10 freed, 3..9 the sub-states 0..6 directly). Hit: the music faded, the windows
// closed, the event 0x20022 (message 0x28), flag 0x56, the tool-kit balance - 2000, freed. Sub-states: 0 wait, 1 sink
// 0x5a ticks (sound 0x228, dust every 8) -> 3, 2 surface (a splash, 0x2d ticks, then 0x40 rising) -> 0, 3 wait -> 4,
// 4 hidden (the controller moves it), 5 / 6 the death leap (re-bound, spinning, until x <= 0x200), 7 freed.
#include "content/saul_kada/lava_pit.hpp"
#include "content/saul_kada/lava_pit_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/draw_commands.hpp"
#include "game/event.hpp"
#include "game/field_alias.hpp"
#include "game/footstep_dust.hpp"
#include "game/game_state.hpp"
#include "game/hit_list.hpp"
#include "game/message.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/rewards.hpp"
#include "game/spark_emitter.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/table_records.hpp"

namespace od::port {

using namespace lava_pit_state;

namespace {

constexpr uint8_t kFlameType = 0x44;
constexpr uint8_t kCreatureType = 0x40, kCreatureSubtype = 3;

// ---- the controllers --------------------------------------------------------------------------------------------
// [0x88bf04]
enum class PitState : uint8_t { Start = 0, Flames = 1, Free = 2, SpawnCreature = 3, MoveCreature = 4, FreeToo = 5 };
// [0x88bf10]
enum class MoverState : uint8_t { SpawnCreature = 0, MoveCreature = 1, Free = 2 };
// A spot the creature surfaces at (exe 0x88bf28, 8 bytes: s16 x / y / z)
const DataTable<RecordTable<ShortPoint, 8, 8>> creatureSpots{TableId::LavaPitCreatureSpots};
const DataTable<ScalarTable<uint8_t, 9>> spotPicks{TableId::LavaPitSpotPicks}; // the RNG's pick of a spot; its next one is the following entry
constexpr uint8_t kNoSpot = 0xff;
constexpr int kTooNearPlayer = 0x600;
// the flames' points
constexpr int16_t kFlamePoints[2][3] = {{0x30, -0x410, 0x8b0}, {0x3d0, -0x410, 0x8b0}};
constexpr int16_t kFlamePitch = 0xc00;
constexpr int16_t kFlameDamage = 8;
// the creature's first place (16.16)
constexpr int32_t kCreatureStart[3] = {int32_t(0xef000000u), 0x800000, 0x11000000};

// The flame's parameter: its damage
struct FlameParams {
    explicit FlameParams(const StaticActor& actor) : FlameParams(const_cast<StaticActor&>(actor)) {}
    explicit FlameParams(StaticActor& actor) : damage(actor.params, 0) {}
    BytesField<int16_t, 4> damage;
};
// The creature's parameter: it spins
struct CreatureParams {
    explicit CreatureParams(const StaticActor& actor) : CreatureParams(const_cast<StaticActor&>(actor)) {}
    explicit CreatureParams(StaticActor& actor) : spins(actor.params[0]) {}
    uint8_t& spins;
};
// [0x8d0700] (the states 3..9 run sub-states 0..6 directly)
enum class CreatureSub : uint8_t {
    Wait = 0, Sink = 1, Surface = 2, WaitHidden = 3, Hidden = 4, LeapStart = 5, Leap = 6, Free = 7,
};

// exe 0x426a70: two flames every 4th tick of ticks 16..31 of 32
void spawnFlames() {
    const uint8_t t = uint8_t(gameState().frame);
    if (!(t & 0x10) || (t & 3)) return;
    for (const auto& p : kFlamePoints) {
        const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (!slot) continue;
        StaticActor& flame = staticActorAt(slot);
        flame.pos.x.whole = p[0];
        flame.flags = kActorLive;
        flame.type = kFlameType;
        flame.variant = 0;
        flame.pos.y.whole = p[1];
        flame.pos.z.whole = p[2];
        flame.rotation.x = kFlamePitch;
        flame.rotation.y = 0;
        flame.rotation.z = 0;
        FlameParams(staticActorAt(slot)).damage = kFlameDamage;
    }
}
// exe 0x426b40: the creature at its first place
void spawnCreature(ControllerActor& actor, PitFields& pit) {
    ++actor.state;
    pit.lastSpot = kNoSpot;
    const uint32_t slot = allocActor60(kActorStageClass);
    if (!slot) return;
    StaticActor& creature = staticActorAt(slot);
    creature.flags = kActorLive;
    creature.type = kCreatureType;
    creature.subtype = kCreatureSubtype;
    creature.variant = 0;
    creature.spawnByte = 0;
    creature.rotation.z = 0;
    creature.rotation.x = 0;
    creature.rotation.y = 0;
    CreatureParams(staticActorAt(slot)).spins = 1;
    creature.pos.x = kCreatureStart[0];
    creature.pos.y = kCreatureStart[1];
    creature.pos.z = kCreatureStart[2];
    pit.creature = slot;
}
// exe 0x426ba0: once the creature hides, it surfaces at a random spot (not near the player, not the last one)
void moveCreature(PitFields& pit) {
    ActorHeader& creature = actorHeaderAt(pit.creature);
    if (CreatureSub(creature.subState) != CreatureSub::Hidden) return;
    const uint8_t k = uint8_t(rngStep() & 7);
    const Player& p = player();
    const int16_t playerPoint[3] = {p.pos.x.whole, p.pos.y.whole, p.pos.z.whole};
    uint8_t spot = spotPicks[k];
    const uint8_t next = k == 7 ? spotPicks[0] : spotPicks[k + 1u];
    const ShortPoint candidate = creatureSpots[spot];
    const int16_t candidatePoint[3] = {candidate.x, 0, candidate.z};
    if (planarDistance16(playerPoint, candidatePoint) < kTooNearPlayer) spot = next; // exe 0x5e3b40
    if (pit.lastSpot == spot) spot = next;
    pit.lastSpot = spot;
    const ShortPoint point = creatureSpots[spot];
    Actor& body = actorAt(pit.creature);
    body.pos.x = int32_t(uint32_t(int32_t(point.x)) << 16);
    body.pos.y = int32_t(uint32_t(int32_t(point.y)) << 16);
    body.pos.z = int32_t(uint32_t(int32_t(point.z)) << 16);
    creature.subState = uint8_t(CreatureSub::Surface);
}

// exe 0x426a40
void pitController(ControllerActor& actor) {
    PitFields& pit = classState<PitFields>(actor);
    switch (PitState(actor.state)) {
    case PitState::Start: // 0x426a60
        actor.state = uint8_t(PitState::Flames);
        actor.subState = 0;
        return;
    case PitState::Flames: spawnFlames(); return;
    case PitState::Free:
    case PitState::FreeToo: freeActor(actor); return; // 0x427cf0
    case PitState::SpawnCreature: spawnCreature(actor, pit); return;
    case PitState::MoveCreature: moveCreature(pit); return;
    default: notPortedAt(kStoryPort, "0x426a40 state", actor.state);
    }
}
// exe 0x426b20
void pitCreatureMover(ControllerActor& actor) {
    PitFields& pit = classState<PitFields>(actor);
    switch (MoverState(actor.state)) {
    case MoverState::SpawnCreature: spawnCreature(actor, pit); return;
    case MoverState::MoveCreature: moveCreature(pit); return;
    case MoverState::Free: freeActor(actor); return; // 0x427cf0 -> exe 0x5e15c0
    default: notPortedAt(kStoryPort, "0x426b20 state (table 0x88bf10)", actor.state);
    }
}

// ---- exe 0x5b29b0: a flame ----------------------------------------------------------------------------------------
enum class FlameState : uint8_t { Start = 0, Burn = 1, Fade = 2 };
constexpr uint16_t kBurnTicks = 0xf;
constexpr int16_t kFlameSize = 0x30, kFlameBrightness = 0x80, kFlameSpeed = int16_t(0xff00);
// its hit entry: group 3, its radius the size - 0x20; a strong, burning touch
constexpr uint32_t kFlameAttack = kHitAttackStrong | kHitAttackBurning;

void pitFlame(StaticActor& actor) {
    FlameFields& flame = classState<FlameFields>(actor);
    actor.prevPos = actor.pos;
    actor.wasAirborne = actor.airborne;
    actor.prevRotation = actor.rotation;
    switch (FlameState(actor.state)) {
    case FlameState::Start:
        actor.clearClassBlock(); // exe 0x40dc40
        flame.burnTicks = kBurnTicks;
        flame.size = kFlameSize;
        flame.brightness = kFlameBrightness;
        actor.velocity.z = kFlameSpeed;
        actor.collisionBox.pointAt(TableId::LavaPitFlameBox);
        actor.enterState(uint8_t(FlameState::Burn));
        [[fallthrough]];
    case FlameState::Burn: {
        moveAlongRotation(actor.pos, actor.rotation, 0, 0, actor.velocity.z); // exe 0x5e37b0
        flame.size += 6u;
        --flame.burnTicks;
        if (flame.burnTicks == 0) actor.enterState(uint8_t(FlameState::Fade));
        const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        flame.hitBox.previousX = actor.prevPos.x.whole;
        flame.hitBox.previousZ = actor.prevPos.z.whole;
        const uint32_t shape = uint32_t(((int32_t(flame.size) - 0x20) | 0x1800000) >> 2); // group 3, radius size - 0x20
        hitListSubmit(pos, flame.hitBox, shape | hitOwner(actor.slot),
                      uint32_t(int32_t(FlameParams(actor).damage)) | kFlameAttack); // exe 0x5e4120
        break;
    }
    case FlameState::Fade:
        moveAlongRotation(actor.pos, actor.rotation, 0, 0, actor.velocity.z);
        flame.size += 6u;
        flame.brightness -= 0x10u;
        if (flame.brightness <= 0) {
            freeActor(actor); // exe 0x5e1540
            return;
        }
        break;
    default: break;
    }
    const uint32_t grey = uint8_t(flame.brightness), u = uint32_t(int32_t(flame.frameU)), u1 = u + 0x1f;
    dc::emit({0, ((grey << 8 | grey) << 8 | grey) | 0x2e000000u,
              uint32_t(uint16_t(actor.pos.x.whole)) | uint32_t(int32_t(actor.pos.y.whole)) << 16, u | 0x7cd24000u,
              uint32_t(uint16_t(actor.pos.z.whole)), u1 | 0x2f4000u, uint32_t(int32_t(flame.size)), u | 0x5f00u,
              0, u1 | 0x5f00u},
             actor.at, false);
    flame.frameU = int16_t((uint8_t(flame.frameU) + 0x20u) & 0xff);
}

// ---- exe 0x5b18c0: the creature -------------------------------------------------------------------------------------
// [0x8d06f4]
enum class CreatureState : uint8_t { Bind = 0, Run = 1, Free = 2, FirstSub = 3, LastSub = 9, FreeToo = 10 };
constexpr uint16_t kSplashSound = 0x228;
constexpr uint8_t kSplashVariant = 0xb;
constexpr int16_t kSplashLifetime = 0x32;
constexpr uint8_t kDustVariant = 1;
constexpr uint32_t kCaughtEvent = 0x20022;
constexpr uint16_t kCaughtMessage = 0x28;
constexpr uint32_t kFlagCreatureCaught = 0x56;
constexpr int kCaughtBalance = -2000;
// its hit entry: a body (radius 0x64), no attack
constexpr uint32_t kCreatureShape = hitGroup(kHitGroupBodies) | hitRadius(0x64);

// exe 0x5ad170(a, 1, {x, 0x20, z}): the dust where the creature breaks the surface
void surfaceDust(const StaticActor& actor) {
    const int16_t point[3] = {actor.pos.x.whole, 0x20, actor.pos.z.whole};
    footstepDustAt(actor, kDustVariant, point);
}
void creatureSub(StaticActor& actor, CreatureFields& creature, CreatureSub sub) {
    switch (sub) {
    case CreatureSub::Wait: // 0x5b1a10
        if (creature.ticks != 0) {
            --creature.ticks;
            return;
        }
        actor.step = 0;
        ++actor.subState;
        return;
    case CreatureSub::Sink: // 0x5b1a40
        if (actor.step == 0) {
            actor.step = 1;
            creature.ticks = 0x5a;
            soundPlayAtPos(kSplashSound, actor.pos); // exe 0x5d5380
        } else {
            --creature.ticks;
            if (creature.ticks == 0) {
                actor.subState = uint8_t(CreatureSub::WaitHidden);
                actor.step = 0;
                creature.hidden = 1;
                creature.ticks = 0x1e;
            }
        }
        actor.pos.y.whole += 4u;
        if (!(gameState().frame & 7)) surfaceDust(actor);
        return;
    case CreatureSub::Surface: // 0x5b1ae0
        if (actor.step == 0) {
            actor.step = 1;
            creature.ticks = 0x2d;
            creature.hidden = 0;
            if (const uint32_t slot = allocActor60(kActorStageClass)) {
                StaticActor& splash = staticActorAt(slot);
                SparkEmitterParams& params = classState<SparkEmitterParams>(staticActorAt(slot));
                splash.flags = kActorLive;
                splash.loopFlags |= kActorSweepGroup2;
                splash.type = kSparkEmitterStageType;
                splash.variant = kSplashVariant;
                splash.pos.y.whole = 0;
                splash.pos.x.whole = actor.pos.x.whole;
                splash.pos.z.whole = actor.pos.z.whole;
                params.driftX = 0;
                params.driftZ = 0;
                params.lifetime = kSplashLifetime;
            }
            soundPlayAtPos(kSplashSound, actor.pos);
        } else if (actor.step == 1) {
            if (creature.ticks != 0) {
                --creature.ticks;
            } else {
                actor.step = 2;
                creature.ticks = 0x40;
            }
        } else {
            --creature.ticks;
            if (creature.ticks == 0) {
                actor.subState = uint8_t(CreatureSub::Wait);
                actor.step = 0;
                creature.ticks = 0x5a;
            }
            actor.pos.y.whole -= 4u;
        }
        if (!(gameState().frame & 7)) surfaceDust(actor);
        return;
    case CreatureSub::WaitHidden: // 0x5b1be0
        if (creature.ticks != 0) {
            --creature.ticks;
            return;
        }
        ++actor.subState;
        return;
    case CreatureSub::Hidden: actor.step = 0; return; // 0x5b1c00
    case CreatureSub::LeapStart: // 0x5b1c30
        if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
            freeActor(actor);
            return;
        }
        actor.velocity.y = int16_t(0xfd00);
        actor.velocity.x = 0;
        actor.acceleration.x = 0;
        actor.acceleration.z = 0;
        actor.velocity.z = int16_t(0xfec0);
        actor.acceleration.y = 0x28;
        ++actor.state;
        return;
    case CreatureSub::Leap: // 0x5b1c80
        actor.velocity.y += uint16_t(actor.acceleration.y);
        actor.rotation.y = int16_t((uint16_t(actor.rotation.y) + 0x100u) & 0xfff);
        moveAlongYaw(actor.pos, 0x200, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
        if (actor.pos.x.whole <= 0x200) ++actor.state;
        return;
    case CreatureSub::Free: freeActor(actor); return; // 0x5afdd0
    default: notPortedAt(kStoryPort, "0x5b18c0 sub-state", uint32_t(sub));
    }
}
void pitCreature(StaticActor& actor) {
    CreatureFields& creature = classState<CreatureFields>(actor);
    switch (CreatureState(actor.state)) {
    case CreatureState::Bind: // 0x5b18e0
        if (!bindActorModel(actor, 0)) {
            freeActor(actor);
            return;
        }
        actor.state = uint8_t(CreatureState::Run);
        actor.flags |= kActorDrawn;
        actor.subState = uint8_t(CreatureSub::WaitHidden);
        actor.step = 0;
        creature.hidden = 1;
        creature.velocitySpare = 0;
        actor.collisionBox.pointAt(TableId::LavaPitCreatureBox);
        creature.hitBox.result = 0;
        creature.ticks = 0x5a;
        return;
    case CreatureState::Run: { // 0x5b1930
        if (creature.hitBox.result & kHitAttackBump) { // caught
            soundFadeOut(kMusicFades, 0x222, 0);                                 // exe 0x5d5780
            windowCloseAll(WindowClose::Force);                                  // exe 0x5577d0
            eventStartAtActor(actor, kCaughtEvent, 0, kCaughtMessage);           // exe 0x543af0
            setStoryFlag(kFlagCreatureCaught);
            toolKitBalanceAdd(kCaughtBalance); // exe 0x5e5260
            freeActor(actor);                  // 0x5afdd0
            return;
        }
        creatureSub(actor, creature, CreatureSub(actor.subState));
        if (actor.flags == 0) return; // freed by its sub-state
        if (CreatureParams(actor).spins != 0) actor.rotation.y += 0x20u;
        if (creature.hidden != 0) return;
        creature.hitBox.previousX = actor.prevPos.x.whole;
        creature.hitBox.previousZ = actor.prevPos.z.whole;
        creature.hitBox.result = 0;
        const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        hitListSubmit(pos, creature.hitBox, hitOwner(actor.slot) | kCreatureShape, 0); // exe 0x5e4120
        return;
    }
    case CreatureState::Free:
    case CreatureState::FreeToo: freeActor(actor); return; // 0x5afdd0
    default:
        if (actor.state <= uint8_t(CreatureState::LastSub)) { // the main table repeats sub-states 0..6 directly
            creatureSub(actor, creature, CreatureSub(actor.state - uint8_t(CreatureState::FirstSub)));
            return;
        }
        notPortedAt(kStoryPort, "0x5b18c0 state", actor.state);
    }
}

} // namespace

void pitControllerUpdate(const PlayerEnv&, uint32_t actor) { pitController(controllerActorAt(actor)); }
void pitCreatureMoverUpdate(const PlayerEnv&, uint32_t actor) { pitCreatureMover(controllerActorAt(actor)); }
void pitFlameUpdate(const PlayerEnv&, uint32_t actor) { pitFlame(staticActorAt(actor)); }
void pitCreatureUpdate(const PlayerEnv&, uint32_t actor) { pitCreature(staticActorAt(actor)); }

} // namespace od::port
