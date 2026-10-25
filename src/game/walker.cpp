#include "game/walker.hpp"

#include "engine/data_source.hpp"
#include "engine/x87.hpp"
#include "game/angles.hpp"

#include <cmath>

namespace od {

namespace {

// Pause lengths (ticks) of the arrival pause, table 0x88dc60 indexed by rng & 0xf (the data source's).
const DataTable<ScalarTable<int16_t, 16>> pauseTicks{TableId::TownNpcPauseTicks};

// Maybe_IntegrateActorVelocity(actor, 0, 0, speed): (0, 0, speed << 12) rotated by yaw.
void integrate(Walker& w) {
    const int k = w.yaw & 0xFFF;
    w.x += int32_t((int64_t(w.speed) * 4096 * port::fixedSin(k)) >> 12); // the exe's table 0x847ad8 (game/angles)
    w.z += int32_t((int64_t(w.speed) * 4096 * port::fixedCos(k)) >> 12);
}

bool near(int16_t target, int32_t pos) { return uint32_t(int(target) - int(int16_t(pos >> 16)) + 0x80) < 0x100; }

} // namespace

int angleTo(int32_t ax, int32_t az, int32_t bx, int32_t bz) {
    // FUN_0040b960: fpatan on floats, * 2048 * the scale (1 / 3.14, game/angles), __ftol truncation (engine/x87: 24-bit
    // precision)
    return od::port::x87::atanAngle(port::atanScale(), float(bx - ax), float(bz - az)) & 0xFFF;
}

uint8_t chooseNext(const Waypoint& wp, uint8_t prev, Rng& rng) {
    int count = 0;
    for (uint8_t n : wp.next) count += n < 0x80;
    if (count < 2) return wp.next[0];
    int k = int((rng.next() & 0xFFFF) % uint32_t(count));
    if (wp.next[size_t(k)] == prev) k = (k + 1) % count;
    return wp.next[size_t(k)];
}

void stepWalker(Walker& w, const Route& route, Rng& rng, int forcedNext) {
    auto advance = [&] { // FUN_004446d0
        const auto it = route.find(w.cur);
        const uint8_t next = forcedNext >= 0 ? uint8_t(forcedNext)
                             : it != route.end() ? chooseNext(it->second, w.prev, rng)
                                                 : w.cur;
        w.prev = w.cur;
        w.cur = next;
    };
    switch (w.sub) {
    case 5: // substate 3 (0x442470) for one tick, then FUN_00445150 resumes the path (no player yet)
        w.sub = 0;
        return;
    case 4: // 0x442050: stand still; its first tick draws the duration
        if (w.pause <= 0) w.pause = pauseTicks[rng.next() & 0xF];
        if (--w.pause == 0) w.sub = 5;
        return;
    case 2: // 0x441f10: next waypoint, keep walking this tick
        advance();
        w.sub = 0;
        integrate(w);
        return;
    case 0: { // 0x441dc0: target = current waypoint, then move in the same tick
        const auto it = route.find(w.cur);
        if (it != route.end()) {
            w.tx = it->second.x;
            w.tz = it->second.z;
        }
        w.sub = 1;
        [[fallthrough]];
    }
    case 1: { // 0x441e10: turn toward the target by `turn` (FUN_005e3db0), move, arrive within 0x80
        const int d =
            port::angleDiff(uint32_t(w.yaw), uint32_t(angleTo(int32_t(w.tx) << 16, int32_t(w.tz) << 16, w.x, w.z)));
        const int t = w.turn < 0 ? -w.turn : w.turn;
        if (t <= d) w.yaw = int16_t(w.yaw + w.turn);
        if (d <= -t) w.yaw = int16_t(w.yaw - w.turn);
        integrate(w);
        if (near(w.tx, w.x) && near(w.tz, w.z)) {
            if (int32_t(0x5500aau << (rng.next() & 31)) < 0 && (w.flags & 0x10)) {
                advance();
                w.sub = 4;
                w.pause = 0;
            } else {
                w.sub = 2;
            }
        }
        return;
    }
    default:
        w.sub = 0;
    }
}

namespace {

// FUN_005e3a10(a, b): (int) sqrt of the squared distance between the integer parts of two positions
// (16-bit differences, FUN_0040b980).
int distance(int32_t ax, int32_t ay, int32_t az, int32_t bx, int32_t by, int32_t bz) {
    const int dx = int16_t(int16_t(ax >> 16) - int16_t(bx >> 16));
    const int dy = int16_t(int16_t(ay >> 16) - int16_t(by >> 16));
    const int dz = int16_t(int16_t(az >> 16) - int16_t(bz >> 16));
    const int32_t n = int32_t(uint32_t(dx * dx) + uint32_t(dy * dy) + uint32_t(dz * dz));
    return n < 0 ? int32_t(0x80000000u) : int(std::sqrt(double(n)));
}

} // namespace

void stepWalker(Walker& w, const Route& route, Rng& rng, const WalkerPlayer& player) {
    if (w.state == 3) { // FUN_00442470
        const int d = distance(player.x, player.y, player.z, w.x, w.y, w.z);
        const int a = port::angleDiff(uint32_t(w.yaw), uint32_t(angleTo(player.x, player.z, w.x, w.z)));
        if (0x180 < d || 0x500u < uint32_t(a + 0x280)) { // FUN_00445150: back to the route (state 1 instead
                                                          // when +0xd & 0x80 -- no ported walker has it)
            w.state = 0;
            w.sub = 0;
        }
        return;
    }
    stepWalker(w, route, rng);
    const int d = distance(player.x, player.y, player.z, w.x, w.y, w.z); // FUN_00441cb0's tail
    if (d < 0x100) {
        const int a = port::angleDiff(uint32_t(w.yaw), uint32_t(angleTo(player.x, player.z, w.x, w.z)));
        if (uint32_t(a + 0x200) < 0x400u) {
            w.state = 3;
            w.sub = 0;
        }
    }
}

} // namespace od
