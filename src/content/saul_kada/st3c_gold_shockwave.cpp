// The gold-statue boss's shockwaves (ST3C static type 0x5d, exe 0x5bcc10; hypothesis names). Per sub-type state tables
// [0x8d0d98 + sub * 4] (state 2 = 0x5afdd0 freed in all three); the exe keeps the running actor's block (+0xbc: a
// timer, the scale's steps) in the global 0xa5a930.
//  sub 0, the spawner (the boss spawns it; 0 start 0x5bcc70, 1 run 0x5bccb0 by variant [0x8d0da4]): the shockwaves at
//  its point on its timer -- variant 0 (0x5bccd0) a ring at once, a dome (variant 1) after 5, a ring after 0x1e;
//  variant 1 (0x5bcdd0) a ring at once and after 0xf; variant 2 (0x5bce80) a ring, a dome after 5, a ring after 0xf
//  (its step 3: after 0x1e), and every 4th tick a blast rising with the timer.
//  subs 1 / 2, the ring (0x5bcfe0 / 0x5bd070) / the dome (0x5bd160 / 0x5bd1f0): a model (by its variant) whose scale
//  grows by the block's steps on fixed ticks, spinning (0x123 / -0x123), hidden at its end.
#include "content/saul_kada/st3c_gold_shockwave.hpp"
#include "content/saul_kada/st3c_gold_shockwave_state.hpp"

#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

using namespace od::port::st3c_gold_shockwave_state;

namespace od::port {

using namespace st3c_gold_shockwave_state;

namespace {

enum class WaveState : uint8_t { Start = 0, Run = 1, Free = 2 };
enum class WaveSub : uint8_t { Spawner = 0, Ring = 1, Dome = 2 };
// the exe's global holding the running actor's block (its code reads the block through it: always its own)
constexpr uint16_t kSpawnerTint = 0x4a52;
constexpr uint8_t kRingVariant = 0, kDomeVariant = 1;
constexpr int16_t kRingSpin = 0x123, kDomeSpin = int16_t(0xfedd);

void tick(WaveFields& wave) { wave.timer += 1u; }
// a ring / a dome at the spawner's position; false when the pool is full
bool spawnWave(const StaticActor& actor, WaveSub sub, uint8_t variant) {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return false;
    StaticActor& wave = staticActorAt(slot);
    wave.flags = kActorLive;
    wave.type = kGoldShockwaveType;
    wave.subtype = uint8_t(sub);
    wave.variant = variant;
    wave.spawnByte = 0;
    wave.pos = actor.pos;
    return true;
}
// exe 0x5bccd0 (variant 0)
void spawnerA(StaticActor& actor, WaveFields& wave) {
    tick(wave);
    switch (actor.step) {
    case 0:
        spawnWave(actor, WaveSub::Ring, kRingVariant);
        ++actor.step;
        return;
    case 1:
        tick(wave);
        if (wave.timer < 5) return;
        spawnWave(actor, WaveSub::Dome, kDomeVariant);
        ++actor.step;
        return;
    case 2:
        tick(wave);
        if (wave.timer < 0x1e) return;
        spawnWave(actor, WaveSub::Ring, kRingVariant);
        actor.enterState(uint8_t(WaveState::Free));
        return;
    default: return;
    }
}
// exe 0x5bcdd0 (variant 1)
void spawnerB(StaticActor& actor, WaveFields& wave) {
    tick(wave);
    switch (actor.step) {
    case 0:
        spawnWave(actor, WaveSub::Ring, kRingVariant);
        ++actor.step;
        return;
    case 1:
        tick(wave);
        if (wave.timer < 0xf) return;
        spawnWave(actor, WaveSub::Ring, kRingVariant);
        actor.enterState(uint8_t(WaveState::Free));
        return;
    default: return;
    }
}
// exe 0x5bce80 (variant 2; its steps [0x5bcfc8]): and a blast rising with the timer every 4th tick
void spawnerC(StaticActor& actor, WaveFields& wave) {
    tick(wave);
    switch (actor.step) {
    case 0:
        spawnWave(actor, WaveSub::Ring, kRingVariant);
        ++actor.step;
        break;
    case 1:
        tick(wave);
        if (wave.timer < 5) break;
        spawnWave(actor, WaveSub::Dome, kDomeVariant);
        ++actor.step;
        break;
    case 2:
        tick(wave);
        if (wave.timer < 0xf) break;
        spawnWave(actor, WaveSub::Ring, kRingVariant);
        actor.enterState(uint8_t(WaveState::Free));
        break;
    case 3:
        tick(wave);
        if (wave.timer < 0x1e) break;
        spawnWave(actor, WaveSub::Ring, kRingVariant);
        actor.enterState(uint8_t(WaveState::Free));
        break;
    default: break;
    }
    if (wave.timerLow & 3) return;
    const uint32_t slot = allocActor60(0); // exe 0x5e1300
    if (!slot) return;
    StaticActor& blast = staticActorAt(slot);
    blast.flags = kActorLive;
    blast.type = typeByte(GlobalStaticType::Blast);
    blast.subtype = 0;
    blast.variant = 0;
    blast.spawnByte = 0;
    blast.pos.x = actor.pos.x;
    blast.pos.y = int32_t(uint32_t(actor.pos.y) - ((uint32_t(wave.timer) & 0xfffffffcu) << 24));
    blast.pos.z = actor.pos.z;
    BlastParams(actorHeaderAt(slot)).size = 0x100;
}
// exe 0x5bccb0: by its variant [0x8d0da4]
void spawnerRun(StaticActor& actor, WaveFields& wave) {
    switch (actor.variant) {
    case 0: spawnerA(actor, wave); return;
    case 1: spawnerB(actor, wave); return;
    case 2: spawnerC(actor, wave); return;
    // exe 0x5bd340 / 0x5bd550 take a second argument (a particle block) this dispatcher does not pass (the exe would
    // hand them its own return address)
    default: notPortedAt(kStoryPort, "0x5bccb0 variant (table 0x8d0da4; 3 / 4 need a block argument)", actor.variant);
    }
}
// exe 0x5bcc70: hidden, its block cleared, its tint
void spawnerStart(StaticActor& actor, WaveFields& wave) {
    actor.setDrawn(false);
    actor.clearClassBlock(); // exe 0x40dc40
    actor.tint = kSpawnerTint;
    actor.enterState(uint8_t(WaveState::Run));
    spawnerRun(actor, wave);
}
// exe 0x5bcfe0 / 0x5bd160: its model, hidden, scale 0, its spin
bool waveStart(StaticActor& actor, WaveFields& wave, int16_t spin) {
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
        freeActor(actor);                        // exe 0x5e1540
        return false;
    }
    actor.setDrawn(false);
    actor.airborne = 1;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.collisionBox = nullptr;
    actor.actorHit = 0;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.spin.x = 0;
    actor.spin.y = spin;
    wave.setScaleXY(0);
    actor.scale.z = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    actor.enterState(uint8_t(WaveState::Run));
    return true;
}
// the tail of exe 0x5bd070 / 0x5bd1f0: its scale x / z by the x / z step, y by the y step; spinning
void waveGrow(StaticActor& actor, WaveFields& wave) {
    actor.scale.x = int16_t(uint16_t(uint16_t(actor.scale.x) + wave.stepXZ));
    actor.scale.z = actor.scale.x;
    actor.scale.y = int16_t(uint16_t(uint16_t(actor.scale.y) + wave.stepY));
    actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + uint16_t(actor.spin.y)));
}
// exe 0x5bd070: the ring
void ringRun(StaticActor& actor, WaveFields& wave) {
    tick(wave);
    switch (wave.timer) {
    case 1:
        actor.setDrawn(true);
        wave.stepXZ = 0x2f;
        wave.stepY = 0x2f;
        break;
    case 0xc:
        wave.setScaleXY(0x2000200);
        actor.scale.z = 0x200;
        wave.stepXZ = 0x18;
        wave.stepY = 0xffd1;
        break;
    case 0x17:
        wave.setScaleXY(0x300);
        actor.scale.z = 0x300;
        wave.stepXZ = 0;
        wave.stepY = 0;
        actor.enterState(uint8_t(WaveState::Free));
        actor.setDrawn(false);
        break;
    default: break;
    }
    waveGrow(actor, wave);
}
// exe 0x5bd1f0 (its byte table 0x5bd2f4): the dome
void domeRun(StaticActor& actor, WaveFields& wave) {
    tick(wave);
    switch (wave.timer) {
    case 1:
        actor.setDrawn(true);
        wave.stepXZ = 0x1c;
        wave.stepY = 0x1c;
        break;
    case 0x13:
        wave.setScaleXY(0x2000200);
        actor.scale.z = 0x200;
        wave.stepXZ = 0xab;
        wave.stepY = 0;
        break;
    case 0x15:
        wave.stepXZ = 0xfff9;
        wave.stepY = 0xffd9;
        break;
    case 0x22:
        wave.stepXZ = 0;
        wave.stepY = 0;
        actor.setDrawn(false);
        actor.enterState(uint8_t(WaveState::Free));
        break;
    default: break;
    }
    waveGrow(actor, wave);
}
void goldShockwave(StaticActor& actor) { // exe 0x5bcc10
    WaveFields& wave = classState<WaveFields>(actor);
    actor.savePreviousPose();
    actor.wasAirborne = actor.airborne;
    st3cGoldShockwaveGlobals().waveBlock = slotFieldWord(actor, kStaticClassBlockOffset); // its timer's exe address
    const uint32_t sub = actor.subtype, state = actor.state;
    if (WaveState(state) == WaveState::Free && sub <= uint8_t(WaveSub::Dome)) {
        freeActor(actor); // 0x5afdd0: exe 0x5e1540
        return;
    }
    if (WaveSub(sub) == WaveSub::Spawner && WaveState(state) == WaveState::Start) return spawnerStart(actor, wave);
    if (WaveSub(sub) == WaveSub::Spawner && WaveState(state) == WaveState::Run) return spawnerRun(actor, wave);
    if (WaveSub(sub) == WaveSub::Ring && WaveState(state) == WaveState::Start) {
        if (waveStart(actor, wave, kRingSpin)) ringRun(actor, wave); // exe 0x5bcfe0
        return;
    }
    if (WaveSub(sub) == WaveSub::Ring && WaveState(state) == WaveState::Run) return ringRun(actor, wave);
    if (WaveSub(sub) == WaveSub::Dome && WaveState(state) == WaveState::Start) {
        if (waveStart(actor, wave, kDomeSpin)) domeRun(actor, wave); // exe 0x5bd160
        return;
    }
    if (WaveSub(sub) == WaveSub::Dome && WaveState(state) == WaveState::Run) return domeRun(actor, wave);
    notPortedAt(kStoryPort, "0x5bcc10 sub << 8 | state (table 0x8d0d98)", sub << 8 | state);
}

} // namespace

void goldShockwaveUpdate(const PlayerEnv&, uint32_t actor) { goldShockwave(staticActorAt(actor)); }

} // namespace od::port
