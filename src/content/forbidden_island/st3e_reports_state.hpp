// The class states of st3e_reports.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/script_state.hpp"

namespace od::port::st3e_reports_state {

// The speaker's class block and its requests (the spawn parameters, which the scripts rewrite)
struct SpeakerFields : ActorClassState {
    explicit SpeakerFields(AnimatedActor& actor) : animRequest(actor.params[0]), yawOffset(actor.params[1]), turnRequest(actor.params[2]), faceRequest(actor.params[3]) {}
    uint8_t& animRequest; // kNoRequest: none
    FieldAlias<int8_t, uint8_t> yawOffset; // the yaw turned to = its yaw + 4 * this (0: none)
    FieldAlias<int8_t, uint8_t> turnRequest; // the turn step / 4 (0: none)
    FieldAlias<int8_t, uint8_t> faceRequest; // the script actor to face (>= kFacePlayer: the player; kNoRequest)
    uint8_t holdPose = 0; // & 3: the pose held (decoded without stepping)
    int16_t targetYaw = 0;
    int16_t turnStep = 0; // 0: not turning

    void clearBlock() override {
        holdPose = 0;
        targetYaw = 0;
        turnStep = 0;
    }
};

// the script's own byte: which talk runs
struct Script64Bytes : ScriptPart<Script64Bytes> {
    uint8_t secondTalk = 0; // +0xc
};

} // namespace od::port::st3e_reports_state
