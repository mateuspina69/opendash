// The shared world's payloads (app/netplay_world): the host's world steps (net::WorldState's payload, in parts) and the
// game events (net::Event's payload). Little endian, fixed fields in declaration order; a decode that does not read
// exactly the payload is nullopt.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "game/net_puppets.hpp"
#include "game/net_world.hpp"
#include "game/player/player_snapshot.hpp"

namespace od::app {

constexpr int kMaxWorldParts = 8;
// a WorldState's payload budget (the datagram's 1200 less the header, the sequence, the tick and the blob length)
constexpr size_t kWorldPartBytes = 1150;

// one shared actor of a step: the host's slot generation (a reused slot is a new actor) and its state
struct WorldPuppet {
    uint8_t generation = 0;
    od::port::PuppetState state;
};
// one part of a world step
struct WorldPart {
    od::port::GhostRoom room;
    uint64_t fingerprint = 0; // the host's loaded data (game/loaded_data_fingerprint)
    uint32_t tick = 0;
    uint8_t part = 0, parts = 1;
    std::vector<WorldPuppet> puppets;
};
// a step in at most kMaxWorldParts parts of at most maxBytes; past that the puppets farthest from every player are left
// out (*trimmed set)
std::vector<std::vector<uint8_t>> encodeWorld(const od::port::GhostRoom& room, uint64_t fingerprint, uint32_t tick,
                                              std::vector<WorldPuppet> puppets,
                                              const std::vector<std::array<int32_t, 3>>& players, size_t maxBytes,
                                              bool* trimmed);
std::optional<WorldPart> decodeWorld(std::span<const uint8_t> payload);

enum class EventKind : uint8_t {
    HitClaim = 1,  // client -> host
    PlayerHit,     // host -> the victim
    Downed,        // client -> host (id: the player)
    Respawn,       // host -> client (id: the player)
    DropSpawned,   // host -> all
    ObjectChanged, // host -> all (a breakable wall's step)
    ScriptStarted, // host -> all (id: the script)
    ScriptDone,    // client -> host (id: the script)
    Travel,        // host -> all
    Settings,      // host -> all
    FireChanged,   // host -> all (a fire's strength, ST1E)
};
enum class ClaimTarget : uint8_t { Puppet = 0, Player = 1, Wall = 2, Fire = 3 }; // Fire: its place in contact x / z,
                                                                                 // the damage in attack

struct HitClaim {
    ClaimTarget target = ClaimTarget::Puppet;
    uint8_t hostSlot = 0, generation = 0; // Puppet
    uint8_t playerId = 0;                 // Player: the victim
    uint32_t wallResult = 0;              // Wall: the cell word the wall test gave
    uint32_t attack = 0;                  // the damage | the attack flags
    int16_t contact[3] = {};
    uint8_t weapon = 0;                   // the shooter's active weapon (for a host that re-simulates the shot)
    int16_t origin[3] = {}, dir[3] = {};  // the shooter's position and facing at the hit
    uint32_t tick = 0;                    // the shooter's tick
};
struct PlayerHit {
    uint8_t attacker = 0;
    uint32_t attack = 0; // scaled by the host
    int16_t contact[3] = {};
};
struct TravelOrder {
    int stage = 0, sub = 0;
    bool transition = false;
    od::port::NetStory story; // the host's: the room loads with it on the client
};
struct NetCombatSettings {
    bool friendlyFire = false;
    int scale = 100; // 25..100: % of a PvP hit's damage
    bool operator==(const NetCombatSettings&) const = default;
};

// one game event: `kind` names the field its body is in
struct NetEvent {
    EventKind kind = EventKind::HitClaim;
    od::port::GhostRoom room; // the sender's room (an event of another room is ignored)
    HitClaim claim;
    PlayerHit hit;
    uint8_t id = 0;
    od::port::PickupSpawn pickup;
    od::port::WallStep wall;
    TravelOrder travel;
    NetCombatSettings settings;
    od::port::FireStep fire;
};
std::vector<uint8_t> encodeEvent(const NetEvent& e);
std::optional<NetEvent> decodeEvent(std::span<const uint8_t> payload);

} // namespace od::app
