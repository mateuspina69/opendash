// The actor classes of content/forbidden_island in the class registry (game/content_registry), registered by
// content/registry.cpp. Moved from the registry table of game/actors.cpp (same entries, same order).
#include "content/registry.hpp"

#include "content/forbidden_island/elephant_boss.hpp"
#include "content/forbidden_island/island_visit.hpp"
#include "content/forbidden_island/st10_cls.hpp"
#include "content/forbidden_island/st11_boss.hpp"
#include "content/forbidden_island/st11_script2a.hpp"
#include "content/forbidden_island/st1c_cast.hpp"
#include "content/forbidden_island/st3e_reports.hpp"
#include "content/forbidden_island/st49_dropship.hpp"
#include "content/forbidden_island/st49_landing.hpp"
#include "content/forbidden_island/st1d_rock_burst.hpp"

namespace od::content {

using namespace od::port;

void registerForbiddenIslandClasses(ContentRegistry& r) {
    r.addClasses({
        {RoutineId::St1cRisingPropUpdate, &st1cRisingPropUpdate},
        {RoutineId::St1cFaceFigureUpdate, &st1cFaceFigureUpdate},
        {RoutineId::St1cMeshFlipFigureUpdate, &st1cMeshFlipFigureUpdate},
        {RoutineId::St3eSpeakerUpdate, &st3eSpeakerUpdate},
        {RoutineId::St49FlutterUpdate, &st49FlutterUpdate},
        {RoutineId::St49DropshipUpdate, &st49DropshipUpdate},
        {RoutineId::ParkedDropshipUpdate, &parkedDropshipUpdate},
        {RoutineId::IslandDecorationUpdate, &islandDecorationUpdate},
        {RoutineId::HiddenReaverbotUpdate, &hiddenReaverbotUpdate},
        {RoutineId::ReaverbotPackUpdate, &reaverbotPackUpdate},
        {RoutineId::CellBlockerUpdate, &cellBlockerUpdate},
        {RoutineId::St11FighterUpdate, &st11FighterUpdate},
        {RoutineId::St1cCuedFigureUpdate, &st1cCuedFigureUpdate},
        {RoutineId::St1cWalkingFigureUpdate, &st1cWalkingFigureUpdate},
        {RoutineId::St1cPosingFigureUpdate, &st1cPosingFigureUpdate},
        {RoutineId::St1cFidgetingFigureUpdate, &st1cFidgetingFigureUpdate},
        {RoutineId::St1cHoppingFlyerUpdate, &st1cHoppingFlyerUpdate},
        {RoutineId::St1cVanishingWalkerUpdate, &st1cVanishingWalkerUpdate},
        {RoutineId::St1cFlyerUpdate, &st1cFlyerUpdate},
        {RoutineId::St1cGroundedHopperUpdate, &st1cGroundedHopperUpdate},
        {RoutineId::St1cFallingFigureUpdate, &st1cFallingFigureUpdate},
        {RoutineId::St1cGlowingFigureUpdate, &st1cGlowingFigureUpdate},
        {RoutineId::St1cHopperUpdate, &st1cHopperUpdate},
        {RoutineId::St1cIdleFigureUpdate, &st1cIdleFigureUpdate},
        {RoutineId::St10RollerUpdate, &st10RollerUpdate},
        {RoutineId::St1dCrystalUpdate, &st1dCrystalUpdate},
        {RoutineId::ElephantCutsceneUpdate, &elephantCutsceneUpdate},
        {RoutineId::ElephantBossUpdate, &elephantBossUpdate},
        {RoutineId::St1dRockUpdate, &st1dRockUpdate},
        {RoutineId::St1dPillarUpdate, &st1dPillarUpdate},
        {RoutineId::St1dBoulderTrailUpdate, &st1dRockUpdate},
        {RoutineId::St1dExitLightUpdate, &st1dExitLightUpdate},
        {RoutineId::St1dBoulderUpdate, &st1dBoulderUpdate},
        {RoutineId::St1dDustRingUpdate, &st1dDustRingUpdate},
        {RoutineId::Script2aArrivalCastUpdate, &script2aArrivalCastUpdate},
        {RoutineId::St3eFloatingModelUpdate, &st3eFloatingModelUpdate},
        {RoutineId::St49StillModelUpdate, &st49StillModelUpdate},
        {RoutineId::St1cShipUpdate, &st1cShipUpdate},
        {RoutineId::St1cStillModelUpdate, &st1cStillModelUpdate},
        {RoutineId::St1cSecondStillModelUpdate, &st1cSecondStillModelUpdate},
        {RoutineId::St49DropshipCargoUpdate, &st49DropshipCargoUpdate},
        {RoutineId::St49DropshipCableUpdate, &st49DropshipCableUpdate},
        {RoutineId::RockBurstUpdate, &rockBurstUpdate},
    });
}

} // namespace od::content
