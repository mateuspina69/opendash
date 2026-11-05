// The windowed game as a client of a world of its own (see own_world.hpp).
#include "app/own_world.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

#include "game/actor_sweeps.hpp"
#include "game/event.hpp"
#include "game/game_state.hpp"
#include "game/global_state.hpp"
#include "game/pickups.hpp"
#include "game/player/player_interaction.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_status.hpp"
#include "game/player/player_weapons.hpp"
#include "game/player/remote_player.hpp"
#include "game/player/player_snapshot.hpp"
#include "game/player/player_state.hpp"

namespace od::app {

OwnWorldClient::OwnWorldClient(std::filesystem::path exe, std::filesystem::path storySave) {
    auto coordinator = std::make_unique<CoopHost>(std::move(exe), std::move(storySave));
    coordinator_ = coordinator.get();
    pipe_ = std::move(coordinator);
    // Use pressed by nothing of this machine's: the world looks among its actors
    od::port::setUseNothingHook([this] {
        if (room_ && joined_) pipe_->sendReliable(playerId_, WorldMessageType::NetUse, {});
    });
}

void OwnWorldClient::useSession(od::net::Session* session, bool host) {
    if (!session) return;
    if (host) {
        coordinator_->useSession(session);
        return;
    }
    coordinator_ = nullptr;
    pipe_ = std::make_unique<SessionPipe>(*session);
}

std::optional<std::vector<uint8_t>> OwnWorldClient::takeHostSave(const std::vector<uint8_t>& current) {
    if (!hostSaveWhole_) return std::nullopt;
    hostSaveWhole_ = false;
    if (hostSave_ == current) return std::nullopt;
    SDL_Log("own world: the host's save differs: the game restarts from it");
    return hostSave_;
}

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
    for (uint8_t id = 1; id < od::port::kPlayerSlots; ++id)
        if (od::port::playerIsRemote(id)) od::port::remotePlayerLeave(id);
    state_ = std::make_unique<od::port::NetStateClient>(*actors_);
    clock_ = std::make_unique<od::port::NetRenderClock>();
    shownAny_ = false;
    room_ = WorldRoom{stage, sub};
    joined_ = false;
    lastEquipment_.clear();
    uplink_ = {};
    guestUplink_ = {};
    for (Other& o : others_) o = Other{};
    ownShots_ = {};
    reported_ = {};
    ownEvent_ = false;
    od::port::globalWatchRebase();
}

void OwnWorldClient::joinRoom() {
    pipe_->enter(playerId_, *room_, playerState());
    pipe_->sendReliable(playerId_, WorldMessageType::NetJoin, {});
    if (guest_ && coordinator_) pipe_->enter(kGuestId, *room_, playerState());
    joined_ = true;
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
    pipe_->poll(nowNs);
    if (!joined_) {
        const std::optional<uint8_t> id = pipe_->playerId();
        if (!id) return; // the session has not welcomed this game yet
        playerId_ = *id;
        actors_->setPlayerMap(playerId_);
        joinRoom();
    }
    ++localTick_;
    if (testLevelTick_ != 0 && localTick_ == testLevelTick_) {
        od::port::player().weaponLevels[od::port::weaponLevelIndex(od::port::player().busterWeapon, od::port::WeaponStat::Attack)] = testLevel_;
        SDL_Log("own world: buster attack level set to %d at tick %u", int(testLevel_), localTick_);
    }
    const od::port::NetPlayerFields fields = watch_.see(od::port::player(), localTick_);
    if (localTick_ % od::port::kNetDefaultUpdateTicks == 0) {
        // an update after its length byte, as the world's lists of them
        const auto listed = [](std::vector<uint8_t> one) {
            one.insert(one.begin(), uint8_t(one.size()));
            return one;
        };
        pipe_->sendLatest(playerId_, WorldMessageType::NetPlayer, listed(uplink_.update(playerId_, localTick_, fields)));
        if (guest_ && coordinator_) {
            od::port::NetPlayerFields shifted = fields;
            shifted.pos[0] = int16_t(shifted.pos[0] + guest_->first);
            shifted.pos[2] = int16_t(shifted.pos[2] + guest_->second);
            pipe_->sendLatest(kGuestId, WorldMessageType::NetPlayer, listed(guestUplink_.update(kGuestId, localTick_, shifted)));
        }
    }
    if (std::vector<uint8_t> equipment = od::port::netEquipmentOf(od::port::player()); equipment != lastEquipment_) {
        lastEquipment_ = equipment;
        pipe_->sendReliable(playerId_, WorldMessageType::NetEquip, std::move(equipment));
    }
    pipe_->poll(nowNs);
    if (guest_ && coordinator_) (void)pipe_->take(kGuestId);
    for (const WorldMessage& m : pipe_->take(playerId_)) handle(m, nowNs);
    // what happened in this game goes to the session; not what a stage's load does
    if (const od::port::GameState& g = od::port::gameState(); int(g.stage) == room_->stage && g.gameMode() != od::port::GameMode::StageLoad) {
        if (const od::port::GlobalChange change = od::port::globalWatchTake(uint8_t(room_->stage)); !change.empty())
            pipe_->sendReliable(playerId_, WorldMessageType::GlobalChange, od::port::encode(change));
    }
    const od::port::NetPlayerReport report = od::port::netPlayerReportOf(od::port::player());
    if (report != reported_) {
        reported_ = report;
        pipe_->sendReliable(playerId_, WorldMessageType::NetReport, od::port::encode(report));
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

void OwnWorldClient::handle(const WorldMessage& m, uint64_t nowNs) {
    switch (m.type) {
    case WorldMessageType::NetEvents:
        if (!state_->applyEvents(m.netState)) SDL_Log("own world: a bad event batch");
        if (const auto sequence = state_->applyHeld()) {
            pipe_->sendLatest(playerId_, WorldMessageType::NetAck, {uint8_t(*sequence), uint8_t(*sequence >> 8)});
        }
        break;
    case WorldMessageType::NetUpdate:
        if (const auto sequence = state_->applyUpdate(m.netState))
            pipe_->sendLatest(playerId_, WorldMessageType::NetAck, {uint8_t(*sequence), uint8_t(*sequence >> 8)});
        if (state_->heard()) clock_->heard(state_->newestTick());
        break;
    case WorldMessageType::NetPlayerAck:
        if (m.netState.size() == 3) uplink_.acknowledge(uint16_t(m.netState[1] | m.netState[2] << 8));
        break;
    case WorldMessageType::NetPlayer: {
        std::vector<uint8_t> acks;
        for (size_t at = 0; at < m.netState.size();) {
            const size_t n = m.netState[at++];
            if (at + n > m.netState.size()) break;
            const std::span<const uint8_t> one(m.netState.data() + at, n);
            at += n;
            const auto id = od::port::netPlayerIdOf(one);
            if (!id || *id >= others_.size()) continue;
            Other& o = others_[*id];
            const auto heard = o.from.apply(one);
            if (!heard) continue;
            acks.push_back(*id);
            acks.push_back(uint8_t(heard->sequence));
            acks.push_back(uint8_t(heard->sequence >> 8));
            // the fields of this tick in the smoothing buffer: its frames are the client's, set when shown
            od::port::RemotePlayerState s;
            s.room = od::port::ghostRoomOf();
            s.snapshot = od::port::netPlayerSnapshot(o.from.fields(), o.tracks);
            o.buffer.push(nowNs, heard->tick, s);
        }
        if (!acks.empty()) pipe_->sendLatest(playerId_, WorldMessageType::NetPlayerAck, std::move(acks));
        break;
    }
    case WorldMessageType::NetHit:
        if (const auto hit = od::port::decodeHitResult(m.netState)) od::port::applyPlayerHit(od::port::player(), *hit);
        break;
    case WorldMessageType::NetShotHit: {
        const auto hit = od::port::decodeShotHit(m.netState);
        if (hit && hit->clientSlot < ownShots_.size() && ownShots_[hit->clientSlot].live && ownShots_[hit->clientSlot].serial == hit->serial)
            od::port::applyShotHit(od::port::shotPool()[hit->clientSlot], hit->hit);
        break;
    }
    case WorldMessageType::GlobalChange:
        if (const auto change = od::port::decodeGlobalChange(m.netState)) od::port::globalWatchApply(*change);
        break;
    case WorldMessageType::NetGift:
        if (const auto gift = od::port::decodeGift(m.netState)) {
            SDL_Log("own world: gift of kind %d, amount %d", int(gift->kind), int(gift->amount));
            od::port::pickupGiftArrived(od::port::player(), *gift);
        }
        break;
    case WorldMessageType::NetEventStart:
        if (const auto e = od::port::decodeOwnerEvent(m.netState)) {
            eventPoint_.x = e->x;
            eventPoint_.y = e->y;
            eventPoint_.z = e->z;
            od::port::eventStart(e->hasPos ? &eventPoint_ : nullptr, nullptr, e->param, e->window, e->message);
            ownEvent_ = true;
        }
        break;
    case WorldMessageType::NetSave:
        hostSave_.insert(hostSave_.end(), m.netState.begin(), m.netState.end());
        if (m.netState.size() < 1000) hostSaveWhole_ = true; // the last chunk is the short one
        break;
    default: break;
    }
}

// The other player in this machine's table: what the classes that follow them read (their bones, their arm)
void OwnWorldClient::otherPlayerRules(uint8_t id, Other& o, const od::port::PlayerSnapshot& shown) {
    const uint8_t table = actors_->tableOfPlayer(id);
    if (table >= od::port::kPlayerSlots || table == 0) return;
    if (!o.joined) {
        od::port::remotePlayerJoin(table, shown);
        o.joined = true;
    } else {
        od::port::remotePlayerState(table, shown);
    }
    od::port::Player& p = od::port::playerById(table);
    const od::port::NetPlayerFields& f = o.from.fields();
    p.status = uint16_t((p.status & od::port::kStatusMarkerMask) | f.status);
    p.specialFiring = int8_t(f.specialFiring);
    const od::port::ScopedActingPlayer acting(table);
    od::port::showStatusMarkers(p);
    od::port::showChargeMarker(p, f.charge);
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
        otherPlayerRules(uint8_t(id), o, shown->snapshot);
        ghosts.setRemote(int(slot++), *shown);
    }
}

void OwnWorldClient::addGuest(int dx, int dz) { guest_ = std::make_pair(dx, dz); }

void OwnWorldClient::endFrame() {
    if (!room_) return;
    if (ownEvent_) {
        od::port::EventState& ev = od::port::eventState();
        if (ev.speakerAnimation < od::port::kSpeakerAnimationCount) {
            pipe_->sendReliable(playerId_, WorldMessageType::NetSpeaker, {ev.speakerAnimation});
            ev.speakerAnimation = od::port::kNoSpeakerAnimation;
        }
        if (ev.active == 0) {
            pipe_->sendReliable(playerId_, WorldMessageType::NetEventEnd, {});
            ownEvent_ = false;
        }
    }
    actors_->submitLockPoints();
    od::port::hitListLockOnOnly();
    for (size_t i = 0; i < ownShots_.size(); ++i) {
        const od::port::ShotActor& shot = od::port::shotPool()[i];
        const bool mine = (shot.flags & od::port::kActorLive) && (shot.runFlags & od::port::kActorRun);
        if (mine && !ownShots_[i].live) {
            ownShots_[i] = {true, nextSerial_++};
            pipe_->sendReliable(playerId_, WorldMessageType::NetFire, od::port::encode(od::port::netFireOf(shot, uint8_t(i), ownShots_[i].serial)));
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
