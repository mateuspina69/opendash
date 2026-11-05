// A player down in a session (see player_downed.hpp).
#include "game/player/player_downed.hpp"

#include "game/player/player_state.hpp"

namespace od::port {

namespace {
bool g_downs = false;
} // namespace

void setPlayerDownsInstead(bool downs) { g_downs = downs; }
bool playerDownsInstead() { return g_downs; }

bool playerIsDowned(const Player& p) {
    return p.state == uint8_t(PlayerState::Dead) && p.subState == uint8_t(DeadStep::Downed);
}

void playerRevive(Player& p, int16_t life) {
    p.hitPoints = life;
    p.enterState(PlayerState::Playing);
    p.moveToSub(PlayerSub::KnockDown, KnockDownStep::Lie); // its animation is over: they get up
}

} // namespace od::port
