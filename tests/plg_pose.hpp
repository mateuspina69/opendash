// Shared by the player pose tests (test_player_snapshot, test_ghost): the PLG2/PLG3 pose golden layout
// (see plg.hpp) and the comparison of a posed actor's outputs (+0x74..+0x94, +0x2b0..size) with the game's bytes.
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "plg.hpp"
#include "translator/gamemem.hpp"

// bytes of the actor struct the game never writes (pads); not compared
inline bool isPad(uint32_t o) {
    if (o == 0x86 || o == 0x87) return true;
    if (o >= 0x2b0 && o < 0x2b0 + 15 * 0x20) {
        const uint32_t k = (o - 0x2b0) % 0x20;
        return k == 0x12 || k == 0x13;
    }
    if (o >= 0x490) return (o - 0x490) % 8 >= 6; // bone point pad (never written)
    return false;
}

// pose outputs of the actor at `actor` equal the game's bytes (pads skipped); reports the first few diffs
inline bool sameOutputs(const od::GameMem& m, uint32_t actor, const uint8_t* after, uint32_t size, uint32_t tick) {
    static int reported = 0;
    bool same = true;
    auto check = [&](uint32_t lo, uint32_t hi) {
        for (uint32_t o = lo; o < hi; ++o) {
            if (isPad(o) || m.u8(actor + o) == after[o]) continue;
            if (reported < 16) {
                std::fprintf(stderr, "tick %u +0x%03x: want %02x got %02x\n", tick, o, after[o], m.u8(actor + o));
                ++reported;
            }
            same = false;
        }
    };
    check(0x74, 0x94);
    check(0x2b0, size);
    return same;
}

struct PlgRange { uint32_t addr, size; };

// A loaded pose golden: record i = file.data() + p + i * rec; layout: tick(4) + 2 pad, `size` bytes before / input /
// after (after = record + 6 + 3 * size), then the range bytes (record + 6 + 4 * size).
struct PlgPose {
    std::vector<uint8_t> file;
    uint32_t count = 0, size = 0;
    std::vector<PlgRange> ranges;
    size_t p = 0, rec = 0;

    bool load(const std::filesystem::path& path) {
        file = od::readFile(path);
        if (!plgMagic(file)) return false;
        count = od::rd32(file.data() + 4);
        size = od::rd32(file.data() + 8);
        const uint32_t nr = od::rd32(file.data() + 12);
        ranges.clear();
        p = 16;
        for (uint32_t i = 0; i < nr; ++i, p += 8) ranges.push_back({od::rd32(file.data() + p), od::rd32(file.data() + p + 4)});
        rec = 6 + 4 * size_t(size);
        for (const PlgRange& r : ranges) rec += r.size;
        rec += plgSkipPass(file, p);
        return true;
    }
    const uint8_t* record(uint32_t i) const { return file.data() + p + size_t(i) * rec; }
    const uint8_t* after(const uint8_t* r) const { return r + 6 + 3 * size_t(size); }
    // writes the tick's range bytes and the actor bytes after the tick into `m` at kPlayer
    void apply(od::GameMem& m, const uint8_t* r, uint32_t player) const {
        const uint8_t* q = r + 6 + 4 * size_t(size);
        for (const PlgRange& rg : ranges) {
            m.write(rg.addr, q, rg.size);
            q += rg.size;
        }
        m.write(player, after(r), size);
    }
};
