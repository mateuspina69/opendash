// Netplay in the app (see netplay.hpp).
#include "app/netplay.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <exception>
#include <stdexcept>

#include "app/build_id.hpp"
#include "app/room_mesh.hpp"
#include "engine/game_files_hash.hpp"
#include "game/loaded_data_fingerprint.hpp"
#include "game/player/player_state.hpp"
#include "net/protocol.hpp"

namespace od::app {

namespace {

std::string userName() {
    std::string name = od::envValue("USERNAME"); // Windows's; USER elsewhere
    if (name.empty()) name = od::envValue("USER");
    return name.empty() ? "player" : name;
}

uint16_t portOf(int port, const char* what) {
    if (port < 0 || port > 65535) throw std::invalid_argument(std::string(what) + " outside 0..65535");
    return uint16_t(port);
}

} // namespace

NetSettings resolveNetSettings(const od::Config& cfg, const NetFlags& flags) {
    NetSettings s;
    if (flags.mode) s.mode = *flags.mode;
    else if (cfg.net == "off" || cfg.net.empty()) s.mode = NetMode::Off;
    else if (cfg.net == "host") s.mode = NetMode::Host;
    else if (cfg.net == "join") s.mode = NetMode::Join;
    else throw std::invalid_argument("net=" + cfg.net + ": not off, host or join");
    if (s.mode == NetMode::Host) { // a client binds any free port (the host answers where its datagrams come from)
        s.port = portOf(flags.port ? *flags.port : cfg.netPort, "net_port");
        if (s.port == 0) s.port = kDefaultNetPort;
    }
    s.join = flags.join ? *flags.join : cfg.netJoin;
    if (s.mode == NetMode::Join && s.join.empty()) throw std::invalid_argument("net=join without net_join=ip:port");
    s.name = flags.name ? *flags.name : cfg.netName;
    if (s.name.empty()) s.name = userName();
    s.key = flags.key ? *flags.key : cfg.netKey;
    s.sim = flags.sim;
    return s;
}

void Netplay::start(const NetSettings& settings, const std::optional<MirrorSettings>& mirror,
                    const std::filesystem::path& datDir, const std::filesystem::path& exe) {
    mirror_ = mirror;
    if (mirror_) claimSlot(Source::Kind::Mirror, 0, "mirror");
    if (settings.mode == NetMode::Off) return;
    try {
        od::net::SessionConfig c;
        c.role = settings.mode == NetMode::Host ? od::net::Role::Host : od::net::Role::Client;
        c.port = settings.port;
        if (settings.mode == NetMode::Join) c.host = od::net::Endpoint::parse(settings.join);
        c.name = settings.name;
        c.buildId = buildId();
        const uint64_t t0 = SDL_GetTicksNS();
        const od::GameFilesHash files = od::gameFilesHash(datDir, exe);
        SDL_Log("netplay: game files hash %016llx (exe %016llx) in %llu ms", (unsigned long long)files.all,
                (unsigned long long)files.exe, (unsigned long long)((SDL_GetTicksNS() - t0) / 1'000'000));
        c.gameFilesHash = files.all;
        c.exeHash = files.exe;
        c.sessionKeyHash = od::net::sessionKeyHash(settings.key);
        c.sim = settings.sim;
        c.seed = SDL_GetPerformanceCounter();
        session_ = std::make_unique<od::net::Session>(c);
        SDL_Log("netplay: %s on UDP port %u as \"%s\", build %s%s", settings.mode == NetMode::Host ? "hosting" : "joining",
                unsigned(session_->status(0).port), settings.name.c_str(), c.buildId.c_str(),
                settings.mode == NetMode::Join ? (" -> " + c.host.text()).c_str() : "");
        if (settings.sim)
            SDL_Log("netplay: --net-sim latency %d ms, jitter %d ms, loss %.1f %% (test instrument)",
                    settings.sim->latencyMs, settings.sim->jitterMs, settings.sim->lossPercent);
    } catch (const std::exception& e) {
        error_ = e.what();
        session_.reset();
        SDL_Log("netplay: not started: %s", e.what());
    }
}

void Netplay::stop() { session_.reset(); }

int Netplay::claimSlot(Source::Kind kind, uint8_t playerId, const std::string& name) {
    for (int slot = 0; slot < od::ghost::kMaxGhosts; ++slot) {
        Source& s = sources_[size_t(slot)];
        if (s.kind != Source::Kind::None) continue;
        s = Source{};
        s.kind = kind;
        s.playerId = playerId;
        s.name = name;
        return slot;
    }
    return -1;
}

void Netplay::release(int slot, GhostView& ghosts) {
    Source& s = sources_[size_t(slot)];
    if (s.remoteSet) ghosts.clearRemote(slot);
    s = Source{};
}

int Netplay::slotOf(uint8_t playerId) const {
    for (int slot = 0; slot < od::ghost::kMaxGhosts; ++slot)
        if (sources_[size_t(slot)].kind == Source::Kind::Peer && sources_[size_t(slot)].playerId == playerId)
            return slot;
    return -1;
}

std::string Netplay::slotSource(int slot) const {
    const Source& s = sources_.at(size_t(slot));
    switch (s.kind) {
    case Source::Kind::None: return "";
    case Source::Kind::Mirror: return "mirror";
    case Source::Kind::Peer: return s.name + " (id " + std::to_string(s.playerId) + ")";
    }
    return "";
}

od::net::SessionStatus Netplay::status(uint64_t nowNs) const {
    return session_ ? session_->status(nowNs) : od::net::SessionStatus{};
}

void Netplay::afterTick() {
    if (!session_ && !mirror_) return;
    ++localTick_;
    od::port::RemotePlayerState s{od::port::ghostRoomOf(),
                                  od::port::loadedDataFingerprint(loadedDataIdentity()),
                                  od::port::capturePlayerSnapshot(od::port::player())};
    if (mirror_) {
        mirrorHistory_.emplace_back(localTick_, s);
        while (mirrorHistory_.size() > size_t(mirror_->delayTicks) + 1) mirrorHistory_.pop_front();
        if (mirrorHistory_.size() == size_t(mirror_->delayTicks) + 1) { // the state of delay ticks ago
            auto [tick, m] = mirrorHistory_.front();
            m.snapshot.pos[0] += mirror_->dx * 65536;
            m.snapshot.pos[2] += mirror_->dz * 65536;
            if (mirror_->look) m.snapshot.look = *mirror_->look;
            mirrorDue_.emplace_back(tick, m);
        }
    }
    latest_ = std::move(s);
}

void Netplay::step(uint64_t nowNs, GhostView& ghosts) {
    if (session_) {
        if (latest_) {
            session_->sendPlayerState(nowNs, localTick_, od::port::encodePlayerState(*latest_));
            latest_.reset();
        }
        session_->poll(nowNs);
        for (const od::net::PeerEvent& e : session_->takePeerEvents()) {
            if (e.kind == od::net::PeerEvent::Kind::Joined) {
                SDL_Log("netplay: %s (id %u) joined", e.peer.name.c_str(), unsigned(e.peer.playerId));
                if (slotOf(e.peer.playerId) < 0 &&
                    claimSlot(Source::Kind::Peer, e.peer.playerId, e.peer.name) < 0 &&
                    noSlotLogged_.insert(e.peer.playerId).second)
                    SDL_Log("netplay: no ghost slot free for %s (id %u): not shown", e.peer.name.c_str(),
                            unsigned(e.peer.playerId));
            } else {
                SDL_Log("netplay: %s (id %u) left", e.peer.name.c_str(), unsigned(e.peer.playerId));
                noSlotLogged_.erase(e.peer.playerId);
                badPayloadLogged_.erase(e.peer.playerId);
                if (const int slot = slotOf(e.peer.playerId); slot >= 0) release(slot, ghosts);
            }
        }
        for (const od::net::ReceivedState& r : session_->takePlayerStates()) {
            const int slot = slotOf(r.playerId);
            if (slot < 0) { // not announced, or no slot was free: dropped (logged once)
                if (noSlotLogged_.insert(r.playerId).second)
                    SDL_Log("netplay: a state of player %u, who has no ghost slot: dropped", unsigned(r.playerId));
                continue;
            }
            const auto decoded = od::port::decodePlayerState(r.payload);
            if (!decoded) {
                if (badPayloadLogged_.insert(r.playerId).second)
                    SDL_Log("netplay: player %u sent a state of another layout or out of range (%zu bytes): dropped",
                            unsigned(r.playerId), r.payload.size());
                continue;
            }
            sources_[size_t(slot)].buffer.push(nowNs, r.tick, *decoded);
        }
    } else {
        latest_.reset();
    }
    for (int slot = 0; slot < od::ghost::kMaxGhosts; ++slot) {
        Source& s = sources_[size_t(slot)];
        if (s.kind == Source::Kind::Mirror) {
            for (const auto& [tick, m] : mirrorDue_) s.buffer.push(nowNs, tick, m);
            mirrorDue_.clear();
        }
        if (s.kind == Source::Kind::None) continue;
        if (const auto shown = s.buffer.sample(nowNs)) {
            ghosts.setRemote(slot, *shown);
            s.remoteSet = true;
        } else if (s.remoteSet) { // silent: the buffer let it go
            ghosts.clearRemote(slot);
            s.remoteSet = false;
        }
    }
}

} // namespace od::app
