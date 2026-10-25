// Sparse image of dash2.exe's address space for ports that are first transcribed function by function and only later
// refactored into clean structs. Regions: the exe's data sections, PS1 RAM segments of .DAT files (PS1 address +
// 0x630000, see docs/file_inventory.md), and blocks of game state (player struct 0xa71d20, pad 0xa6bec0, ...).
// Reading an unmapped address throws with the address, so a missing global shows up immediately. The refactor's
// native state moves the systems' state out of the image one system at a time; the test-only translator
// (tests/translator) writes it into the image's layout for the goldens and fences the bytes it owns while the game
// runs (setFences / setFenced): a game access to a fenced byte is a NativeStateAccess error, never a silent read of a
// stale copy.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "engine/file_data.hpp"

namespace od {

// a game access, through the image, to a byte the native state owns (GameMem::fence)
class NativeStateAccess : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class GameMem {
public:
    static constexpr uint32_t kPsxBase = 0x630000; // PS1 RAM 0 in the PC process (FUN_0040eb90)

    // Maps (or overwrites, if already mapped) [base, base + bytes.size()).
    void map(uint32_t base, std::span<const uint8_t> bytes);
    void mapZero(uint32_t base, uint32_t size);
    // .data/.rdata of the user's dash2.exe at their virtual addresses (image base 0x400000)
    void mapExeData(const std::filesystem::path& exe);
    // type-0 segments of a .DAT at their PS1 load addresses
    void mapDatPsx(std::span<const uint8_t> datFile);

    bool mapped(uint32_t a, uint32_t n = 1) const { return find(a, n) != nullptr; }
    // the block holding [a, a + n), shared (FileDataBlock::share: it follows the image's writes), and a's offset in it
    std::pair<FileDataBlock, size_t> sharedAt(uint32_t a, uint32_t n) const {
        const Region* r = find(a, n);
        if (!r) throw std::out_of_range("GameMem: no region holds the shared range");
        return {r->bytes.share(), size_t(a - r->base)};
    }
    // the host's accesses (a test's loads and compares, the app's file loads): never fenced
    void read(uint32_t a, void* out, uint32_t n) const { std::memcpy(out, at(a, n), n); }
    void write(uint32_t a, const void* in, uint32_t n) {
        uint8_t* p = at(a, n);
        std::memcpy(p, in, n);
        files_.write(a, in, n);
    }
    // the game's accesses (translator/game_memory's views): the fence checked (checkFence)
    void gameRead(uint32_t a, void* out, uint32_t n) const {
        checkFence(a, n);
        const uint8_t* p = at(a, n);
        std::memcpy(out, p, n);
    }
    void gameWrite(uint32_t a, const void* in, uint32_t n) {
        checkFence(a, n);
        uint8_t* p = at(a, n);
        std::memcpy(p, in, n);
        files_.write(a, in, n);
    }

    uint8_t u8(uint32_t a) const { return *at(a, 1); }
    int8_t s8(uint32_t a) const { return int8_t(u8(a)); }
    uint16_t u16(uint32_t a) const { uint16_t v; read(a, &v, 2); return v; }
    int16_t s16(uint32_t a) const { return int16_t(u16(a)); }
    uint32_t u32(uint32_t a) const { uint32_t v; read(a, &v, 4); return v; }
    int32_t s32(uint32_t a) const { return int32_t(u32(a)); }
    void set8(uint32_t a, uint32_t v) { const uint8_t x = uint8_t(v); write(a, &x, 1); }
    void set16(uint32_t a, uint32_t v) { const uint16_t x = uint16_t(v); write(a, &x, 2); }
    void set32(uint32_t a, uint32_t v) { write(a, &v, 4); }

    std::span<const uint8_t> view(uint32_t a, uint32_t n) const { return {at(a, n), n}; }

    // Resolve a file pointer once at the compatibility boundary. Consumers retain
    // a native reader; scalar reads never go through the emulated image.
    const LoadedFileData& loadedFiles() const { return files_; }
    LoadedFileData& mutableLoadedFiles() { return files_; }
    bool fileDataMapped(uint32_t address, uint32_t bytes = 1) const { return files_.mapped(address, bytes); }
    FileDataReader fileData(uint32_t address, uint32_t bytes = 1) const {
        return files_.reader(address, bytes);
    }
    // A snapshot of the bindings with shared backing blocks. No image bytes are
    // copied; the first write detaches its block while its readers follow that
    // binding. Used by the legacy ghost inspection API.
    GameMem sharedSnapshot() const { return *this; }

    // the ranges [begin, end) owned by the native state (they replace the previous ones): while fenced, a game access
    // to them (checkFence: the game's views, game/game_memory) throws NativeStateAccess; the test's own reads and
    // writes of the image are not checked
    void setFences(std::vector<std::pair<uint32_t, uint32_t>> ranges);
    bool hasFences() const { return !fences_.empty(); }
    void checkFence(uint32_t a, uint32_t n) const {
        if (fenced_) checkFenced(a, n);
    }
    void setFenced(bool on) { fenced_ = on; }
    bool fenced() const { return fenced_; }

private:
    struct Region {
        uint32_t base;
        FileDataBlock bytes;
        // exe sections under exe_coverage only: bytes [0, fileBytes) come from the file; written[i] once anything
        // wrote byte i (a read before that is a dependency on the exe's contents)
        uint32_t fileBytes = 0;
        std::vector<uint8_t> written;
    };
    LoadedFileData files_; // native loaded payloads; regions_ is the compatibility image
    std::vector<Region> regions_;
    std::vector<std::pair<uint32_t, uint32_t>> fences_; // sorted, disjoint [begin, end)
    bool fenced_ = false;

    void checkFenced(uint32_t a, uint32_t n) const;

    const Region* find(uint32_t a, uint32_t n) const {
        for (const Region& r : regions_)
            if (a >= r.base && uint64_t(a) + n <= uint64_t(r.base) + r.bytes.size()) return &r;
        return nullptr;
    }
    const Region& locate(uint32_t a, uint32_t n) const {
        const Region* r = find(a, n);
        if (!r) {
            char msg[64];
            std::snprintf(msg, sizeof msg, "GameMem: unmapped 0x%08x (+%u)", a, n);
            throw std::out_of_range(msg);
        }
        return *r;
    }
    static void noteRead(const Region& r, uint32_t a, uint32_t n);
    static void noteWrite(Region& r, uint32_t a, uint32_t n) {
        std::fill_n(r.written.begin() + (a - r.base), n, uint8_t(1));
    }
    uint8_t* at(uint32_t a, uint32_t n) { // writes
        Region& r = const_cast<Region&>(locate(a, n));
        if (!r.written.empty()) noteWrite(r, a, n);
        return r.bytes.mutableData() + (a - r.base);
    }
    const uint8_t* at(uint32_t a, uint32_t n) const { // reads
        const Region& r = locate(a, n);
        if (!r.written.empty()) noteRead(r, a, n);
        return r.bytes.data() + (a - r.base);
    }
};

} // namespace od
