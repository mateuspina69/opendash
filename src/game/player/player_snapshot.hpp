// Compact snapshot of the actor fields that playerPose (exe 0x5e1f90 / exe 0x5e25b0) and the draw
// (PlayerView::build, exe 0x5f3440) read, so a remote player's Mega Man can be posed in a
// player of its own ("ghost", game/ghost) without the rest of the actor struct. Widths follow how player_pose.cpp and
// scene_builder.cpp read each field. Validated against the ST09 pose golden (tests/test_player_snapshot.cpp).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace od::port {

struct PlayerLook { // what selects the player's files (player_model.hpp: exe 0x5d5c40)
    uint8_t kind = 0;           // +0x4   (0 = Megaman)
    uint8_t costume = 1;        // +0x1a2
    uint8_t costumeVariant = 0; // +0x1a3
    int8_t specialWeapon = 0;   // +0x18e (saved special weapon; > 2 has a PL00R file)
    bool operator==(const PlayerLook&) const = default;
};

struct PlayerSnapshot { // every member is value-initialised: `PlayerSnapshot s;` is fully defined
    PlayerLook look;
    uint8_t drawFlags[2]{};             // +0, +1  (drawn when +0 & +1 & 2)
    int32_t pos[3]{};                   // +0x10..+0x18 (16.16)
    int16_t pitch{}, yaw{}, yawOfs{};   // +0x28, +0x2a, +0xf2
    uint8_t animId[2]{}, animStep[2]{}; // +0xa0/+0xb0, +0xa2/+0xb2
    int8_t animFrame[2]{};              // s8 at [+0x98], [+0xa8]
    int8_t channelMix{};                // +0xc0
    uint8_t blend[4]{};                 // +0x2a4..+0x2a7
    uint32_t light{};                   // +0x2a8 the model's light colour (0x808080 neutral; the status tints)
    uint32_t stageAnimBase{};           // +0x298 (meaningful only for ids >= 0x80; same data on both sides by fingerprint)
    int16_t lean{}, lean2{};            // +0x104, +0x108
    uint8_t aimFlags{};                 // +0x13c
    uint8_t armed{};                    // +0x140
    int8_t weaponSlot{}, busterSlot{};  // +0x18c, +0x18d
    int8_t face[2]{};                   // +0x1a0, +0x1a1
    bool operator==(const PlayerSnapshot&) const = default;
};

// The room a player is in (what decides whether its ghost is shown, app/ghost_view)
struct GhostRoom {
    uint8_t stage = 0, sub = 0; // 0xa6d190 / 0xa6d191
    bool fieldPlay = false;     // game mode 0xa6d180 == 3 && 0xa6d181 == 2
    bool operator==(const GhostRoom&) const = default;
};
// the local player's (the game state)
GhostRoom ghostRoomOf();

// What a player sends of its own character every game tick (the netplay payload, net/session carries it opaque): its
// room, the fingerprint of the loaded data its pose reads (game/loaded_data_fingerprint) and the snapshot
struct RemotePlayerState {
    GhostRoom room;
    uint64_t fingerprint = 0;
    PlayerSnapshot snapshot;
    bool operator==(const RemotePlayerState&) const = default;
};
// Explicit little-endian field-by-field layout (kRemoteStateLayout, then every field in declaration order; no struct
// copy). Decode returns nullopt on a size other than kRemoteStateBytes, another layout, a stage outside the game's
// stages or a flag byte other than 0 / 1.
constexpr uint8_t kRemoteStateLayout = 2;
constexpr size_t kRemoteStateBytes = 65;
std::vector<uint8_t> encodePlayerState(const RemotePlayerState& s);
std::optional<RemotePlayerState> decodePlayerState(std::span<const uint8_t> bytes);

struct Player;

// Safe on any image: when a frame pointer ([+0x98] / [+0xa8]) is not mapped (player not initialised yet, e.g. outside
// field play) that channel's frame is captured as 0 and the snapshot is marked not drawable (drawFlags {0, 0}).
PlayerSnapshot capturePlayerSnapshot(const Player& p);

// Writes every field into the player struct; the two frame bytes go to frameBytes / frameBytes + 1 of the bound loaded
// files and +0x98 / +0xa8 point there. Leaves the rest of the struct as it is.
void applyPlayerSnapshot(Player& p, uint32_t frameBytes, const PlayerSnapshot& s);

} // namespace od::port
