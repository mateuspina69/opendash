// The class states of talking_npc.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/save_menus.hpp"
#include "game/field_alias.hpp"

namespace od::port::talking_npc_state {

// the talking NPC's own fields
struct TalkingNpcFields : ActorClassState {
    explicit TalkingNpcFields(AnimatedActor& actor)
        : anim(actor.params[0]), behaviour(actor.params[1]), hintsMessage(actor.params[2]), message(actor.params[3]) {}
    uint8_t& anim;         // the animation it plays (the message script's choice)
    uint8_t& behaviour;    // kTalkerFrozen | kTalkerEventFlag
    uint8_t& hintsMessage; // the twin's hints message (in the stage's message bank)
    uint8_t& message;      // its talk event's message
    HitBoxHead hitBox;
    uint8_t talkFlags = 0; // kTalking
    uint8_t talkPad = 0;   // the byte after it (cleared with the talk state)
    uint8_t startKey = 0;  // the key a new animation starts at
    uint8_t frameCopy = 0; // the current key's frame byte, copied every tick
    SaveMenuBlock saveMenu; // the save menu's block (game/save_menus saveMenuTick)

    // the talk flags, the byte after, the start key and the frame copy cleared (the exe's one dword store)
    void clearTalkState() {
        talkFlags = 0;
        talkPad = 0;
        startKey = 0;
        frameCopy = 0;
    }

    void clearBlock() override {
        hitBox = {};
        clearTalkState();
        saveMenu = {};
    }
};

} // namespace od::port::talking_npc_state
