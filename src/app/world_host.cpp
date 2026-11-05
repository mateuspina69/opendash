// The coordinator's worlds (see world_host.hpp).
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
}

void WorldHost::enter(uint8_t playerId, WorldRoom room, const std::vector<uint8_t>& playerState) {
    if (playerRoom_.at(playerId) == room) return;
    removeFromWorld(playerId);
    World& world = worldOf(room);
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

void WorldHost::poll(uint64_t nowNs) {
    for (auto it = worlds_.begin(); it != worlds_.end();) {
        World& world = *it->second;
        for (const WorldMessage& m : world.link.poll(nowNs))
            if (m.type == WorldMessageType::Report) world.report = WorldReport{m.tick, m.liveActors, m.players, m.digest};
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
        it = worlds_.erase(it);
    }
    std::erase_if(stopping_, [&](const std::unique_ptr<World>& world) {
        world->link.poll(nowNs); // the stop is resent until taken
        return SDL_WaitProcess(world->process, false, nullptr);
    });
}

std::optional<WorldReport> WorldHost::report(WorldRoom room) const {
    const auto it = worlds_.find(room);
    return it == worlds_.end() ? std::nullopt : it->second->report;
}

} // namespace od::app
