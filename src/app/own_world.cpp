// The windowed game as a client of a world of its own (see own_world.hpp).
#include "app/own_world.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

#include "game/actor_sweeps.hpp"
#include "game/hit_list.hpp"
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
    // the room setup's actors go, but what a client keeps
    od::port::StaticPool& statics = od::port::staticPool();
    for (size_t i = 0; i < od::port::StaticPool::size(); ++i)
        if ((statics[i].flags & od::port::kActorLive) && !od::port::netClientKeepsActor(statics[i])) od::port::freeActor(statics[i]);
    for (size_t i = 0; i < od::port::kAnimatedFieldSlots; ++i)
        if (od::port::animatedPool()[i].flags & od::port::kActorLive) od::port::freeActor(od::port::animatedPool()[i]);
    for (size_t i = 0; i < od::port::ProjectilePool::size(); ++i)
        if (od::port::projectilePool()[i].flags & od::port::kActorLive) od::port::freeActor(od::port::projectilePool()[i]);
    for (size_t i = 0; i < od::port::EffectPool::size(); ++i)
        if (od::port::effectPool()[i].flags & od::port::kActorLive) od::port::freeActor(od::port::effectPool()[i]);
    for (size_t i = 0; i < od::port::ControllerPool::size(); ++i)
        if (od::port::controllerPool()[i].flags & od::port::kActorLive) od::port::freeActor(od::port::controllerPool()[i]);
    od::port::applyActorFrees();
    actors_ = std::make_unique<od::port::PoolNetActors>(true);
    state_ = std::make_unique<od::port::NetStateClient>(*actors_);
    clock_ = std::make_unique<od::port::NetRenderClock>();
    shownAny_ = false;
    room_ = WorldRoom{stage, sub};
    host_.enter(kPlayerId, *room_, playerState());
    host_.netJoin(kPlayerId);
    if (guest_) host_.enter(kGuestId, *room_, playerState());
    uplink_ = {};
    guestUplink_ = {};
    for (Other& o : others_) o = Other{};
    ownShots_ = {};
    reported_ = {};
}

void OwnWorldClient::turn(uint64_t nowNs) {
    if (!room_) return;
    // a tick never runs ahead of the world's clock (a scripted run renders as fast as it can)
    constexpr uint64_t kTickNs = 1'000'000'000ull / 30;
    nextTickNs_ = std::max(nextTickNs_ + kTickNs, nowNs);
    if (nextTickNs_ > nowNs) {
        SDL_DelayNS(nextTickNs_ - nowNs);
        nowNs = nextTickNs_;
    }
    ++localTick_;
    const od::port::NetPlayerFields fields = watch_.see(od::port::player(), localTick_);
    if (localTick_ % od::port::kNetDefaultUpdateTicks == 0) {
        host_.netSendLatest(kPlayerId, WorldMessageType::NetPlayer, uplink_.update(kPlayerId, localTick_, fields));
        if (guest_) {
            od::port::NetPlayerFields shifted = fields;
            shifted.pos[0] = int16_t(shifted.pos[0] + guest_->first);
            shifted.pos[2] = int16_t(shifted.pos[2] + guest_->second);
            host_.netSendLatest(kGuestId, WorldMessageType::NetPlayer, guestUplink_.update(kGuestId, localTick_, shifted));
        }
    }
    host_.poll(nowNs);
    if (guest_) (void)host_.takeNet(kGuestId);
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
        } else if (m.type == WorldMessageType::NetPlayerAck) {
            if (m.netState.size() == 3) uplink_.acknowledge(uint16_t(m.netState[1] | m.netState[2] << 8));
        } else if (m.type == WorldMessageType::NetPlayer) {
            const auto id = od::port::netPlayerIdOf(m.netState);
            if (!id || *id >= others_.size()) continue;
            Other& o = others_[*id];
            const auto heard = o.from.apply(m.netState);
            if (!heard) continue;
            std::vector<uint8_t> ack = {*id, uint8_t(heard->sequence), uint8_t(heard->sequence >> 8)};
            host_.netSendLatest(kPlayerId, WorldMessageType::NetPlayerAck, std::move(ack));
            // the fields of this tick in the smoothing buffer: its frames are the client's, set when shown
            od::port::RemotePlayerState s;
            s.room = od::port::ghostRoomOf();
            s.snapshot = od::port::netPlayerSnapshot(o.from.fields(), o.tracks);
            o.buffer.push(nowNs, heard->tick, s);
        } else if (m.type == WorldMessageType::NetHit) {
            if (const auto hit = od::port::decodeHitResult(m.netState)) od::port::applyPlayerHit(od::port::player(), *hit);
        } else if (m.type == WorldMessageType::NetShotHit) {
            const auto hit = od::port::decodeShotHit(m.netState);
            if (hit && hit->clientSlot < ownShots_.size() && ownShots_[hit->clientSlot].live &&
                ownShots_[hit->clientSlot].serial == hit->serial)
                od::port::applyShotHit(od::port::shotPool()[hit->clientSlot], hit->hit);
        }
    }
    const od::port::NetPlayerReport report = od::port::netPlayerReportOf(od::port::player());
    if (report != reported_) {
        reported_ = report;
        host_.netSend(kPlayerId, WorldMessageType::NetReport, od::port::encode(report));
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

void OwnWorldClient::showPlayers(GhostView& ghosts, uint64_t localFingerprint, uint64_t nowNs) {
    for (size_t id = 0, slot = 0; id < others_.size() && slot < size_t(od::ghost::kMaxGhosts); ++id) {
        Other& o = others_[id];
        if (o.buffer.empty()) continue;
        auto shown = o.buffer.sample(nowNs);
        if (!shown) {
            ghosts.clearRemote(int(slot++));
            continue;
        }
        // the animation is played here, from the slot's own files, up to the shown tick
        const int64_t tick = int64_t(o.buffer.renderTick());
        if (od::LoadedFileData* files = ghosts.files(int(slot))) {
            const od::ScopedLoadedFiles bound(*files);
            const od::port::NetPlayerFields& f = o.from.fields();
            for (int64_t t = o.stepped < 0 ? tick : o.stepped + 1; t <= tick; ++t)
                for (size_t c = 0; c < 2; ++c) o.tracks[c].step(f.channels[c], f.stageAnims, uint32_t(t));
            o.stepped = tick;
        }
        const od::port::PlayerSnapshot played = od::port::netPlayerSnapshot(o.from.fields(), o.tracks);
        for (size_t c = 0; c < 2; ++c) {
            shown->snapshot.animId[c] = played.animId[c];
            shown->snapshot.animStep[c] = played.animStep[c];
            shown->snapshot.animFrame[c] = played.animFrame[c];
            shown->snapshot.blend[c * 2] = played.blend[c * 2];
            shown->snapshot.blend[c * 2 + 1] = played.blend[c * 2 + 1];
        }
        shown->fingerprint = localFingerprint;
        ghosts.setRemote(int(slot++), *shown);
    }
}

void OwnWorldClient::addGuest(int dx, int dz) { guest_ = std::make_pair(dx, dz); }

void OwnWorldClient::endFrame() {
    if (!room_) return;
    actors_->submitLockPoints();
    od::port::hitListLockOnOnly();
    for (size_t i = 0; i < ownShots_.size(); ++i) {
        const od::port::ShotActor& shot = od::port::shotPool()[i];
        const bool mine = (shot.flags & od::port::kActorLive) && (shot.runFlags & od::port::kActorRun);
        if (mine && !ownShots_[i].live) {
            ownShots_[i] = {true, nextSerial_++};
            host_.netSend(kPlayerId, WorldMessageType::NetFire, od::port::encode(od::port::netFireOf(shot, uint8_t(i), ownShots_[i].serial)));
        } else if (!mine) {
            ownShots_[i].live = false;
        }
    }
}

void OwnWorldClient::localActors(const od::port::PlayerEnv& env, od::port::ActorContext& context, bool late) {
    od::port::runNetLocalActors(env, context, late, failures_);
    if (!late && actors_) actors_->submitSolids();
}

} // namespace od::app
