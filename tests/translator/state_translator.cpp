// The test-only translator (state_translator.hpp): the systems' layouts run as loads, stores and fences.
#include "translator/state_translator.hpp"

#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>

#include "translator/layout.hpp"
#include "translator/actor_layout.hpp"
#include "translator/block_parts_layout.hpp"

namespace od::test {

namespace {

// every native system's layout
void allLayouts(LayoutVisitor& v) {
    gameGlobalsLayout(v);
    stageStateLayout(v);
    blockPartsLayout(v);
    cameraLayout(v);
    geometryLayout(v);
    audioLayout(v);
    displayLayout(v);
    worldLayout(v);
    encounterLayout(v);
    hudLayout(v);
    inputLayout(v);
    messageLayout(v);
    filesLayout(v);
    boneBlocksLayout(v);
    actorClassesLayout(v);
    actorSystemLayout(v);
    playerGlobalsLayout(v);
    contentGlobalsLayout(v);
    stateGlobalsLayout(v);
    patchedDataLayout(v);
    textLayout(v);
    weatherDrawLayout(v);
}

class Load final : public LayoutVisitor {
public:
    explicit Load(const GameMem& m) : m_(m) {}
    bool loading() const override { return true; }
    // A held state can belong to the previous occupant until classState replaces
    // it. Preserve uninterpretable words for that replacement's lazy load.
    bool strictSlots() const override { return false; }
    bool interprets(uint32_t a, size_t n) const override { return !generic || !classOwns(a, n); }
    void bytes(uint32_t address, void* p, size_t n) override {
        if (interprets(address, n) && m_.mapped(address, uint32_t(n))) {
            m_.read(address, p, uint32_t(n));
            return;
        }
        // A snapshot may map only part of a native record.
        for (size_t i = 0; i < n; ++i)
            if (interprets(address + uint32_t(i), 1) && m_.mapped(address + uint32_t(i)))
                m_.read(address + uint32_t(i), static_cast<uint8_t*>(p) + i, 1);
    }
    void pointer(uint32_t a, void*& p, const std::type_info& t) override {
        if (!m_.mapped(a, 4)) return;
        resolveNativePointer(a, m_.u32(a), t, p);
    }
    bool generic = false;

private:
    const GameMem& m_;
};

class Store final : public LayoutVisitor {
public:
    explicit Store(GameMem& m) : m_(m) {}
    bool storing() const override { return true; }
    void bytes(uint32_t address, void* p, size_t n) override {
        if (m_.mapped(address, uint32_t(n))) m_.write(address, p, uint32_t(n));
        else for (size_t i = 0; i < n; ++i)
            if (m_.mapped(address + uint32_t(i)))
                m_.write(address + uint32_t(i), static_cast<uint8_t*>(p) + i, 1);
    }
    void pointer(uint32_t a, void*& p, const std::type_info& t) override {
        uint32_t word = nativePointerWord(a, p, t);
        bytes(a, &word, 4);
    }

private:
    GameMem& m_;
};

class Ranges final : public LayoutVisitor {
public:
    void bytes(uint32_t address, void*, size_t n) override { ranges.push_back({address, address + uint32_t(n)}); }
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
};

// Class layouts include work areas whose addresses change with bone allocations.
std::vector<std::pair<uint32_t, uint32_t>> nativeRanges() {
        Ranges r;
        actorPoolsLayout(r);
        allLayouts(r);
        return r.ranges;
}

} // namespace

void refreshNativeFences(GameMem& image) {
    image.setFences(nativeRanges());
    image.setFenced(true);
}

RoutineId LayoutVisitor::routineOf(uint32_t pointer) {
    if (pointer == 0) return RoutineId::None;
    const RoutineRef r = stateRoutine(pointer);
    if (!r.mapped()) {
        char b[96];
        std::snprintf(b, sizeof b, "translator: the function pointer 0x%08x in the image names no routine", pointer);
        throw std::runtime_error(b);
    }
    return r.id();
}

void loadNativeState(GameMem& image) {
    prepareActorLoad(image);
    Load load(image);
    load.generic = true;
    actorPoolsLayout(load);
    load.generic = false;
    allLayouts(load);
    finishActorLoad();
    refreshNativeFences(image);
}

void storeNativeState(GameMem& image) {
    prepareActorStore(image);
    Store store(image);
    actorPoolsLayout(store);
    allLayouts(store);
}

void loadPlayerAt(const GameMem& image, uint32_t address, od::port::Player& player) {
    Load load(image);
    layoutValue(load, address, player);
}

void storePlayerAt(GameMem& image, uint32_t address, od::port::Player& player) {
    Store store(image);
    layoutValue(store, address, player);
}

} // namespace od::test
