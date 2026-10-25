// The weather overlays (game/weather_draw) vs. the GPU packets dash2.exe wrote, tick by tick
// (tests/data/weather_*.bin -- not in git (*.bin);
// weather_ST1D.bin = save 00 on Forbidden Island, the blizzard layer of effect 0x431af0; weather_ST09_snow.bin = the
// snow sprites FUN_00591c10 of Yosyonke's 0x591640 statics). Per tick the golden holds the state weather_draw reads
// (after the frame) and the packets; the test rebuilds every packet word of the quads of the 320-pixel frame
// (original == true) and compares them as multisets (the OT order is not checked).
//   test_weather_draw <dash2.exe> <tests/data>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <vector>

#include "translator/state_translator.hpp"
#include "data_source_binding.hpp"
#include "content/registry.hpp"
#include "translator/gamemem.hpp"
#include "translator/game_memory.hpp"
#include "game/weather_draw.hpp"

namespace {

std::vector<uint8_t> readAll(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

using Words = std::vector<uint32_t>;

uint32_t xy(const od::weather::Quad& q, int k) { return uint32_t(uint16_t(q.x[k])) | uint32_t(uint16_t(q.y[k])) << 16; }
uint32_t uv(const od::weather::Quad& q, int k) { return uint32_t(q.u[k]) | uint32_t(q.v[k]) << 8; }
uint32_t rgb(uint8_t g) { return uint32_t(g) * 0x10101u; }

Words packet(const od::weather::Quad& q) {
    if (q.source == od::RoutineId::BlizzardLayerUpdate) // POLY_GT4
        return {0x3e000000u | rgb(q.grey[0]), xy(q, 0), uv(q, 0) | uint32_t(q.clut) << 16,
                rgb(q.grey[1]), xy(q, 1), uv(q, 1) | uint32_t(q.tpage) << 16,
                rgb(q.grey[2]), xy(q, 2), uv(q, 2),
                rgb(q.grey[3]), xy(q, 3), uv(q, 3)};
    return {0x2e000000u | rgb(q.grey[0]), xy(q, 0), uv(q, 0) | uint32_t(q.clut) << 16, // POLY_FT4
            xy(q, 1), uv(q, 1) | uint32_t(q.tpage) << 16, xy(q, 2), uv(q, 2), xy(q, 3), uv(q, 3)};
}

int run(const std::filesystem::path& exe, const std::filesystem::path& file, int& checked) {
    const auto b = readAll(file);
    size_t o = 0;
    auto rd32 = [&]() {
        uint32_t v;
        std::memcpy(&v, &b[o], 4);
        o += 4;
        return v;
    };
    if (b.size() < 8 || std::memcmp(b.data(), "STD1", 4) != 0) throw std::runtime_error("bad golden " + file.string());
    o = 4;
    std::vector<std::pair<uint32_t, uint32_t>> ranges(rd32());
    for (auto& r : ranges) {
        r.first = rd32();
        r.second = rd32();
    }
    od::GameMem m;
    const od::ScopedGameMemory bound(m);
    m.mapExeData(exe);
    od::test::loadNativeState(m); // the native state from this golden's image (nothing of the previous one)
    int fails = 0;
    while (o < b.size()) {
        const uint32_t tick = rd32();
        od::test::storeNativeState(m);
        for (auto [a, n] : ranges) {
            m.map(a, std::span<const uint8_t>(&b[o], n));
            o += n;
        }
        od::test::loadNativeState(m);
        std::vector<Words> game(rd32());
        for (Words& w : game) {
            rd32(); // writer pc
            w.resize((rd32() >> 16) & 0xff);
            for (uint32_t& x : w) x = rd32();
        }
        if (rd32() != 0) throw std::runtime_error("draw commands in a weather golden");
        std::vector<od::weather::Quad> quads;
        od::weather::collect(quads, 64);
        std::vector<Words> mine;
        for (const auto& q : quads)
            if (q.original) mine.push_back(packet(q));
        std::sort(game.begin(), game.end());
        std::sort(mine.begin(), mine.end());
        checked += int(game.size());
        if (game != mine) {
            if (++fails <= 8) {
                std::printf("  %s tick %u: %zu game packets, %zu rebuilt\n", file.filename().string().c_str(), tick,
                            game.size(), mine.size());
                for (size_t i = 0; i < std::min(game.size(), mine.size()); ++i)
                    if (game[i] != mine[i]) {
                        std::printf("    first difference #%zu:", i);
                        for (size_t k = 0; k < game[i].size(); ++k) std::printf(" %08x", game[i][k]);
                        std::printf("\n    rebuilt:            ");
                        for (size_t k = 0; k < mine[i].size(); ++k) std::printf(" %08x", mine[i][k]);
                        std::printf("\n");
                        break;
                    }
            }
        }
    }
    return fails;
}

} // namespace

int main(int argc, char** argv) {
    od::content::registerAll(); // the game code dispatches into the content through its registry
    if (argc < 3) {
        std::printf("usage: test_weather_draw <dash2.exe> <tests/data>\n");
        return 2;
    }
    od::bindTestDataSource(argv[1]);
    int fails = 0, files = 0, checked = 0;
    try {
        for (const auto& e : std::filesystem::directory_iterator(argv[2])) {
            const auto n = e.path().filename().string();
            if (n.rfind("weather_", 0) != 0 || e.path().extension() != ".bin") continue;
            const int f = run(argv[1], e.path(), checked);
            std::printf("%s: %s\n", n.c_str(), f ? "FAIL" : "ok");
            fails += f;
            ++files;
        }
    } catch (const std::exception& e) {
        std::printf("error: %s\n", e.what());
        return 1;
    }
    std::printf("%d golden(s), %d packets compared, %d failing tick(s)\n", files, checked, fails);
    if (files == 0) std::printf("(no weather_*.bin goldens: skipped)\n");
    return fails ? 1 : 0;
}
