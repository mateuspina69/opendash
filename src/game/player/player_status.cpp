// The player's status effects (see player_status.hpp).
#include "game/player/player_status.hpp"

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_points.hpp"
#include "game/message.hpp"
#include "game/player/player_effects.hpp"
#include "game/player/player_state.hpp"
#include "game/actor_movement.hpp"
#include "game/script_state.hpp"
#include "game/status_markers.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

constexpr int8_t kDrainDrag = 8;            // the floor drain's drag
constexpr int8_t kSlowDrag = 0xc;
// the orbit status's marker (GlobalStaticType::SmokeColumn): its variant (the other markers: game/status_markers)
constexpr uint8_t kOrbitMarkerVariant = 2;
constexpr int16_t kMovingShortens = 4;      // the directions pressed shorten a timer by this more
// the four orbit markers' phases (exe 0x8c4aec)
const DataTable<ScalarTable<uint8_t, 4>> orbitMarkerPhases{TableId::PlayerStatusOrbitMarkerPhases};
// the light colours by the balance (good above +0x4000, bad below -0x4000, neutral within +-0x2000, graded between)
constexpr uint32_t kLightNeutral = 0x808080, kLightGood = 0xc0c0c0, kLightBad = 0x404040;
constexpr int kBalanceFull = 0x4000, kBalanceNeutral = 0x2000;

// the marker variant: the status word's top nibble's bit into bit 7
uint8_t markerVariant(const Player& p, uint8_t low) { return uint8_t(((p.status >> 12) << 7 | low) & 0xff); }

// a status timer runs (4 more while a direction is pressed); true when it ran out
bool timerRunsOut(Player& p, size_t effect) {
    auto&& ticks = p.statusTicks[effect];
    if (p.pressed & p.directionButtons()) ticks = wrap16(ticks - kMovingShortens);
    ticks = wrap16(ticks - 1);
    return ticks <= 0;
}

} // namespace

namespace {

constexpr int16_t kFloorStatusTicks = 0x200;
constexpr int8_t kBurnFloorWade = 0x40;
constexpr int8_t kLegsGrip = 4; // the leg parts the slippery floor does not slide
constexpr size_t kDrainTimer = 0, kBurnTimer = 1; // Player::statusTicks

// the player's height on the floor (the lower of its y and its ground) past the floor's limit, not airborne for two
// ticks, no script running (exe 0x572090 / 0x5720f0's common test)
bool onStatusFloor(const PlayerEnv& env, const Player& p, int limit) {
    const int y = p.pos.y.whole;
    int height = actorGround(env, p); // exe 0x40f190
    if (y < height) height = y;
    else height = actorGround(env, p);
    if (height <= limit) return false;
    if (p.airborne != 0 && p.wasAirborne != 0) return false;
    return scriptState().flags == 0;
}

} // namespace

int drainFloor(const PlayerEnv& env, Player& p, int limit) {
    if (!onStatusFloor(env, p, limit)) return 0;
    p.setStatusBits(kStatusDrain | kStatusDrainFloor);
    p.statusTicks[kDrainTimer] = kFloorStatusTicks;
    return 1;
}

int burnFloor(const PlayerEnv& env, Player& p, int limit) {
    if (!onStatusFloor(env, p, limit)) return 0;
    if (!storyFlag(kFlagBurnArmour)) {
        p.setStatusBits(kStatusBurn | kStatusBurnFloor);
        p.statusTicks[kBurnTimer] = kFloorStatusTicks;
    } else {
        p.setStatusBits(kStatusBurnFloor);
    }
    if (p.legPart != int8_t(kLegsNoBurn)) p.wadeDepth = kBurnFloorWade;
    return 1;
}

void slipperyFloor(Player& p) {
    if (scriptState().flags == 0 && p.legPart != kLegsGrip) p.slippery = 1;
}

void updateStatusEffects(Player& p) {
    if (storyFlag(kFlagBurnArmour)) p.status &= uint16_t(~kStatusBurn);
    if (storyFlag(kFlagSlowArmour)) p.status &= uint16_t(~kStatusSlowed);
    const uint8_t frame = uint8_t(gameState().frame);
    if (p.status & (kStatusDrain | kStatusDrainFloor)) {
        const bool floor = (p.status & kStatusDrainFloor) != 0;
        if (floor) p.drag = kDrainDrag;
        if (uint8_t(p.immunity) == 0 && p.hitPoints > 0) {
            bool hurt;
            if (floor) hurt = p.legPart != kLegsNoBurn || (frame & 0xf) == 0;
            else hurt = (frame & 0xf) == 0;
            if (hurt) hitPointsChange(p, -1); // exe 0x5e5320 (game/hit_points)
        }
        if (!(p.status & (kStatusDrain << kStatusMarkerShift))) {
            p.status &= uint16_t(~(kStatusBurn | kStatusBurnFloor));
            statusMarker(GlobalStaticType::DrainBurnMarker, markerVariant(p, kMarkerOfDrain));
            p.setStatusBits(uint16_t(kStatusDrain << kStatusMarkerShift));
        }
        if ((p.status & kStatusDrain) && timerRunsOut(p, 0)) p.status &= uint16_t(~kStatusDrain);
    }
    if (p.status & (kStatusBurn | kStatusBurnFloor)) {
        if (uint8_t(p.immunity) == 0 && p.hitPoints > 0) {
            int d = 0;
            if (p.status & kStatusBurnFloor) {
                if (p.legPart != kLegsNoBurn) d = -2;
                else if ((frame & 7) == 0) d = -1;
            } else if ((frame & 7) == 0) {
                d = -1;
            }
            if (d) hitPointsChange(p, d); // exe 0x5e5320 (game/hit_points)
        }
        if (!(p.status & (kStatusBurn << kStatusMarkerShift))) {
            p.status &= uint16_t(~(kStatusDrain | kStatusDrainFloor));
            statusMarker(GlobalStaticType::DrainBurnMarker, markerVariant(p, 0));
            p.setStatusBits(uint16_t(kStatusBurn << kStatusMarkerShift));
        }
        if ((p.status & kStatusBurn) && timerRunsOut(p, 1)) p.status &= uint16_t(~kStatusBurn);
    }
    if (p.status & (kStatusSlowed | kStatusSlowedFloor)) {
        p.drag = kSlowDrag;
        if (!(p.status & (kStatusSlowed << kStatusMarkerShift))) {
            statusMarker(GlobalStaticType::SlowMarker, 0);
            p.setStatusBits(uint16_t(kStatusSlowed << kStatusMarkerShift));
        }
        if (p.status & kStatusSlowed) {
            p.statusTicks[2] = wrap16(p.statusTicks[2] - 1);
            if (p.statusTicks[2] <= 0) p.status &= uint16_t(~kStatusSlowed);
        }
    }
    if (p.status & (kStatusOrbit | kStatusOrbitFloor)) {
        if (!(p.status & (kStatusOrbit << kStatusMarkerShift))) {
            for (size_t i = 0; i < orbitMarkerPhases.size(); ++i) {
                const uint32_t a = statusMarker(GlobalStaticType::SmokeColumn, kOrbitMarkerVariant);
                if (!a) break;
                staticActorAt(a).params[0] = orbitMarkerPhases[i];
            }
            p.setStatusBits(uint16_t(kStatusOrbit << kStatusMarkerShift));
        }
        if (p.status & kStatusOrbit) {
            p.statusTicks[3] = wrap16(p.statusTicks[3] - 1);
            if (p.statusTicks[3] <= 0) p.status &= uint16_t(~kStatusOrbit);
        }
    }
    if ((p.status & 0xff) == 0) { // no status: the light colour by the balance
        int i = gameState().balance;
        uint32_t light = 0;
        bool graded = false;
        if (i < 0) {
            if (i < -kBalanceFull) light = kLightBad;
            else if (-kBalanceNeutral < i) light = kLightNeutral;
            else {
                i += 0x6000;
                graded = true;
            }
        } else {
            if (kBalanceFull < i) light = kLightGood;
            else if (i < kBalanceNeutral) light = kLightNeutral;
            else {
                i += kBalanceNeutral;
                graded = true;
            }
        }
        if (graded) {
            const uint32_t v = uint32_t(i >> 7);
            light = (v << 8 | v) << 8 | v;
        }
        p.ambientColour = light;
    }
    if (uint8_t(p.wadeDepth) != 0) { // the wade's drag by the sink depth
        const int d = (-int(p.collisionBox->maxY) >> 3) + 7;
        if (p.drag < d) p.drag = int8_t(d);
    }
    p.status &= kStatusTickMask;
}

} // namespace od::port
