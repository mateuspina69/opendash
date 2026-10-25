// Netplay in the app: the glue between the session (net/session, no game code) and the ghosts (app/ghost_view). It
// owns the session and one smoothing buffer (game/ghost_buffer) per ghost slot, and maps each remote player id to a
// slot (an id with no free slot is dropped and logged, never passed to GhostView).
//
// Between game ticks only (never inside one): after every logic tick afterTick() captures the local player's state
// (game/player/player_snapshot, reads only); once per sim step step() sends the newest one, polls the session, feeds the
// received states into the buffers and sets each slot of GhostView from its buffer's render point (clearRemote when a
// player leaves or falls silent). The shared world (app/netplay_world) runs alongside while a session is up: it writes
// the game state only through the game's netplay hooks, between ticks.
//
// The dev mirror (--ghost-mirror) is a source like a peer: the local player's state of `delay` ticks ago, moved by
// (dx, dz), through the same buffer into its own slot.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "app/ghost_view.hpp"
#include "app/netplay_world.hpp"
#include "engine/config.hpp"
#include "game/ghost.hpp"
#include "game/ghost_buffer.hpp"
#include "game/player/player_snapshot.hpp"
#include "net/link_conditioner.hpp"
#include "net/session.hpp"

namespace od::app {

enum class NetMode : uint8_t { Off, Host, Join };
constexpr uint16_t kDefaultNetPort = 27460; // --host without a port, net=host without net_port

// what the command line gave (each overrides its opendash.ini key)
struct NetFlags {
    std::optional<NetMode> mode;      // --host [port] / --join ip:port
    std::optional<int> port;          // --host's port
    std::optional<std::string> join;  // --join's ip:port
    std::optional<std::string> name;  // --net-name
    std::optional<std::string> key;   // --net-key
    std::optional<od::net::LinkSim> sim; // --net-sim <latency ms> <jitter ms> <loss %> (test instrument)
};

struct NetSettings {
    NetMode mode = NetMode::Off;
    uint16_t port = 0;   // host: its listening port (a client binds any free port)
    std::string join;    // client: the host's ip:port
    std::string name;    // this player's (default: the Windows user name)
    std::string key;     // the session key
    std::optional<od::net::LinkSim> sim;
};
// opendash.ini's net keys under the flags; throws std::invalid_argument on a bad net= / net_port= / join
NetSettings resolveNetSettings(const od::Config& cfg, const NetFlags& flags);

// --ghost-mirror <delay ticks> <dx> <dz> [--ghost-look <costume> <variant> <weapon>]
struct MirrorSettings {
    int delayTicks = 0;
    int dx = 0, dz = 0; // game units
    std::optional<od::port::PlayerLook> look;
};

class Netplay {
public:
    // starts the session (mode != Off) and the mirror; a session that cannot start (the port in use, an address that
    // does not resolve, unreadable game files) is logged and shown in the F1 panel (error()), and netplay stays off
    void start(const NetSettings& settings, const std::optional<MirrorSettings>& mirror,
               const std::filesystem::path& datDir, const std::filesystem::path& exe);
    void stop(); // Bye to the others (shutdown)

    // after every logic tick (between ticks): the local player's state; reads the game state only
    void afterTick();
    // once per sim step: send, poll, feed the buffers, set GhostView's slots
    void step(uint64_t nowNs, GhostView& ghosts);

    // ---- the F1 panel
    bool sessionActive() const { return session_ != nullptr; }
    bool mirrorActive() const { return mirror_.has_value(); }
    const std::string& error() const { return error_; }
    od::net::SessionStatus status(uint64_t nowNs) const;
    // the ghost slot of a remote player (-1: none)
    int slotOf(uint8_t playerId) const;
    // the shared world (app/netplay_world)
    NetWorld& world() { return world_; }
    const NetWorld& world() const { return world_; }
    // a session joined to a host (the client role)
    bool isClient() const { return session_ && session_->status(0).role == od::net::Role::Client; }
    // the host was lost since the last call (a connected client's host left or fell silent; the world ended)
    bool takeHostLost() { return std::exchange(hostLost_, false); }
    // the host was lost and not found again yet (F1)
    bool hostGone() const { return hostGone_; }
    // what feeds a slot: "" (free), "mirror", or "<name> (id N)"
    std::string slotSource(int slot) const;

private:
    struct Source {
        enum class Kind : uint8_t { None, Peer, Mirror } kind = Kind::None;
        uint8_t playerId = 0;
        std::string name;
        od::ghost::GhostBuffer buffer;
        bool remoteSet = false; // GhostView has a state for this slot
    };
    int claimSlot(Source::Kind kind, uint8_t playerId, const std::string& name); // -1: none free
    void release(int slot, GhostView& ghosts);

    std::unique_ptr<od::net::Session> session_;
    NetWorld world_;
    bool hostLost_ = false, hostGone_ = false;
    std::string error_;
    std::optional<MirrorSettings> mirror_;
    std::array<Source, od::ghost::kMaxGhosts> sources_;
    std::set<uint8_t> noSlotLogged_; // ids already logged as without a slot
    std::set<uint8_t> badPayloadLogged_;
    // the local player
    uint32_t localTick_ = 0;
    std::optional<od::port::RemotePlayerState> latest_; // captured after the last logic tick, not sent yet
    std::deque<std::pair<uint32_t, od::port::RemotePlayerState>> mirrorHistory_; // the last delay + 1 ticks
    std::vector<std::pair<uint32_t, od::port::RemotePlayerState>> mirrorDue_;    // to push in the next step
};

} // namespace od::app
