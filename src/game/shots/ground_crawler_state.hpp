// The class states of ground_crawler.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/shots/shot_common.hpp"
#include "game/field_alias.hpp"

namespace od::port::ground_crawler_state {

// Its block +0xbc: the stats, the turn rate and its step
struct CrawlerFields : ActorClassState {
    explicit CrawlerFields(ShotActor&) {}
    ShotStats stats;
    uint16_t turnRate = 0;
    uint16_t turnStep = 0;
};

} // namespace od::port::ground_crawler_state
