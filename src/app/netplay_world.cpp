// Netplay's shared world (see netplay_world.hpp).
#include "app/netplay_world.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

#include "app/room_mesh.hpp"
#include "game/breakable_walls.hpp"
#include "game/hit_list.hpp"
#include "game/loaded_data_fingerprint.hpp"
#include "game/net_puppets.hpp"
#include "game/net_world.hpp"
#include "game/player/player_state.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"

namespace od::app {

namespace {

constexpr uint64_t kRespawnNs = 3'000'000'000ull;
constexpr uint64_t kPuppetGoneNs = 1'000'000'000ull;
constexpr uint64_t kScriptWaitNs = 60'000'000'000ull;
constexpr uint8_t kExitScript = 0, kTravelScript = 7, kShopScript = 8; // not shared (rooms come through Travel)
constexpr uint8_t kStandInSlot = 0x01;  // the stand-ins' owner slots 0x01..0x07 (no actor lives there)
constexpr int16_t kPlayerHitHeight = 0x58; // the player's entry above its feet (game/player/player_update)

bool sameRoom(const od::port::GhostRoom& a, const od::port::GhostRoom& b) { return a.stage == b.stage && a.sub == b.sub; }

uint64_t localFingerprint() { return od::port::loadedDataFingerprint(loadedDataIdentity()); }

} // namespace

void NetWorld::begin(bool host, uint8_t localId) {
    end();
    active_ = true;
    host_ = host;
    localId_ = localId;
    installHooks();
    SDL_Log("netplay: shared world on (%s)", host ? "host: this world simulates" : "client: the host's world shown");
}

void NetWorld::end() {
    if (!active_) return;
    od::port::netWorldHooks() = {};
    od::port::hitListSetRemoteBodies({});
    od::port::puppetsReset();
    od::port::firesReset();
    active_ = false;
    outgoing_.clear();
    tracks_ = {};
    capture_.clear();
    clients_.clear();
    respawnAt_.clear();
    sharedScript_ = -1;
    scriptSentTo_.clear();
    scriptDone_.clear();
    waiting_.clear();
    holdStarting_ = false;
    shown_.clear();
    noSlotLogged_.clear();
    allowScript_ = hostScript_ = -1;
    queuedScripts_.clear();
    travel_.reset();
    hostRoom_.reset();
    strayFrom_.reset();
    travelling_ = false;
    // a client's own story back (the room the app loads next is its own world's)
    if (od::port::netOwnStory()) od::port::showStory(*od::port::netOwnStory());
    od::port::netOwnStory().reset();
    od::port::netHostStory().reset();
    heldEvents_.clear();
    partsSeen_ = 0;
    bodyPlayers_.clear();
    wasDown_ = false;
    stats_ = {};
}

void NetWorld::installHooks() {
    od::port::NetWorldHooks& h = od::port::netWorldHooks();
    h = {};
    h.holdDeath = true;
    if (host_) {
        h.scriptStarted = [this](uint8_t id) {
            if (id == kExitScript || id == kTravelScript || id == kShopScript) return;
            if (sharedScript_ >= 0) sharedScriptEnded(); // one script ended and started the next in the same tick
            sharedScript_ = id;
            scriptSentTo_ = clients_;
            SDL_Log("netplay: script %02x started: sent to %zu client(s)", unsigned(id), clients_.size());
            NetEvent e;
            e.kind = EventKind::ScriptStarted;
            e.id = id;
            send(od::net::kAllPlayers, e);
        };
        h.pickupSpawned = [this](const od::port::PickupSpawn& p) {
            NetEvent e;
            e.kind = EventKind::DropSpawned;
            e.pickup = p;
            send(od::net::kAllPlayers, e);
        };
        h.fireChanged = [this](const od::port::FireStep& f) {
            NetEvent e;
            e.kind = EventKind::FireChanged;
            e.fire = f;
            send(od::net::kAllPlayers, e);
        };
        h.wallBroken = [this](const od::port::WallStep& w) {
            NetEvent e;
            e.kind = EventKind::ObjectChanged;
            e.wall = w;
            send(od::net::kAllPlayers, e);
        };
    } else {
        h.mirror = true;
        h.scriptGate = [this](uint8_t id) { return id == kShopScript || int(id) == allowScript_; };
        h.wallHit = [this](uint32_t result, int attack) {
            HitClaim c = claimFromHere(lastTick_);
            c.target = ClaimTarget::Wall;
            c.wallResult = result;
            c.attack = uint32_t(attack);
            claim(c);
        };
        h.fireHit = [this](int16_t x, int16_t z, int damage) {
            HitClaim c = claimFromHere(lastTick_);
            c.target = ClaimTarget::Fire;
            c.contact[0] = x;
            c.contact[2] = z;
            c.attack = uint32_t(damage);
            claim(c);
        };
    }
}

void NetWorld::send(uint8_t to, NetEvent e) {
    e.room = od::port::ghostRoomOf();
    outgoing_.push_back({to, std::move(e)});
}

HitClaim NetWorld::claimFromHere(uint32_t tick) const {
    const od::port::Player& p = od::port::player();
    HitClaim c;
    c.weapon = uint8_t(p.activeBuster);
    c.origin[0] = p.pos.x.whole;
    c.origin[1] = p.pos.y.whole;
    c.origin[2] = p.pos.z.whole;
    c.dir[0] = p.rotation.x;
    c.dir[1] = p.rotation.y;
    c.dir[2] = p.rotation.z;
    c.tick = tick;
    return c;
}

void NetWorld::claim(const HitClaim& c) {
    ++stats_.claimsSent;
    if (host_) {
        hostClaim(localId_, c);
        return;
    }
    NetEvent e;
    e.kind = EventKind::HitClaim;
    e.claim = c;
    send(od::net::kHostPlayerId, e);
}

void NetWorld::afterTick(uint32_t tick) {
    if (!active_) return;
    lastTick_ = tick;
    // both: a local attack on a stand-in is a claim on its player
    for (size_t k = 0; k < bodyPlayers_.size(); ++k) {
        const od::port::HitBox& box = od::port::hitListRemoteBodyBox(k);
        if (!(box.result & od::port::kHitAttackByPlayer)) continue;
        HitClaim c = claimFromHere(tick);
        c.target = ClaimTarget::Player;
        c.playerId = bodyPlayers_[k];
        c.attack = box.result & (od::port::kHitDamageMask | od::port::kHitFlagsMask);
        c.contact[0] = box.contact.x;
        c.contact[1] = box.contact.y;
        c.contact[2] = box.contact.z;
        claim(c);
    }
    // both: where the player entered this room (its respawn point)
    const od::port::GhostRoom room = od::port::ghostRoomOf();
    if (room.fieldPlay && (!entryValid_ || !sameRoom(room, entryRoom_))) {
        const od::port::Player& p = od::port::player();
        entryRoom_ = room;
        entryValid_ = true;
        entryPos_[0] = p.pos.x;
        entryPos_[1] = p.pos.y;
        entryPos_[2] = p.pos.z;
        entryYaw_ = p.rotation.y;
    }
    // both: downed
    const bool down = od::port::playerDown();
    if (down && !wasDown_) {
        if (host_) {
            respawnAt_[localId_] = 0;
        } else {
            NetEvent e;
            e.kind = EventKind::Downed;
            e.id = localId_;
            send(od::net::kHostPlayerId, e);
        }
    }
    wasDown_ = down;
    const od::port::ScriptState& script = od::port::scriptState();
    const bool running = (script.flags & od::port::kScriptRunning) != 0;
    if (host_) {
        // the shared actors and their generations (a slot's new occupant, or one that was gone, is a new actor)
        std::vector<od::port::PuppetState> world = od::port::captureWorld();
        std::array<bool, 256> seen{};
        capture_.clear();
        for (const od::port::PuppetState& s : world) {
            SlotTrack& t = tracks_[s.hostSlot];
            if (!t.present || t.routine != s.routine) ++t.generation;
            t.present = true;
            t.routine = s.routine;
            seen[s.hostSlot] = true;
            capture_.push_back({t.generation, s});
        }
        for (size_t slot = 0; slot < tracks_.size(); ++slot)
            if (!seen[slot]) tracks_[slot].present = false;
        // the shared script's end: hold for the clients that have not finished it
        if (sharedScript_ >= 0 && !(running && script.id == uint8_t(sharedScript_))) sharedScriptEnded();
    } else {
        // the host's script ended here (or another took its place)
        if (hostScript_ >= 0 && !(running && script.id == uint8_t(hostScript_))) {
            SDL_Log("netplay: the host's script %02x ended here", unsigned(hostScript_));
            NetEvent e;
            e.kind = EventKind::ScriptDone;
            e.id = uint8_t(hostScript_);
            send(od::net::kHostPlayerId, e);
            hostScript_ = -1;
            if (!queuedScripts_.empty()) { // the host's next one, held while this one ran
                const uint8_t next = queuedScripts_.front();
                queuedScripts_.erase(queuedScripts_.begin());
                clientStartScript(next);
            }
        }
        // a local attack on a puppet
        for (const od::port::PuppetHit& hit : od::port::takePuppetHits()) {
            const auto it = std::find_if(shown_.begin(), shown_.end(),
                                         [&](const auto& kv) { return kv.second.actor == hit.actor; });
            if (it == shown_.end()) continue;
            HitClaim c = claimFromHere(tick);
            c.target = ClaimTarget::Puppet;
            c.hostSlot = it->first;
            c.generation = it->second.generation;
            c.attack = hit.result & (od::port::kHitDamageMask | od::port::kHitFlagsMask);
            c.contact[0] = hit.contact[0];
            c.contact[1] = hit.contact[1];
            c.contact[2] = hit.contact[2];
            claim(c);
        }
    }
}

void NetWorld::hostClaim(uint8_t from, const HitClaim& c) {
    switch (c.target) {
    case ClaimTarget::Puppet: {
        const SlotTrack& t = tracks_[c.hostSlot];
        if (!t.present || t.generation != c.generation) {
            ++stats_.claimsDropped; // gone, or the slot holds another actor now
            return;
        }
        od::port::hitListQueueExternal(
            {c.hostSlot, c.attack | od::port::kHitAttackByPlayer, {c.contact[0], c.contact[1], c.contact[2]}});
        break;
    }
    case ClaimTarget::Wall: od::port::queueBreakableWallHit(c.wallResult, int(c.attack)); break;
    case ClaimTarget::Fire: od::port::fireQueueHit(c.contact[0], c.contact[2], int(c.attack)); break;
    case ClaimTarget::Player: {
        if (!combat_.friendlyFire || c.playerId == from) {
            ++stats_.claimsDropped;
            return;
        }
        const uint32_t attack = od::port::netPlayerHitAttack(c.attack, combat_.scale);
        if (c.playerId == localId_) {
            od::port::hitListQueueExternal(
                {od::port::player().slot, attack, {c.contact[0], c.contact[1], c.contact[2]}});
        } else {
            NetEvent e;
            e.kind = EventKind::PlayerHit;
            e.hit = {from, attack, {c.contact[0], c.contact[1], c.contact[2]}};
            send(c.playerId, e);
        }
        break;
    }
    }
    ++stats_.claimsApplied;
}

void NetWorld::hostApply(uint64_t nowNs, uint8_t from, const NetEvent& e) {
    const bool here = sameRoom(e.room, od::port::ghostRoomOf());
    switch (e.kind) {
    case EventKind::HitClaim:
        if (!here) {
            ++stats_.claimsDropped;
            return;
        }
        hostClaim(from, e.claim);
        break;
    case EventKind::Downed: respawnAt_[from] = nowNs + kRespawnNs; break;
    case EventKind::ScriptDone:
        scriptDone_.insert({e.id, from});
        waiting_.erase({e.id, from});
        break;
    default: break; // the host's own kinds: from a client they are ignored
    }
}

void NetWorld::clientApply(uint64_t nowNs, const NetEvent& e) {
    (void)nowNs;
    const bool here = sameRoom(e.room, od::port::ghostRoomOf());
    switch (e.kind) {
    case EventKind::PlayerHit:
        if (here)
            od::port::hitListQueueExternal(
                {od::port::player().slot, e.hit.attack, {e.hit.contact[0], e.hit.contact[1], e.hit.contact[2]}});
        break;
    case EventKind::Respawn:
        if (od::port::playerDown()) respawnHere();
        break;
    case EventKind::DropSpawned:
        if (here) od::port::spawnPickup(e.pickup);
        break;
    case EventKind::ObjectChanged:
        if (here) od::port::breakableWallApplyStep(e.wall);
        break;
    case EventKind::FireChanged:
        if (here) od::port::fireApplyStep(e.fire);
        break;
    case EventKind::ScriptStarted:
        if (!here || !od::port::ghostRoomOf().fieldPlay) { // not where it plays (or out of play): the host need not wait
            SDL_Log("netplay: the host's script %02x not played here (%s)", unsigned(e.id),
                    here ? "out of play" : "another room");
            NetEvent done;
            done.kind = EventKind::ScriptDone;
            done.id = e.id;
            send(od::net::kHostPlayerId, done);
        } else if (hostScript_ >= 0) {
            SDL_Log("netplay: the host's script %02x queued after %02x", unsigned(e.id), unsigned(hostScript_));
            queuedScripts_.push_back(e.id); // after the one running here
        } else {
            clientStartScript(e.id);
        }
        break;
    case EventKind::Travel: {
        hostRoom_ = e.travel;
        od::port::netHostStory() = e.travel.story;
        if (hostScript_ >= 0) { // the shared script changes the room here too (its own load); strayed() after its end
            SDL_Log("netplay: the host went to ST%02X sub %d in script %02x: this one's script takes the player there",
                    unsigned(e.travel.stage), e.travel.sub, unsigned(hostScript_));
            break;
        }
        travel_ = e.travel;
        travelling_ = true;
        // the room changes under the scripts: nothing more to wait for
        std::vector<uint8_t> done = std::move(queuedScripts_);
        queuedScripts_.clear();
        if (hostScript_ >= 0) done.push_back(uint8_t(hostScript_));
        hostScript_ = -1;
        for (uint8_t id : done) {
            NetEvent d;
            d.kind = EventKind::ScriptDone;
            d.id = id;
            send(od::net::kHostPlayerId, d);
        }
        break;
    }
    case EventKind::Settings: combat_ = e.settings; break;
    default: break; // the clients' own kinds
    }
}

void NetWorld::clientWorld(uint64_t nowNs, const WorldPart& part) {
    if (!sameRoom(part.room, od::port::ghostRoomOf()) || !od::port::ghostRoomOf().fieldPlay ||
        part.fingerprint != localFingerprint())
        return;
    lastWorldNs_ = nowNs;
    if (part.tick != partTick_) {
        partTick_ = part.tick;
        partsSeen_ = 0;
    }
    ++partsSeen_;
    for (const WorldPuppet& p : part.puppets) {
        Shown& s = shown_[p.state.hostSlot];
        s.lastTick = part.tick;
        if (s.actor != 0 && s.generation != p.generation) { // the slot holds another actor now
            od::port::puppetFree(s.actor);
            s.actor = 0;
        }
        s.generation = p.generation;
        s.lastNs = nowNs;
        if (s.actor == 0) {
            s.actor = od::port::puppetSpawn(p.state);
            if (s.actor == 0) {
                if (noSlotLogged_.insert({p.state.hostSlot, p.generation}).second)
                    SDL_Log("netplay: no free slot for the host's actor %02x (generation %u): not shown",
                            unsigned(p.state.hostSlot), unsigned(p.generation));
                continue;
            }
        }
        od::port::puppetApply(s.actor, p.state);
    }
    if (partsSeen_ < part.parts) return;
    // the whole step arrived: what it does not list is gone on the host (killed, burst)
    for (auto it = shown_.begin(); it != shown_.end();) {
        if (it->second.lastTick == part.tick) {
            ++it;
            continue;
        }
        if (it->second.actor != 0) od::port::puppetFree(it->second.actor);
        it = shown_.erase(it);
    }
}

void NetWorld::step(uint64_t nowNs, od::net::Session& session, const GhostView& ghosts,
                    const std::array<int, od::ghost::kMaxGhosts>& slotPlayer) {
    if (!active_) return;
    names_.clear();
    for (const od::net::PlayerStatus& p : session.status(nowNs).players) names_[p.playerId] = p.name;
    for (const od::net::ReceivedEvent& r : session.takeEvents()) {
        const auto e = decodeEvent(r.payload);
        if (!e) continue;
        if (host_) hostApply(nowNs, r.from, *e);
        else if (travelling_ && e->kind != EventKind::Travel && e->kind != EventKind::Settings)
            heldEvents_.push_back(*e); // for the room the client goes to: once it is there
        else clientApply(nowNs, *e);
    }
    if (host_) {
        if (holdStarting_) {
            holdSinceNs_ = nowNs;
            holdStarting_ = false;
        }
        if (!waiting_.empty() && nowNs - holdSinceNs_ >= kScriptWaitNs) {
            SDL_Log("netplay: %s still in the script after 60 s: going on", waitingFor().c_str());
            waiting_.clear();
        }
        for (auto it = respawnAt_.begin(); it != respawnAt_.end();) {
            if (it->second == 0) it->second = nowNs + kRespawnNs;
            if (nowNs < it->second) {
                ++it;
                continue;
            }
            if (it->first == localId_) {
                if (od::port::playerDown()) respawnHere();
            } else {
                NetEvent e;
                e.kind = EventKind::Respawn;
                e.id = it->first;
                send(it->first, e);
            }
            it = respawnAt_.erase(it);
        }
        if (od::port::ghostRoomOf().fieldPlay) { // every step, the last capture again while the ticks are held
            std::vector<std::array<int32_t, 3>> players;
            const od::port::Player& me = od::port::player();
            players.push_back({me.pos.x, me.pos.y, me.pos.z});
            for (int slot = 0; slot < od::ghost::kMaxGhosts; ++slot)
                if (const od::port::RemotePlayerState* g = ghosts.shownState(slot))
                    players.push_back({g->snapshot.pos[0], g->snapshot.pos[1], g->snapshot.pos[2]});
            const od::port::GhostRoom room = od::port::ghostRoomOf();
            bool trimmed = false;
            for (const auto& part :
                 encodeWorld(room, localFingerprint(), lastTick_, capture_, players, kWorldPartBytes, &trimmed))
                session.sendWorldState(nowNs, lastTick_, part);
            if (trimmed && trimmedLogged_.insert(uint16_t(room.stage << 8 | room.sub)).second)
                SDL_Log("netplay: ST%02X sub %u has more shared actors than %d parts hold: the farthest left out",
                        unsigned(room.stage), unsigned(room.sub), kMaxWorldParts);
        }
    } else {
        for (const od::net::ReceivedWorld& w : session.takeWorldStates())
            if (const auto part = decodeWorld(w.payload)) clientWorld(nowNs, *part);
        // a puppet unheard of for 1 s while the steps arrive (its parts lost) is gone; none while the host sends none
        const bool arriving = nowNs - lastWorldNs_ < kPuppetGoneNs;
        for (auto it = shown_.begin(); it != shown_.end();) {
            if (!arriving || nowNs - it->second.lastNs < kPuppetGoneNs) {
                ++it;
                continue;
            }
            if (it->second.actor != 0) od::port::puppetFree(it->second.actor);
            it = shown_.erase(it);
        }
    }
    // both: the stand-ins of the shown ghosts while friendly fire is on
    std::vector<od::port::RemoteBody> bodies;
    bodyPlayers_.clear();
    if (combat_.friendlyFire)
        for (int slot = 0; slot < od::ghost::kMaxGhosts; ++slot) {
            const od::port::RemotePlayerState* g = ghosts.shownState(slot);
            if (!g || slotPlayer[size_t(slot)] < 0) continue;
            od::port::RemoteBody b;
            b.slot = uint8_t(kStandInSlot + slot);
            b.pos[0] = int16_t(g->snapshot.pos[0] >> 16);
            b.pos[1] = int16_t((g->snapshot.pos[1] >> 16) - kPlayerHitHeight);
            b.pos[2] = int16_t(g->snapshot.pos[2] >> 16);
            b.attack = od::port::kHitAttackSolid;
            bodies.push_back(b);
            bodyPlayers_.push_back(uint8_t(slotPlayer[size_t(slot)]));
        }
    od::port::hitListSetRemoteBodies(std::move(bodies));
    for (Outgoing& o : outgoing_) session.sendEvent(nowNs, o.to, encodeEvent(o.event));
    outgoing_.clear();
}

void NetWorld::peerJoined(uint8_t id) {
    if (!active_ || !host_) return;
    clients_.insert(id);
    NetEvent s;
    s.kind = EventKind::Settings;
    s.settings = combat_;
    send(id, s);
    if (room_) {
        NetEvent t;
        t.kind = EventKind::Travel;
        t.travel = *room_;
        t.travel.story = od::port::liveStory(); // the story now (scripts may have moved it since the room loaded)
        send(id, t);
    }
}

void NetWorld::peerLeft(uint8_t id) {
    clients_.erase(id);
    scriptSentTo_.erase(id);
    std::erase_if(waiting_, [&](const ScriptWait& w) { return w.second == id; });
    respawnAt_.erase(id);
    std::erase_if(outgoing_, [&](const Outgoing& o) { return o.to == id; });
}

void NetWorld::setCombat(const NetCombatSettings& c) {
    if (active_ && !host_) return; // the host's
    combat_ = c;
    combat_.scale = std::clamp(combat_.scale, 25, 100);
    if (!active_) return;
    NetEvent e;
    e.kind = EventKind::Settings;
    e.settings = combat_;
    send(od::net::kAllPlayers, e);
}

void NetWorld::hostTravelled(int stage, int sub, bool transition) {
    if (active_ && !host_) return;
    room_ = TravelOrder{stage, sub, transition, od::port::liveStory()};
    tracks_ = {}; // a new room: every shared actor a new one
    if (!active_) return;
    NetEvent e;
    e.kind = EventKind::Travel;
    e.travel = *room_;
    send(od::net::kAllPlayers, e);
}

std::optional<TravelOrder> NetWorld::takeTravel() { return std::exchange(travel_, std::nullopt); }

std::optional<TravelOrder> NetWorld::strayed() {
    if (!active_ || host_ || !hostRoom_ || travelling_) return std::nullopt;
    const od::port::GhostRoom room = od::port::ghostRoomOf();
    if (!room.fieldPlay) return std::nullopt;
    if (room.stage == hostRoom_->stage && room.sub == hostRoom_->sub) {
        strayFrom_.reset();
        return std::nullopt;
    }
    if (strayFrom_ && sameRoom(*strayFrom_, room)) return std::nullopt; // sent once from this room (a failed load)
    strayFrom_ = room;
    TravelOrder t = *hostRoom_;
    t.transition = true; // in play again, beside the host
    return t;
}

void NetWorld::sharedScriptEnded() {
    const bool wasHeld = !waiting_.empty();
    for (uint8_t id : scriptSentTo_)
        if (clients_.contains(id) && !scriptDone_.contains({uint8_t(sharedScript_), id}))
            waiting_.insert({uint8_t(sharedScript_), id});
    if (!wasHeld && !waiting_.empty()) holdStarting_ = true;
    sharedScript_ = -1;
    scriptSentTo_.clear();
}

void NetWorld::clientStartScript(uint8_t id) {
    allowScript_ = id;
    od::port::scriptRun(id);
    allowScript_ = -1;
    if ((od::port::scriptState().flags & od::port::kScriptRunning) && od::port::scriptState().id == id) {
        hostScript_ = id;
        SDL_Log("netplay: the host's script %02x started here", unsigned(id));
        return;
    }
    SDL_Log("netplay: the host's script %02x could not start here (the player down, or another script running)",
            unsigned(id));
    NetEvent done; // it could not start here (the player down): the host need not wait
    done.kind = EventKind::ScriptDone;
    done.id = id;
    send(od::net::kHostPlayerId, done);
}

void NetWorld::leavingRoom() {
    od::port::puppetsReset();
    od::port::firesReset();
    shown_.clear();
    noSlotLogged_.clear();
    partsSeen_ = 0;
}

void NetWorld::arrived(uint64_t nowNs) {
    od::port::puppetsForget(); // the room's init reused the freed slots: none of them is a puppet
    travelling_ = false;
    std::vector<NetEvent> held = std::move(heldEvents_);
    heldEvents_.clear();
    for (const NetEvent& e : held) clientApply(nowNs, e);
}

void NetWorld::respawnHere() {
    const od::port::Player& p = od::port::player();
    const bool entry = entryValid_ && sameRoom(entryRoom_, od::port::ghostRoomOf());
    const int32_t here[3] = {p.pos.x, p.pos.y, p.pos.z};
    od::port::playerRespawn(entry ? entryPos_ : here, entry ? entryYaw_ : p.rotation.y);
}

std::string NetWorld::waitingFor() const {
    std::set<uint8_t> ids;
    for (const ScriptWait& w : waiting_) ids.insert(w.second);
    std::string s;
    for (uint8_t id : ids) {
        if (!s.empty()) s += ", ";
        const auto it = names_.find(id);
        s += it != names_.end() ? it->second : "id " + std::to_string(id);
    }
    return s;
}

NetWorld::Stats NetWorld::stats() const {
    Stats s = stats_;
    s.puppets = 0;
    if (host_) s.puppets = int(capture_.size());
    else
        for (const auto& [slot, shown] : shown_)
            if (shown.actor != 0) ++s.puppets;
    return s;
}

} // namespace od::app
