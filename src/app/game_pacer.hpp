#pragma once
// The PC exe's frame pacer (WinMain 0x54a0b0's loop 0x54a330), on the host clock instead of GetTickCount:
//   FUN_00549c00 (loop start): deadline 0xa5a500 = now + FRAME_INTERVAL_TABLE[sel] * step 0xa5a510
//   each iteration: the game frame runs only while now < deadline (a late iteration drops it), then
//   FUN_0054aa80(pace 0x6d0005) waits until now >= deadline; step = pace (0 -> 1); deadline += table[sel] * step
//   0x54a4ec (after the iteration): now - prev 0xa5a50c > 1000 -> prev = prev + 1000 < now ? now : prev + 1000,
//   sel 0xa5a530 = (sel + 1) & 3
// FRAME_INTERVAL_TABLE (0x8afaa8) = 16, 17, 17, 17 ms: one game frame every 33.5 ms on average at pace 2 (29.85 / s),
// 50.25 ms at pace 3 (19.90 / s) -- not 30 / 20 per second. The cutscenes' face rows run on these frames against
// voice tracks that play in real time. The host checks the deadline every rendered frame, so a game frame runs when
// it is due (not on a 30 Hz grid) and the drawn frames blend over its real interval (phase).
#include <cstdint>

namespace od::app {

class GamePacer {
public:
    void tick(uint64_t ns) { clockNs_ += ns; }
    // true: a new iteration starts now and runs its game frame (call again: several can be due after a long frame)
    bool next(int pace);
    // where the clock is in the current frame's interval (0 at its start, 1 at the next deadline): the blend of the
    // last two game frames
    float phase() const;

private:
    static constexpr uint32_t kIntervalMs[4] = {16, 17, 17, 17};
    void advanceDeadline(uint64_t at);
    uint64_t clockNs_ = 0;
    uint64_t deadlineMs_ = 0, prevMs_ = 0, startMs_ = 0;
    uint32_t sel_ = 0, step_ = 1;
    bool started_ = false;
};

} // namespace od::app
