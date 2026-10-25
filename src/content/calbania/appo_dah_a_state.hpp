// The class states of appo_dah_a.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/loaded_data.hpp"
#include "content/calbania/appo_dah_face.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::appo_dah_a_state {

// ---- the guide (exe 0x4c4980) -----------------------------------------------------------------------------------
// A node of the guide's path (exe data, 12 bytes): its point by the variant (Appo / Dah), the mask its next node is
// picked by, the next nodes
struct GuideNode : DataRecord {
    static constexpr uint32_t kBytes = 0xc;
    std::array<int16_t, 2> x{}; // +0x0
    std::array<int16_t, 2> z{}; // +0x4
    int8_t choiceMask = 0; // +0x8
    std::array<uint8_t, 3> next{}; // +0x9
    template <class R> static GuideNode load(const R& r) {
        GuideNode v;
        for (size_t i = 0; i < v.x.size(); ++i) v.x[i] = r.template read<int16_t>(0x0 + i * sizeof(int16_t));
        for (size_t i = 0; i < v.z.size(); ++i) v.z[i] = r.template read<int16_t>(0x4 + i * sizeof(int16_t));
        v.choiceMask = r.template read<int8_t>(0x8);
        for (size_t i = 0; i < v.next.size(); ++i) v.next[i] = r.template read<uint8_t>(0x9 + i * sizeof(uint8_t));
        return v;
    }
};

// ---- the gate puppets (exe 0x4c6650) -----------------------------------------------------------------------------
struct GatePuppetFields : ActorClassState {
    explicit GatePuppetFields(AnimatedActor& actor) : routine(actor.params[0]), noGround(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> routine; // the variant routine (0 only)
    uint8_t& noGround; // not kept on the ground
    uint8_t castFlags = 0; // kPuppetRestart
    uint8_t animKey = 0;
    int8_t lastTick = 0;
    AppoDahMouth mouth;

    void clearBlock() override {
        castFlags = 0;
        animKey = 0;
        lastTick = 0;
        mouth = {};
    }
};

constexpr uint32_t kGuideNodeSize = 0xc;

struct GuideFields : ActorClassState {
    explicit GuideFields(AnimatedActor& actor) : path(actor.params[0]), collision(actor.hopperBytes[3]), flags(actor.hopperBytes[4]), blinkTicks(actor.townBirdHitsHigh) {}
    uint8_t& path; // 0: the short path (ST21), else the long one
    int8_t lastNode = 0;
    uint8_t& collision; // kGuide* collision bits of the tick
    uint8_t& flags; // kGuide* flags
    int8_t reachedNode = 0;
    int8_t targetNode = 0;
    uint8_t& blinkTicks; // the blink after getting up (no hit entry while it runs)
    int8_t animKey = 0; // the key a new animation starts at
    HitBox hitBox; // its contact angle: the hit's direction
    uint16_t shoutTicks = 0; // carried: ticks to the next shout
    uint16_t knockYaw = 0; // the direction it was hit from
    uint32_t nodes = 0; // its path's first node (exe data)
    GuideNode node(int n) const { return dataRecord<GuideNode>(nodes + uint32_t(n) * kGuideNodeSize, GuideNode::kBytes); }

    void clearBlock() override {
        lastNode = 0;
        reachedNode = 0;
        targetNode = 0;
        animKey = 0;
        hitBox = {};
        shoutTicks = 0;
        knockYaw = 0;
        nodes = 0;
    }
};

} // namespace od::port::appo_dah_a_state
