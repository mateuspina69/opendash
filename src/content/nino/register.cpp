// The actor classes of content/nino in the class registry (game/content_registry), registered by
// content/registry.cpp. Moved from the registry table of game/actors.cpp (same entries, same order).
#include "content/registry.hpp"

#include "content/nino/field_spawners.hpp"
#include "content/nino/nino_ruin_spawners.hpp"

#include "content/calbania/ch3_story.hpp"
#include "content/nino/glyde_attack_cast.hpp"
#include "content/nino/glyde_attack_talkers.hpp"
#include "content/nino/glyde_box_walkers.hpp"
#include "content/nino/glyde_missiles.hpp"
#include "content/nino/glyde_ship_cast.hpp"
#include "content/nino/fly_point.hpp"
#include "content/nino/repair_machine.hpp"
#include "content/nino/screen_ripple.hpp"
#include "content/nino/mission_tank.hpp"
#include "content/nino/st16_crane.hpp"
#include "content/nino/st18_bomber_birdbots.hpp"
#include "content/nino/ch2_field_cast.hpp"
#include "content/nino/ch2_script_cast.hpp"
#include "content/nino/bars_and_gate.hpp"
#include "content/nino/field_enemies.hpp"
#include "content/nino/guardian_cast.hpp"
#include "content/nino/pedestal_item.hpp"
#include "content/nino/ruin_guardian.hpp"
#include "content/nino/blade_trap.hpp"
#include "content/nino/fleeing_reaverbot.hpp"
#include "content/nino/floor_switch.hpp"
#include "content/nino/ruin_effects.hpp"
#include "content/nino/gauge_reaverbots.hpp"
#include "content/nino/homing_shot.hpp"
#include "content/nino/ruin_swimmers.hpp"
#include "content/nino/platform_boss.hpp"
#include "content/nino/st15_cls.hpp"
#include "content/nino/st15_props.hpp"
#include "content/nino/st15_spray.hpp"
#include "content/nino/st18_npc.hpp"
#include "content/nino/st19.hpp"
#include "content/nino/st1a_movers.hpp"
#include "content/nino/water.hpp"
#include "content/nino/carrier_boat.hpp"
#include "content/nino/glyde_ship_arrival.hpp"
#include "content/nino/mission_gunships.hpp"
#include "content/nino/mission_targets.hpp"
#include "content/nino/square_gunship.hpp"
#include "content/nino/st18_gunships.hpp"
#include "content/nino/st18_props.hpp"
#include "content/saul_kada/lava_vents.hpp"

namespace od::content {

using namespace od::port;

void registerNinoClasses(ContentRegistry& r) {
    r.addClasses({
        // the pool-e0 type-0xa enemy spawners of the area's stages
        {RoutineId::SpawnerSt15Update, &spawnerSt15Update},
        {RoutineId::SpawnerSt16Update, &spawnerSt16Update},
        {RoutineId::SpawnerSt17Update, &spawnerSt17Update},
        {RoutineId::SpawnerSt18Update, &spawnerSt18Update},
        {RoutineId::SpawnerSt36Update, &spawnerSt36Update},
        {RoutineId::SpawnerSt37Update, &spawnerSt37Update},
        {RoutineId::SpawnerSt38Update, &spawnerSt38Update},
        {RoutineId::CutsceneGunshipUpdate, &cutsceneGunshipUpdate},
        {RoutineId::ShipFightGunshipUpdate, &shipFightGunshipUpdate},
        {RoutineId::St18StillModelUpdate, &st18StillModelUpdate},
        {RoutineId::St18SunUpdate, &st18SunUpdate},
        {RoutineId::St18SquarePropUpdate, &st18SquarePropUpdate},
        {RoutineId::GlydeShipArrivalUpdate, &glydeShipArrivalUpdate},
        {RoutineId::MissionGunshipUpdate, &missionGunshipUpdate},
        {RoutineId::MissionTargetUpdate, &missionTargetUpdate},
        {RoutineId::CarrierBoatUpdate, &carrierBoatUpdate},
        {RoutineId::SquareGunshipUpdate, &squareGunshipUpdate},
        {RoutineId::GlydeBaseGunshipUpdate, &glydeBaseGunshipUpdate},
        {RoutineId::St17GateUpdate, &st17GateUpdate},
        {RoutineId::St17LeverUpdate, &st17LeverUpdate},
        {RoutineId::St16BarUpdate, &st16BarUpdate},
        {RoutineId::LavaPlumeUpdate, &lavaPlumeUpdate},
        {RoutineId::GroupMemberUpdate, &groupMemberUpdate},
        {RoutineId::St19MachineUpdate, &st19MachineUpdate},
        {RoutineId::BoxWalkerUpdate, &boxWalkerUpdate},
        {RoutineId::WalkerBombUpdate, &walkerBombUpdate},
        {RoutineId::RepairMachineUpdate, &repairMachineUpdate},
        {RoutineId::MachinePartUpdate, &machinePartUpdate},
        {RoutineId::RepairRollUpdate, &repairRollUpdate},
        {RoutineId::MissileShipUpdate, &missileShipUpdate},
        {RoutineId::GlydeMissileUpdate, &glydeMissileUpdate},
        {RoutineId::FlyPointUpdate, &flyPointUpdate},
        {RoutineId::ScreenRippleUpdate, &screenRippleUpdate},
        {RoutineId::RuinGuardianUpdate, &ruinGuardianUpdate},
        {RoutineId::GuardianWakesUpdate, &guardianWakesUpdate},
        {RoutineId::GuardianBeatenUpdate, &guardianBeatenUpdate},
        {RoutineId::GuardianDropsInUpdate, &guardianDropsInUpdate},
        {RoutineId::GuardianDissolvesUpdate, &guardianDissolvesUpdate},
        {RoutineId::FadingCastUpdate, &fadingCastUpdate},
        {RoutineId::PedestalItemUpdate, &pedestalItemUpdate},
        {RoutineId::GuardianBeamUpdate, &guardianBeamUpdate},
        {RoutineId::GuardianOrbUpdate, &guardianOrbUpdate},
        {RoutineId::WaterSplashUpdate, &waterSplashUpdate},
        {RoutineId::PlatformBossUpdate, &platformBossUpdate},
        {RoutineId::BossSparkUpdate, &bossSparkUpdate},
        {RoutineId::LightningBoltUpdate, &lightningBoltUpdate},
        {RoutineId::AimedShotUpdate, &aimedShotUpdate},
        {RoutineId::HomingOrbUpdate, &homingOrbUpdate},
        {RoutineId::FleeingReaverbotUpdate, &fleeingReaverbotUpdate},
        {RoutineId::FloorSwitchUpdate, &floorSwitchUpdate},
        {RoutineId::BladeTrapUpdate, &bladeTrapUpdate},
        {RoutineId::GlowBlobUpdate, &glowBlobUpdate},
        {RoutineId::RuinFlashUpdate, &ruinFlashUpdate},
        {RoutineId::RuinVariantEffectUpdate, &ruinVariantEffectUpdate},
        {RoutineId::GaugeReaverbotUpdate, &gaugeReaverbotUpdate},
        {RoutineId::FloatingMineUpdate, &floatingMineUpdate},
        {RoutineId::GaugeCastUpdate, &gaugeCastUpdate},
        {RoutineId::TorpedoSwimmerUpdate, &torpedoSwimmerUpdate},
        {RoutineId::LungingSwimmerUpdate, &lungingSwimmerUpdate},
        {RoutineId::PressurePlateUpdate, &pressurePlateUpdate},
        {RoutineId::TorpedoUpdate, &torpedoUpdate},
        {RoutineId::HomingShotUpdate, &homingShotUpdate},
        {RoutineId::WaterBubbleControllerUpdate, &waterBubbleControllerUpdate},
        {RoutineId::MachinePartPropUpdate, &machinePartPropUpdate},
        {RoutineId::St17TalkerUpdate, &st17TalkerUpdate},
        {RoutineId::St18TalkerUpdate, &st18TalkerUpdate},
        {RoutineId::ActorGlowUpdate, &actorGlowUpdate},
        {RoutineId::GlydeFlyInShipUpdate, &glydeFlyInShipUpdate},
        {RoutineId::GlydeHoverShipUpdate, &glydeHoverShipUpdate},
        {RoutineId::Script39BirdbotUpdate, &script39BirdbotUpdate},
        {RoutineId::Script3bBirdbotUpdate, &script3bBirdbotUpdate},
        {RoutineId::Script39DescenderUpdate, &script39DescenderUpdate},
        {RoutineId::Script39BlinkerUpdate, &script39BlinkerUpdate},
        {RoutineId::Script39RiserUpdate, &script39RiserUpdate},
        {RoutineId::St18NpcUpdate, &st18NpcUpdate},
        {RoutineId::St1aMoverUpdate, &st1aMoverUpdate},
        {RoutineId::CritterUpdate, &critterUpdate},
        {RoutineId::BoxCarrierUpdate, &boxCarrierUpdate},
        {RoutineId::St15StillModelUpdate, &st15StillModelUpdate},
        {RoutineId::PatrolDroneUpdate, &patrolDroneUpdate},
        {RoutineId::St19CutsceneDroneUpdate, &st19CutsceneDroneUpdate},
        {RoutineId::HoverMachineUpdate, &hoverMachineUpdate},
        {RoutineId::St19TargetUpdate, &st19TargetUpdate},
        {RoutineId::HoverTargetUpdate, &hoverTargetUpdate},
        {RoutineId::HoverDrifterUpdate, &hoverDrifterUpdate},
        {RoutineId::CarrierCrashUpdate, &carrierCrashUpdate},
        {RoutineId::ScriptedWalkerUpdate, &scriptedWalkerUpdate},
        {RoutineId::TakeOffCastUpdate, &takeOffCastUpdate},
        {RoutineId::Script32CastUpdate, &script32CastUpdate},
        {RoutineId::Script37CastUpdate, &script37CastUpdate},
        {RoutineId::Script37FlashUpdate, &script37FlashUpdate},
        {RoutineId::St15SprayUpdate, &st15SprayUpdate},
        {RoutineId::MissionTankUpdate, &missionTankUpdate},
        {RoutineId::St16CraneUpdate, &st16CraneUpdate},
        {RoutineId::St18BomberBirdbotUpdate, &st18BomberBirdbotUpdate},
    });
}

} // namespace od::content
