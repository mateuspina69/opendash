// Netplay's shared world (spec: docs/superpowers/specs/2026-10-25-netplay-shared-world-design.md): one world simulates,
// the host's; the clients mirror it. app/netplay drives it between ticks; it sets the game's netplay hooks
// (game/net_world, game/net_puppets, game/hit_list's external hits and remote bodies) and moves the world's steps and
// the game events over the session (app/netplay_world_codec).
//
// Host: after each tick the shared actors captured (their slot generations counted), the step sent once per sim step;
// the clients' hit claims applied through the hit pass (a puppet's, a wall's) or routed to the victim (a player's,
// friendly fire on, the damage scaled); the downed respawned after 3 s; the drops, the wall steps, the story scripts
// and the room changes announced; after a shared script the host's ticks held until every client finished it (60 s at
// most).
// Client: its own shared actors held back (game/net_world's mirror), the host's shown as puppets, its hits on them, on
// a wall or on a ghost sent as claims; the host's scripts run on its own copy; a room change queued for the app.
// Both: while friendly fire is on, a stand-in body per shown ghost (a hit on it is a claim on that player).
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "app/ghost_view.hpp"
#include "app/netplay_world_codec.hpp"
#include "game/ghost.hpp"
#include "net/session.hpp"

namespace od::app {

class NetWorld {
public:
    // the game hooks installed (host: the observers; client: the mirror, the gates)
    void begin(bool host, uint8_t localId);
    // every hook back to its default, every puppet freed
    void end();
    bool active() const { return active_; }
    bool host() const { return host_; }

    // after every logic tick (between ticks)
    void afterTick(uint32_t tick);
    // once per sim step: receive, apply, send; slotPlayer[slot]: the player id a ghost slot shows (-1: none)
    void step(uint64_t nowNs, od::net::Session& session, const GhostView& ghosts,
              const std::array<int, od::ghost::kMaxGhosts>& slotPlayer);
    // host: a client joined (the settings and the room sent to it) / left
    void peerJoined(uint8_t id);
    void peerLeft(uint8_t id);
    // client: before the app loads the host's room (every puppet freed) / after (the slots the room's init reused
    // forgotten, the events held for that room applied)
    void leavingRoom();
    void arrived(uint64_t nowNs);
    // the player respawned here (the room's entry point; the host leaving)
    void respawnHere();

    // the combat settings: host (or netplay off): set and sent; client: the host's
    void setCombat(const NetCombatSettings& c);
    const NetCombatSettings& combat() const { return combat_; }
    // host: the room it loaded (announced while a session runs)
    void hostTravelled(int stage, int sub, bool transition);
    // client: a room the host loaded, for the app to load
    std::optional<TravelOrder> takeTravel();
    // host: its ticks held (a client still in a shared script)
    bool holdTicks() const { return !waiting_.empty(); }
    std::string waitingFor() const;

    struct Stats {
        int puppets = 0;
        int claimsSent = 0, claimsApplied = 0, claimsDropped = 0;
    };
    Stats stats() const;

private:
    struct Outgoing {
        uint8_t to = od::net::kAllPlayers;
        NetEvent event;
    };
    // host: a slot's occupant over the captures
    struct SlotTrack {
        bool present = false;
        uint16_t routine = 0;
        uint8_t generation = 0;
    };
    // client: a host slot's puppet
    struct Shown {
        uint8_t generation = 0;
        uint32_t actor = 0;    // 0: not spawned (the pool was full)
        uint64_t lastNs = 0;   // its last state
        uint32_t lastTick = 0; // ... the host tick of that state
    };
    using ScriptWait = std::pair<uint8_t, uint8_t>; // {script, client}

    void send(uint8_t to, NetEvent e);
    void hostApply(uint64_t nowNs, uint8_t from, const NetEvent& e);
    void hostClaim(uint8_t from, const HitClaim& c);
    void clientApply(uint64_t nowNs, const NetEvent& e);
    void clientWorld(uint64_t nowNs, const WorldPart& part);
    HitClaim claimFromHere(uint32_t tick) const; // the shooter's own data of a claim
    void claim(const HitClaim& c);               // host: applied here; client: sent
    void installHooks();
    void sharedScriptEnded(); // host: the clients it was sent to and that have not finished it are waited for
    void clientStartScript(uint8_t id);

    bool active_ = false, host_ = false;
    uint8_t localId_ = 0;
    NetCombatSettings combat_;
    std::vector<Outgoing> outgoing_;
    uint32_t lastTick_ = 0;
    bool wasDown_ = false;
    Stats stats_;
    // host
    std::array<SlotTrack, 256> tracks_{};
    std::vector<WorldPuppet> capture_;   // the last tick's, sent with the next step
    std::set<uint8_t> clients_;          // the accepted clients
    std::map<uint8_t, uint64_t> respawnAt_; // player id -> when (0: at the next step's time + 3 s)
    std::optional<TravelOrder> room_;    // the host's current room
    int sharedScript_ = -1;              // the shared script running (-1: none)
    std::set<uint8_t> scriptSentTo_;     // the clients it was sent to
    std::set<ScriptWait> scriptDone_;    // the clients that finished a script
    std::set<ScriptWait> waiting_;       // the clients the host waits for, by script
    uint64_t holdSinceNs_ = 0;
    bool holdStarting_ = false;
    std::map<uint8_t, std::string> names_;
    std::set<uint16_t> trimmedLogged_;
    // client
    std::map<uint8_t, Shown> shown_;     // host slot -> its puppet
    std::set<std::pair<uint8_t, uint8_t>> noSlotLogged_;
    int allowScript_ = -1;               // the host's script being started
    int hostScript_ = -1;                // the host's script running here (its end: ScriptDone)
    std::vector<uint8_t> queuedScripts_; // the host's scripts that came while one ran here (started after it)
    std::optional<TravelOrder> travel_;
    bool travelling_ = false;            // a Travel taken, the room not loaded yet
    std::vector<NetEvent> heldEvents_;   // events for the room the client goes to (applied once it is there)
    uint32_t partTick_ = 0;              // the world step being received
    int partsSeen_ = 0;
    uint64_t lastWorldNs_ = 0;           // the last world step received
    // both: where the player entered its room (the respawn point)
    od::port::GhostRoom entryRoom_;
    bool entryValid_ = false;
    int32_t entryPos_[3] = {};
    int16_t entryYaw_ = 0;
    // both: the stand-in bodies' players, by body index
    std::vector<uint8_t> bodyPlayers_;
};

} // namespace od::app
