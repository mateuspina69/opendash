// How unevenly a stream's states arrive: each one's tick against the local time it was taken at, over the last seconds, with
// the sender's own rate taken out. What a receiver shows that far in the past has a state to show on every one of its ticks.
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>

namespace od::port {

class ArrivalJitter {
public:
    // a state of this tick, taken at nowNs (several taken at once, or one after a newer one: those were late)
    void heard(uint32_t tick, uint64_t nowNs) {
        window_.push_back({nowNs, tick});
        while (window_.size() > 2 && nowNs > window_.front().ns && nowNs - window_.front().ns > kWindowNs) window_.pop_front();
    }
    // the stream's ticks per nanosecond over its last spanNs of arrivals (`otherwise` before they cover minSpanNs)
    double rate(uint64_t spanNs, uint64_t minSpanNs, double otherwise) const {
        if (window_.empty()) return otherwise;
        const Sample& last = window_.back();
        const Sample* first = &last;
        for (auto it = window_.rbegin(); it != window_.rend(); ++it) {
            if (last.ns - std::min(last.ns, it->ns) > spanNs) break;
            first = &*it;
        }
        const uint64_t span = last.ns - std::min(last.ns, first->ns);
        if (span < minSpanNs || int32_t(last.tick - first->tick) <= 0) return otherwise;
        return double(last.tick - first->tick) / double(span);
    }
    // the spread of the arrivals, in the stream's ticks (before enough has arrived: one tick)
    double ticks() const {
        if (window_.size() < kMinSamples) return 1.0;
        // the stream's rate: the line that fits the window's arrivals best (a late state at either end does not tilt it)
        const Sample& first = window_.front();
        double sumT = 0.0, sumK = 0.0, sumTT = 0.0, sumTK = 0.0;
        for (const Sample& s : window_) {
            const double t = double(s.ns - std::min(s.ns, first.ns)), k = double(int32_t(s.tick - first.tick));
            sumT += t;
            sumK += k;
            sumTT += t * t;
            sumTK += t * k;
        }
        const double n = double(window_.size()), spread = n * sumTT - sumT * sumT;
        if (spread <= 0.0) return 1.0;
        const double rate = (n * sumTK - sumT * sumK) / spread;
        double low = 0.0, high = 0.0;
        for (const Sample& s : window_) {
            const double ahead = double(int32_t(s.tick - first.tick)) - double(s.ns - std::min(s.ns, first.ns)) * rate;
            low = std::min(low, ahead);
            high = std::max(high, ahead);
        }
        return high - low;
    }

private:
    static constexpr uint64_t kWindowNs = 3'000'000'000ull;
    static constexpr size_t kMinSamples = 8;
    struct Sample {
        uint64_t ns = 0;
        uint32_t tick = 0;
    };
    std::deque<Sample> window_;
};

} // namespace od::port
