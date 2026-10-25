// The actor pools, their resolvers and typed lookups, the slot ids, the allocation of slots and bone blocks and the
// pending frees (see actor_pools.hpp).
#include "game/actor_pools.hpp"

#include <cstdio>

#include "game/actor_state.hpp"
#include "game/resolution_error.hpp"
#include "game/player/player_state.hpp"

namespace od::port {

void notAnActorSlot(uint32_t address, uint32_t poolBase) {
    char b[96];
    std::snprintf(b, sizeof b, "actor pools: %06x is no slot of the pool %06x", address, poolBase);
    throw ResolutionError(b);
}

ShotPool& shotPool() {
    static ShotPool p;
    return p;
}
AnimatedPool& animatedPool() {
    static AnimatedPool p;
    return p;
}
ProjectilePool& projectilePool() {
    static ProjectilePool p;
    return p;
}
ProjectileActor* projectileOfHitBox(const HitBoxHead& box) {
    ProjectilePool& pool = projectilePool();
    for (uint32_t i = 0; i < 32; ++i)
        if (&pool[i].hitBox == &box) return &pool[i];
    return nullptr;
}
StaticPool& staticPool() {
    static StaticPool p;
    return p;
}
EffectPool& effectPool() {
    static EffectPool p;
    return p;
}
ControllerPool& controllerPool() {
    static ControllerPool p;
    return p;
}
BoneBlockPool& boneBlockPool() {
    static BoneBlockPool p;
    return p;
}

ActorSystemState& actorSystem() {
    static ActorSystemState s;
    return s;
}

// ---- the class states ---------------------------------------------------------------------------------------------
namespace {
ClassStateObserver* observer = nullptr;
} // namespace
ClassStateObserver* classStateObserver() { return observer; }
void resetActorPools() {
    shotPool().reset();
    animatedPool().reset();
    projectilePool().reset();
    staticPool().reset();
    effectPool().reset();
    controllerPool().reset();
    boneBlockPool().reset();
}
void setClassStateObserver(ClassStateObserver* o) { observer = o; }

void classStateMismatch(const ActorHeader& slot, const char* held, const char* asked) {
    char b[320];
    std::snprintf(b, sizeof b, "actor %06x (type %02x): class state %s asked for while it holds %s", slot.at,
                  unsigned(slot.type), asked, held);
    throw ClassStateMismatch(b);
}

void Actor::copyRootWordsFrom(const BoneBlock& block, size_t k) {
    uint32_t words[8];
    for (size_t i = 0; i < 8; ++i) words[i] = block.matrixWord(k, i);
    setRootWords(words);
}

// ---- resolvers ------------------------------------------------------------------------------------------------------
namespace {
// the model-bearing slot (or the player) at an address, nullptr when it is none of them
Actor* modelActorAt(uint32_t address) {
    if (address == kPlayer) return &player();
    if (ShotPool::contains(address)) return &shotPool().at(address);
    if (AnimatedPool::contains(address)) return &animatedPool().at(address);
    if (ProjectilePool::contains(address)) return &projectilePool().at(address);
    if (StaticPool::contains(address)) return &staticPool().at(address);
    return nullptr;
}
} // namespace

Fixed16Vec3& actorPositionAt(uint32_t address) { return actorAt(address - kSlotPositionOffset).pos; }

Actor& actorAt(uint32_t address) {
    if (Actor* a = modelActorAt(address)) return *a;
    notAnActorSlot(address, 0);
}

MeshActor& meshActorAt(uint32_t address) {
    if (ShotPool::contains(address)) return shotPool().at(address);
    if (ProjectilePool::contains(address)) return projectilePool().at(address);
    if (StaticPool::contains(address)) return staticPool().at(address);
    notAnActorSlot(address, 0);
}

ActorHeader& actorHeaderAt(uint32_t address) {
    if (Actor* a = modelActorAt(address)) return *a;
    if (EffectPool::contains(address)) return effectPool().at(address);
    if (ControllerPool::contains(address)) return controllerPool().at(address);
    notAnActorSlot(address, 0);
}

ActorHeader* viewAt(std::type_identity<ActorHeader>, uint32_t address) {
    return address == 0 ? nullptr : &actorHeaderAt(address);
}
Actor* viewAt(std::type_identity<Actor>, uint32_t address) { return address == 0 ? nullptr : &actorAt(address); }
AnimatedActor* viewAt(std::type_identity<AnimatedActor>, uint32_t address) {
    return address == 0 ? nullptr : &animatedPool().at(address);
}
StaticActor* viewAt(std::type_identity<StaticActor>, uint32_t address) {
    return address == 0 ? nullptr : &staticPool().at(address);
}
ShotActor* viewAt(std::type_identity<ShotActor>, uint32_t address) {
    return address == 0 ? nullptr : &shotPool().at(address);
}
ProjectileActor* viewAt(std::type_identity<ProjectileActor>, uint32_t address) {
    return address == 0 ? nullptr : &projectilePool().at(address);
}
EffectActor* viewAt(std::type_identity<EffectActor>, uint32_t address) {
    return address == 0 ? nullptr : &effectPool().at(address);
}
ControllerActor* viewAt(std::type_identity<ControllerActor>, uint32_t address) {
    return address == 0 ? nullptr : &controllerPool().at(address);
}
BoneBlock* viewAt(std::type_identity<BoneBlock>, uint32_t address) {
    return address == 0 ? nullptr : &boneBlockPool().at(address);
}

// ---- typed lookups --------------------------------------------------------------------------------------------------
AnimatedActor& asAnimated(ActorHeader& actor) { return animatedPool().of(actor); }
const AnimatedActor& asAnimated(const ActorHeader& actor) { return animatedPool().of(actor); }
StaticActor& asStatic(ActorHeader& actor) { return staticPool().of(actor); }
const StaticActor& asStatic(const ActorHeader& actor) { return staticPool().of(actor); }
ShotActor& asShot(ActorHeader& actor) { return shotPool().of(actor); }
ProjectileActor& asProjectile(ActorHeader& actor) { return projectilePool().of(actor); }
EffectActor& asEffect(ActorHeader& actor) { return effectPool().of(actor); }
ControllerActor& asController(ActorHeader& actor) { return controllerPool().of(actor); }
Player& asPlayer(ActorHeader& actor) {
    if (actor.at != kPlayer) notAnActorSlot(actor.at, kPlayer);
    return player();
}
const Player& asPlayer(const ActorHeader& actor) {
    if (actor.at != kPlayer) notAnActorSlot(actor.at, kPlayer);
    return player();
}
Actor& actorLayoutAt(uint32_t address) {
    if (Actor* a = modelActorAt(address)) return *a;
    if (EffectPool::contains(address) || ControllerPool::contains(address)) {
        char b[96];
        std::snprintf(b, sizeof b, "actor pools: the Actor layout read at the effect / controller slot %06x", address);
        throw ActorLayoutError(b);
    }
    notAnActorSlot(address, 0);
}

Actor& actorLayoutOf(ActorHeader& slot) { return actorLayoutAt(slot.at); }
const Actor& actorLayoutOf(const ActorHeader& slot) { return actorLayoutAt(slot.at); }

// ---- slot ids -------------------------------------------------------------------------------------------------------
uint32_t actorSlotClass(uint32_t slot) {
    if (slot < kAnimatedClass) return slot & kShotClass;
    uint32_t c = slot & 0xe0;
    if (c == 0x80) return kStaticClass;      // slots 0x80..0x9f: the static pool's second half
    if (c == 0xc0) c = kEffectClass;         // slots 0xc0..0xdf: the effect pool's second half
    return c;
}

uint32_t actorBySlot(uint32_t slot) {
    switch (actorSlotClass(slot)) {
    case kShotClass: return kShotPool + (slot - kShotClass) * ShotPool::stride();
    case kAnimatedClass: return kAnimatedPool + (slot - kAnimatedClass) * AnimatedPool::stride();
    case kProjectileClass: return kProjectilePool + (slot - kProjectileClass) * ProjectilePool::stride();
    case kStaticClass: return kStaticPool + (slot - kStaticClass) * StaticPool::stride();
    case kEffectClass: return kEffectPool + (slot - kEffectClass) * EffectPool::stride();
    case kControllerClass: return kControllerPool + (slot - kControllerClass) * ControllerPool::stride();
    default: return kPlayer; // class 0
    }
}

// ---- allocation -----------------------------------------------------------------------------------------------------
namespace {
// the bit of slot `index` in its mask word (bit 31 - index)
constexpr uint32_t slotBit(uint32_t index) { return 0x80000000u >> (index & 0x1f); }

// the first clear bit (from bit 31) of a live mask word among its first n, set; -1 when they are all set
int takeSlot(uint32_t& mask, uint32_t n, uint32_t first = 0) {
    const uint32_t m = mask;
    for (uint32_t b = first; b < n; ++b)
        if (int32_t(m << b) >= 0) {
            mask = m | slotBit(b);
            return int(b);
        }
    return -1;
}

// the head of a new slot: run flags, loop flags, type / state / parameters cleared
void startSlot(ActorHeader& actor, uint8_t loopFlags) {
    actor.beginLifetime();
    actor.runFlags = kActorRun | kActorShown;
    actor.loopFlags = loopFlags;
    actor.clearIdentity();
}
// ... and the contact fields of a model-bearing slot: airborne / platform bytes, platform, interaction box and byte
void clearContact(Actor& actor) {
    actor.airborne = 0;
    actor.wasAirborne = 0;
    actor.onPlatform = 0;
    actor.wasOnPlatform = 0;
    actor.platform = nullptr;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
}
} // namespace

uint32_t allocActor10() {
    const int i = takeSlot(actorSystem().shotsLive, ShotPool::size());
    if (i < 0) return 0;
    ShotActor& actor = shotPool()[size_t(i)];
    startSlot(actor, 0);
    clearContact(actor);
    return actor.at;
}

uint32_t allocActor20() {
    const int i = takeSlot(actorSystem().animatedLive, kAnimatedFieldSlots);
    if (i < 0) return 0;
    AnimatedActor& actor = animatedPool()[size_t(i)];
    startSlot(actor, kActorStageClass);
    clearContact(actor);
    return actor.at;
}

uint32_t allocSubScreenActor() {
    const int i = takeSlot(actorSystem().animatedLive, AnimatedPool::size(), kAnimatedFieldSlots);
    if (i < 0) return 0;
    AnimatedActor& actor = animatedPool()[size_t(i)];
    // the slot id (set at boot for all the slots; the model binding takes the class from it): the host initialises the
    // field's slots only
    if (actor.slot == 0) actor.slot = uint8_t(kAnimatedClass + uint32_t(i));
    startSlot(actor, kActorStageClass);
    actor.clearContactWord();
    actor.platform = nullptr;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    return actor.at;
}

uint32_t allocActor40(uint8_t loopFlags) {
    const int i = takeSlot(actorSystem().projectilesLive, ProjectilePool::size());
    if (i < 0) return 0;
    ProjectileActor& actor = projectilePool()[size_t(i)];
    startSlot(actor, loopFlags);
    clearContact(actor);
    return actor.at;
}

uint32_t allocActor60(uint8_t loopFlags) {
    for (uint32_t w = 0; w < 2; ++w) {
        auto&& live = actorSystem().staticsLive[w];
        const int b = takeSlot(live, 0x20);
        if (b < 0) continue;
        StaticActor& actor = staticPool()[w * 32 + uint32_t(b)];
        startSlot(actor, loopFlags);
        clearContact(actor);
        return actor.at;
    }
    return 0;
}

uint32_t allocActorA0(uint8_t loopFlags) {
    for (uint32_t w = 0; w < 2; ++w) {
        auto&& live = actorSystem().effectsLive[w];
        const int b = takeSlot(live, 0x20);
        if (b < 0) continue;
        EffectActor& actor = effectPool()[w * 32 + uint32_t(b)];
        startSlot(actor, loopFlags);
        return actor.at;
    }
    return 0;
}

uint32_t allocActorE0() {
    const int i = takeSlot(actorSystem().controllersLive, ControllerPool::size());
    if (i < 0) return 0;
    ControllerActor& actor = controllerPool()[size_t(i)];
    startSlot(actor, kActorStageClass);
    return actor.at;
}

bool allocBoneBlocks(AnimatedActor& actor, uint32_t n) {
    if (n == 0) return false;
    ActorSystemState& s = actorSystem();
    for (uint32_t i = 0; i < BoneBlockPool::size(); ++i) {
        if (int32_t(s.boneBlocksUsed << i) < 0) continue;
        const uint32_t bit = slotBit(i);
        --n;
        s.boneBlocksUsed |= bit;
        actor.boneBlocks |= bit;
        BoneBlock& block = boneBlockPool()[i];
        for (size_t k = 0; k < kBonesPerBlock; ++k) block.colour(k) = kTintNeutral;
        if (n == 0) {
            actor.lastBoneBlock = &block;
            return true;
        }
    }
    return false;
}

// ---- pending frees --------------------------------------------------------------------------------------------------
void freeActor(ShotActor& actor) {
    actorSystem().shotsFreed |= slotBit(ShotPool::index(actor));
    actor.flags = 0;
}
void freeActor(AnimatedActor& actor) {
    ActorSystemState& s = actorSystem();
    s.animatedFreed |= slotBit(AnimatedPool::index(actor));
    actor.flags = 0;
    s.boneBlocksUsed &= ~uint32_t(actor.boneBlocks); // exe 0x5e1760
}
void freeActor(ProjectileActor& actor) {
    actorSystem().projectilesFreed |= slotBit(ProjectilePool::index(actor));
    actor.flags = 0;
}
void freeActor(StaticActor& actor) {
    const uint32_t i = StaticPool::index(actor);
    actorSystem().staticsFreed[i >> 5] |= slotBit(i);
    actor.flags = 0;
}
void freeActor(EffectActor& actor) {
    const uint32_t i = EffectPool::index(actor);
    actorSystem().effectsFreed[i >> 5] |= slotBit(i);
    actor.flags = 0;
}
void freeActor(ControllerActor& actor) {
    actorSystem().controllersFreed |= slotBit(ControllerPool::index(actor));
    actor.flags = 0;
}

void freeActor(ActorHeader& actor) {
    switch (actorSlotClass(actor.slot)) {
    case kAnimatedClass: freeActor(animatedPool().of(actor)); return;
    case kProjectileClass: freeActor(projectilePool().of(actor)); return;
    case kStaticClass: freeActor(staticPool().of(actor)); return;
    case kEffectClass: freeActor(effectPool().of(actor)); return;
    case kControllerClass: freeActor(controllerPool().of(actor)); return;
    default: return; // the player, the shots: exe 0x511120 has no case for them
    }
}

void actorFree(uint32_t address) {
    if (address == 0) return;
    freeActor(actorHeaderAt(address));
}

void applyActorFrees() {
    ActorSystemState& s = actorSystem();
    s.animatedLive ^= uint32_t(s.animatedFreed);
    s.shotsLive ^= uint32_t(s.shotsFreed);
    s.projectilesLive ^= uint32_t(s.projectilesFreed);
    s.staticsLive[0] ^= uint32_t(s.staticsFreed[0]);
    s.staticsLive[1] ^= uint32_t(s.staticsFreed[1]);
    s.effectsLive[0] ^= uint32_t(s.effectsFreed[0]);
    s.effectsLive[1] ^= uint32_t(s.effectsFreed[1]);
    s.animatedFreed = 0;
    s.shotsFreed = 0;
    s.projectilesFreed = 0;
    s.staticsFreed[0] = 0;
    s.staticsFreed[1] = 0;
    s.effectsFreed[0] = 0;
    s.effectsFreed[1] = 0;
    s.controllersLive ^= uint32_t(s.controllersFreed);
    s.controllersFreed = 0;
}

} // namespace od::port
