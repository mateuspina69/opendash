// The windowed game as a client of a world of its own (see own_world.hpp).
#include "app/own_world.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>

#include "engine/config.hpp"
#include "game/save_image.hpp"
#include "game/camera_view.hpp"
#include "game/actor_sweeps.hpp"
#include "game/player/room_player_effects.hpp"
#include "game/fade_state.hpp"
#include "game/transition.hpp"
#include "game/hit_effects.hpp"
#include "game/story_flags.hpp"
#include "game/story_table.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/world_view.hpp"
#include "content/nino/st16_script33.hpp"
#include "game/player/player_input.hpp"
#include "game/distances.hpp"
#include "game/player/player_downed.hpp"
#include "game/transition_state.hpp"
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
    od::port::setPlayerDownsInstead(true);
    coordinator_->setGateTeleport(od::loadConfig().coopGate == "teleport");
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
    pipe_->setStays(stays_);
}

namespace {
constexpr uint32_t kReloadFade = 0x11; // to black, as the time trial's restart
constexpr uint8_t kStageFadeOut = 0x11, kStageFadeIn = 2;
} // namespace

std::optional<std::vector<uint8_t>> OwnWorldClient::takeReload() {
    if (coordinator_) {
        if (auto save = coordinator_->takeReload()) {
            SDL_Log("own world: everyone is down: the session goes on from the last save");
            reloading_ = true;
            reloadSave_ = std::move(*save);
            if (fade_) od::port::fadeStart(*fade_, kReloadFade, 0);
        }
    } else if (reloading_ && hostSaveWhole_) {
        reloadSave_ = hostSave_;
    }
    if (!reloading_ || reloadSave_.empty() || od::port::fadeState().busy != 0) return std::nullopt;
    reloading_ = false;
    hostSaveWhole_ = false;
    room_.reset();
    return std::exchange(reloadSave_, {});
}

std::optional<std::vector<uint8_t>> OwnWorldClient::takeHostSave(const std::vector<uint8_t>& current) {
    if (reloading_ || !hostSaveWhole_) return std::nullopt;
    hostSaveWhole_ = false;
    if (hostSave_ == current) return std::nullopt;
    SDL_Log("own world: the host's save differs: the game restarts from it");
    return hostSave_;
}

void OwnWorldClient::gameStarted(std::span<const uint8_t> image) {
    if (!coordinator_) return;
    coordinator_->hostLoaded(image, SDL_GetTicksNS());
    joined_ = false; // its world starts over: this game enters it again
}

std::optional<std::vector<uint8_t>> OwnWorldClient::takeHostSaveAway() {
    if (coordinator_ || !hostSaveWhole_ || hostSave_.size() != od::port::kSaveSize) return std::nullopt;
    hostSaveWhole_ = false;
    reloading_ = false;
    return hostSave_;
}

void OwnWorldClient::notice(const std::string& text) {
    SDL_Log("own world: %s", text.c_str());
    alert_ = text;
    alertUntilNs_ = SDL_GetTicksNS() + 10'000'000'000ull;
}

std::vector<uint8_t> OwnWorldClient::playerState() const {
    const od::port::RemotePlayerState s{od::port::ghostRoomOf(), 0, od::port::capturePlayerSnapshot(od::port::player())};
    return od::port::encodePlayerState(s);
}

void OwnWorldClient::roomBound(int stage, int sub) {
    if (warping_) { // a warp inside the stage: the room's world goes on; only this game's room moved
        warping_ = false;
        return;
    }
    if (localScene_) { // a room the scene's script went to: its actors are this game's, as in the normal game
        room_ = WorldRoom{stage, sub};
        return;
    }
    od::port::presentationLoopsClear();
    if (const auto at = std::exchange(doorArrival_, std::nullopt); at && (*at)[0] == stage && (*at)[1] == sub) {
        // through a gated door together: the door's script arrived at the exit's spot, the coordinator's is beside the others
        od::port::Player& p = od::port::player();
        p.pos.x.whole = p.prevPos.x.whole = (*at)[2];
        p.pos.y.whole = p.prevPos.y.whole = (*at)[3];
        p.pos.z.whole = p.prevPos.z.whole = (*at)[4];
    }
    if (od::port::storyStageRunsAlone(uint8_t(stage))) { // the race runs here, as in the game; out of the session's worlds
        if (room_ && joined_) pipe_->sendReliable(playerId_, WorldMessageType::NetLeave, {});
        room_.reset();
        joined_ = false;
        SDL_Log("own world: ST%02X runs on this machine alone", unsigned(stage));
        return;
    }
    bindWorld(stage, sub);
    // the room's first field tick runs its scripts as the normal game does (the room setup's actors still here): a scene
    // they start plays here from that tick; else the world takes the room over (roomScriptsRan)
    entering_ = true;
    od::port::setWorldIsElsewhere(false);
}

void OwnWorldClient::roomScriptsRan() {
    if (!entering_) return;
    entering_ = false;
    const od::port::ScriptState& s = od::port::scriptState();
    if ((s.flags & od::port::kScriptRunning) && s.id != 0) {
        startedHere(s.id);
        return;
    }
    freeLocalActors();
}

void OwnWorldClient::startedHere(uint8_t script) {
    localScene_ = true;
    scene_ = true;
    sceneOwner_ = 0; // the world's rule: the first player in the room, the host when here; the world's start says who
    scenePad_ = sentPad_ = 0;
    sceneRoom_ = room_;
    sceneEndAt_.reset();
    sceneHeard_ = false;
    // a scene that waits for everyone: held here as in the world, until the world starts it
    heldHere_ = !od::port::storyOptionalStage(uint8_t(room_->stage)) && od::port::storyScriptGated(script);
    if (heldHere_) od::port::setScriptsGated(true);
    SDL_Log("own world: the room starts a scene here (script %02x)%s", unsigned(script), heldHere_ ? ", held until everyone is in" : "");
}

void OwnWorldClient::freeLocalActors() {
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
}

void OwnWorldClient::bindWorld(int stage, int sub) {
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
    gateAsked_ = false;
    gateAtDoor_ = false;
    doorPass_ = false;
    bossFight_ = false;
    od::port::globalWatchRebase();
    scene_ = false;
    sceneHeard_ = false;
    if (std::exchange(heldHere_, false)) od::port::setScriptsGated(false);
}

void OwnWorldClient::joinRoom() {
    pipe_->enter(playerId_, *room_, playerState());
    pipe_->sendReliable(playerId_, WorldMessageType::NetJoin, {});
    if (guest_ && coordinator_) pipe_->enter(kGuestId, *room_, playerState());
    joined_ = true;
}

void OwnWorldClient::awayTurn(uint64_t nowNs) {
    pipe_->poll(nowNs);
    const std::optional<uint8_t> id = pipe_->playerId();
    if (!id) return;
    for (const WorldMessage& m : pipe_->take(*id))
        if (m.type == WorldMessageType::NetSave || m.type == WorldMessageType::NetReload || m.type == WorldMessageType::NetAlert) handle(m, nowNs);
}

void OwnWorldClient::turn(uint64_t nowNs) {
    if (!room_ || entering_) return; // entering: the room's first field tick decides first (roomScriptsRan)
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
    if (coordinator_ && testFlagTick_ != 0 && localTick_ == testFlagTick_) {
        coordinator_->testFlagNow(testFlag_, testFlagSet_, uint8_t(room_->stage));
        SDL_Log("own world: flag %03x %s at tick %u", unsigned(testFlag_), testFlagSet_ ? "set" : "cleared", localTick_);
    }
    if (testLifeTick_ != 0 && localTick_ == testLifeTick_) {
        od::port::Player& p = od::port::player();
        p.hitPoints = testLife_;
        if (testLife_ < 0) p.moveToSub(od::port::PlayerSub::KnockDown, od::port::KnockDownStep::Thrown);
    }
    od::port::NetPlayerFields fields = watch_.see(od::port::player(), localTick_);
    fields.camera = od::port::thisPlayerCamera(); // what the world counts as seen
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
            shifted.camera.target[0] = int16_t(shifted.camera.target[0] + guest_->first);
            shifted.camera.target[2] = int16_t(shifted.camera.target[2] + guest_->second);
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
    for (const od::port::PoolNetActors::Appeared& a : actors_->takeAppeared())
        SDL_Log("own world: actor %02x appeared: type %02x subtype %02x variant %02x model %02x%s", unsigned(a.slot),
                unsigned(a.identity.type), unsigned(a.identity.subtype), unsigned(a.identity.variant),
                unsigned(a.identity.modelEntry), a.bound ? "" : " (its model does not bind here)");
    for (const od::port::PoolNetActors::Started& s : actors_->takeStarted())
        SDL_Log("own world: %s of player %u (type %02x variant %02x)%s", s.pool == od::port::kShotClass ? "shot" : "effect",
                unsigned(s.owner), unsigned(s.type), unsigned(s.variant),
                s.parent == od::port::kNetNoParent ? ""
                                                   : (" on player " + std::to_string(s.parent) + ", here " + std::to_string(s.parentTable)).c_str());
    sceneTurn();
    localSceneTurn();
    if (!room_) return;
    // a scene's own room changes are the game's, as in the normal game; one held for everyone waits at the gate
    if (!localScene_ || heldHere_) gateTurn();
    od::port::presentationLoopsTick();
    // what happened in this game goes to the session; not what a stage's load does; in a scene the world does it all
    if (scene_) {
        od::port::globalWatchRebase();
    } else if (const od::port::GameState& g = od::port::gameState(); int(g.stage) == room_->stage && g.gameMode() != od::port::GameMode::StageLoad) {
        if (const od::port::GlobalChange change = od::port::globalWatchTake(uint8_t(room_->stage)); !change.empty())
            pipe_->sendReliable(playerId_, WorldMessageType::GlobalChange, od::port::encode(change));
    }
    const od::port::NetPlayerReport report = od::port::netPlayerReportOf(od::port::player());
    if (report != reported_) {
        reported_ = report;
        pipe_->sendReliable(playerId_, WorldMessageType::NetReport, od::port::encode(report));
    }
    if (clock_->running() && !localScene_) { // in a scene the room's actors are this game's own
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
    // a scene this game runs itself: what the world shows of its actors, its warps and its events are not this game's now
    // (the room is told all of it again once the scene is over)
    if (localScene_ && (m.type == WorldMessageType::NetEvents || m.type == WorldMessageType::NetUpdate ||
                        m.type == WorldMessageType::NetPresent || m.type == WorldMessageType::NetWarp ||
                        m.type == WorldMessageType::NetScreen || m.type == WorldMessageType::NetHold ||
                        m.type == WorldMessageType::NetEventStart))
        return;
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
    case WorldMessageType::NetEquip: // another player's equipment: u8 their id, then what they wear
        if (!m.netState.empty() && m.netState[0] < others_.size() && m.netState[0] != playerId_)
            others_[m.netState[0]].equipment.assign(m.netState.begin() + 1, m.netState.end());
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
            SDL_Log("own world: an event of the room's world, message %u", unsigned(e->message));
            ownEvent_ = true;
        }
        break;
    case WorldMessageType::NetRevived:
        if (od::port::playerIsDowned(od::port::player())) {
            SDL_Log("own world: revived");
            od::port::playerRevive(od::port::player(), int16_t(od::port::player().maxHitPoints / 2));
        }
        break;
    case WorldMessageType::NetRespawnAt:
        if (od::port::playerIsDowned(od::port::player()) && m.netState.size() > 2) {
            const auto brief = decodeBrief(std::span<const uint8_t>(m.netState).subspan(2));
            if (brief && brief->players.size() == 1) {
                const WorldBrief::Entry& e = brief->players[0];
                od::port::Player& p = od::port::player();
                SDL_Log("own world: back next to a teammate, ST%02X sub %d", unsigned(m.netState[0]), int(m.netState[1]));
                od::port::playerRevive(p, 0); // the game's danger state: alive, the next hit downs them
                constexpr int kBeside = 0x80;
                const int16_t x = int16_t(e.x + kBeside);
                if (m.netState[0] == uint8_t(room_->stage) && m.netState[1] == uint8_t(room_->sub)) {
                    p.pos.x = int32_t(uint32_t(int32_t(x)) << 16);
                    p.pos.y = int32_t(uint32_t(int32_t(e.y)) << 16);
                    p.pos.z = int32_t(uint32_t(int32_t(e.z)) << 16);
                    p.prevPos = p.pos;
                } else {
                    od::port::requestTransition(od::port::kTransitionAccepted, m.netState[0], m.netState[1], x, e.y, e.z, e.yaw, 2, 0x11);
                }
            }
        }
        break;
    case WorldMessageType::NetReload:
        SDL_Log("own world: everyone is down: the session goes on from the host's last save");
        reloading_ = true;
        hostSave_.clear();
        hostSaveWhole_ = false;
        if (fade_) od::port::fadeStart(*fade_, kReloadFade, 0);
        break;
    case WorldMessageType::NetPresent:
        if (fade_) od::port::applyPresentation(m.netState, *fade_, playerId_, scenePad_);
        break;
    case WorldMessageType::NetSceneStart:
        if (m.netState.size() < 2) break;
        if (localScene_) { // started here on the room's first tick: the world's start confirms it and names its owner
            if (sceneHeard_) break;
            sceneHeard_ = true;
            sceneOwner_ = m.netState[0];
            if (std::exchange(heldHere_, false)) { // everyone is in: it plays from its start, with the room as it began
                od::port::setScriptsGated(false);
                sceneStart_ = m.netState[1];
            }
            SDL_Log("own world: the world starts the scene too (script %02x), its owner is player %u", unsigned(m.netState[1]),
                    unsigned(sceneOwner_));
            break;
        }
        scene_ = true;
        sceneOwner_ = m.netState[0];
        scenePad_ = sentPad_ = 0;
        localScene_ = true;
        sceneHeard_ = true;
        sceneStart_ = m.netState[1];
        sceneRoom_ = room_;
        sceneEndAt_.reset();
        od::port::presentationLoopsClear(); // the world's sounds: the scene's are this game's own
        SDL_Log("own world: a scene starts (script %02x), its owner is player %u: it runs here", unsigned(m.netState[1]),
                unsigned(sceneOwner_));
        break;
    case WorldMessageType::NetScenePad:
        if (m.netState.size() == 2) scenePad_ = uint16_t(m.netState[0] | m.netState[1] << 8);
        break;
    case WorldMessageType::NetSceneEnd:
        // this game's copy ends once its script has too (until then what it changes is the world's to say)
        if (localScene_) sceneEndAt_ = m.netState;
        else scene_ = false;
        break;
    case WorldMessageType::NetMoveTo:
        if (gateAsked_ && gateAtDoor_) {
            // the gate this player waited at, a door's, opens: the door's own script and scene go on here, as in the game
            gateAsked_ = false;
            gateAtDoor_ = false;
            doorPass_ = true;
            if (m.netState.size() >= 8) {
                const auto half = [&](size_t i) { return int16_t(uint16_t(m.netState[i] | m.netState[i + 1] << 8)); };
                doorArrival_ = std::array<int16_t, 5>{int16_t(m.netState[0]), int16_t(m.netState[1]), half(2), half(4), half(6)};
            }
            od::port::transitionRequest().kind = uint8_t(od::port::TransitionKind::DoorActor);
            od::port::setStoryFlag(od::port::kFlagTransitionArmed);
            SDL_Log("own world: the gate opens: through the door");
            break;
        }
        pendingMove_ = m.netState;
        break;
    case WorldMessageType::NetWarp:
        if (m.netState.size() == 16) { // as the world did: this game's Mega Man and room with it
            const auto half = [&](size_t i) { return int16_t(uint16_t(m.netState[i] | m.netState[i + 1] << 8)); };
            od::port::TransitionRequest& r = od::port::transitionRequest();
            r.destStage = m.netState[0];
            r.destRoom = m.netState[1];
            r.trigger.x = half(2);
            r.trigger.y = half(4);
            r.trigger.z = half(6);
            r.destination.x = half(8);
            r.destination.y = half(10);
            r.destination.z = half(12);
            r.destination.yaw = half(14);
            r.state = od::port::kTransitionWarp;
            warping_ = true;
        }
        break;
    case WorldMessageType::NetScreen:
        od::port::netScreenApply(m.netState);
        break;
    case WorldMessageType::NetGateGlow:
        if (m.netState.size() == 6) {
            const auto half = [&](size_t i) { return int16_t(uint16_t(m.netState[i] | m.netState[i + 1] << 8)); };
            gateGlow_ = std::array<int16_t, 3>{half(0), half(2), half(4)};
            gateGlowTicks_ = 45; // the coordinator repeats it while the gate waits
        }
        break;
    case WorldMessageType::NetRoomPlayer:
        if (m.netState.size() == 3 && m.netState[0] <= uint8_t(od::port::RoomPlayerField::FullLife))
            od::port::applyRoomPlayerField(od::port::player(), od::port::RoomPlayerField(m.netState[0]), int16_t(uint16_t(m.netState[1] | m.netState[2] << 8)));
        break;
    case WorldMessageType::NetNotice:
        notice_.assign(m.netState.begin(), m.netState.end());
        noticeTicks_ = 30;
        break;
    case WorldMessageType::NetBossFight:
        bossFight_ = !m.netState.empty() && m.netState[0] != 0;
        break;
    case WorldMessageType::NetHold:
        if (m.netState.size() == 3) { // the box carrier holds this player, and only them
            if (od::port::ActorHeader* carrier = actors_->mirrorOf(m.netState[0]))
                od::port::st16ClientCatch(*carrier, m.netState[1], m.netState[2] != 0);
        }
        break;
    case WorldMessageType::NetAlert:
        notice(std::string(m.netState.begin(), m.netState.end()));
        break;
    case WorldMessageType::NetSave:
        if (std::exchange(saveEnded_, false)) { // the first chunk of another save
            hostSave_.clear();
            hostSaveWhole_ = false;
        }
        hostSave_.insert(hostSave_.end(), m.netState.begin(), m.netState.end());
        if (m.netState.size() < 1000) hostSaveWhole_ = saveEnded_ = true; // the last chunk is the short one
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
    if (!o.equipment.empty()) od::port::applyEquipment(p, o.equipment); // their shots here take their weapons
    const od::port::NetPlayerFields& f = o.from.fields();
    p.status = uint16_t((p.status & od::port::kStatusMarkerMask) | f.status);
    od::port::applyNetArm(p, f);
    if (actors_->shotRunsFor(table)) { // the world's copy says when it ends, the owner's fields come later
        p.specialFiring = 1;
        p.held |= p.button(od::port::PlayerButton::Special);
        p.armFlags |= od::port::kArmSpecial;
    }
    const od::port::ScopedActingPlayer acting(table);
    od::port::showStatusMarkers(p);
    od::port::showChargeMarker(p, f.charge);
}

void OwnWorldClient::showPlayers(GhostView& ghosts, uint64_t localFingerprint, uint64_t nowNs) {
    if (localScene_) { // a scene has one Mega Man, this game's: the others are hidden until it ends
        for (int slot = 0; slot < od::ghost::kMaxGhosts; ++slot) ghosts.clearRemote(slot);
        return;
    }
    for (size_t id = 0, slot = 0; id < others_.size() && slot < size_t(od::ghost::kMaxGhosts); ++id) {
        Other& o = others_[id];
        if (o.buffer.empty()) continue;
        auto shown = o.buffer.sample(nowNs);
        if (!shown) {
            ghosts.clearRemote(int(slot++));
            continue;
        }
        // the animation is played here, from the slot's own files, up to the shown tick; only where the ghost is posed (both
        // in field play, in the same room): a stage's animations are this machine's data only in that stage
        const int64_t tick = int64_t(o.buffer.renderTick());
        const od::port::GhostRoom here = od::port::ghostRoomOf();
        const bool posed = here.fieldPlay && shown->room.fieldPlay && shown->room.stage == here.stage && shown->room.sub == here.sub;
        if (!posed) o.stepped = -1; // where it is next posed it starts at the shown tick
        else if (od::LoadedFileData* files = ghosts.files(int(slot))) {
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
    downedTurn();
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

void OwnWorldClient::sceneTurn() {
    if (localScene_) { // the scene runs in this game as in the normal game; the owner's pad drives every copy of it
        if (sceneOwner_ == playerId_) {
            const uint16_t held = od::port::pad0().held;
            if (held != sentPad_) {
                sentPad_ = held;
                pipe_->sendReliable(playerId_, WorldMessageType::NetScenePad, {uint8_t(held), uint8_t(held >> 8)});
            }
        }
        return;
    }
    // a move the coordinator asked: once this game is playing
    const od::port::GameState& g = od::port::gameState();
    if (!pendingMove_.empty() && g.gameMode() == od::port::GameMode::Field && g.fieldStep() == od::port::FieldStep::Playing &&
        od::port::transitionRequest().state == od::port::kTransitionNone && !hostSaveWhole_) {
        movedTo(pendingMove_);
        pendingMove_.clear();
    }
}

void OwnWorldClient::movedTo(const std::vector<uint8_t>& to) {
    if (to.size() != 12) return;
    const auto half = [&](size_t at) { return int16_t(uint16_t(to[at] | to[at + 1] << 8)); };
    const int16_t x = half(2), y = half(4), z = half(6), yaw = half(8);
    od::port::Player& p = od::port::player();
    if (to[0] == uint8_t(room_->stage) && to[1] == uint8_t(room_->sub)) {
        p.pos.x = int32_t(uint32_t(int32_t(x)) << 16);
        p.pos.y = int32_t(uint32_t(int32_t(y)) << 16);
        p.pos.z = int32_t(uint32_t(int32_t(z)) << 16);
        p.prevPos = p.pos;
        p.rotation.y = yaw;
        p.prevRotation.y = yaw;
    } else {
        // another stage loads its files behind a fade (a world's script may have switched in place, its files loaded there)
        const bool otherStage = to[0] != uint8_t(od::port::gameState().stage);
        const uint8_t fadeOut = otherStage && to[11] == od::port::kNoFade ? kStageFadeOut : to[11];
        const uint8_t fadeIn = otherStage && to[10] == od::port::kNoFade ? kStageFadeIn : to[10];
        od::port::requestTransition(od::port::kTransitionAccepted, to[0], to[1], x, y, z, yaw, fadeIn, fadeOut);
    }
    SDL_Log("own world: moved to ST%02X sub %d at %d %d %d", unsigned(to[0]), int(to[1]), int(x), int(y), int(z));
}

// The world's scene is over (or the world never started the one this game's room started), and the one this game ran
// is too (its script done, no room change pending, the field playing): what the scene left goes, this game is in its
// room's world again and is told all of it (a fresh join)
void OwnWorldClient::localSceneTurn() {
    if (!localScene_ || (!sceneEndAt_ && sceneHeard_)) return;
    const od::port::GameState& g = od::port::gameState();
    if (od::port::scriptState().inUse() || od::port::transitionRequest().state != od::port::kTransitionNone ||
        g.gameMode() != od::port::GameMode::Field || g.fieldStep() != od::port::FieldStep::Playing)
        return;
    const std::optional<std::vector<uint8_t>> at = std::exchange(sceneEndAt_, std::nullopt);
    const bool sameRoom = sceneRoom_ && room_ && *sceneRoom_ == *room_;
    localScene_ = false;
    sceneRoom_.reset();
    const WorldRoom here = *room_;
    if (od::port::storyStageRunsAlone(uint8_t(here.stage))) { // the scene went to a race: it runs here alone
        if (joined_) pipe_->sendReliable(playerId_, WorldMessageType::NetLeave, {});
        room_.reset();
        joined_ = false;
        return;
    }
    od::port::presentationLoopsClear();
    freeLocalActors();
    bindWorld(here.stage, here.sub);
    if (sameRoom && at) sceneEnd(*at);
    SDL_Log("own world: the scene ended, back in the room's world");
}

void OwnWorldClient::sceneEnd(const std::vector<uint8_t>& at) {
    od::port::Player& p = od::port::player();
    if (at.size() == 9) { // next to where the scene's Mega Man stands; its owner there
        const auto half = [&](size_t i) { return int16_t(uint16_t(at[i] | at[i + 1] << 8)); };
        constexpr int kBeside = 0x80;
        const int16_t x = int16_t(half(0) + (playerId_ == at[8] ? 0 : kBeside)), y = half(2), z = half(4), yaw = half(6);
        p.pos.x = int32_t(uint32_t(int32_t(x)) << 16);
        p.pos.y = int32_t(uint32_t(int32_t(y)) << 16);
        p.pos.z = int32_t(uint32_t(int32_t(z)) << 16);
        p.prevPos = p.pos;
        p.rotation.y = yaw;
        p.prevRotation.y = yaw;
    }
}

void OwnWorldClient::overridePad(uint16_t& buttons) {
    if (localScene_ && sceneOwner_ != playerId_) buttons = scenePad_;
}

// An exit this player has taken: not while a story boss is being fought; at a gate they wait, with who is at it
void OwnWorldClient::gateTurn() {
    if (noticeTicks_ > 0) --noticeTicks_;
    if (gateGlow_) {
        if (--gateGlowTicks_ <= 0) gateGlow_.reset();
        else if (gateGlowTicks_ % 20 == 0) od::port::doorGlowAt((*gateGlow_)[0], (*gateGlow_)[1], (*gateGlow_)[2]);
    }
    od::port::TransitionRequest& r = od::port::transitionRequest();
    const od::port::GameState& g = od::port::gameState();
    const bool asked = od::port::storyFlag(od::port::kFlagTransitionArmed) || r.state == od::port::kTransitionAsked;
    if (asked && r.kind == uint8_t(od::port::TransitionKind::DoorActor)) od::port::doorActorExitAhead(od::port::player());
    if (coordinator_ && noticeTicks_ > 0 && (od::port::player().pressedNow(od::port::PlayerButton::Use) || localTick_ == testForceTick_))
        pipe_->sendReliable(playerId_, WorldMessageType::NetGateForce, {});
    bool stands = false;
    const bool gateWasAsked = gateAsked_;
    if (asked && g.gameMode() == od::port::GameMode::Field && g.fieldStep() == od::port::FieldStep::Playing) {
        const bool mission = od::port::storyPlaceOf(uint8_t(room_->stage), room_->sub, g.story) == od::port::StoryPlaceKind::Mission;
        const bool door = r.kind == uint8_t(od::port::TransitionKind::DoorActor);
        const bool passing = doorPass_ && door; // the gate opened for this door: its script runs
        if (passing) doorPass_ = false;
        const bool gated = !passing && (mission || od::port::storyDoorGated(uint8_t(room_->stage), r.destStage, r.destRoom, g.story));
        if (bossFight_ || testBossFight_ || gated) {
            r.state = od::port::kTransitionNone;
            od::port::clearStoryFlag(od::port::kFlagTransitionArmed);
        }
        if (bossFight_ || testBossFight_) {
            notice_ = "Nobody leaves during the fight";
            noticeTicks_ = 60;
        } else if (gated) {
            stands = true;
            if (!gateAsked_) {
                const auto half = [](int16_t v, std::vector<uint8_t>& o) {
                    o.push_back(uint8_t(v));
                    o.push_back(uint8_t(uint16_t(v) >> 8));
                };
                std::vector<uint8_t> move = {r.destStage, r.destRoom};
                half(r.destination.x, move);
                half(r.destination.y, move);
                half(r.destination.z, move);
                half(r.destination.yaw, move);
                move.push_back(r.fadeIn);
                move.push_back(r.fadeOut);
                move.push_back(uint8_t(mission ? 1 : 0));
                pipe_->sendReliable(playerId_, WorldMessageType::NetGateAsk, std::move(move));
                gateAsked_ = true;
                gateAtDoor_ = door;
            }
        }
    }
    constexpr int kWalkedOff = 0x180;
    if (stands && !gateWasAsked) {
        gatePlace_ = od::port::player().pos;
    } else if (gateAsked_ && od::port::distance3(gatePlace_, od::port::player().pos) > kWalkedOff) {
        pipe_->sendReliable(playerId_, WorldMessageType::NetGateLeave, {});
        gateAsked_ = false;
        gateAtDoor_ = false;
    }
}

void OwnWorldClient::downedTurn() {
    od::port::Player& p = od::port::player();
    if (od::port::playerIsDowned(p)) {
        reviving_.reset();
        reviveTicks_ = 0;
        ++downTicks_;
        if (downTicks_ >= od::port::kRespawnAloneTicks && p.pressedNow(od::port::PlayerButton::Use))
            pipe_->sendReliable(playerId_, WorldMessageType::NetRespawnAsk, {});
        return;
    }
    downTicks_ = 0;
    // a teammate down within reach, Use held: about 3 s and they are up
    std::optional<uint8_t> by;
    if (p.holds(od::port::PlayerButton::Use) && p.hitPoints >= 0) {
        for (uint8_t id = 0; id < others_.size() && !by; ++id) {
            const od::port::NetPlayerFields& f = others_[id].from.fields();
            if (id == playerId_ || f.downed == 0) continue;
            od::port::Fixed16Vec3 at;
            at.x.whole = f.pos[0];
            at.y.whole = f.pos[1];
            at.z.whole = f.pos[2];
            if (od::port::distance3(at, p.pos) < od::port::kReviveReach) by = id;
        }
    }
    if (by != reviving_) reviveTicks_ = 0;
    reviving_ = by;
    if (!by) return;
    if (++reviveTicks_ == od::port::kReviveTicks) {
        pipe_->sendReliable(playerId_, WorldMessageType::NetRevive, {*by});
        reviveTicks_ = 0;
    }
}

void OwnWorldClient::drawNotice() const {
    char text[192] = "";
    if (SDL_GetTicksNS() < alertUntilNs_) std::snprintf(text, sizeof text, "%s", alert_.c_str());
    const od::port::Player& p = od::port::player();
    if (text[0] != 0 || !room_) {
    } else if (od::port::playerIsDowned(p)) {
        if (downTicks_ < od::port::kRespawnAloneTicks) std::snprintf(text, sizeof text, "You are down. A teammate can revive you.");
        else std::snprintf(text, sizeof text, "You are down. Use: back next to a teammate.");
    } else if (reviving_) {
        std::snprintf(text, sizeof text, "Reviving: %d%%", reviveTicks_ * 100 / od::port::kReviveTicks);
    }
    if (text[0] == 0 && noticeTicks_ > 0) std::snprintf(text, sizeof text, "%s", notice_.c_str());
    if (text[0] == 0) return;
    const ImGuiViewport* view = ImGui::GetMainViewport();
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 at(view->Pos.x + (view->Size.x - size.x) / 2, view->Pos.y + view->Size.y * 0.15f);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(ImVec2(at.x - 8, at.y - 4), ImVec2(at.x + size.x + 8, at.y + size.y + 4), IM_COL32(0, 0, 0, 170), 4.0f);
    draw->AddText(at, IM_COL32_WHITE, text);
}

void OwnWorldClient::localActors(const od::port::PlayerEnv& env, od::port::ActorContext& context, bool late) {
    od::port::runNetLocalActors(env, context, late, failures_);
    if (!late && actors_) actors_->submitSolids();
}

} // namespace od::app
