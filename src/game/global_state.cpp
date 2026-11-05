// What a session's processes share (see global_state.hpp).
#include "game/global_state.hpp"

#include <algorithm>
#include <array>

#include "game/game_state.hpp"
#include "game/rewards.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

constexpr size_t kFlagBytes = kStoryFlagBytes + kExtraFlagBytes;
constexpr size_t kSaveZennyAt = 0x200;

// the flags that say what this process is doing, not what happened in the game
constexpr uint32_t kShopAsked = 0x6ff, kRestoreOnLeave = 0x6f1, kFieldMenusHeld = 0x6f2, kInputBackOnLeave = 0x247;
constexpr uint32_t kLocalSystemFirst = 0x700, kLocalSystemEnd = 0x710;
constexpr uint32_t kStageFirst = 0x680, kStageEnd = 0x750;

using Bytes = std::array<uint8_t, kFlagBytes>;

// The bits of each flag byte a scope holds
struct Masks {
    Bytes shared{}, stage{};
    Masks() {
        for (uint32_t n = 0; n < kFlagBytes * 8; ++n) {
            const FlagScope s = flagScope(n);
            if (s == FlagScope::Shared) shared[n >> 3] |= uint8_t(1u << (n & 7));
            if (s == FlagScope::Stage) stage[n >> 3] |= uint8_t(1u << (n & 7));
        }
    }
};
const Masks& masks() {
    static const Masks m;
    return m;
}

uint8_t flagByte(size_t i) { return storyFlagByte(uint32_t(i) << 3); }

// the values that belong to the session: the story, the balances, the message scripts' counters, the entries collected
constexpr size_t kValueBytes = 2 + 2 + 2 + 2 + 8 + 1 + 0x20 + 4;
using Values = std::array<uint8_t, kValueBytes>;
template <class F> void eachValue(F&& f) {
    GameState& g = gameState();
    size_t at = 0;
    const auto byte = [&](auto& field) {
        uint8_t v = uint8_t(field);
        f(at++, v);
        field = std::remove_reference_t<decltype(field)>(v);
    };
    const auto word = [&](auto& field) {
        uint8_t lo = uint8_t(uint16_t(field)), hi = uint8_t(uint16_t(field) >> 8);
        f(at++, lo);
        f(at++, hi);
        field = std::remove_reference_t<decltype(field)>(uint16_t(lo | hi << 8));
    };
    byte(g.story);
    byte(g.nextStory);
    word(g.balance);
    word(g.toolKitBalance);
    byte(g.balanceTier);
    byte(g.toolKitTier);
    for (size_t i = 0; i < g.dockCounters.size(); ++i) byte(g.dockCounters[i]);
    byte(g.countedDock);
    for (size_t i = 0; i < g.entryLog.size(); ++i) byte(g.entryLog[i]);
    word(g.scriptCounters[0]);
    word(g.scriptCounters[1]);
}
Values valuesNow() {
    Values v{};
    eachValue([&](size_t at, uint8_t& b) { v[at] = b; });
    return v;
}

struct Watch {
    Bytes flags{};
    Values values{};
    int32_t zenny = 0;
};
Watch& watch() {
    static Watch w;
    return w;
}

std::optional<GlobalChange> g_starting;

void put16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(uint8_t(v));
    out.push_back(uint8_t(v >> 8));
}
void put32(std::vector<uint8_t>& out, int32_t v) {
    put16(out, uint16_t(uint32_t(v)));
    put16(out, uint16_t(uint32_t(v) >> 16));
}

void mergeBits(std::map<uint16_t, GlobalChange::Bits>& held, const GlobalChange::Bits& b) {
    GlobalChange::Bits& h = held[b.byte];
    h.byte = b.byte;
    h.set = uint8_t((h.set & ~b.clear) | b.set);
    h.clear = uint8_t((h.clear & ~b.set) | b.clear);
}

} // namespace

FlagScope flagScope(uint32_t n) {
    if (n == kFlagStoryMenuOpen || n == kFlagGameOverRescue || n == kInputBackOnLeave || n == kFlagSaveMenuAsked || n == kShopAsked ||
        n == kRestoreOnLeave || n == kFieldMenusHeld)
        return FlagScope::Local;
    if (n >= kLocalSystemFirst && n < kLocalSystemEnd) return FlagScope::Local;
    if (n >= kStageFirst && n < kStageEnd) return FlagScope::Stage;
    return FlagScope::Shared;
}

std::vector<uint8_t> encode(const GlobalChange& c) {
    std::vector<uint8_t> out;
    out.push_back(c.stage);
    put16(out, uint16_t(c.flags.size()));
    for (const GlobalChange::Bits& b : c.flags) {
        put16(out, b.byte);
        out.push_back(b.set);
        out.push_back(b.clear);
    }
    out.push_back(uint8_t(c.values.size()));
    for (const GlobalChange::Value& v : c.values) {
        out.push_back(v.at);
        out.push_back(v.value);
    }
    out.push_back(uint8_t((c.zennyDelta ? 1 : 0) | (c.zennyNow ? 2 : 0)));
    if (c.zennyDelta) put32(out, *c.zennyDelta);
    if (c.zennyNow) put32(out, *c.zennyNow);
    return out;
}

std::optional<GlobalChange> decodeGlobalChange(std::span<const uint8_t> b) {
    GlobalChange c;
    size_t at = 0;
    const auto left = [&] { return b.size() - at; };
    if (left() < 3) return std::nullopt;
    c.stage = b[at++];
    const size_t flags = size_t(b[at] | b[at + 1] << 8);
    at += 2;
    if (left() < flags * 4 + 1) return std::nullopt;
    for (size_t i = 0; i < flags; ++i, at += 4) {
        const uint16_t byte = uint16_t(b[at] | b[at + 1] << 8);
        if (byte >= kFlagBytes) return std::nullopt;
        c.flags.push_back({byte, b[at + 2], b[at + 3]});
    }
    const size_t values = b[at++];
    if (left() < values * 2 + 1) return std::nullopt;
    for (size_t i = 0; i < values; ++i, at += 2) {
        if (b[at] >= kValueBytes) return std::nullopt;
        c.values.push_back({b[at], b[at + 1]});
    }
    const uint8_t zenny = b[at++];
    const auto word = [&] {
        const uint32_t v = uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
        at += 4;
        return int32_t(v);
    };
    if (left() != size_t((zenny & 1) ? 4 : 0) + size_t((zenny & 2) ? 4 : 0)) return std::nullopt;
    if (zenny & 1) c.zennyDelta = word();
    if (zenny & 2) c.zennyNow = word();
    return c;
}

void globalWatchRebase() {
    Watch& w = watch();
    for (size_t i = 0; i < kFlagBytes; ++i) w.flags[i] = flagByte(i);
    w.values = valuesNow();
    w.zenny = gameState().zenny;
}

GlobalChange globalWatchTake(uint8_t stage) {
    Watch& w = watch();
    GlobalChange c;
    c.stage = stage;
    const Masks& m = masks();
    for (size_t i = 0; i < kFlagBytes; ++i) {
        const uint8_t now = flagByte(i), was = w.flags[i];
        if (now == was) continue;
        w.flags[i] = now;
        const uint8_t travels = uint8_t(m.shared[i] | m.stage[i]);
        const uint8_t set = uint8_t(now & ~was & travels), clear = uint8_t(was & ~now & travels);
        if (set || clear) c.flags.push_back({uint16_t(i), set, clear});
    }
    const Values values = valuesNow();
    for (size_t i = 0; i < kValueBytes; ++i)
        if (values[i] != w.values[i]) c.values.push_back({uint8_t(i), values[i]});
    w.values = values;
    const int32_t zenny = gameState().zenny;
    if (zenny != w.zenny) c.zennyDelta = zenny - w.zenny;
    w.zenny = zenny;
    return c;
}

void globalWatchApply(const GlobalChange& c) {
    Watch& w = watch();
    const Masks& m = masks();
    const bool myStage = c.stage == gameState().stage;
    for (const GlobalChange::Bits& b : c.flags) {
        const uint8_t may = uint8_t(m.shared[b.byte] | (myStage ? m.stage[b.byte] : 0));
        const uint8_t set = uint8_t(b.set & may), clear = uint8_t(b.clear & may);
        uint8_t& byte = storyFlagByte(uint32_t(b.byte) << 3);
        byte = uint8_t((byte | set) & ~clear);
        w.flags[b.byte] = uint8_t((w.flags[b.byte] | set) & ~clear);
    }
    if (!c.values.empty()) {
        eachValue([&](size_t at, uint8_t& v) {
            for (const GlobalChange::Value& x : c.values)
                if (x.at == at) v = x.value;
        });
        for (const GlobalChange::Value& x : c.values) w.values[x.at] = x.value;
    }
    if (c.zennyNow) {
        // what this process added and has not told yet stays on top of it
        gameState().zenny = *c.zennyNow + (gameState().zenny - w.zenny);
        w.zenny = *c.zennyNow;
    }
}

void setStartingGlobalState(const GlobalChange& c) { g_starting = c; }

void applyStartingGlobalState() {
    if (!g_starting) return;
    globalWatchRebase();
    globalWatchApply(*g_starting);
    g_starting.reset();
}

GlobalMaster::Out GlobalMaster::merge(const GlobalChange& c) {
    Out out;
    out.shared.stage = out.stage.stage = c.stage;
    const Masks& m = masks();
    for (const GlobalChange::Bits& b : c.flags) {
        const GlobalChange::Bits shared{b.byte, uint8_t(b.set & m.shared[b.byte]), uint8_t(b.clear & m.shared[b.byte])};
        const GlobalChange::Bits stage{b.byte, uint8_t(b.set & m.stage[b.byte]), uint8_t(b.clear & m.stage[b.byte])};
        if (shared.set || shared.clear) {
            mergeBits(shared_, shared);
            out.shared.flags.push_back(shared);
        }
        if (stage.set || stage.clear) {
            mergeBits(stageBits_[c.stage], stage);
            out.stage.flags.push_back(stage);
        }
    }
    for (const GlobalChange::Value& v : c.values) {
        values_[v.at] = v.value;
        out.shared.values.push_back(v);
    }
    if (c.zennyDelta) {
        zenny_ = std::clamp(zenny_ + *c.zennyDelta, 0, kZennyMax);
        out.zenny = zenny_;
    }
    return out;
}

GlobalChange GlobalMaster::all(uint8_t stage) const {
    GlobalChange c;
    c.stage = stage;
    for (const auto& [byte, bits] : shared_) c.flags.push_back(bits);
    if (const auto it = stageBits_.find(stage); it != stageBits_.end())
        for (const auto& [byte, bits] : it->second) c.flags.push_back(bits);
    for (const auto& [at, value] : values_) c.values.push_back({at, value});
    c.zennyNow = zenny_;
    return c;
}

int32_t saveZenny(std::span<const uint8_t> save) {
    if (save.size() < kSaveZennyAt + 4) return 0;
    return int32_t(uint32_t(save[kSaveZennyAt]) | uint32_t(save[kSaveZennyAt + 1]) << 8 | uint32_t(save[kSaveZennyAt + 2]) << 16 |
                   uint32_t(save[kSaveZennyAt + 3]) << 24);
}

} // namespace od::port
