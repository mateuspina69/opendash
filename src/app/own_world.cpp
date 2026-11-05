// The windowed game as a client of a world of its own (see own_world.hpp).
#include "app/own_world.hpp"

#include <SDL3/SDL.h>

#include "game/actor_sweeps.hpp"
#include "game/player/player_snapshot.hpp"
#include "game/player/player_state.hpp"

namespace od::app {

OwnWorldClient::OwnWorldClient(std::filesystem::path exe, std::filesystem::path storySave)
    : host_(std::move(exe), std::move(storySave)) {}

std::vector<uint8_t> OwnWorldClient::playerState() const {
    const od::port::RemotePlayerState s{od::port::ghostRoomOf(), 0, od::port::capturePlayerSnapshot(od::port::player())};
    return od::port::encodePlayerState(s);
}

void OwnWorldClient::roomBound(int stage, int sub) {
    od::port::killAllActors();
    actors_ = std::make_unique<od::port::PoolNetActors>(true);
    state_ = std::make_unique<od::port::NetStateClient>(*actors_);
    clock_ = std::make_unique<od::port::NetRenderClock>();
    shownAny_ = false;
    room_ = WorldRoom{stage, sub};
    host_.enter(kPlayerId, *room_, playerState());
    host_.netJoin(kPlayerId);
}

void OwnWorldClient::turn(uint64_t nowNs) {
    if (!room_) return;
    host_.state(kPlayerId, playerState());
    host_.poll(nowNs);
    const auto acknowledge = [&](std::optional<uint16_t> sequence) {
        if (sequence) host_.netAck(kPlayerId, *sequence);
    };
    for (const WorldMessage& m : host_.takeNet(kPlayerId)) {
        if (m.type == WorldMessageType::NetEvents) {
            if (!state_->applyEvents(m.netState)) SDL_Log("own world: a bad event batch");
            acknowledge(state_->applyHeld());
        } else if (m.type == WorldMessageType::NetUpdate) {
            acknowledge(state_->applyUpdate(m.netState));
            if (state_->heard()) clock_->heard(state_->newestTick());
        }
    }
    if (clock_->running()) {
        const int64_t at = int64_t(clock_->at(nowNs));
        while (at >= 0 && (!shownAny_ || int64_t(shown_) < at)) {
            shown_ = shownAny_ ? shown_ + 1 : uint32_t(at);
            shownAny_ = true;
            state_->advance(shown_);
        }
        actors_->endTick();
    }
    for (; failuresLogged_ < failures_.classes.size(); ++failuresLogged_)
        SDL_Log("own world: failed: %s", failures_.classes[failuresLogged_].c_str());
}

void OwnWorldClient::localActors(const od::port::PlayerEnv& env, od::port::ActorContext& context, bool late) {
    od::port::runNetLocalActors(env, context, late, failures_);
}

} // namespace od::app
