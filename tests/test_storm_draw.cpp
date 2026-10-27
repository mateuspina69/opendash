// The storm draw reproduction (content/intro/crash_landing_draw) vs. the GPU packets dash2.exe wrote, tick by tick
// .
// Per tick the golden holds the state the storm draw reads (after the frame) and the storm packets in allocation order;
// the test maps the state over the exe data, runs od::storm::collect and compares:
//  * the 2D sprites of the 320-pixel frame (backdrop 0x517cf0, panorama 0x599a50 / 0x59a3e0): x, y, w, h, u, v, CLUT,
//    tpage (the last E1 before the packet) -- as sorted lists (the order follows the OT, see content/intro/crash_landing_draw.cpp);
//  * the cloud puffs of 0x599580: count and order, uv / CLUT / tpage, and the centre (projected with the game camera)
//    against the midpoint of the packet's corners 0 / 3 (1 pixel);
//  * the flash ring triangles of 0x436f00: every vertex and colour;
//  * the draw commands of the debris / flash / enemy shots / craft jets, word for word (before the draw pass).
//   test_storm_draw <dash2.exe> <tests/data>
// Recipes (cd tools;
// --out ../opendash/tests/data/<name>; ck_s = the success chain, see story.md section 8), as name N/S/F:
//   storm_ST3A_a 12000/150/200 (script 6 state 0, the backdrop)   storm_ST3A_b 12300/230/120 (backdrop off, variant 5)
//   storm_ST3A_c 14400/0/120 (variants 2 / 3, then 5)              storm_ST3A_d 15300/280/150 (backdrop on, 2 / 3)
//   storm_ST3A_e 16200/50/150 (variant 5, the flash ring of 16275)   storm_ST3A_f 15900/200/230 (falling debris)
//   storm_ST3A_g 15600/130/80 (the craft's jet glows)               storm_ST3A_h 17100/30/150 (enemy shots, flashes)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <vector>

#include "translator/state_translator.hpp"
#include "data_source_binding.hpp"
#include "content/forbidden_island/elephant_boss.hpp"
#include "content/registry.hpp"
#include "translator/gamemem.hpp"
#include "game/draw_command_decode.hpp"
#include "game/draw_commands.hpp"
#include "translator/game_memory.hpp"
#include "game/projection.hpp"
#include "content/intro/crash_landing_draw.hpp"

namespace {

std::vector<uint8_t> readAll(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

struct Packet {
    uint32_t pc = 0;
    uint16_t tpage = 0;
    std::vector<uint32_t> w;
};

using Key = std::tuple<int, int, int, int, int, int, int, int>; // x y w h u v clut tpage

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
        std::vector<Packet> pk(rd32());
        for (Packet& p : pk) {
            p.pc = rd32();
            uint32_t h = rd32();
            p.tpage = uint16_t(h);
            p.w.resize((h >> 16) & 0xff);
            for (uint32_t& w : p.w) w = rd32();
        }
        std::vector<od::storm::DrawCommand> gameCmds(rd32());
        for (od::storm::DrawCommand& d : gameCmds) {
            d.pc = rd32();
            d.w.resize(rd32());
            for (uint32_t& w : d.w) w = rd32();
        }
        od::fx::TextureCache cache; // on demand: the entries the emitters met
        od::storm::Frame f;
        std::vector<od::fx::Prim> prims;
        std::vector<od::storm::DrawCommand> cmds;
        od::storm::collect(cache, 0, f, prims, &cmds);
        const auto& tex = cache.entries();
        auto fail = [&](const char* what) {
            if (++fails <= 12) std::printf("  %s tick %u: %s\n", file.filename().string().c_str(), tick, what);
        };
        // 2D sprites
        std::vector<Key> game, mine;
        std::vector<const Packet*> puffs, rings;
        for (const Packet& p : pk) {
            const uint8_t cmd = uint8_t(p.w[0] >> 24);
            if (p.pc == 0x599950) {
                puffs.push_back(&p);
                continue;
            }
            if (p.pc == 0x437004) {
                rings.push_back(&p);
                continue;
            }
            const int x = int16_t(p.w[1]), y = int16_t(p.w[1] >> 16);
            const int u = p.w[2] & 0xff, v = (p.w[2] >> 8) & 0xff, clut = int(p.w[2] >> 16);
            int w = 16, h = 16;
            if (cmd == 0x64) {
                w = int(p.w[3] & 0xffff);
                h = int(p.w[3] >> 16);
            }
            game.emplace_back(x, y, w, h, u, v, clut, p.tpage & 0x1ff);
        }
        std::vector<od::storm::Sprite> sprites = f.back; // the slot-0x88 chains and the depth-OT chains (the sea)
        for (const od::storm::DepthChain& d : f.depth) sprites.insert(sprites.end(), d.sprites.begin(), d.sprites.end());
        for (const od::storm::Sprite& s : sprites)
            if (s.original) mine.emplace_back(s.x, s.y, s.w, s.h, s.u, s.v, s.clut, s.tpage & 0x1ff);
        std::sort(game.begin(), game.end());
        std::sort(mine.begin(), mine.end());
        if (game != mine) {
            char msg[96];
            std::snprintf(msg, sizeof msg, "2D sprites differ (game %zu, port %zu)", game.size(), mine.size());
            fail(msg);
            for (size_t i = 0; i < std::max(game.size(), mine.size()) && fails <= 2; ++i) {
                auto pr = [](const char* w, const std::vector<Key>& v, size_t i) {
                    if (i >= v.size()) return;
                    auto [x, y, ww, h, u, vv, c, t] = v[i];
                    std::printf("    %s x %d y %d w %d h %d u %d v %d clut %04x tpage %03x\n", w, x, y, ww, h, u, vv, c, t);
                };
                if (i < game.size() && i < mine.size() && game[i] == mine[i]) continue;
                pr("game", game, i);
                pr("port", mine, i);
            }
        }
        for (const od::storm::Sprite& s : sprites)
            if (s.texture < 0) fail("sprite without texture");
        // draw commands: as multisets (their order in the buffer does not matter to the draw pass), word 0 of a 0x2c
        // billboard ignored (left unwritten by the emitters); the three records of the flash as one writer family
        auto norm = [](std::vector<od::storm::DrawCommand> v) {
            std::vector<std::vector<uint32_t>> o;
            for (od::storm::DrawCommand& d : v) {
                if (d.w.size() == 10) d.w[0] = 0;
                std::vector<uint32_t> k{(d.pc & ~0xfu) == 0x4317a0 ? 0x4317a0u : d.pc};
                k.insert(k.end(), d.w.begin(), d.w.end());
                o.push_back(std::move(k));
            }
            std::sort(o.begin(), o.end());
            return o;
        };
        const auto gc = norm(gameCmds), mc = norm(cmds);
        if (gc != mc) {
            char msg[96];
            std::snprintf(msg, sizeof msg, "draw commands differ (game %zu, port %zu)", gc.size(), mc.size());
            fail(msg);
            for (size_t i = 0, shown = 0; i < std::max(gc.size(), mc.size()) && shown < 2 && fails <= 3; ++i) {
                if (i < gc.size() && i < mc.size() && gc[i] == mc[i]) continue;
                auto show = [](const char* who, const std::vector<std::vector<uint32_t>>& v, size_t i) {
                    std::printf("    %s", who);
                    if (i < v.size())
                        for (uint32_t x : v[i]) std::printf(" %08x", x);
                    std::printf("\n");
                };
                show("game", gc, i);
                show("port", mc, i);
                ++shown;
            }
        }
        // cloud puffs (the prims of variant 2 come first: the pool-0x60 loop runs before the draw commands')
        size_t nPuffPrims = 0;
        for (const od::fx::Prim& q : prims)
            if ((q.object & 0xff000u) == 0x70000u) ++nPuffPrims;
        if (puffs.size() != nPuffPrims) {
            char msg[96];
            std::snprintf(msg, sizeof msg, "cloud puffs: game %zu, port %zu", puffs.size(), nPuffPrims);
            fail(msg);
        } else {
            const int sh = m.u8(0x6d00b5) & 0x1f;
            for (size_t i = 0; i < puffs.size(); ++i) {
                const Packet& p = *puffs[i];
                const od::fx::Prim& q = prims[i];
                const od::fx::Texture& t = tex[size_t(q.texture)];
                const int u0 = p.w[2] & 0xff, v0 = (p.w[2] >> 8) & 0xff;
                if (t.clut != uint16_t(p.w[2] >> 16) || t.tpage != uint16_t(p.w[4] >> 16) ||
                    int(q.v[0].u - 0.5f) != u0 || int(q.v[0].v - 0.5f) != v0 || int(q.v[3].u - 0.5f) != u0 + 0x3f) {
                    fail("cloud puff texture / uv");
                    break;
                }
                int16_t c[3];
                for (int k = 0; k < 3; ++k) c[k] = int16_t(uint16_t(int(q.centre[k])) << sh);
                const od::port::CameraPoint cp = od::port::cameraPoint(c);
                const int mx = (int16_t(p.w[1]) + int16_t(p.w[7])) / 2, my = (int16_t(p.w[1] >> 16) + int16_t(p.w[7] >> 16)) / 2;
                if (std::abs(mx - cp.sx) > 1 || std::abs(my - cp.sy) > 1) {
                    char msg[96];
                    std::snprintf(msg, sizeof msg, "cloud puff %zu at (%d, %d), port (%d, %d)", i, mx, my, cp.sx, cp.sy);
                    fail(msg);
                    break;
                }
            }
        }
        // flash ring triangles (G3: colour0 | code, xy0, colour1, xy1, colour2, xy2)
        if (rings.size() != f.front.size()) {
            fail("flash ring triangle count");
        } else {
            for (size_t i = 0; i < rings.size(); ++i) {
                const Packet& p = *rings[i];
                const od::storm::Tri& t = f.front[i];
                for (int k = 0; k < 3; ++k) {
                    const uint32_t col = p.w[size_t(k) * 2] & 0xffffff, xy = p.w[size_t(k) * 2 + 1];
                    if (col != t.colour[k] || int16_t(xy) != int(t.x[k]) || int16_t(xy >> 16) != int(t.y[k])) {
                        char msg[160];
                        std::snprintf(msg, sizeof msg, "flash ring tri %zu v%d: game %06x (%d, %d) port %06x (%d, %d)", i, k,
                                      col, int16_t(xy), int16_t(xy >> 16), t.colour[k], int(t.x[k]), int(t.y[k]));
                        fail(msg);
                        i = rings.size();
                        break;
                    }
                }
            }
        }
        checked += int(game.size() + puffs.size() + rings.size() + gc.size());
    }
    return fails;
}


// The draw-command decoder (game/draw_commands) vs. the exe's Maybe_ProcessDrawCommands 0x5f0580 run on the same
// recipe:
//  * 0xf8: every GT4 the exe linked (count after its NCLIP culling), each vertex projected with the game camera
//    (+-1 pixel), uv, colour, CLUT / tpage;
//  * 0xf0: the G4 count, colours and the vertices (camera-plane offsets scaled by h / depth, +-2 pixels);
//  * 0x2c: drawn or culled as the exe, the in-place FT4 corners (+-2 pixels);
//  * the emitter FUN_005c7e00 (od::port::st1dShockRingCommand): its 8 words.
int runDc(const std::filesystem::path& exe, const std::filesystem::path& file, int& checked) {
    const auto b = readAll(file);
    size_t o = 0;
    auto rd32 = [&]() {
        uint32_t v;
        std::memcpy(&v, &b[o], 4);
        o += 4;
        return v;
    };
    auto rdv = [&]() {
        std::vector<uint32_t> v(rd32());
        for (uint32_t& x : v) x = rd32();
        return v;
    };
    if (b.size() < 8 || std::memcmp(b.data(), "DCG1", 4) != 0) throw std::runtime_error("bad golden " + file.string());
    o = 4;
    std::vector<std::pair<uint32_t, uint32_t>> ranges(rd32());
    for (auto& r : ranges) {
        r.first = rd32();
        r.second = rd32();
    }
    od::GameMem m;
    const od::ScopedGameMemory bound(m);
    m.mapExeData(exe);
    for (auto [a, n] : ranges) {
        m.map(a, std::span<const uint8_t>(&b[o], n));
        o += n;
    }
    od::test::loadNativeState(m);
    const int sh = m.u8(0x6d00b5) & 0x1f;
    const float h = float(m.s32(0xa59d10));
    int fails = 0;
    const uint32_t cases = rd32();
    for (uint32_t ci = 0; ci < cases; ++ci) {
        char msg[200];
        auto fail = [&](const char* what) {
            if (++fails <= 12) std::printf("  %s case %u: %s\n", file.filename().string().c_str(), ci, what);
        };
        if (rd32() == 1) { // the emitter
            const uint32_t actor = rd32(), frame = rd32() & 0xffff;
            od::test::storeNativeState(m);
            m.map(actor, std::span<const uint8_t>(&b[o], 0xdc));
            o += 0xdc;
            const auto want = rdv();
            m.set16(0x6d0006, frame);
            od::test::loadNativeState(m);
            od::dc::resetCursor();
            const uint32_t at = od::dc::drawCommands().cursor; // the native cursor the reset set
            od::port::st1dShockRingCommand(actor);
            od::test::storeNativeState(m);
            const uint32_t n = (m.u32(od::dc::kCursor) - at) / 4;
            std::vector<uint32_t> got(n);
            for (uint32_t i = 0; i < n; ++i) got[i] = m.u32(at + i * 4);
            if (got != want) fail("FUN_005c7e00 words differ");
            checked += int(want.size());
            continue;
        }
        const auto words = rdv(), after = rdv(), pk = rdv();
        od::fx::TextureCache tex;
        std::vector<od::fx::Prim> out;
        od::dc::decodeRecords(words.data(), words.size(), tex, out);
        const uint32_t op = (words[1] >> 24) & 0xfc;
        // a vertex of a camera-plane prim on the screen: the centre's screen point + offset * h / depth
        auto planar = [&](const od::fx::Prim& p, int k, float& x, float& y) {
            int16_t c[3];
            for (int q = 0; q < 3; ++q) c[q] = int16_t(std::lround(p.centre[q] * float(1 << sh)));
            const od::port::CameraPoint cp = od::port::cameraPoint(c);
            const float s = h * float(1 << sh) / float(cp.view[2]);
            x = float(cp.sx) + p.v[k].off[0] * s;
            y = float(cp.sy) + p.v[k].off[1] * s;
        };
        auto colourOf = [](const od::fx::Vertex& v, float unit) {
            uint32_t c = 0;
            for (int q = 0; q < 3; ++q) c |= uint32_t(std::lround(v.color[q] * unit)) << (8 * q);
            return c;
        };
        if (op == 0xf8) {
            // OpenDash draws every segment, both sides (game/draw_command_decode decodeF8); the exe links the ones its
            // NCLIPs keep, in the same order: each of its GT4s is the next port segment that matches
            size_t pi = 0;
            for (size_t i = 0; i < pk.size() / 13; ++i) {
                const uint32_t* g = &pk[i * 13];
                auto matches = [&](const od::fx::Prim& p) {
                    const od::fx::Texture& t = tex.entries()[size_t(p.texture)];
                    if (t.clut != uint16_t(g[3] >> 16) || t.tpage != uint16_t(g[6] >> 16)) return false;
                    for (int k = 0; k < 4; ++k) {
                        int16_t v[3];
                        for (int q = 0; q < 3; ++q)
                            v[q] = int16_t(std::lround((p.centre[q] + p.v[k].off[q]) * float(1 << sh)));
                        const od::port::CameraPoint cp = od::port::cameraPoint(v);
                        const uint32_t xy = g[2 + k * 3], uv = g[3 + k * 3], col = g[1 + k * 3] & 0xffffff;
                        const int pu = int(p.v[k].u - 0.5f), pv = int(p.v[k].v - 0.5f);
                        if (std::abs(int16_t(xy) - cp.sx) > 1 || std::abs(int16_t(xy >> 16) - cp.sy) > 1 ||
                            pu != int(uv & 0xff) || pv != int((uv >> 8) & 0xff) || colourOf(p.v[k], 128.0f) != col)
                            return false;
                    }
                    return true;
                };
                while (pi < out.size() && !matches(out[pi])) ++pi;
                if (pi == out.size()) {
                    std::snprintf(msg, sizeof msg, "0xf8: game GT4 %zu of %zu has no port segment (port %zu)", i,
                                  pk.size() / 13, out.size());
                    fail(msg);
                    break;
                }
                ++pi;
            }
            checked += int(pk.size() / 13);
        } else if (op == 0xf0) {
            // OpenDash keeps the ring's widest segment range, the ones the exe skips collapsed (game/draw_command_decode
            // ringSegments): the exe's G4s are the shown ones, start + 1 .. end - 1
            const int s = int((words[4] >> 16) & 0x1f);
            const int start = (int(words[0] & 0xff) - 0x10) >> s, end = ((int((words[0] >> 8) & 0xff) - 0x10) >> s) + 1;
            const int lo = ((0 - 0x10) >> s) + 1;
            std::vector<od::fx::Prim> shown;
            for (size_t i = 0; i < out.size(); ++i)
                if (const int j = lo + int(i); j > start && j < end) shown.push_back(out[i]);
            if (pk.size() / 9 != shown.size()) {
                std::snprintf(msg, sizeof msg, "0xf0: game %zu G4, port %zu", pk.size() / 9, shown.size());
                fail(msg);
                continue;
            }
            for (size_t i = 0; i < shown.size(); ++i) {
                const uint32_t* g = &pk[i * 9];
                for (int k = 0; k < 4; ++k) {
                    float x, y;
                    planar(shown[i], k, x, y);
                    const uint32_t xy = g[2 + k * 2], col = g[1 + k * 2] & 0xffffff;
                    if (std::fabs(float(int16_t(xy)) - x) > 2.0f || std::fabs(float(int16_t(xy >> 16)) - y) > 2.0f ||
                        colourOf(shown[i].v[k], 255.0f) != col) {
                        std::snprintf(msg, sizeof msg, "0xf0 seg %zu v%d: game (%d, %d) %06x, port (%.1f, %.1f) %06x", i,
                                      k, int16_t(xy), int16_t(xy >> 16), col, x, y, colourOf(shown[i].v[k], 255.0f));
                        fail(msg);
                        i = shown.size();
                        break;
                    }
                }
            }
            checked += int(shown.size());
        } else if (op == 0x2c) {
            const bool drawn = (after[0] >> 24) == 9;
            if (drawn != (out.size() == 1)) {
                fail("0x2c drawn / culled differs");
                continue;
            }
            if (drawn)
                for (int k = 0; k < 4; ++k) {
                    float x, y;
                    planar(out[0], k, x, y);
                    const uint32_t xy = after[2 + size_t(k) * 2];
                    if (std::fabs(float(int16_t(xy)) - x) > 2.0f || std::fabs(float(int16_t(xy >> 16)) - y) > 2.0f) {
                        std::snprintf(msg, sizeof msg, "0x2c corner %d: game (%d, %d), port (%.1f, %.1f)", k,
                                      int16_t(xy), int16_t(xy >> 16), x, y);
                        fail(msg);
                        break;
                    }
                }
            ++checked;
        }
    }
    return fails;
}

} // namespace

int main(int argc, char** argv) {
    od::content::registerAll(); // the game code dispatches into the content through its registry
    if (argc < 3) {
        std::printf("usage: test_storm_draw <dash2.exe> <tests/data>\n");
        return 2;
    }
    od::bindTestDataSource(argv[1]);
    int fails = 0, files = 0, checked = 0;
    try {
        for (const auto& e : std::filesystem::directory_iterator(argv[2])) {
            const auto n = e.path().filename().string();
            const bool dcg = n.rfind("drawcmd_", 0) == 0;
            if ((!dcg && n.rfind("storm_", 0) != 0) || e.path().extension() != ".bin") continue;
            const int f = dcg ? runDc(argv[1], e.path(), checked) : run(argv[1], e.path(), checked);
            std::printf("%s: %s\n", n.c_str(), f ? "FAIL" : "ok");
            fails += f;
            ++files;
        }
    } catch (const std::exception& e) {
        std::printf("error: %s\n", e.what());
        return 1;
    }
    std::printf("%d golden(s), %d packets compared, %d failing tick check(s)\n", files, checked, fails);
    if (files == 0) std::printf("(no storm_*.bin goldens: skipped)\n");
    return fails ? 1 : 0;
}
