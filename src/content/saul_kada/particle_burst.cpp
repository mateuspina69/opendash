// The burst of particles (static type 0x4c sub 0: ST27 / 28 / 34 / 42 / 55, exe 0x5b5a00; hypothesis names): the ruin
// flyers' shot. Start: particle 0 at the burst, 1..9 scattered (+-0x20 x / z, -0x20..0x5f y), each with a random spin
// nibble; one velocity (forward -0x1bf..-0x140, falling 8 more every other tick); each tick every particle moves by it
// (its x speed its nibble, mirrored for particles 0 / 1) and draws a 0xf0 disc whose colour cycles 9..0; one hit entry
// at particle 0; freed when the forward speed reaches 0. States 3 / 4: the cutscene variant ([0x8d09d0], the script's
// cue byte).
// States [0x8d09a0]: 0 start 0x5b5a20, 1 fly 0x5b5b80, 2 / 5 freed 0x5afdd0, 3 the cutscene bind 0x5b5dd0, 4 the
// cutscene's cue 0x5b5e50.
#include "content/saul_kada/particle_burst.hpp"
#include "content/saul_kada/particle_burst_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/draw_commands.hpp"
#include "game/game_state.hpp"
#include "game/hit_list.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace particle_burst_state;

namespace {

enum class BurstState : uint8_t { Start = 0, Fly = 1, Free = 2, CutsceneBind = 3, CutsceneCue = 4, FreeToo = 5 };
// the variant 0 cue routines (exe table 0x8d09b8, by the script's cue byte: the named routines of engine/routines.csv)
const DataTable<RoutineTable<0x10>> cutsceneCues{TableId::ParticleBurstCutsceneCues};
// its hit entry: group 3 (radius 0x64), a strong hit (0x4000: its other bit, not traced)
constexpr uint32_t kBurstShape = hitGroup(kHitGroupHeavyThrown) | hitRadius(0x64);
constexpr uint32_t kBurstAttack = kHitAttackStrong | 0x4000u;

// exe 0x5b5a20
void start(StaticActor& actor, BurstFields& burst) {
    for (uint32_t i = 0; i < kParticles; ++i) {
        BurstParticle& p = burst.particles[i];
        if (i == 0) {
            p.point.x = actor.pos.x.whole;
            p.point.y = actor.pos.y.whole;
            p.point.z = actor.pos.z.whole;
            p.word = 0;
            continue;
        }
        p.point.x = int16_t((rngStep() & 0x3f) + uint16_t(actor.pos.x.whole) - 0x20u);
        p.point.y = int16_t((rngStep() & 0x7f) + uint16_t(actor.pos.y.whole) - 0x20u);
        p.point.z = int16_t((rngStep() & 0x3f) + uint16_t(actor.pos.z.whole) - 0x20u);
        p.word = uint16_t(((rngStep() & 0xf) << 4) | i);
    }
    ++actor.state;
    const uint32_t r = rngStep();
    actor.velocity.y = 0;
    actor.acceleration.z = 6;
    actor.velocity.z = int16_t(int32_t(r & 0x7f) - 0x1bf);
    actor.acceleration.y = 8;
}
// exe 0x5b5b80
void fly(StaticActor& actor, BurstFields& burst) {
    actor.savePreviousPose();
    for (uint32_t i = 0; i < kParticles; ++i) {
        BurstParticle& p = burst.particles[i];
        actor.pos.x.whole = p.point.x;
        actor.pos.y.whole = p.point.y;
        actor.pos.z.whole = p.point.z;
        const int spin = (int8_t(uint8_t(p.word)) >> 4) & 0xf;
        actor.velocity.x = int16_t(i / 2 != 0 ? spin : -spin);
        integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
        p.point.x = actor.pos.x.whole;
        p.point.y = actor.pos.y.whole;
        p.point.z = actor.pos.z.whole;
    }
    if (actor.velocity.z < 0) {
        actor.velocity.z += uint16_t(actor.acceleration.z);
    } else {
        actor.velocity.z = 0;
        actor.state = uint8_t(BurstState::Free);
    }
    if (gameState().frame & 1) actor.velocity.y += uint16_t(actor.acceleration.y);
    // 0x5b5cc0: the particles' discs (0xf0 records)
    for (uint32_t i = 0; i < kParticles; ++i) {
        BurstParticle& p = burst.particles[i];
        const uint32_t w = p.word, s = w & 0xf, colour = s ? s - 1 : 9;
        p.word = uint16_t((w & 0xf0) | colour);
        dc::emit({(colour << 18) | 0x4000u, ((s * 0x101) << 4) | 0xf0002060u, uint32_t(uint16_t(p.point.x)) | uint32_t(uint16_t(p.point.y)) << 16, s * 0x40cu | 0x22002060u,
                  uint32_t(uint16_t(p.point.z)) | 0x40000u, 0},
                 actor.at);
    }
    burst.hitBox.previousX = actor.prevPos.x.whole;
    burst.hitBox.previousZ = actor.prevPos.z.whole;
    burst.hitBox.result = 0;
    const BurstParticle first = burst.particles[0];
    const int16_t pos[3] = {first.point.x, first.point.y, first.point.z};
    hitListSubmit(pos, burst.hitBox, hitOwner(actor.slot) | kBurstShape, kBurstAttack); // exe 0x5e4120
    actor.pos.x.whole = first.point.x;
    actor.pos.y.whole = first.point.y;
    actor.pos.z.whole = first.point.z;
}
// exe 0x5b5dd0: false when the model is missing (the burst freed)
bool cutsceneBind(StaticActor& actor, BurstFields& burst) {
    if (!bindActorModel(actor, 0)) {
        freeActor(actor);
        return false;
    }
    actor.flags |= kActorDrawn;
    actor.clearClassBlock();
    actor.velocity.z = 0;
    actor.velocity.y = 0;
    actor.velocity.x = 0;
    actor.acceleration.z = 0;
    actor.acceleration.y = 0;
    actor.acceleration.x = 0;
    actor.spin.y = 0;
    actor.spin.x = 0;
    burst.spinSpare = 0;
    actor.spin.z = 0;
    actor.enterState(uint8_t(BurstState::Fly));
    return true;
}
// exe 0x5b5e50: [[0x8d09d0 + variant * 4] + the script's cue * 4]
void cutsceneCue(StaticActor& actor) {
    if (actor.variant != 0) {
        notPortedAt(kStoryPort, "0x5b5e50 variant (table 0x8d09d0 points into code)", actor.variant);
        return;
    }
    const size_t cue = size_t(int32_t(int8_t(scriptState().part<CastTimelineStep>().stepSub)));
    if (cutsceneCues->idAt(cue) != RoutineId::EmptyRoutine)
        notPortedAt(kStoryPort, "0x5b5e50 cue function", cutsceneCues->pointerAt(cue));
}

void particleBurst(StaticActor& actor) {
    BurstFields& burst = classState<BurstFields>(actor);
    switch (BurstState(actor.state)) {
    case BurstState::Start: start(actor, burst); return;
    case BurstState::Fly: fly(actor, burst); return;
    case BurstState::Free:
    case BurstState::FreeToo: freeActor(actor); return; // 0x5afdd0 -> exe 0x5e1540
    case BurstState::CutsceneBind:
        if (!cutsceneBind(actor, burst)) return;
        [[fallthrough]];
    case BurstState::CutsceneCue: cutsceneCue(actor); return;
    default: notPortedAt(kStoryPort, "0x5b5a00 state (table 0x8d09a0)", actor.state);
    }
}

} // namespace

void particleBurstUpdate(const PlayerEnv&, uint32_t actor) { particleBurst(staticActorAt(actor)); }

} // namespace od::port
