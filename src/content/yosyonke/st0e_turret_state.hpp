// The class states of st0e_turret.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "content/yosyonke/st0e_turret.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st0e_turret_state {

// ==== the turret (exe 0x503bd0) =======================================================================================
// Its block (bone block 2, kept as the turret's own data)
struct TurretBlock {
    // +0x00: the three parts' fire emitters and the arms' (kNoActor: none; their slots' identity words), then the hit
    // owners of the parts' entries (the second arm's emitter is the first watcher's word, as the exe stores it)
    StateArray<uint32_t, 12> words{};
    StateArray<HitBoxHead, 8> boxes{};
    StateArray<int16_t, 8> partHp{}; // the arms, the three parts, the core (< 0: gone)
    int16_t sweep = 0;               // the beam's sweep and its speed
    int16_t sweepSpeed = 0;
    int16_t cannonTimer = 0;
    int16_t volleyTimer = 0;
    StateArray<int16_t, 2> armTimer{}; // the arms' blasts' delays
    int16_t dyingTimer = 0;
    int16_t endDelay = 0;
    int8_t burstCount = 0;
    int8_t bombSlot = 0;

    uint32_t& emitters(size_t k) {
        if (k >= 5) stateIndexError(k, 5);
        return words[k];
    }
    uint32_t& watchers(size_t k) {
        if (k >= 7) stateIndexError(k, 7);
        return words[5 + k];
    }
    // the arm k's fire emitter (the exe stores it at +0x10 + k * 4: the second arm's over the first watcher)
    uint32_t& armEmitter(uint32_t k) { return words[4 + k]; }
    // the parts 2 / 3's HPs gone (the exe's dword store at +0x7c)
    void clearMiddlePartsWord() {
        partHp[2] = -1;
        partHp[3] = -1;
    }
};

// ==== the glow (exe 0x43a760, effect stage type 0x1b) =================================================================
struct GlowFields : ActorClassState {
    explicit GlowFields(EffectActor& actor) : released(actor.step), targetSize(actor.params, 0), growth(actor.params[2]), pulse(actor.params[3]), size(actor.trail[2]) {}
    uint8_t& released; // set when the turret dies (the step's byte)
    BytesField<int16_t, 4> targetSize;
    FieldAlias<int8_t, uint8_t> growth;
    uint8_t& pulse;
    int16_t& size;
    uint32_t colour = 0;
};

constexpr uint32_t kNoActor = 0xffffffffu;

// the turret's bytes
struct TurretFields : ActorClassState {
    explicit TurretFields(AnimatedActor& actor) : beamStep(actor.stepCounter), volleySlot(actor.params[0]), bombsLeft(actor.params[1]), rounds(actor.params[2]), beamTimer(actor.params[3]), hurtVoiceTimer(actor.spin.x), blastMark(actor.spin.y, 0), voicesSaid(actor.spin.y, 1), flagsLow(actor.townBirdHits), flagsHigh(actor.townBirdHitsHigh), objectState(actor.hopperTimer) {}
    uint8_t& beamStep; // the step counter's byte
    FieldAlias<int8_t, uint8_t> volleySlot;
    uint8_t& bombsLeft;
    uint8_t& rounds;
    uint8_t& beamTimer;
    int16_t& hurtVoiceTimer; // the spin's x lane
    PartField<uint8_t, int16_t> blastMark; // set to 3 by the blasts (its reader is not traced)
    PartField<uint8_t, int16_t> voicesSaid;
    uint8_t& flagsLow;
    uint8_t& flagsHigh;
    uint8_t& objectState;
    BoneBlockArea<TurretBlock> block;
    uint32_t beam = 0; // the beam (static 0x56; kNoActor: none)
    uint32_t glow = 0; // the beam's glow (effect 0x1b; kNoActor: none)
    int16_t bombAim = 0;
    int16_t beamAim = 0;
    int16_t beamYaw = 0;
    StateArray<int16_t, 2> cannonYaw{};
    StateArray<int16_t, 2> cannonPitch{};
    int16_t coreTimer = 0;
    uint16_t flags = 0; // kTurret*
    uint8_t damageCoolDown = 0;
    void clearPitchesWord() {
        cannonPitch = {};
    }

    void clearBlock() override {
        block = nullptr;
        beam = 0;
        glow = 0;
        bombAim = 0;
        beamAim = 0;
        beamYaw = 0;
        cannonYaw = {};
        cannonPitch = {};
        coreTimer = 0;
        flags = 0;
        damageCoolDown = 0;
    }
};

// ==== the bomb (exe 0x5cc850, pool 0x40 stage type 0x24) ==============================================================
struct BombFields : ActorClassState, BombSpawnParams {
    explicit BombFields(ProjectileActor& actor) : BombSpawnParams(actor), frameU(actor.params, 2), box(actor.hitBox) {}
    BytesField<int16_t, 4> frameU; // the sprite's u cell
    HitBoxHead& box;
    uint16_t unusedC8 = 0;
    int16_t blastDelay = 0; // 0: the blast's sound (always 0: its writer is the init)
    int16_t frameTimer = 0;
    int16_t size = 0;
    uint8_t unusedD6 = 0;
    uint8_t grey = 0;
};

// the exe's debug counters of the turret (the second pad's bits 0x10 / 0x40 count the cannon shots' counter) and the
// sweep voice's roll
struct TurretDebug {
    uint16_t counterA = 0; // 0xa59eb0
    uint16_t counterB = 0; // 0xa59eb2
    uint16_t shotCounter = 0; // 0xa59eb4
    uint32_t sweepRoll = 0; // 0xa59eac
};
inline TurretDebug& turretDebug() {
    static TurretDebug s;
    return s;
}

} // namespace od::port::st0e_turret_state
