// The ST0E encounter switch (pool 0xe0 type 0xa, exe 0x41b830; see st0e_spawner.hpp).
#include "content/yosyonke/st0e_spawner.hpp"
#include "content/yosyonke/st0e_spawner_state.hpp"

#include "game/loaded_data.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/encounter.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"

namespace od::port {

using namespace st0e_spawner_state;

namespace {

constexpr const char* kEnemySpawnerPort = "enemy spawner port"; // the area of its port-gap errors

// +8 (the exe's table 0x88a59c): the init, then one state per variant (+6) + 1
enum class EncounterSwitchState : uint8_t {
    Init = 0,       // exe 0x41b850: the class bytes cleared, the variant's state
    EastGate = 1,   // exe 0x41b870: the field encounters on west of x 0x700
    WestGate = 2,   // exe 0x41b8d0: on east of x -0x3000
    EastToggle = 3, // exe 0x41b930: on west of x 0x700 (its sub-states the other way round)
};
// +9 of EastGate / WestGate
enum class GateSub : uint8_t {
    Arm = 0, // the formation picks set, then Off
    Off = 1, // the field encounters suspended
    On = 2,  // the field encounters running
};
// +9 of EastToggle
enum class ToggleSub : uint8_t {
    Off = 0, // the formation picks set every tick
    On = 1,
};


constexpr uint32_t kFieldKind = 1u << uint32_t(EncounterKind::Field);
constexpr int16_t kEastGateX = 0x700, kWestGateX = -0x3000;

// the field encounters' formation picks pointed at an exe table
void setFormationPicks(uint32_t picks) { patchedRecordFor<FieldEncounterTable>(encounterState().fieldTable).formationPick = picks; }
void fieldEncountersOn() { encounterSuspend(kFieldKind, 1); }
void fieldEncountersOff() { encounterSuspend(kFieldKind, 0); }

// exe 0x41b830
void encounterSwitch(ControllerActor& actor) {
    const int16_t px = player().pos.x.whole;
    switch (EncounterSwitchState(actor.state)) {
    case EncounterSwitchState::Init:
        classState<EncounterSwitchFields>(actor).scratch = {};
        actor.enterState(uint8_t(uint32_t(actor.variant) + 1u));
        return;
    case EncounterSwitchState::EastGate:
        switch (GateSub(actor.subState)) {
        case GateSub::Arm:
            setFormationPicks(stateAddress(TableId::St0eSpawnerEastFormationPicks));
            [[fallthrough]];
        case GateSub::Off:
            if (px < kEastGateX) {
                fieldEncountersOn();
                actor.subState = uint8_t(GateSub::On);
            }
            return;
        case GateSub::On:
            if (px > kEastGateX) {
                fieldEncountersOff();
                actor.subState = uint8_t(GateSub::Off);
            }
            return;
        default: return;
        }
    case EncounterSwitchState::WestGate:
        switch (GateSub(actor.subState)) {
        case GateSub::Arm:
            setFormationPicks(stateAddress(TableId::St0eSpawnerWestFormationPicks));
            [[fallthrough]];
        case GateSub::Off:
            if (px > kWestGateX) {
                fieldEncountersOn();
                actor.subState = uint8_t(GateSub::On);
            }
            return;
        case GateSub::On:
            if (px < kWestGateX) {
                fieldEncountersOff();
                actor.subState = uint8_t(GateSub::Off);
            }
            return;
        default: return;
        }
    case EncounterSwitchState::EastToggle:
        switch (ToggleSub(actor.subState)) {
        case ToggleSub::Off:
            setFormationPicks(stateAddress(TableId::St0eSpawnerEastFormationPicks));
            if (px < kEastGateX) {
                fieldEncountersOn();
                actor.subState = uint8_t(ToggleSub::On);
            }
            return;
        case ToggleSub::On:
            if (px > kEastGateX) {
                fieldEncountersOff();
                actor.subState = uint8_t(ToggleSub::Off);
            }
            return;
        default: return;
        }
    default: notPortedAt(kEnemySpawnerPort, "ST0E spawner 0x41b830 state", actor.state);
    }
}

} // namespace

void encounterSwitchUpdate(const PlayerEnv&, uint32_t actor) { encounterSwitch(controllerActorAt(actor)); }

} // namespace od::port
