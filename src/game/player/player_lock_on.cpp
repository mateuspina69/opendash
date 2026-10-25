// The player's lock-on (see player_lock_on.hpp).
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/player/player_lock_on.hpp"

#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera_state.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/projection.hpp"

namespace od::port {

namespace {

constexpr int32_t kAimHeight = 0x800000;  // the aim's origin above the feet (16.16)
constexpr int kLockPitchStep = 0x40;
constexpr int kLockYawStep = 0x80;        // airborne: kLockYawStepAirborne
constexpr int kLockYawStepAirborne = 0x40;
constexpr int kAimPitchLimit = 0x200;
constexpr uint16_t kScreenTop = 0x10, kScreenBottom = 0xc0; // the hit point on screen within these: the camera holds

// A controller that can be locked on stands for an actor (its spawn parameters' dword: the actor's address)
// (a tail of the controller's slot)
struct LockProxy {
    explicit LockProxy(const ControllerActor& actor) : LockProxy(const_cast<ControllerActor&>(actor)) {}
    explicit LockProxy(ControllerActor& actor) : actor(actor.params, 0) {}
    BytesField<uint32_t, 4> actor;
};

// the actor of a lock / soft target slot (the exe's switch of 0x5780e0 / 0x578500): the slot of its pool, a controller's
// actor; false for the player's and the shots' classes
bool targetActor(uint32_t slot, ActorHeader*& out) {
    switch (actorSlotClass(slot)) {
    case kAnimatedClass: out = &animatedPool()[slot - kAnimatedClass]; return true;
    case kProjectileClass: out = &projectilePool()[slot - kProjectileClass]; return true;
    case kStaticClass: out = &staticPool()[slot - kStaticClass]; return true;
    case kEffectClass: out = &effectPool()[slot - kEffectClass]; return true;
    case kControllerClass:
        out = slotByIdentityWord<ActorHeader>(LockProxy(controllerPool()[slot - kControllerClass]).actor);
        return true;
    default: return false;
    }
}

} // namespace

void dropLockOn(Player& p) {
    ActorHeader* actor = nullptr;
    if (targetActor(p.softTarget, actor)) {
        p.softTargetActor = actor;
    } else {
        p.clearLockOnTarget();
        p.softTargetActor = nullptr;
    }
    p.lockRequest &= kLockKeepAim;
    p.lockTarget = 0;
    p.lockedActor = nullptr;
}

void trackLockOn(Player& p) {
    ActorHeader* actor = nullptr;
    if (!targetActor(p.lockTarget, actor)) {
        p.clearLockOnTarget();
        p.softTargetActor = nullptr;
        p.lockedActor = nullptr;
        return;
    }
    p.lockedActor = actor;
    const int32_t from[3] = {p.pos.x, int32_t(uint32_t(int32_t(p.pos.y)) - uint32_t(kAimHeight)), p.pos.z};
    const ShortVec4& hit = p.lockAimPoint;
    const int32_t to[3] = {int32_t(uint32_t(int32_t(hit.x)) << 16), int32_t(uint32_t(int32_t(hit.y)) << 16),
                           int32_t(uint32_t(int32_t(hit.z)) << 16)};
    p.softTarget = 0;
    p.softTargetActor = 0;
    const int pitch = signedAngle(pitchOf(from, to) - kHalfTurn);
    p.rotation.x = int16_t(approachAngle(pitch, signedAngle(uint16_t(p.rotation.x)), kLockPitchStep));
    CameraState& camera = cameraState();
    bool cameraTurns = true;
    if ((p.lockFlags & kLockPitchOn) && p.airborne == 0) {
        int16_t screen[2] = {0, 0};
        const int16_t point[3] = {hit.x, hit.y, hit.z};
        projectOnScreen(point, screen); // exe 0x5f2dd0
        const uint16_t y = uint16_t(screen[1]);
        if (y < kScreenTop || y > kScreenBottom || p.pressedNow(PlayerButton::Lock))
            p.lockFlags &= uint8_t(~kLockPitchOn);
        else
            cameraTurns = false;
    }
    if (cameraTurns) camera.lookPitch = int16_t(approachAngle(pitch, camera.lookPitch, kLockPitchStep));
    const int16_t aim = p.rotation.x;
    const int absAim = aim < 0 ? -int(aim) : int(aim);
    if ((pitch == aim && pitch == camera.lookPitch) || absAim == kAimPitchLimit) p.lockFlags |= kLockPitchOn;
    const int yaw = signedAngle(angleOf(from[0], from[2], to[0], to[2]) - kHalfTurn);
    const int body = signedAngle(uint16_t(p.rotation.y));
    const int diff = signedAngle(yaw - body);
    const int limit = p.airborne != 0 ? kLockYawStepAirborne : kLockYawStep;
    uint8_t flags;
    if (diff == 0) {
        flags = uint8_t(p.lockFlags | kLockYawOn);
    } else {
        int v = yaw;
        if (diff > 0) {
            if (diff > limit) v = body + limit;
        } else if (diff < -limit) {
            v = body - limit;
        }
        p.rotation.y = int16_t(v);
        flags = yaw != v ? uint8_t(p.lockFlags & ~kLockYawOn) : uint8_t(p.lockFlags | kLockYawOn);
    }
    p.lockFlags = flags;
    if ((flags & (kLockPitchOn | kLockYawOn)) == (kLockPitchOn | kLockYawOn)) { // reached: the lock request fires
        const uint16_t request = p.lockRequest;
        if ((request & (kLockSpecial | kLockBuster)) && !(request & kLockFired)) {
            p.lockRequest = uint16_t(request | kLockFired);
            if (request & kLockSpecial) {
                if (specialEnergyReady(p)) p.setArmBits(kArmSpecialQueued);
            } else {
                p.setArmBits(kArmBusterQueued);
            }
        }
    }
    const int8_t ticks = p.lockSoundTicks;
    if (ticks >= 0) {
        p.lockSoundTicks = int8_t(ticks - 1);
        if (int8_t(ticks - 1) < 0) soundPlayAtPlayer(uint16_t(PlayerSound::LockOn)); // exe 0x5d53e0
    }
}

} // namespace od::port
