// Netplay's shared actors (see net_puppets.hpp).
#include "game/net_puppets.hpp"

#include <array>
#include <cstdlib>
#include <set>
#include <utility>

#include "game/actor_animation.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actors.hpp"
#include "game/hit_list.hpp"
#include "game/net_world.hpp"

namespace od::port {

namespace {

constexpr uint8_t kAnimatedFirst = 0x20, kProjectileFirst = 0x40, kProjectileEnd = 0x60;

// a slot id of the shared pools: the field's animated slots and the projectiles
bool sharedPoolSlot(uint8_t slot) {
    return (slot >= kAnimatedFirst && slot < kAnimatedFirst + kAnimatedFieldSlots) ||
           (slot >= kProjectileFirst && slot < kProjectileEnd);
}
ActorHeader& slotHeader(uint8_t slot) {
    if (slot < kProjectileFirst) return animatedPool()[slot - kAnimatedFirst];
    return projectilePool()[slot - kProjectileFirst];
}
ActorPool poolOfSlot(uint8_t slot) { return slot < kProjectileFirst ? ActorPool::Animated : ActorPool::Projectiles; }

struct Entry {
    bool any = false;
    uint32_t pass = 0;
    int16_t pos[3] = {};
    uint32_t ownerShape = 0, attack = 0;
};
struct Puppet {
    bool on = false;
    bool initRan = false; // its class ran its first tick (the init: the model, the bones)
    PuppetState target;
    HitBox box;
};

std::array<Entry, 256> gEntries;
std::array<Puppet, 256> gPuppets;
std::set<RoutineId> gShared;
std::vector<PuppetHit> gHits;
uint32_t gPass = 0;

RoutineId classOf(const ActorHeader& a, ActorPool pool) { return actorHandler(a, pool).id(); }

void learn(RoutineId cls) {
    if (cls != RoutineId::None && !netLocalOnly(cls)) gShared.insert(cls);
}

void fill(PuppetState& s, const Actor& a) {
    s.hostSlot = a.slot;
    s.typeWord = uint32_t(a.type) | uint32_t(a.subtype) << 8 | uint32_t(a.variant) << 16 | uint32_t(a.spawnByte) << 24;
    s.paramsWord = a.paramsWord();
    s.loopFlags = a.loopFlags;
    s.pos[0] = a.pos.x;
    s.pos[1] = a.pos.y;
    s.pos[2] = a.pos.z;
    s.rotation[0] = a.rotation.x;
    s.rotation[1] = a.rotation.y;
    s.rotation[2] = a.rotation.z;
    s.hitPoints = a.hitPoints;
    s.maxHitPoints = a.maxHitPoints;
}

// the puppet's tick: the host's pose, the animation toward its key, its hit entry, a local attack's hit reported
void puppetTick(ActorHeader& h) {
    Puppet& p = gPuppets[h.slot];
    const PuppetState& t = p.target;
    Actor& a = static_cast<Actor&>(h);
    a.savePreviousPose();
    a.pos.x = t.pos[0];
    a.pos.y = t.pos[1];
    a.pos.z = t.pos[2];
    a.rotation.x = t.rotation[0];
    a.rotation.y = t.rotation[1];
    a.rotation.z = t.rotation[2];
    a.hitPoints = t.hitPoints;
    a.maxHitPoints = t.maxHitPoints;
    if (h.slot < kProjectileFirst) {
        AnimatedActor& an = animatedPool()[h.slot - kAnimatedFirst];
        an.scale = {t.scale[0], t.scale[1], t.scale[2]};
        an.tint = t.tint;
        an.shownParts = t.shownParts;
        if (an.boneBlocks != 0) { // the class's init gave it its model and bones
            an.anim = t.anim;
            if (an.anim != an.prevAnim || std::abs(int(an.keyIndex) - int(t.keyIndex)) > 2)
                animSetPose(an, an.anim, t.keyIndex);
            else
                animStepPose(an);
            an.prevAnim = an.anim;
        }
    }
    if (p.box.result & kHitAttackByPlayer)
        gHits.push_back({a.at, p.box.result, {p.box.contact.x, p.box.contact.y, p.box.contact.z}});
    p.box.result = 0;
    if ((t.hitShape & kHitRadiusMask) != 0) {
        p.box.previousX = a.prevPos.x.whole;
        p.box.previousZ = a.prevPos.z.whole;
        hitListSubmit(t.hitPos, p.box, hitOwner(a.slot) | (t.hitOwnerFlags & ~kHitSlotMask) | t.hitShape, t.hitAttack);
    }
}

} // namespace

void noteHitEntry(uint8_t slot, const int16_t pos[3], uint32_t ownerShape, uint32_t attack) {
    if (!sharedPoolSlot(slot)) return;
    Entry& e = gEntries[slot];
    e.any = true;
    e.pass = gPass;
    e.pos[0] = pos[0];
    e.pos[1] = pos[1];
    e.pos[2] = pos[2];
    e.ownerShape = ownerShape;
    e.attack = attack;
    if (netSharedEntry(ownerShape, attack)) learn(classOf(slotHeader(slot), poolOfSlot(slot)));
}

void noteHitPassEnded() { ++gPass; }

bool netSharedClass(RoutineId cls) { return gShared.contains(cls); }

std::vector<PuppetState> captureWorld() {
    std::vector<PuppetState> out;
    for (int slot = kAnimatedFirst; slot < kProjectileEnd; ++slot) {
        if (!sharedPoolSlot(uint8_t(slot))) continue;
        const ActorHeader& h = slotHeader(uint8_t(slot));
        const ActorPool pool = poolOfSlot(uint8_t(slot));
        const RoutineId cls = classOf(h, pool);
        if (cls == RoutineId::None || !netSharedClass(cls)) continue;
        PuppetState s;
        s.pool = slot < kProjectileFirst ? kAnimatedFirst : kProjectileFirst;
        s.routine = uint16_t(cls);
        fill(s, static_cast<const Actor&>(h));
        if (pool == ActorPool::Animated) {
            const AnimatedActor& an = animatedPool()[slot - kAnimatedFirst];
            s.anim = an.anim;
            s.keyIndex = an.keyIndex;
            s.shownParts = an.shownParts;
            s.tint = an.tint;
            s.scale[0] = an.scale.x;
            s.scale[1] = an.scale.y;
            s.scale[2] = an.scale.z;
        }
        const Entry& e = gEntries[size_t(slot)];
        if (e.any && e.pass + 1 == gPass) {
            s.hitShape = uint16_t(e.ownerShape);
            s.hitPos[0] = e.pos[0];
            s.hitPos[1] = e.pos[1];
            s.hitPos[2] = e.pos[2];
            s.hitOwnerFlags = e.ownerShape & kHitOwnerMask & ~kHitSlotMask;
            s.hitAttack = e.attack;
        }
        out.push_back(s);
    }
    return out;
}

uint32_t puppetSpawn(const PuppetState& s) {
    const uint32_t address = s.pool == kAnimatedFirst ? allocActor20() : allocActor40(s.loopFlags);
    if (address == 0) return 0;
    ActorHeader& h = actorHeaderAt(address);
    h.flags = kActorLive;
    h.setTypeWord(s.typeWord);
    h.setParamsWord(s.paramsWord);
    h.loopFlags = s.loopFlags;
    h.state = 0;
    Actor& a = static_cast<Actor&>(h);
    a.pos.x = s.pos[0];
    a.pos.y = s.pos[1];
    a.pos.z = s.pos[2];
    a.prevPos = a.pos;
    a.rotation.x = s.rotation[0];
    a.rotation.y = s.rotation[1];
    a.rotation.z = s.rotation[2];
    a.prevRotation = a.rotation;
    Puppet& p = gPuppets[h.slot];
    p = Puppet{};
    p.on = true;
    p.target = s;
    learn(RoutineId(s.routine));
    return address;
}

void puppetApply(uint32_t actor, const PuppetState& s) {
    Puppet& p = gPuppets[actorHeaderAt(actor).slot];
    if (p.on) p.target = s;
}

void puppetFree(uint32_t actor) {
    ActorHeader& h = actorHeaderAt(actor);
    Puppet& p = gPuppets[h.slot];
    if (!p.on) return;
    p = Puppet{};
    freeActor(h);
}

void puppetsReset() {
    for (int slot = kAnimatedFirst; slot < kProjectileEnd; ++slot)
        if (gPuppets[size_t(slot)].on) puppetFree(slotHeader(uint8_t(slot)).at);
    gHits.clear();
}

bool isPuppetActor(const ActorHeader& a) { return gPuppets[a.slot].on; }

std::vector<PuppetHit> takePuppetHits() { return std::exchange(gHits, {}); }

bool netPoolBefore(const PlayerEnv& env, ActorHeader& actor, ActorPool pool, RoutineId cls) {
    (void)env;
    if (!netWorldHooks().mirror || !sharedPoolSlot(actor.slot)) return false;
    if (pool != ActorPool::Animated && pool != ActorPool::Projectiles) return false;
    if (isPuppetActor(actor)) {
        Puppet& p = gPuppets[actor.slot];
        if (!p.initRan) { // its class's first tick: the init (the model, the bones)
            p.initRan = true;
            return false;
        }
        puppetTick(actor);
        return true;
    }
    if (netSharedClass(cls)) { // the client's own copy of a shared class: the host's is shown instead
        freeActor(actor);
        return true;
    }
    return false;
}

void netPoolAfter(ActorHeader& actor, ActorPool pool, RoutineId cls) {
    if (!netWorldHooks().mirror || isPuppetActor(actor) || !sharedPoolSlot(actor.slot)) return;
    if (pool != ActorPool::Animated && pool != ActorPool::Projectiles) return;
    if (netSharedClass(cls) && actor.live()) freeActor(actor); // learned during its own tick
}

} // namespace od::port
