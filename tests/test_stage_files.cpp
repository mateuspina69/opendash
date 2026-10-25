// The stage DAT file selectors (od::port::stageDatFiles = FUN_005d5cd0's 22 per-stage functions) vs the exe's own
// 0..0x14 x subset of the flags its selector reads x sub 0 / 7, on pristine tables. The selector writes the stage's
// geometry / texture file indices into the u16 tables 0x8d3458 / 0x8d3514, as the exe does.
//   test_stage_files <dash2.exe> <tests/data>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "translator/state_translator.hpp"
#include "data_source_binding.hpp"
#include "translator/gamemem.hpp"
#include "translator/game_memory.hpp"
#include "game/stage_files.hpp"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: test_stage_files <dash2.exe> <tests/data>\n");
        return 2;
    }
    od::GameMem m;
    const od::ScopedGameMemory bound(m);
    m.mapExeData(argv[1]);
    od::bindTestDataSource(argv[1]);
    constexpr uint32_t kGeo = 0x8d3458, kTex = 0x8d3514, kTablesSize = kTex - kGeo + 0x5d * 2;
    std::vector<uint8_t> pristine(kTablesSize);
    for (uint32_t i = 0; i < kTablesSize; ++i) pristine[i] = m.u8(kGeo + i);
    if (!m.mapped(0xa73cc0, 0x100)) m.mapZero(0xa73cc0, 0x100);
    if (!m.mapped(0xa6d191, 4)) m.mapZero(0xa6d000, 0x1000);
    od::test::loadNativeState(m);
    std::ifstream in(std::string(argv[2]) + "/stage_files.txt");
    CHECK(in.good());
    int cases = 0, bad = 0;
    for (std::string line; std::getline(in, line);) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        unsigned stage, story, sub, geo, tex;
        std::string flags;
        ss >> std::hex >> stage >> story >> std::dec >> sub >> flags >> std::hex >> geo >> tex;
        od::test::storeNativeState(m);
        for (uint32_t i = 0; i < kTablesSize; ++i) m.set8(kGeo + i, pristine[i]);
        for (uint32_t i = 0; i < 0x100; ++i) m.set8(0xa73cc0 + i, 0);
        m.set8(0xa6d194, uint8_t(story));
        m.set8(0xa6d191, uint8_t(sub));
        if (flags != "-") {
            std::istringstream fs(flags);
            for (std::string f; std::getline(fs, f, ',');) {
                const uint32_t n = uint32_t(std::stoul(f, nullptr, 16));
                m.set8(0xa73cc0 + (n >> 3), uint8_t(m.u8(0xa73cc0 + (n >> 3)) | (1u << (n & 7))));
            }
        }
        od::test::loadNativeState(m);
        std::string g, t;
        od::port::stageDatFiles(int(stage), g, t);
        od::test::storeNativeState(m);
        const uint16_t gi = m.u16(kGeo + stage * 2), ti = m.u16(kTex + stage * 2);
        ++cases;
        if (gi != geo || ti != tex) {
            if (++bad <= 20)
                std::fprintf(stderr, "stage %02x story %x sub %u flags %s: want %x %x got %x %x\n", stage, story, sub,
                             flags.c_str(), geo, tex, gi, ti);
        }
    }
    std::printf("stage files: %d cases, %d wrong\n", cases, bad);
    CHECK(cases > 5000);
    CHECK(bad == 0);
    return testResult("test_stage_files");
}
