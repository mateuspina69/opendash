#include "app/game_pacer.hpp"

#include <algorithm>

namespace od::app {

// FUN_0054aa80's deadline step + the selector check of 0x54a4ec at the end of the iteration (time `at`)
void GamePacer::advanceDeadline(uint64_t at) {
    startMs_ = deadlineMs_;
    deadlineMs_ += uint64_t(kIntervalMs[sel_]) * step_;
    if (at - prevMs_ > 1000) {
        const uint64_t next = prevMs_ + 1000;
        prevMs_ = next < at ? at : next;
        sel_ = (sel_ + 1) & 3;
    }
}

bool GamePacer::next(int pace) {
    const uint64_t now = clockNs_ / 1'000'000; // GetTickCount (ms)
    if (!started_) {                           // FUN_00549c00: the first iteration runs at once
        started_ = true;
        prevMs_ = startMs_ = now;
        deadlineMs_ = now + uint64_t(kIntervalMs[sel_]) * step_;
        return true;
    }
    if (now < deadlineMs_) return false; // FUN_0054aa80 still waiting
    step_ = pace > 0 ? uint32_t(pace) : 1u;
    advanceDeadline(now);
    // a late host (a long frame): the exe's iterations that find now >= deadline run no game frame
    while (now >= deadlineMs_) advanceDeadline(now);
    return true;
}

float GamePacer::phase() const {
    const uint64_t span = deadlineMs_ - startMs_;
    if (!started_ || span == 0) return 1.0f;
    const double t = (double(clockNs_) / 1'000'000.0 - double(startMs_)) / double(span);
    return float(std::clamp(t, 0.0, 1.0));
}

} // namespace od::app
