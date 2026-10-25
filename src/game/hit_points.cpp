// The actors' hit points and the defeat tally (see hit_points.hpp).
#include "game/hit_points.hpp"

#include "game/actor_state.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

constexpr uint32_t kFlagNoDamage = 0x6f4; // the player takes no damage (a scripted scene)

} // namespace

int hitPointsAdd(Actor& actor, int delta) {
    actor.hitPoints = wrap16(actor.hitPoints + delta);
    if (actor.hitPoints < 0) {
        actor.hitPoints = -1;
        return -1;
    }
    if (actor.maxHitPoints < actor.hitPoints) actor.hitPoints = actor.maxHitPoints;
    return 0;
}

int hitPointsChange(Actor& actor, int delta) {
    if (delta < 0) {
        if (gameState().fieldStep() != FieldStep::Playing || storyFlag(kFlagNoDamage)) return 0;
        gameState().damageTaken = uint32_t(-delta);
        gameState().damageShown = uint32_t(-delta);
    }
    const int16_t hp = actor.hitPoints;
    if (hp > 0 && delta < 0) {
        const int16_t left = wrap16(hp + delta);
        actor.hitPoints = left;
        if (left >= 0) return 0;
        actor.hitPoints = 0;
        return 1;
    }
    return hitPointsAdd(actor, int16_t(delta));
}

void countDefeat() {
    gameState().defeats += 1u;
    gameState().defeatsTotal += 1u;
}

} // namespace od::port
