// The player's tick (see player_update.hpp).
#include "game/player/player_update.hpp"

#include <cstdlib>

#include <cstdio>

#include "engine/data_source.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/camera_drive.hpp"
#include "game/camera_state.hpp"
#include "game/distances.hpp"
#include "game/event_state.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/hit_list.hpp"
#include "game/player/moving_fire.hpp"
#include "game/player/player_animation.hpp"
#include "game/player/player_carry.hpp"
#include "game/player/player_damage.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_lock_on.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_pose.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_status.hpp"
#include "game/player/player_substates.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/room_triggers.hpp"
#include "game/script_state.hpp"
#include "game/message.hpp"
#include "game/event.hpp"
#include "game/net_world.hpp"
#include "game/story_flags.hpp"
#include "game/transition_state.hpp"

namespace od::port {

namespace {

// The state table (exe 0x8c4f5c) and the sub-state tables (A 0x8c4fc0: the input, B 0x8c4f6c: the motion): the
// handlers by state / sub-state (the named routines, engine/routines.csv)
const DataTable<RoutineTable<4>> stateHandlers{TableId::PlayerStateHandlers};
const DataTable<RoutineTable<21>> subInputHandlers{TableId::PlayerInputHandlers};
const DataTable<RoutineTable<21>> subMotionHandlers{TableId::PlayerMotionHandlers};

constexpr uint32_t kFlagNoLook = 0x24d;     // the look mode and the camera stick off
constexpr int kStickDeadZone = 0x38;
constexpr int kLookAimMax = 0x40;
constexpr int kAimFollowStep = 0x20;        // the aim pitch's return toward the camera's look pitch
constexpr int16_t kLockHoldTicks = 8;       // a trigger's lock request holds this long after it is released
constexpr uint8_t kLockReleaseTicks = 0xc;  // the lock button released: the remembered target kept this long
constexpr int8_t kLockTimeMax = 0x40;
constexpr int kGroundSnap = 0x10;           // the ground within this below the feet: snapped (on the skates only this;
constexpr int kGroundSnapWalking = 0x20;    // walking: this more)
constexpr int16_t kWaterSpeedCap = 0x140;   // the vertical speed's cap in water
constexpr int16_t kSkateWallSpeed = -0x280; // the skates' speed after a wall hit
constexpr int16_t kFallRunSpeed = -0x200;   // a fall faster forward than this: the running jump's fall
constexpr int16_t kSinkStep = 2;            // the water sink of the collision box per tick
constexpr int16_t kSinkMax = -0x40;
constexpr int16_t kSinkLegs = -0x30, kSinkFeet = -0x18; // deeper than these: the legs / the feet hidden
constexpr uint16_t kLegsHidden = 0x6c00, kFeetHidden = 0x4800;
constexpr int16_t kHitHeight = 0x58;        // the player's hit-list entry above the feet
constexpr uint32_t kPlayerHitRadius = 0x30;
constexpr uint32_t kPlayerTouchDamage = 4;
constexpr int8_t kFallImmuneTicks = 0x40;
constexpr uint32_t kAmbientNeutral = 0x808080;
constexpr uint32_t kStageAnimsUnset = 0xffffffffu;

// exe 0x575200's sub-state call: the handler of the table at the player's sub-state
void runSubHandler(const DataTable<RoutineTable<21>>& table, const PlayerEnv& env, Player& p) {
    const RoutineRef& fn = table[p.subState];
    switch (fn.id()) {
    case RoutineId::EmptyRoutine: return; // an empty handler
    case RoutineId::PlayerIdleInput: idleInput(p); return;
    case RoutineId::PlayerRunInput: runInput(p); return;
    case RoutineId::PlayerTurnInPlaceInput: turnInPlaceInput(p); return;
    case RoutineId::PlayerTurnInPlaceMotion: turnInPlaceMotion(p); return;
    case RoutineId::PlayerJumpInput: jumpInput(env, p); return;
    case RoutineId::PlayerIdleMotion: idleMotion(p); return;
    case RoutineId::PlayerRunMotion: runMotion(p); return;
    case RoutineId::PlayerJumpMotion: jumpMotion(p); return;
    case RoutineId::PlayerRunningJumpMotion: runningJumpMotion(p); return;
    case RoutineId::PlayerSideHopMotion: sideHopMotion(p); return;
    case RoutineId::PlayerBarHangMotion: barHangMotion(p); return;
    case RoutineId::PlayerLiftMotion: liftMotion(p); return;
    case RoutineId::PlayerBarHangInput: barHangInput(p); return;
    case RoutineId::PlayerLedgeHangInput: ledgeHangInput(p); return;
    case RoutineId::PlayerLedgeHangMotion: ledgeHangMotion(env, p); return;
    case RoutineId::PlayerWalkInput: walkInput(p); return;
    case RoutineId::PlayerWalkMotion: walkMotion(p); return;
    case RoutineId::PlayerQuickTurnMotion: quickTurnMotion(p); return;
    case RoutineId::PlayerSideStepInput: sideStepInput(p); return;
    case RoutineId::PlayerSideStepMotion: sideStepMotion(p); return;
    case RoutineId::PlayerLookInput: lookInput(p); return;
    case RoutineId::PlayerLookMotion: lookMotion(p); return;
    case RoutineId::PlayerTalkMotion: talkMotion(p); return;
    case RoutineId::PlayerEventPoseMotion: eventPoseMotion(p); return; // sub-state 19
    case RoutineId::PlayerItemGetMotion: itemGetMotion(p); return;   // sub-state 20
    case RoutineId::PlayerSkateInput: skateInput(p); return;
    case RoutineId::PlayerSkateMotion: skateMotion(p); return;
    case RoutineId::PlayerTryLedgeGrab: tryLedgeGrab(env, p); return; // A 9: the ledge grab while falling
    case RoutineId::PlayerSkateFallMotion: skateFallMotion(p); return;
    case RoutineId::PlayerKnockBackInput: knockBackInput(p); return;
    case RoutineId::PlayerKnockBackMotion: knockBackMotion(p); return;
    case RoutineId::PlayerKnockDownMotion: knockDownMotion(p); return;
    case RoutineId::PlayerHeldInput: heldInput(p); return;
    case RoutineId::PlayerHeldMotion: heldMotion(p); return;
    default: {
        char msg[64];
        std::snprintf(msg, sizeof msg, "sub-state %u handler 0x%06x", unsigned(p.subState), fn.pointerValue());
        notPorted(kPlayerArea, msg);
    }
    }
}

// exe 0x575330: the init: playing, the motion and the animations cleared, the floor / control bytes, the weapons, the
// hit boxes, the light, the button map, the idle tables, the idle animation
void initState(Player& p) {
    p.enterState(PlayerState::Playing);
    p.velocity.clearWords();
    p.runFlags |= uint8_t(kActorRun | kActorShown);
    p.pushVelocity.clearWords();
    p.body.prevAnim = uint8_t(PlayerAnim::None);
    p.body.anim = uint8_t(PlayerAnim::None);
    p.idleTracks[1].table = -1;
    p.idleTracks[0].table = -1;
    p.drag = 0;
    p.slippery = 0;
    p.inWater = 0;
    p.burden = 0;
    p.channelMix = 0;
    p.leanLocked = 0;
    p.inputMode = 0;
    p.recentreAim = 0;
    p.lockSuspended = 0;
    p.jumpBuffered = 0;
    p.liftMode = 0;
    p.specialFiring = 0;
    p.suction = 0;
    clearWeaponState(p);
    enableFiring(p);
    p.solidHit.result = 0;
    p.hitBox.result = 0;
    p.ambientColour = kAmbientNeutral;
    p.stageAnimMarker = kStageAnimsUnset;
    p.hurtSum = 0;
    p.hurtCount = 0;
    loadDefaultButtonMap(p);
    p.padType = pad0().type;
    selectIdleTables(p, kIdleCalm, kIdleCalm);
    requestAnim(p, PlayerAnim::Idle, 0, true);
    startUpperAnimation(p, 0, PlayerAnim::Idle, 0);
}

// exe 0x575440's immunity: the blink of a timed immunity (hidden on alternate pairs of ticks)
void updateImmunity(Player& p) {
    if (p.invulnerable == 0 && p.immunity == 0) {
        p.runFlags |= uint8_t(kActorRun | kActorShown);
        applyHits(p);
        return;
    }
    uint8_t flags;
    if (p.immunity < 1) {
        flags = uint8_t(p.runFlags | kActorRun | kActorShown);
    } else {
        const uint8_t ticks = uint8_t(p.immunityTicks - 1);
        p.immunityTicks = int8_t(ticks);
        if (int8_t(ticks) < 0) {
            p.runFlags |= uint8_t(kActorRun | kActorShown);
            p.immunity = 0;
            return;
        }
        if (p.immunity != kImmuneBlinking || !(ticks & 2))
            flags = uint8_t(p.runFlags | kActorRun | kActorShown);
        else
            flags = uint8_t(p.runFlags & ~kActorShown);
    }
    p.runFlags = flags;
}

// exe 0x575440's lock-on: the triggers' lock requests (with their auto-lock options), the lock button, the aim pitch's
// return or the tracking of the target
void updateLockOn(Player& p, uint16_t held) {
    const GameOptions& options = gameOptions();
    const uint16_t special = p.button(PlayerButton::Special), fire = p.button(PlayerButton::Fire);
    const uint16_t lock = p.button(PlayerButton::Lock);
    if ((!(special & held) || options.autoLockSpecial == 0) && (!(fire & held) || options.autoLockBuster == 0)) {
        if ((p.lockRequest & (kLockSpecial | kLockBuster)) && !(p.carryFlags & kCarryThrowing)) {
            p.lockRequestTicks = wrap16(p.lockRequestTicks - 1);
            if (p.lockRequestTicks < 0) {
                p.lockRequestTicks = 0;
                p.recentreAim = 1;
                if (p.lockRequest & kLockFired) p.lockRequest = 0;
            }
        }
    } else {
        bool timed = true;
        if (!(special & held)) {
            if (!(p.armFlags & kArmBuster) && p.carryFlags == 0) p.lockRequest = kLockKeepAim | kLockBuster;
        } else if (!(p.armFlags & kArmSpecial)) {
            p.lockRequest = kLockKeepAim | kLockSpecial;
            p.lockRequestTicks = kLockHoldTicks;
            timed = false;
        }
        if (timed) p.lockRequestTicks = kLockHoldTicks;
    }
    if (lock & p.released) p.recentreAim = 1;
    if (!(lock & held) && !(p.lockRequest & (kLockSpecial | kLockBuster))) { // no lock: the aim back to the camera
        if (cameraDriven()) p.recentreAim = 0; // modern: the camera is the player's, nothing to recentre (camera_drive)
        if (p.recentreAim == 0)
            p.rotation.x = int16_t(approachAngle(cameraState().lookPitch, p.rotation.x, kAimFollowStep));
        else
            recentreAim(p);
        dropLockOn(p);
        if (p.lockReleaseTicks != 0) {
            const uint8_t ticks = uint8_t(p.lockReleaseTicks - 1);
            p.lockReleaseTicks = ticks;
            if (ticks == 0) {
                p.lockRemembered = 0;
                p.homingTarget = 0;
            }
        }
        p.lockTicks = 0;
    } else {
        if (p.lockTarget != 0 && p.lockSuspended != 0 &&
            (p.sub() == PlayerSub::KnockBack || p.sub() == PlayerSub::RunningJump)) {
            // OpenDash (modern aim, not the exe): knocked back by a hit (the knock-back suspends the lock, then the
            // recovery's landing), the lock stays on its target -- no turning while hurt, but no drop / re-acquire either
            // (the lock camera kept swinging away and back on every hit of the train fight's bombs)
            p.lockTicks = 0;
        } else if (p.lockTarget == 0 || p.lockSuspended != 0) {
            p.lockTicks = 0;
            if (p.recentreAim != 0) recentreAim(p);
            dropLockOn(p);
        } else { // the exe's 0x57564e: the lock time counts while the target stays the remembered one
            if (p.lockTarget == p.lockRemembered) {
                if (p.lockTicks < kLockTimeMax) p.lockTicks = int8_t(p.lockTicks + 1);
            } else {
                p.lockTicks = 0;
            }
            p.recentreAim = 0;
            trackLockOn(p);
        }
        p.lockReleaseTicks = kLockReleaseTicks;
    }
    // the camera stick aims (an analog pad, no lock-on)
    if (!p.holds(PlayerButton::Aim) && (!p.holds(PlayerButton::Lock) || p.lockedActor != nullptr)) {
        if (uint8_t(p.padType) == kPadTypeAnalog && !storyFlag(kFlagNoLook) && p.lockedActor == nullptr &&
            !p.holds(PlayerButton::Look)) {
            int rate = std::abs(p.lookAxis) - kStickDeadZone;
            if (rate >= 0) {
                rate = ((rate * rate) >> 7) + 1;
                p.recentreAim = 0;
                if (rate > kLookAimMax) rate = kLookAimMax;
                CameraState& camera = cameraState();
                if (turnAimByLookStick(p, wrap16(rate), rate) != 0)
                    camera.lookPitch = int16_t(approachAngle(p.rotation.x, camera.lookPitch, rate));
            }
        }
    } else if (!(p.lockRequest & kLockKeepAim)) {
        p.recentreAim = 1;
    }
}

// exe 0x575440: the play: the previous pose, the hits, the lock-on, the arms, the carry, the sub-state handlers (A,
// then B), the charge, the move (the speeds cut by the drag / in water) in the facing frame, the push-out, the head
// on the ceiling, the skates' wall, the actor collision's mover entry
void playingState(const PlayerEnv& env, Player& p) {
    const uint16_t held = p.held;
    p.savePreviousPose();
    p.body.prevAnim = p.body.anim;
    p.upper.prevAnim = p.upper.anim;
    p.savePreviousContact();
    updateImmunity(p);
    updateLockOn(p, held);
    p.specialFiring = 0;
    p.suction = 0;
    if (p.armFlags & kArmUp) {
        if (p.armFlags & kArmBuster)
            busterArmUpdate(p);
        else
            specialArmUpdate(p);
    }
    rechargeSpecialGauge(p);
    if (p.carryFlags & kCarryThrowing) throwUpdate(p);
    runSubHandler(subInputHandlers, env, p);
    runSubHandler(subMotionHandlers, env, p);
    keepFiringWhileMoving(p); // OpenDash (game/player/moving_fire)
    updateCharge(p, held);
    int16_t vx = wrap16(p.pushVelocity.x + p.velocity.x);
    int16_t vz = wrap16(p.pushVelocity.z + p.velocity.z);
    int16_t vy = wrap16(p.velocity.y + p.pushVelocity.y);
    if (!scriptState().inUse()) {
        const int k = 0x10 - p.drag;
        if (p.inWater == 0) {
            vz = wrap16((vz * k) >> 4);
            vx = wrap16((vx * k) >> 4);
        } else {
            if (p.velocity.y > kWaterSpeedCap) p.velocity.y = kWaterSpeedCap;
            vy = int16_t(vy >> 1);
            if (p.legPart == kLegsWaterSkates && (p.sub() == PlayerSub::Skate || p.sub() == PlayerSub::SkateFall)) {
                vz = wrap16((vz * k) >> 4);
                vx = wrap16((vx * k) >> 4);
            } else {
                vz = wrap16((vz * k) >> 5);
                vx = wrap16((vx * k) >> 5);
            }
        }
    }
    moveInFacing(p, vx, vy, vz);
    p.drag = 0;
    const uint32_t r = actorPushOut(env, p) | p.actorHit;
    p.wallHit = r;
    const PlayerSub sub = p.sub();
    if (p.airborne == kAirborneFalling && (r & kWallOnYMinus) && (sub == PlayerSub::Jump || sub == PlayerSub::RunningJump) &&
        p.step < uint8_t(JumpStep::Peak)) { // the head hit the ceiling
        requestAnim(p, PlayerAnim::RunningJumpFall, 0, true);
        p.setStep(JumpStep::Peak);
        p.velocity.y = 0;
        p.pushVelocity.y = 0;
        p.acceleration.y = 0;
    }
    if ((r & kWallOnSides) && sub == PlayerSub::Skate && p.step > uint8_t(SkateStep::EngineStart) &&
        p.step < uint8_t(SkateStep::Stop)) {
        const int8_t b = p.burden;
        p.velocity.z = kSkateWallSpeed;
        if (b > 0) p.velocity.z = wrap16(kSkateWallSpeed - wrap16((b * kSkateWallSpeed) >> 6));
    }
    p.solidHit.result = 0;
    actorCollisionSubmitMover(p);
}

// exe 0x57b6d0: dead (from the knock-down): collision only; the first tick the game over (game mode 8)
void deadState(const PlayerEnv& env, Player& p) {
    actorPushOut(env, p);
    p.prevPos = p.pos;
    if (p.subState == uint8_t(DeadStep::Enter) && !netWorldHooks().holdDeath) { // netplay: held for the respawn
        gameState().setMode(GameMode::GameOver);
        gameState().modeStep = 0;
        p.subState = uint8_t(p.subState + 1u);
    }
}

// the ground within the snap distance below the feet (on the skates the shorter one)
int snapReach(const Player& p) { return ((int(p.skating) ^ 1) * kGroundSnapWalking) + kGroundSnap; }

// exe 0x575200: the state's handler, the ground snap, the idle tracks, the upper body
// following the body unless a weapon / the carry is busy, the two animation channels
void dispatch(const PlayerEnv& env, Player& p) {
    const RoutineRef& handler = stateHandlers[p.state];
    switch (handler.id()) {
    case RoutineId::PlayerInitState: initState(p); break;
    case RoutineId::PlayerPlayingState: playingState(env, p); break;
    case RoutineId::PlayerDeadState: deadState(env, p); break;
    case RoutineId::PlayerScriptedUpdate: scriptedUpdate(p); break;
    default: {
        char msg[48];
        std::snprintf(msg, sizeof msg, "player state %u (0x%06x)", unsigned(p.state), handler.pointerValue());
        notPorted(kPlayerArea, msg);
    }
    }
    if (transitionRequest().state == kTransitionNone) {
        const int ground = actorGround(env, p) - p.collisionBox->maxY;
        if (p.onPlatform == 0 && p.airborne == 0) {
            const int d = ground - p.pos.y.whole;
            if (d > 0 && d <= snapReach(p)) p.pos.y = int32_t(uint32_t(ground) << 16);
        }
    }
    stepIdleTracks(p);
    if (weaponBusy(p) == 0) {
        p.upper.anim = p.body.anim;
        p.upper.key.frame = p.body.key.frame;
    }
    const uint8_t mix = uint8_t((p.carryFlags | p.armFlags) & kArmMixMask);
    if (p.upper.anim == p.upper.prevAnim) {
        advanceUpperAnimation(p, mix);
    } else {
        startUpperAnimation(p, mix, PlayerAnim(p.upper.anim), p.upper.key.frame);
        p.upper.prevAnim = p.upper.anim;
    }
    if (p.body.anim != p.body.prevAnim)
        startBodyAnimation(p, PlayerAnim(p.body.anim), p.body.key.frame);
    else
        advanceBodyAnimation(p);
}

// exe 0x574e3d: pulled toward the point (a whirlpool / a push-back trap): the pull fades to 0 at its radius; a step
// that would overshoot snaps onto the point
void pullTowardPoint(Player& p) {
    const int16_t me[3] = {p.pos.x.whole, p.pos.y.whole, p.pos.z.whole};
    const int16_t point[3] = {p.pullX, 0, p.pullZ};
    const int ang = angleOf(me, point); // exe 0x5e3c00
    const int dist = planarDistance16(me, point); // exe 0x5e3b40
    const int radius = p.pullRadius, strength = p.pullStrength;
    if (radius == 0) notPorted(kPlayerArea, "0x574e3d pull radius +0xea = 0 (the exe divides by zero)");
    const int f = ((radius - dist) * strength) / radius;
    const int32_t vx = int32_t(uint32_t(fixedSin(ang)) * uint32_t(f));
    const int ax = vx < 0 ? -(vx >> 16) : (vx >> 16);
    if (dist < ax && f > 0)
        p.pos.x = int32_t(uint32_t(int32_t(p.pullX)) << 16);
    else
        p.pos.x = int32_t(uint32_t(p.pos.x) + uint32_t(vx));
    const int32_t vz = int32_t(uint32_t(fixedCos(ang)) * uint32_t(f));
    const int az = vz < 0 ? -(vz >> 16) : (vz >> 16);
    if (dist < az && f > 0)
        p.pos.z = int32_t(uint32_t(int32_t(p.pullZ)) << 16);
    else
        p.pos.z = int32_t(uint32_t(p.pos.z) + uint32_t(vz));
}

// exe 0x574d30's fall start: the ground too far below the feet -> the fall (on the skates the skate fall)
void startFall(Player& p) {
    if (p.sub() == PlayerSub::Lift) {
        p.interactionTarget.clear();
        p.carryFlags = 0;
    }
    if (p.sub() == PlayerSub::KnockDown) {
        if (p.immunity < 0) {
            p.immunity = kImmuneBlinking;
            p.immunityTicks = kFallImmuneTicks;
        }
    } else if (p.immunity != 0) {
        p.immunity = kImmuneBlinking;
        p.immunityTicks = 0;
    }
    if (p.skating != 0) {
        p.subState = uint8_t(PlayerSub::SkateFall);
        p.enterStep(0);
    } else {
        enableFiring(p);
        p.resetStep(JumpStep::Peak);
        p.subState = uint8_t(p.velocity.z <= kFallRunSpeed ? PlayerSub::RunningJump : PlayerSub::Jump);
    }
    p.velocity.y = 0;
    p.pushVelocity.y = 0;
    p.airborne = kAirborneFalling;
    selectIdleTables(p, kIdleCalm, kIdleCalm);
    if (eventState().active != 0) eventAbort(); // exe 0x543aa0
}

} // namespace

void playerInputAndDispatch(const PlayerEnv& env) {
    Player& p = player();
    if (!p.live()) return;
    playerReadPad(p);
    dispatch(env, p);
}

void playerUpdate(const PlayerEnv& env) {
    Player& p = player();
    if (p.onPlatform != 0)
        actorCarry(p, *p.platform); // exe 0x414f20: follow the platform actor stood on
    else if (p.wasOnPlatform != 0 && p.airborne == 0 && p.wasAirborne == 0)
        turnOrStopVelocities(p, 0, 0);
    if (p.hitPoints >= 0) roomTriggers(env, p);
    auto&& box = *p.collisionBox;
    if (uint8_t(p.wadeDepth) == 0 || p.hitPoints < 0) {
        if (box.maxY != 0) {
            box.maxY = 0;
            p.stageAnimMarker = 0;
        }
    } else { // standing in water: the collision box sinks
        box.maxY = wrap16(box.maxY - kSinkStep);
        if (int(box.maxY) < -int(p.wadeDepth)) box.maxY = wrap16(-int(p.wadeDepth));
        if (box.maxY < kSinkMax) box.maxY = kSinkMax;
        if (box.maxY < kSinkLegs)
            p.hiddenBones = kLegsHidden;
        else if (box.maxY < kSinkFeet)
            p.hiddenBones = kFeetHidden;
    }
    if (p.pulled != 0) pullTowardPoint(p);
    p.wallHit = actorPushOut(env, p) | p.actorHit;
    if ((p.wallHit & kWallOnSides) && p.sub() == PlayerSub::Skate && p.step > uint8_t(SkateStep::EngineStart) &&
        p.step < uint8_t(SkateStep::Stop)) {
        p.velocity.z = kSkateWallSpeed;
        const int8_t b = p.burden;
        if (b > 0) p.velocity.z = wrap16(kSkateWallSpeed - ((b * kSkateWallSpeed) >> 6));
    }
    const int ground = actorGround(env, p) - p.collisionBox->maxY;
    const PlayerState state = p.playerState();
    if ((state == PlayerState::Playing || state == PlayerState::Dead) && p.hitPoints >= 0 && p.onPlatform == 0 &&
        p.airborne == 0) {
        const int d = ground - p.pos.y.whole;
        if (snapReach(p) < d) { // the ground too far below: the fall
            if (p.wasAirborne == 0) startFall(p);
        } else if (d > 0) {
            p.pos.y = int32_t(uint32_t(ground) << 16); // snapped down onto the ground
        }
    }
    easeLeanAndHead(p);
    playerPose(p); // exe 0x5e1f90 + 0x5e25b0
    updateStatusEffects(p);
    // exe 0x5f20f0: the shadow (output only)
    if (p.lockedActor != nullptr && actorLayoutOf(*p.lockedActor).flags == 0) { // the lock-on target was freed
        p.clearLockOnTarget();
        p.lockedActor = 0;
    }
    const int16_t pos[3] = {p.pos.x.whole, wrap16(p.pos.y.whole - kHitHeight), p.pos.z.whole};
    p.hitBox.previousX = p.prevPos.x.whole;
    p.hitBox.result = 0;
    p.hitBox.previousZ = p.prevPos.z.whole;
    // exe 0x5e4120: the player's hit-list entry (game/hit_list): its result box (read back by exe 0x57a910 next tick),
    // the contact as the attacker's motion, the height level, the player's group; a touch of 4. Not while immune
    if ((p.invulnerable | uint8_t(p.immunity)) == 0)
        hitListSubmit(pos, p.hitBox,
                      hitOwner(p.slot) | hitGroup(kHitGroupPlayer) | hitHeightLevel(p.hitHeightLevel) |
                          kHitCompareHeights | kHitContactMotion | hitRadius(kPlayerHitRadius),
                      kHitAttackBump | kPlayerTouchDamage);
}

void playerTickReset() {
    Player& p = player();
    p.status &= kStatusTickResetKeep;
    p.hiddenBones = 0; // (+0x296, the animation pause, stays)
    p.pulled = 0;
    p.wadeDepth = 0;
    p.slippery = 0;
    p.inWater = 0;
    p.burden = 0;
}

void playerRestore() {
    Player& p = player();
    p.hitPoints = p.maxHitPoints;
    p.clearStatusWords();
    for (size_t i = 0; i < p.statusTicks.size(); ++i) p.statusTicks[i] = 0;
}

void playerRespawn() {
    playerRestore();
    Player& p = player();
    const TransitionRequest& r = transitionRequest();
    const int32_t at[3] = {wholeToFixed16(r.destination.x), wholeToFixed16(r.destination.y),
                           wholeToFixed16(r.destination.z)};
    p.pos.x = at[0];
    p.pos.y = at[1];
    p.pos.z = at[2];
    p.prevPos = p.pos;
    p.rotation.y = r.destination.yaw;
    p.prevRotation.y = r.destination.yaw;
    p.airborne = 0;
    p.wasAirborne = 0;
    p.enterState(PlayerState::Init);
}

} // namespace od::port
