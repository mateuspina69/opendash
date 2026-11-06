// What the world and a player's owner tell each other of a fight: the world's hit pass decides, the owner applies the hit to
// their own Mega Man and reports the result; a client's shot flies at once on the client and starts again in the world from
// its fire event, and what it hit there comes back to the shot on the client.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "game/actor_state.hpp"

namespace od::port {

struct Player;

// A hit box's result as the hit pass left it
struct NetHitResult {
    uint32_t result = 0;
    std::array<int16_t, 4> contact{}; // x, y, z, kind
    friend bool operator==(const NetHitResult&, const NetHitResult&) = default;
};
// a player's result in the world, once per tick
std::optional<NetHitResult> takeRemotePlayerHit(const Player& p);
// the world's result, into this machine's Mega Man's box, for the next playerUpdate to apply. Not while immune.
void applyPlayerHit(Player& p, const NetHitResult& hit);

// A shot a client's Mega Man fired
struct NetFire {
    uint8_t clientSlot = 0;   // its index in the client's shot pool
    uint16_t serial = 0;      // tells it from the next shot in that slot
    uint8_t type = 0, variant = 0, loopFlags = 0, flags = 0;
    std::array<uint8_t, 4> params{};
    std::array<int16_t, 4> aimPoint{};
    uint8_t target = 0;       // the target's slot id (0: none)
    int16_t pitch = 0, yaw = 0; // the angles the shot left the owner's hand with (their crosshair's, with the free aim)
    friend bool operator==(const NetFire&, const NetFire&) = default;
};
NetFire netFireOf(const ShotActor& shot, uint8_t clientSlot, uint16_t serial);
// the shot started in the world, owned by the player: its class starts it from their Mega Man (nullptr: the pool is full)
ShotActor* startFiredShot(size_t playerId, const NetFire& fire);

// What the owner reports of their Mega Man to the world
struct NetPlayerReport {
    int16_t hitPoints = 0, maxHitPoints = 0;
    bool immune = false;
    friend bool operator==(const NetPlayerReport&, const NetPlayerReport&) = default;
};
NetPlayerReport netPlayerReportOf(const Player& p);
void applyPlayerReport(Player& p, const NetPlayerReport& report);

// What a player wears, chosen in their own status menu: their shots and damage in the world follow it
std::vector<uint8_t> netEquipmentOf(const Player& p);
void applyEquipment(Player& p, std::span<const uint8_t> bytes);

// What a pickup or a chest gave a player in a world, for their owner to take
enum class NetGiftKind : uint8_t { Zenny, WeaponEnergy, Life };
struct NetGift {
    uint8_t player = 0;
    NetGiftKind kind = NetGiftKind::Zenny;
    int16_t amount = 0;
    friend bool operator==(const NetGift&, const NetGift&) = default;
};
void noteGift(const NetGift& gift);
std::vector<NetGift> takeGifts();
std::vector<uint8_t> encode(const NetGift& v);
std::optional<NetGift> decodeGift(std::span<const uint8_t> b);

// the shot's hit result, read and taken by the owner's shot
struct NetShotHit {
    uint8_t clientSlot = 0;
    uint16_t serial = 0;
    NetHitResult hit;
    friend bool operator==(const NetShotHit&, const NetShotHit&) = default;
};
// the result into the client's shot of that slot
void applyShotHit(ShotActor& shot, const NetHitResult& hit);

// little-endian wire forms (a failure: nullopt)
std::vector<uint8_t> encode(const NetHitResult& v);
std::optional<NetHitResult> decodeHitResult(std::span<const uint8_t> b);
std::vector<uint8_t> encode(const NetFire& v);
std::optional<NetFire> decodeFire(std::span<const uint8_t> b);
std::vector<uint8_t> encode(const NetPlayerReport& v);
std::optional<NetPlayerReport> decodeReport(std::span<const uint8_t> b);
std::vector<uint8_t> encode(const NetShotHit& v);
std::optional<NetShotHit> decodeShotHit(std::span<const uint8_t> b);

} // namespace od::port
