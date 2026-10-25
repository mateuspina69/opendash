// The suction effect (see suction_effect.hpp).
#include "game/actor_state.hpp"
#include "game/suction_effect.hpp"

#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/draw_commands.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/shots/shot_common.hpp"
#include "game/stage_state.hpp"

namespace od::port {

SuctionParticles::SuctionParticles(VacuumShotFields& shot) {
    bytes_.reserve(0x40);
    for (auto* row : {&shot.angle, &shot.spare, &shot.life, &shot.brightness})
        for (size_t k = 0; k < 0x10; ++k) bytes_.emplace_back((*row)[k]);
}

namespace {

inline constexpr const char* kSuctionArea = "suction effect";

// byte b (0..0x27) of an effect slot as the exe lays it out: the header bytes, the position's and the trail's shorts
ParticleByte effectSlotByte(EffectActor& e, size_t b) {
    uint8_t* const header[16] = {&e.flags, &e.runFlags, &e.slot, &e.loopFlags, &e.type, &e.subtype, &e.variant,
                                 &e.spawnByte, &e.state, &e.subState, &e.step, &e.stepCounter, &e.params[0],
                                 &e.params[1], &e.params[2], &e.params[3]};
    if (b < 0x10) return ParticleByte(*header[b]);
    int16_t* const lanes[4] = {&e.pos.x, &e.pos.y, &e.pos.z, &e.posSpare};
    if (b < 0x18) return ParticleByte(*lanes[(b - 0x10) / 2], unsigned(b & 1));
    return ParticleByte(e.trail[(b - 0x18) / 2], unsigned(b & 1));
}

enum class SuctionState : uint8_t { Start = 0, Hold = 1, Fade = 2 };
constexpr uint8_t kSuctionPulls = 0x01; // the player's suction bit the effect grows on
constexpr int8_t kMaxStrength = 8;
constexpr uint8_t kSoundTicks = 0x1a, kHumTicks = 9;
constexpr uint16_t kStartSound = 0xf7, kHumSound = 0xf8, kStopSound = 0xf9;
constexpr uint8_t kParticleTicks = 0x10;

// a particle reborn: a random angle step, byte, life (0x10..0x1f) and the starting brightness
void rebirth(SuctionParticles& p, uint32_t k) {
    p.angle[k] = uint8_t(rngStep() & 0x3f);
    p.spare[k] = uint8_t((rngStep() & 0x7f) + 0x40);
    p.life[k] = uint8_t((rngStep() & 0xf) + kParticleTicks);
    p.brightness[k] = 4;
}
// the effect at the player's hand
void atHand(EffectActor& effect) {
    const ShortVec3& hand = player().bonePoints[kLeftHandBone];
    effect.pos.x = hand.x;
    effect.pos.y = hand.y;
    effect.pos.z = hand.z;
}
// a grey of level g in the three colour bytes
uint32_t grey(uint32_t g) { return ((g << 8 | g) << 8) | g; }

// exe 0x55e110(effect, particles): while the effect's point (<< the stage shift) is within the effects' depth and on
// the screen (else nothing, no RNG either): the cone -- 8 rings j of 16 POLY_G4 (OT packets, DR_MODE 0xe1000220: abr 1)
// of local points (x = cos64 [i], z = sin64 [i] at the 64-step index i, radius j * 32 inner / (j + 1) * 32 outer,
// >> (0xc - shift); y = -j * 32 / (-1 - j) * 32) through Rz(0) Ry Rx of the player's aim (the angle word
// (pitch + 0x400) | yaw << 16, OR'ed as the exe does) scaled by ((range + 2) << (shift + 9)) >> 12; quad
// (inner [i - 4], outer [i], inner [i], outer [i + 4]), grey colours pulsing with the frame and fading with the ring and
// the strength -- then the 16 particles (each life ticks down, the angle steps by 2 and the brightness by 4, reborn at
// 0) drawn as 0xf0 discs rotated by the player's aim from the effect. Port: the cone's corners go through the
// presentation side channel (dc::otQuadG4) as camera-input points (the exe premultiplies the camera matrix: rounding differs, presentation only).
void drawSuction(const EffectActor& effect, SuctionParticles& particles) {
    const SuctionFields& fields = classState<SuctionFields>(effect);
    const uint32_t sh = stage().coordinateShift;
    const int16_t v[3] = {wrap16(int(uint32_t(int32_t(effect.pos.x)) << (sh & 0x1f))),
                          wrap16(int(uint32_t(int32_t(effect.pos.y)) << (sh & 0x1f))),
                          wrap16(int(uint32_t(int32_t(effect.pos.z)) << (sh & 0x1f)))};
    const CameraPoint c = cameraPoint(v);
    if (!effectDepthVisible(c) || !effectOnScreen(c)) return;
    const Player& p = player();
    const uint32_t scale = uint32_t(int32_t(fields.rangeLevel) + 2) << ((sh + 9) & 0x1f);
    const uint32_t aim = uint32_t(int32_t(p.rotation.x) + kQuarterTurn) | uint32_t(int32_t(p.rotation.y)) << 16;
    Matrix3 r = rotationZYX(int16_t(aim), int16_t(aim >> 16), 0); // exe 0x40c070
    for (int k = 0; k < 9; ++k) r.e[k] = wrap16(int(uint32_t(int32_t(r.e[k])) * scale >> 12));
    const uint32_t s = (0xcu - sh) & 0x1f;
    // a local ring point (x / z = the 64-step tables * rad >> s, y as given) -> a camera-input point
    auto corner = [&](uint32_t idx, uint32_t rad, int16_t y, int16_t out[3]) {
        const int16_t q[3] = {wrap16(int(uint32_t(int32_t(cos64(idx))) * rad >> s)), y,
                              wrap16(int(uint32_t(int32_t(sin64(idx))) * rad >> s))};
        for (int k = 0; k < 3; ++k)
            out[k] = wrap16(
                v[k] +
                ((int32_t(r.e[k * 3]) * q[0] + int32_t(r.e[k * 3 + 1]) * q[1] + int32_t(r.e[k * 3 + 2]) * q[2]) >> 12));
    };
    const uint32_t frame = gameState().frame;
    const uint32_t strength = uint32_t(int32_t(fields.strength));
    for (uint32_t j = 0; j < 8; ++j) {
        const int16_t yIn = wrap16(int(uint32_t(0u - j) << 5)), yOut = wrap16(int(uint32_t(0xffffffffu - j) << 5));
        const uint32_t phase = (j + frame * 2) << 3;
        const uint32_t colA = (uint32_t(sin64(phase) + 0x2000) * (8 - j) * strength * 3) >> 15;
        const uint32_t colB = (uint32_t(sin64(phase + 8) + 0x2000) * (7 - j) * strength * 3) >> 15;
        int16_t inPrev[3], outPrev[3];
        corner(60, j << 5, yIn, inPrev);
        corner(0, (j + 1) << 5, yOut, outPrev);
        for (uint32_t i = 0; i < 0x40; i += 4) {
            int16_t inNew[3], outNew[3];
            corner(i, j << 5, yIn, inNew);
            const uint32_t e = i + (frame - j) * 4;
            const uint32_t w0 = uint32_t(sin64((e & 0xf) * 4) + 0x1000),
                           w1 = uint32_t(sin64((e * 4 + 0x10) & 0x3f) + 0x1000);
            const uint32_t c0 = (w0 * colA) >> 13, c1 = (w0 * colB) >> 13;
            corner((i + 4) & 0x3f, (j + 1) << 5, yOut, outNew);
            const uint32_t c2 = (w1 * colA) >> 13, c3 = (w1 * colB) >> 13;
            const uint32_t col[4] = {dc::packetColour(dc::kPolyG4Semi, grey(c0)), grey(c1), grey(c2), grey(c3)};
            const int16_t q[4][3] = {{inPrev[0], inPrev[1], inPrev[2]},
                                     {outPrev[0], outPrev[1], outPrev[2]},
                                     {inNew[0], inNew[1], inNew[2]},
                                     {outNew[0], outNew[1], outNew[2]}};
            const float off[4][2] = {};
            dc::otQuadG4(effect.at, col, (0x220 >> 5) & 3, q, off); // DR_MODE 0xe1000220 linked before it
            for (int k = 0; k < 3; ++k) {
                inPrev[k] = inNew[k];
                outPrev[k] = outNew[k];
            }
        }
    }
    for (uint32_t k = 0; k < 0x10; ++k) {
        const uint8_t life = uint8_t(particles.life[k] - 1u);
        particles.life[k] = life;
        if (life != 0) {
            particles.angle[k] = uint8_t((particles.angle[k] + 2u) & 0x3fu);
            particles.brightness[k] = uint8_t(particles.brightness[k] + 4u);
        } else {
            rebirth(particles, k);
        }
        const uint32_t reach = (uint32_t(int32_t(fields.rangeLevel) + 2) * particles.life[k]) << ((sh + 4) & 0x1f);
        const uint32_t step = particles.angle[k];
        int32_t o[3];
        rotateVector(p.rotation, int(uint32_t(int32_t(cos64(step))) * reach >> s),
                     int(uint32_t(int32_t(sin64(step))) * reach >> s), int((0u - reach) >> 6), o); // exe 0x5e3970
        const uint16_t px = uint16_t((o[0] >> 16) + effect.pos.x), py = uint16_t((o[1] >> 16) + effect.pos.y),
                       pz = uint16_t((o[2] >> 16) + effect.pos.z);
        const uint32_t g = uint32_t((int32_t(particles.brightness[k]) * int32_t(fields.strength)) >> 3);
        const uint32_t colour = ((((g | 0xfffff000u) << 8) | g) << 8) | g;
        dc::emitDisc(effect.at, colour, uint32_t(px) | uint32_t(py) << 16, uint32_t(pz) | dc::ringStep(4), 0x20000u);
    }
}

} // namespace

void suctionStart(EffectActor& effect, SuctionParticles& particles) {
    auto&& fields = classState<SuctionFields>(effect);
    fields.strength = 1;
    effect.runFlags |= kActorWarpKept;
    fields.soundTicks = kSoundTicks;
    for (uint32_t k = 0; k < 0x10; ++k) rebirth(particles, k);
    drawSuction(effect, particles);
    soundPlayAtPlayer(kStartSound);
    ++effect.state;
}

void suctionHold(EffectActor& effect, SuctionParticles& particles) {
    auto&& fields = classState<SuctionFields>(effect);
    if (!(player().suction & kSuctionPulls)) {
        soundPlayAtPlayer(kStopSound);
        ++effect.state;
        drawSuction(effect, particles);
        return;
    }
    --fields.soundTicks;
    if (fields.soundTicks == 0) {
        fields.soundTicks = kHumTicks;
        soundPlayAtPlayer(kHumSound);
    }
    atHand(effect);
    if (fields.strength < kMaxStrength) ++fields.strength;
    drawSuction(effect, particles);
}

void suctionFade(EffectActor& effect, SuctionParticles& particles) {
    auto&& fields = classState<SuctionFields>(effect);
    --fields.strength;
    if (uint8_t(fields.strength) == 0) {
        freeActor(effect); // exe 0x5e1580
        return;
    }
    atHand(effect);
    drawSuction(effect, particles);
}

void suctionUpdate(const PlayerEnv&, uint32_t address) {
    EffectActor& effect = effectActorAt(address);
    SuctionParticles particles(classState<VacuumShotFields>(*classState<SuctionFields>(effect).vacuumShot));
    switch (SuctionState(effect.state)) { // [0x8c3f14](effect, [effect + 0x18])
    case SuctionState::Start: suctionStart(effect, particles); return;
    case SuctionState::Hold: suctionHold(effect, particles); return;
    case SuctionState::Fade: suctionFade(effect, particles); return;
    default: notPortedAt(kSuctionArea, "0x55df40 state", effect.state);
    }
}

SuctionParticles::SuctionParticles(EffectActor& slot) {
    EffectPool& pool = effectPool();
    const size_t first = EffectPool::index(slot);
    bytes_.reserve(0x40);
    for (size_t j = 0; j < 0x40; ++j) {
        const size_t at = 0x18 + j; // from the slot's trail on, across the next slots
        const size_t i = first + at / EffectPool::stride();
        if (i >= EffectPool::size())
            throw ActorLayoutError("suction effect: the jet's particles run past the effect pool (the exe's memory after it)");
        bytes_.push_back(effectSlotByte(pool[i], at % EffectPool::stride()));
    }
}

} // namespace od::port
