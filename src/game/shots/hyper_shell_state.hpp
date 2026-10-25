// The class states of hyper_shell.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/shots/shot_common.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::hyper_shell_state {

constexpr uint32_t kTrailPoints = 8;

// One point of the trail: where it is and its age (its sprite's cell)
struct TrailPoint {
    ShortVec3 point;
    int16_t age = 0;
};

// Its block +0xbc: the stats, the homing's delay, the lock-on flag at the shot, the after-blasts' ticks and half, the
// closest distance to the aim, the trail
struct ShellFields : ActorClassState {
    explicit ShellFields(ShotActor&) {}
    ShotStats stats;
    uint16_t homingDelay = 0;
    uint16_t locked = 0;
    uint16_t blastTicks = 0;
    uint16_t blastHalf = 0;
    uint32_t closest = 0;
    uint32_t trail = 0; // the dark trail's static slot
};

// The trail's fields: the shell it follows (the spawn's parameter), the points drawn, the newest point, the ring of
// points. Its own position collects the moves the in-stage warps give it (it is kept through them), added to the points
struct DarkTrailFields : ActorClassState {
    explicit DarkTrailFields(StaticActor& actor) : parent(actor.params, 0), length(actor.wallHit, 0), head(actor.wallHit, 2) {}
    BytesField<uint32_t, 4> parent;
    PartField<int16_t, uint32_t> length;
    PartField<int16_t, uint32_t> head;
    StateArray<TrailPoint, kTrailPoints> points{};
};

} // namespace od::port::hyper_shell_state
