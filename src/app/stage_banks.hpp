// The stage's alternate model banks a script mapped (a boss's second bank, the hall after a fight: app/room_mesh
// g_altBankDat) and the pointers the script set to them (the stage's model bank, the player's stage animations). They
// stay through the stage's rooms. In a session they are the same on every machine of a room: the world a player enters
// starts with the banks that player's game holds, and a game takes the banks of the world it is in (its mirrors bind the
// world's model entries).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace od::app {

struct StageBanks {
    uint8_t stage = 0xff;                  // the stage they are of (0xff: none)
    std::array<std::string, 2> names;      // the DAT in each type-1 buffer ("" = the buffer holds none of a script's)
    uint8_t bankBuffer = 0;                // StageState::bankBuffer: the buffer the next DAT goes into
    uint32_t modelBank = 0, stageAnims = 0, stageAnimsExtra = 0;
    bool any() const { return !names[0].empty() || !names[1].empty(); }
    friend bool operator==(const StageBanks&, const StageBanks&) = default;
};

std::vector<uint8_t> encodeStageBanks(const StageBanks& b);
std::optional<StageBanks> decodeStageBanks(std::span<const uint8_t> bytes);

} // namespace od::app
