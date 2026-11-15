// How unevenly a stream's states arrive: each one's tick against the local time it was taken at, over the last seconds, with
// the sender's own rate taken out. What a receiver shows that far in the past has a state to show on every one of its ticks.
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>

namespace od::port {

class ArrivalJitter {
public:
    // a state of this tick, taken at nowNs (several taken at once: a burst, the earlier ones were late)
    void heard(uint32_t tick, uint64_t nowNs) {
        if (!window_.empty() && int32_t(tick - window_.back().tick) <= 0) return; // not newer: it tells nothing
        window_.push_back({nowNs, tick});
        while (window_.size() > 2 && nowNs - window_.front().ns > kWindowNs) window_.pop_front();
    }
    // the spread of the arrivals, in the stream's ticks (before enough has arrived: one tick)
    double ticks() const {
        if (window_.size() < kMinSamples || window_.back().ns == window_.front().ns) return 1.0;
        const Sample& first = window_.front();
        const double rate = double(window_.back().tick - first.tick) / double(window_.back().ns - first.ns);
        double low = 0.0, high = 0.0;
        for (const Sample& s : window_) {
            const double ahead = double(s.tick - first.tick) - double(s.ns - first.ns) * rate;
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
