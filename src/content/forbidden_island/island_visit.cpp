// The first visit of the Forbidden Island (see island_visit.hpp). One routine per exe routine, addresses in the
// comments; the names are hypotheses from the observed behaviour. Draw-only parts are left out as in the other ports.
#include "content/forbidden_island/island_visit.hpp"
#include "content/forbidden_island/island_visit_state.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actors.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_grid.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace island_visit_state;

namespace {

// ---- the arrival messages -----------------------------------------------------------------------------------------
// The message controllers of pool e0 (type 0x1b: a message with an argument, 0x1d: a message shown once): the
// message, its argument and (type 0x1b) the word after the record's point
struct MessageControllerFields {
    explicit MessageControllerFields(const ControllerActor& actor) : MessageControllerFields(const_cast<ControllerActor&>(actor)) {}
    explicit MessageControllerFields(ControllerActor& actor) : message(actor.params, 0), argument(actor.params[2]), speaker(actor.recordPoint.spare) {}
    BytesField<uint16_t, 4> message;
    uint8_t& argument;
    FieldAlias<uint16_t, int16_t> speaker; // kNoSpeaker (hypothesis: no speaker's face)
};
constexpr uint8_t kMessageWithArgument = 0x1b, kMessageOnce = 0x1d;
constexpr uint16_t kNoSpeaker = 0xffff;

// exe 0x57e090(id, arg, kind): a message controller (type 0x1b with the argument, or 0x1d shown once); true when a
// slot was free
bool spawnMessage(uint16_t id, uint8_t arg, bool once) {
    const uint32_t p = allocActorE0(); // exe 0x5e1440
    if (!p) return false;
    ControllerActor& c = controllerActorAt(p);
    MessageControllerFields fields(c);
    c.flags = kActorLive;
    c.type = once ? kMessageOnce : kMessageWithArgument;
    c.variant = 0;
    if (!once) fields.speaker = kNoSpeaker;
    fields.message = id;
    fields.argument = once ? uint8_t(1) : arg;
    return true;
}

// ---- the room handlers' records and flags -------------------------------------------------------------------------
constexpr uint32_t kFlagIslandBlizzard = 0x680; // the blizzard layer's records spawned (once per visit)
constexpr uint32_t kFlagSt11FightSeen = 0x591, kFlagSt11FightWon = 0x592;
constexpr uint32_t kFlagFirstArrival = 0x593, kFlagSecondArrival = 0x59d, kFlagArrivalMessages = 0x59e;
constexpr uint32_t kFlagReturnMessage = 0x140;
constexpr uint32_t kFlagSt1dPackMet = 0x683; // set by ST1D sub 1's handler
constexpr uint8_t kHandlerDone = 0xff;
constexpr uint8_t kExitLightOpen = 2; // the exit light's state that opens it
constexpr uint8_t kScriptSt11Arrival = 0x2a;

RoomScriptState& room() { return roomScriptState(); }
void nextHandlerStep() { ++room().handlerStep; }
void spawnBlizzardOnce() {
    if (!storyFlag(kFlagIslandBlizzard)) {
        spawnRecords(stateAddress(TableId::IslandVisitBlizzardSpawns), 1);
        setStoryFlag(kFlagIslandBlizzard);
    }
}
// the room record's actor k
ActorHeader& roomActor(size_t k) { return actorHeaderAt(room().actors[k]); }
void openExitLight(size_t k) { roomActor(k).state = kExitLightOpen; }

// ---- ST10 (the landing field) -------------------------------------------------------------------------------------
// exe 0x57dec0 (story 1 sub 0): the dropship, the blizzard, the encounter group (0 / 1 after the ST11 fight), the
// arrival messages (0x1e the first time, 0x23 once after the first), a trigger
void st10FirstLanding() {
    if (room().handlerStep != 0) return;
    spawnRecords(stateAddress(TableId::IslandVisitSt10DropshipSpawns), 1);
    spawnBlizzardOnce();
    encounterSpawnGroupZoneSlot(storyFlag(kFlagSt11FightWon) ? 1 : 0); // exe 0x5e6640
    if (!storyFlag(kFlagArrivalMessages)) {
        setStoryFlag(kFlagArrivalMessages);
        uint16_t msg = 0;
        if (!storyFlag(kFlagFirstArrival)) {
            setStoryFlag(kFlagFirstArrival);
            msg = 0x1e;
        } else if (!storyFlag(kFlagSecondArrival)) {
            setStoryFlag(kFlagSecondArrival);
            msg = 0x23;
        }
        if (msg) spawnMessage(msg, 1, true);
    }
    spawnRecords(stateAddress(TableId::IslandVisitSt10TriggerSpawns), 1);
    nextHandlerStep();
}
// exe 0x57dfb0 (story 1 sub 1): the decorations / statics 0x8c6980 (10), the blizzard
void st10FirstDecorations() {
    if (room().handlerStep != 0) return;
    spawnRecords(stateAddress(TableId::IslandVisitSt10DecorationSpawns), 10);
    spawnBlizzardOnce();
    nextHandlerStep();
}
// exe 0x57e000 (story 2.. sub 0): the dropship + an actor, message 0x28 once, a trigger
void st10LaterLanding() {
    if (room().handlerStep != 0) return;
    spawnRecords(stateAddress(TableId::IslandVisitSt10LaterDropshipSpawns), 2);
    if (!storyFlag(kFlagReturnMessage)) {
        setStoryFlag(kFlagReturnMessage);
        spawnMessage(0x28, 1, true);
    }
    spawnRecords(stateAddress(TableId::IslandVisitSt10LaterTriggerSpawns), 1);
    nextHandlerStep();
}
// exe 0x57e060 (story 2.. sub 1)
void st10LaterDecorations() {
    if (room().handlerStep != 0) return;
    spawnRecords(stateAddress(TableId::IslandVisitSt10LaterDecorationSpawns), 1);
    spawnRecords(stateAddress(TableId::IslandVisitSt10LaterStaticSpawns), 1);
    nextHandlerStep();
}

// ---- ST11 ---------------------------------------------------------------------------------------------------------
// exe 0x57e180 (story 1 sub 0): the reaverbots by depth, script 0x2a at z >= 0x500 (the fight), its end
void st11FirstVisit() {
    RoomScriptState& r = room();
    switch (r.handlerStep) {
    case 0:
        spawnRecords(stateAddress(TableId::IslandVisitSt11FirstSpawns), 1);
        if (!storyFlag(kFlagIslandBlizzard)) {
            spawnRecords(stateAddress(TableId::IslandVisitSt11SecondSpawns), 1);
            setStoryFlag(kFlagIslandBlizzard);
        }
        nextHandlerStep();
        [[fallthrough]];
    case 1: {
        if (storyFlag(kFlagSt11FightSeen)) {
            spawnRecords(stateAddress(TableId::IslandVisitSt11ExitLightSpawns), 6);
            r.handlerStep = kHandlerDone;
            return;
        }
        const int16_t z = player().pos.z.whole;
        if (z < 0x500) {
            // the depth thresholds of the four reaverbot records (exe data)
            const DataArray<int16_t> depths(stateAddress(TableId::IslandVisitSt11ReaverbotDepths));
            for (uint32_t i = 0; i < 4; ++i) { // record 0x8c6d1c + i * 0x14 once (bit i of byte 3) past depth i
                if ((r.handlerByte3 & (1u << i)) == 0 && depths[i] < z) {
                    spawnRecords(stateAddress(TableId::IslandVisitSt11ReaverbotRecords) + i * 0x14, 1);
                    r.handlerByte3 = uint8_t(r.handlerByte3 | (1u << i));
                }
            }
            return;
        }
        scriptRun(kScriptSt11Arrival); // exe 0x511270
        setStoryFlag(kFlagSt11FightSeen);
        nextHandlerStep();
        return;
    }
    case 2:
        if (storyFlag(kFlagSt11FightSeen) && !scriptState().inUse()) {
            spawnMessage(1, 2, false);
            nextHandlerStep();
        }
        return;
    case 3:
        if ((roomActor(0).flags & kActorLive) == 0) { // the fight's actor (script 0x2a's record 0x8a4a58 -> slot 0) gone
            setStoryFlag(kFlagSt11FightWon);
            soundFadeOut(0xf, 0x333, 0); // exe 0x5d5780
            openExitLight(1);            // the exit lights (slots 1 / 2)
            openExitLight(2);
            spawnRecords(stateAddress(TableId::IslandVisitSt11ExitLightSpawns), 6);
            r.handlerStep = kHandlerDone;
        }
        return;
    default: return;
    }
}

// ---- ST1D sub 0 ---------------------------------------------------------------------------------------------------
// exe 0x5804b0 (story 1 sub 0): the blizzard, a trigger; before flag 0x683 the spawners 0x8c7f94 (6) + 0x8c8098, after
// it 0x8c800c (6); entering one of two cell boxes spawns the three type-0x34 reaverbots 0x8c7f58 (slots 0..2) and,
// before 0x683, the exit light 0x8c8084 (slot 15) that opens when the three are gone
void st1dFirstVisit() {
    static constexpr CellBoxValues kPackBoxes[2] = {{-10, 4, 4, 5}, {6, 10, 2, 1}};
    RoomScriptState& r = room();
    const uint8_t s = r.handlerStep;
    if (s == 0) {
        spawnRecords(stateAddress(TableId::IslandRoomsSt1DCastSpawns), 1);
        setStoryFlag(kFlagIslandBlizzard);
        spawnRecords(stateAddress(TableId::IslandVisitSt1dTriggerSpawns), 1);
        nextHandlerStep();
        if (!storyFlag(kFlagSt1dPackMet)) {
            spawnRecords(stateAddress(TableId::IslandVisitSt1dSpawnerSpawns), 6);
            spawnRecords(stateAddress(TableId::IslandVisitSt1dSpawnerMoreSpawns), 1);
        } else {
            spawnRecords(stateAddress(TableId::IslandVisitSt1dLaterSpawnerSpawns), 6);
        }
    } else if (s == 2) {
        if (!storyFlag(kFlagSt1dPackMet)) {
            for (size_t i = 0; i < 3; ++i)
                if (roomActor(i).flags != 0) return;
            openExitLight(15);
        }
        r.handlerStep = kHandlerDone;
        return;
    } else if (s != 1) {
        return;
    }
    const Fixed16Vec3& pos = player().pos;
    if (!pointInCellBox(pos, kPackBoxes[0].column, kPackBoxes[0].row, kPackBoxes[0].columns, kPackBoxes[0].rows) &&
        !pointInCellBox(pos, kPackBoxes[1].column, kPackBoxes[1].row, kPackBoxes[1].columns, kPackBoxes[1].rows))
        return;
    spawnRecords(stateAddress(TableId::IslandVisitSt1dReaverbotSpawns), 3);
    if (!storyFlag(kFlagSt1dPackMet)) spawnRecords(stateAddress(TableId::IslandVisitSt1dExitLightSpawns), 1);
    nextHandlerStep();
}

// ---- classes ------------------------------------------------------------------------------------------------------
enum class PropState : uint8_t { Bind = 0, Animate = 1 };

// exe 0x492270 (ST10 type 0x2c): the dropship parked on the island -- bound (variant 0) at animation 0, then animated
void parkedDropship(AnimatedActor& a) {
    a.savePreviousPose();
    if (PropState(a.state) != PropState::Bind) { // exe 0x492320
        animStepPose(a); // exe 0x5e1e80
        return;
    }
    if (!bindActorModel(a, 0)) { // exe 0x4922b0: exe 0x5e0ba0 (variant 0)
        freeActor(a);            // exe 0x5e1490
        return;
    }
    a.collisionBox.pointAt(TableId::IslandVisitParkedDropshipBox);
    a.interactionBox = nullptr;
    a.clearClassBlock();
    a.enterState(uint8_t(PropState::Animate));
    a.anim = 0;
    a.prevAnim = kNoAnimShown;
    a.airborne = 0;
    animSetPose(a, 0, 0); // exe 0x5e1c30
}

// exe 0x4acbe0 (ST10 type 0x37): a two-part decoration of sub 1 (variant 1 binds the model of type 0x68), animated
constexpr uint8_t kDecorationType = 0x37, kDecorationAltModelType = 0x68;
void decoration(AnimatedActor& a) {
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    if (PropState(a.state) != PropState::Bind) {
        animStepPose(a);
        return;
    }
    if (a.variant != 0) {
        a.type = kDecorationAltModelType;
        a.variant = 0;
    }
    if (!bindActorModel(a, a.variant)) { // exe 0x5e0bc0(a, a + 6)
        freeActor(a);
        return;
    }
    if (a.type == kDecorationAltModelType) {
        a.type = kDecorationType;
        a.variant = 1;
    }
    a.flags |= kActorDrawn;
    a.prevAnim = 0;
    a.anim = 0;
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.velocity.setZSpare(0, 0);
    a.velocity.setXY(0, 0);
    a.collisionBox.pointAt(TableId::IslandVisitDecorationBox);
    a.actorHit = 0;
    a.platform = nullptr;
    animSetPose(a, 0, 0);
    a.enterState(uint8_t(PropState::Animate));
}

// exe 0x5a37c0 (ST10 pool 0x60 type 0x1d): sub 1's statics; variant 0 blocks its grid cell (segment variant 1); kept
// on the ground
constexpr int kStaticHover = 0x90;
void keepOnGround(const PlayerEnv& env, StaticActor& a) { // exe 0x40f190
    a.pos.y = int32_t(uint32_t(actorGround(env, a) + kStaticHover) << 16);
}
void cellBlocker(const PlayerEnv& env, StaticActor& a) {
    a.savePreviousPose();
    a.savePreviousContact();
    if (PropState(a.state) != PropState::Bind) {
        keepOnGround(env, a);
        return;
    }
    if (!bindActorModel(a, a.variant)) { // exe 0x5e0bc0(a, a + 6)
        freeActor(a);                    // exe 0x5e1540
        return;
    }
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.savePreviousPose();
    a.flags |= kActorDrawn;
    a.velocity.clearBytes();
    a.acceleration.clearBytes();
    a.airborne = 0;
    a.onPlatform = 0;
    a.platform = nullptr;
    a.collisionBox.pointAt(TableId::IslandVisitCellBlockerBox);
    a.actorHit = 0;
    a.interactionBox = nullptr;
    a.interaction = 0;
    if (a.variant == 0) {
        a.semiTransparency = semiTransparencyBits(1, 0); // exe 0x5e1a70(a, 1, 0)
        // its grid cell (the column / row of its integer position, unchecked as the exe's inlined index)
        const uint32_t cell = gridCellAtIndex(uint32_t(a.pos.x.whole >> kCellShift) + kCellBias,
                                              uint32_t(a.pos.z.whole >> kCellShift) + kCellBias);
        switchCellSegmentVariant(GridCell(cell).head, 1); // exe 0x58d5f0
    }
    keepOnGround(env, a);
    a.enterState(uint8_t(PropState::Animate));
}

constexpr uint8_t kReaverbotType = 5, kHiddenReaverbotVariant = 2;
constexpr uint16_t kSoundSnowBurst = 0xa3;
constexpr int kPopOutReach = 0x400;

// exe 0x427440 (ST1D pool e0 type 0x24): a reaverbot hidden in the snow -- the player within 0x400 (planar, exe
// 0x5e3b00) -> one pool-0x20 type-5 sub-0 actor (variant 2, its parameter = this variant, the record's yaw, at this
// point), the snow's sound there, this controller freed (exe 0x5e15c0)
void hiddenReaverbot(ControllerActor& c) {
    const RecordPointFields& rec = classState<RecordPointFields>(c);
    const int32_t x = rec.point.x, y = rec.point.y, z = rec.point.z;
    const Player& p = player();
    const int dx = int16_t(uint16_t(uint32_t(p.pos.x.whole - int16_t(x)))),
              dz = int16_t(uint16_t(uint32_t(p.pos.z.whole - int16_t(z))));
    if (isqrt(int32_t(uint32_t(dx * dx) + uint32_t(dz * dz))) > kPopOutReach) return;
    const uint32_t address = allocActor20(); // exe 0x5e1160
    if (!address) return;
    AnimatedActor& r = animatedActorAt(address);
    r.flags = kActorLive;
    r.loopFlags |= uint8_t(kActorSweepGroup2 | kActorStageClass);
    r.type = kReaverbotType;
    r.subtype = 0;
    r.variant = kHiddenReaverbotVariant;
    r.spawnByte = 0;
    r.prevRotation.x = 0;
    r.params[0] = c.variant;
    r.rotation.x = 0;
    r.prevRotation.z = 0;
    r.prevRotation.y = rec.yaw;
    r.rotation.y = rec.yaw;
    r.rotation.z = 0;
    r.pos.x = int32_t(uint32_t(x) << 16);
    r.pos.y = int32_t(uint32_t(y) << 16);
    r.pos.z = int32_t(uint32_t(z) << 16);
    soundPlayAtPos(kSoundSnowBurst, r.pos); // exe 0x5d5380
    freeActor(c);                           // exe 0x5e15c0
}

enum class PackState : uint8_t { Start = 0, Run = 1 };
constexpr uint16_t kPackDelayEnd = 0xffff;
constexpr uint8_t kPackReaverbotType = 0x34, kPackReaverbotSubtype = 1;

void reaverbotPack(ControllerActor& c) {
    PackFields& fields = classState<PackFields>(c);
    if (PackState(c.state) == PackState::Start) {
        fields.spareWord = 0;
        fields.tickWord = 0;
        c.enterState(uint8_t(PackState::Run));
        return;
    }
    if (PackState(c.state) != PackState::Run) notPortedAt(kStoryPort, "0x424d10 state", c.state);
    const DataArray<uint16_t> delays(stateAddress(TableId::IslandVisitPackDelays)); // exe data, ended by kPackDelayEnd
    const uint16_t t = fields.tick;
    if (t != delays[fields.delayIndex]) {
        fields.tick = uint16_t(t + 1u);
        return;
    }
    const Player& p = player();
    if (uint32_t(int32_t(p.pos.z.whole) - 0x600) > 0x600u || uint32_t(int32_t(p.pos.x.whole) - 0x1000) > 0x1d00u)
        return;
    ++fields.delayIndex;
    fields.tick = 0;
    if (delays[fields.delayIndex] == kPackDelayEnd) fields.delayIndex = 0;
    const uint32_t address = allocActor20();
    if (!address) return;
    AnimatedActor& r = animatedActorAt(address);
    r.flags = kActorLive;
    r.type = kPackReaverbotType;
    r.variant = 0;
    r.subtype = kPackReaverbotSubtype;
    r.loopFlags |= uint8_t(kActorSweepGroup2 | kActorStageClass);
}

} // namespace

bool islandVisitRoomHandler(RoutineId fn, bool run) {
    // The two handlers that lock the player in a fight are only taken over once their cast is ported (else the room
    // keeps the snapshot path, without the lock): ST1D sub 0 needs the type-0x34 reaverbots 0x4a2a50 (its exit
    // light opens when they are gone), ST11 sub 0 script 0x2a and its fight actor (type 0x3b, 0x4b3b10)
    if (fn == RoutineId::St1DFirstVisitRoomHandler && !portedActorClass(RoutineId::SnowReaverbotUpdate)) return false;
    if (fn == RoutineId::St11FirstVisitRoomHandler &&
        (!storyScriptPorted(kScriptSt11Arrival) || !portedActorClass(RoutineId::St11FighterUpdate)))
        return false;
    void (*h)() = nullptr;
    switch (fn) {
    case RoutineId::St10FirstLandingRoomHandler: h = &st10FirstLanding; break;
    case RoutineId::St10FirstDecorationsRoomHandler: h = &st10FirstDecorations; break;
    case RoutineId::St10LaterLandingRoomHandler: h = &st10LaterLanding; break;
    case RoutineId::St10LaterDecorationsRoomHandler: h = &st10LaterDecorations; break;
    case RoutineId::St11FirstVisitRoomHandler: h = &st11FirstVisit; break;
    case RoutineId::IslandVisitRecordsRoomHandler: h = [] { spawnRecords(stateAddress(TableId::IslandVisitRecordsSpawns), 1); }; break;
        // every tick (no state test in the exe)
    case RoutineId::St1DFirstVisitRoomHandler: h = &st1dFirstVisit; break;
    case RoutineId::IslandVisitStory2Sub2RoomHandler: // story 2.. sub 2
        h = [] {
            if (room().handlerStep != 0) return;
            spawnRecords(stateAddress(TableId::IslandVisitStory2Sub2Spawns), 1);
            nextHandlerStep();
        };
        break;
    default: return false;
    }
    if (run) h();
    return true;
}

void parkedDropshipUpdate(const PlayerEnv&, uint32_t actor) { parkedDropship(animatedActorAt(actor)); }
void islandDecorationUpdate(const PlayerEnv&, uint32_t actor) { decoration(animatedActorAt(actor)); }
void hiddenReaverbotUpdate(const PlayerEnv&, uint32_t actor) { hiddenReaverbot(controllerActorAt(actor)); }
void reaverbotPackUpdate(const PlayerEnv&, uint32_t actor) { reaverbotPack(controllerActorAt(actor)); }
void cellBlockerUpdate(const PlayerEnv& env, uint32_t actor) { cellBlocker(env, staticActorAt(actor)); }

} // namespace od::port
