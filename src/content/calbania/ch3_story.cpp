// Story chapter 3: Calbania (see content/calbania/ch3_story.hpp): the scripts by their handlers and the room-script
// handlers, each in the file of its subject (content/calbania/ch3_scripts.hpp, ch3_rooms.hpp).
#include "content/calbania/ch3_story.hpp"

#include "content/calbania/ch3_rooms.hpp"
#include "content/calbania/ch3_scripts.hpp"
#include "engine/routine_ids.hpp"

namespace od::port {

namespace {

// the scripts of the chapter by their handler (the exe's table [0x89b6d0 + id * 4]: the named routines of
// engine/routines.csv)
struct ScriptEntry {
    RoutineId handler;
    void (*run)();
};
constexpr ScriptEntry kScripts[] = {
    {RoutineId::CalbaniaArrivalScript, &calbaniaArrivalScript},
    {RoutineId::TownFirstVisitScript, &townFirstVisitScript},
    {RoutineId::SquareOpeningScript, &squareOpeningScript},
    {RoutineId::SquareSecondPhaseScript, &squareSecondPhaseScript},
    {RoutineId::SquareThirdPhaseScript, &squareThirdPhaseScript},
    {RoutineId::SquareWallLiftScript, &squareWallLiftScript},
    {RoutineId::BaseEntranceScript, &baseEntranceScript},
    {RoutineId::BaseCameraScript, &baseCameraScript},
    {RoutineId::RescueScript, &rescueScript},
    {RoutineId::BrotherHeldScript, &brotherHeldScript},
    {RoutineId::BrotherFreedScript, &brotherFreedScript},
    {RoutineId::BaseExplosionScript, &baseExplosionScript},
    {RoutineId::TownReturnScript, &townReturnScript},
    {RoutineId::CalbaniaDepartureScript, &calbaniaDepartureScript},
    {RoutineId::NinoAttackEndScript, &ninoAttackEndScript},
    {RoutineId::RollToMachineScript, &rollToMachineScript},
    {RoutineId::NinoReturnScript, &ninoReturnScript},
    {RoutineId::IslandBlownScript, &islandBlownScript},
};

} // namespace

bool ch3ScriptPorted(RoutineId handler) {
    for (const ScriptEntry& e : kScripts)
        if (e.handler == handler) return true;
    return false;
}

bool ch3ScriptTick(const PlayerEnv& env, RoutineId handler) {
    address_forms::E = &env;
    for (const ScriptEntry& e : kScripts) {
        if (e.handler != handler) continue;
        e.run();
        return true;
    }
    return false;
}

bool ch3RoomHandler(RoutineId fn, bool run) {
    return ninoReturnRoomHandler(fn, run) || calbaniaStoryRoomHandler(fn, run) || glydeBaseRoomHandler(fn, run);
}

} // namespace od::port
