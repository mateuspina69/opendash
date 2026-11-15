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
#include "game/door_actor.hpp"
#include "game/door_leaves.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/room_grid.hpp"
#include "game/hit_effects.hpp"
#include "game/story_flags.hpp"
#include "game/story_table.hpp"
#include "game/script_engine.hpp"
#include "game/spawn.hpp"
#include "game/script_state.hpp"
#include "game/world_view.hpp"
#include "content/nino/st16_script33.hpp"
#include "game/player/player_input.hpp"
#include "game/distances.hpp"
#include "game/player/player_downed.hpp"
#include "game/transition_state.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/message.hpp"
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

bool OwnWorldClient::holdsArrival() const {
    if (!room_ || localScene_ || worldLost_ || !pipe_->playerId()) return false;
    return entering_ || !joined_ || !shownAny_;
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
    sceneLeft_.clear();
    playedScene_.reset();
    roomEntry_ = RoomEntry{WorldRoom{stage, sub}, od::port::storyFlags(), od::port::roomScriptState()};
    // the room is loaded, its first field tick still to come (also one a scene's script went to)
    if (coordinator_) coordinator_->hostBound(WorldRoom{stage, sub});
    if (localScene_) { // a room the scene's script went to: its actors are this game's, as in the normal game
        room_ = WorldRoom{stage, sub};
        sceneMoved_ = true;
        sceneLeaveTold_ = true; // (what the load did is not the scene's: nothing of this room change is told from here)
        od::port::spawnNotesStart(); // what its script spawns here is the room's world's to make, once it is over
        return;
    }
    od::port::presentationLoopsClear();
    doorMirrors_ = {}; // (the room left took their leaves; its doors are this room's)
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
            beforeLogic_.reset();
            startedHere(s.id);
            return;
        }
        // ... else, the scene's script over, the scene that brought this game ends on this first tick: the room is its
        // world's from here, as on any entry -- what its own logic opened now (a mission's question) is dropped before its
        // window opens, and its logic runs no further here (the next step's message, its actors)
        if (!s.inUse()) {
            dropRoomEvent();
            logicToWorld();
            leaveSceneHere();
        }
        beforeLogic_.reset();
        return;
    }
    if (!entering_) return;
    entering_ = false;
    if ((s.flags & od::port::kScriptRunning) && s.id != 0) {
        beforeLogic_.reset(); // the scene is this game's from this tick, with what its logic did
        startedHere(s.id);
        return;
    }
    dropRoomEvent();
    logicToWorld();
    freeLocalActors();
}

void OwnWorldClient::roomLogicRuns() {
    if (room_ && (entering_ || (sceneMoved_ && localScene_))) beforeLogic_ = od::port::globalHold();
}

// The world takes the room: what the room's logic did to the session's state on this game's own first tick (a first
// visit's flag, the story's step) is the world's to do -- its logic runs from the room's entry, and the session hears it
void OwnWorldClient::logicToWorld() {
    if (const auto held = std::exchange(beforeLogic_, std::nullopt)) od::port::globalPutBack(*held);
}

// The world takes the room: a message its own logic opened here first (a mission's briefing on the room's first tick) is
// the world's -- its logic opens it for the room, and it plays from there once
void OwnWorldClient::dropRoomEvent() {
    od::port::EventState& ev = od::port::eventState();
    if (ev.active == 0) return;
    SDL_Log("own world: the room's message %d started here: its world plays it", int(ev.message));
    od::port::windowClose(int8_t(ev.window), od::port::WindowClose::Force);
    od::port::eventAbort();
    od::port::player().inputMode &= uint8_t(~od::port::kInputOff);
}

void OwnWorldClient::startedHere(uint8_t script) {
    localScene_ = true;
    sceneRan_ = false;
    scene_ = true;
    sceneAlone_ = false;
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
    worldLost_ = false;
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
    gateExit_.reset();
    doorPass_ = false;
    bossFight_ = false;
    od::port::globalWatchRebase();
    takeSceneValues();
    sharedEvent_ = false;
    if (followEvent_) followEnd(); // another's event of the room left
    scene_ = false;
    sceneHeard_ = false;
    sceneAlone_ = false;
    sceneLeaveTold_ = false;
    if (std::exchange(heldHere_, false)) od::port::setScriptsGated(false);
}

// The scene this game played its own copy of is over: the session's values heard meanwhile (the later of each)
void OwnWorldClient::takeSceneValues() {
    if (localScene_ || sceneValues_.empty()) return;
    od::port::GlobalChange heard;
    heard.values = std::exchange(sceneValues_, {});
    od::port::globalWatchApply(heard);
}

void OwnWorldClient::joinRoom() {
    const std::vector<uint8_t> banks = heldBanks_ ? heldBanks_() : std::vector<uint8_t>{};
    pipe_->enter(playerId_, *room_, playerState(), banks);
    pipe_->sendReliable(playerId_, WorldMessageType::NetJoin, {});
    if (guest_ && coordinator_) pipe_->enter(kGuestId, *room_, playerState(), banks);
    joined_ = true;
}

void OwnWorldClient::pump(uint64_t nowNs) {
    if (!pipe_) return;
    if (lastPumpNs_ != 0 && nowNs > lastPumpNs_) pumpGapMaxNs_ = std::max(pumpGapMaxNs_, nowNs - lastPumpNs_);
    lastPumpNs_ = nowNs;
    pipe_->poll(nowNs);
    pipe_->flush(nowNs);
}

void OwnWorldClient::awayTurn(uint64_t nowNs) {
    pipe_->poll(nowNs);
    pipe_->flush(nowNs);
    const std::optional<uint8_t> id = pipe_->playerId();
    if (!id) return;
    for (const WorldMessage& m : pipe_->take(*id))
        if (m.type == WorldMessageType::NetSave || m.type == WorldMessageType::NetReload || m.type == WorldMessageType::NetAlert) handle(m, nowNs);
}

void OwnWorldClient::turn(uint64_t nowNs) {
    if (!room_ || entering_) return; // entering: the room's first field tick decides first (roomScriptsRan)
    // all this turn sends -- this player's state, the acknowledgements of what it read -- leaves once, as it ends
    struct Flush {
        WorldPipe& pipe;
        uint64_t nowNs;
        ~Flush() { pipe.flush(nowNs); }
    } flushAtEnd{*pipe_, nowNs};
    pipe_->poll(nowNs);
    if (!joined_) {
        const std::optional<uint8_t> id = pipe_->playerId();
        if (!id) return; // the session has not welcomed this game yet
        playerId_ = *id;
        actors_->setPlayerMap(playerId_);
        joinRoom();
        if (!sceneLeft_.empty()) {
            SDL_Log("own world: what the scene left in this room is told to its world");
            pipe_->sendReliable(playerId_, WorldMessageType::NetSceneLeft, std::exchange(sceneLeft_, {}));
        }
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
        std::vector<uint8_t> update = uplink_.update(playerId_, localTick_, fields);
        uplinkSentNs_[size_t(update[1] | update[2] << 8) % uplinkSentNs_.size()] = nowNs; // (its sequence: after its id)
        pipe_->sendLatest(playerId_, WorldMessageType::NetPlayer, listed(std::move(update)));
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
        sceneAlone_ = true;
        roomRebuild_ = true;
        SDL_Log("own world: a script of this game's started in the room's world (script %02x): a scene here", unsigned(script));
    }
    if (noticeTicks_ > 0) --noticeTicks_; // a notice lasts its ticks, a scene playing here or not
    storyMoveTurn();
    sceneTurn();
    liftTurn();
    localSceneTurn();
    if (!room_) return;
    doorMirrorTurn();
    // a scene's own room changes are the game's, as in the normal game; one held for everyone waits at the gate; once its
    // script is done (its world's end still to come) an exit is the player's own
    if (!localScene_ || heldHere_ || !od::port::scriptState().inUse()) gateTurn();
    od::port::presentationLoopsTick();
    // what happened in this game goes to the session; not what a stage's load does. A scene the world plays too: the
    // world's copy does it all -- but what its owner's copy did by the time it leaves the room (the world's copy may not
    // get there: it is left behind, and a cursor it cannot follow leaves it elsewhere) is told then, by the owner
    const od::port::GameState& g = od::port::gameState();
    const bool stageHere = int(g.stage) == room_->stage && g.gameMode() != od::port::GameMode::StageLoad;
    const bool sceneMine = scene_ && localScene_ && !heldHere_ && sceneOwner_ == playerId_;
    if (followEvent_ || (scene_ && !sceneMine)) { // (another's event played here: what it does comes from their game)
        od::port::globalWatchRebase();
    } else if (scene_ && !sceneAlone_) {
        if (od::port::transitionRequest().state == od::port::kTransitionNone) {
            sceneLeaveTold_ = false;
        } else if (sceneLeaveTold_) {
            od::port::globalWatchRebase(); // (the room change's own doing -- a stage's load -- is not the scene's)
        } else if (stageHere) {
            sceneLeaveTold_ = true;
            od::port::GlobalChange change = od::port::globalWatchTake(uint8_t(room_->stage));
            change.zennyDelta.reset(); // (the world's count)
            if (!change.empty()) {
                SDL_Log("own world: the scene leaves the room: what it did here is told");
                pipe_->sendReliable(playerId_, WorldMessageType::GlobalChange, od::port::encode(change));
            }
        }
    } else if (stageHere) { // (a scene of this game's alone included: nobody else plays it)
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
        if (shownAny_ && int64_t(shown_) >= at) ++stoodTicks_;
        while (at >= 0 && (!shownAny_ || int64_t(shown_) < at)) {
            shown_ = shownAny_ ? shown_ + 1 : uint32_t(at);
            shownAny_ = true;
            state_->advance(shown_);
        }
        actors_->endTick();
        behindSum_ += state_->newestTick() - shown_;
        ++behindTicks_;
    }
    netCheck(nowNs);
    for (; failuresLogged_ < failures_.classes.size(); ++failuresLogged_)
        SDL_Log("own world: failed: %s", failures_.classes[failuresLogged_].c_str());
}

void OwnWorldClient::netCheck(uint64_t nowNs) {
    constexpr uint32_t kEvery = 150;
    if (localTick_ % kEvery != 0 || roundTrips_ == 0) return;
    const od::net::Session::Traffic traffic = pipe_->traffic();
    const double seconds = checkedNs_ != 0 && nowNs > checkedNs_ ? double(nowNs - checkedNs_) * 1e-9 : 0.0;
    const auto rate = [&](uint64_t now, uint64_t before) { return seconds > 0.0 ? double(now - before) / seconds : 0.0; };
    SDL_Log("netcheck: to the world and back avg %.1f ms, max %.1f ms (%u player updates); the world shown %.1f ticks behind the newest heard, "
            "its clock stood %u of %u turns; the session left unread %.1f ms at most (this game's frames %.1f ms apart at most); sent %.0f "
            "datagrams %.0f bytes a second, received %.0f datagrams %.0f bytes a second",
            double(roundTripSumNs_) / double(roundTrips_) * 1e-6, double(roundTripMaxNs_) * 1e-6, roundTrips_,
            behindTicks_ ? double(behindSum_) / double(behindTicks_) : 0.0, stoodTicks_, behindTicks_,
            double(coordinator_ ? coordinator_->takeRelayGapNs() : pumpGapMaxNs_) * 1e-6, // (a host's is read by its relay)
            double(pumpGapMaxNs_) * 1e-6,
            rate(traffic.sentDatagrams, trafficAt_.sentDatagrams), rate(traffic.sentBytes, trafficAt_.sentBytes),
            rate(traffic.receivedDatagrams, trafficAt_.receivedDatagrams), rate(traffic.receivedBytes, trafficAt_.receivedBytes));
    trafficAt_ = traffic;
    checkedNs_ = nowNs;
    stoodTicks_ = 0;
    pumpGapMaxNs_ = 0;
    roundTripSumNs_ = roundTripMaxNs_ = 0;
    roundTrips_ = behindSum_ = behindTicks_ = 0;
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
        if (state_->heard()) clock_->heard(state_->newestTick(), nowNs);
        break;
    case WorldMessageType::NetPlayerAck:
        if (m.netState.size() == 3) {
            const uint16_t sequence = uint16_t(m.netState[1] | m.netState[2] << 8);
            uplink_.acknowledge(sequence);
            if (const uint64_t sent = std::exchange(uplinkSentNs_[sequence % uplinkSentNs_.size()], 0); sent != 0 && nowNs >= sent) {
                roundTripSumNs_ += nowNs - sent;
                roundTripMaxNs_ = std::max(roundTripMaxNs_, nowNs - sent);
                ++roundTrips_;
            }
        }
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
            for (size_t c = 0; c < 2; ++c) { // a start not heard before: kept by its tick
                const od::port::NetPlayerChannel& ch = o.from.fields().channels[c];
                if (ch.tick == od::port::kNetPlayerNoTick) continue;
                std::deque<od::port::NetPlayerChannel>& q = o.starts[c];
                if (!q.empty() && q.back().tick == ch.tick) {
                    q.back() = ch;
                    continue;
                }
                q.push_back(ch);
                constexpr size_t kKeptStarts = 16;
                while (q.size() > kKeptStarts) q.pop_front();
            }
            acks.push_back(*id);
            acks.push_back(uint8_t(heard->sequence));
            acks.push_back(uint8_t(heard->sequence >> 8));
            // the fields of this tick in the smoothing buffer: its frames are the client's, set when shown
            od::port::RemotePlayerState s;
            s.room = od::port::ghostRoomOf();
            s.snapshot = od::port::netPlayerSnapshot(o.from.fields(), o.tracks);
            o.buffer.push(nowNs, heard->tick, s);
            // the door they open on that tick (shown here at the tick their ghost is shown at)
            if (o.doors.empty() || o.doors.back().second != o.from.fields().door) {
                o.doors.emplace_back(heard->tick, o.from.fields().door);
                constexpr size_t kKeptDoors = 64;
                while (o.doors.size() > kKeptDoors) o.doors.pop_front();
            }
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
        if (auto change = od::port::decodeGlobalChange(m.netState)) {
            // this game's own copy of a scene the world plays too: what the scene counts (the story's next step, a
            // counter) its own copy counts here. The world's copy, told first, would be counted on top of: the session's
            // values wait for the scene's end, where both say the same
            if (localScene_ && !sceneAlone_) {
                for (const od::port::GlobalChange::Value& v : change->values) sceneValues_.push_back(v);
                change->values.clear();
            }
            od::port::globalWatchApply(*change);
        }
        break;
    case WorldMessageType::NetGift:
        if (const auto gift = od::port::decodeGift(m.netState)) {
            SDL_Log("own world: gift of kind %d, amount %d", int(gift->kind), int(gift->amount));
            od::port::pickupGiftArrived(od::port::player(), *gift);
        }
        break;
    case WorldMessageType::NetEventStart:
        if (const auto e = od::port::decodeOwnerEvent(m.netState)) {
            const bool follows = e->follows != od::port::kEventOwnRun;
            if (follows && ownEvent_) break; // this player's own event runs here: theirs is not seen over it
            if (follows && followEvent_) followEnd();
            eventPoint_.x = e->x;
            eventPoint_.y = e->y;
            eventPoint_.z = e->z;
            if (!ownEvent_ && !followEvent_) {
                inputBeforeEvent_ = od::port::player().inputMode;
                drawnBeforeEvent_ = (od::port::player().flags & od::port::kActorDrawn) != 0;
            }
            od::port::eventStart(e->hasPos ? &eventPoint_ : nullptr, nullptr, e->param, e->window, e->message);
            eventMenus_ = e->menus; // what its talker's choices open plays here, with the event
            eventHints_ = e->hintsMessage;
            eventSaveMenu_ = {};
            if (follows) {
                SDL_Log("own world: player %u's event, message %u: played here on their pad", unsigned(e->follows), unsigned(e->message));
                followEvent_ = true;
                followPads_.start();
                followEndAt_.reset();
                followSinceEnd_ = 0;
                od::port::setScriptStartsBlocked(true);
            } else {
                SDL_Log("own world: an event of the room's world, message %u%s", unsigned(e->message),
                        e->reach == od::port::EventReach::Owner ? "" : " (the others see it too)");
                ownEvent_ = true;
                sharedEvent_ = e->reach != od::port::EventReach::Owner;
                eventTick_ = 0;
                sentEventPad_ = 0;
            }
        }
        break;
    case WorldMessageType::NetEventPad:
        if (const auto pad = ScenePads::decode(m.netState); pad && followEvent_) followPads_.heard(pad->at, pad->pad);
        break;
    case WorldMessageType::NetEventEnd: // the owner's event of what this game plays ended at their tick
        if (followEvent_ && m.netState.size() == 4)
            followEndAt_ = uint32_t(m.netState[0]) | uint32_t(m.netState[1]) << 8 | uint32_t(m.netState[2]) << 16 | uint32_t(m.netState[3]) << 24;
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
        // the story flags the room's world entered the room with: the scene's rebuild replays the room's logic from them,
        // as the world's did (this game may have come in after the room's first visit set its flags)
        if (m.netState.size() == 7 + od::port::kStoryFlagBytes && roomEntry_ && room_ && roomEntry_->room == *room_)
            std::copy(m.netState.begin() + 7, m.netState.end(), roomEntry_->flags.bytes.begin());
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
        sceneAlone_ = !sceneHeard_;
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
        if (gateAsked_ && gateExit_) {
            // the gate this player waited at opens: the exit's own script and scene go on here, as in the game (a door
            // actor's talk, a door leaf opening, a passage walked through)
            const od::port::TransitionRequest exit = *std::exchange(gateExit_, std::nullopt);
            gateAsked_ = false;
            doorPass_ = true;
            if (move.size() >= 8) {
                const auto half = [&](size_t i) { return int16_t(uint16_t(move[i] | move[i + 1] << 8)); };
                doorArrival_ = std::array<int16_t, 5>{int16_t(move[0]), int16_t(move[1]), half(2), half(4), half(6)};
            }
            if (exit.kind == uint8_t(od::port::TransitionKind::DoorActor)) {
                od::port::transitionRequest().kind = exit.kind;
                od::port::setStoryFlag(od::port::kFlagTransitionArmed);
            } else {
                // the request as the exit's trigger armed it, its flag with it: the flag is what starts the exit's
                // script, and the wait took it (gateTurn) -- without it the field waits for a script that never runs
                od::port::transitionRequest() = exit;
                od::port::transitionRequest().state = od::port::kTransitionAsked;
                od::port::setStoryFlag(od::port::kFlagTransitionArmed);
            }
            SDL_Log("own world: the gate opens: through the exit (kind %u)", unsigned(exit.kind));
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
        worldLost_ = true; // (a world that stopped, a session that ends: what waits for the world goes on)
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
        hostSave_ = m.netState;
        hostSaveWhole_ = true;
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
    for (Other& o : others_) o.doorShown = {}; // (set below for each ghost posed here)
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
        if (posed) // the door they open at the shown tick: the newest by then
            for (auto it = o.doors.rbegin(); it != o.doors.rend(); ++it)
                if (int32_t(it->first - uint32_t(tick)) <= 0) {
                    o.doorShown = it->second;
                    o.doorShownTick = it->first;
                    break;
                }
        if (!posed) o.stepped = -1; // where it is next posed it starts at the shown tick
        else if (od::LoadedFileData* files = ghosts.files(int(slot))) {
            const od::ScopedLoadedFiles bound(*files);
            const od::port::NetPlayerFields& f = o.from.fields();
            for (int64_t t = o.stepped < 0 ? tick : o.stepped + 1; t <= tick; ++t)
                for (size_t c = 0; c < 2; ++c) {
                    // the start in play on tick t: the newest heard by then (the latest fields: none kept yet)
                    const od::port::NetPlayerChannel* in = &f.channels[c];
                    std::deque<od::port::NetPlayerChannel>& q = o.starts[c];
                    for (auto it = q.rbegin(); it != q.rend(); ++it)
                        if (int32_t(it->tick - uint32_t(t)) <= 0) {
                            in = &*it;
                            break;
                        }
                    // what started before the newest one of them goes (it is played)
                    while (q.size() > 1 && int32_t(q[1].tick - uint32_t(t)) <= 0) q.pop_front();
                    o.tracks[c].step(*in, f.stageAnims, uint32_t(t));
                }
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

void OwnWorldClient::endFrame(const od::CollisionRoom& room) {
    if (!room_) return;
    // what the tick's body sends -- this tick's shots -- leaves as it ends, not with the next frame
    struct Flush {
        WorldPipe& pipe;
        ~Flush() { pipe.flush(SDL_GetTicksNS()); }
    } flushAtEnd{*pipe_};
    if (ownEvent_) {
        od::port::talkerMenusTick(eventSaveMenu_, eventMenus_, eventHints_);
        od::port::EventState& ev = od::port::eventState();
        if (ev.speakerAnimation < od::port::kSpeakerAnimationCount) {
            pipe_->sendReliable(playerId_, WorldMessageType::NetSpeaker, {ev.speakerAnimation});
            ev.speakerAnimation = od::port::kNoSpeakerAnimation;
        }
        ownEventOver();
    } else if (followEvent_) {
        od::port::EventState& ev = od::port::eventState();
        ev.speakerAnimation = od::port::kNoSpeakerAnimation; // the speaker is the world's: its owner's game asks it
        // over here as on its owner's game; else ended where it did (a while later: the pad comes late), or at once
        constexpr uint32_t kFollowGraceTicks = 30;
        if (ev.active == 0) {
            restoreEventInput(ev);
            followEvent_ = false;
            od::port::setScriptStartsBlocked(false);
        } else if (followEndAt_ && (*followEndAt_ == 0 || followPads_.tick() > *followEndAt_ + kFollowGraceTicks ||
                                    ++followSinceEnd_ > 4 * kFollowGraceTicks)) {
            followEnd();
        }
    }
    downedTurn();
    actors_->submitLockPoints();
    od::port::hitListLockOnOnly(&room);
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
        if (followEvent_) followEnd(); // the session moves this player: another's event played here is over for them
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
    ownEventOver(); // an event the scene began over (a talk's answer started it) ends under it, as in the normal game
    // a scene is over once its script has run here and ended (one the world started begins on the game's next frame)
    // (its own script: another one ending, a door's, is not the scene's end)
    if (od::port::scriptState().inUse() && od::port::scriptState().id == sceneScript_) sceneRan_ = true;
    if (!sceneRan_ || sceneStart_) return;
    const od::port::GameState& g = od::port::gameState();
    if (od::port::scriptState().inUse() || od::port::transitionRequest().state != od::port::kTransitionNone ||
        g.gameMode() != od::port::GameMode::Field || g.fieldStep() != od::port::FieldStep::Playing)
        return;
    std::optional<std::vector<uint8_t>> at = std::exchange(sceneEndAt_, std::nullopt);
    const bool sameRoom = sceneRoom_ && room_ && *sceneRoom_ == *room_;
    // (a room the scene went to: what its script left there is its world's to make -- its owner's game tells it)
    if (const std::vector<od::port::SpawnNote> left = od::port::spawnNotesEnd(); !sameRoom && sceneOwner_ == playerId_)
        sceneLeft_ = od::port::encodeSpawnNotes(left);
    // (the world of a room the scene went to never played it: what it shows is not the scene's)
    if (!at && sceneHeard_ && sameRoom) {
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
    localScene_ = false;
    sceneRoom_.reset();
    const WorldRoom here = *room_;
    if (od::port::storyStageRunsAlone(uint8_t(here.stage))) { // the scene went to a race: it runs here alone
        if (joined_) pipe_->sendReliable(playerId_, WorldMessageType::NetLeave, {});
        room_.reset();
        joined_ = false;
        takeSceneValues();
        return;
    }
    od::port::presentationLoopsClear();
    // its script went to a room whose first field tick is still to come: that tick decides, as on any entry (the room
    // setup's actors stay for a scene it starts)
    const bool firstTickToCome = std::exchange(sceneMoved_, false);
    if (!firstTickToCome) {
        if (!sameRoom) dropRoomEvent(); // the room the scene went to: its world's from here, its messages too
        freeLocalActors();
    }
    bindWorld(here.stage, here.sub);
    entering_ = firstTickToCome;
    if (sameRoom && at) sceneEnd(*at);
    SDL_Log("own world: the scene ended, back in the room's world");
}

// The scene that brought this game to this room is over on the room's first tick (its script done there): this game is
// in the room's world from this tick on, the room setup's actors gone as on any entry
void OwnWorldClient::leaveSceneHere() {
    if (const std::vector<od::port::SpawnNote> left = od::port::spawnNotesEnd(); sceneOwner_ == playerId_)
        sceneLeft_ = od::port::encodeSpawnNotes(left);
    localScene_ = false;
    sceneRoom_.reset();
    const WorldRoom here = *room_;
    if (od::port::storyStageRunsAlone(uint8_t(here.stage))) { // the scene went to a race: it runs here alone
        if (joined_) pipe_->sendReliable(playerId_, WorldMessageType::NetLeave, {});
        room_.reset();
        joined_ = false;
        takeSceneValues();
        return;
    }
    od::port::presentationLoopsClear();
    freeLocalActors();
    bindWorld(here.stage, here.sub);
    SDL_Log("own world: the scene ended on the room's first tick, the room is its world's");
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

// A room change of this player's own event (its message's warp) or of a scene only this game plays: the story goes
// there, and so does everyone (the coordinator moves the others next to where this one arrives)
void OwnWorldClient::storyMoveTurn() {
    const od::port::TransitionRequest& r = od::port::transitionRequest();
    const bool asked = r.state == od::port::kTransitionAsked || r.state == od::port::kTransitionAccepted;
    if (!asked) {
        storyMoveSent_ = false;
        return;
    }
    if (storyMoveSent_ || !room_ || (r.destStage == uint8_t(room_->stage) && r.destRoom == room_->sub)) return;
    if (followEvent_) { // another's event played here: its room change is theirs, and the session brings this one along
        od::port::transitionRequest().state = od::port::kTransitionNone;
        od::port::clearStoryFlag(od::port::kFlagTransitionArmed);
        return;
    }
    if (localScene_ && od::port::storyScriptOwnRoom(sceneScript_)) return; // a menu's room: this player's alone
    const bool ownStory = ownEvent_ || (localScene_ && !sceneHeard_ && sceneOwner_ == playerId_);
    if (!ownStory) return;
    std::vector<uint8_t> move = {r.destStage, r.destRoom};
    const auto half = [&](int16_t v) {
        move.push_back(uint8_t(v));
        move.push_back(uint8_t(uint16_t(v) >> 8));
    };
    half(r.destination.x);
    half(r.destination.y);
    half(r.destination.z);
    half(r.destination.yaw);
    move.push_back(r.fadeIn);
    move.push_back(r.fadeOut);
    pipe_->sendReliable(playerId_, WorldMessageType::NetStoryMove, std::move(move));
    storyMoveSent_ = true;
    SDL_Log("own world: this player's story goes to ST%02X sub %u: the others come", unsigned(r.destStage), unsigned(r.destRoom));
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
    actors_->repose(lifted_->slot); // (its bones were posed where the world had it: all of it at the hands)
}

// This player's event of the room's world is over here: its world ends it there (its speaker is free again), and the
// others' copies of it
void OwnWorldClient::ownEventOver() {
    const od::port::EventState& ev = od::port::eventState();
    if (!ownEvent_ || ev.active != 0) return;
    restoreEventInput(ev);
    pipe_->sendReliable(playerId_, WorldMessageType::NetEventEnd,
                        {uint8_t(eventTick_), uint8_t(eventTick_ >> 8), uint8_t(eventTick_ >> 16), uint8_t(eventTick_ >> 24)});
    ownEvent_ = false;
    sharedEvent_ = false;
}

bool OwnWorldClient::eventTicking() const {
    const od::port::EventState& ev = od::port::eventState();
    return ev.active != 0 && !(ev.eventMode() == od::port::EventMode::Talk && ev.state == uint8_t(od::port::TalkEventState::WaitPlayer));
}

// An event of the world's leaves this game's input as it found it: one that keeps the input off as it ends (a mission's
// briefing) is followed in the normal game by the room's logic giving it back -- in a session that logic runs in the
// world, on the world's players
void OwnWorldClient::restoreEventInput(const od::port::EventState& ev) {
    if (ev.flags & od::port::kEventKeepsInput) od::port::player().inputMode = inputBeforeEvent_;
}

void OwnWorldClient::followEnd() {
    od::port::EventState& ev = od::port::eventState();
    if (ev.active) {
        od::port::windowClose(int8_t(ev.window), od::port::WindowClose::Force);
        od::port::eventAbort();
        restoreEventInput(ev);
        // a message that hid this Mega Man shows him again at its own end (its room change's, text_script's warp): cut
        // short here, that end never plays
        od::port::player().setDrawn(drawnBeforeEvent_);
    }
    followEvent_ = false;
    od::port::setScriptStartsBlocked(false);
}

void OwnWorldClient::overridePad(uint16_t& buttons) {
    const uint16_t raw = buttons;
    const uint16_t before = std::exchange(padBefore_, raw);
    rawPressed_ = uint16_t(raw & ~before);
    if (ownEvent_ && sharedEvent_ && eventTicking()) { // what drives the others' copies, stamped with the event's tick
        // what was held as it began (the press that started it) is nobody's press in it until let go: the copies start
        // from nothing held, and so does it here
        if (eventTick_ == 0) eventHeld_ = uint16_t(raw & before);
        eventHeld_ &= raw;
        buttons = uint16_t(raw & ~eventHeld_);
        if (buttons != sentEventPad_) {
            sentEventPad_ = buttons;
            pipe_->sendReliable(playerId_, WorldMessageType::NetEventPad, ScenePads::encode(buttons, eventTick_, 0));
        }
        ++eventTick_;
    } else if (followEvent_) {
        buttons = eventTicking() ? followPads_.step() : 0;
        return;
    }
    if (!localScene_) return;
    if (heldHere_) { // a scene waiting for everyone: nobody's pad reaches it or its Mega Man, here as in the world
        buttons = 0;
        return;
    }
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

namespace {
int32_t wholeToFixed(int16_t v) { return int32_t(uint32_t(uint16_t(v)) << 16); }
int16_t between(int16_t from, int16_t to, int num, int den) { return int16_t(from + (int(to) - int(from)) * num / den); }
int16_t yawBetween(int16_t from, int16_t to, int num, int den) {
    const int d = int((uint32_t(uint16_t(to)) - uint32_t(uint16_t(from)) + 0x800u) & 0xfffu) - 0x800;
    return int16_t((int(from) + d * num / den) & 0xfff);
}
void poseMirrorLeaf(uint32_t at, const std::array<int16_t, 3>& pos, int16_t yaw) {
    od::port::StaticActor& l = od::port::staticActorAt(at);
    l.prevPos = l.pos;
    l.prevRotation = l.rotation;
    l.pos.x = wholeToFixed(pos[0]);
    l.pos.y = wholeToFixed(pos[1]);
    l.pos.z = wholeToFixed(pos[2]);
    l.rotation.y = yaw;
}
// this game's door actor in that slot (the room's setup placed it as on theirs); nullptr when it is not one
od::port::StaticActor* mirrorDoorActor(uint8_t slot) {
    if (od::port::actorSlotClass(slot) != od::port::kStaticClass) return nullptr;
    od::port::StaticActor& door = od::port::staticActorAt(od::port::actorBySlot(slot));
    return (door.flags & od::port::kActorLive) && door.type == od::port::kDoorActorType ? &door : nullptr;
}
} // namespace

void OwnWorldClient::doorMirrorEnd(DoorMirror& m) {
    for (size_t k = 0; k < m.leafCount; ++k)
        if (m.leafAt[k] != 0) od::port::freeActor(od::port::staticActorAt(m.leafAt[k]));
    if (m.segment != od::port::kNetNoSegment) od::port::segmentSetDoorsHidden(m.segment, false);
    if (od::port::StaticActor* door = m.doorSlot != 0 ? mirrorDoorActor(m.doorSlot) : nullptr) {
        door->prevPos.y = door->pos.y;
        door->pos.y = wholeToFixed(m.doorBaseY);
    }
    const uint32_t told = m.toldTick; // (kept: a door shown again from before it is not a new one)
    m = DoorMirror{};
    m.toldTick = told;
}

// The doors the others open, shown here as on their screen (with no camera of theirs): their leaves posed as theirs tick by
// tick, the door actor carrying them at its height; once they are through, the leaves go back closed and go, the door actor
// back up to its place
void OwnWorldClient::doorMirrorTurn() {
    constexpr int kLeafCloseTicks = 10; // a leaf back to closed (as a sliding leaf's 10-tick slide)
    constexpr int kDoorBackTicks = 30;  // the door actor back to its place
    for (size_t id = 0; id < others_.size() && id < doorMirrors_.size(); ++id) {
        DoorMirror& m = doorMirrors_[id];
        for (size_t k = 0; k < m.leafCount; ++k) { // a leaf's slot taken since (a scene's rebuild): not ours any more
            if (m.leafAt[k] == 0) continue;
            const od::port::StaticActor& l = od::port::staticActorAt(m.leafAt[k]);
            if (!(l.flags & od::port::kActorLive) || l.type != m.leaves[k].type || l.variant != m.leaves[k].variant) m.leafAt[k] = 0;
        }
        // their door at the tick shown, taken only going forward: open, a state of that tick or after; closing, a newer one
        // (their ghost shown again from before, after they left, brings back the states it showed already)
        const Other& o = others_[id];
        const bool fresh = o.doorShown.any() && (m.any() && m.closing < 0 ? int32_t(o.doorShownTick - m.toldTick) >= 0
                                                                            : int32_t(o.doorShownTick - m.toldTick) > 0 || m.toldTick == 0);
        const od::port::NetDoor told = fresh ? o.doorShown : od::port::NetDoor{};
        if (fresh && m.closing >= 0) doorMirrorEnd(m); // a newer door while the last one closes: that one is done
        if (fresh) m.toldTick = o.doorShownTick;
        if (told.any()) {
            if (!m.any()) {
                m.leafCount = told.leafCount;
                for (size_t k = 0; k < told.leafCount; ++k) {
                    const od::port::NetDoorLeaf& t = told.leaves[k];
                    m.leaves[k] = t;
                    const uint32_t at = od::port::allocActor60(0);
                    if (at == 0) continue;
                    od::port::StaticActor& l = od::port::staticActorAt(at);
                    l.flags = od::port::kActorLiveDrawn;
                    l.type = t.type;
                    l.variant = t.variant;
                    od::port::DoorLeafParams(l).clear();
                    if (!od::port::bindActorModel(l, l.variant)) {
                        od::port::freeActor(l);
                        continue;
                    }
                    // placed and still: its pose is theirs (the leaves' pass leaves an idle / stopped leaf where it is)
                    l.state = uint8_t(od::port::DoorLeafState::Moving);
                    l.subState = 0;
                    l.step = 0;
                    l.stepCounter = 0;
                    l.velocity.x = 0;
                    l.velocity.y = 0;
                    l.velocity.z = 0;
                    l.rotation.x = 0;
                    l.rotation.z = 0;
                    poseMirrorLeaf(at, t.pos, t.yaw);
                    l.prevPos = l.pos;
                    l.prevRotation = l.rotation;
                    m.leafAt[k] = at;
                }
                m.segment = told.leafCount != 0 ? told.segment : od::port::kNetNoSegment;
                if (m.segment != od::port::kNetNoSegment) od::port::segmentSetDoorsHidden(m.segment, true);
                if (const od::port::StaticActor* door = told.doorSlot != 0 ? mirrorDoorActor(told.doorSlot) : nullptr) {
                    m.doorSlot = told.doorSlot;
                    m.doorBaseY = door->pos.y.whole;
                    m.doorY = m.doorBaseY;
                }
                SDL_Log("own world: player %zu opens a door here (%u leaves, segment %u, door actor %02x)", id, unsigned(m.leafCount),
                        unsigned(m.segment), unsigned(m.doorSlot));
            }
            for (size_t k = 0; k < m.leafCount && k < told.leafCount; ++k) {
                m.leaves[k] = told.leaves[k];
                if (m.leafAt[k] != 0) poseMirrorLeaf(m.leafAt[k], told.leaves[k].pos, told.leaves[k].yaw);
            }
            if (m.doorSlot != 0 && told.doorSlot == m.doorSlot)
                if (od::port::StaticActor* door = mirrorDoorActor(m.doorSlot)) {
                    m.doorY = told.doorY;
                    door->prevPos.y = door->pos.y;
                    door->pos.y = wholeToFixed(m.doorY);
                }
            continue;
        }
        if (!m.any()) continue;
        ++m.closing; // they are through (or gone from here): it closes behind them
        const int leafT = std::min(m.closing, kLeafCloseTicks), doorT = std::min(m.closing, kDoorBackTicks);
        for (size_t k = 0; k < m.leafCount; ++k) {
            if (m.leafAt[k] == 0) continue;
            const od::port::NetDoorLeaf& l = m.leaves[k];
            const std::array<int16_t, 3> pos = {between(l.pos[0], l.closedPos[0], leafT, kLeafCloseTicks),
                                                between(l.pos[1], l.closedPos[1], leafT, kLeafCloseTicks),
                                                between(l.pos[2], l.closedPos[2], leafT, kLeafCloseTicks)};
            poseMirrorLeaf(m.leafAt[k], pos, yawBetween(l.yaw, l.closedYaw, leafT, kLeafCloseTicks));
        }
        if (od::port::StaticActor* door = m.doorSlot != 0 ? mirrorDoorActor(m.doorSlot) : nullptr) {
            door->prevPos.y = door->pos.y;
            door->pos.y = wholeToFixed(between(m.doorY, m.doorBaseY, doorT, kDoorBackTicks));
        }
        // closed (a tick held there) and back: it goes
        const int lasts = std::max(m.leafCount != 0 ? kLeafCloseTicks + 1 : 0, m.doorSlot != 0 ? kDoorBackTicks : 0);
        if (m.closing >= lasts) {
            SDL_Log("own world: player %zu's door closed here", id);
            doorMirrorEnd(m);
        }
    }
}

// An exit this player has taken: not while a story boss is being fought; at a gate they wait, with who is at it
void OwnWorldClient::gateTurn() {
    if (gateGlow_) {
        if (--gateGlowTicks_ <= 0) gateGlow_.reset();
        else if (gateGlowTicks_ % 20 == 0) od::port::doorGlowAt((*gateGlow_)[0], (*gateGlow_)[1], (*gateGlow_)[2]);
    }
    od::port::TransitionRequest& r = od::port::transitionRequest();
    const od::port::GameState& g = od::port::gameState();
    const bool asked = od::port::storyFlag(od::port::kFlagTransitionArmed) || r.state == od::port::kTransitionAsked;
    if (asked && r.kind == uint8_t(od::port::TransitionKind::DoorActor)) od::port::doorActorExitAhead(od::port::player());
    if (coordinator_ && noticeTicks_ > 0 && ((rawPressed_ & od::port::player().button(od::port::PlayerButton::Use)) != 0 || localTick_ == testForceTick_))
        pipe_->sendReliable(playerId_, WorldMessageType::NetGateForce, {});
    bool stands = false;
    const bool gateWasAsked = gateAsked_;
    if (asked && g.gameMode() == od::port::GameMode::Field && g.fieldStep() == od::port::FieldStep::Playing) {
        const bool mission = od::port::storyPlaceOf(uint8_t(room_->stage), room_->sub, g.story) == od::port::StoryPlaceKind::Mission;
        // the gate opened for this exit: its script runs, its own asks too (the pass lasts until the room changes)
        const bool passing = doorPass_;
        const bool gated = !passing && (mission || od::port::storyDoorGated(uint8_t(room_->stage), r.destStage, r.destRoom, g.story));
        const od::port::TransitionRequest exitAsked = r; // the exit's request, as it asked
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
                gateExit_ = exitAsked;
            }
        }
    }
    constexpr int kWalkedOff = 0x180;
    if (stands && !gateWasAsked) {
        gatePlace_ = od::port::player().pos;
    } else if (gateAsked_ && od::port::distance3(gatePlace_, od::port::player().pos) > kWalkedOff) {
        pipe_->sendReliable(playerId_, WorldMessageType::NetGateLeave, {});
        gateAsked_ = false;
        gateExit_.reset();
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
    // what this Mega Man holds, at its hands as this tick's player update left them (its update has run: no tick behind)
    if (late) liftPlace();
}

} // namespace od::app
