// Test-only snapshot retention, class lifetime hooks, and native pointer identities.
// The retained image owns its bytes: no observer outlives a caller's GameMem.
#include "translator/actor_layout.hpp"
#include "translator/state_translator.hpp"
#include "content/calbania/calbania_rooms.hpp"
#include "game/event_state.hpp"
#include "game/guardian_reaverbot.hpp"
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <typeindex>
#include <vector>

namespace od::test {
using namespace od::port;
namespace {
struct Binding { uint32_t address; void* pointer; std::type_index type; };
std::vector<Binding> bindings;
std::map<uint32_t, uint32_t> pointerWords;
// the object each pointer field was resolved to (the word it was resolved from: pointerWords): a pointer still naming
// it stores that word, also once the object is no longer registered (a retired class state's member)
std::map<uint32_t, const void*> resolvedObjects;
std::set<uint32_t> unresolved;
std::vector<std::pair<uint32_t, uint32_t>> classRanges;
GameMem retained;
bool haveSnapshot = false;
// The slots whose held class state owns none of their bytes: a previous occupant's since the slot's allocation, or
// one whose slot the loaded image gives another class than the last store's (the image's slot was reallocated). Their
// bytes are the retained ones under the slot's shared fields (loaded from the retained bytes at the allocation:
// lifetimeBegins, or from the image); the state takes them when the slot's class asks for it (created / resumed), so
// the shared fields the game wrote meanwhile are in it, as in the exe.
std::set<const ActorHeader*> retired;
// the class of each slot holding a state, at the last store (its type, sub-type and stage-class bit)
std::map<const ActorHeader*, uint32_t> slotClass;
constexpr uint32_t kSlotLoopFlags = 3, kSlotType = 4, kSlotSubtype = 5, kSlotHeadBytes = 6; // the header's bytes
uint32_t classOfSlot(uint8_t type, uint8_t subtype, uint8_t loopFlags) {
    return uint32_t(type) | uint32_t(subtype) << 8 | uint32_t(loopFlags & kActorStageClass) << 16;
}

class Register final : public LayoutVisitor {
public:
    void bytes(uint32_t, void*, size_t) override {}
    void object(uint32_t a, void* p, const std::type_info& t) override { bindings.push_back({a, p, t}); }
};
class Coverage final : public LayoutVisitor {
public:
    void bytes(uint32_t a, void*, size_t n) override { classRanges.emplace_back(a, a + uint32_t(n)); }
};
class SnapshotStore final : public LayoutVisitor {
public:
    bool storing() const override { return true; }
    void bytes(uint32_t a, void* p, size_t n) override {
        if (retained.mapped(a, uint32_t(n))) retained.write(a, p, uint32_t(n));
    }
    void pointer(uint32_t a, void*& p, const std::type_info& t) override {
        uint32_t word = nativePointerWord(a, p, t);
        bytes(a, &word, 4);
    }
};
class SnapshotLoad final : public LayoutVisitor {
public:
    bool loading() const override { return true; }
    // a retained word no slot names (another occupant's bytes) is kept for the field, as the exe keeps it
    bool strictSlots() const override { return false; }
    void bytes(uint32_t a, void* p, size_t n) override {
        if (retained.mapped(a, uint32_t(n))) retained.read(a, p, uint32_t(n));
    }
    void pointer(uint32_t a, void*& p, const std::type_info& t) override {
        uint32_t word = 0;
        bytes(a, &word, 4);
        resolveNativePointer(a, word, t, p);
    }
};
// Repair forward pointers after registering a newly constructed class, without
// loading the snapshot over fields the game has already updated this frame.
class Resolve final : public LayoutVisitor {
public:
    void bytes(uint32_t, void*, size_t) override {}
    void pointer(uint32_t a, void*& p, const std::type_info& t) override {
        if (pointerWords.contains(a)) resolveNativePointer(a, pointerWords.at(a), t, p);
    }
};
void registerAll() {
    bindings.clear();
    Register r;
    // first, so that every later binding of an address wins: the retired states (a slot's pointer can still name a
    // member of its previous occupant's state, its box: the exe keeps the word)
    for (const ActorHeader* slot : retired)
        if (ActorClassState* held = slot->heldClassState()) classLayout(r, slot->at, *held);
    actorPoolsLayout(r);
    boneBlocksLayout(r); // before the classes: a class's work record laid over a block is the later binding
    r.bind(0xa5a040, eventStartPoint().point); // the event's speaker point (a pointer the event state keeps)
    patchedDataLayout(r); // the patched records (a slot's box can be one: the falling rocks' shared box)
    // the globals the actors' state points at: the guardian's weak point (a hit entry's box), the Glyde base's escape
    // ticks (a controller's counter)
    r.field(0xa59e10, guardianWeakPointBox());
    r.bind(0xa5a6c8, od::content::glydeBaseRoomState().escapeTicks);
    actorClassesLayout(r);
    classRanges.clear();
    Coverage c;
    actorClassesLayout(c);
    std::sort(classRanges.begin(), classRanges.end());
    std::vector<std::pair<uint32_t, uint32_t>> merged;
    for (auto r : classRanges) {
        if (!merged.empty() && r.first <= merged.back().second)
            merged.back().second = std::max(merged.back().second, r.second);
        else merged.push_back(r);
    }
    classRanges = std::move(merged);
}
void resolveAll() {
    Resolve r;
    actorPoolsLayout(r);
    actorClassesLayout(r);
    const auto repairBox = [](Actor& actor) {
        auto& box = actor.collisionBox;
        const uint32_t field = actor.at + 0x58;
        const uint32_t word = box.own() && pointerWords.contains(field) ? pointerWords.at(field) : box.recordAddress();
        void* own = nullptr;
        if (resolveNativePointer(field, word, typeid(BoxExtents), own) && own)
            box.pointTo(*static_cast<BoxExtents*>(own));
    };
    for (size_t i = 0; i < shotPool().size(); ++i) repairBox(shotPool()[i]);
    for (size_t i = 0; i < animatedPool().size(); ++i) repairBox(animatedPool()[i]);
    for (size_t i = 0; i < projectilePool().size(); ++i) repairBox(projectilePool()[i]);
    for (size_t i = 0; i < staticPool().size(); ++i) repairBox(staticPool()[i]);
    repairBox(player());
    // Hit entries have two native pointers for the exe's one box word.
    for (auto& entry : actorSystem().hitList) {
        const uint32_t i = uint32_t(&entry - actorSystem().hitList.data());
        void* box = entry.box;
        r.pointer(0xa7a1a0 + i * 0x18 + 0x14, box, typeid(HitBoxHead));
        entry.box = static_cast<HitBoxHead*>(box);
        entry.contactBox = nativeContactBox(entry.box);
    }
}
std::pair<uint32_t, uint32_t> clearRange(const ActorHeader& slot) {
    if (ShotPool::contains(slot.at)) return {slot.at + 0xbc, slot.at + 0xdc};
    if (AnimatedPool::contains(slot.at)) return {slot.at + 0x14c, slot.at + 0x16c};
    if (ProjectilePool::contains(slot.at)) return {slot.at + 0xbc, slot.at + 0xdc};
    if (StaticPool::contains(slot.at)) return {slot.at + 0xbc, slot.at + 0xcc};
    if (EffectPool::contains(slot.at)) return {slot.at + 0x18, slot.at + 0x28};
    if (ControllerPool::contains(slot.at)) return {slot.at + 0x18, slot.at + 0x20};
    throw std::runtime_error("translator: class block of an unknown pool");
}
// the class state of a slot loaded from the retained bytes (the previous state's stored first when it is still the
// slot's own: a class switch within one lifetime)
void loadClassState(ActorHeader& slot, ActorClassState* previous, ActorClassState& state) {
    const bool retiredState = retired.erase(&slot) != 0;
    // Keep everything the previous occupant left, including bytes outside
    // the class-clear interval. Shared members are written first.
    SnapshotStore store;
    actorPoolsLayout(store);
    actorClassesLayout(store, &slot);
    if (previous && !retiredState) {
        classLayout(store, slot.at, *previous);
        // the shared fields the previous state's bytes lay over take them (it no longer owns them)
        SnapshotLoad shared;
        slotLayout(shared, slot);
    }
    actorSystemLayout(store); // retain incoming hit-box pointer identities, including live entries
    // Remove registry entries for the previous state before it is destroyed.
    registerAll();
    SnapshotLoad load;
    classLayout(load, slot.at, state);
    // Area addresses have now loaded; register their contents, then fix links.
    registerAll();
    resolveAll();
    if (od::detail::boundGameMemory) refreshNativeFences(gameMemory());
}
class Observer final : public ClassStateObserver {
public:
    void created(ActorHeader& slot, ActorClassState* previous, ActorClassState& state) override {
        if (!haveSnapshot) return;
        loadClassState(slot, previous, state);
    }
    // the held state taken again by the slot's class: its members are the slot's bytes as they are now
    void resumed(ActorHeader& slot, ActorClassState& state) override {
        if (!haveSnapshot || !retired.contains(&slot)) return;
        loadClassState(slot, nullptr, state);
    }
    // a new occupant's allocation while the class state is current: the retained bytes take the class's (the exe's
    // slot keeps what the class wrote; the next class state made for the slot loads them)
    void lifetimeEnds(ActorHeader& slot, ActorClassState& state) override {
        if (!haveSnapshot) return;
        SnapshotStore store;
        actorPoolsLayout(store);
        classLayout(store, slot.at, state);
    }
    // An allocation: the slot's shared fields take its retained bytes (the previous occupant's, a class's own bytes
    // among them: what the exe's slot holds), then the allocation writes them
    void lifetimeBegins(ActorHeader& slot) override {
        if (!haveSnapshot) return;
        retired.insert(&slot);
        SnapshotLoad load;
        slotLayout(load, slot);
    }
    void blockCleared(ActorHeader& slot) override {
        const auto [first, last] = clearRange(slot);
        clearBytes(first, last);
    }
    void blockBytesCleared(ActorHeader& slot, uint32_t begin, uint32_t end) override {
        clearBytes(slot.at + begin, slot.at + end);
    }
    void clearBytes(uint32_t first, uint32_t last) {
        if (!haveSnapshot) return;
        for (uint32_t a = first; a < last; ++a)
            if (retained.mapped(a)) retained.set8(a, 0);
        for (auto it = pointerWords.lower_bound(first); it != pointerWords.end() && it->first < last;)
            it = pointerWords.erase(it);
        for (auto it = unresolved.lower_bound(first); it != unresolved.end() && *it < last;)
            it = unresolved.erase(it);
    }
};
Observer observer;

template <class Pool> void beginSnapshot(Pool& pool) {
    for (size_t i = 0; i < pool.size(); ++i) pool[i].beginLifetime();
}
}

bool classOwns(uint32_t a, size_t n) {
    const auto it = std::lower_bound(classRanges.begin(), classRanges.end(), a,
        [](const auto& r, uint32_t address) { return r.second <= address; });
    return it != classRanges.end() && uint64_t(it->first) < uint64_t(a) + n;
}
bool resolveNativePointer(uint32_t field, uint32_t word, const std::type_info& type, void*& pointer) {
    pointerWords[field] = word;
    if (word == 0) { pointer = nullptr; unresolved.erase(field); return true; }
    // The last binding is the class's field when it overlays a generic field.
    for (auto i = bindings.rbegin(); i != bindings.rend(); ++i)
        if (i->address == word && i->type == std::type_index(type)) {
            pointer = i->pointer; unresolved.erase(field); resolvedObjects[field] = pointer; return true;
        }
    pointer = nullptr;
    unresolved.insert(field);
    return false; // its owner's class may be instantiated later
}
uint32_t nativePointerWord(uint32_t field, const void* pointer, const std::type_info& type) {
    if (!pointer) {
        if (unresolved.contains(field)) return pointerWords.at(field);
        pointerWords[field] = 0;
        return 0;
    }
    for (auto i = bindings.rbegin(); i != bindings.rend(); ++i)
        if (i->pointer == pointer && i->type == std::type_index(type)) {
            pointerWords[field] = i->address;
            return i->address;
        }
    const auto kept = resolvedObjects.find(field);
    if (kept != resolvedObjects.end() && kept->second == pointer && pointerWords.contains(field))
        return pointerWords.at(field);
    char at[16];
    std::snprintf(at, sizeof at, "0x%x", field);
    throw std::runtime_error(std::string("translator: unregistered native pointer of type ") + type.name() + " (the field at " + at + ")");
}
HitBox* nativeContactBox(HitBoxHead* head) {
    if (!head) return nullptr;
    for (const auto& b : bindings)
        if (b.type == std::type_index(typeid(HitBox)) && static_cast<HitBoxHead*>(static_cast<HitBox*>(b.pointer)) == head)
            return static_cast<HitBox*>(b.pointer);
    return nullptr;
}
// the actor-owned areas of the image the snapshot retains: the pools, the player and the bone blocks
constexpr std::pair<uint32_t, uint32_t> kRetainedAreas[] = {
    {kShotPool, 16 * 0x100}, {kAnimatedPool, 32 * 0x16c}, {kProjectilePool, 32 * 0xdc},
    {kStaticPool, 64 * 0xcc}, {kEffectPool, 64 * 0x28}, {kControllerPool, 32 * 0x20},
    {kPlayer, 0x500}, {kBoneBlocks, 32 * 0xb0}};
void prepareActorLoad(const GameMem& image) {
    // Copy only the actor-owned areas. Unmapped portions remain unmapped. (No observer runs until the snapshot is
    // taken: the lifetimes restarted below store nothing of the previous native state into it.)
    haveSnapshot = false;
    retained = GameMem{};
    for (auto [a, n] : kRetainedAreas) {
        if (image.mapped(a, n)) {
            retained.map(a, image.view(a, n));
            continue;
        }
        // Map contiguous runs, also supporting tests which map part of a slot.
        uint32_t first = a;
        while (first < a + n) {
            while (first < a + n && !image.mapped(first)) ++first;
            uint32_t last = first;
            while (last < a + n && image.mapped(last)) ++last;
            if (last > first) retained.map(first, image.view(first, last - first));
            first = last;
        }
    }
    pointerWords.clear(); resolvedObjects.clear(); unresolved.clear();
    // each held state's ownership by the class the image's slot holds: the class of the last store's slot (the new
    // occupant's for a retired state, the state's own else)
    const auto own = [&image](auto& pool) {
        for (size_t i = 0; i < pool.size(); ++i) {
            const ActorHeader& slot = pool[i];
            const auto stored = slotClass.find(&slot);
            if (!slot.heldClassState() || stored == slotClass.end() || !image.mapped(slot.at, kSlotHeadBytes)) {
                retired.erase(&slot);
                continue;
            }
            const bool same = classOfSlot(image.u8(slot.at + kSlotType), image.u8(slot.at + kSlotSubtype),
                                          image.u8(slot.at + kSlotLoopFlags)) == stored->second;
            if (!same) retired.insert(&slot); // the image's slot holds another class: the state is not its
            // else: the same occupant as at the store (retired: still the new occupant's, its class not asked yet)
        }
    };
    own(shotPool()); own(animatedPool()); own(projectilePool());
    own(staticPool()); own(effectPool()); own(controllerPool());
    slotClass.clear();
    beginSnapshot(shotPool()); beginSnapshot(animatedPool()); beginSnapshot(projectilePool());
    beginSnapshot(staticPool()); beginSnapshot(effectPool()); beginSnapshot(controllerPool());
    haveSnapshot = true;
    setClassStateObserver(&observer);
    registerAll();
}
void prepareActorStore(GameMem& image) {
    registerAll();
    slotClass.clear();
    const auto classes = [](auto& pool) {
        for (size_t i = 0; i < pool.size(); ++i)
            if (pool[i].heldClassState()) slotClass[&pool[i]] = classOfSlot(pool[i].type, pool[i].subtype, pool[i].loopFlags);
    };
    classes(shotPool()); classes(animatedPool()); classes(projectilePool());
    classes(staticPool()); classes(effectPool()); classes(controllerPool());
    // The retained bytes first: what the exe keeps in the areas between their owners (a block clear's zeroes, a
    // previous occupant's bytes no current layout names); the layouts then store every native field over them.
    if (!haveSnapshot) return;
    for (auto [a, n] : kRetainedAreas) {
        if (retained.mapped(a, n) && image.mapped(a, n)) {
            image.write(a, retained.view(a, n).data(), n);
            continue;
        }
        for (uint32_t i = 0; i < n; ++i)
            if (retained.mapped(a + i) && image.mapped(a + i)) image.set8(a + i, retained.u8(a + i));
    }
}
bool heldStateRetired(const ActorHeader& slot) { return retired.contains(&slot); }
void resetNativeActors() {
    haveSnapshot = false;
    retired.clear();
    slotClass.clear();
    pointerWords.clear();
    resolvedObjects.clear();
    unresolved.clear();
    bindings.clear();
    od::port::resetActorPools();
}
void finishActorLoad() { registerAll(); resolveAll(); }
} // namespace od::test
