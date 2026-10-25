// The Flutter's travel map (see travel.hpp): script 7, its map icons and the Flutter's dock; one function per exe
// function, every write keeps the exe's width.
#include "game/travel.hpp"
#include "game/travel_state.hpp"

#include <cstdint>

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/display.hpp"
#include "game/game_state.hpp"
#include "game/message.hpp"
#include "game/message_state.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_exits.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/room_scripts.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::travel_state;

namespace od::port {

using namespace travel_state;

namespace {

constexpr const char* kTravelPort = "travel port"; // the area of its port-gap errors

// ---- script 7 (exe 0x517f30, states [0x89f92c]): the travel map --------------------------------------------------------
// +4 of the script block
enum class TravelState : uint8_t {
    SetUp = 0,  // exe 0x517f50: the map, the icons, the camera
    Choose = 1, // exe 0x5181f0: the icon steered until a destination is picked, then the fade out
    Leave = 2,  // exe 0x5182d0: the request to the destination, the block's end
};
// +5 of Choose
enum class TravelChooseStep : uint8_t { Steer = 0, Fly = 1 };
// +5 of Leave
enum class TravelLeaveStep : uint8_t { Sweep = 0, Request = 1, WaitArrival = 2 };

TravelMap& travelMap() {
    return scriptState().part<TravelMap>();
}
constexpr uint8_t kTravelOverDestination = 0x01; // the icon is over a destination
constexpr uint8_t kTravelNoFlight = 0x02;        // the pick leaves without the flight
constexpr uint8_t kTravelSecondIcon = 0x80;      // the map shows the second ship
constexpr uint8_t kTravelPickFlags = kTravelOverDestination | kTravelNoFlight;

// A destination of a map (exe data, 0x10 bytes): its stage and room, its place on the map and the icon's heading there,
// the request's place
struct TravelDestination : DataRecord {
    static constexpr uint32_t kBytes = 0x10;
    uint8_t stage = 0, room = 0;
    int16_t mapX = 0, mapZ = 0;
    uint16_t heading = 0;
    RoomPoint place;
    static TravelDestination load(const TableReader& r) {
        return {{}, r.u8(0), r.u8(1), r.s16(2), r.s16(4), r.u16(6), {r.s16(8), r.s16(0xa), r.s16(0xc), r.s16(0xe)}};
    }
};
// The maps by story (exe table 0x89f8e0: pointers to a map's destination list, pointers to the destinations, 0-ended)
struct TravelMaps : DataRecord {
    static constexpr size_t kMaps = 0x16, kMaxDestinations = 0x10;
    static constexpr uint32_t kBytes = kMaps * 4;
    std::array<bool, kMaps> present{};
    std::array<std::vector<TravelDestination>, kMaps> destinations;
    // the map's destinations (a map without a list: none)
    const std::vector<TravelDestination>& operator[](size_t map) const {
        if (map >= kMaps) tableIndexError(map, kMaps);
        return destinations[map];
    }
    static TravelMaps load(const TableReader& r) {
        TravelMaps t;
        for (size_t m = 0; m < kMaps; ++m) {
            const TableReader list = r.follow(m * 4, kMaxDestinations * 4);
            t.present[m] = !list.empty();
            for (size_t i = 0; i + 4 <= list.size() && list.u32(i) != 0; i += 4) {
                const TableReader d = list.follow(i, TravelDestination::kBytes);
                if (d.size() < TravelDestination::kBytes) break;
                t.destinations[m].push_back(TravelDestination::load(d));
            }
        }
        return t;
    }
};
const DataTable<TravelMaps> travelMaps{TableId::TravelMaps};
// the destinations' callbacks by their index (exe table 0x89f938: the named routines of engine/routines.csv)
const DataTable<RoutineTable<7>> travelPicks{TableId::TravelPicks};
// the map's destination i
const TravelDestination& destinationOf(uint8_t map, uint32_t i) {
    const std::vector<TravelDestination>& list = travelMaps[map];
    if (i >= list.size()) tableIndexError(i, list.size());
    return list[i];
}
// the map camera's angles (16.16)
constexpr int32_t kMapCameraYaw = 0, kMapCameraPitch = 0x4000000, kMapCameraDistance = 0xc00000;
// the story's maps
constexpr uint8_t kFirstMap = 1; // the map without the second ship
constexpr uint8_t kStoryWithLateMap = 7;
constexpr uint32_t kLateMapFlag = 0x582;  // story 7: the map 7 once set, else 6
constexpr uint8_t kNoMarker = 0xff;
constexpr uint8_t kLastIsland = 6;
constexpr uint8_t kFlyingIsland = 2;      // the Flutter starts in the air at this island (and at 1 on the first map)
constexpr uint8_t kArrivalFadeIn = 2;
// the icons' moves: the map's bounds, the stick's shift (square held: half speed), the second icon's box, the camera's
// margins and bounds
constexpr int16_t kIconMinX = -0xd0, kIconMaxX = 0xd0, kIconMinZ = -0xc8, kIconMaxZ = 0xb8;
constexpr int kSecondBoxX = 0x2b, kSecondBoxZ = 0x13;
constexpr int kMarginLeft = -0x70, kMarginTop = -0x38, kMarginRight = 0x70, kMarginBottom = 0x28;
constexpr int16_t kCameraMinX = -0x60, kCameraMaxX = 0x60, kCameraMinZ = -0x90, kCameraMaxZ = 0x90;
// a destination's reach (the third one's box is wider)
constexpr int kReachX = 0x10, kReachZ = 0xc, kWideReachX = 0x33, kWideReachZ = 0x1b;
constexpr uint32_t kWideDestination = 2;
// the hint's ticks
constexpr uint8_t kHintTicks = 0x1e;
constexpr int16_t kHintMessage = 0x20;
constexpr int8_t kNameWindow = 0, kHintWindow = 1;
// the destinations' callbacks' constants: the maps they act on and the requests they change
constexpr uint32_t kFirstDropFlag = 0x5c0; // the first pick on map 1 drops to ST49 sub 2 instead
constexpr uint32_t kLateStoryFlag = 0x580;

uint8_t mapOf() { // the map of the story (+0x38)
    const uint8_t st = gameState().story;
    if (st != kStoryWithLateMap) return st;
    return storyFlag(kLateMapFlag) ? 7 : 6;
}
uint8_t storyMarkerOf() { // the story's marker (+0x39; the jump table 0x51819c)
    const uint32_t s = gameState().story;
    switch (s) {
    case 1: return 1;
    case 2: case 3: return 3;
    case 4: return storyFlag(0x3d0) ? 2 : 3;
    case 5: case 9: return 4;
    case 7: return storyFlag(kLateMapFlag) ? 5 : 4;
    case 8: case 0x11: case 0x12: case 0x13: case 0x14: return 5;
    case 10: return storyFlag(0x3d1) ? 2 : 4;
    case 11: case 12: return 6;
    case 13: return storyFlag(0x3d2) ? 2 : 6;
    case 14: case 15: return 0;
    case 16: return storyFlag(0x3d3) ? 2 : 0;
    default: return kNoMarker; // 0, 6, > 0x14
    }
}

// +0xb: the flight
enum class TravelFlight : uint8_t { Rising = 0, Flying = 1, Landing = 2, Landed = 3 };
constexpr uint8_t kIconShadow = 0, kIconShip = 1;
constexpr uint8_t kFlutterVariant = 0, kSulphurVariant = 1;
constexpr uint8_t kIconType = 0x33;
constexpr uint16_t kFlightAltitude = 0x14;
// the icons' scales: the Flutter's shadow / ship, the Sulphur Bottom's
constexpr uint16_t kFlutterShadowScale = 0x1c, kFlutterShipScale = 0x20, kSulphurShadowScale = 0x38,
                   kSulphurShipScale = 0x48;
constexpr int kSulphurShipDy = -0xc, kSulphurShipDz = 0x18;
constexpr uint8_t kShadowSemiTransparency = 0x20; // exe 0x5e1a70(actor, 1, 0)
// the turn toward the heading: at least this step, half the difference past kTurnHalving
constexpr int kTurnStep = 0xc0, kTurnHalving = 0x180;
// the icons' collision boxes by variant (exe data, 12 bytes each) and the bank by the heading (pitch / roll by
// ((yaw + 0x80) >> 8) & 0xf)
constexpr uint32_t kIconBoxSize = 12;
struct IconBank : DataRecord {
    uint16_t pitch = 0;
    uint16_t roll = 0;
    static IconBank load(const TableReader& r) {
        IconBank rec;
        rec.pitch = r.u16(0);
        rec.roll = r.u16(2);
        return rec;
    }
};
const DataTable<RecordTable<IconBank, 0x10, 4>> iconBanks{TableId::TravelIconBanks};

// the script's icon k (its actor record k)
StaticActor& iconActor(int k) { return asStatic(*scriptState().actors[size_t(k)]); }

// exe 0x5183a0: the Flutter icon steered by the stick (square held: half speed), within the map
void steer(TravelMap& b) {
    b.prevIconX = b.iconX;
    b.prevIconZ = b.iconZ;
    PadState& pad = pad0();
    const uint8_t sh = uint8_t(((uint16_t(~pad.held) >> 15) | 4u));
    const int16_t dx = int16_t(int8_t(pad.dpadX >> sh));
    const int16_t dz = int16_t(-int(int8_t(pad.dpadY >> sh)));
    if (dx != 0 || dz != 0) b.idleTicks = 0;
    b.iconX = int16_t(uint16_t(b.iconX) + uint16_t(dx));
    b.iconZ = int16_t(uint16_t(b.iconZ) + uint16_t(dz));
    if (b.iconX < kIconMinX) b.iconX = kIconMinX;
    if (b.iconX > kIconMaxX) b.iconX = kIconMaxX;
    if (b.iconZ < kIconMinZ) b.iconZ = kIconMinZ;
    if (b.iconZ > kIconMaxZ) b.iconZ = kIconMaxZ;
    const int16_t mx = int16_t(uint16_t(b.prevIconX) - uint16_t(b.iconX));
    const int16_t mz = int16_t(uint16_t(b.prevIconZ) - uint16_t(b.iconZ));
    int ang = -1;
    if (mx != 0 || mz != 0) ang = atanAngle(float(mx), float(mz)) & kAngleMask;
    StaticActor& a = iconActor(0);
    a.pos.x.whole = b.iconX;
    a.pos.z.whole = b.iconZ;
    if (int16_t(ang) >= 0) classState<TravelIconFields>(a).heading = uint16_t(ang);
}
// exe 0x5184a0: the second icon's record follows its place
void secondIconFollow(TravelMap& b) {
    b.prevSecondX = b.secondX;
    b.prevSecondZ = b.secondZ;
    StaticActor& a = iconActor(1);
    a.pos.x.whole = b.secondX;
    a.pos.z.whole = b.secondZ;
}
// exe 0x5184d0: the Flutter icon kept out of the second icon's box
void keepApart(TravelMap& b) {
    int16_t x = b.iconX, z = b.iconZ;
    int ex = int(x) - int(b.secondX);
    if (ex < 0) ex = -ex;
    int ez = int(z) - int(b.secondZ);
    if (ez < 0) ez = -ez;
    const int ox = kSecondBoxX - ex, oz = kSecondBoxZ - ez;
    if (ox < 0 || oz < 0) return;
    StaticActor& a = iconActor(0);
    if (ox < oz) {
        if (b.prevIconX < b.secondX) x = int16_t(x - ox);
        else x = int16_t(x + ox);
        b.iconX = x;
        b.prevIconX = x;
    } else {
        if (b.prevIconZ < b.secondZ) z = int16_t(z - oz);
        else z = int16_t(z + oz);
        b.iconZ = z;
        b.prevIconZ = z;
    }
    a.pos.x.whole = x;
    a.pos.z.whole = b.iconZ;
}
// the exe's / 112 by multiplication (0x92492493, >> 6, + the sign)
int32_t div112(int32_t v) {
    int32_t e = int32_t((int64_t(v) * int64_t(int32_t(0x92492493u))) >> 32);
    e += v;
    e >>= 6;
    e += int32_t(uint32_t(e) >> 31);
    return e;
}
// the exe's / 64 (rounded toward 0)
int32_t div64(int32_t v) { return (v + ((v >> 31) & 0x3f)) >> 6; }
// exe 0x5185a0: the map camera follows the icon with margins, its lead toward the icons
void follow(TravelMap& b) {
    if (int(b.iconX) - int(b.cameraX) < kMarginLeft)
        b.cameraX = int16_t(uint16_t(b.iconX) - uint16_t(b.prevIconX) + uint16_t(b.cameraX));
    if (int(b.iconZ) - int(b.cameraZ) < kMarginTop)
        b.cameraZ = int16_t(uint16_t(b.cameraZ) - uint16_t(b.prevIconZ) + uint16_t(b.iconZ));
    if (int(b.iconX) - int(b.cameraX) > kMarginRight)
        b.cameraX = int16_t(uint16_t(b.iconX) - uint16_t(b.prevIconX) + uint16_t(b.cameraX));
    if (int(b.iconZ) - int(b.cameraZ) > kMarginBottom)
        b.cameraZ = int16_t(uint16_t(b.cameraZ) - uint16_t(b.prevIconZ) + uint16_t(b.iconZ));
    if (b.cameraX < kCameraMinX) b.cameraX = kCameraMinX;
    if (b.cameraX > kCameraMaxX) b.cameraX = kCameraMaxX;
    if (b.cameraZ < kCameraMinZ) b.cameraZ = kCameraMinZ;
    if (b.cameraZ > kCameraMaxZ) b.cameraZ = kCameraMaxZ;
    ScriptCamera& cam = scriptCamera();
    cam.point[0] = int32_t(uint32_t(int32_t(b.cameraX)) << 16);
    cam.point[2] = int32_t(uint32_t(int32_t(b.cameraZ)) << 16);
    b.leadX = int16_t(div112((int32_t(b.iconX) - b.cameraX) << 3));
    b.leadZ = int16_t(div64((int32_t(b.cameraZ) - b.iconZ) * 6));
    if (b.flags & kTravelSecondIcon) {
        b.secondLeadX = int16_t(div112((int32_t(b.secondX) - b.cameraX) << 3));
        b.secondLeadZ = int16_t(div64((int32_t(b.cameraZ) - b.secondZ) * 6));
    }
}
// [0x89f938 + index](block): a destination's callback before leaving (by the exe routine the table holds)
void pickCallback(const RoutineRef& fn, TravelMap& b) {
    GameState& g = gameState();
    const uint8_t map = b.map;
    switch (fn.id()) {
    case RoutineId::TravelPickNextOnMapE: // index 0 (ST08): the next story +1 on map 0xe
        if (map == 0xe) g.nextStory += 1;
        return;
    case RoutineId::TravelPickFirstDrop: // index 1: on map 1, the first time the drop to ST49 sub 2 instead
        if (map != kFirstMap) return;
        if (!storyFlag(kFirstDropFlag)) {
            setStoryFlag(kFirstDropFlag);
            b.destStage = 0x49;
            b.destRoom = 2;
            b.destPlace.x = 0;
            b.destPlace.y = -1;
            b.destPlace.z = 0;
            b.destPlace.yaw = 0;
        }
        b.flags |= kTravelNoFlight;
        return;
    case RoutineId::TravelPickNoFlight: b.flags |= kTravelNoFlight; return; // index 2
    case RoutineId::TravelPickNextOnMap2: // index 3
        if (map == 2) {
            g.nextStory += 1;
            b.arrivalFade = kNoFade;
        }
        return;
    case RoutineId::TravelPickSt46: // index 4: on map 5 to ST46
        if (map != 5) return;
        b.destStage = 0x46;
        b.flags |= kTravelNoFlight;
        b.destPlace.y = -1;
        b.destRoom = 0;
        b.destPlace.x = 0;
        b.destPlace.z = 0;
        b.destPlace.yaw = 0;
        b.arrivalFade = kNoFade;
        return;
    case RoutineId::TravelPickNextOrSt39: // index 5
        if (map == 7) {
            g.nextStory += 1;
            b.arrivalFade = kNoFade;
        } else if (map == 0x11 && !storyFlag(kLateStoryFlag)) {
            b.destStage = 0x39;
            b.destRoom = 1;
            b.destPlace.x = 0x80;
            b.destPlace.y = 0;
            b.destPlace.z = 0x40;
            b.destPlace.yaw = 0;
            b.arrivalFade = kNoFade;
        }
        return;
    case RoutineId::TravelPickNextOnMapB: // index 6
        if (map == 0xb) {
            g.nextStory += 1;
            b.arrivalFade = kNoFade;
        }
        return;
    default: notPortedAt(kTravelPort, "travel pick callback", fn.pointerValue());
    }
}
// exe 0x518710: the destination under the icon (its request copied); circle / start once ready -> its callback
int pick(TravelMap& b) {
    const std::vector<TravelDestination>& list = travelMaps[b.map];
    for (uint32_t i = 0;; ++i) {
        if (i >= list.size()) return 0;
        const TravelDestination& e = list[i];
        int dx = int(b.iconX) - int(e.mapX) + int(b.leadX);
        if (dx < 0) dx = int(e.mapX) - int(b.iconX) - int(b.leadX);
        int dz = int(b.leadZ) - int(b.iconZ) + int(e.mapZ);
        if (dz < 0) dz = int(b.iconZ) - int(b.leadZ) - int(e.mapZ);
        const bool near = i == kWideDestination ? (dx <= kWideReachX && dz <= kWideReachZ)
                                                : (dx <= kReachX && dz <= kReachZ);
        if (!near) continue;
        b.destination = uint8_t(i);
        b.flags |= kTravelOverDestination;
        b.destStage = e.stage;
        b.destRoom = e.room;
        b.destPlace.x = e.place.x;
        b.destPlace.y = e.place.y;
        b.destPlace.z = e.place.z;
        b.destPlace.yaw = e.place.yaw;
        if ((pad0().pressed & kPadConfirm) && b.ready == 1) {
            pickCallback((i & 0xff) < travelPicks.size() ? travelPicks[i & 0xff] : RoutineRef(), b);
            return 1;
        }
        return 0;
    }
}
// exe 0x518960: the destination's name (window 0), the hint (window 1) after the first ticks and when idle
void windows(TravelMap& b) {
    if (b.flags & kTravelOverDestination) {
        if (windowStatus(kNameWindow) == 0) windowOpen(kNameWindow, kMessageBank, int16_t(b.destination));
    } else if (windowStatus(kNameWindow) != 0) {
        windowClose(kNameWindow, WindowClose::Force);
    }
    int hint;
    if (b.hintShown == 0) {
        hint = 1;
        if (b.hintTicks < kHintTicks) {
            b.hintTicks += 1;
        } else {
            b.hintShown = 1;
            b.hintTicks = 0;
            b.idleTicks = kHintTicks;
            b.ready = 1;
        }
    } else if (b.idleTicks < kHintTicks) {
        hint = 0;
        b.idleTicks += 1;
    } else {
        hint = 1;
    }
    if (b.picked == 0 && hint) {
        if (windowStatus(kHintWindow) == 0) windowOpen(kHintWindow, kMessageBank, kHintMessage);
    } else if (windowStatus(kHintWindow) != 0) {
        windowClose(kHintWindow, WindowClose::Force);
    }
    // exe 0x518a30 / 0x518c00: the destination markers and the map grid (draw packets, render output)
}
// the movement shared by SetUp and Choose
void moveIcons(TravelMap& b) {
    steer(b);
    if (b.flags & kTravelSecondIcon) secondIconFollow(b);
    follow(b);
    if (b.flags & kTravelSecondIcon) keepApart(b);
    follow(b);
}
// exe 0x517f50
void travelSetUp(TravelMap& b) {
    player().flags &= uint8_t(~kActorLiveDrawn);
    scriptSequenceStart(stateAddress(TableId::TravelCameraScript), stateAddress(TableId::TravelCameraSteps));
    ScriptCamera& cam = scriptCamera();
    cam.angles[kScriptPitch] = kMapCameraPitch;
    cam.angles[kScriptYaw] = kMapCameraYaw;
    cam.angles[kScriptDistance] = kMapCameraDistance;
    b.map = mapOf();
    b.storyMarker = storyMarkerOf();
    const uint8_t dock = gameState().flutterDock;
    b.island = dock <= kLastIsland ? dock : 0;
    b.flags = b.map == kFirstMap ? 0 : kTravelSecondIcon;
    const TravelDestination& e = destinationOf(b.map, b.island);
    b.cameraX = e.mapX;
    b.cameraZ = e.mapZ;
    b.clearHints();
    b.ready = 0;
    b.arrivalFade = kArrivalFadeIn;
    b.prevIconX = e.mapX;
    b.iconX = e.mapX;
    b.prevIconZ = b.cameraZ;
    b.iconZ = b.cameraZ;
    scriptActorSpawnRecord(stateAddress(TableId::TravelFlutterIconRecord));
    StaticActor& a = iconActor(0);
    TravelIconFields& icon = classState<TravelIconFields>(a);
    a.pos.x.whole = b.iconX;
    a.pos.z.whole = b.iconZ;
    icon.heading = e.heading;
    a.rotation.y = int16_t(e.heading);
    const uint8_t isl = b.island;
    if (isl == kFlyingIsland || (isl == 1 && b.map == kFirstMap)) icon.startsFlying = 1;
    if (b.flags & kTravelSecondIcon) {
        scriptActorSpawnRecord(stateAddress(TableId::TravelSecondIconRecord));
        StaticActor& a2 = iconActor(1);
        b.prevSecondX = a2.pos.x.whole;
        b.secondX = a2.pos.x.whole;
        b.prevSecondZ = a2.pos.z.whole;
        b.secondZ = a2.pos.z.whole;
    }
    moveIcons(b);
    scriptState().enterState(uint8_t(TravelState::Choose));
}
// exe 0x5181f0
void travelChoose(TravelMap& b) {
    ScriptState& s = scriptState();
    b.flags &= uint8_t(~kTravelPickFlags);
    if (s.subState == uint8_t(TravelChooseStep::Steer)) {
        moveIcons(b);
        if (pick(b)) {
            classState<TravelIconFields>(iconActor(0)).flightPhase =
                uint8_t(((b.flags & kTravelNoFlight) | 4) >> 1);
            b.picked = 1;
            s.subState += 1;
        }
    } else if (s.subState == uint8_t(TravelChooseStep::Fly)) {
        if (classState<TravelIconFields>(iconActor(0)).flightPhase == uint8_t(TravelFlight::Landed) &&
            windowStatus(kHintWindow) == 0 && display().drawBuffer != 0) {
            scriptFade(kWipe20Request + 2, 0);
            s.enterState(uint8_t(TravelState::Leave));
        }
    }
    windows(b);
    scriptSequenceTick();
}
// exe 0x5182d0
void travelLeave(TravelMap& b) {
    ScriptState& s = scriptState();
    switch (TravelLeaveStep(s.subState)) {
    case TravelLeaveStep::Sweep:
        actorsSweep(0xffe, 0x80);
        windowCloseAll(WindowClose::Gentle);
        s.subState += 1;
        [[fallthrough]];
    case TravelLeaveStep::Request:
        s.subState = uint8_t(TravelLeaveStep::WaitArrival);
        requestTransition(kTransitionAccepted, b.destStage, b.destRoom, b.destPlace.x, b.destPlace.y, b.destPlace.z,
                          b.destPlace.yaw, b.arrivalFade, kNoFade);
        return;
    case TravelLeaveStep::WaitArrival:
        if (transitionRequest().state != kTransitionNone) return;
        player().flags |= kActorLiveDrawn;
        scriptEndBlock();
        return;
    default: return;
    }
}

// the icon's set-up once its model is bound: the contact bytes, the collision box of its variant, the rotation and the
// velocity cleared, the scale (the shadow: flat, its heading from its yaw), state 1
void iconInit(StaticActor& a, uint16_t scale, bool shadow) {
    a.airborne = 0;
    a.onPlatform = 0;
    a.lastMover = nullptr;
    a.collisionBox.pointAt(stateAddress(TableId::TravelIconBoxes) + uint32_t(a.variant) * kIconBoxSize);
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.velocity.z = 0;
    a.velocity.y = 0;
    a.velocity.x = 0;
    a.interactionBox = nullptr;
    a.interaction = 0;
    if (shadow) {
        a.scale.z = int16_t(scale);
        a.scale.x = int16_t(scale);
        a.scale.y = 0;
        a.tint = 0;
        classState<TravelIconFields>(a).heading = uint16_t(a.rotation.y);
    } else {
        a.scale.z = int16_t(scale);
        a.scale.y = int16_t(scale);
        a.scale.x = int16_t(scale);
    }
    a.enterState(1);
}
// the shadow's ship spawned (a static of the icon type, its variant, at the shadow, following it), once
void spawnShip(StaticActor& a, uint8_t variant) {
    const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (address) {
        StaticActor& s = staticActorAt(address);
        s.flags |= kActorLiveDrawn;
        s.type = kIconType;
        s.subtype = 0;
        s.variant = variant;
        s.pos = a.pos;
        s.rotation = a.rotation;
        TravelIconFields& ship = classState<TravelIconFields>(s);
        ship.parentSlot = a.slot;
        ship.kind = kIconShip;
    }
    a.subState += 1;
}
// the pitch / roll by the heading
void bankIcon(StaticActor& a) {
    const uint32_t k = (uint32_t(int32_t(a.rotation.y)) + 0x80) >> 8 & 0xf; // sar on the sign-extended word
    a.rotation.x = int16_t(iconBanks[k].pitch);
    a.rotation.z = int16_t(iconBanks[k].roll);
}
// the icon's model bound (its variant); not in the bank: freed (exe 0x5e1540)
bool iconBind(StaticActor& a) {
    if (bindActorModel(a, a.variant)) return true;
    freeActor(a);
    return false;
}
// the ship: at its shadow (the Flutter raised by the altitude, the Sulphur Bottom by an offset)
void shipFollow(StaticActor& a, uint16_t scale, int dy, int dz, bool byAltitude) {
    if (a.state == 0) {
        if (!iconBind(a)) return;
        iconInit(a, scale, false);
    }
    StaticActor& p = staticActorAt(actorBySlot(uint32_t(int32_t(int8_t(classState<TravelIconFields>(a).parentSlot)))));
    a.pos = p.pos;
    a.rotation = p.rotation;
    if (byAltitude) {
        const uint16_t altitude = classState<TravelIconFields>(p).altitude;
        a.pos.y.whole = int16_t(uint32_t(uint16_t(a.pos.y.whole)) - (altitude >> 1));
        a.pos.z.whole = int16_t(uint32_t(uint16_t(a.pos.z.whole)) + altitude);
    } else {
        a.pos.y.whole = int16_t(uint32_t(uint16_t(a.pos.y.whole)) + uint32_t(dy));
        a.pos.z.whole = int16_t(uint32_t(uint16_t(a.pos.z.whole)) + uint32_t(dz));
    }
}
// exe 0x5ad230: the Flutter's shadow -- turns toward its heading, banks, rises / lands by its flight, spawns the ship
void flutterShadow(StaticActor& a) {
    TravelIconFields& icon = classState<TravelIconFields>(a);
    if (a.state == 0) {
        if (!iconBind(a)) return;
        iconInit(a, kFlutterShadowScale, true);
        a.semiTransparency = kShadowSemiTransparency;
        if (icon.startsFlying == 0) {
            icon.altitude = 0;
            icon.flightPhase = uint8_t(TravelFlight::Rising);
        } else {
            icon.altitude = kFlightAltitude;
            icon.flightPhase = uint8_t(TravelFlight::Flying);
        }
    }
    // the turn toward the heading
    const uint16_t want = icon.heading;
    const uint16_t yaw = uint16_t(a.rotation.y);
    int16_t d = int16_t((yaw - want) & kAngleMask);
    if (d != 0) {
        if (d >= kHalfTurn) d = int16_t(d - kFullTurn);
        const int m = d < 0 ? -d : d;
        const int step = m > kTurnHalving ? (m >> 1) : kTurnStep;
        if (d > 0) {
            if (d < int16_t(step)) a.rotation.y = int16_t(want & kAngleMask);
            else a.rotation.y = int16_t(uint16_t(yaw - step));
        } else if (d < 0) {
            if (int(d) > -int(int16_t(step))) a.rotation.y = int16_t(want & kAngleMask);
            else a.rotation.y = int16_t(uint16_t(yaw + step));
        }
    }
    bankIcon(a);
    if (icon.flightPhase == uint8_t(TravelFlight::Rising)) {
        icon.altitude += 1u;
        if (icon.altitude == kFlightAltitude) icon.flightPhase += 1u;
    } else if (icon.flightPhase == uint8_t(TravelFlight::Landing)) {
        icon.altitude -= 1u;
        if (icon.altitude == 0) icon.flightPhase += 1u;
    }
    if (a.subState == 0) spawnShip(a, kFlutterVariant);
}
// exe 0x5ad510: the Sulphur Bottom's shadow -- banks, spawns the ship (no flight)
void sulphurShadow(StaticActor& a) {
    if (a.state == 0) {
        if (!iconBind(a)) return;
        iconInit(a, kSulphurShadowScale, true);
        a.semiTransparency = kShadowSemiTransparency;
    }
    bankIcon(a);
    if (a.subState == 0) spawnShip(a, kSulphurVariant);
}

} // namespace

bool travelScriptTick(uint8_t id) {
    if (!storyScriptHandler(id).is(RoutineId::TravelScript)) return false;
    TravelMap& b = travelMap();
    const uint8_t state = scriptState().state;
    switch (TravelState(state)) {
    case TravelState::SetUp: travelSetUp(b); return true;
    case TravelState::Choose: travelChoose(b); return true;
    case TravelState::Leave: travelLeave(b); return true;
    default: notPortedAt(kTravelPort, "script 7 state", state);
    }
}

bool travelMapDraw(TravelMapDraw& out) {
    ScriptState& s = scriptState();
    if (!(s.flags & kScriptRunning) || s.id != kTravelScriptId ||
        s.state != uint8_t(TravelState::Choose))
        return false;
    const TravelMap& b = travelMap();
    out.cameraX = b.cameraX;
    out.cameraZ = b.cameraZ;
    out.storyMarker = b.storyMarker;
    out.destinations.clear();
    if (!travelMaps.available() || b.map >= TravelMaps::kMaps || !travelMaps->present[b.map]) return true;
    const std::vector<TravelDestination>& list = travelMaps[b.map];
    for (uint32_t i = 0; i < list.size(); ++i) {
        if (i == 2) continue;
        out.destinations.push_back({i, list[i].mapX, list[i].mapZ});
    }
    return true;
}

void travelRoomCase() {
    RoomScriptState& room = roomScriptState();
    if (room.handlerStep != 0) return;
    scriptRun(kTravelScriptId); // exe 0x511270(7)

    room.handlerStep += 1;
}

void travelIconUpdate(const PlayerEnv&, uint32_t address) { // exe 0x5ad1c0
    StaticActor& a = staticActorAt(address);
    a.savePreviousPose();
    a.savePreviousContact();
    const TravelIconFields& icon = classState<TravelIconFields>(a);
    switch (a.variant) {
    case kFlutterVariant:
        if (icon.kind == kIconShadow) flutterShadow(a);
        else shipFollow(a, kFlutterShipScale, 0, 0, true);
        return;
    case kSulphurVariant:
        if (icon.kind == kIconShadow) sulphurShadow(a);
        else shipFollow(a, kSulphurShipScale, kSulphurShipDy, kSulphurShipDz, false);
        return;
    default: notPortedAt(kTravelPort, "0x5ad1c0 variant", a.variant);
    }
}

namespace {
// the Flutter's dock of each stage (exe data; kKeepDock: the dock stays)
const DataTable<ScalarTable<int8_t, 0x60>> stageDocks{TableId::TravelStageDocks};
constexpr int8_t kKeepDock = -1;
constexpr uint8_t kFirstFieldStage = 8; // the dock counters advance only from this stage on
constexpr uint8_t kNoDockCount = 0xff;  // a dock counter that does not advance
} // namespace

void flutterDockUpdate() {
    GameState& g = gameState();
    const uint8_t st = g.stage;
    if (stageDocks[st] != kKeepDock) g.flutterDock = uint8_t(stageDocks[st]);
    if (g.flutterDock != g.countedDock && g.stage >= kFirstFieldStage) {
        g.countedDock = g.flutterDock;
        for (size_t i = 0; i < g.dockCounters.size(); ++i) {
            const uint8_t c = g.dockCounters[i];
            if (c != 0 && c != kNoDockCount) g.dockCounters[i] = uint8_t(c + 1);
        }
    }
}

} // namespace od::port
