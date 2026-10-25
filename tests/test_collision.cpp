// Collision queries vs. the exe's own functions.
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "check.hpp"
#include "translator/state_translator.hpp"
#include "data_source_binding.hpp"
#include "content/registry.hpp"
#include "game/collision.hpp"
#include "engine/dat.hpp"
#include "engine/room.hpp"

namespace fs = std::filesystem;

namespace {
bool readBox(std::istringstream& ss, int16_t box[6]) {
    std::string tok;
    bool any = true;
    for (int i = 0; i < 6; ++i) {
        ss >> tok;
        if (tok == "-") any = false;
        else box[i] = int16_t(std::stoi(tok));
    }
    return any;
}
// the i-th short of the hit rect block (0x6d00d8..0x6d00e7: x0, top, z0, noStepUp | lastPushAxis << 8, x1, bottom, z1, shape) as the
// golden dumps it
int16_t hitWord(od::CollisionState& s, int i) {
    uint8_t image[0x80];
    od::test::collisionStateBytes(s, image);
    const size_t k = 0x18 + size_t(i) * 2;
    return int16_t(uint16_t(image[k] | image[k + 1] << 8));
}
} // namespace

int main(int argc, char** argv) {
    od::content::registerAll(); // the game code dispatches into the content through its registry
    if (argc < 4) return 2;
    const fs::path datDir = argv[1], data = argv[2];
    od::bindTestDataSource(argv[3]); // the collision queries' arc tangent scale
    { // the warp triggers: the ST0E train track wraps (type 1); ST40 sub 2's exit to itself is a door (type 0xd)
        auto warps = [&](const char* file, size_t sub) {
            const od::GameFile dat = od::pcGameFile(file, od::readFile(datDir / file));
            const std::span<const uint8_t> b = od::roomBlock(dat);
            return od::roomHasTrigger(od::collisionRoom(b, od::roomGrids(b)[sub]), od::kTriggerWarpType);
        };
        CHECK(warps("ST0E.DAT", 2));
        CHECK(!warps("ST40.DAT", 2));
    }
    std::ifstream in(data / "collision_ST08.txt");
    CHECK(bool(in));
    od::GameFile st;
    std::span<const uint8_t> block;
    od::CollisionRoom room;
    bool haveRoom = false;
    int probes = 0, moves = 0, resolves = 0, walls = 0, triggers = 0, bad = 0;
    for (std::string line; std::getline(in, line);) {
        std::istringstream ss(line);
        char tag;
        ss >> tag;
        if (tag == 'R') {
            std::string file;
            int x0, z0, w, h;
            long long cellsRel, segsRel;
            ss >> file >> x0 >> z0 >> w >> h >> cellsRel >> segsRel;
            st = od::pcGameFile(file, od::readFile(datDir / file)); // the room the player script ended in
            block = od::roomBlock(st);
            for (const od::RoomGrid& g : od::roomGrids(block))
                if (g.x0 == x0 && g.z0 == z0) {
                    room = od::collisionRoom(block, g);
                    haveRoom = true;
                }
            CHECK(haveRoom);
            if (!haveRoom) break;
            CHECK(room.w == w && room.h == h);
            CHECK((long long)room.cells - (long long)room.pool == cellsRel);
            CHECK((long long)room.segments - (long long)room.pool == segsRel);
            continue;
        }
        int ax, ay, az;
        ss >> ax >> ay >> az;
        od::CollisionState s;
        int16_t box[6] = {};
        if (tag == 'G') {
            const bool hasBox = readBox(ss, box);
            int mode;
            std::string maskHex;
            int32_t expect;
            ss >> mode >> maskHex >> expect;
            int hit[8];
            for (int& v : hit) ss >> v;
            const int32_t got = od::probeGround(room, s, int16_t(ax), int16_t(ay), int16_t(az), hasBox ? box : nullptr,
                                                mode, uint32_t(std::stoul(maskHex, nullptr, 16)));
            bool ok = got == expect;
            for (int i = 0; i < 8; ++i) ok = ok && hitWord(s, i) == int16_t(hit[i]);
            ++probes;
            if (!ok && ++bad <= 8) std::fprintf(stderr, "G %s -> %d\n", line.c_str(), got);
        } else if (tag == 'M') {
            int bx, by, bz;
            ss >> bx >> by >> bz;
            const bool hasBox = readBox(ss, box);
            std::string maskHex, expectHex;
            ss >> maskHex >> expectHex;
            int hit[8];
            for (int& v : hit) ss >> v;
            const uint32_t got = od::testMove(room, s, {int16_t(ax), int16_t(ay), int16_t(az)},
                                              {int16_t(bx), int16_t(by), int16_t(bz)}, hasBox ? box : nullptr,
                                              uint32_t(std::stoul(maskHex, nullptr, 16)));
            bool ok = got == uint32_t(std::stoul(expectHex, nullptr, 16));
            for (int i = 0; i < 8; ++i) ok = ok && hitWord(s, i) == int16_t(hit[i]);
            ++moves;
            if (!ok && ++bad <= 8) std::fprintf(stderr, "M %s -> %x\n", line.c_str(), got);
        } else if (tag == 'T') { // FUN_00413c80 trigger scan
            int yaw, pyaw;
            std::string expectHex, outHex;
            ss >> yaw >> pyaw >> expectHex >> outHex;
            od::TriggerHit hit;
            const uint32_t got = od::triggerScan(room, s, {int16_t(ax), int16_t(ay), int16_t(az)}, int16_t(yaw),
                                                 int16_t(pyaw), hit);
            bool ok = got == uint32_t(std::stoul(expectHex, nullptr, 16));
            uint8_t want[12];
            for (int i = 0; i < 12; ++i) want[i] = uint8_t(std::stoul(outHex.substr(size_t(i) * 2, 2), nullptr, 16));
            auto w16 = [&](int k) { return uint16_t(want[k * 2] | want[k * 2 + 1] << 8); };
            ok = ok && hit.type == w16(0) && hit.flags == w16(1);
            if (got) ok = ok && hit.x == int16_t(w16(2)) && hit.y == int16_t(w16(3)) && hit.z == int16_t(w16(4)) && hit.yaw == w16(5);
            ++triggers;
            if (!ok && ++bad <= 12)
                std::fprintf(stderr, "T %s -> %u type %x flags %x %d %d %d yaw %x\n", line.c_str(), got, hit.type, hit.flags,
                             hit.x, hit.y, hit.z, hit.yaw);
        } else if (tag == 'W') { // FUN_00410040 wall query
            int yaw;
            ss >> yaw;
            const bool hasBox = readBox(ss, box);
            std::string maskHex, expectHex, memHex;
            ss >> maskHex >> expectHex >> memHex;
            const uint32_t got = od::wallQuery(room, s, {int16_t(ax), int16_t(ay), int16_t(az)}, hasBox ? box : nullptr,
                                               yaw, uint32_t(std::stoul(maskHex, nullptr, 16)));
            bool ok = got == uint32_t(std::stoul(expectHex, nullptr, 16));
            int firstDiff = -1;
            uint8_t image[0x80];
            od::test::collisionStateBytes(s, image);
            for (size_t i = 0; i < memHex.size() / 2; ++i) {
                const uint8_t want = uint8_t(std::stoul(memHex.substr(i * 2, 2), nullptr, 16));
                if (image[i] != want && firstDiff < 0) firstDiff = int(i);
            }
            ok = ok && firstDiff < 0;
            ++walls;
            if (!ok && ++bad <= 12)
            {
                std::fprintf(stderr, "W %s -> %x (first byte diff at 0x6d00%02x)\n  got ", line.c_str(), got,
                             firstDiff < 0 ? 0 : 0xc0 + firstDiff);
                for (size_t i = 0; i < memHex.size() / 2; ++i) std::fprintf(stderr, "%02x", image[i]);
                std::fprintf(stderr, "\n");
            }
        } else if (tag == 'S') { // FUN_00410a60 push-out: result + every global it touches
            int bx, by, bz, air;
            ss >> bx >> by >> bz >> air;
            readBox(ss, box);
            std::string maskHex, expectHex, memHex;
            ss >> maskHex >> expectHex >> memHex;
            s.setBox(box);
            const uint32_t got = od::resolveMove(room, s, {int16_t(ax), int16_t(ay), int16_t(az)},
                                                 {int16_t(bx), int16_t(by), int16_t(bz)}, uint8_t(air),
                                                 uint32_t(std::stoul(maskHex, nullptr, 16)));
            bool ok = got == uint32_t(std::stoul(expectHex, nullptr, 16));
            int firstDiff = -1;
            uint8_t image[0x80];
            od::test::collisionStateBytes(s, image);
            for (size_t i = 0; i < memHex.size() / 2; ++i) {
                const uint8_t want = uint8_t(std::stoul(memHex.substr(i * 2, 2), nullptr, 16));
                if (image[i] != want && firstDiff < 0) firstDiff = int(i);
            }
            ok = ok && firstDiff < 0;
            ++resolves;
            if (!ok && ++bad <= 12)
                std::fprintf(stderr, "S %s -> %x (first byte diff at 0x6d00%02x)\n", line.c_str(), got,
                             firstDiff < 0 ? 0 : 0xc0 + firstDiff);
        }
    }
    std::printf("%d probes, %d moves, %d resolves, %d walls, %d triggers, %d mismatches\n", probes, moves, resolves,
                walls, triggers, bad);
    CHECK(probes > 1000 && moves > 1000 && resolves > 1000 && walls > 500 && triggers > 300);
    CHECK(bad == 0);
    return testResult("test_collision");
}
