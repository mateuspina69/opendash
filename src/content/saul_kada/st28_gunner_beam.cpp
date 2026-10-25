// The fire-ruin gunner boss's beam (ST28 pool 0x40 type 0x19, exe 0x5c9770; hypothesis names); the boss spawns it with
// itself in its fields. Its states by variant [[0x8d1504 + variant * 4]]:
//  variant 0, the aiming ray: 0 0x5c9790, 1 0x5c9800 (the player inside the box 0x1000 long, radius 0x80, along the
//  boss's rotation from the ray's point -> the signal to fire, freed; else 8 moving dots, 0xf0 draw commands, exe
//  0x5c9f70), 2 freed (the boss also sets it).
//  variant 1, the shot: 0 0x5c98a0, 1 the homing flight 0x5c9920 (its yaw / pitch snapped at the player, speed 0x600,
//  sparks; hitting something -> the player caught, hidden, sound 0x219, its effect 0x258 ticks; a wall -> the signal,
//  freed), 2 0x5c9af0 (the caught effect 0x5c9b20 around the player: 8 rings (0xf0 draw commands) and 4 OT spikes),
//  3 freed.
#include "content/saul_kada/st28_gunner_beam.hpp"
#include "content/saul_kada/st28_gunner_beam_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/draw_commands.hpp"
#include "game/game_state.hpp"
#include "game/geometry_scratch.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st28_gunner_beam_state;

namespace {

enum class RayState : uint8_t { Start = 0, Aim = 1, Free = 2 };
enum class ShotState : uint8_t { Start = 0, Flight = 1, Caught = 2, Free = 3 };
// the caught effect's rings (exe data 0x8d1488, 12 bytes): two colour words, the size bytes, the depth bytes
struct CaughtRing : DataRecord {
    uint32_t colour = 0;
    uint32_t colour2 = 0;
    uint8_t sizeA = 0;
    uint8_t sizeB = 0;
    uint8_t depthA = 0;
    uint8_t depthB = 0;
    static CaughtRing load(const TableReader& r) {
        CaughtRing rec;
        rec.colour = r.u32(0);
        rec.colour2 = r.u32(4);
        rec.sizeA = r.u8(8);
        rec.sizeB = r.u8(9);
        rec.depthA = r.u8(0xa);
        rec.depthB = r.u8(0xb);
        return rec;
    }
};
const DataTable<RecordTable<CaughtRing, 8, 12>> caughtRings{TableId::St28GunnerBeamCaughtRings};
constexpr uint8_t kSparkVariant = 3;
constexpr uint16_t kCaughtSound = 0x219;
constexpr int16_t kCaughtTicks = 0x258;

const Actor& bossOf(const ProjectileActor& actor) { // the exe dereferences it without a test
    const uint32_t boss = GunnerBeamFields(actor).boss;
    if (!boss) notPorted(kStoryPort, "0x5c9770: no boss actor in +0xbc (the exe dereferences address 0)");
    return animatedActorAt(boss);
}
// exe 0x5c9720(actor, size): a spark (the global hit spark, variant 3) at it
void spark(const ProjectileActor& actor, uint16_t size) {
    const uint32_t slot = allocActorA0(0); // exe 0x5e13b0
    if (!slot) return;
    EffectActor& effect = effectActorAt(slot);
    effect.flags = kActorLiveDrawn;
    effect.loopFlags |= kActorSweepGroup2;
    effect.type = typeByte(GlobalEffectType::HitSpark);
    effect.variant = kSparkVariant;
    effect.pos.x = actor.pos.x.whole;
    effect.pos.y = actor.pos.y.whole;
    effect.pos.z = actor.pos.z.whole;
    classState<HitSparkFields>(effectActorAt(slot)).size = size;
}
// the start both variants share: its box, airborne, no signal, its speed, its hit box
void startCommon(ProjectileActor& actor, BeamOwnFields& beam, int16_t speed) {
    actor.collisionBox.pointAt(TableId::St28GunnerBeamBox);
    actor.airborne = 1;
    actor.flags |= kActorDrawn;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.setParamsWord(0);
    actor.velocity.x = 0;
    actor.velocity.y = 0;
    actor.velocity.z = speed;
    beam.effectTicks = 0;
}

// ---- variant 0: the aiming ray -----------------------------------------------------------------------------------
// exe 0x5c9790 (no model)
void rayStart(ProjectileActor& actor, BeamOwnFields& beam) {
    startCommon(actor, beam, int16_t(0xf400));
    beam.dotPhase = 0;
    beam.hitBox.previousX = actor.prevPos.x.whole;
    beam.hitBox.previousZ = actor.prevPos.z.whole;
    beam.hitBox.result = 0;
    actor.state = uint8_t(RayState::Aim);
}
// exe 0x5c9f70: 8 dots (0xf0 draw commands, 0xc0) along the boss's rotation, -(n * 8 + phase) << 10 (the phase cycling
// 0..7) from the boss's position
void rayDots(const ProjectileActor& actor, BeamOwnFields& beam) {
    const uint8_t phase = uint8_t(beam.dotPhase + 1u);
    beam.dotPhase = phase;
    if (phase >= 8) beam.dotPhase = 0;
    for (int e = 0; e > -0x40; e -= 8) {
        const Actor& boss = bossOf(actor);
        const int16_t rot[3] = {boss.rotation.x, boss.rotation.y, boss.rotation.z};
        int32_t o[3];
        rotateVector(rot, 0, 0, int32_t(uint32_t(e - int(beam.dotPhase)) << 10), o); // exe 0x5e3970
        const uint16_t x = uint16_t((o[0] >> 16) + uint16_t(boss.pos.x.whole)),
                       y = uint16_t((o[1] >> 16) + uint16_t(boss.pos.y.whole)),
                       z = uint16_t((o[2] >> 16) + uint16_t(boss.pos.z.whole));
        dc::emit({0x4000u, 0xf00000c0u, uint32_t(x) | uint32_t(y) << 16, 0xc0u, z, 0x80000u}, actor.at);
    }
}
// exe 0x5c9800: the player (0x64 above his feet) inside the ray's box -> the signal to fire, freed; else its dots
void rayAim(ProjectileActor& actor, BeamOwnFields& beam) {
    const Player& p = player();
    const int16_t target[3] = {p.pos.x.whole, int16_t(uint16_t(p.pos.y.whole - 0x64)), p.pos.z.whole};
    const int16_t from[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const Actor& boss = bossOf(actor);
    const Matrix3 turn = rotationYXZ(boss.rotation.x, boss.rotation.y, boss.rotation.z); // exe 0x40bfd0
    if (pointInBeam(from, target, turn, 0x1000, 0x80)) {                                  // exe 0x5e5b40
        GunnerBeamFields(actor).signal = uint8_t(GunnerBeamSignal::Fire);
        actor.state = uint8_t(RayState::Free);
        return;
    }
    rayDots(actor, beam);
}

// ---- variant 1: the shot -----------------------------------------------------------------------------------------
// exe 0x5c98a0: its model (none: freed), speed 0x600, a spark of 0xc0
void shotStart(ProjectileActor& actor, BeamOwnFields& beam) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        actor.state = uint8_t(ShotState::Free);
        return;
    }
    startCommon(actor, beam, int16_t(0xfa00));
    beam.hitBox.previousX = actor.prevPos.x.whole;
    beam.hitBox.previousZ = actor.prevPos.z.whole;
    beam.hitBox.result = 0;
    spark(actor, 0xc0);
    actor.state = uint8_t(ShotState::Flight);
}
// exe 0x5c9920: homing at the player 0x5a above his feet (its yaw / pitch snapped), forward by its speed; a hit -> the
// player caught (hidden, 0x258 ticks of the effect); a wall -> the signal, freed; else its capsule +-0x40 across its yaw
void shotFlight(const PlayerEnv& env, ProjectileActor& actor, BeamOwnFields& beam) {
    const Player& p = player();
    const int32_t target[3] = {p.pos.x, int32_t((uint32_t(p.pos.y) & 0xffffu) | uint32_t(uint16_t(uint16_t(p.pos.y.whole) - 0x5au)) << 16),
                               p.pos.z};
    actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) +
                                        uint32_t(angleDiff(uint32_t(int32_t(actor.rotation.y)),
                                                           uint32_t(int32_t(int16_t(angleOf(target[0], target[2], actor.pos.x, actor.pos.z))))))));
    const int32_t at[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
    const int pitch = pitchOf(target, at); // exe 0x5e3ca0
    actor.rotation.x = int16_t(uint16_t(uint16_t(actor.rotation.x) +
                                        uint32_t(angleDiff(uint32_t(int32_t(actor.rotation.x)),
                                                           uint32_t(int32_t(int16_t(uint16_t(0x800 - pitch))))))));
    moveAlongRotation(actor.pos, actor.rotation, 0, 0, actor.velocity.z); // exe 0x5e3810
    if (gameState().frame & 1) spark(actor, 0x80);
    GunnerBeamFields fields(actor);
    if (beam.hitBox.result & kHitAttackBump) {
        fields.signal = uint8_t(GunnerBeamSignal::Caught);
        actor.state = uint8_t(ShotState::Caught);
        actor.setDrawn(false);
        beam.effectTicks = kCaughtTicks;
        soundPlayAtPos(kCaughtSound, actor.pos);
        return;
    }
    if (int32_t(actorTestMove(env, actor)) < 0) { // exe 0x40f570
        fields.signal = uint8_t(GunnerBeamSignal::Fire);
        actor.state = uint8_t(ShotState::Free);
        return;
    }
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    int16_t dx, dz;
    angleVector(actor.rotation.y, 0x40, dx, dz); // exe 0x5e40c0
    const int16_t pa[3] = {int16_t(uint16_t(dx + actor.pos.x.whole)), actor.pos.y.whole, int16_t(uint16_t(dz + actor.pos.z.whole))};
    angleVector(actor.rotation.y, -0x40, dx, dz);
    const int16_t pb[3] = {int16_t(uint16_t(dx + actor.pos.x.whole)), actor.pos.y.whole, int16_t(uint16_t(dz + actor.pos.z.whole))};
    beam.hitBox.previousX = actor.prevPos.x.whole;
    beam.hitBox.previousZ = actor.prevPos.z.whole;
    beam.hitBox.result = 0;
    hitListSubmitCapsule(pa, 8, pb, 8, beam.hitBox,
                         hitOwner(actor.slot) | hitGroup(kHitGroupHeavyThrown) | kHitNoLockOn | hitCapsule(2),
                         kHitAttackStrong); // exe 0x5e4190
}
// exe 0x5c9b20: the caught effect at the player's point P (his integer position, 0x5a up): within the depth and on the
// screen -> 8 rings at P (0xf0 draw commands), then (the GTE on the identity 0x6d0100) 4 spikes in the camera plane
// around P's view point: DR_MODE 0xe1000220 + POLY_G4 0x3a, red 0xc0, corners at angles k - 2 / k / k / k + 2 (64
// steps; k = 8, 0x18, 0x28, 0x38) and radii r / 0x10 / r / r (r = |the 64-step sine of the frame| / 64 + 0x18, << the
// stage shift >> 12); each followed by a DR_MODE 0xe1000240 + POLY_G4 0x3a (green 0xc0) whose corners the exe copies
// from the wrong words of the first packet (two of its colours, the DR_MODE tag, 0: not screen points; not drawn by the
// port); the chain linked into OT entry 1
void caughtEffect(const ProjectileActor& actor) {
    const Player& p = player();
    const int16_t point[3] = {p.pos.x.whole, int16_t(uint16_t(p.pos.y.whole - 0x5a)), p.pos.z.whole};
    const CameraPoint c = cameraPoint(point); // exe 0x40c3c0 / 0x40c3e0 / 0x40c6a0 / 0x40c800 / 0x40caa0 / 0x40cae0
    if (c.view[2] > int32_t(uint32_t(uint16_t(stage().effectsFar)) * 4) || c.view[2] < 0x30) return;
    if (c.clipped) return;
    if (uint16_t(c.sx + 0x160) > 0x400 || uint16_t(c.sy + 0x88) > 0x400) return;
    const uint32_t xy = uint32_t(uint16_t(point[0])) | uint32_t(uint16_t(point[1])) << 16, z = uint16_t(point[2]);
    for (size_t i = 0; i < caughtRings.size(); ++i) {
        const CaughtRing ring = caughtRings[i];
        dc::emit({uint32_t(ring.depthA) | uint32_t(ring.depthB) << 8, ring.colour | 0xf0000000u, xy,
                  ring.colour2 | 0x22000000u, z | 0x1000000u, uint32_t(ring.sizeA) | uint32_t(ring.sizeB) << 16},
                 actor.at);
    }
    // the identity rotation / zero translation loaded into the GTE (the geometry scratch's transform at +0x40)
    geometryScratch().transform(0x40).setIdentityWords();
    const Trig64Tables& trig = trig64Tables();
    const int f = trig.sine[gameState().frame & 0x3f];
    const int r = ((f < 0 ? -f : f) >> 6) + 0x18;
    const int shift = (0xc - int(stage().coordinateShift)) & 0x1f;
    const int32_t vx = c.view[0], vy = c.view[1];
    auto X = [&](uint32_t i) { return int(trig.cosine[i & 0x3f]); };
    auto Y = [&](uint32_t i) { return int(trig.sine[i & 0x3f]); };
    // a corner word int16(d + view) brought back to an offset from the view point (camera-input units)
    auto offset = [&](int dX, int dY, float o[2]) {
        o[0] = float(int16_t(uint16_t(int(int16_t(uint16_t(dX + vx))) - vx)));
        o[1] = float(int16_t(uint16_t(int(int16_t(uint16_t(dY + vy))) - vy)));
    };
    for (uint32_t k = 8; k <= 0x38; k += 0x10) {
        float o[4][2];
        offset((X(k - 2) * r) >> shift, (Y(k - 2) * r) >> shift, o[0]);
        offset((X(k) << 4) >> shift, (Y(k) << 4) >> shift, o[1]);
        offset((X(k) * r) >> shift, (Y(k) * r) >> shift, o[2]);
        offset((X(k + 2) * r) >> shift, (Y(k + 2) * r) >> shift, o[3]);
        const uint32_t colours[4] = {0x3a0000c0u, 0xc0u, 0xc0u, 0xc0u};
        const int16_t v[4][3] = {{point[0], point[1], point[2]}, {point[0], point[1], point[2]},
                                 {point[0], point[1], point[2]}, {point[0], point[1], point[2]}};
        dc::otQuadG4(actor.at, colours, 1, v, o); // DR_MODE 0xe1000220 (abr 1)
    }
}
// exe 0x5c9af0: the caught effect until its ticks run out
void shotCaught(ProjectileActor& actor, BeamOwnFields& beam) {
    caughtEffect(actor);
    beam.effectTicks -= 1u;
    if (beam.effectTicks == 0) actor.state = uint8_t(ShotState::Free);
}

void bossBeam(const PlayerEnv& env, ProjectileActor& actor) { // exe 0x5c9770 ([[0x8d1504 + variant * 4] + state * 4])
    BeamOwnFields& beam = classState<BeamOwnFields>(actor);
    const uint32_t variant = actor.variant, state = actor.state;
    if (GunnerBeamVariant(variant) == GunnerBeamVariant::Ray) { // [0x8d14e8]
        switch (RayState(state)) {
        case RayState::Start: rayStart(actor, beam); return;
        case RayState::Aim: rayAim(actor, beam); return;
        case RayState::Free: freeActor(actor); return; // 0x5d0bc0
        default: notPortedAt(kStoryPort, "0x5c9770 variant 0 state past [0x8d14e8]", state);
        }
    }
    if (GunnerBeamVariant(variant) == GunnerBeamVariant::Shot) { // [0x8d14f4]
        switch (ShotState(state)) {
        case ShotState::Start: shotStart(actor, beam); return;
        case ShotState::Flight: shotFlight(env, actor, beam); return;
        case ShotState::Caught: shotCaught(actor, beam); return;
        case ShotState::Free: freeActor(actor); return; // 0x5d0bc0
        default: notPortedAt(kStoryPort, "0x5c9770 variant 1 state past [0x8d14f4]", state);
        }
    }
    notPortedAt(kStoryPort, "0x5c9770 variant past [0x8d1504]", variant);
}

} // namespace

void gunnerBeamUpdate(const PlayerEnv& env, uint32_t actor) { bossBeam(env, projectileActorAt(actor)); }

} // namespace od::port
