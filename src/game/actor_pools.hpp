// The actor pools of dash2.exe as arrays of native slots, the actor
// system's globals (live / pending-free masks, the update loops' cursors, the hit list, the bone blocks), the mapping
// between slot ids, addresses and slots, the allocation of slots and bone blocks, and the pending frees.
//
//   slot class  base      count x size   slot
//   0x00        0xa71d20  1 x 0x500      Player (game/player/player_state)
//   0x10        0xa6ab80  16 x 0x100     ShotActor
//   0x20        0xa6e3a0  32 x 0x16c     AnimatedActor (the field updates slots 0..23; 24..31 are the sub-screen's)
//   0x40        0xa78420  32 x 0xdc      ProjectileActor
//   0x60        0xa67860  64 x 0xcc      StaticActor
//   0xa0        0xa6c6e0  64 x 0x28      EffectActor
//   0xe0        0xa6bfa0  32 x 0x20      ControllerActor
//   bone blocks 0xa72660  32 x 0xb0      BoneBlock (4 bones of an animated actor each)
//
// Every pool is an ActorPoolOf<slot>: `animatedPool()[i]` is slot i (it lives for the life of the program),
// `.at(address)` the slot at an exe address (the identity the states still in the exe's layout store), `.index(slot)`
// its slot index. Routines take the slot (`AnimatedActor&`); a class update, whose signature keeps the slot's address
// (game/actors ActorUpdateFn), starts with `animatedActorAt(address)`. A routine holding a slot of a wider kind gets
// the narrower one through the typed lookups (`asAnimated(actor)`).
#pragma once

#include <memory>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include "game/actor_state.hpp"
#include "game/player/player_state.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port {

struct Player;

constexpr uint32_t kShotPool = 0xa6ab80, kAnimatedPool = 0xa6e3a0, kProjectilePool = 0xa78420,
                   kStaticPool = 0xa67860, kEffectPool = 0xa6c6e0, kControllerPool = 0xa6bfa0;
constexpr uint32_t kHitListCapacity = 0x60;
constexpr uint32_t kBoneBlocks = 0xa72660;                    // 32 bone blocks x 0xb0
constexpr uint32_t kAnimatedFieldSlots = 24;                  // pool 0x20 slots of the field (the update loop's)

// the slot classes (the high bits of a slot id; exe 0x5e0f70)
constexpr uint32_t kPlayerClass = 0x00, kShotClass = 0x10, kAnimatedClass = 0x20, kProjectileClass = 0x40,
                   kStaticClass = 0x60, kEffectClass = 0xa0, kControllerClass = 0xe0;
// the slot sizes of the pools
constexpr uint32_t kAnimatedSlotSize = 0x16c, kStaticSlotSize = 0xcc, kEffectSlotSize = 0x28, kControllerSlotSize = 0x20;
// The address of a slot id's slot by the exe's own arithmetic (the pool + (slot - its class) * the slot size, no check:
// a class that keeps a slot id of its pool computes it so; a stale or foreign id gives an address outside the pool, as
// in the exe)
constexpr uint32_t animatedPoolSlot(int32_t slot) {
    return uint32_t(int32_t(kAnimatedPool) + (slot - int32_t(kAnimatedClass)) * int32_t(kAnimatedSlotSize));
}
constexpr uint32_t staticPoolSlot(int32_t slot) {
    return uint32_t(int32_t(kStaticPool) + (slot - int32_t(kStaticClass)) * int32_t(kStaticSlotSize));
}
constexpr uint32_t effectPoolSlot(int32_t slot) {
    return uint32_t(int32_t(kEffectPool) + (slot - int32_t(kEffectClass)) * int32_t(kEffectSlotSize));
}
constexpr uint32_t controllerPoolSlot(int32_t slot) {
    return uint32_t(int32_t(kControllerPool) + (slot - int32_t(kControllerClass)) * int32_t(kControllerSlotSize));
}

// an address that is no slot of the pool (or of any pool, poolBase 0): a ResolutionError (never a port gap)
[[noreturn]] void notAnActorSlot(uint32_t address, uint32_t poolBase);
// the position of the model-bearing slot whose +0x10 is at `address` (a parameter word that names an actor's position)
Fixed16Vec3& actorPositionAt(uint32_t address);
// the Actor layout asked of a slot without it (an effect / controller slot, actorLayoutAt): a refactor error naming the
// slot, never a port gap
class ActorLayoutError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// One pool: N slots of kind V that live for the life of the program (an allocation resets some of their fields, never
// the slot); slot i's address in the exe is Base + i * Stride (the identity the states still in the exe's layout
// store).
template <class V, size_t N, uint32_t Base, uint32_t Stride> class ActorPoolOf {
public:
    ActorPoolOf() {
        for (size_t i = 0; i < N; ++i) slots_[i].at = Base + uint32_t(i) * Stride;
    }
    ActorPoolOf(const ActorPoolOf&) = delete;
    ActorPoolOf& operator=(const ActorPoolOf&) = delete;
    // every slot as built (its native state and class state dropped; a host's fresh game state)
    void reset() {
        for (size_t i = 0; i < N; ++i) {
            std::destroy_at(&slots_[i]);
            std::construct_at(&slots_[i]);
            slots_[i].at = Base + uint32_t(i) * Stride;
        }
    }

    V& operator[](size_t i) { return slots_[i]; }
    static constexpr size_t size() { return N; }
    static constexpr uint32_t base() { return Base; }
    static constexpr uint32_t stride() { return Stride; }
    static constexpr bool contains(uint32_t address) {
        return address >= Base && address < Base + N * Stride && (address - Base) % Stride == 0;
    }
    // the slot index of a slot of this pool (a slot of any other pool: notAnActorSlot)
    static uint32_t index(const ActorHeader& slot) {
        if (!contains(slot.at)) notAnActorSlot(slot.at, Base);
        return (slot.at - Base) / Stride;
    }
    // the slot at `address` (notAnActorSlot when it is no slot of this pool)
    V& at(uint32_t address) {
        if (!contains(address)) notAnActorSlot(address, Base);
        return slots_[(address - Base) / Stride];
    }
    // the slot `slot` is, as this pool's kind (a slot of another pool: notAnActorSlot)
    V& of(const ActorHeader& slot) { return slots_[index(slot)]; }
    // the slot after `slot` in the pool: the exe lets some classes keep bytes past their own slot, which land in the
    // next slot's header (the game relies on that layout); past the pool's last slot an ActorLayoutError naming `who`
    V& following(const ActorHeader& slot, const char* who) {
        const uint32_t i = index(slot);
        if (i + 1 >= N) throw ActorLayoutError(std::string(who) + ": bytes past the pool's last slot (the exe's memory after it)");
        return slots_[i + 1];
    }

private:
    std::array<V, N> slots_;
};

using ShotPool = ActorPoolOf<ShotActor, 16, kShotPool, 0x100>;
using AnimatedPool = ActorPoolOf<AnimatedActor, 32, kAnimatedPool, 0x16c>;
using ProjectilePool = ActorPoolOf<ProjectileActor, 32, kProjectilePool, 0xdc>;
using StaticPool = ActorPoolOf<StaticActor, 64, kStaticPool, 0xcc>;
using EffectPool = ActorPoolOf<EffectActor, 64, kEffectPool, 0x28>;
using ControllerPool = ActorPoolOf<ControllerActor, 32, kControllerPool, 0x20>;

// The bone blocks of the animated pool (given to the actors by ActorSystemState::boneBlocksUsed)
class BoneBlockPool {
public:
    BoneBlockPool() {
        for (size_t i = 0; i < blocks_.size(); ++i) blocks_[i].at = kBoneBlocks + uint32_t(i) * 0xb0;
    }
    BoneBlockPool(const BoneBlockPool&) = delete;
    BoneBlockPool& operator=(const BoneBlockPool&) = delete;
    void reset() {
        for (size_t i = 0; i < blocks_.size(); ++i) {
            blocks_[i] = BoneBlock{};
            blocks_[i].at = kBoneBlocks + uint32_t(i) * 0xb0;
        }
    }

    BoneBlock& operator[](size_t i) { return blocks_[i]; }
    static constexpr size_t size() { return 32; }
    static constexpr uint32_t stride() { return 0xb0; }
    static constexpr bool contains(uint32_t address) {
        return address >= kBoneBlocks && address < kBoneBlocks + 32 * 0xb0 && (address - kBoneBlocks) % 0xb0 == 0;
    }
    BoneBlock& at(uint32_t address) {
        if (!contains(address)) notAnActorSlot(address, kBoneBlocks);
        return blocks_[(address - kBoneBlocks) / 0xb0];
    }

private:
    std::array<BoneBlock, 32> blocks_{};
};

// the pools (each built on first use, for the life of the program)
ShotPool& shotPool();
AnimatedPool& animatedPool();
ProjectilePool& projectilePool();
StaticPool& staticPool();
EffectPool& effectPool();
ControllerPool& controllerPool();
BoneBlockPool& boneBlockPool();
// every pool's slots and the bone blocks as built (a host starting from a fresh game state)
void resetActorPools();
// the projectile slot whose hit box (+0xbc) `box` is, or null: the exe writes a hit's contact-motion output to the
// dword after it (+0xc4, ProjectileActor::debrisFlags)
ProjectileActor* projectileOfHitBox(const HitBoxHead& box);

// the view of the slot at an address (a class update's first line in Phase 1)
inline ShotActor& shotActorAt(uint32_t address) { return shotPool().at(address); }
inline AnimatedActor& animatedActorAt(uint32_t address) { return animatedPool().at(address); }
inline ProjectileActor& projectileActorAt(uint32_t address) { return projectilePool().at(address); }
inline StaticActor& staticActorAt(uint32_t address) { return staticPool().at(address); }
inline EffectActor& effectActorAt(uint32_t address) { return effectPool().at(address); }
inline ControllerActor& controllerActorAt(uint32_t address) { return controllerPool().at(address); }
// the model-bearing slot (or the player) at an address
Actor& actorAt(uint32_t address);
// the mesh slot (shot, projectile, static) at an address
MeshActor& meshActorAt(uint32_t address);
// the shared head of any slot (or of the player) at an address
ActorHeader& actorHeaderAt(uint32_t address);

// ---- typed lookups: the narrower view of a slot held as a wider one (const stays const) ----------------------------
AnimatedActor& asAnimated(ActorHeader& actor);
const AnimatedActor& asAnimated(const ActorHeader& actor);
StaticActor& asStatic(ActorHeader& actor);
const StaticActor& asStatic(const ActorHeader& actor);
ShotActor& asShot(ActorHeader& actor);
ProjectileActor& asProjectile(ActorHeader& actor);
EffectActor& asEffect(ActorHeader& actor);
ControllerActor& asController(ActorHeader& actor);
// the player's view (the slot must be the player's)
Player& asPlayer(ActorHeader& actor);
const Player& asPlayer(const ActorHeader& actor);
// The Actor layout at a slot whatever its pool: the exe reads a queued solid / mover, a rider's platform or the actor
// a movement routine (game/actor_movement) is given with the Actor offsets without checking the pool. The native
// slots of the effect and controller pools have no Actor fields: such a read is an ActorLayoutError naming the slot
// (an exe overlap the port models by name where a recording shows it); any other address (0 included) is a failed
// lookup (notAnActorSlot).
Actor& actorLayoutAt(uint32_t address);
// ... of a slot (an effect / controller slot: an ActorLayoutError)
Actor& actorLayoutOf(ActorHeader& slot);
const Actor& actorLayoutOf(const ActorHeader& slot);

// ---- slot identity words -------------------------------------------------------------------------------------------
// The word the exe keeps for a slot where a value names an actor (a spawn parameter word, a word past a slot): the
// slot's address in the exe (its pool's base + index * stride; the player's). Never read through: the slot it names is
// found by slotByIdentityWord.
inline uint32_t slotIdentityWord(const ActorHeader& slot) { return slot.at; }
// ... of a referenced slot (none: 0)
template <class V> uint32_t slotIdentityWord(const ActorRef<V>& ref) { return ref ? slotIdentityWord(*ref) : 0; }
// the slot named by an identity word as kind V (0: nullptr; a word naming no slot of V's pools: ResolutionError)
template <class V> V* slotByIdentityWord(uint32_t word) { return viewAt(std::type_identity<V>{}, word); }
// the exe address of a field of a slot (its offset in the exe's layout), a value the exe publishes (a global's word,
// a spawn parameter): never read through
inline uint32_t slotFieldWord(const ActorHeader& slot, uint32_t offset) { return slotIdentityWord(slot) + offset; }
// the offsets those words name: the position, the class block of the animated / the mesh pools
constexpr uint32_t kSlotPositionOffset = 0x10, kAnimatedClassBlockOffset = 0x14c, kStaticClassBlockOffset = 0xbc;
// a reference set to the slot an identity word names (a spawn parameter word, an allocator's result)
template <class V> void pointAtWord(ActorRef<V>& ref, uint32_t word) { ref = slotByIdentityWord<V>(word); }

// ---- slot ids -------------------------------------------------------------------------------------------------------
// exe 0x5e0f70: the pool class of a slot id (kPlayerClass ... kControllerClass)
uint32_t actorSlotClass(uint32_t slot);
// exe 0x5e0fa0: the address of the actor of a slot id (the player for class 0). The exe's arithmetic for any value: a
// slot read as a signed byte (a static's slot 0x80.. sign-extended) gives an address outside the pools, as in the exe.
uint32_t actorBySlot(uint32_t slot);

// ---- the actor system's globals -----------------------------------------------------------------------------------
// One hit-list entry (exe 0x5e4120 writes them; game/hit_list's pass reads them; the words: game/hit_list.hpp)
struct HitEntry {
    ShortVec4 pos;          // the centre (world); its 4th lane is the shape word (the exe copies the 8 bytes whole
                            // into the lock-on candidates' points)
    uint32_t attack = 0;    // damage | flags ORed into the other's box | group immunities
    uint32_t owner = 0;     // slot << 24 | group << 21 | flags
    int16_t previousX = 0;
    int16_t previousZ = 0;
    HitBoxHead* box = nullptr; // the owner's result box
    HitBox* contactBox = nullptr; // ... when it has the contact output (a HitBox): the box the contact goes to

    // the radius / 4, the height level, the capsule and contact bits (pos's 4th lane)
    int16_t shape() const { return pos.spare; }
    void setShape(int16_t v) { pos.spare = v; }
};

struct ActorSystemState {
    // the pools' live masks (bit 31 - slot index; the static and effect pools have two words)
    uint32_t shotsLive = 0;
    uint32_t animatedLive = 0;
    uint32_t projectilesLive = 0;
    StateArray<uint32_t, 2> staticsLive{};
    StateArray<uint32_t, 2> effectsLive{};
    uint32_t controllersLive = 0;
    // the pending frees (exe 0x5e15f0 moves them into the live masks after the game mode's tick)
    uint32_t shotsFreed = 0;
    uint32_t animatedFreed = 0;
    uint32_t projectilesFreed = 0;
    StateArray<uint32_t, 2> staticsFreed{};
    StateArray<uint32_t, 2> effectsFreed{};
    uint32_t controllersFreed = 0;
    // the update loops' current actor (its slot's exe address, its identity; a loop past its pool points past it)
    uint32_t animatedCursor = 0;
    uint32_t shotCursor = 0;
    uint32_t projectileCursor = 0;
    uint32_t staticCursor = 0;
    uint32_t effectCursor = 0;
    uint32_t controllerCursor = 0;
    // the allocation mask of the bone blocks (bit 31 - block index; boneBlockPool())
    uint32_t boneBlocksUsed = 0;
    // the hit list (filled by the updates, read and emptied by game/hit_list's pass)
    uint32_t hitCount = 0;
    StateArray<HitEntry, kHitListCapacity> hitList{};
};
// the actor system's globals (one set, for the life of the program)
ActorSystemState& actorSystem();

// ---- allocation (the first free slot of the live mask from bit 31, marked live; the slot's run flags kActorRun |
// kActorShown, its loop flags `loopFlags`, its type / state / parameters and its contact fields cleared). Each
// returns the slot's address, 0 when the pool is full (Phase 1: the callers still work on addresses). ----------------
uint32_t allocActor10();                   // exe 0x5e1220: pool 0x10 (loop flags 0)
uint32_t allocActor20();                   // exe 0x5e1160: pool 0x20 (loop flags kActorStageClass)
// exe 0x5e11c0: pool 0x20's sub-screen slots (24..31: the status sub-screen's model) as allocActor20, the contact bytes
// cleared as one dword (the exe's store)
uint32_t allocSubScreenActor();
uint32_t allocActor40(uint8_t loopFlags);  // exe 0x5e1290 (exe 0x5e1280 = kActorStageClass)
uint32_t allocActor60(uint8_t loopFlags);  // exe 0x5e1310 (exe 0x5e12f0 = kActorStageClass, 0x5e1300 = 0)
uint32_t allocActorA0(uint8_t loopFlags);  // exe 0x5e13c0 (exe 0x5e13a0 = kActorStageClass, 0x5e13b0 = 0)
uint32_t allocActorE0();                   // exe 0x5e1440: pool 0xe0 (loop flags kActorStageClass)
// exe 0x5e16c0(actor, n): n free bone blocks (from bit 31) given to the actor (its boneBlocks bits, lastBoneBlock = the
// last one), each bone's colour neutral. false when they run out (the blocks taken so far stay taken).
bool allocBoneBlocks(AnimatedActor& actor, uint32_t n);

// ---- pending frees (the slot's bit in its pool's pending mask, byte 0 cleared) -------------------------------------
void freeActor(ShotActor& actor);       // exe 0x5e14d0
void freeActor(AnimatedActor& actor);   // exe 0x5e1490 (also releases its bone blocks)
void freeActor(ProjectileActor& actor); // exe 0x5e1500
void freeActor(StaticActor& actor);     // exe 0x5e1540
void freeActor(EffectActor& actor);     // exe 0x5e1580
void freeActor(ControllerActor& actor); // exe 0x5e15c0
// exe 0x511120: the free of the actor's pool (by its slot class; the player and unknown classes: nothing)
void freeActor(ActorHeader& actor);
// exe 0x511120 for an actor's address (the content callers' form until they hold views): 0 frees nothing
void actorFree(uint32_t address);
// exe 0x5e15f0 (after the game mode's tick): the pending frees moved out of the pools' live masks, cleared
void applyActorFrees();

} // namespace od::port
