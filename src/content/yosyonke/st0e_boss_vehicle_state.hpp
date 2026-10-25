// The class states of st0e_boss_vehicle.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st0e_boss_vehicle_state {

// ==== the boss vehicle (exe 0x501d70) ================================================================================
// Its block (bone block 3, kept as the vehicle's own data)
struct VehicleBlock {
    StateArray<HitBoxHead, 10> boxes{};
    StateArray<uint32_t, 2> watchers{};  // the e0 watchers owning the front points' entries (identity words)
    uint32_t fireball = 0;               // the charging fireball (kNoFireball: none)
    StateArray<uint32_t, 2> emitters{};  // the burning sequence's fire emitters
    uint32_t unread0 = 0;
    int16_t chargeTicks = 0;
    int16_t restTicks = 0;
    int16_t burnTicks = 0;
    uint8_t burnStep = 0;
};

// ==== the fireball (exe 0x5bc3f0, static stage type 0x5c) ============================================================
// Its bytes (it has no model: the slot's model fields hold its sparks): the five sparks' sizes, yaws, pitches, falls and
// delays, its homing ticks, the burst's step, its hit-list box, its colour, size and the burst's angle
struct FireballFields : ActorClassState {
    explicit FireballFields(StaticActor& actor) : burstStep(actor.colourTables[0], 3), box(actor.hitBox) {}
    PartField<int8_t, uint32_t> burstStep;
    HitBoxHead& box;
    StateArray<int16_t, 5> sparkSize{};
    StateArray<int16_t, 5> sparkYaw{};
    StateArray<int16_t, 5> sparkPitch{};
    StateArray<int16_t, 5> sparkFall{};
    int16_t homingTicks = 0;
    StateArray<uint8_t, 5> sparkDelay{};
    uint32_t colour = 0;
    int16_t size = 0;
    uint16_t burstAngle = 0;

    void clearBlock() override {
        colour = 0;
        size = 0;
        burstAngle = 0;
    }
};

// the vehicle's bytes
struct VehicleFields : ActorClassState {
    explicit VehicleFields(AnimatedActor& actor) : fireballsSpawned(actor.stepCounter), turret(actor.bossFlags), flagsHigh(actor.guardianBits), cratesBeforeTilt(actor.hopperTimer) {}
    uint8_t& fireballsSpawned; // the charge's fireball spawned (the step counter's byte)
    uint32_t& turret;
    uint8_t& flagsHigh;
    uint8_t& cratesBeforeTilt;
    uint32_t headWord_ = 0;
    BoneBlockArea<VehicleBlock> block;
    int16_t timer = 0; // the crate's timer / the end's delay
    uint16_t flags = 0; // kVehicle*
    int16_t fireballAim = 0;
    uint16_t chargePointHp = 0;
    int16_t damageCoolDown = 0;
    int16_t voiceTimer = 0; // the 0x278 voice's ticks
    int16_t hurtVoiceTimer = 0;
    uint8_t voiceDelay = 0;
    PartField<uint8_t, int16_t> timerLow{timer, 0};
    PartField<uint8_t, int16_t> timerHigh{timer, 1};
    PartField<uint8_t, uint16_t> flagsLow{flags, 0};
    void clearHead() {
        headWord_ = 0;
    }
    void clearFlagsWord() {
        flags = 0;
        fireballAim = 0;
        flagsHigh = 0;
    }
    void resetVoiceWord() { set_voiceWord_(uint32_t(0x3c000000u)); }

private:
    uint32_t get_voiceWord_() const { return uint32_t(uint32_t(std::make_unsigned_t<int16_t>(hurtVoiceTimer)) << 0 | uint32_t(std::make_unsigned_t<uint8_t>(voiceDelay)) << 24 | uint32_t(std::make_unsigned_t<uint8_t>(cratesBeforeTilt)) << 16); }
    void set_voiceWord_(uint32_t v) { hurtVoiceTimer = int16_t(std::make_unsigned_t<int16_t>(v >> 0)); voiceDelay = uint8_t(std::make_unsigned_t<uint8_t>(v >> 24)); cratesBeforeTilt = uint8_t(std::make_unsigned_t<uint8_t>(v >> 16)); }
public:

    void clearBlock() override {
        headWord_ = 0;
        block = nullptr;
        timer = 0;
        flags = 0;
        fireballAim = 0;
        chargePointHp = 0;
        damageCoolDown = 0;
        voiceTimer = 0;
        hurtVoiceTimer = 0;
        voiceDelay = 0;
    }
};

} // namespace od::port::st0e_boss_vehicle_state
