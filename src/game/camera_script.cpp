// The camera scripts (see camera_script.hpp).
#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/camera_script.hpp"

#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/fixed_point.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_params.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"
#include "game/state_array.hpp"

namespace od::port {

namespace {

constexpr const char* kCameraScriptArea = "camera script port"; // the area of its port-gap errors

// The ops of a camera script (the word's high byte; the low bytes and the words after it are the op's arguments)
enum class CameraOp : uint8_t {
    WaitTotalTicks = 0x00,  // until the sequence's ticks reach the low 24 bits
    WaitStepId = 0x01,      // until the step id is byte 2 and its ticks the low 16 bits
    WaitStepSub = 0x02,     // until the step sub is byte 2 and the step's ticks the low 16 bits
    AimByAngles = 0x03,     // the view by the angles (yaw / pitch / distance)
    AimAtPoint = 0x04,      // the view aimed at the look-at point
    AnchorAbsolute = 0x05,  // the point is absolute ...
    AnchorActor = 0x06,     // ... the offset from a script actor ...
    AnchorPlayer = 0x07,    // ... from the player
    SetPoint = 0x10,        // the point (absolute anchor)
    SetActorOffset = 0x11,  // the offset from the script actor of byte 2
    SetPlayerOffset = 0x12, // the offset from the player
    SetAngles = 0x13,
    SetLookAtPoint = 0x14,
    SetPointSpeed = 0x15,   // the point's / offset's speed (its interpolation stopped)
    SetAngleSpeed = 0x16,   // the angles' speed (the angles' interpolation stopped)
    SetLookAtSpeed = 0x17,  // the look-at point's speed (the same)
    EasePoint = 0x18,       // the point / offset to the arguments over the low 16 bits' ticks, easing (byte 2 & 7)
    EaseAngles = 0x19,      // the angles ...
    EaseLookAtPoint = 0x1a, // ... the look-at point
    SetRoll = 0x1b,
    CoordinateShift0 = 0x20, // the coordinate shift 0 / 1 / 2
    CoordinateShift1 = 0x21,
    CoordinateShift2 = 0x22,
    TogglePause = 0x30,     // the camera script and the step list held / released (exe 0x511690); the tick ends
    AllowSkip = 0x31,       // the block's skip allowed / prevented; the tick ends
    PreventSkip = 0x32,
    SpawnActor = 0x40,      // a script actor spawned from a record (exe 0x511740)
    FreeActor = 0x41,       // ... freed (exe 0x511760)
    PlacePlayer = 0x42,     // the player's 16.16 position and yaw
    RestoreDepthOffset = 0x50, // the room record's depth offset / depth range / fog distance / fog colour back
    RestoreDepthRange = 0x51,
    RestoreFogDistance = 0x52,
    RestoreFogColour = 0x53,
    SetDepthOffset = 0x54,  // ... set from the low bytes
    SetDepthRange = 0x55,
    SetFogDistance = 0x56,
    SetFogColour = 0x57,
    End = 0xff,             // the script is over (the pointer stays on it)
};
constexpr uint32_t kOpShift = 24;
constexpr uint32_t kNoCameraScript = 0xffffffffu; // a script whose first word is this does nothing
constexpr uint32_t kOpWordSize = 4, kOpWithArgSize = 8, kOpWithPointSize = 16, kPlacePlayerSize = 20;
constexpr size_t kPointEase = 0, kAngleEase = 1;

// One op of a camera script (exe data): its word, its three argument words (a point / angles / speeds; a record; the
// roll) and the yaw of PlacePlayer
struct CameraScriptOp : DataRecord {
    static constexpr uint32_t kBytes = 0x12;
    uint32_t word = 0; // +0x0
    std::array<uint32_t, 3> args{}; // +0x4
    uint16_t placeYaw = 0; // +0x10
    template <class R> static CameraScriptOp load(const R& r) {
        CameraScriptOp v;
        v.word = r.template read<uint32_t>(0x0);
        for (size_t i = 0; i < v.args.size(); ++i) v.args[i] = r.template read<uint32_t>(0x4 + i * sizeof(uint32_t));
        v.placeYaw = r.template read<uint16_t>(0x10);
        return v;
    }
};

// the easing curves (exe table 0x89b6b0 by the easing kind: the named routines, engine/routines.csv)
const DataTable<RoutineTable<8>> easingRoutines{TableId::CameraEasingRoutines};

// (the interpolations' arithmetic wraps like the exe's 32-bit imul / add / sub: game/fixed_point mul32 / add32 / sub32)

// exe 0x512360: linear, the whole and fraction halves interpolated apart
void easeLinear(const CameraEase& e, int32_t out[3]) {
    for (size_t i = 0; i < 3; ++i) {
        const int32_t t = e.tick, n = e.length, a = e.from[i], b = e.to[i];
        const int32_t hi = add32(mul32(b >> 16, t), mul32(a >> 16, n - t)) / n;
        const int32_t lo =
            add32(mul32(int32_t(uint32_t(b) & 0xffff), t), mul32(int32_t(uint32_t(a) & 0xffff), n - t)) / n;
        out[i] = add32(int32_t(uint32_t(hi) << 16), lo);
    }
}
// exe 0x512460: cosine (half a turn of the cosine over the length)
void easeCosine(const CameraEase& e, int32_t out[3]) {
    const int32_t c = fixedCos(int(uint32_t(int32_t(uint32_t(int32_t(e.tick)) << 11) / int32_t(e.length)) & 0xfff));
    for (size_t i = 0; i < 3; ++i) {
        const int32_t a = e.from[i], b = e.to[i];
        if (a == b) {
            out[i] = b;
            continue;
        }
        const int32_t d = (a >> 1) - (b >> 1);
        out[i] = add32(add32(mul32(d / 4096, c), a >> 1), b >> 1); // (d + (d >> 31 & 0xfff)) >> 12 == d / 4096
    }
}
// exe 0x512510: ease-out (the square of the remaining fraction)
void easeOut(const CameraEase& e, int32_t out[3]) {
    int32_t f = mul32(sub32(e.tick, e.length), 0x100) / int32_t(e.length);
    f = mul32(f, f) >> 8;
    for (size_t i = 0; i < 3; ++i) {
        const int32_t a = e.from[i], b = e.to[i];
        out[i] = a == b ? b : add32(mul32(sub32(a, b) >> 8, f), b);
    }
}
// exe 0x512590: ease-in (the square of the elapsed fraction)
void easeIn(const CameraEase& e, int32_t out[3]) {
    int32_t f = int32_t(uint32_t(int32_t(e.tick)) << 8) / int32_t(e.length);
    f = mul32(f, f) >> 8;
    for (size_t i = 0; i < 3; ++i) {
        const int32_t a = e.from[i], b = e.to[i];
        out[i] = a == b ? b : add32(mul32(sub32(b, a) >> 8, f), a);
    }
}

// exe 0x512260(block, k): one tick of interpolation k (kPointEase: into the point / offset by the anchor; kAngleEase:
// into the angles / look-at point): the start values on its first tick, the end values (and the interpolation
// cleared, its easing bits off) on its last, else the easing curve's values
void easeTick(size_t k) {
    ScriptCamera& sc = scriptCamera();
    auto&& e = sc.eases[k];
    const uint32_t nibble = uint32_t(k) * 4;
    int32_t v[3];
    if (e.tick == 0) {
        for (size_t i = 0; i < 3; ++i) v[i] = e.from[i];
        e.tick = 1;
    } else if (e.tick == e.length - 1) {
        for (size_t i = 0; i < 3; ++i) v[i] = e.to[i];
        e.tick = 0;
        e.length = 0;
        for (size_t i = 0; i < 3; ++i) e.from[i] = 0;
        for (size_t i = 0; i < 3; ++i) e.to[i] = 0;
        sc.easing &= ~(0xfu << nibble);
    } else {
        const RoutineRef& routine = easingRoutines[(sc.easing >> nibble) & kEaseKindMask];
        switch (routine.id()) {
        case RoutineId::CameraEaseLinear: easeLinear(e, v); break;
        case RoutineId::CameraEaseCosine: easeCosine(e, v); break;
        case RoutineId::CameraEaseOut: easeOut(e, v); break;
        case RoutineId::CameraEaseIn: easeIn(e, v); break;
        default: notPortedAt(kCameraScriptArea, "the script camera's easing (exe 0x512260)", routine.pointerValue());
        }
        ++e.tick;
    }
    StateArray<int32_t, 3>& dst = k != kPointEase ? (sc.lookAt != 0 ? sc.lookAtPoint : sc.angles)
                                                : (sc.anchor() != ScriptCameraAnchor::Absolute ? sc.offset : sc.point);
    for (size_t i = 0; i < 3; ++i) dst[i] = v[i];
}

// the three lanes of `to` = the op's arguments
void setFromArgs(StateArray<int32_t, 3>& to, const CameraScriptOp& op) {
    for (size_t i = 0; i < 3; ++i) to[i] = int32_t(op.args[i]);
}

} // namespace

CameraEaseCurve scriptCameraEaseCurve(size_t k) {
    const uint32_t nibble = uint32_t(k) * 4;
    switch (easingRoutines[(scriptCamera().easing >> nibble) & kEaseKindMask].id()) {
    case RoutineId::CameraEaseLinear: return CameraEaseCurve::Linear;
    case RoutineId::CameraEaseCosine: return CameraEaseCurve::Cosine;
    case RoutineId::CameraEaseOut: return CameraEaseCurve::Out;
    case RoutineId::CameraEaseIn: return CameraEaseCurve::In;
    default: return CameraEaseCurve::Unknown;
    }
}

void scriptCameraStart(uint32_t script) {

    ScriptCamera& sc = scriptCamera();
    CameraState& cam = cameraState();
    sc.point[1] = int32_t(uint32_t(int32_t(cam.target.y)) << 16);
    sc.point[2] = int32_t(uint32_t(int32_t(cam.target.z)) << 16);
    sc.angles[kScriptYaw] = int32_t((uint32_t(uint16_t(cam.pose.yaw)) & kAngleMask) << 16);
    sc.point[0] = int32_t(uint32_t(int32_t(cam.target.x)) << 16);
    sc.angles[kScriptPitch] = int32_t((uint32_t(uint16_t(cam.pose.pitch)) & kAngleMask) << 16);
    sc.angles[kScriptDistance] = int32_t((uint32_t(uint16_t(cam.pose.distance)) & kAngleMask) << 16);
    sc.roll = 0;
    for (size_t i = 0; i < 3; ++i) sc.pointSpeed[i] = 0;
    for (size_t i = 0; i < 3; ++i) sc.angleSpeed[i] = 0;
    for (size_t i = 0; i < 3; ++i) sc.lookAtSpeed[i] = 0;
    sc.clearSettings();
    for (size_t k = 0; k < sc.eases.size(); ++k) {
        auto&& e = sc.eases[k];
        e.length = 0;
        e.tick = 0;
        for (size_t i = 0; i < 3; ++i) e.to[i] = 0;
        for (size_t i = 0; i < 3; ++i) e.from[i] = 0;
    }
    sc.script = script;
    cam.setMode(CameraMode::Scripted);
    cam.shake = 0;
    cam.unreadDA = 0;
    cam.shakeAmplitude = 0;
}

void scriptCameraEnd() {
    CameraState& cam = cameraState();
    cam.setMode(CameraMode::Follow);
    cam.shake = 0;
    cam.unreadDA = 0;
    cam.shakeAmplitude = 0;
}

void scriptCameraTick() {
    ScriptState& s = scriptState();
    if (s.flags & kScriptCameraPaused) return;
    ScriptCamera& sc = scriptCamera();
    uint32_t p = sc.script;
    auto advance = [&](uint32_t size) {
        p += size;
        sc.script = p;
    };
    if (dataRecord<CameraScriptOp>(p, CameraScriptOp::kBytes).word != kNoCameraScript) {
        for (;;) {
            const CameraScriptOp op = dataRecord<CameraScriptOp>(p, CameraScriptOp::kBytes);
            const uint32_t w = op.word;
            const CameraOp code = CameraOp(w >> kOpShift);
            if (code == CameraOp::End) break;
            switch (code) {
            case CameraOp::WaitTotalTicks:
                if (uint32_t(int32_t(s.totalTicks)) < (w & 0xffffff)) goto moved;
                advance(kOpWordSize);
                continue;
            case CameraOp::WaitStepId:
                if (s.stepId != uint8_t(w >> 16) || uint32_t(int32_t(s.idTicks)) != (w & 0xffff)) goto moved;
                advance(kOpWordSize);
                continue;
            case CameraOp::WaitStepSub:
                if (s.stepSub != uint8_t(w >> 16) || uint32_t(int32_t(s.stepTick)) != (w & 0xffff)) goto moved;
                advance(kOpWordSize);
                continue;
            case CameraOp::AimByAngles: sc.lookAt = 0; advance(kOpWordSize); continue;
            case CameraOp::AimAtPoint: sc.lookAt = 1; advance(kOpWordSize); continue;
            case CameraOp::AnchorAbsolute: sc.setAnchor(ScriptCameraAnchor::Absolute); advance(kOpWordSize); continue;
            case CameraOp::AnchorActor: sc.setAnchor(ScriptCameraAnchor::Actor); advance(kOpWordSize); continue;
            case CameraOp::AnchorPlayer: sc.setAnchor(ScriptCameraAnchor::Player); advance(kOpWordSize); continue;
            case CameraOp::SetPoint:
                sc.setAnchor(ScriptCameraAnchor::Absolute);
                setFromArgs(sc.point, op);
                advance(kOpWithPointSize);
                continue;
            case CameraOp::SetActorOffset:
            case CameraOp::SetPlayerOffset:
                if (code == CameraOp::SetActorOffset) {
                    sc.setAnchor(ScriptCameraAnchor::Actor);
                    sc.anchorActor = uint8_t(w >> 16);
                } else {
                    sc.setAnchor(ScriptCameraAnchor::Player);
                }
                setFromArgs(sc.offset, op);
                advance(kOpWithPointSize);
                continue;
            case CameraOp::SetAngles: setFromArgs(sc.angles, op); advance(kOpWithPointSize); continue;
            case CameraOp::SetLookAtPoint: setFromArgs(sc.lookAtPoint, op); advance(kOpWithPointSize); continue;
            case CameraOp::SetPointSpeed:
                sc.easing &= 0xf0u;
                setFromArgs(sc.pointSpeed, op);
                advance(kOpWithPointSize);
                continue;
            case CameraOp::SetAngleSpeed:
                sc.easing &= 0xfu;
                setFromArgs(sc.angleSpeed, op);
                advance(kOpWithPointSize);
                continue;
            case CameraOp::SetLookAtSpeed:
                sc.easing &= 0xfu;
                setFromArgs(sc.lookAtSpeed, op);
                advance(kOpWithPointSize);
                continue;
            case CameraOp::EasePoint: {
                auto&& e = sc.eases[kPointEase];
                e.tick = 0;
                sc.easing = uint8_t((sc.easing & 0xf0u) | ((w >> 16) & kEaseKindMask) | kEasePoint);
                e.length = int32_t(w & 0xffff);
                StateArray<int32_t, 3>& from = sc.anchor() == ScriptCameraAnchor::Absolute ? sc.point : sc.offset;
                for (size_t i = 0; i < 3; ++i) e.from[i] = from[i];
                setFromArgs(e.to, op);
                for (size_t i = 3; i-- > 0;) sc.pointSpeed[i] = 0;
                advance(kOpWithPointSize);
                continue;
            }
            case CameraOp::EaseAngles:
            case CameraOp::EaseLookAtPoint: {
                auto&& e = sc.eases[kAngleEase];
                e.tick = 0;
                sc.easing = uint8_t((sc.easing & 0xfu) | ((w >> 12) & 0xf0u) | kEaseAngles);
                e.length = int32_t(w & 0xffff);
                const bool angles = code == CameraOp::EaseAngles;
                StateArray<int32_t, 3>& from = angles ? sc.angles : sc.lookAtPoint;
                for (size_t i = 0; i < 3; ++i) e.from[i] = from[i];
                setFromArgs(e.to, op);
                StateArray<int32_t, 3>& speed = angles ? sc.angleSpeed : sc.lookAtSpeed;
                for (size_t i = 3; i-- > 0;) speed[i] = 0;
                advance(kOpWithPointSize);
                continue;
            }
            case CameraOp::SetRoll: sc.roll = op.args[0]; advance(kOpWithArgSize); continue;
            case CameraOp::CoordinateShift0: setCoordinateShift(0); advance(kOpWordSize); continue;
            case CameraOp::CoordinateShift1: setCoordinateShift(1); advance(kOpWordSize); continue;
            case CameraOp::CoordinateShift2: setCoordinateShift(2); advance(kOpWordSize); continue;
            case CameraOp::TogglePause: s.flags ^= kScriptCameraPaused; advance(kOpWordSize); return;
            case CameraOp::AllowSkip: s.flags &= uint8_t(~kScriptUnskippable); advance(kOpWordSize); return;
            case CameraOp::PreventSkip: s.flags |= kScriptUnskippable; advance(kOpWordSize); return;
            case CameraOp::SpawnActor: scriptActorSpawnRecord(op.args[0]); advance(kOpWithArgSize); continue;
            case CameraOp::FreeActor: scriptActorFreeRecord(op.args[0]); advance(kOpWithArgSize); continue;
            case CameraOp::PlacePlayer: {
                Player& pl = player();
                pl.pos.x = int32_t(op.args[0]);
                pl.pos.y = int32_t(op.args[1]);
                pl.pos.z = int32_t(op.args[2]);
                pl.rotation.y = int16_t(op.placeYaw);
                advance(kPlacePlayerSize);
                continue;
            }
            case CameraOp::RestoreDepthOffset: {
                const RoomParams params = *roomDraw().params;
                stage().depthOffset = params.depthOffset;
                advance(kOpWordSize);
                continue;
            }
            case CameraOp::RestoreDepthRange:
                stage().depthRange = roomDraw().depthRanges[stage().coordinateShift];
                advance(kOpWordSize);
                continue;
            case CameraOp::RestoreFogDistance: {
                const RoomParams params = *roomDraw().params;
                setFogDistance(int32_t(uint32_t(params.fogDistance)));
                advance(kOpWordSize);
                continue;
            }
            case CameraOp::RestoreFogColour: {
                const RoomParams params = *roomDraw().params;
                setFogColour(params.fogColour[0], params.fogColour[1], params.fogColour[2]);
                advance(kOpWordSize);
                continue;
            }
            case CameraOp::SetDepthOffset: stage().depthOffset = uint16_t(w); advance(kOpWordSize); continue;
            case CameraOp::SetDepthRange: stage().depthRange = uint16_t(w); advance(kOpWordSize); continue;
            case CameraOp::SetFogDistance: setFogDistance(int32_t(w & 0xffff)); advance(kOpWordSize); continue;
            case CameraOp::SetFogColour:
                setFogColour(uint8_t(w), uint8_t(w >> 8), uint8_t(w >> 16));
                advance(kOpWordSize);
                continue;
            default: notPortedAt(kCameraScriptArea, "camera script op (exe 0x5118a0)", w >> kOpShift);
            }
        }
    }
moved:
    if ((sc.easing & kEasePoint) == 0) {
        StateArray<int32_t, 3>& point = sc.anchor() == ScriptCameraAnchor::Absolute ? sc.point : sc.offset;
        point[1] += int32_t(sc.pointSpeed[1]);
        point[0] += int32_t(sc.pointSpeed[0]);
        point[2] += int32_t(sc.pointSpeed[2]);
    } else {
        easeTick(kPointEase);
    }
    if (sc.easing & kEaseAngles) {
        easeTick(kAngleEase);
        return;
    }
    if (sc.lookAt == 0) {
        sc.angles[1] += int32_t(sc.angleSpeed[1]);
        sc.angles[0] += int32_t(sc.angleSpeed[0]);
        sc.angles[2] += int32_t(sc.angleSpeed[2]);
    } else {
        sc.lookAtPoint[1] += int32_t(sc.lookAtSpeed[1]);
        sc.lookAtPoint[0] += int32_t(sc.lookAtSpeed[0]);
        sc.lookAtPoint[2] += int32_t(sc.lookAtSpeed[2]);
    }
}

} // namespace od::port
