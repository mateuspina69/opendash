// The class states of ruin_guardian.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::ruin_guardian_state {

// The guardian's own values: the mines' sweep yaw (the rotation's spare lane) and its class block
struct GuardianFields : ActorClassState {
    explicit GuardianFields(AnimatedActor& actor) : sweepYaw(actor.rotation.spare), mode(actor.hopperBytes[2]), mineGroup(actor.hopperBytes[3]), mineMarks(actor.hopperBytes[4]), shotsLeft(actor.hopperBytes[5]), damageStage(actor.townBirdHits) {}
    int16_t& sweepYaw;
    uint8_t& mode; // kGuardian*
    uint8_t& mineGroup; // the enemy-group entries: its own (the mines go off on its mark) ...
    uint8_t& mineMarks; // ... and the mines' (they mark it near the player)
    uint8_t& shotsLeft;
    uint8_t& damageStage;
    HitBoxHead hit;
    uint16_t ticks = 0;
    union {
        uint16_t volleys = 0; // the volleys left
        int16_t groundY; // the jump's landing height
    };
    int16_t headYaw = 0; // the head's turn and pitch (added to the pose)
    int16_t headPitch = 0;
    int16_t aimOffset = 0;
    uint16_t hurtVoiceTicks = 0;
    StateArray<int8_t, 2> beams{}; // the beams' slots (-1: none)
    uint8_t minesLive = 0;

    void clearBlock() override {
        hit = {};
        ticks = 0;
        volleys = 0;
        headYaw = 0;
        headPitch = 0;
        aimOffset = 0;
        hurtVoiceTicks = 0;
        beams = {};
        minesLive = 0;
    }
};

// ---- the beams ------------------------------------------------------------------------------------------------------
// The beam's own values: its length (its spawn parameters' dword), its damage (the header's last state byte), its speed
// (the velocity's z), its sway (the spin's lanes: the phase, the speed, the phase step, the speed step), its hit box,
// its last point, its length backwards, its loop sound, its life, its width; its longest
struct BeamFields : ActorClassState {
    explicit BeamFields(StaticActor& actor) : length(actor.params, 0), swayPhase(actor.spin.x), swaySpeed(actor.spin.y), phaseStep(actor.spin.z), swayStep(actor.spin.spare), loopSound(actor.rootTranslation[0]), life(actor.rootTranslation[1], 0), width(actor.rootTranslation[1], 2) {}
    BytesField<int32_t, 4> length;
    int16_t& swayPhase;
    int16_t& swaySpeed;
    int16_t& phaseStep;
    int16_t& swayStep;
    FieldAlias<uint32_t, int32_t> loopSound;
    PartField<uint16_t, int32_t> life;
    PartField<int8_t, int32_t> width;
    HitBoxHead hit;
    ShortVec3 lastPoint;
    int32_t backLength = 0;
    uint16_t longest = 0;

    void clearBlock() override {
        longest = 0;
    }
};

// The orb's own values (the projectile slot's tail): the trail phase, the points (the big orb's two trails of 5, the
// ring's 9; the mine's run: 8 x / z pairs), its guardian, the ticks, the turn level, the hit box, the bob's target
// height, the bob phase / the ring's radius target and radius, the drift's target, the mines' group entries
// The orb's own values (the projectile slot's tail, over the mesh fields it does not use): the trail phase, the points
// (the big orb's two trails of 5, the ring's 9; the mine's run: 8 x / z pairs), its guardian (the hit box's result
// word: OrbParams), the ticks, the turn level, the hit box, the bob's target height, the bob phase / the ring's radius
// target and radius, the drift's target, the mines' group entries
struct OrbFields : ActorClassState {
    explicit OrbFields(ProjectileActor& actor) : guardian(actor.hitBox.result), slot_(actor) {}
    // points of the row from `first` on
    struct Rows {
        StateArray<ShortVec3, 10>& row;
        size_t first;
        size_t count;
        ShortVec3& operator[](size_t k) const {
            if (k >= count) stateIndexError(k, count);
            return row[first + k];
        }
    };
    uint16_t phase = 0;
    StateArray<ShortVec3, 10> pointRow{};
    const Rows points{pointRow, 0, 9};
    const Rows trailA{pointRow, 0, 5};
    const Rows trailB{pointRow, 5, 5};
    uint32_t& guardian; // the guardian's identity word
    uint16_t ticks = 0;
    int16_t turnLevel = 0;
    HitBoxHead hit;
    int16_t bobY = 0;
    uint8_t bobPhase = 0;   // the mine's bob / the ring's radius target
    uint8_t ringRadius = 0;
    int16_t driftX = 0;
    int16_t driftZ = 0;
    uint8_t ownGroup = 0;
    uint8_t guardianGroup = 0;

    // exe 0x40dc40(+0xc0, +0xd8): the block from the ticks on cleared (the slot's hit-box previous x / z and debris
    // word with it)
    void clearBlockBytes() {
        slot_.hitBox.previousX = 0;
        slot_.hitBox.previousZ = 0;
        slot_.debrisFlags = 0;
        ticks = 0;
        turnLevel = 0;
        hit = {};
        bobY = 0;
        bobPhase = 0;
        ringRadius = 0;
        driftX = 0;
        driftZ = 0;
        ownGroup = 0;
        guardianGroup = 0;
    }

private:
    ProjectileActor& slot_;
};

} // namespace od::port::ruin_guardian_state
