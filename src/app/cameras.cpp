#include "app/play_session.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "game/actor_pools.hpp"
#include "game/camera_drive.hpp"
#include "game/camera_script.hpp"
#include "game/camera_state.hpp"
#include "game/event_state.hpp"
#include "game/game_state.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"
#include "game/state_array.hpp"
#include "game/transition_state.hpp"

#include "game/player/precise_pose.hpp"
#include "game/player/player_state.hpp"

namespace od::app {

void PlayerView::orbitBehind() {
    const float b = float(od::port::player().rotation.y) * 6.2831853f / 4096.0f;
    orbitYaw = b + 3.14159265f; // body forward is (-sin b, -cos b)
}

bool PlayerView::lockedOn() const { return od::port::player().lockedActor != nullptr; }

void PlayerView::orbitArrival() {
    if (!arrivalPending || od::port::gameState().gameMode() != od::port::GameMode::Field || gameCameraActive()) return;
    arrivalPending = false;
    usingGameCamera = false;
    orbitBehind();
    const od::port::CameraPose& pose = od::port::cameraState().pose;
    orbitPitch = std::clamp(float(pose.pitch) * 6.2831853f / 4096.0f, -0.35f, 1.25f);
    SDL_Log("orbit camera after the arrival: yaw %d pitch %d (game yaw %d), distance %d height %d",
            int(std::lround(orbitYaw * 4096.0f / 6.2831853f)) & 0xfff, int(pose.pitch), int(pose.yaw) & 0xfff,
            int(pose.distance), int(pose.height));
}

void PlayerView::orbit(float dx, float dy) {
    if (lockCamActive()) return; // locked on: the camera is the lock's (lockCamera), mouse / right stick ignored
    if (dx != 0.0f || dy != 0.0f) manualOrbitAge = 0.0f; // the skate chase waits skateTune.manualHold after this
    orbitYaw += dx;
    orbitPitch = std::clamp(orbitPitch + dy, -0.35f, 1.25f);
}

uint16_t PlayerView::steer(uint16_t ps1, float lx, float ly) {
    if (!modern) return ps1;
    const float mag = std::sqrt(lx * lx + ly * ly);
    ps1 &= uint16_t(~0xf0u); // the direction is decided here
    // the game drives the player (a door / exit transition 0xa6d2e0 walking it with the replay pad, a script, the
    // player's script-driven bit +0xc2 & 0x80): no steering, the body yaw stays the game's
    od::port::Player& pl = od::port::player();
    if (od::port::transitionRequest().state != 0 || (od::port::scriptState().flags & od::port::kScriptRunning) ||
        (pl.inputMode & od::port::kInputOff))
        return ps1;
    if (fireFacing(ps1, lx, ly)) return ps1; // the buster firing: face the camera, strafe (app/fire_facing)
    if (mag < 0.2f) return ps1;
    if (skating()) {
        // jet skates (sub-states 3 / 9) steer like a vehicle. With the game's button masks (+0x120 / +0x122 =
        // L1 / R1, +0x124 / +0x126 = d-pad left / right; FUN_00574860 tail, game/player/player_input) the TURN is the analog X
        // +0x118 (or L1 / R1 forcing it to +-0x7f): FUN_00571950 turns by ((|x| - 0x38)^2 >> 7) + 1, full = the
        // L1 / R1 rate. The d-pad left / right bits only LEAN (+0xf2 = 0x40 / 0x80 / 0xc0 accelerating / cruising /
        // braking, FUN_005765c0) and do not touch +0x118, so both go: the stick X as is, plus the d-pad bit once
        // it is pushed well over (brake + full turn = the game's tight leaning turn).
        steerStickX = uint8_t(std::clamp(int(std::lround(lx * 127.0f)) + 0x80, 0, 0xff));
        if (lx <= -0.6f) ps1 |= 0x80;
        else if (lx >= 0.6f) ps1 |= 0x20;
        // accelerate / brake are the d-pad up / down bits (+0x11c / +0x11e; the analog Y does not set them:
        // skatecam log vz stayed -0x340 with the stick up). The camera chases behind the body (skateCamera).
        if (ly < -0.5f) ps1 |= 0x10;
        if (ly > 0.5f) ps1 |= 0x40;
        return ps1;
    }
    // desired world direction (x, z) from the camera's forward / right
    const float fx = std::sin(orbitYaw), fz = std::cos(orbitYaw);
    const float dx = fx * -ly + fz * lx, dz = fz * -ly - fx * lx;
    const int want = int(std::lround(std::atan2(-dx, -dz) * 4096.0f / 6.2831853f)) & 0xfff;
    const int body = pl.rotation.y;
    const int diff = ((want - body + 0x800) & 0xfff) - 0x800;
    const uint8_t sub = pl.subState;
    const bool locomotion = pl.playerState() == od::port::PlayerState::Playing &&
                            (sub == 0 || sub == 1 || sub == 2 || sub == 5 || sub == 6);
    if (lockedOn()) {
        // locked on (FUN_005780e0 turns +0x2a toward the target): the body yaw is the game's and the stick is the
        // body-relative d-pad (8 ways: up = toward the target, sideways = strafe around it), as in the game. The
        // lock camera sits behind the player on the player -> target axis, so body-relative = screen-relative.
        const int stickAngle = int(std::lround(std::atan2(lx, -ly) * 4096.0f / 6.2831853f)); // 0 up, +0x400 right
        const int o = ((stickAngle + 0x100) & 0xfff) >> 9; // 0 ahead, 2 / 6 the sides, 4 behind
        // diff +0x400 = the body's right (verified: classic r2+right moves the locked player that way, ST13)
        static constexpr uint16_t kDir8[8] = {0x10, 0x10 | 0x20, 0x20, 0x20 | 0x40, 0x40, 0x40 | 0x80, 0x80, 0x80 | 0x10};
        return uint16_t(ps1 | kDir8[o]);
    }
    if (locomotion) {
        const int step = std::clamp(diff, -0x180, 0x180);
        pl.rotation.y = int16_t((body + step) & 0xfff);
        return uint16_t(ps1 | 0x10); // run forward
    }
    // other states (air, ledge, skating ...): body-relative d-pad
    const int q = ((diff + 0x200) & 0xfff) >> 10; // 0 ahead, 1 right, 2 behind, 3 left
    static constexpr uint16_t kDir[4] = {0x10, 0x20, 0x40, 0x80};
    return uint16_t(ps1 | kDir[q]);
}

bool PlayerView::gameCameraActive() const {
    return od::port::cameraState().mode() != od::port::CameraMode::Follow || od::port::eventState().active != 0;
}

bool PlayerView::orbitCamera(const RoomMesh& mesh, od::Camera& cam, float alpha, float dt) {
    lockCamShown = false;
    od::port::cameraSetDrive({}); // set again below while this camera is the one on screen
    if (!play || !modern) return false;
    if (gameCameraActive()) {
        usingGameCamera = true;
        lockBlend = 0.0f;
        return false;
    }
    if (usingGameCamera && !arrivalPending) { // back from the game camera: continue from its yaw / pitch
        usingGameCamera = false;
        orbitYaw = camCur.yaw;
        orbitPitch = std::clamp(-camCur.pitch, -0.35f, 1.25f);
    }
    const float scale = float(1 << mesh.shift);
    float t[3];
    for (int i = 0; i < 3; ++i) t[i] = posPrev[i] + (posCur[i] - posPrev[i]) * alpha;
    const float height = float(od::port::cameraState().pose.height), dist = float(od::port::cameraState().pose.distance);
    const float tx = t[0], ty = -(t[1] - height), tz = t[2];
    // lock-on: ease the orbit yaw onto the player -> target axis and the elevation to kLockElevation
    // (time constant kLockTau: ~95 % after 3 tau = 0.24 s, the ease-in), independent of the frame rate
    constexpr float kLockTau = 0.08f, kLockElevation = 0.32f, kLockSmooth = 0.09f;
    const float k = 1.0f - std::exp(-std::max(dt, 0.0f) / kLockTau);
    const bool locked = lockCamActive() && lockTarget != 0;
    float g[3]; // the target, interpolated like the player (the last one's place while the view eases out)
    for (int i = 0; i < 3; ++i) g[i] = tgtPrev[i] + (tgtCur[i] - tgtPrev[i]) * alpha;
    if (locked) {
        lockAimValid = true;
        const float dx = g[0] - t[0], dz = g[2] - t[2];
        // critically damped (smooth start, no overshoot): ~0.25 s to settle from a standstill
        if (dx * dx + dz * dz > 1.0f)
            orbitYaw += smoothDamp(0.0f, wrapPi(std::atan2(dx, dz) - orbitYaw), lockYawVel, kLockSmooth, dt);
        orbitPitch += smoothDamp(0.0f, kLockElevation - orbitPitch, lockPitchVel, kLockSmooth, dt);
        lockCamShown = true;
    }
    if (!locked) lockYawVel = lockPitchVel = 0.0f; // the next lock starts from rest
    lockBlend += ((locked ? 1.0f : 0.0f) - lockBlend) * k;
    if (!locked && lockBlend < 1e-3f) {
        lockBlend = 0.0f;
        lockAimValid = false;
    }
    skateCamera(locked, dt);
    cam.roll = skateRoll;
    // pulled in front of the first opaque room surface between the player and the eye
    const float from[3] = {tx * scale, ty * scale, tz * scale};
    auto placeEye = [&](float yaw, float d, float* eye) {
        const float fx = std::sin(yaw) * std::cos(orbitPitch), fy = -std::sin(orbitPitch),
                    fz = std::cos(yaw) * std::cos(orbitPitch);
        const float ray[3] = {-fx * d * scale, -fy * d * scale, -fz * d * scale};
        const float keep = std::max(0.0f, rayHit(mesh, from, ray) - 24.0f / d); // margin: 24 game units before the wall
        for (int i = 0; i < 3; ++i) eye[i] = from[i] + ray[i] * keep;
    };
    // lock-on framing: the player's drawn body, then the target's (BodyBox corners; feet + pivot when not drawn).
    // A target taken since the last built tick is measured now, from the same builders the frame draws with
    if (locked && targetBodyOf != lockTarget) {
        std::vector<od::DynamicObject> objects = liveActors(mesh);
        liveScene(mesh, nullptr, &objects, nullptr);
        measureBodies(mesh, objects);
    }
    std::array<std::array<float, 3>, 16> pts;
    int npts = 0;
    auto addBody = [&](float x, float feet, float z, const BodyBox& b) { // feet: render y (up)
        if (!b.valid) {
            pts[size_t(npts++)] = {x, feet, z};
            pts[size_t(npts++)] = {x, feet + height * scale, z};
            return;
        }
        for (int c = 0; c < 8; ++c)
            pts[size_t(npts++)] = {x + (c & 1 ? b.hi : b.lo)[0], feet + (c & 2 ? b.hi : b.lo)[1],
                                   z + (c & 4 ? b.hi : b.lo)[2]};
    };
    addBody(tx * scale, -t[1] * scale, tz * scale, playerBody);
    const int nplayer = npts;
    if (lockAimValid) addBody(g[0] * scale, -g[1] * scale, g[2] * scale, targetBody);
    // the points as vertical angles from the eye along the view yaw (screen y = tan(angle - pitch), exact for any
    // sideways offset); in frame = within kFrameEdge of the edge
    auto angles = [&](const float* eye, float yaw, int last, float& lo, float& hi) {
        lo = 1e9f;
        hi = -1e9f;
        for (int i = 0; i < last; ++i) {
            const float h = (pts[size_t(i)][0] - eye[0]) * std::sin(yaw) + (pts[size_t(i)][2] - eye[2]) * std::cos(yaw);
            const float a = std::atan2(pts[size_t(i)][1] - eye[1], h);
            lo = std::min(lo, a);
            hi = std::max(hi, a);
        }
    };
    const float edge = std::atan(kFrameEdge * kTanHalfFov); // the largest angle off the view axis in frame
    // the view pitch from an eye: the orbit's, eased (lockBlend) toward the midpoint of the player's pivot and the
    // target's (same pivot height above its feet); locked on, turned to frame both bodies (centred on them when they
    // are taller than the frame: the FOV takes the rest); easing out of a lock, the player's body alone
    auto framePitch = [&](const float* eye, float yaw) {
        float pitch = -orbitPitch;
        if (!lockAimValid || lockBlend <= 0.0f) return pitch;
        const float aim[3] = {(tx + g[0]) * 0.5f * scale, (ty - (g[1] - height)) * 0.5f * scale,
                              (tz + g[2]) * 0.5f * scale};
        const float ax = aim[0] - eye[0], ay = aim[1] - eye[1], az = aim[2] - eye[2];
        const float aimPitch = std::clamp(std::atan2(ay, std::hypot(ax, az)), -0.9f, 0.6f);
        pitch += (aimPitch - pitch) * lockBlend;
        float lo, hi;
        angles(eye, yaw, locked ? npts : nplayer, lo, hi);
        return hi - lo <= 2.0f * edge ? std::clamp(pitch, hi - edge, lo + edge) : (lo + hi) * 0.5f;
    };
    // tan(half the vertical FOV) that puts every point within kFrameEdge of the edges (horizontally at the window's
    // aspect); a point behind the eye fits no FOV
    constexpr float kNoFit = 1e9f;
    struct Need {
        float h = 0.0f, v = 0.0f;
        float both() const { return std::max(h, v); }
    };
    const float aspect = std::max(viewAspect, 0.1f);
    auto need = [&](const float* eye, float yaw, float pitch) {
        const float cp = std::cos(pitch), sp = std::sin(pitch), cy = std::cos(yaw), sy = std::sin(yaw);
        const float f[3] = {sy * cp, sp, cy * cp}, r[3] = {cy, 0.0f, -sy}, u[3] = {-sy * sp, cp, -cy * sp};
        Need n;
        for (int i = 0; i < npts; ++i) {
            const float d[3] = {pts[size_t(i)][0] - eye[0], pts[size_t(i)][1] - eye[1], pts[size_t(i)][2] - eye[2]};
            const float z = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
            if (z <= 1e-3f * std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])) return Need{kNoFit, kNoFit};
            n.h = std::max(n.h, std::abs(d[0] * r[0] + d[2] * r[2]) / z / (kFrameEdge * aspect));
            n.v = std::max(n.v, std::abs(d[0] * u[0] + d[1] * u[1] + d[2] * u[2]) / z / kFrameEdge);
        }
        return n;
    };
    float want = 1.0f; // the eye distance (x dist) at which player + target fit (the eye backs off to frame them)
    if (locked) {
        float eye[3];
        auto at = [&](float yaw, float s) {
            placeEye(yaw, dist * s, eye);
            return need(eye, yaw, framePitch(eye, yaw));
        };
        // sideways (a strafing / passing target, the eye pulled in by a wall): the yaw turns toward the player ->
        // target axis just far enough (on the axis the target is centred)
        const float dx = g[0] - t[0], dz = g[2] - t[2];
        if (dx * dx + dz * dz > 1.0f && at(orbitYaw, lockDist).h > kTanHalfFov) {
            const float err = wrapPi(std::atan2(dx, dz) - orbitYaw);
            float lo = 0.0f, hi = 1.0f;
            for (int it = 0; it < 12; ++it) {
                const float mid = (lo + hi) * 0.5f;
                (at(orbitYaw + err * mid, lockDist).h > kTanHalfFov ? lo : hi) = mid;
            }
            orbitYaw += err * hi;
        }
        for (float s : {1.0f, 1.25f, 1.5f, 2.0f, 2.5f}) {
            want = s;
            if (at(orbitYaw, s).both() <= kTanHalfFov) break;
        }
    }
    lockDist += smoothDamp(lockDist, want, lockDistVel, want > lockDist ? 0.15f : 0.5f, dt);
    cam.yaw = orbitYaw;
    placeEye(cam.yaw, dist * lockDist, cam.eye);
    cam.pitch = framePitch(cam.eye, cam.yaw);
    // what the yaw / distance / pitch could not fit (the eye still easing back, a wall behind the player, a body
    // taller than the frame) widens the FOV on this frame; it eases back to the game's once they fit
    constexpr float kMaxFovTan = 2.75f; // 140 degrees vertically
    const float fit = locked ? std::min(need(cam.eye, cam.yaw, cam.pitch).both(), kMaxFovTan) : kTanHalfFov;
    lockFovTan = std::max({fit, kTanHalfFov, lockFovTan + (kTanHalfFov - lockFovTan) * k});
    cam.fovY = 2.0f * std::atan(lockFovTan) + skateFov;
    if (!arrivalPending) { // one camera: the view on screen drives the game camera's next tick (camera_drive)
        const float ex = cam.eye[0] - from[0], ey = cam.eye[1] - from[1], ez = cam.eye[2] - from[2];
        od::port::cameraSetDrive({true, int(std::lround(cam.yaw * 4096.0f / 6.2831853f)) & 0xfff,
                                  int(std::lround(-cam.pitch * 4096.0f / 6.2831853f)),
                                  int(std::lround(std::sqrt(ex * ex + ey * ey + ez * ez) / scale))});
    }
    return true;
}

float PlayerView::rayHit(const RoomMesh& mesh, const float* o, const float* d) {
    float best = 1.0f;
    const size_t n = mesh.camTris.size() / 3 * 3;
    for (size_t i = 0; i < n; i += 3) {
        const float* a = mesh.camTris[i].data();
        const float* b = mesh.camTris[i + 1].data();
        const float* c = mesh.camTris[i + 2].data();
        const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const float p[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
        const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
        if (std::abs(det) < 1e-6f) continue;
        const float inv = 1.0f / det, s[3] = {o[0] - a[0], o[1] - a[1], o[2] - a[2]};
        const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
        if (u < 0.0f || u > 1.0f) continue;
        const float q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
        const float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
        if (v < 0.0f || u + v > 1.0f) continue;
        const float t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
        if (t > 0.0f && t < best) best = t;
    }
    return best;
}

void PlayerView::trackPosition(bool snapNow) {
    od::port::Player& pl = od::port::player();
    const int32_t at[3] = {pl.pos.x, pl.pos.y, pl.pos.z};
    for (int i = 0; i < 3; ++i) {
        posPrev[i] = snapNow ? float(at[i]) / 65536.0f : posCur[i];
        posCur[i] = float(at[i]) / 65536.0f;
    }
    // the lock-on target (+0x174) the same way; a new target starts without a blend
    const uint32_t t = od::port::slotIdentityWord(pl.lockedActor);
    const bool fresh = snapNow || t != lockTarget;
    lockTarget = t;
    if (!t) return;
    const od::port::Actor& target = od::port::actorAt(t);
    const int32_t tat[3] = {target.pos.x, target.pos.y, target.pos.z};
    for (int i = 0; i < 3; ++i) {
        const float v = float(tat[i]) / 65536.0f;
        tgtPrev[i] = fresh ? v : tgtCur[i];
        tgtCur[i] = v;
    }
}

bool PlayerView::objectOfActor(uint32_t address, uint32_t id) {
    const uint32_t kind = id >> 24, index = id & 0xffffffu;
    auto slotOf = [&](uint32_t base, uint32_t count, uint32_t stride, uint32_t& slot) {
        if (address < base || address >= base + count * stride || (address - base) % stride) return false;
        slot = (address - base) / stride;
        return true;
    };
    uint32_t slot = 0;
    if (slotOf(od::port::kAnimatedPool, 24, 0x16c, slot)) return kind == od::kKindActor && (index & 0xff) == slot;
    if (kind != od::kKindPoolModel) return false;
    if (slotOf(od::port::kStaticPool, 64, 0xcc, slot)) return index == (0x60u << 8 | slot);
    if (slotOf(od::port::kProjectilePool, 32, 0xdc, slot)) return index == (0x40u << 8 | slot);
    if (slotOf(od::port::kShotPool, 16, 0x100, slot)) return index == (0x10u << 8 | slot);
    return false;
}

void PlayerView::measureBodies(const RoomMesh& mesh, const std::vector<od::DynamicObject>& objects) {
    const float scale = float(1 << mesh.shift);
    auto measure = [&](const float* at, auto&& mine) { // at: the tick's position (game axes)
        BodyBox b;
        const float p[3] = {at[0] * scale, -at[1] * scale, at[2] * scale};
        for (const od::DynamicObject& o : objects) {
            if (!mine(o.id)) continue;
            for (const od::RenderVertex& v : o.tris)
                for (int c = 0; c < 3; ++c) {
                    const float d = v.pos[c] - p[c];
                    b.lo[c] = b.valid ? std::min(b.lo[c], d) : d;
                    b.hi[c] = b.valid ? std::max(b.hi[c], d) : d;
                    if (c == 2) b.valid = true;
                }
        }
        return b;
    };
    if (std::any_of(objects.begin(), objects.end(),
                    [](const od::DynamicObject& o) { return o.id == od::objectId(od::kKindPlayer, 0); }))
        playerBody = measure(posCur, [](uint32_t id) { return id == od::objectId(od::kKindPlayer, 0); });
    targetBodyOf = lockTarget;
    targetBody = lockTarget ? measure(tgtCur, [&](uint32_t id) { return objectOfActor(lockTarget, id); }) : BodyBox{};
}

float PlayerView::smoothDamp(float current, float target, float& vel, float smoothTime, float dt) {
    if (dt <= 0.0f) return 0.0f;
    const float omega = 2.0f / smoothTime, x = omega * dt;
    const float e = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    const float change = current - target, temp = (vel + omega * change) * dt;
    vel = (vel - omega * temp) * e;
    return (target + (change + temp) * e) - current;
}

bool PlayerView::skating() const {
    const od::port::Player& pl = od::port::player();
    return pl.playerState() == od::port::PlayerState::Playing &&
           (pl.sub() == od::port::PlayerSub::Skate || pl.sub() == od::port::PlayerSub::SkateFall);
}

void PlayerView::skateCamera(bool locked, float dt) {
    dt = std::max(dt, 0.0f);
    manualOrbitAge += dt;
    const bool on = skating() && !locked;
    float rollT = 0.0f, fovT = 0.0f;
    const od::port::Player& pl = od::port::player();
    if (on) {
        if (manualOrbitAge > skateTune.manualHold) {
            const float behind = float(pl.rotation.y) * 6.2831853f / 4096.0f + 3.14159265f; // see orbitBehind
            orbitYaw += smoothDamp(0.0f, wrapPi(behind - orbitYaw), skateYawVel, skateTune.chaseSmooth, dt);
        } else {
            skateYawVel = 0.0f;
        }
        const int lean = pl.lean; // FUN_00571d90's smoothed lean (the one the model shows)
        rollT = std::clamp(float(lean) / kSkateLeanFull, -1.0f, 1.0f) * skateTune.rollDeg * 0.0174533f;
        const float speed = float(-pl.velocity.z);
        fovT = std::clamp((speed - 640.0f) / (1024.0f - 640.0f), 0.0f, 1.0f) * skateTune.fovDeg * 0.0174533f;
    } else {
        skateYawVel = 0.0f;
    }
    const float k = 1.0f - std::exp(-dt / 0.12f); // roll / FOV ease (also back to 0 off the skates)
    skateRoll += (rollT - skateRoll) * k;
    skateFov += (fovT - skateFov) * k;
    if (logSkateCam && on && ++skateLogFrames % 4 == 0)
        SDL_Log("skatecam sub %u.%u body %d orbit %d f2 %d 104 %d 106 %d 4a %d vz %d roll %.2f fov %.2f manual %.2f",
                unsigned(pl.subState), unsigned(pl.step), int(pl.rotation.y) & 0xfff,
                int(std::lround(orbitYaw * 4096.0f / 6.2831853f)) & 0xfff, int(pl.directionOffset.y),
                int(pl.lean), int(pl.leanTarget), int(pl.spin.y), int(pl.velocity.z),
                skateRoll * 57.2958f, skateFov * 57.2958f, std::min(manualOrbitAge, 99.0f));
}

bool PlayerView::lockCamActive() const { return play && modern && lockedOn() && !gameCameraActive(); }

float PlayerView::wrapPi(float a) {
    a = std::fmod(a + 3.14159265f, 6.2831853f);
    return (a < 0.0f ? a + 6.2831853f : a) - 3.14159265f;
}

// FUN_00512260(k): slot s = 0xa6bc10 + k * 0x20 {+0 counter c, +4 ticks n, +8 from a[3], +0x14 to b[3]}, active while
// 0xa6bba7 has bit 8 << (k * 4); curve (0xa6bba7 >> (k * 4)) & 7 into 0x89b6b0 (0x512360 linear, 0x512460 cosine,
// 0x512510 ease-out, 0x512590 ease-in). Tick t = c - 1 gave a (t = 0), the curve at t / n (0 < t < n - 1), then b
// at t = n - 1. The exe evaluates the curves on 8-bit factors (ease-in / out: ((t << 8) / n)^2 >> 8 times (b - a) >> 8)
// and jumps the last 2 / n at once, so a pan advances by 1, 2 or 3 uneven units per tick (ST52: pitch +1.5, +1.5,
// +0.5, +1.5, ...) -- the stutter. For drawing, the same curve continuous over u = t / (n - 1): same first and last
// tick, no quantization. The logic keeps the exe's values.
bool PlayerView::smoothCameraSlot(uint32_t k, float out[3]) const {
    const od::port::ScriptCamera& sc = od::port::scriptCamera();
    if (!(sc.easing & (8u << (k * 4)))) return false;
    const od::port::CameraEase e = sc.eases[k];
    const int32_t c = e.tick, n = e.length;
    if (c < 1 || n < 2 || c > n - 1) return false;
    const double u = double(c - 1) / double(n - 1);
    const od::port::CameraEaseCurve curve = od::port::scriptCameraEaseCurve(k);
    for (uint32_t i = 0; i < 3; ++i) {
        const double a = double(e.from[i]), b = double(e.to[i]);
        double v;
        switch (curve) {
        case od::port::CameraEaseCurve::Linear: v = a + (b - a) * u; break;
        case od::port::CameraEaseCurve::Cosine: v = (a + b) * 0.5 + (a - b) * 0.5 * std::cos(3.14159265358979 * u); break;
        case od::port::CameraEaseCurve::Out: v = b + (a - b) * (1.0 - u) * (1.0 - u); break;
        case od::port::CameraEaseCurve::In: v = a + (b - a) * u * u; break;
        default: return false; // the logic stops on an unknown curve (FUN_00512260 notPorted)
        }
        out[i] = float(v / 65536.0);
    }
    return true;
}

PlayerView::CamPose PlayerView::readCamera() const {
    CamPose c;
    constexpr float kAngle = 6.2831853f / 4096.0f;
    const od::port::CameraState& camera = od::port::cameraState();
    c.eye[0] = float(camera.eye.x);
    c.eye[1] = -float(camera.eye.y);
    c.eye[2] = float(camera.eye.z);
    // the angles Maybe_SetupCameraTransform 0x40de70 built the view matrix from (0xa6bd1c = -pitch,
    // 0xa6bd1e = yaw, 12-bit): every mode (follow, script camera FUN_00512600, mode-2 blend) goes through it.
    // The follow fields +0x98 / +0x9a are NOT the view in mode 1: the script camera takes its angles from
    // 0xa6bbf8.. and leaves them stale (ST08 walk-off: eye ahead of the player, view turned away from it)
    auto s12 = [](int v) { return ((v & 0xfff) ^ 0x800) - 0x800; };
    c.yaw = float(s12(camera.viewAngles.y)) * kAngle;
    c.pitch = float(s12(camera.viewAngles.x)) * kAngle;
    c.shift = od::port::stage().coordinateShift;
    // precise poses (F1): the script camera (mode 1, FUN_00512600) eases its target / angles / distance in 16.16, but
    // the transform takes their integer parts (>> 16, 12-bit angles): a slow move then advances 0 or 1 unit per tick
    // and the eased pans stutter. For drawing, the same transform on the unrounded values; the logic keeps the exe's.
    if (od::precise::enabled() && camera.mode() == od::port::CameraMode::Scripted) {
        const od::port::ScriptCamera& sc = od::port::scriptCamera();
        auto fx = [](const od::port::StateArray<int32_t, 3>& v, size_t i) { return float(v[i]) / 65536.0f; };
        // the target P (slot 0 writes it while the anchor is absolute) and the angles / Q (slot 1: lookAt picks Q)
        float pt[3] = {fx(sc.point, 0), fx(sc.point, 1), fx(sc.point, 2)}, sl[3];
        if (sc.anchor() == od::port::ScriptCameraAnchor::Absolute && smoothCameraSlot(0, sl)) std::copy(sl, sl + 3, pt);
        const bool lookAt = sc.lookAt != 0;
        float ang[3] = {fx(sc.angles, 0), fx(sc.angles, 1), fx(sc.angles, 2)},
              q[3] = {fx(sc.lookAtPoint, 0), fx(sc.lookAtPoint, 1), fx(sc.lookAtPoint, 2)};
        if (smoothCameraSlot(1, sl)) std::copy(sl, sl + 3, lookAt ? q : ang);
        const float px = pt[0], py = pt[1] - float(camera.shake >> 8), pz = pt[2];
        float dist, yaw, pitch; // FUN_0040de70's distance and angles (4096 units, pitch as passed)
        if (lookAt) { // Maybe_SetupLookAtCameraTransform 0x40e090: from P towards Q = 0xa6bbe8..
            const float qx = q[0], qy = q[1], qz = q[2];
            dist = std::sqrt((px - qx) * (px - qx) + (pz - qz) * (pz - qz));
            yaw = std::atan2(qx - px, qz - pz) * 2048.0f / 3.14159265f + 2048.0f;
            pitch = -std::atan2(qy - py, dist) * 2048.0f / 3.14159265f;
        } else {
            dist = ang[2];
            yaw = ang[0];
            pitch = ang[1];
        }
        const float y = yaw * kAngle, p = pitch * kAngle; // eye = target - dist * (sin y cos p, sin p, cos y cos p)
        c.eye[0] = px - dist * std::sin(y) * std::cos(p);
        c.eye[1] = -(py - dist * std::sin(p));
        c.eye[2] = pz - dist * std::cos(y) * std::cos(p);
        c.yaw = std::remainder(y, 6.2831853f);
        c.pitch = std::remainder(-p, 6.2831853f); // 0xa6bd1c = -pitch
    }
    return c;
}

bool PlayerView::gameCamera(const RoomMesh& mesh, od::Camera& cam, float alpha) const {
    if (!play) return false;
    const float scale = float(1 << mesh.shift);
    const CamPose& a = camPrev.shift == camCur.shift ? camPrev : camCur;
    const CamPose& b = camCur;
    for (int i = 0; i < 3; ++i) cam.eye[i] = (a.eye[i] + (b.eye[i] - a.eye[i]) * alpha) * scale;
    float dyaw = std::remainder(b.yaw - a.yaw, 6.2831853f); // shortest way round
    cam.yaw = a.yaw + dyaw * alpha;
    cam.pitch = a.pitch + (b.pitch - a.pitch) * alpha;
    // GTE projection: H = 0x180 (FUN_0040ba00 at boot) on a 320x240 screen (mode table 0x88c3cc,
    // offsets 160 / 120): vertical FOV 2 * atan(120 / 384); widescreen keeps it and widens horizontally
    cam.fovY = 2.0f * std::atan(120.0f / 384.0f);
    cam.roll = 0.0f;
    return true;
}

} // namespace od::app
