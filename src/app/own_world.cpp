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
#include "game/hud.hpp"
#include "game/modern_aim.hpp"
#include "game/pickups.hpp"
#include "game/player/player_interaction.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_status.hpp"
#include "game/player/player_weapons.hpp"
#include "game/player/remote_player.hpp"
#include "game/player/player_snapshot.hpp"
#include "game/player/player_state.hpp"
#include "game/carry.hpp"

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
    // ... the lift too: the world's actors are what can be lifted
    od::port::setLiftNothingHook([this] {
        if (room_ && joined_ && !localScene_) pipe_->sendReliable(playerId_, WorldMessageType::NetLift, {});
    });
    ghostSlotOfTable_.fill(-1);
    od::port::setRemotePlayerFiles([this](size_t table) { return filesOfTable(table); });
}

OwnWorldClient::~OwnWorldClient() { od::port::setRemotePlayerFiles(nullptr); }

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
    playedScene_.reset();
    roomEntry_ = RoomEntry{WorldRoom{stage, sub}, od::port::storyFlags(), od::port::roomScriptState()};
    // the room is loaded, its first field tick still to come (also one a scene's script went to)
    if (coordinator_) coordinator_->hostBound(WorldRoom{stage, sub});
    if (localScene_) { // a room the scene's script went to: its actors are this game's, as in the normal game
        room_ = WorldRoom{stage, sub};
        sceneMoved_ = true;
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
    const od::port::ScriptState& s = od::port::scriptState();
    if (std::exchange(sceneMoved_, false) && localScene_) {
        // the scene's script went here and is over: the room's own scene started (a boss's next stage). The scene that
        // brought this game is done; this one is the room's, as the world's for the first one in: the world's start of it
        // confirms it, it plays once
        if ((s.flags & od::port::kScriptRunning) && s.id != 0 && s.id != sceneScript_) {
            localScene_ = false;
            sceneRoom_.reset();
            od::port::presentationLoopsClear();
            bindWorld(room_->stage, room_->sub);
            startedHere(s.id);
        }
        return;
    }
    if (!entering_) return;
    entering_ = false;
    if ((s.flags & od::port::kScriptRunning) && s.id != 0) {
        startedHere(s.id);
        return;
    }
    freeLocalActors();
}

void OwnWorldClient::startedHere(uint8_t script) {
    localScene_ = true;
    sceneRan_ = false;
    scene_ = true;
    sceneOwner_ = 0; // the world's rule: the first player in the room, the host when here; the world's start says who
    scenePads_.start();
    sentPad_ = 0;
    sceneTick_ = 0;
    sceneRoom_ = room_;
    sceneScript_ = script;
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
        if ((od::port::effectPool()[i].flags & od::port::kActorLive) && !od::port::netClientKeepsEffect(od::port::effectPool()[i]))
            od::port::freeActor(od::port::effectPool()[i]);
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
    if (!room_ || room_->stage != stage || room_->sub != sub) pendingMove_.clear(); // a move of the room left
    room_ = WorldRoom{stage, sub};
    joined_ = false;
    lastEquipment_.clear();
    // the numbering goes on (a room bound again after a scene is the same world, told of no join: an update numbered
    // over from the start would be older there than every one before), every field goes again until one is acknowledged
    uplink_.restart();
    guestUplink_.restart();
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
    const std::vector<uint8_t> banks = heldBanks_ ? heldBanks_() : std::vector<uint8_t>{};
    pipe_->enter(playerId_, *room_, playerState(), banks);
    pipe_->sendReliable(playerId_, WorldMessageType::NetJoin, {});
    if (guest_ && coordinator_) pipe_->enter(kGuestId, *room_, playerState(), banks);
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
    for (const TestFlag& t : testFlags_)
        if (coordinator_ && t.tick == localTick_) {
            coordinator_->testFlagNow(t.flag, t.set, uint8_t(room_->stage));
            SDL_Log("own world: flag %03x %s at tick %u", unsigned(t.flag), t.set ? "set" : "cleared", localTick_);
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
    // a story script this game's own started in the room's world (a message's op, the room's arrival): a scene of this
    // game's, as in the normal game, with the room's own actors (script 0 is a door's: the transition, this game's own)
    if (!localScene_ && !entering_ && od::port::scriptState().inUse() && od::port::scriptState().id != 0) {
        const uint8_t script = od::port::scriptState().id;
        localScene_ = true;
        sceneRan_ = true;
        scene_ = true;
        sceneOwner_ = playerId_;
        scenePads_.start();
        sentPad_ = 0;
        sceneTick_ = 0;
        sceneRoom_ = room_;
        sceneScript_ = script;
        sceneEndAt_.reset();
        sceneHeard_ = false; // the world plays none of it: it ends here with its script
        roomRebuild_ = true;
        SDL_Log("own world: a script of this game's started in the room's world (script %02x): a scene here", unsigned(script));
    }
    sceneTurn();
    liftTurn();
    localSceneTurn();
    if (!room_) return;
    // a scene's own room changes are the game's, as in the normal game; one held for everyone waits at the gate; once its
    // script is done (its world's end still to come) an exit is the player's own
    if (!localScene_ || heldHere_ || !od::port::scriptState().inUse()) gateTurn();
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
    liftPlace();
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
    // ... and what the world's copy of one this game has played to its end shows, until it ends there too: its windows, its
    // fades, its warps are the scene's, seen here already
    if (playedScene_ && (m.type == WorldMessageType::NetPresent || m.type == WorldMessageType::NetWarp ||
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
    case WorldMessageType::NetGauges: // the world's enemy gauges, on this game's copies of their actors
        if (const size_t n = m.netState.size() / 11; n != gaugesHeard_) {
            gaugesHeard_ = n;
            SDL_Log("own world: the world shows %zu enemy gauge(s)%s", n,
                    n ? (", the first of slot " + std::to_string(m.netState[6]) + " at " +
                         std::to_string(int16_t(uint16_t(m.netState[7] | m.netState[8] << 8))) + " hp").c_str() : "");
        }
        od::port::hudNetGaugesApply(m.netState, [this](uint8_t slot) -> od::port::Actor* {
            od::port::ActorHeader* head = actors_->mirrorOf(slot);
            return head && (head->flags & od::port::kActorLive) ? &od::port::actorLayoutOf(*head) : nullptr;
        });
        break;
    case WorldMessageType::NetEquip: // another player's equipment: u8 their id, then what they wear
        if (!m.netState.empty() && m.netState[0] < others_.size() && m.netState[0] != playerId_)
            others_[m.netState[0]].equipment.assign(m.netState.begin() + 1, m.netState.end());
        break;
    case WorldMessageType::NetShotHit: {
        const auto hit = od::port::decodeShotHit(m.netState);
        if (!hit) break;
        bool applied = false;
        if (hit->serial == od::port::kNetShotCopy) { // another player's shot: the copy running here
            if (od::port::ShotActor* copy = actors_->heldCopy(hit->clientSlot)) {
                od::port::applyShotEvent(*copy, *hit);
                applied = true;
            }
        } else if (hit->clientSlot < ownShots_.size() && ownShots_[hit->clientSlot].live && ownShots_[hit->clientSlot].serial == hit->serial) {
            od::port::applyShotEvent(od::port::shotPool()[hit->clientSlot], *hit);
            applied = true;
        }
        static constexpr const char* kEvents[] = {"hit", "turned", "ended on a solid actor"};
        SDL_Log("own world: %s shot %s, result %08x at %d %d %d%s", hit->serial == od::port::kNetShotCopy ? "another's" : "own",
                kEvents[size_t(hit->event)], unsigned(hit->hit.result), int(hit->event == od::port::NetShotEvent::Result ? hit->hit.contact[0] : hit->pos[0]),
                int(hit->event == od::port::NetShotEvent::Result ? hit->hit.contact[1] : hit->pos[1]),
                int(hit->event == od::port::NetShotEvent::Result ? hit->hit.contact[2] : hit->pos[2]), applied ? "" : " (the shot is gone here)");
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
            eventMenus_ = e->menus; // what its talker's choices open plays here, with the event
            eventHints_ = e->hintsMessage;
            eventSaveMenu_ = {};
        }
        break;
    case WorldMessageType::NetRevived:
        if (od::port::playerIsDowned(od::port::player())) {
            SDL_Log("own world: revived");
            od::port::playerRevive(od::port::player(), 0); // the game's danger state, as a respawn: the next hit downs them
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
        if (fade_) od::port::applyPresentation(m.netState, *fade_, playerId_);
        break;
    case WorldMessageType::NetSceneStart: {
        if (m.netState.size() < 2) break;
        // how many ticks into the scene the world is (one joining it late)
        const uint32_t at = m.netState.size() >= 7 ? uint32_t(m.netState[3]) | uint32_t(m.netState[4]) << 8 |
                                                        uint32_t(m.netState[5]) << 16 | uint32_t(m.netState[6]) << 24
                                                  : 0u;
        if (localScene_) { // started here on the room's first tick: the world's start confirms it and names its owner
            if (sceneHeard_) break;
            sceneHeard_ = true;
            sceneOwner_ = m.netState[0];
            if (std::exchange(heldHere_, false)) { // everyone is in: it plays from its start, with the room as it began
                od::port::setScriptsGated(false);
                sceneStart_ = m.netState[1];
                scenePads_.start(at); // its ticks count from here, everywhere
                sentPad_ = 0;
                sceneTick_ = at;
            }
            SDL_Log("own world: the world starts the scene too (script %02x), its owner is player %u", unsigned(m.netState[1]),
                    unsigned(sceneOwner_));
            break;
        }
        if (playedScene_ && room_ && playedScene_->room == *room_ && playedScene_->script == m.netState[1]) {
            // the world's copy of the scene this game has played already, told on joining it again: not played twice
            SDL_Log("own world: the world's scene (script %02x) was played here already", unsigned(m.netState[1]));
            break;
        }
        scene_ = true;
        sceneOwner_ = m.netState[0];
        scenePads_.start(at);
        sentPad_ = 0;
        sceneTick_ = at;
        localScene_ = true;
        sceneRan_ = false;
        // (a scene of this player's alone, m.netState[2]: the world does not play it, no end of it comes from there --
        // it ends here with its script)
        sceneHeard_ = !(m.netState.size() > 2 && m.netState[2] != 0);
        sceneStart_ = m.netState[1];
        sceneScript_ = m.netState[1];
        sceneRoom_ = room_;
        sceneEndAt_.reset();
        od::port::presentationLoopsClear(); // the world's sounds: the scene's are this game's own
        SDL_Log("own world: a scene starts (script %02x), its owner is player %u: it runs here", unsigned(m.netState[1]),
                unsigned(sceneOwner_));
        break;
    }
    case WorldMessageType::NetScenePad:
        if (const auto pad = ScenePads::decode(m.netState); pad && pad->script == sceneScript_) scenePads_.heard(pad->at, pad->pad);
        break;
    case WorldMessageType::NetSceneEnd:
        // this game's copy ends once its script has too (until then what it changes is the world's to say)
        // (the world's end of a scene comes after its start: one before it is the end of the one this game left, sent by
        // that room's world before it heard of the move)
        if (localScene_ && sceneHeard_) {
            sceneEndAt_ = m.netState;
        } else if (!localScene_) {
            scene_ = false;
            playedScene_.reset(); // the world's copy of the one this game ended first is over too
        }
        break;
    case WorldMessageType::NetMoveTo: {
        // a move of the room this game is in (one of a room left since is stale: a world's exit sent before the move)
        if (m.netState.size() < 2) break;
        const uint8_t fromStage = m.netState[m.netState.size() - 2], fromSub = m.netState[m.netState.size() - 1];
        if (!room_ || fromStage != uint8_t(room_->stage) || fromSub != uint8_t(room_->sub)) {
            SDL_Log("own world: a move of ST%02X sub %u, left already: not taken", unsigned(fromStage), unsigned(fromSub));
            break;
        }
        const std::vector<uint8_t> move(m.netState.begin(), m.netState.end() - 2);
        if (gateAsked_ && gateAtDoor_) {
            // the gate this player waited at, a door's, opens: the door's own script and scene go on here, as in the game
            gateAsked_ = false;
            gateAtDoor_ = false;
            doorPass_ = true;
            if (move.size() >= 8) {
                const auto half = [&](size_t i) { return int16_t(uint16_t(move[i] | move[i + 1] << 8)); };
                doorArrival_ = std::array<int16_t, 5>{int16_t(move[0]), int16_t(move[1]), half(2), half(4), half(6)};
            }
            od::port::transitionRequest().kind = uint8_t(od::port::TransitionKind::DoorActor);
            od::port::setStoryFlag(od::port::kFlagTransitionArmed);
            SDL_Log("own world: the gate opens: through the door");
            break;
        }
        pendingMove_ = move;
        break;
    }
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
    case WorldMessageType::NetStageBanks:
        // the world's banks: its actors' model entries are of them (a scene this game runs loads its own)
        if (!localScene_ && takeBanks_) takeBanks_(m.netState);
        break;
    case WorldMessageType::NetLifted:
        if (m.netState.size() == 10) { // how the class places it: this game places its copy at this Mega Man's hands so
            const auto half = [&](size_t i) { return int16_t(uint16_t(m.netState[i] | m.netState[i + 1] << 8)); };
            if (lifted_ && lifted_->slot == m.netState[0])
                lifted_->place = std::array<int32_t, 3>{half(2), int32_t(uint32_t(uint16_t(half(6))) | uint32_t(uint16_t(half(8))) << 16), half(4)};
            break;
        }
        // the world's class took this player's lift: the actor's copy here goes up in this Mega Man's hands, as the
        // class's lift starts a player's (game/carry playerLiftTargetCheck); the throw goes back in this player's state
        if (m.netState.size() == 2) {
            od::port::Player& p = od::port::player();
            od::port::ActorHeader* copy = actors_->mirrorOf(m.netState[0]);
            if (copy && (copy->flags & od::port::kActorLive) && p.airborne == 0 && p.carryFlags == 0 && p.hitPoints >= 0) {
                p.interactionTarget.set(uint32_t(od::port::kInteractLift) << 24, *copy);
                p.liftMode = m.netState[1];
                p.subState = uint8_t(od::port::PlayerSub::Lift);
                p.enterStep(0);
                lifted_ = Lifted{m.netState[0], copy->type};
                SDL_Log("own world: lifted the world's actor %02x", unsigned(m.netState[0]));
            } else {
                // this Mega Man cannot (in the air, its hands full, down): the world's class lets it go
                pipe_->sendReliable(playerId_, WorldMessageType::NetLift, {kLiftRefused});
            }
        }
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
    od::port::setPlayerAimPoint(table, f.aimSet != 0, f.aimPoint); // their shots and beams here aim at their crosshair
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
    ghostSlotOfTable_.fill(-1);
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
        if (const uint8_t table = actors_->tableOfPlayer(uint8_t(id)); table < ghostSlotOfTable_.size()) ghostSlotOfTable_[table] = int(slot);
        ghosts.setRemote(int(slot++), *shown);
    }
    // their poses here (their bones, for what follows them) run on the files their ghost is drawn from
    ghosts_ = &ghosts;
}

od::LoadedFileData* OwnWorldClient::filesOfTable(size_t table) {
    if (!ghosts_ || table >= ghostSlotOfTable_.size() || ghostSlotOfTable_[table] < 0) return nullptr;
    return ghosts_->files(ghostSlotOfTable_[table]);
}

void OwnWorldClient::addGuest(int dx, int dz) { guest_ = std::make_pair(dx, dz); }

void OwnWorldClient::endFrame() {
    if (!room_) return;
    if (ownEvent_) {
        od::port::talkerMenusTick(eventSaveMenu_, eventMenus_, eventHints_);
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
        // this machine's Mega Man's (table 0): the others' shots run in this pool too, from the world's events, and are
        // never this player's to fire again
        const bool mine = (shot.flags & od::port::kActorLive) && (shot.runFlags & od::port::kActorRun) && shot.ownerPlayer == 0 &&
                          !actors_->heldShot(i);
        if (mine && !ownShots_[i].live) {
            if (nextSerial_ == od::port::kNetShotCopy) nextSerial_ = 0; // that value names another player's shot
            ownShots_[i] = {true, nextSerial_++};
            static const bool fireLog = std::getenv("OD_NET_FIRE_LOG") != nullptr; // test instrument
            if (fireLog) SDL_Log("own world: fire slot %zu type %02x variant %02x", i, unsigned(shot.type), unsigned(shot.variant));
            pipe_->sendReliable(playerId_, WorldMessageType::NetFire, od::port::encode(od::port::netFireOf(shot, uint8_t(i), ownShots_[i].serial)));
        } else if (!mine) {
            ownShots_[i].live = false;
        }
    }
}

void OwnWorldClient::sceneTurn() {
    if (localScene_) return; // the scene runs in this game as in the normal game; the owner's pad drives every copy (overridePad)
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

// The scene this game ran is over (its script done, no room change pending, the field playing): what the scene left goes,
// this game is in its room's world again and is told all of it (a fresh join). The world's copy of it may still run: it
// started after this game's (the world starts with the room) and plays the same pad on the same ticks, so it ends where
// this one did, later; its start, told again on the join, is not played a second time here
void OwnWorldClient::localSceneTurn() {
    if (!localScene_) return;
    // a scene is over once its script has run here and ended (one the world started begins on the game's next frame)
    if (od::port::scriptState().inUse()) sceneRan_ = true;
    if (!sceneRan_ || sceneStart_) return;
    const od::port::GameState& g = od::port::gameState();
    if (od::port::scriptState().inUse() || od::port::transitionRequest().state != od::port::kTransitionNone ||
        g.gameMode() != od::port::GameMode::Field || g.fieldStep() != od::port::FieldStep::Playing)
        return;
    std::optional<std::vector<uint8_t>> at = std::exchange(sceneEndAt_, std::nullopt);
    if (!at && sceneHeard_ && room_) {
        playedScene_ = PlayedScene{*room_, sceneScript_};
        // where the world's will end: where this game's Mega Man (the scene's, on every copy) stands, its owner there
        const od::port::Player& man = od::port::player();
        std::vector<uint8_t> mine;
        for (const int16_t v : {man.pos.x.whole, man.pos.y.whole, man.pos.z.whole, man.rotation.y}) {
            mine.push_back(uint8_t(v));
            mine.push_back(uint8_t(uint16_t(v) >> 8));
        }
        mine.push_back(sceneOwner_);
        at = std::move(mine);
    }
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
    // its script went to a room whose first field tick is still to come: that tick decides, as on any entry (the room
    // setup's actors stay for a scene it starts)
    const bool firstTickToCome = std::exchange(sceneMoved_, false);
    if (!firstTickToCome) freeLocalActors();
    bindWorld(here.stage, here.sub);
    entering_ = firstTickToCome;
    if (sameRoom && at) sceneEnd(*at);
    SDL_Log("own world: the scene ended, back in the room's world");
}

void OwnWorldClient::sceneEnd(const std::vector<uint8_t>& at) {
    od::port::Player& p = od::port::player();
    if (at.size() == 9) { // next to where the scene's Mega Man stands; its owner there
        const auto half = [&](size_t i) { return int16_t(uint16_t(at[i] | at[i + 1] << 8)); };
        // the others a step ahead of it, the way it faces (a step aside may be in a wall: the scene left it where it stands)
        constexpr int kStep = 0x80;
        int16_t ahead[3] = {half(0), half(2), half(4)};
        if (playerId_ != at[8]) od::port::moveAlongYaw16(ahead, half(6), 0, 0, kStep);
        const int16_t x = ahead[0], y = ahead[1], z = ahead[2], yaw = half(6);
        p.pos.x = int32_t(uint32_t(int32_t(x)) << 16);
        p.pos.y = int32_t(uint32_t(int32_t(y)) << 16);
        p.pos.z = int32_t(uint32_t(int32_t(z)) << 16);
        p.prevPos = p.pos;
        p.rotation.y = yaw;
        p.prevRotation.y = yaw;
    }
}

// What this Mega Man lifted of the world's: while it holds it, the actor's copy is there; one the world has done with
// (gone, another occupant in its slot) is let go here as a carried actor that goes is (the carry dropped)
void OwnWorldClient::liftTurn() {
    if (!lifted_) return;
    od::port::Player& p = od::port::player();
    const od::port::ActorHeader* copy = actors_->mirrorOf(lifted_->slot);
    const bool carrying = p.carryFlags != 0 || p.sub() == od::port::PlayerSub::Lift;
    const bool there = copy && (copy->flags & od::port::kActorLive) && copy->type == lifted_->type &&
                       p.interactionTarget.actor.get() == copy;
    if (carrying && !there) {
        p.interactionTarget.clear();
        p.carryFlags = 0;
        if (p.sub() == od::port::PlayerSub::Lift) p.enterSub(od::port::PlayerSub::Idle);
    }
    if (!carrying || !there) lifted_.reset();
}

// ... and what it holds is at its hands here as its class puts it there in the world (the world's place of it comes late)
void OwnWorldClient::liftPlace() {
    if (!lifted_ || !lifted_->place) return;
    od::port::ActorHeader* copy = actors_->mirrorOf(lifted_->slot);
    const od::port::Player& p = od::port::player();
    if (!copy || p.interactionTarget.actor.get() != copy || (p.carryFlags & od::port::kCarryReleased)) return;
    const std::array<int32_t, 3>& at = *lifted_->place;
    od::port::carryAtHandsOf(p, od::port::actorLayoutOf(*copy), at[0], at[1], at[2]);
}

void OwnWorldClient::overridePad(uint16_t& buttons) {
    if (!localScene_ || heldHere_) return;
    if (sceneOwner_ == playerId_) { // what drives every copy: each change, stamped with the scene's tick it was made on
        if (buttons != sentPad_) {
            sentPad_ = buttons;
            pipe_->sendReliable(playerId_, WorldMessageType::NetScenePad, ScenePads::encode(buttons, sceneTick_, sceneScript_));
        }
        ++sceneTick_;
        return;
    }
    buttons = scenePads_.step();
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
        // the gate opened for this door: its script runs, its own asks too (the pass lasts until the room changes)
        const bool passing = doorPass_ && door;
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
