// The actor classes of content/manda in the class registry (game/content_registry), registered by
// content/registry.cpp. Moved from the registry table of game/actors.cpp (same entries, same order).
#include "content/registry.hpp"

#include "content/manda/exam_spawner.hpp"
#include "content/manda/ruin_spawners.hpp"

#include "content/manda/exam_enemies_a.hpp"
#include "content/manda/exam_enemies_b.hpp"
#include "content/manda/hopping_drone.hpp"
#include "content/manda/manda_boss.hpp"
#include "content/manda/manda_guardian.hpp"
#include "content/manda/manda_ruin_scripts.hpp"
#include "content/manda/manda_ruin_burrower.hpp"
#include "content/manda/manda_ruin_props.hpp"
#include "content/manda/st12_14.hpp"
#include "content/manda/st13_wall_break.hpp"
#include "content/manda/st14_enemy.hpp"
#include "content/manda/st24_ruin_gate.hpp"
#include "content/manda/st25_tron_scenes.hpp"

namespace od::content {

using namespace od::port;

void registerMandaClasses(ContentRegistry& r) {
    r.addClasses({
        // the story's cast (the scripts of Pokte Village and of the Manda ruin)
        {RoutineId::St25TronUpdate, &st25TronUpdate},
        {RoutineId::St25TankLandingUpdate, &st25TankLandingUpdate},
        {RoutineId::St25TankWreckUpdate, &st25TankWreckUpdate},
        {RoutineId::St25Script1cFigureUpdate, &st25Script1cFigureUpdate},
        {RoutineId::St24GateGuardianUpdate, &st24GateGuardianUpdate},
        {RoutineId::MandaRuinGroundedFigureUpdate, &mandaRuinGroundedFigureUpdate},
        {RoutineId::MandaRuinBurningWreckUpdate, &mandaRuinBurningWreckUpdate},
        {RoutineId::MandaRuinKeyItemUpdate, &mandaRuinKeyItemUpdate},
        // the pool-e0 type-0xa enemy spawners of the area's stages
        {RoutineId::SpawnerSt12Update, &spawnerSt12Update},
        {RoutineId::SpawnerSt13Update, &spawnerSt13Update},
        {RoutineId::SpawnerSt14Update, &spawnerSt14Update},
        {RoutineId::SpawnerSt55Update, &spawnerSt55Update},
        {RoutineId::MandaGuardianUpdate, &mandaGuardianUpdate},
        {RoutineId::MandaCastGuardianUpdate, &mandaCastGuardianUpdate},
        {RoutineId::MandaCast2GuardianUpdate, &mandaCast2GuardianUpdate},
        {RoutineId::MandaOpenedChestUpdate, &mandaOpenedChestUpdate},
        {RoutineId::MandaBladeUpdate, &mandaBladeUpdate},
        {RoutineId::MandaFallingBladeUpdate, &mandaFallingBladeUpdate},
        {RoutineId::MandaOutroGuardianUpdate, &mandaOutroGuardianUpdate},
        {RoutineId::MandaHopperUpdate, &mandaHopperUpdate},
        {RoutineId::HoppingDroneUpdate, &hoppingDroneUpdate},
        {RoutineId::MandaShotUpdate, &mandaShotUpdate},
        {RoutineId::ShotTrailUpdate, &shotTrailUpdate},
        {RoutineId::HitMuteUpdate, &hitMuteUpdate},
        {RoutineId::MandaCloudUpdate, &mandaCloudUpdate},
        {RoutineId::St14GuardianUpdate, &st14GuardianUpdate},
        {RoutineId::St14RisingPuppetUpdate, &st14RisingPuppetUpdate},
        {RoutineId::St14DroppingPuppetUpdate, &st14DroppingPuppetUpdate},
        {RoutineId::St14ControllerUpdate, &st14ControllerUpdate},
        {RoutineId::St14BeamUpdate, &st14BeamUpdate},
        {RoutineId::St14DebrisUpdate, &st14DebrisUpdate},
        {RoutineId::St14FloaterUpdate, &st14FloaterUpdate},
        {RoutineId::HoverReaverbotUpdate, &hoverReaverbotUpdate},
        {RoutineId::St13GunTurretUpdate, &st13GunTurretUpdate},
        {RoutineId::DropInWalkerUpdate, &dropInWalkerUpdate},
        {RoutineId::TumblingBlockUpdate, &tumblingBlockUpdate},
        {RoutineId::RollingBoulderUpdate, &rollingBoulderUpdate},
        {RoutineId::ExamTumblerUpdate, &examTumblerUpdate},
        {RoutineId::ExamShieldGunnerUpdate, &examShieldGunnerUpdate},
        {RoutineId::ExamGunnerShotUpdate, &examGunnerShotUpdate},
        {RoutineId::ExamShieldUpdate, &examShieldUpdate},
        {RoutineId::ExamHopperUpdate, &examHopperUpdate},
        {RoutineId::ExamDroneUpdate, &examDroneUpdate},
        {RoutineId::ExamAmbushUpdate, &examAmbushUpdate},
        {RoutineId::ExamMineUpdate, &examMineUpdate},
        {RoutineId::ExamDiggerUpdate, &examDiggerUpdate},
        {RoutineId::ExamAmbushFlyerUpdate, &examAmbushFlyerUpdate},
        {RoutineId::HomingMarkerUpdate, &homingMarkerUpdate},
        {RoutineId::ExamHatchUpdate, &examHatchUpdate},
        {RoutineId::MandaBurrowerUpdate, &mandaBurrowerUpdate},
        {RoutineId::MandaPlatformUpdate, &mandaPlatformUpdate},
        {RoutineId::MandaGateUpdate, &mandaGateUpdate},
        {RoutineId::MandaDebrisPieceUpdate, &mandaDebrisPieceUpdate},
        {RoutineId::WallBreakBurstUpdate, &wallBreakBurstUpdate},
    });
}

} // namespace od::content
