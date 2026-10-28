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
#include "render/scene_pieces.hpp"

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
    // lock-on (OpenDash): the eye behind the player's pivot on the player -> target axis (critically damped, kLockSmooth)
    // at kLockElevation, looking at the midpoint of the player's pivot and the target's (same height above its feet);
    // a wall pulls the eye in. Both bodies (spheres; a boss's radius capped by kTargetCore) stay within the frame's
    // safe area: the eye backs off as far as they need (at once; back slowly, kBackSettle) and the view pitch is held
    // so that both are in (the player first); what still does not fit widens the FOV a little at most
    constexpr float kLockTau = 0.08f, kLockElevation = 0.32f, kLockSmooth = 0.09f, kFraming = 0.85f, kMaxBack = 3.0f,
                    kTargetCore = 4.0f, kBackSettle = 0.8f;
    const float k = 1.0f - std::exp(-std::max(dt, 0.0f) / kLockTau);
    const bool locked = lockCamActive() && lockTarget != 0;
    float g[3]; // the target, interpolated like the player (the last one's place while the view eases out)
    for (int i = 0; i < 3; ++i) g[i] = tgtPrev[i] + (tgtCur[i] - tgtPrev[i]) * alpha;
    if (locked && !lockAimValid) { // a new lock starts from the orbit's view
        lockYaw = orbitYaw;
        lockYawVel = 0.0f;
        lockCentreValid = false;
    }
    if (locked) {
        lockAimValid = true;
        lockCamShown = true;
    }
    lockBlend += ((locked ? 1.0f : 0.0f) - lockBlend) * k;
    if (!locked && lockBlend < 1e-3f) {
        lockBlend = 0.0f;
        lockAimValid = false;
    }
    skateCamera(locked, dt);
    cam.roll = skateRoll;
    // pulled in front of the first opaque room surface between `from` and the eye
    const float from[3] = {tx * scale, ty * scale, tz * scale};
    auto placeEye = [&](const float* o, float yaw, float elevation, float d, float* eye) {
        const float fx = std::sin(yaw) * std::cos(elevation), fy = -std::sin(elevation),
                    fz = std::cos(yaw) * std::cos(elevation);
        const float ray[3] = {-fx * d, -fy * d, -fz * d};
        const float keep = std::max(0.0f, rayHit(mesh, o, ray) - 24.0f * scale / d); // 24 game units before the wall
        for (int i = 0; i < 3; ++i) eye[i] = o[i] + ray[i] * keep;
        return keep;
    };
    const float follow = dist * scale;
    // the orbit view (the eye behind the player's pivot)
    lockDist += smoothDamp(lockDist, 1.0f, lockDistVel, 0.5f, dt);
    float orbitEye[3];
    placeEye(from, orbitYaw, orbitPitch, follow * (locked ? 1.0f : lockDist), orbitEye);
    float eye[3] = {orbitEye[0], orbitEye[1], orbitEye[2]}, yaw = orbitYaw, pitch = -orbitPitch;
    float fovTan = kTanHalfFov;
    if (lockAimValid) {
        // a target taken since the last built tick is measured now, from the same builders the frame draws with
        if (locked && targetBodyOf != lockTarget) {
            std::vector<od::DynamicObject> objects = liveActors(mesh);
            liveScene(mesh, nullptr, &objects, nullptr);
            measureBodies(mesh, objects);
        }
        // the two spheres (the measured bodies; the pivot height when a body is not drawn)
        auto sphere = [&](const float* at, const BodyBox& b, float* c, float& r) {
            const float base[3] = {at[0] * scale, -at[1] * scale, at[2] * scale};
            if (!b.valid) {
                const float h = height * scale * 0.5f;
                c[0] = base[0];
                c[1] = base[1] + h;
                c[2] = base[2];
                r = h;
                return;
            }
            r = 0.0f;
            for (int i = 0; i < 3; ++i) {
                c[i] = base[i] + (b.lo[i] + b.hi[i]) * 0.5f;
                r = std::max(r, (b.hi[i] - b.lo[i]) * 0.5f);
            }
        };
        float cp[3], ct[3], rp, rt;
        sphere(t, playerBody, cp, rp);
        sphere(g, targetBody, ct, rt);
        rt = std::min(rt, rp * kTargetCore);
        // the aim: the midpoint of the player's pivot and the target's (the same height above its feet)
        lockCentre[0] = (from[0] + g[0] * scale) * 0.5f;
        lockCentre[1] = (from[1] - (g[1] - height) * scale) * 0.5f;
        lockCentre[2] = (from[2] + g[2] * scale) * 0.5f;
        if (!lockCentreValid) {
            lockCentreValid = true;
            lockBack = follow;
        }
        // the yaw: onto the player -> target axis
        const float dx = g[0] - t[0], dz = g[2] - t[2];
        if (dx * dx + dz * dz > 1.0f) lockYaw += smoothDamp(0.0f, wrapPi(std::atan2(dx, dz) - lockYaw), lockYawVel, kLockSmooth, dt);
        lockYaw = wrapPi(lockYaw);
        // a sphere's vertical extent from an eye, as angles along the view yaw (screen y = tan(angle - pitch)), and its
        // horizontal half extent off the view yaw (radians)
        struct Extent {
            float lo, hi, side;
        };
        auto extent = [&](const float* e, float viewYaw, const float* c, float r) {
            const float d[3] = {c[0] - e[0], c[1] - e[1], c[2] - e[2]};
            const float len3 = std::max(std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), 1e-3f);
            const float h = d[0] * std::sin(viewYaw) + d[2] * std::cos(viewYaw);
            const float a = std::atan2(d[1], h), half = std::asin(std::min(1.0f, r / len3));
            const float off = std::abs(wrapPi(std::atan2(d[0], d[2]) - viewYaw));
            return Extent{a - half, a + half, h > 0.0f ? off + half : 3.14159265f};
        };
        auto lookPitch = [&](const float* e, float viewYaw) {
            const float d[3] = {lockCentre[0] - e[0], lockCentre[1] - e[1], lockCentre[2] - e[2]};
            return std::clamp(std::atan2(d[1], d[0] * std::sin(viewYaw) + d[2] * std::cos(viewYaw)), -0.9f, 0.6f);
        };
        const float aspect = std::max(viewAspect, 0.1f);
        const float vEdge = std::atan(kFrameEdge * kTanHalfFov), hEdge = std::atan(kFrameEdge * kTanHalfFov * aspect);
        // both within kFraming of the frame's safe area once the pitch holds them, the eye at d behind the pivot
        auto fitsAt = [&](float d) {
            const float e[3] = {from[0] - std::sin(lockYaw) * std::cos(kLockElevation) * d,
                                from[1] + std::sin(kLockElevation) * d,
                                from[2] - std::cos(lockYaw) * std::cos(kLockElevation) * d};
            const Extent ep = extent(e, lockYaw, cp, rp), et = extent(e, lockYaw, ct, rt);
            return std::max(ep.hi, et.hi) - std::min(ep.lo, et.lo) <= 2.0f * vEdge * kFraming &&
                   std::max(ep.side, et.side) <= hEdge * kFraming;
        };
        float need = follow;
        if (!fitsAt(follow)) {
            float lo = follow, hi = follow * kMaxBack;
            for (int it = 0; it < 10; ++it) {
                const float mid = (lo + hi) * 0.5f;
                (fitsAt(mid) ? hi : lo) = mid;
            }
            need = hi;
        }
        lockBack = need > lockBack ? need : lockBack + (need - lockBack) * std::min(1.0f, dt / kBackSettle);
        const float viewYaw = lockYaw, elevation = kLockElevation;
        float lockEye[3];
        placeEye(from, viewYaw, elevation, lockBack, lockEye);
        // the view: toward the aim; the frame's safe area holds both (the player first when both do not fit)
        float pv = lookPitch(lockEye, viewYaw);
        const Extent ep = extent(lockEye, viewYaw, cp, rp), et = extent(lockEye, viewYaw, ct, rt);
        const float lo = std::min(ep.lo, et.lo), hi = std::max(ep.hi, et.hi);
        if (hi - lo <= 2.0f * vEdge) pv = std::clamp(pv, hi - vEdge, lo + vEdge);
        else pv = ep.hi - ep.lo <= 2.0f * vEdge ? std::clamp(std::clamp(pv, hi - vEdge, lo + vEdge), ep.hi - vEdge, ep.lo + vEdge)
                                               : (ep.lo + ep.hi) * 0.5f;
        // still not whole (the eye pulled in by a wall, a boss larger than the frame): the FOV widens a little at most
        const float over = std::max({ep.hi - pv, pv - ep.lo, et.hi - pv, pv - et.lo});
        const float sideOver = std::max(ep.side, et.side);
        const float needTan = std::max(std::tan(std::min(over, 1.4f)) / kFrameEdge,
                                       std::tan(std::min(sideOver, 1.4f)) / (kFrameEdge * aspect));
        fovTan = std::clamp(needTan, kTanHalfFov, 0.36f);
        if (locked) { // the orbit continues from the lock's view on release
            orbitYaw = wrapPi(viewYaw);
            orbitPitch = std::clamp(elevation, -0.35f, 1.25f);
        }
        const float b = lockBlend;
        for (int i = 0; i < 3; ++i) eye[i] += (lockEye[i] - eye[i]) * b;
        yaw = orbitYaw + wrapPi(viewYaw - orbitYaw) * b;
        pitch += (pv - pitch) * b;
    } else {
        lockCentreValid = false;
    }
    cam.yaw = yaw;
    std::copy(eye, eye + 3, cam.eye);
    cam.pitch = pitch;
    lockFovTan = std::max(fovTan, lockFovTan + (kTanHalfFov - lockFovTan) * k);
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
        const float t = od::segmentHitsTriangle(o, d, mesh.camTris[i].data(), mesh.camTris[i + 1].data(),
                                                mesh.camTris[i + 2].data());
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

bool PlayerView::freeView(const RoomMesh& mesh) const {
    return play && roomStage == mesh.stage && roomSub == mesh.sub && !gameCameraActive() &&
           !(od::port::scriptState().flags & od::port::kScriptRunning);
}

void PlayerView::pivotAt(const RoomMesh& mesh, float out[3]) const {
    const float scale = float(1 << mesh.shift);
    out[0] = posCur[0] * scale;
    out[1] = -posCur[1] * scale; // render y up
    out[2] = posCur[2] * scale;
}

void PlayerView::seeThroughBodies(const RoomMesh& mesh, float alpha, bool drawn,
                                  std::vector<od::SeeThroughBody>& out) const {
    out.clear();
    if (!freeView(mesh)) return; // cutscenes show the scenery whole
    const float scale = float(1 << mesh.shift);
    auto add = [&](const float* prev, const float* cur, const BodyBox& b) {
        if (!b.valid) return;
        od::SeeThroughBody body;
        for (int c = 0; c < 3; ++c) {
            float at = (prev[c] + (cur[c] - prev[c]) * alpha) * scale;
            if (c == 1) at = -at; // render y up
            if (drawn) at += renderOffset[c];
            body.lo[c] = at + b.lo[c];
            body.hi[c] = at + b.hi[c];
        }
        out.push_back(body);
    };
    const od::port::Player& pl = od::port::player();
    if (pl.flags & pl.runFlags & od::port::kActorDrawn) add(posPrev, posCur, playerBody);
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
    // the kept box grows at once (the framing never lets a limb out) and shrinks by kBodyShrink a tick: an animation's
    // poses no longer move the framing (and with it the camera) every tick
    auto settle = [](BodyBox& kept, const BodyBox& now) {
        if (!now.valid || !kept.valid) {
            kept = now;
            return;
        }
        for (int c = 0; c < 3; ++c) {
            kept.lo[c] = std::min(now.lo[c], kept.lo[c] + (now.lo[c] - kept.lo[c]) * kBodyShrink);
            kept.hi[c] = std::max(now.hi[c], kept.hi[c] + (now.hi[c] - kept.hi[c]) * kBodyShrink);
        }
    };
    if (std::any_of(objects.begin(), objects.end(),
                    [](const od::DynamicObject& o) { return o.id == od::objectId(od::kKindPlayer, 0); }))
        settle(playerBody, measure(posCur, [](uint32_t id) { return id == od::objectId(od::kKindPlayer, 0); }));
    if (targetBodyOf != lockTarget) targetBody = BodyBox{}; // a new target starts from its own pose
    targetBodyOf = lockTarget;
    if (lockTarget) settle(targetBody, measure(tgtCur, [&](uint32_t id) { return objectOfActor(lockTarget, id); }));
    else targetBody = BodyBox{};
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
