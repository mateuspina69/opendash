// The player on the jet skates: the skate sub-state (the kneel, the launch with the engine loop, the straight run and the
// leans of the turns, the stop and the brake) and the skate fall (the ground lost while skating: the hop, the fall, the
// landing back into skating or to a stop).
#include "game/audio_logic.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/player/player_animation.hpp"
#include "game/player/player_effects.hpp"
#include "game/player/player_jumps.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_moves.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_substates.hpp"
#include "game/player/player_weapons.hpp"

namespace od::port {

namespace {

constexpr int16_t kSkateLaunchSpeed = -0x280; // the launch's forward speed (and the engine pitch's zero)
constexpr int16_t kSkateCruise = -0x340;      // no direction held: the cruise speed
constexpr int16_t kSkateBrakeSpeed = -0x280;  // back held: braked to this
constexpr int16_t kSkateTopSpeed = -0x400;    // forward held: the top speed
constexpr int16_t kSkateAccel = 8;
constexpr int16_t kSkateDecel = 0x10;
constexpr int16_t kSkateLeanCruise = 0x80;    // the lean (the move direction's offset) by the speed held
constexpr int16_t kSkateLeanBrake = 0xc0;
constexpr int16_t kSkateLeanTop = 0x40;
constexpr int8_t kKneelLaunchFrame = 0xc;     // the kneel's frame that launches
constexpr int16_t kSkateStopDrag = 0x28;      // the brake per tick (on a slippery floor: kSkateStopSlide)
constexpr int16_t kSkateStopSlide = 8;
constexpr int16_t kSkateSprayBrakeSpeed = -0x140; // braking in water faster than this: the spray
constexpr int16_t kSkateHopSpeed = -0x180;    // the skate fall's hop (forward held: kSkateHopForward)
constexpr int16_t kSkateHopForward = -0x2e0;
constexpr int8_t kSkateLandDustFrame = 2;
constexpr int8_t kSkateResumeFrame = 6;       // the brake's frame after which the run takes over again

// the speed cut by the burden (> 0: burden / 0x40 of it)
int16_t withBurden(const Player& p, int v) {
    return p.burden > 0 ? int16_t(v - wrap16((p.burden * v) >> 6)) : int16_t(v);
}

// exe 0x5765c0: the skating speed and lean: no direction -> the cruise, back -> the brake speed, forward -> the top
// speed; the lean by the speed band (to the right: negated); returns the turn side (0 none, 1 / 2) of the stick turn,
// forced by left / right
int skateSpeed(Player& p) {
    const uint16_t held = p.held;
    uint32_t side;
    int16_t limit;
    bool reached = false; // the speed reached the band's limit this tick (else it is set to the limit)
    if (!(held & (p.button(PlayerButton::Back) | p.button(PlayerButton::Forward)))) {
        side = turnByAnalogStick(p, kSkateCruiseTurn, kAnalogGentle);
        p.directionOffset.y = kSkateLeanCruise;
        limit = withBurden(p, kSkateCruise);
        const int16_t v = p.velocity.z;
        if (limit < v) {
            p.velocity.z = wrap16(v - kSkateAccel);
            reached = limit <= wrap16(v - kSkateAccel);
        } else {
            p.velocity.z = wrap16(v + kSkateDecel);
            reached = wrap16(v + kSkateDecel) <= limit;
        }
    } else if (!(held & p.button(PlayerButton::Forward))) {
        side = turnByAnalogStick(p, kSkateBrakeTurn, kAnalogGentle);
        p.directionOffset.y = kSkateLeanBrake;
        limit = withBurden(p, kSkateBrakeSpeed);
        p.velocity.z = wrap16(p.velocity.z + kSkateDecel);
        reached = p.velocity.z <= limit;
    } else {
        side = turnByAnalogStick(p, kSkateTopTurn, kAnalogGentle);
        p.directionOffset.y = kSkateLeanTop;
        limit = withBurden(p, kSkateTopSpeed);
        p.velocity.z = wrap16(p.velocity.z - kSkateAccel);
        reached = limit <= p.velocity.z;
    }
    if (!reached) p.velocity.z = limit;
    const uint16_t sides = uint16_t(p.button(PlayerButton::Left) | p.button(PlayerButton::Right));
    const uint16_t d = sides & held;
    if (d == 0 || d == sides) {
        p.directionOffset.y = 0;
        return int(side);
    }
    if (held & p.button(PlayerButton::Right)) {
        if (side == 0) side = 1;
        p.directionOffset.y = wrap16(-p.directionOffset.y);
        return int(side);
    }
    return side == 0 ? 2 : int(side);
}

// exe 0x5d56b0(+0x14b, (-0x280 - vz) >> 4): the engine loop's voice pitched by the speed (the exe's 0x57639d / 0x57648b)
void engineSound(Player& p) {
    soundUpdateLoopAtPlayer(uint32_t(int32_t(p.skateEngine)), uint16_t((kSkateLaunchSpeed - int32_t(p.velocity.z)) >> 4));
}
// the exe's 0x5764aa: the spray in water on even frames
void sprayInWater(Player& p) {
    if ((gameState().frame & 1) == 0 && p.inWater != 0) skateSpray(p);
}
// the exe's 0x57647e / 0x576481: a turn's animation (and its step), the turn sound, the engine, the spray
void skateTurn(Player& p, int8_t anim, int stepDelta) {
    if (anim >= 0) {
        requestAnimId(p, anim, 0, false);
        p.step = uint8_t(int8_t(p.step) + stepDelta);
    }
    soundPlayAtPlayer(uint16_t(PlayerSound::SkateTurn));
    engineSound(p);
    sprayInWater(p);
}
// the engine loop started (exe 0x5d5580 -> 0x5d5480: its voice handle kept)
void startEngine(Player& p) {
    p.skateEngine = int8_t(uint8_t(soundStartLoopAtPlayer(uint16_t(PlayerSound::SkateEngine), 0)));
}

} // namespace

void skateInput(Player& p) {
    const uint8_t step = p.step;
    const bool walk = p.holds(PlayerButton::Walk);
    if (step < uint8_t(SkateStep::Launch)) { // the modifier released while kneeling: jump or stand
        if (!walk) {
            p.stopSkating();
            if (tryJump(p) == 0) {
                turnOrStopVelocities(p, 0, kAlsoLocalVelocity);
                p.enterSub(PlayerSub::Idle);
            }
        }
        return;
    }
    if (step > uint8_t(SkateStep::EngineStart) && step < uint8_t(SkateStep::Stop) &&
        (!walk || (p.legPart != kLegsJetSkates && p.inWater == 0) || (p.legPart != kLegsWaterSkates && p.inWater != 0)))
        p.setStep(SkateStep::Stop);
    if (p.step > uint8_t(SkateStep::Stop) && p.body.key.frame > kSkateResumeFrame) { // braked: run again
        tryRunForward(p, kRunTurned);
        p.stopSkating();
    }
}

void skateMotion(Player& p) {
    switch (SkateStep(p.step)) {
    case SkateStep::Kneel:
        p.setArmBits(kArmUpperOnly);
        p.skating = 1;
        p.skateHoldTicks = int8_t(p.skateHoldTicks + 1);
        p.skateSpray = 0;
        p.velocity.x = 0;
        p.velocity.y = 0;
        p.lockSuspended = 1;
        requestAnim(p, PlayerAnim::SkateKneel, 0, false);
        p.step = uint8_t(p.step + 1u);
        return;
    case SkateStep::KneelWait:
        if (p.body.key.frame == kKneelLaunchFrame) p.step = uint8_t(p.step + 1u);
        return;
    case SkateStep::Launch:
        p.velocity.z = kSkateLaunchSpeed;
        if (p.burden > 0) p.velocity.z = wrap16(kSkateLaunchSpeed - wrap16((p.burden * kSkateLaunchSpeed) >> 6));
        if (p.bodyAnimEnded()) {
            requestAnim(p, PlayerAnim::Skate, 0, false);
            startEngine(p);
            p.step = uint8_t(p.step + 2u);
        }
        return;
    case SkateStep::EngineStart:
        startEngine(p);
        p.step = uint8_t(p.step + 1u);
        [[fallthrough]];
    case SkateStep::Straight: {
        const int side = skateSpeed(p);
        if (side == 0) {
            if (p.bodyAnimEnded()) requestAnim(p, PlayerAnim::Skate, 0, false);
        } else if (side == 1) {
            requestAnim(p, PlayerAnim::SkateTurnOneWay, 0, false);
            p.step = uint8_t(p.step + 1u);
        } else if (side == 2) {
            requestAnim(p, PlayerAnim::SkateTurnOtherWay, 0, false);
            p.step = uint8_t(p.step + 2u);
        }
        // every path reaches 0x57639d: a tick without the refresh lets the loop voices' update (game/audio_logic) free the engine voice
        engineSound(p);
        sprayInWater(p);
        return;
    }
    case SkateStep::TurnOneWay: {
        const int side = skateSpeed(p);
        if (side == 0) {
            skateTurn(p, int8_t(PlayerAnim::SkateTurnOneWayBack), -1);
        } else if (side == 2) {
            skateTurn(p, int8_t(PlayerAnim::SkateSwitchToOtherWay), 1);
        } else {
            if (side == 1 && p.bodyAnimEnded()) requestAnim(p, PlayerAnim::SkateLeanOneWay, 0, false);
            skateTurn(p, -1, 0);
        }
        return;
    }
    case SkateStep::TurnOtherWay: {
        const int side = skateSpeed(p);
        if (side == 0) {
            skateTurn(p, int8_t(PlayerAnim::SkateTurnOtherWayBack), -2);
        } else if (side == 1) {
            skateTurn(p, int8_t(PlayerAnim::SkateSwitchToOneWay), -1);
        } else {
            if (side == 2 && p.bodyAnimEnded()) requestAnim(p, PlayerAnim::SkateLeanOtherWay, 0, false);
            skateTurn(p, -1, 0);
        }
        return;
    }
    case SkateStep::Stop:
        requestAnim(p, PlayerAnim::SkateStop, 0, false);
        turnByAnalogStick(p, kSkateBrakeTurn, kAnalogGentle);
        soundStopLoop(uint32_t(int32_t(p.skateEngine))); // exe 0x5d56f0: the engine stopped
        soundPlayAtPlayer(uint16_t(PlayerSound::SkateStop));
        p.step = uint8_t(p.step + 1u);
        return;
    case SkateStep::Brake:
        p.velocity.z = wrap16(p.velocity.z + (p.slippery != 0 ? kSkateStopSlide : kSkateStopDrag));
        if (p.velocity.z > 0) p.velocity.z = 0;
        turnByAnalogStick(p, kSkateBrakeTurn, kAnalogGentle);
        if (p.inWater == 0)
            skidSparks(p); // the brake's sparks
        else if (p.velocity.z < kSkateSprayBrakeSpeed)
            skateSpray(p);
        if (p.bodyAnimEnded()) {
            turnOrStopVelocities(p, 0, kAlsoLocalVelocity);
            p.lockSuspended = 0;
            p.stopSkating();
            p.enterSub(PlayerSub::Idle);
        }
        return;
    default:
        return;
    }
}

void skateFallMotion(Player& p) {
    const uint16_t held = p.held;
    switch (p.stepAs<SkateFallStep>()) {
    case SkateFallStep::Hop:
        p.body.prevAnim = p.body.anim;
        p.airborne = kAirborneFalling;
        if (((p.button(PlayerButton::Back) | p.button(PlayerButton::Forward)) & held) == 0) {
            p.velocity.y = kSkateHopSpeed;
            if (p.inWater != 0) splash(p); // exe 0x57b990
            soundPlayAtPlayer(uint16_t(PlayerSound::Jump));
        } else if (p.button(PlayerButton::Forward) & held) {
            p.velocity.y = kSkateHopForward;
            if (p.inWater != 0) splash(p);
            soundPlayAtPlayer(uint16_t(PlayerSound::Jump));
        } else {
            p.velocity.y = 0;
        }
        soundStopLoop(uint32_t(int32_t(p.skateEngine))); // exe 0x5d56f0: the engine stopped
        soundPlayAtPlayer(uint16_t(PlayerSound::SkateStop));
        putArmDown(p);
        requestAnim(p, PlayerAnim::SkateHop, 0, false);
        airControl(p, kAirSkateFall);
        p.step = uint8_t(p.step + 1u);
        return;
    case SkateFallStep::HopRising:
        if (p.bodyAnimEnded()) {
            requestAnim(p, PlayerAnim::SkateFall, 0, false);
            p.step = uint8_t(p.step + 1u);
        }
        [[fallthrough]];
    case SkateFallStep::Falling:
        applyJumpGravity(p, kGravityRise);
        airControl(p, kAirSkateFall);
        return;
    case SkateFallStep::Land:
        if (p.body.key.frame == kSkateLandDustFrame) dustPuff(p, kDustAtFeet); // the landing dust
        if (p.button(PlayerButton::Walk) & held) { // the modifier held: back to skating
            if (!p.bodyAnimEnded()) return;
            requestAnim(p, PlayerAnim::SkateLandOn, 0, false);
            p.leanTarget = 0;
            p.enterSubAt(PlayerSub::Skate, SkateStep::EngineStart);
            return;
        }
        p.step = uint8_t(p.step + 1u);
        return;
    case SkateFallStep::Stop:
        if (p.bodyAnimEnded()) {
            p.stopSkating();
            if (p.button(PlayerButton::Forward) & held) {
                requestAnim(p, PlayerAnim::SkateLandStop, 0, false);
                p.enterSubAt(PlayerSub::Skate, SkateStep::Stop);
                return;
            }
            p.leanTarget = 0;
            p.enterSubAt(PlayerSub::Skate, SkateStep::Stop);
        }
        return;
    default:
        return;
    }
}

} // namespace od::port
