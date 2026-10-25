// The actor classes of content/yosyonke in the class registry (game/content_registry), registered by
// content/registry.cpp. Moved from the registry table of game/actors.cpp (same entries, same order).
#include "content/registry.hpp"

#include "content/yosyonke/arrival.hpp"
#include "content/yosyonke/ice_ruin_appearing_block.hpp"
#include "content/yosyonke/ice_ruin_blade_barrier.hpp"
#include "content/yosyonke/ice_ruin_bubble_trail.hpp"
#include "content/yosyonke/ice_ruin_burrowing_shooter.hpp"
#include "content/yosyonke/ice_ruin_critters.hpp"
#include "content/yosyonke/ice_ruin_flag_barrier.hpp"
#include "content/yosyonke/ice_ruin_herded_creature.hpp"
#include "content/yosyonke/ice_ruin_shrink_block.hpp"
#include "content/yosyonke/abandoned_mine_relic.hpp"
#include "content/yosyonke/abandoned_mine_roll.hpp"
#include "content/yosyonke/roll_follower.hpp"
#include "content/yosyonke/roll_spawners.hpp"
#include "content/yosyonke/st05_st06_props.hpp"
#include "content/yosyonke/st07_hangar_dropship.hpp"
#include "content/yosyonke/st09_kick_ball.hpp"
#include "content/yosyonke/st09_props.hpp"
#include "content/yosyonke/st09_push_block.hpp"
#include "content/yosyonke/st0a_fx.hpp"
#include "content/yosyonke/st0d_critter.hpp"
#include "content/yosyonke/st0e_boss_vehicle.hpp"
#include "content/yosyonke/st0e_carrier_crate.hpp"
#include "content/yosyonke/st0e_debug_beam.hpp"
#include "content/yosyonke/st0e_defended_vehicle.hpp"
#include "content/yosyonke/st0e_servbots.hpp"
#include "content/yosyonke/st0e_spawner.hpp"
#include "content/yosyonke/st0e_turret.hpp"
#include "content/yosyonke/town_props.hpp"
#include "content/yosyonke/town_side_scripts.hpp"
#include "content/yosyonke/town_story_cast.hpp"
#include "content/yosyonke/weapon_model.hpp"

namespace od::content {

using namespace od::port;

void registerYosyonkeClasses(ContentRegistry& r) {
    r.addClasses({
        {RoutineId::HangarDropshipUpdate, &hangarDropshipUpdate},
        // the pool-e0 type-0xa enemy spawners of the area's stages
        {RoutineId::EncounterSwitchUpdate, &encounterSwitchUpdate},
        {RoutineId::StageEffectUpdate, &stageEffectUpdate},
        {RoutineId::BossVehicleUpdate, &bossVehicleUpdate},
        {RoutineId::BossTurretUpdate, &bossTurretUpdate},
        {RoutineId::TurretBombUpdate, &turretBombUpdate},
        {RoutineId::TurretShotUpdate, &turretShotUpdate},
        {RoutineId::BeamGlowUpdate, &beamGlowUpdate},
        {RoutineId::VehicleFireballUpdate, &vehicleFireballUpdate},
        {RoutineId::DefendedVehicleUpdate, &defendedVehicleUpdate},
        {RoutineId::CarrierCrateUpdate, &carrierCrateUpdate},
        {RoutineId::CrateServbotUpdate, &crateServbotUpdate},
        {RoutineId::RiderServbotUpdate, &riderServbotUpdate},
        {RoutineId::FlungServbotUpdate, &flungServbotUpdate},
        {RoutineId::PushBlockUpdate, &pushBlockUpdate},
        {RoutineId::KickBallUpdate, &kickBallUpdate},
        {RoutineId::RollUpdate, &rollUpdate},
        {RoutineId::ShedRollUpdate, &shedRollUpdate},
        {RoutineId::FieldRollUpdate, &fieldRollUpdate},
        {RoutineId::FlutterRollUpdate, &flutterRollUpdate},
        {RoutineId::CabinAnimatedUpdate, &cabinAnimatedUpdate},
        {RoutineId::CabinModelUpdate, &cabinModelUpdate},
        {RoutineId::CabinNpcUpdate, &cabinNpcUpdate},
        {RoutineId::JunkShopCastUpdate, &junkShopCastUpdate},
        {RoutineId::RollRunsOffUpdate, &rollRunsOffUpdate},
        {RoutineId::JoesLabCastUpdate, &joesLabCastUpdate},
        {RoutineId::JunkShopPropUpdate, &junkShopPropUpdate},
        {RoutineId::ShyCritterUpdate, &shyCritterUpdate},
        {RoutineId::RollSpawnerST0DUpdate, &rollSpawnerST0DUpdate},
        {RoutineId::AbandonedMineRollUpdate, &abandonedMineRollUpdate},
        {RoutineId::RollSpawnerST08Update, &rollSpawnerST08Update},
        {RoutineId::RollSpawnerST09Update, &rollSpawnerST09Update},
        {RoutineId::St47PersonUpdate, &st47PersonUpdate},
        {RoutineId::St08PropUpdate, &st08PropUpdate},
        {RoutineId::St09PropUpdate, &st09PropUpdate},
        {RoutineId::PosedFigureUpdate, &posedFigureUpdate},
        {RoutineId::TownTalkPropUpdate, &townTalkPropUpdate},
        {RoutineId::DecorationUpdate, &decorationUpdate},
        {RoutineId::St09BystanderUpdate, &st09BystanderUpdate},
        {RoutineId::St47VisitorUpdate, &st47VisitorUpdate},
        {RoutineId::St47FlyerUpdate, &st47FlyerUpdate},
        {RoutineId::StillModelUpdate, &stillModelUpdate},
        {RoutineId::WeaponModelUpdate, &weaponModelUpdate},
        {RoutineId::DebugBeamUpdate, &debugBeamUpdate},
        // the Abandoned Mine (ST0F)
        {RoutineId::AbandonedMineRelicUpdate, &abandonedMineRelicUpdate},
        // the ice ruin (ST2D / ST2E / ST2F)
        {RoutineId::BladeBarrierUpdate, &bladeBarrierUpdate},
        {RoutineId::ShrinkBlockUpdate, &shrinkBlockUpdate},
        {RoutineId::FlagBarrierUpdate, &flagBarrierUpdate},
        {RoutineId::AppearingBlockUpdate, &appearingBlockUpdate},
        {RoutineId::BurrowingShooterUpdate, &burrowingShooterUpdate},
        {RoutineId::ChargedShotUpdate, &chargedShotUpdate},
        {RoutineId::SceneCritterUpdate, &sceneCritterUpdate},
        {RoutineId::CatchableCritterUpdate, &catchableCritterUpdate},
        {RoutineId::HerdedCreatureUpdate, &herdedCreatureUpdate},
        {RoutineId::BubbleTrailUpdate, &bubbleTrailUpdate},
    });
}

} // namespace od::content
