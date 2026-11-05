// The coordinator's worlds (see world_host.hpp).
#include <utility>
#include "app/world_host.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace od::app {

namespace {
constexpr uint64_t kKeepAliveNs = 1'000'000'000;
} // namespace

WorldHost::World::~World() {
    if (!process) return;
    int code = 0;
    if (!SDL_WaitProcess(process, false, &code)) SDL_KillProcess(process, true);
    SDL_DestroyProcess(process);
}

WorldHost::WorldHost(std::filesystem::path exe, std::filesystem::path storySave, int32_t zenny)
    : exe_(std::move(exe)), storySave_(std::move(storySave)), global_(zenny) {}

WorldHost::~WorldHost() {
    for (auto& [room, world] : worlds_) world->link.sendReliable({WorldMessageType::Stop});
    const uint64_t until = SDL_GetTicksNS() + 2'000'000'000;
    while (SDL_GetTicksNS() < until) {
        bool settled = true;
        for (auto& [room, world] : worlds_) {
            world->link.poll(SDL_GetTicksNS());
            settled = settled && world->link.settled();
        }
        if (settled) break;
        SDL_DelayNS(5'000'000);
    }
    // ~World: a world that did not take the stop in time is ended
}

WorldHost::World& WorldHost::worldOf(WorldRoom room) {
    const auto it = worlds_.find(room);
    if (it != worlds_.end()) return *it->second;
    auto world = std::make_unique<World>();
    char stage[8], sub[8], port[8];
    std::snprintf(stage, sizeof stage, "%02X", unsigned(room.stage));
    std::snprintf(sub, sizeof sub, "%d", room.sub);
    std::snprintf(port, sizeof port, "%u", unsigned(world->link.port()));
    const std::string exe = exe_.string(), save = storySave_.string();
    std::vector<const char*> args = {exe.c_str(), "--world", stage, sub, "--world-link", port};
    std::string recordHex; // a world started on a stage in use takes what the stage's other worlds hold
    if (const auto it = stageRecords_.find(room.stage); it != stageRecords_.end()) {
        char byte[3];
        for (const uint8_t b : od::port::encodeStageRecord(it->second)) {
            std::snprintf(byte, sizeof byte, "%02x", unsigned(b));
            recordHex += byte;
        }
        args.push_back("--stage-record");
        args.push_back(recordHex.c_str());
    }
    std::string globalHex; // what the session's state is by now
    {
        char byte[3];
        for (const uint8_t b : od::port::encode(global_.all(uint8_t(room.stage)))) {
            std::snprintf(byte, sizeof byte, "%02x", unsigned(b));
            globalHex += byte;
        }
        args.push_back("--global-state");
        args.push_back(globalHex.c_str());
    }
    if (!save.empty()) {
        args.push_back("--story-save");
        args.push_back(save.c_str());
    }
    args.push_back(nullptr);
    world->process = SDL_CreateProcess(args.data(), false);
    if (!world->process) throw std::runtime_error(std::string("world process: ") + SDL_GetError());
    SDL_Log("coordinator: world ST%s sub %s started", stage, sub);
    return *worlds_.emplace(room, std::move(world)).first->second;
}

void WorldHost::dropEmptiedStage(int stage) {
    for (const auto& [room, world] : worlds_)
        if (room.stage == stage) return;
    global_.dropStage(uint8_t(stage));
    if (stageRecords_.erase(stage)) SDL_Log("coordinator: ST%02X emptied, its enemies are back", unsigned(stage));
}

void WorldHost::removeFromWorld(uint8_t playerId) {
    const std::optional<WorldRoom> was = playerRoom_.at(playerId);
    if (!was) return;
    playerRoom_[playerId].reset();
    briefs_[playerId].reset();
    gateLeave(playerId);
    const auto it = worlds_.find(*was);
    if (it == worlds_.end()) return;
    World& world = *it->second;
    std::erase(world.players, playerId);
    if (!world.players.empty()) {
        WorldMessage m{WorldMessageType::Leave};
        m.playerId = playerId;
        world.link.sendReliable(m);
        return;
    }
    world.link.sendReliable({WorldMessageType::Stop}); // its last player: the room is empty
    SDL_Log("coordinator: world ST%02X sub %d stopping", unsigned(was->stage), was->sub);
    stopping_.push_back(std::move(it->second));
    worlds_.erase(it);
    dropEmptiedStage(was->stage);
}

void WorldHost::enter(uint8_t playerId, WorldRoom room, const std::vector<uint8_t>& playerState) {
    if (playerRoom_.at(playerId) == room) return;
    World& world = worldOf(room); // before the old world goes: a move inside a stage does not empty it
    if (const auto from = playerRoom_.at(playerId)) {
        WorldMessage leaving{WorldMessageType::NetLeave};
        leaving.playerId = playerId;
        worlds_.at(*from)->link.sendReliable(leaving);
    }
    removeFromWorld(playerId);
    WorldMessage m{WorldMessageType::Join};
    m.playerId = playerId;
    m.playerState = playerState;
    world.link.sendReliable(m);
    world.players.push_back(playerId);
    playerRoom_[playerId] = room;
}

void WorldHost::state(uint8_t playerId, const std::vector<uint8_t>& playerState) {
    const std::optional<WorldRoom> room = playerRoom_.at(playerId);
    if (!room) return;
    WorldMessage m{WorldMessageType::State};
    m.playerId = playerId;
    m.playerState = playerState;
    worlds_.at(*room)->link.sendLatest(m);
}

void WorldHost::leave(uint8_t playerId) { removeFromWorld(playerId); }

void WorldHost::netJoin(uint8_t client) {
    const std::optional<WorldRoom> room = playerRoom_.at(client);
    if (!room) return;
    // what came from the room they left is over; where the coordinator sends them is not
    std::erase_if(netInbox_.at(client), [](const WorldMessage& held) { return held.type != WorldMessageType::NetMoveTo; });
    WorldMessage m{WorldMessageType::NetJoin};
    m.playerId = client;
    worlds_.at(*room)->link.sendReliable(m);
    WorldMessage state{WorldMessageType::GlobalChange}; // what has happened in the session by now
    state.playerId = client;
    state.netState = od::port::encode(global_.all(uint8_t(room->stage)));
    netInbox_.at(client).push_back(std::move(state));
}

void WorldHost::globalChanged(const od::port::GlobalChange& change, const World* fromWorld, std::optional<uint8_t> fromPlayer) {
    const od::port::GlobalMaster::Out out = global_.merge(change);
    for (const od::port::GlobalChange* c : {&out.shared, &out.stage})
        for (const od::port::GlobalChange::Bits& bits : c->flags)
            SDL_Log("coordinator: flags of byte %03x: set %02x, cleared %02x", unsigned(bits.byte), unsigned(bits.set), unsigned(bits.clear));
    if (out.zenny) SDL_Log("coordinator: zenny %d", int(*out.zenny));
    const auto withZenny = [&](od::port::GlobalChange c) {
        c.zennyNow = out.zenny;
        return c;
    };
    const std::vector<uint8_t> shared = od::port::encode(withZenny(out.shared));
    od::port::GlobalChange both = out.shared;
    both.flags.insert(both.flags.end(), out.stage.flags.begin(), out.stage.flags.end());
    const std::vector<uint8_t> inStage = od::port::encode(withZenny(both));
    od::port::GlobalChange justZenny;
    justZenny.stage = change.stage;
    const std::vector<uint8_t> zenny = od::port::encode(withZenny(justZenny));
    const bool nothingShared = out.shared.empty() && !out.zenny, nothingInStage = both.empty() && !out.zenny;
    for (auto& [room, world] : worlds_) {
        const bool here = room.stage == change.stage;
        WorldMessage m{WorldMessageType::GlobalChange};
        if (world.get() == fromWorld) {
            if (!out.zenny) continue;
            m.netState = zenny;
        } else if (here) {
            if (nothingInStage) continue;
            m.netState = inStage;
        } else {
            if (nothingShared) continue;
            m.netState = shared;
        }
        world->link.sendReliable(m);
    }
    for (uint8_t id = 0; id < kPlayers; ++id) {
        const std::optional<WorldRoom> room = playerRoom_[id];
        if (!room) continue;
        WorldMessage m{WorldMessageType::GlobalChange};
        m.playerId = id;
        if (fromPlayer == id) {
            if (!out.zenny) continue;
            m.netState = zenny;
        } else if (room->stage == change.stage) {
            if (nothingInStage) continue;
            m.netState = inStage;
        } else {
            if (nothingShared) continue;
            m.netState = shared;
        }
        netInbox_[id].push_back(std::move(m));
    }
}

void WorldHost::globalFromPlayer(uint8_t playerId, std::span<const uint8_t> bytes) {
    const std::optional<WorldRoom> room = playerRoom_.at(playerId);
    if (!room) return;
    if (auto change = od::port::decodeGlobalChange(bytes)) {
        change->stage = uint8_t(room->stage);
        globalChanged(*change, nullptr, playerId);
    }
}

std::vector<WorldMessage> WorldHost::takeNet(uint8_t client) { return std::exchange(netInbox_.at(client), {}); }

void WorldHost::netSend(uint8_t client, WorldMessageType type, std::vector<uint8_t> bytes) {
    const std::optional<WorldRoom> room = playerRoom_.at(client);
    if (!room) return;
    WorldMessage m{type};
    m.playerId = client;
    m.netState = std::move(bytes);
    worlds_.at(*room)->link.sendReliable(m);
}

void WorldHost::netSendLatest(uint8_t client, WorldMessageType type, std::vector<uint8_t> bytes) {
    const std::optional<WorldRoom> room = playerRoom_.at(client);
    if (!room) return;
    WorldMessage m{type};
    m.playerId = client;
    m.netState = std::move(bytes);
    worlds_.at(*room)->link.sendLatest(m);
}

void WorldHost::netAck(uint8_t client, uint16_t sequence) {
    const std::optional<WorldRoom> room = playerRoom_.at(client);
    if (!room) return;
    WorldMessage m{WorldMessageType::NetAck};
    m.playerId = client;
    m.netSequence = sequence;
    worlds_.at(*room)->link.sendLatest(m);
}

void WorldHost::moveTo(uint8_t playerId, const std::vector<uint8_t>& move) {
    WorldMessage m{WorldMessageType::NetMoveTo};
    m.playerId = playerId;
    m.netState = move;
    netInbox_.at(playerId).push_back(std::move(m));
}

void WorldHost::gateNotice(const std::string& text) {
    for (uint8_t id = 0; id < kPlayers; ++id) {
        if (!playerRoom_[id]) continue;
        WorldMessage m{WorldMessageType::NetNotice};
        m.playerId = id;
        m.netState.assign(text.begin(), text.end());
        netInbox_[id].push_back(std::move(m));
    }
}

void WorldHost::gateAsk(uint8_t playerId, const std::vector<uint8_t>& move) {
    if (move.size() != 13) return;
    if (gate_ && gate_->scene) return; // a scene comes first
    if (!gate_) {
        Gate g;
        g.mission = move[12] != 0;
        g.move.assign(move.begin(), move.begin() + 12);
        g.first = playerId;
        gate_ = g;
    }
    gate_->waiting.insert(playerId);
}

void WorldHost::gateLeave(uint8_t playerId) {
    if (!gate_ || gate_->scene) return;
    gate_->waiting.erase(playerId);
    if (gate_->waiting.empty()) gate_.reset();
}

void WorldHost::gateForce(uint64_t nowNs) {
    constexpr uint64_t kForceCountdownNs = 5'000'000'000;
    if (gate_ && gate_->forceAtNs == 0) gate_->forceAtNs = nowNs + kForceCountdownNs;
}

void WorldHost::gateTurn(uint64_t nowNs) {
    if (!gate_) return;
    Gate& g = *gate_;
    size_t all = 0, here = 0;
    for (uint8_t id = 0; id < kPlayers; ++id) {
        if (!playerRoom_[id] || (briefs_[id] && briefs_[id]->downed)) continue;
        ++all;
        if (g.scene ? playerRoom_[id] == g.room : g.waiting.count(id) != 0) ++here;
    }
    const bool bring = gateTeleport_ && !g.mission;
    if (bring) {
        if (g.scene) { // next to who is there
            for (uint8_t id = 0; id < kPlayers; ++id) {
                if (!playerRoom_[id] || playerRoom_[id] == g.room || !briefs_[g.first] || g.brought.count(id) != 0) continue;
                g.brought.insert(id);
                const WorldBrief::Entry& e = *briefs_[g.first];
                const int16_t x = int16_t(e.x + 0x80);
                moveTo(id, {uint8_t(g.room.stage), uint8_t(g.room.sub), uint8_t(x), uint8_t(uint16_t(x) >> 8), uint8_t(e.y), uint8_t(uint16_t(e.y) >> 8), uint8_t(e.z),
                            uint8_t(uint16_t(e.z) >> 8), uint8_t(e.yaw), uint8_t(uint16_t(e.yaw) >> 8), 2, 0x11});
            }
        } else { // through the door with who asked
            for (uint8_t id = 0; id < kPlayers; ++id)
                if (playerRoom_[id]) moveTo(id, g.move);
            gate_.reset();
            return;
        }
    }
    const bool forced = g.forceAtNs != 0 && nowNs >= g.forceAtNs;
    if (here >= all || forced) {
        if (g.scene) {
            if (const auto it = worlds_.find(g.room); it != worlds_.end()) it->second->link.sendReliable({WorldMessageType::NetSceneGo});
        } else {
            for (const uint8_t id : g.waiting)
                if (playerRoom_[id]) moveTo(id, g.move);
        }
        gate_.reset();
        return;
    }
    if (nowNs < g.nextNoticeNs) return;
    g.nextNoticeNs = nowNs + 500'000'000;
    if (!g.scene && playerRoom_[g.first] && briefs_[g.first]) {
        // the door glows for those in its room
        const WorldBrief::Entry& e = *briefs_[g.first];
        for (uint8_t id = 0; id < kPlayers; ++id) {
            if (playerRoom_[id] != playerRoom_[g.first]) continue;
            WorldMessage glow{WorldMessageType::NetGateGlow};
            glow.playerId = id;
            glow.netState = {uint8_t(e.x), uint8_t(uint16_t(e.x) >> 8), uint8_t(e.y), uint8_t(uint16_t(e.y) >> 8), uint8_t(e.z), uint8_t(uint16_t(e.z) >> 8)};
            netInbox_[id].push_back(std::move(glow));
        }
    }
    char text[96];
    const std::string name = !names_[g.first].empty() ? names_[g.first] : g.first == 0 ? "The host" : "Player " + std::to_string(int(g.first));
    if (g.forceAtNs != 0)
        std::snprintf(text, sizeof text, "Starting in %d", int((g.forceAtNs - nowNs) / 1'000'000'000) + 1);
    else if (g.mission)
        std::snprintf(text, sizeof text, "%s is waiting at the exit (%zu/%zu)", name.c_str(), here, all);
    else
        std::snprintf(text, sizeof text, "%s is waiting (%zu/%zu)", name.c_str(), here, all);
    gateNotice(text);
}

void WorldHost::poll(uint64_t nowNs) {
    for (auto it = worlds_.begin(); it != worlds_.end();) {
        World& world = *it->second;
        for (const WorldMessage& m : world.link.poll(nowNs)) {
            if (m.type == WorldMessageType::Report) world.report = WorldReport{m.tick, m.liveActors, m.players, m.digest};
            if ((m.type == WorldMessageType::NetEvents || m.type == WorldMessageType::NetUpdate ||
                 m.type == WorldMessageType::NetHit || m.type == WorldMessageType::NetShotHit ||
                 m.type == WorldMessageType::NetPlayer || m.type == WorldMessageType::NetPlayerAck ||
                 m.type == WorldMessageType::NetGift || m.type == WorldMessageType::NetEventStart ||
                 m.type == WorldMessageType::NetRevived || m.type == WorldMessageType::NetPresent ||
                 m.type == WorldMessageType::NetSceneStart || m.type == WorldMessageType::NetSceneView ||
                 m.type == WorldMessageType::NetSceneEnd || m.type == WorldMessageType::NetHold ||
                 m.type == WorldMessageType::NetWarp || m.type == WorldMessageType::NetScreen ||
                 m.type == WorldMessageType::NetRoomPlayer) &&
                m.playerId < kPlayers &&
                playerRoom_[m.playerId] == it->first)
                netInbox_[m.playerId].push_back(m);
            if (m.type == WorldMessageType::NetSceneGate) {
                if (!gate_) {
                    // a scene wants the room: who is here is waiting for the rest
                    Gate g;
                    g.scene = true;
                    g.room = it->first;
                    g.first = world.players.empty() ? uint8_t(0) : world.players[0];
                    gate_ = g;
                }
                continue;
            }
            if (m.type == WorldMessageType::NetWorldExit) {
                // everyone in the room goes where the scene or the room's logic takes them
                SDL_Log("coordinator: the world of ST%02X sub %d sends its players on", unsigned(it->first.stage), it->first.sub);
                for (const uint8_t id : world.players) {
                    WorldMessage move{WorldMessageType::NetMoveTo};
                    move.playerId = id;
                    move.netState = m.netState;
                    netInbox_[id].push_back(std::move(move));
                }
                continue;
            }
            if (m.type == WorldMessageType::PlayerBrief) {
                if (const auto brief = decodeBrief(m.netState)) {
                    if (bool& fight = bossFight_[it->first]; fight != brief->bossFight) {
                        fight = brief->bossFight;
                        SDL_Log("coordinator: ST%02X sub %d: a story boss fight %s", unsigned(it->first.stage), it->first.sub, fight ? "starts" : "ends");
                        for (const uint8_t id : world.players) {
                            WorldMessage notice{WorldMessageType::NetBossFight};
                            notice.playerId = id;
                            notice.netState = {uint8_t(fight ? 1 : 0)};
                            netInbox_[id].push_back(std::move(notice));
                        }
                    }
                    for (const WorldBrief::Entry& e : brief->players)
                        if (e.id < kPlayers && playerRoom_[e.id] == it->first) briefs_[e.id] = e;
                }
                continue;
            }
            if (m.type == WorldMessageType::GlobalChange) {
                if (auto change = od::port::decodeGlobalChange(m.netState)) {
                    change->stage = uint8_t(it->first.stage);
                    globalChanged(*change, &world, std::nullopt);
                }
                continue;
            }
            if (m.type != WorldMessageType::StageState) continue;
            const auto changed = od::port::decodeStageRecord(m.stageRecord);
            if (!changed) continue;
            const int stage = it->first.stage;
            const auto held = stageRecords_.find(stage);
            const od::port::StageRecord merged =
                held == stageRecords_.end() ? *changed : od::port::mergeStageRecord(held->second, *changed);
            stageRecords_[stage] = merged;
            WorldMessage out{WorldMessageType::StageState}; // to the stage's other worlds
            out.stageRecord = od::port::encodeStageRecord(merged);
            for (auto& [room, other] : worlds_)
                if (room.stage == stage && other.get() != &world) other->link.sendReliable(out);
        }
        if (nowNs - world.lastKeepAliveNs >= kKeepAliveNs) {
            world.lastKeepAliveNs = nowNs;
            world.link.sendLatest({WorldMessageType::KeepAlive});
        }
        // a world that ended on its own (it failed): its players have no world until they enter again
        if (world.process && !SDL_WaitProcess(world.process, false, nullptr)) {
            ++it;
            continue;
        }
        SDL_Log("coordinator: world ST%02X sub %d ended on its own", unsigned(it->first.stage), it->first.sub);
        for (uint8_t id : world.players) playerRoom_[id].reset();
        const int stage = it->first.stage;
        it = worlds_.erase(it);
        dropEmptiedStage(stage);
    }
    gameOverTurn();
    gateTurn(nowNs);
    if (briefs_[0])
        for (uint8_t id = 1; id < kPlayers; ++id)
            if (joining_[id]) placeNextToHost(id);
    std::erase_if(stopping_, [&](const std::unique_ptr<World>& world) {
        world->link.poll(nowNs); // the stop is resent until taken
        return SDL_WaitProcess(world->process, false, nullptr);
    });
}

void WorldHost::gameOverTurn() {
    bool anyone = false, allDown = true;
    for (uint8_t id = 0; id < kPlayers; ++id) {
        if (!playerRoom_[id]) continue;
        anyone = true;
        allDown = allDown && briefs_[id] && briefs_[id]->downed;
    }
    if (!anyone || !allDown) {
        gameOverSent_ = false;
        return;
    }
    if (gameOverSent_) return;
    gameOverSent_ = true;
    SDL_Log("coordinator: everyone is down");
    everyoneDown_ = true;
}

bool WorldHost::takeEveryoneDown() { return std::exchange(everyoneDown_, false); }

void WorldHost::reloadFrom(const std::vector<uint8_t>& save, const std::filesystem::path& savePath) {
    for (auto& [room, world] : worlds_) {
        world->link.sendReliable({WorldMessageType::Stop});
        stopping_.push_back(std::move(world));
    }
    worlds_.clear();
    for (uint8_t id = 0; id < kPlayers; ++id) {
        playerRoom_[id].reset();
        briefs_[id].reset();
        joining_[id] = false;
        netInbox_[id].clear();
    }
    stageRecords_.clear();
    bossFight_.clear();
    gate_.reset();
    gameOverSent_ = false;
    global_ = od::port::GlobalMaster(od::port::saveZenny(save));
    storySave_ = savePath;
}

void WorldHost::testClearFlag(uint32_t flag) {
    od::port::GlobalChange c;
    c.flags.push_back({uint16_t(flag >> 3), 0, uint8_t(1u << (flag & 7))});
    global_.merge(c);
}

void WorldHost::testStory(uint8_t story) {
    od::port::GlobalChange c;
    c.values = {{0, story}, {1, story}};
    global_.merge(c);
}

void WorldHost::placeNextToHost(uint8_t playerId) {
    if (playerId == 0) return;
    if (!playerRoom_[0] || !briefs_[0]) {
        joining_[playerId] = true;
        return;
    }
    joining_[playerId] = false;
    constexpr int kBeside = 0x80;
    const WorldBrief::Entry& e = *briefs_[0];
    WorldMessage move{WorldMessageType::NetMoveTo};
    move.playerId = playerId;
    const int16_t x = int16_t(e.x + kBeside);
    move.netState = {uint8_t(playerRoom_[0]->stage), uint8_t(playerRoom_[0]->sub), uint8_t(x), uint8_t(uint16_t(x) >> 8), uint8_t(e.y), uint8_t(uint16_t(e.y) >> 8),
                     uint8_t(e.z), uint8_t(uint16_t(e.z) >> 8), uint8_t(e.yaw), uint8_t(uint16_t(e.yaw) >> 8), 2, 0x11};
    netInbox_[playerId].push_back(std::move(move));
}

void WorldHost::respawnAsk(uint8_t playerId) {
    const std::optional<WorldRoom> room = playerRoom_.at(playerId);
    if (!room) return;
    if (const auto fight = bossFight_.find(*room); fight != bossFight_.end() && fight->second) return;
    std::optional<uint8_t> beside;
    for (uint8_t id = 0; id < kPlayers; ++id) {
        if (id == playerId || !playerRoom_[id] || !briefs_[id] || briefs_[id]->downed) continue;
        if (!beside || playerRoom_[id] == room) beside = id;
        if (playerRoom_[id] == room) break;
    }
    if (!beside) return;
    const WorldBrief::Entry& e = *briefs_[*beside];
    const WorldRoom to = *playerRoom_[*beside];
    WorldBrief where; // the teammate's place, in the form of an entry
    where.players.push_back(e);
    WorldMessage m{WorldMessageType::NetRespawnAt};
    m.playerId = playerId;
    m.netState = {uint8_t(to.stage), uint8_t(to.sub)};
    const std::vector<uint8_t> place = encodeBrief(where);
    m.netState.insert(m.netState.end(), place.begin(), place.end());
    netInbox_[playerId].push_back(std::move(m));
}

std::optional<od::port::StageRecord> WorldHost::stageRecord(int stage) const {
    const auto it = stageRecords_.find(stage);
    return it == stageRecords_.end() ? std::nullopt : std::optional(it->second);
}

std::optional<WorldReport> WorldHost::report(WorldRoom room) const {
    const auto it = worlds_.find(room);
    return it == worlds_.end() ? std::nullopt : it->second->report;
}

} // namespace od::app
