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

WorldHost::WorldHost(std::filesystem::path exe, std::filesystem::path storySave)
    : exe_(std::move(exe)), storySave_(std::move(storySave)) {}

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
    if (stageRecords_.erase(stage)) SDL_Log("coordinator: ST%02X emptied, its enemies are back", unsigned(stage));
}

void WorldHost::removeFromWorld(uint8_t playerId) {
    const std::optional<WorldRoom> was = playerRoom_.at(playerId);
    if (!was) return;
    playerRoom_[playerId].reset();
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
    netInbox_.at(client).clear();
    WorldMessage m{WorldMessageType::NetJoin};
    m.playerId = client;
    worlds_.at(*room)->link.sendReliable(m);
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

void WorldHost::netAck(uint8_t client, uint16_t sequence) {
    const std::optional<WorldRoom> room = playerRoom_.at(client);
    if (!room) return;
    WorldMessage m{WorldMessageType::NetAck};
    m.playerId = client;
    m.netSequence = sequence;
    worlds_.at(*room)->link.sendLatest(m);
}

void WorldHost::poll(uint64_t nowNs) {
    for (auto it = worlds_.begin(); it != worlds_.end();) {
        World& world = *it->second;
        for (const WorldMessage& m : world.link.poll(nowNs)) {
            if (m.type == WorldMessageType::Report) world.report = WorldReport{m.tick, m.liveActors, m.players, m.digest};
            if ((m.type == WorldMessageType::NetEvents || m.type == WorldMessageType::NetUpdate ||
                 m.type == WorldMessageType::NetHit || m.type == WorldMessageType::NetShotHit) &&
                m.playerId < kPlayers &&
                playerRoom_[m.playerId] == it->first)
                netInbox_[m.playerId].push_back(m);
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
    std::erase_if(stopping_, [&](const std::unique_ptr<World>& world) {
        world->link.poll(nowNs); // the stop is resent until taken
        return SDL_WaitProcess(world->process, false, nullptr);
    });
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
