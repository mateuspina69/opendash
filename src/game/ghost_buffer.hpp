// The smoothing of a remote player's states (spec "Smoothing"): the states arrive ~30 a second over a path of a few
// hundred ms round trip with jitter and loss; the ghost is shown a little in the past so there is usually a state on
// each side of the shown instant.
//
// Per player the buffer keeps the states ordered by the sender's game tick. The shown instant (the render point, in
// sender ticks) advances with the local clock at the sender's tick rate (estimated from the arrivals) and is steered
// toward the newest received tick minus kInterpolationDelayTicks. Position and the angles (12-bit, wrapping: yaw,
// its offset, pitch, the two leans) are interpolated between the states around it; the discrete fields (animation,
// look, flags, room) come from the nearer state. Past the newest state the position is extrapolated at most
// kMaxExtrapolateTicks, then the ghost freezes. A room change or a jump over kSnapDistance is shown as a cut (no
// interpolation across it). A player silent for kSilentRemoveNs is removed.
#pragma once

#include <cstdint>
#include <deque>
#include <optional>

#include "game/player/player_snapshot.hpp"

namespace od::ghost {

constexpr double kInterpolationDelayTicks = 3.0; // ~100 ms at the game's ~30 ticks / s
constexpr double kMaxExtrapolateTicks = 4.0;
constexpr int64_t kSnapDistance = int64_t(0x400) << 16; // game units (16.16): a jump this long between two states
constexpr uint64_t kSilentRemoveNs = 3'000'000'000ull;
constexpr double kResyncTicks = 8.0;   // the render point is put back on its target when this far from it
constexpr double kSteering = 0.1;      // the share of the error corrected per sample
constexpr double kNominalTicksPerSecond = 29.85; // the pace-2 rate (app/game_pacer) before the arrivals tell
constexpr size_t kBufferStates = 64;   // ~2 s of states

class GhostBuffer {
public:
    // a state received at nowNs (local clock); a tick already held is a duplicate and ignored
    void push(uint64_t nowNs, uint32_t tick, const od::port::RemotePlayerState& s);
    // the state to show at nowNs; nullopt when there is none (nothing received yet, or silent for kSilentRemoveNs:
    // then the buffer is cleared)
    std::optional<od::port::RemotePlayerState> sample(uint64_t nowNs);
    bool empty() const { return states_.empty(); }
    void clear();
    double renderTick() const { return renderTick_; } // the last sample's render point (sender ticks)

private:
    struct Held {
        uint32_t tick;
        uint64_t arrivalNs;
        od::port::RemotePlayerState state;
    };
    double ticksPerNs() const;

    std::deque<Held> states_; // by tick
    bool clockSet_ = false;
    double renderTick_ = 0.0;
    uint64_t lastSampleNs_ = 0;
    uint64_t lastArrivalNs_ = 0;
};

// the state between a and b at fraction f (0..1) of the way (pure: the buffer's interpolation rule)
od::port::RemotePlayerState interpolate(const od::port::RemotePlayerState& a, const od::port::RemotePlayerState& b,
                                        double f);

} // namespace od::ghost
