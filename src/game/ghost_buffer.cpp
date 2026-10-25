// The smoothing of a remote player's states (see ghost_buffer.hpp).
#include "game/ghost_buffer.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>

namespace od::ghost {

namespace {

constexpr int kAngleBits = 0x1000; // the game's 12-bit angles
constexpr int kHalfTurn = kAngleBits / 2;
constexpr double kNsPerSecond = 1e9;
constexpr uint64_t kRateWindowNs = 1'000'000'000ull; // the arrivals the tick rate is estimated from
constexpr uint64_t kMinRateSpanNs = 200'000'000ull;  // ... once they span this much

// a + (b - a) * f along the shorter way round the 12-bit circle, in a's representation (no masking: the pose reads
// the angle the way the game holds it)
int16_t lerpAngle(int16_t a, int16_t b, double f) {
    int d = (int(b) - int(a)) % kAngleBits;
    if (d > kHalfTurn) d -= kAngleBits;
    if (d < -kHalfTurn) d += kAngleBits;
    return int16_t(int(a) + int(std::lround(d * f)));
}

int32_t lerp(int32_t a, int32_t b, double f) {
    return int32_t(int64_t(a) + int64_t(std::llround(double(int64_t(b) - int64_t(a)) * f)));
}

// the two states are one continuous movement (same room and play state, no jump over kSnapDistance)
bool continuous(const od::port::RemotePlayerState& a, const od::port::RemotePlayerState& b) {
    if (!(a.room == b.room)) return false;
    for (int i = 0; i < 3; ++i)
        if (std::llabs(int64_t(b.snapshot.pos[i]) - int64_t(a.snapshot.pos[i])) > kSnapDistance) return false;
    return true;
}

} // namespace

od::port::RemotePlayerState interpolate(const od::port::RemotePlayerState& a, const od::port::RemotePlayerState& b,
                                        double f) {
    od::port::RemotePlayerState out = f < 0.5 ? a : b; // the discrete fields: the nearer state
    od::port::PlayerSnapshot& s = out.snapshot;
    const od::port::PlayerSnapshot &sa = a.snapshot, &sb = b.snapshot;
    for (int i = 0; i < 3; ++i) s.pos[i] = lerp(sa.pos[i], sb.pos[i], f);
    s.pitch = lerpAngle(sa.pitch, sb.pitch, f);
    s.yaw = lerpAngle(sa.yaw, sb.yaw, f);
    s.yawOfs = lerpAngle(sa.yawOfs, sb.yawOfs, f);
    s.lean = lerpAngle(sa.lean, sb.lean, f);
    s.lean2 = lerpAngle(sa.lean2, sb.lean2, f);
    return out;
}

void GhostBuffer::push(uint64_t nowNs, uint32_t tick, const od::port::RemotePlayerState& s) {
    const auto at = std::lower_bound(states_.begin(), states_.end(), tick,
                                     [](const Held& h, uint32_t t) { return h.tick < t; });
    if (at != states_.end() && at->tick == tick) return; // a duplicate
    states_.insert(at, {tick, nowNs, s});
    while (states_.size() > kBufferStates) states_.pop_front();
    lastArrivalNs_ = std::max(lastArrivalNs_, nowNs);
}

void GhostBuffer::clear() { *this = GhostBuffer{}; }

double GhostBuffer::ticksPerNs() const {
    // the newest and the oldest arrival of the last kRateWindowNs (by tick order, arrivals are nearly monotonic)
    const Held& last = states_.back();
    const Held* first = &last;
    for (auto it = states_.rbegin(); it != states_.rend(); ++it) {
        if (last.arrivalNs - std::min(last.arrivalNs, it->arrivalNs) > kRateWindowNs) break;
        first = &*it;
    }
    const uint64_t span = last.arrivalNs - std::min(last.arrivalNs, first->arrivalNs);
    if (span < kMinRateSpanNs || last.tick == first->tick) return kNominalTicksPerSecond / kNsPerSecond;
    return double(last.tick - first->tick) / double(span);
}

std::optional<od::port::RemotePlayerState> GhostBuffer::sample(uint64_t nowNs) {
    if (states_.empty()) return std::nullopt;
    if (nowNs - std::min(nowNs, lastArrivalNs_) >= kSilentRemoveNs) {
        clear();
        return std::nullopt;
    }
    const double target = double(states_.back().tick) - kInterpolationDelayTicks;
    if (!clockSet_) {
        clockSet_ = true;
        renderTick_ = target;
    } else {
        const double previous = renderTick_;
        const double newest = double(states_.back().tick);
        renderTick_ += double(nowNs - std::min(nowNs, lastSampleNs_)) * ticksPerNs();
        const double error = target - renderTick_;
        if (std::abs(error) > kResyncTicks) renderTick_ = target; // a cut: the sender paused, restarted or skipped
        else if (renderTick_ <= newest) renderTick_ = std::max(previous, renderTick_ + kSteering * error); // forward
        // past the newest state (a gap) the clock runs on to the extrapolation limit and stops there
        renderTick_ = std::min(renderTick_, newest + kMaxExtrapolateTicks);
    }
    lastSampleNs_ = nowNs;
    const double t = renderTick_;
    // the states before the shown instant are dropped but the last two at or before it (the movement that an
    // extrapolation continues)
    while (states_.size() > 2 && double(states_[2].tick) <= t) states_.pop_front();
    if (t <= double(states_.front().tick)) return states_.front().state; // before every state held: the oldest
    const size_t i = states_.size() > 1 && double(states_[1].tick) <= t ? 1 : 0; // the last state at or before t
    const Held& a = states_[i];
    if (i + 1 < states_.size()) {
        const Held& b = states_[i + 1];
        if (!continuous(a.state, b.state)) return a.state; // a cut at b's tick
        return interpolate(a.state, b.state, (t - double(a.tick)) / double(b.tick - a.tick));
    }
    // past the newest state (a gap): the position goes on with the last movement at most kMaxExtrapolateTicks, then
    // stays there (frozen until a state arrives); without a movement (one state, or a cut before it) it holds
    od::port::RemotePlayerState out = a.state;
    if (i == 0 || !continuous(states_[0].state, a.state)) return out;
    const Held& p = states_[0];
    const double ahead = std::min(t - double(a.tick), kMaxExtrapolateTicks) / double(a.tick - p.tick);
    for (int k = 0; k < 3; ++k) {
        const int64_t from = a.state.snapshot.pos[k], step = from - int64_t(p.state.snapshot.pos[k]);
        out.snapshot.pos[k] = int32_t(std::clamp<int64_t>(from + std::llround(double(step) * ahead), INT32_MIN, INT32_MAX));
    }
    return out;
}

} // namespace od::ghost
