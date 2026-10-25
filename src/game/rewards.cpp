// The rewards and costs applied to the game's counters (see rewards.hpp), transcribed from dash2.exe.
#include "game/rewards.hpp"

#include "game/game_state.hpp"
#include "game/player/player_state.hpp"

namespace od::port {

namespace {

constexpr int kBalanceLimit = 0x7fff;
constexpr int kTierHighAbove = 0x4000;    // the tier High above this, Low below its negative
constexpr int kTierBackToMiddle = 0x2fff; // High falls back to Middle at this and below (Low at its negative and above)

// exe 0x5e51d0 / 0x5e5260: a balance += d (+-kBalanceLimit) and its tier byte (the stores of the exe: the tier
// written back to Middle when it leaves High / Low, then to High above kTierHighAbove, Low below its negative)
int addToBalance(int16_t& balance, int8_t& tier, int d) {
    int v = balance + d;
    if (d < 1) {
        if (v < -kBalanceLimit) v = -kBalanceLimit;
    } else if (v > kBalanceLimit) {
        v = kBalanceLimit;
    }
    balance = int16_t(v);
    const BalanceTier t = BalanceTier(tier);
    if (t == BalanceTier::High ? v <= kTierBackToMiddle : (t == BalanceTier::Low && v >= -kTierBackToMiddle))
        tier = int8_t(BalanceTier::Middle);
    if (v > kTierHighAbove) tier = int8_t(BalanceTier::High);
    if (v > -(kTierHighAbove + 1)) return tier;
    tier = int8_t(BalanceTier::Low);
    return int(BalanceTier::Low);
}

} // namespace

int zennyAdd(int32_t v) {
    GameState& g = gameState();
    if (g.zenny < -v) return -1;
    g.zenny = int32_t(uint32_t(g.zenny) + uint32_t(v));
    if (g.zenny > kZennyMax) g.zenny = kZennyMax;
    return 0;
}

int balanceAdd(int d) { return addToBalance(gameState().balance, gameState().balanceTier, d); }
int toolKitBalanceAdd(int d) { return addToBalance(gameState().toolKitBalance, gameState().toolKitTier, d); }

int weaponEnergyAdd(Player& p, int v) {
    const int n = p.energyPool + v;
    if (n < 0) {
        p.energyPool = 0;
        return -1;
    }
    p.energyPool = p.energyReserve < n ? p.energyReserve : int16_t(n);
    return 0;
}

} // namespace od::port
