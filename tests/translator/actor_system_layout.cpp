// The actor system's globals in the exe's layout (game/actor_pools, game/actors): the pools' live / pending-free
// masks, the update loops' cursors, the bone blocks' mask, the hit list and the stage's class tables.
#include <cstdio>
#include <stdexcept>
#include <string>

#include "game/actor_collision.hpp"
#include "game/actor_pools.hpp"
#include "game/actors.hpp"
#include "translator/layout.hpp"
#include "translator/actor_layout.hpp"

namespace od::test {

using namespace od::port;

namespace {

std::string hex(uint32_t v) {
    char text[12];
    std::snprintf(text, sizeof text, "%x", v);
    return text;
}

// a queue's count as the exe keeps it: the address past its last entry
void queueEnd(LayoutVisitor& v, uint32_t address, uint32_t& count, uint32_t base, uint32_t stride, uint32_t room) {
    uint32_t end = base + count * stride;
    v.bytes(address, &end, 4);
    if (!v.loading()) return;
    if (end == 0) { // the boot value (the bss before the pass's first reset, which it does before any use): empty
        count = 0;
        return;
    }
    if (end < base || (end - base) % stride != 0 || (end - base) / stride > room)
        throw std::runtime_error("translator: the actor collision queue end at 0x" + hex(address) + " (0x" + hex(end) +
                                 ") is outside its queue 0x" + hex(base));
    count = (end - base) / stride;
}

// the actor collision pass's queues (exe 0xa5f2a0: 64 solids of 12 bytes, then the movers' actor words)
void collisionQueuesLayout(LayoutVisitor& v) {
    CollisionQueues& q = actorCollisionQueues();
    constexpr uint32_t kSolids = 0xa5f2a0, kMovers = 0xa5f5a0, kSolidStride = 12;
    for (size_t i = 0; i < CollisionQueues::kSolids; ++i) {
        SolidEntry& e = q.solids[i];
        const uint32_t a = kSolids + uint32_t(i) * kSolidStride;
        v.field(a + 0, e.actor);
        v.field(a + 4, e.type);
        v.field(a + 6, e.bone);
        v.field(a + 8, e.param);
    }
    for (size_t i = 0; i < CollisionQueues::kMovers; ++i) v.field(kMovers + uint32_t(i) * 4, q.movers[i]);
    queueEnd(v, 0xa743e8, q.solidCount, kSolids, kSolidStride, CollisionQueues::kSolids);
    queueEnd(v, 0xa743e4, q.moverCount, kMovers, 4, CollisionQueues::kMovers);
}

} // namespace

// the animated pool's bone blocks (exe 0xa72660, 32 x 0xb0): four bones' matrix slots, points and scale bytes each
// (laid out before the classes: a class that keeps its bytes in a block of its own lays them out over it)
void boneBlocksLayout(LayoutVisitor& v) {
    BoneBlockPool& pool = boneBlockPool();
    for (size_t i = 0; i < BoneBlockPool::size(); ++i) {
        BoneBlock& b = pool[i];
        v.bind(b.at, b);
        for (size_t k = 0; k < kBonesPerBlock; ++k) {
            const uint32_t m = b.at + uint32_t(k) * 0x20;
            v.field(m, b.matrices[k]);
            v.field(m + 0x12, b.matrixPads[k]);
            v.field(m + 0x14, b.matrixTails[k]);
            v.field(b.at + 0x80 + uint32_t(k) * 8, b.points[k]);
            v.field(b.at + 0xa0 + uint32_t(k) * 4, b.scaleBytes[k]);
            v.field(b.at + 0xa3 + uint32_t(k) * 4, b.semi[k]);
        }
    }
}


void actorSystemLayout(LayoutVisitor& v) {
    ActorSystemState& s = actorSystem();
    v.field(0xa743ec, s.shotsLive);
    v.field(0xa6bd84, s.animatedLive);
    v.field(0xa711a4, s.projectilesLive);
    v.field(0xa79fa8, s.staticsLive);
    v.field(0xa71120, s.effectsLive);
    v.field(0xa71754, s.controllersLive);
    v.field(0xa78400, s.shotsFreed);
    v.field(0xa6ab64, s.animatedFreed);
    v.field(0xa79fa0, s.projectilesFreed);
    v.field(0xa6c418, s.staticsFreed);
    v.field(0xa79ff0, s.effectsFreed);
    v.field(0xa6d174, s.controllersFreed);
    v.field(0x6d0054, s.animatedCursor);
    v.field(0x6d0058, s.shotCursor);
    v.field(0x6d005c, s.projectileCursor);
    v.field(0x6d0060, s.staticCursor);
    v.field(0x6d0064, s.effectCursor);
    v.field(0x6d0068, s.controllerCursor);
    v.field(0xa6bb90, s.boneBlocksUsed);
    v.field(0xa7a184, s.hitCount);
    for (size_t i = 0; i < kHitListCapacity; ++i) {
        HitEntry& e = s.hitList[i];
        const uint32_t a = 0xa7a1a0 + uint32_t(i) * 0x18;
        v.field(a + 0x00, e.pos);
        v.field(a + 0x08, e.attack);
        v.field(a + 0x0c, e.owner);
        v.field(a + 0x10, e.previousX);
        v.field(a + 0x12, e.previousZ);
        v.field(a + 0x14, e.box);
        if (v.loading()) e.contactBox = nativeContactBox(e.box);
    }

    StageClassTables& c = stageClassTables();
    v.field(0xa6d300, c.animated);
    v.field(0xa6bb8c, c.statics);
    v.field(0xa6bc58, c.projectiles);
    v.field(0xa72588, c.effects);
    v.field(0xa6c650, c.controllers);
    v.field(0xa5a018, c.fieldAnimated);
    collisionQueuesLayout(v);
}

} // namespace od::test
