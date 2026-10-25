// The ghosts' smoothing (game/ghost_buffer): ordering, duplicates, loss, the 12-bit wrap of the angles, a cut on a
// teleport / a room change, extrapolation then freeze, and the removal of a silent player.
//   test_ghost_buffer
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "check.hpp"
#include "game/ghost_buffer.hpp"

namespace {

using od::ghost::GhostBuffer;
using od::port::RemotePlayerState;

constexpr uint64_t kTickNs = 33'500'000; // ~29.85 ticks / s
constexpr int32_t kUnit = 0x10000;       // one game unit, 16.16

RemotePlayerState at(int32_t x, uint8_t anim = 0) {
    RemotePlayerState s;
    s.room = {8, 2, true};
    s.snapshot.pos[0] = x * kUnit;
    s.snapshot.animId[0] = anim;
    return s;
}
int32_t xOf(const RemotePlayerState& s) { return s.snapshot.pos[0] / kUnit; }

void testInterpolation() {
    const RemotePlayerState a = at(0, 1), b = at(100, 2);
    const RemotePlayerState m = od::ghost::interpolate(a, b, 0.25);
    CHECK(xOf(m) == 25);
    CHECK(m.snapshot.animId[0] == 1); // discrete: the nearer state
    CHECK(od::ghost::interpolate(a, b, 0.75).snapshot.animId[0] == 2);
    // angles wrap the short way round the 12-bit circle
    RemotePlayerState ya = a, yb = b;
    ya.snapshot.yaw = 0xff0;
    yb.snapshot.yaw = 0x010;
    CHECK(od::ghost::interpolate(ya, yb, 0.5).snapshot.yaw == 0x1000); // forward through 0, not back through 0x800
    ya.snapshot.yaw = 0x010;
    yb.snapshot.yaw = 0xff0;
    CHECK(od::ghost::interpolate(ya, yb, 0.5).snapshot.yaw == 0x000);
    ya.snapshot.pitch = -0x7f0;
    yb.snapshot.pitch = 0x7f0; // 0x20 apart across the half turn
    CHECK(od::ghost::interpolate(ya, yb, 0.5).snapshot.pitch == -0x800);
}

// a sender at a steady pace: tick k at k * kTickNs, x = 10 * k
void steady(GhostBuffer& g, int from, int to) {
    for (int k = from; k <= to; ++k) g.push(uint64_t(k) * kTickNs, uint32_t(k), at(10 * k));
}

void testSteady() {
    GhostBuffer g;
    steady(g, 0, 20);
    auto s = g.sample(20 * kTickNs);
    CHECK(s && std::abs(g.renderTick() - 17.0) < 1e-6); // newest - the interpolation delay
    CHECK(s && xOf(*s) == 170);
    // between ticks the position is between the states
    g.push(21 * kTickNs, 21, at(210));
    s = g.sample(20 * kTickNs + kTickNs / 2);
    CHECK(s && xOf(*s) > 170 && xOf(*s) < 185);
    std::printf("  half a tick later: render tick %.2f, x %d\n", g.renderTick(), s ? xOf(*s) : -1);
}

void testOrderingDuplicatesLoss() {
    GhostBuffer g;
    // out of order: 0..5, then 8, 6, 7 (6 and 7 late); a duplicate of 5 with another position is ignored; 9 lost
    steady(g, 0, 5);
    g.push(5 * kTickNs, 5, at(999));
    g.push(8 * kTickNs, 8, at(80));
    g.push(8 * kTickNs, 6, at(60));
    g.push(8 * kTickNs, 7, at(70));
    g.push(10 * kTickNs, 10, at(100));
    // walk the render point forward: positions never go back, 10 per tick through the loss of 9
    int last = -1;
    bool monotonic = true, linear = true;
    for (uint64_t now = 10 * kTickNs; now < 14 * kTickNs; now += kTickNs / 4) {
        const auto s = g.sample(now);
        if (!s) continue;
        monotonic &= xOf(*s) >= last;
        linear &= std::abs(xOf(*s) - int(std::lround(g.renderTick() * 10))) <= 1;
        last = xOf(*s);
    }
    CHECK(monotonic);
    CHECK(linear); // tick 5's duplicate did not move it to 999; 8 -> 10 interpolated over the lost 9
}

void testCuts() {
    // a teleport (a jump over kSnapDistance): no interpolation across it, the cut at the new state's tick
    GhostBuffer g;
    steady(g, 0, 10);
    g.push(11 * kTickNs, 11, at(50'000));
    g.push(12 * kTickNs, 12, at(50'010));
    g.sample(12 * kTickNs); // render point 9
    bool between = false;
    for (uint64_t now = 12 * kTickNs; now < 15 * kTickNs; now += kTickNs / 4) {
        const auto s = g.sample(now);
        if (s && xOf(*s) > 110 && xOf(*s) < 50'000) between = true;
    }
    CHECK(!between);
    // a room change cuts too
    GhostBuffer r;
    steady(r, 0, 10);
    RemotePlayerState other = at(110);
    other.room.sub = 3;
    r.push(11 * kTickNs, 11, other);
    r.push(12 * kTickNs, 12, other);
    bool mixed = false;
    for (uint64_t now = 12 * kTickNs; now < 15 * kTickNs; now += kTickNs / 4) {
        const auto s = r.sample(now);
        if (!s) continue;
        // before the cut: the old room at its own positions; after it: the new room
        if (s->room.sub == 2 && xOf(*s) > 100) mixed = true;
    }
    CHECK(!mixed);
    CHECK(r.sample(16 * kTickNs)->room.sub == 3);
}

void testExtrapolateFreezeTimeout() {
    GhostBuffer g;
    steady(g, 0, 20);
    g.sample(20 * kTickNs);
    // the sender stops at 20 (x 200): the render point passes it, the position goes on kMaxExtrapolateTicks (x 240),
    // then freezes
    int maxX = 0, lastX = -1, frozenSamples = 0;
    for (uint64_t now = 20 * kTickNs; now < 40 * kTickNs; now += kTickNs / 2) {
        const auto s = g.sample(now);
        CHECK(s.has_value());
        if (!s) continue;
        maxX = std::max(maxX, xOf(*s));
        frozenSamples = xOf(*s) == lastX ? frozenSamples + 1 : 0;
        lastX = xOf(*s);
    }
    std::printf("  extrapolated to x %d (newest 200), render tick %.2f\n", maxX, g.renderTick());
    CHECK(maxX == 240);
    CHECK(frozenSamples >= 10);
    // silent for kSilentRemoveNs since its last arrival (tick 20): removed
    CHECK(g.sample(20 * kTickNs + od::ghost::kSilentRemoveNs - 1).has_value());
    CHECK(!g.sample(20 * kTickNs + od::ghost::kSilentRemoveNs));
    CHECK(g.empty());
    // and a new stream starts afresh
    g.push(100 * kTickNs, 500, at(5));
    const auto s = g.sample(100 * kTickNs);
    CHECK(s && xOf(*s) == 5);
}

void testRateEstimate() {
    // a sender under water (pace 3: ~20 ticks / s): the render point follows its rate, no drift past the newest
    constexpr uint64_t kSlowNs = 50'250'000;
    GhostBuffer g;
    double worst = 0;
    for (int k = 0; k <= 200; ++k) {
        g.push(uint64_t(k) * kSlowNs, uint32_t(k), at(k));
        if (g.sample(uint64_t(k) * kSlowNs) && k > 20) worst = std::max(worst, std::abs(g.renderTick() - (k - 3.0)));
    }
    std::printf("  20 ticks / s sender: render point within %.2f ticks of newest - 3\n", worst);
    CHECK(worst < 1.0);
}

} // namespace

int main() {
    testInterpolation();
    testSteady();
    testOrderingDuplicatesLoss();
    testCuts();
    testExtrapolateFreezeTimeout();
    testRateEstimate();
    return testResult("test_ghost_buffer");
}
