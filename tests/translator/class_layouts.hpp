// Test-only named native records and their exe layouts.
#pragma once
#include "translator/layout.hpp"

namespace od::port { struct Actor; }
namespace od::port { struct ActorHeader; }
namespace od::port { struct AnimBlend; }
namespace od::port { struct AnimChannel; }
namespace od::port { struct AnimatedActor; }
namespace od::port { struct AppoDahFightFields; }
namespace od::port { struct AppoDahMouth; }
namespace od::port { struct BirdbotBody; }
namespace od::port { struct BladeFields; }
namespace od::port { struct BladeTrailFields; }
namespace od::port { struct BlizzardLayerFields; }
namespace od::port { struct BoxCarrierGrip; }
namespace od::port { struct BoxSpawnerFields; }
namespace od::port { struct BurningFields; }
namespace od::port { struct BurstFields; }
namespace od::port { struct BusterShotFields; }
namespace od::port { struct CastFields; }
namespace od::port { struct ChargeGlowParams; }
namespace od::port { struct ChipFields; }
namespace od::port { struct CloudRingFields; }
namespace od::port { struct ControllerActor; }
namespace od::port { struct CraftFields; }
namespace od::port { struct CrashFlashFields; }
namespace od::port { struct DroppedPropParams; }
namespace od::port { struct DustPuff; }
namespace od::port { struct DustPuffFields; }
namespace od::port { struct EffectActor; }
namespace od::port { struct ElectricSparkFields; }
namespace od::port { struct EngineFlameFields; }
namespace od::port { struct ExplosionFields; }
namespace od::port { struct ExplosionFlash; }
namespace od::port { struct ExtinguisherFields; }
namespace od::port { struct FallingPieceFields; }
namespace od::port { struct FireEffectFields; }
namespace od::port { struct FlameFields; }
namespace od::port { struct FlameTongue; }
namespace od::port { struct FlyPointFields; }
namespace od::port { struct FootstepDustFields; }
namespace od::port { struct FuseFields; }
namespace od::port { struct GateDebrisFields; }
namespace od::port { struct GlowTrailFields; }
namespace od::port { struct GrowingRippleFields; }
namespace od::port { struct GuardianFields; }
namespace od::port { struct GunshipFields; }
namespace od::port { struct HitSparkFields; }
namespace od::port { struct HurtSparksFields; }
namespace od::port { struct IdleTrack; }
namespace od::port { struct LaserBeamFields; }
namespace od::port { struct LaserBlock; }
namespace od::port { struct MeshActor; }
namespace od::port { struct MessageTriggerFields; }
namespace od::port { struct MissileTrailFields; }
namespace od::port { struct OwnerPuffFields; }
namespace od::port { struct PartnerFields; }
namespace od::port { struct PickupFields; }
namespace od::port { struct Player; }
namespace od::port { struct PlayerBonePoint; }
namespace od::port { struct PodCarrierFields; }
namespace od::port { struct PoseMatrix; }
namespace od::port { struct ProjectileActor; }
namespace od::port { struct PuppetFields; }
namespace od::port { struct RiderFollowerFields; }
namespace od::port { struct RiderSparkFields; }
namespace od::port { struct RingFlashFields; }
namespace od::port { struct ServbotFields; }
namespace od::port { struct ShotActor; }
namespace od::port { struct ShotStats; }
namespace od::port { struct SkidSparkFields; }
namespace od::port { struct SlidingLeafFields; }
namespace od::port { struct SmokeColumnFields; }
namespace od::port { struct SparkEmitterParams; }
namespace od::port { struct SparkRingParams; }
namespace od::port { struct StaticActor; }
namespace od::port { struct SteeredPropFields; }
namespace od::port { struct StormRibbonFields; }
namespace od::port { struct StreamPartnerFields; }
namespace od::port { struct SuctionFields; }
namespace od::port { struct SwingLeafFields; }
namespace od::port { struct ThrownPropParams; }
namespace od::port { struct ThrownPuffFields; }
namespace od::port { struct TownNpcFields; }
namespace od::port { struct VacuumShotFields; }
namespace od::port { struct VariantEffectFields; }
namespace od::port { struct WallCrumbleFields; }
namespace od::port { struct WarningRingParams; }
namespace od::port { struct WaterSplashFields; }
namespace od::port::abandoned_mine_relic_state { struct RuinRelicFields; }
namespace od::port::abandoned_mine_roll_state { struct RuinRollFields; }
namespace od::port::appo_dah_a_state { struct GatePuppetFields; }
namespace od::port::appo_dah_a_state { struct GuideFields; }
namespace od::port::appo_dah_b_state { struct AppoDahBombFields; }
namespace od::port::appo_dah_b_state { struct ScriptPuppetFields; }
namespace od::port::attract_effects_state { struct BoltPoint; }
namespace od::port::attract_effects_state { struct LightningFields; }
namespace od::port::bars_and_gate_state { struct BarFields; }
namespace od::port::bars_and_gate_state { struct GateFields; }
namespace od::port::base_alarm_state { struct AlarmFields; }
namespace od::port::birdbot_down_state { struct DownFields; }
namespace od::port::birdbot_soldier_state { struct SoldierFields; }
namespace od::port::blade_trap_state { struct TrapFields; }
namespace od::port::blob_boss_shots_state { struct FallingShotFields; }
namespace od::port::blob_boss_shots_state { struct LobbedBlobFields; }
namespace od::port::blob_boss_state { struct BlobFields; }
namespace od::port::blob_boss_state { struct BlobFightBlock; }
namespace od::port::bomb_critter_state { struct CritterFields; }
namespace od::port::bomb_state { struct BombFields; }
namespace od::port::burning_data_state { struct DataFields; }
namespace od::port::buster_cannon_state { struct CannonFields; }
namespace od::port::buster_cannon_state { struct FlashBurstFields; }
namespace od::port::calbania_cast_state { struct SittingPuppetFields; }
namespace od::port::calbania_cast_state { struct StandingCastFields; }
namespace od::port::calbania_cast_state { struct TownBirdFields; }
namespace od::port::calbania_cast_state { struct WalkingPuppetFields; }
namespace od::port::calbania_spawners_state { struct St1fSpawnerFields; }
namespace od::port::carried_npc_state { struct CarriedNpcFields; }
namespace od::port::carrier_boat_state { struct BoatFields; }
namespace od::port::ch2_field_cast_state { struct CrashCarrierFields; }
namespace od::port::ch2_field_cast_state { struct HoverFields; }
namespace od::port::ch2_field_cast_state { struct WalkerFields; }
namespace od::port::ch2_script_cast_state { struct FlashFields; }
namespace od::port::ch2_script_cast_state { struct TakeOffCastFields; }
namespace od::port::ch2_script_cast_state { struct TalkingCastFields; }
namespace od::port::ch3_script_cast_state { struct Script20AppoDahFields; }
namespace od::port::ch3_script_cast_state { struct Script3cCastFields; }
namespace od::port::ch4_story_state { struct SmokeColumnFields; }
namespace od::port::ch4_story_state { struct SmokePuff; }
namespace od::port::charge_glow_state { struct ChargeGlowFields; }
namespace od::port::charge_state { struct ChargeMarkerFields; }
namespace od::port::charge_state { struct ShockWaveFields; }
namespace od::port::clut_animation_state { struct ClutAnimationFields; }
namespace od::port::clut_animation_state { struct RoomFlagClutFields; }
namespace od::port::crash_landing_cast_state { struct TrailingPropFields; }
namespace od::port::crash_landing_state { struct SkyShipFields; }
namespace od::port::crusher_state { struct CrusherFields; }
namespace od::port::crusher_state { struct EnergyOrbFields; }
namespace od::port::drill_arm_state { struct DrillConeFields; }
namespace od::port::drill_arm_state { struct DrillFields; }
namespace od::port::dropped_item_state { struct ItemFields; }
namespace od::port::elephant_boss_state { struct BossFields; }
namespace od::port::elephant_boss_state { struct BoulderFields; }
namespace od::port::elephant_boss_state { struct CrystalFields; }
namespace od::port::elephant_boss_state { struct DustRingFields; }
namespace od::port::elephant_boss_state { struct ExitLightFields; }
namespace od::port::elephant_boss_state { struct PillarFields; }
namespace od::port::elephant_boss_state { struct RockFields; }
namespace od::port::elephant_boss_state { struct TrailFields; }
namespace od::port::elephant_boss_state { struct TuskBlock; }
namespace od::port::end_cast_state { struct BobbingPropFields; }
namespace od::port::end_cast_state { struct EndCastExtra; }
namespace od::port::end_cast_state { struct GlowDiscFields; }
namespace od::port::end_cast_state { struct GlowFanFields; }
namespace od::port::end_cast_state { struct PuffFields; }
namespace od::port::end_cast_state { struct PulseGlowFields; }
namespace od::port::end_cast_state { struct RingBandFields; }
namespace od::port::end_cast_state { struct TrailFields; }
namespace od::port::enemy_spawners_state { struct SpawnerFields; }
namespace od::port::escape_timer_state { struct EscapeTimerFields; }
namespace od::port::exam_enemies_a_state::gunner { struct Fields; }
namespace od::port::exam_enemies_a_state::gunnerShot { struct Fields; }
namespace od::port::exam_enemies_a_state::hopper { struct Fields; }
namespace od::port::exam_enemies_a_state::shield { struct Fields; }
namespace od::port::exam_enemies_a_state::tumbler { struct Fields; }
namespace od::port::exam_enemies_b_state { struct AmbushFields; }
namespace od::port::exam_enemies_b_state { struct DiggerFields; }
namespace od::port::exam_enemies_b_state { struct DiggerHitBlock; }
namespace od::port::exam_enemies_b_state { struct DroneFields; }
namespace od::port::exam_enemies_b_state { struct FlyerFields; }
namespace od::port::exam_enemies_b_state { struct HatchFields; }
namespace od::port::exam_enemies_b_state { struct MineFields; }
namespace od::port::exam_spawner_state { struct ExamSpawnerFields; }
namespace od::port::exit_transitions_state { struct TeleporterSparkFields; }
namespace od::port::explosions_state { struct BlastFields; }
namespace od::port::extinguisher_state { struct MistFields; }
namespace od::port::extinguisher_state { struct MistMotion; }
namespace od::port::extinguisher_state { struct MistParticle; }
namespace od::port::field_enemies_state { struct CarrierFields; }
namespace od::port::field_enemies_state { struct CritterFields; }
namespace od::port::field_hoverbot_state { struct FieldHoverbotFields; }
namespace od::port::field_reaverbot_state { struct FieldReaverbotFields; }
namespace od::port::field_spawners_state { struct St15ClockFields; }
namespace od::port::field_spawners_state { struct St15GroupFields; }
namespace od::port::field_spawners_state { struct St15WaveFields; }
namespace od::port::field_spawners_state { struct St16CarrierFields; }
namespace od::port::field_spawners_state { struct St16ClockFields; }
namespace od::port::field_spawners_state { struct St16GroupFields; }
namespace od::port::field_spawners_state { struct St17ClockFields; }
namespace od::port::field_spawners_state { struct St17WalkerFields; }
namespace od::port::field_spawners_state { struct St18FightFields; }
namespace od::port::field_spawners_state { struct St18RepairFields; }
namespace od::port::field_spawners_state { struct St18ReturnFields; }
namespace od::port::field_spawners_state { struct St18TimerFields; }
namespace od::port::field_spawners_state { struct WalkerEntry; }
namespace od::port::fire_ruin_bon_cast_state { struct BonCastFields; }
namespace od::port::fire_ruin_cutscene_cast_state { struct DropperFields; }
namespace od::port::fire_ruin_cutscene_cast_state { struct HolderFields; }
namespace od::port::fire_ruin_cutscene_cast_state { struct TickCastFields; }
namespace od::port::fire_ruin_servbot_cast_state { struct ServbotFields; }
namespace od::port::flame_tongues_state { struct TongueFields; }
namespace od::port::fleeing_reaverbot_state { struct ReaverbotFields; }
namespace od::port::floating_device_state { struct FloatingDeviceFields; }
namespace od::port::floor_switch_state { struct SwitchFields; }
namespace od::port::flutter_cabin_state { struct CabinNpcFields; }
namespace od::port::flutter_cabin_state { struct FlutterRollFields; }
namespace od::port::gauge_reaverbots_state { struct CastFields; }
namespace od::port::gauge_reaverbots_state { struct GaugeFields; }
namespace od::port::gauge_reaverbots_state { struct MineFields; }
namespace od::port::glyde_attack_cast_state { struct BlinkerFields; }
namespace od::port::glyde_attack_cast_state { struct DescenderFields; }
namespace od::port::glyde_attack_cast_state { struct RiserFields; }
namespace od::port::glyde_attack_cast_state { struct Script39BirdbotFields; }
namespace od::port::glyde_attack_cast_state { struct Script3bBirdbotFields; }
namespace od::port::glyde_attack_talkers_state { struct TalkerFields; }
namespace od::port::glyde_base_bombers_state { struct GlydeBomberFields; }
namespace od::port::glyde_base_gun_state { struct GunFields; }
namespace od::port::glyde_base_key_state { struct GlydeKeyFields; }
namespace od::port::glyde_base_scene_birdbots_state { struct SceneBirdbotFields; }
namespace od::port::glyde_base_tank_state { struct GlydeTankFields; }
namespace od::port::glyde_base_turret_state { struct TurretFields; }
namespace od::port::glyde_box_walkers_state { struct BombFields; }
namespace od::port::glyde_box_walkers_state { struct WalkerFields; }
namespace od::port::glyde_missiles_state { struct GlowFields; }
namespace od::port::glyde_missiles_state { struct MissileFields; }
namespace od::port::glyde_missiles_state { struct MissileShipFields; }
namespace od::port::ground_crawler_state { struct CrawlerFields; }
namespace od::port::guardian_cast_state { struct CastFields; }
namespace od::port::guardian_reaverbot_state { struct GuardianFields; }
namespace od::port::gun_turret_state { struct TurretFields; }
namespace od::port::homing_missile_state { struct MissileFields; }
namespace od::port::homing_shot_state { struct ShotFields; }
namespace od::port::hopping_drone_state { struct DroneFields; }
namespace od::port::hopping_reaverbot_state { struct HopFields; }
namespace od::port::hunter_seeker_state { struct CrystalFields; }
namespace od::port::hunter_seeker_state { struct HunterFields; }
namespace od::port::hyper_shell_state { struct DarkTrailFields; }
namespace od::port::hyper_shell_state { struct ShellFields; }
namespace od::port::hyper_shell_state { struct TrailPoint; }
namespace od::port::ice_ruin_blade_barrier_state { struct BarrierFields; }
namespace od::port::ice_ruin_bubble_trail_state { struct TrailFields; }
namespace od::port::ice_ruin_burrowing_shooter_state { struct ShooterFields; }
namespace od::port::ice_ruin_burrowing_shooter_state { struct ShotFields; }
namespace od::port::ice_ruin_critters_state { struct CritterFields; }
namespace od::port::ice_ruin_herded_creature_state { struct HerdFields; }
namespace od::port::ice_ruin_shrink_block_state { struct ShrinkFields; }
namespace od::port::island_visit_state { struct PackFields; }
namespace od::port::island_visit_state { struct RecordPointFields; }
namespace od::port::late_cast_state { struct CirclerFields; }
namespace od::port::late_cast_state { struct FlyerFields; }
namespace od::port::late_cast_state { struct HoldingTalkerFields; }
namespace od::port::late_cast_state { struct IdleCastFields; }
namespace od::port::late_cast_state { struct MouthTalkerFields; }
namespace od::port::late_cast_state { struct PacerFields; }
namespace od::port::late_cast_state { struct SceneWalkerFields; }
namespace od::port::late_cast_state { struct St4bCastFields; }
namespace od::port::late_cast_state { struct St54CastFields; }
namespace od::port::lava_pit_state { struct CreatureFields; }
namespace od::port::lava_pit_state { struct FlameFields; }
namespace od::port::lava_pit_state { struct PitFields; }
namespace od::port::lava_vents_state { struct VentsFields; }
namespace od::port::leaping_critter_state { struct CritterFields; }
namespace od::port::lens_flare_state { struct DroppingRingFields; }
namespace od::port::lens_flare_state { struct PointedFields; }
namespace od::port::lens_flare_state { struct PulsingDiscFields; }
namespace od::port::lens_flare_state { struct RayFields; }
namespace od::port::lens_flare_state { struct ShrinkingDiscFields; }
namespace od::port::lift_box_state { struct LiftBoxFields; }
namespace od::port::light_column_state { struct GlowFields; }
namespace od::port::line_of_sight_state { struct LineOfSightFields; }
namespace od::port::lobbed_shot_state { struct LobbedShotFields; }
namespace od::port::machine_gun_state { struct CasingFields; }
namespace od::port::machine_gun_state { struct MachineGunFields; }
namespace od::port::machine_gun_state { struct MuzzleFields; }
namespace od::port::manda_boss_state { struct BeamHeadFields; }
namespace od::port::manda_boss_state { struct BeamSegmentFields; }
namespace od::port::manda_boss_state { struct DebrisFields; }
namespace od::port::manda_boss_state { struct FightFields; }
namespace od::port::manda_boss_state { struct FloaterFields; }
namespace od::port::manda_boss_state { struct GuardianFields; }
namespace od::port::manda_boss_state { struct GuardianHitBoxes; }
namespace od::port::manda_boss_state { struct GuardianFightBlock; }
namespace od::port::manda_boss_state { struct PuppetFields; }
namespace od::port::manda_guardian_state { struct BladeFields; }
namespace od::port::manda_guardian_state { struct ChestFields; }
namespace od::port::manda_guardian_state { struct CloudFields; }
namespace od::port::manda_guardian_state { struct FallingBladeFields; }
namespace od::port::manda_guardian_state { struct GuardianFields; }
namespace od::port::manda_guardian_state { struct HopperFields; }
namespace od::port::manda_guardian_state { struct ShotFields; }
namespace od::port::manda_guardian_state { struct ShotTrailFields; }
namespace od::port::manda_ruin_burrower_state { struct BurrowerFields; }
namespace od::port::manda_ruin_props_state { struct PlatformFields; }
namespace od::port::manda_ruin_scripts_state { struct BurningWreckFields; }
namespace od::port::manda_ruin_scripts_state { struct GroundedFigureFields; }
namespace od::port::mission_gunships_state { struct MissionGunshipFields; }
namespace od::port::mission_tank_state { struct TankFields; }
namespace od::port::mission_targets_state { struct SmokeColumnsFields; }
namespace od::port::nino_return_scripts_state { struct FlyPointFields; }
namespace od::port::nino_ruin_spawners_state { struct RuinSpawnerFields; }
namespace od::port::opening_cast_state { struct CrewFields; }
namespace od::port::opening_cast_state { struct OpeningFlutterFields; }
namespace od::port::opening_cast_state { struct TalkerFields; }
namespace od::port::particle_burst_state { struct BurstFields; }
namespace od::port::particle_burst_state { struct BurstParticle; }
namespace od::port::pickup_eater_state { struct EaterFields; }
namespace od::port::pirate_servbots_state { struct BomberFields; }
namespace od::port::pirate_servbots_state { struct ServbotFields; }
namespace od::port::pirate_tank_state { struct TankFields; }
namespace od::port::placed_models_state { struct PlacedModelFields; }
namespace od::port::placed_models_state { struct TurningModelFields; }
namespace od::port::platform_boss_state { struct BoltFields; }
namespace od::port::platform_boss_state { struct BossFields; }
namespace od::port::platform_boss_state { struct ShotFields; }
namespace od::port::platform_boss_state { struct SparkFields; }
namespace od::port::platform_boss_state { struct SparkParticle; }
namespace od::port::quicksand_state { struct PitParams; }
namespace od::port::race_bonus_state { struct BonusFields; }
namespace od::port::reaverbot_turret_state { struct TurretFields; }
namespace od::port::reaverbot_walker_state { struct WalkerFields; }
namespace od::port::reflector_state { struct ReflectorFields; }
namespace od::port::reflector_state { struct SparkSprayFields; }
namespace od::port::reflector_state { struct SpraySpark; }
namespace od::port::repair_machine_state { struct MachineFields; }
namespace od::port::repair_machine_state { struct PartFields; }
namespace od::port::repair_machine_state { struct PropFields; }
namespace od::port::repair_machine_state { struct RollFields; }
namespace od::port::roll_follower_state { struct RollFields; }
namespace od::port::roll_follower_state { struct ShedRollFields; }
namespace od::port::roll_spawners_state { struct RollSpawnerFields; }
namespace od::port::ruin_blocks_state { struct BlockFields; }
namespace od::port::ruin_effects_state { struct BlobFields; }
namespace od::port::ruin_effects_state { struct FlashFields; }
namespace od::port::ruin_fading_figure_state { struct FigureFields; }
namespace od::port::ruin_guardian_state { struct BeamFields; }
namespace od::port::ruin_guardian_state { struct GuardianFields; }
namespace od::port::ruin_guardian_state { struct OrbFields; }
namespace od::port::ruin_spawners_state { struct DroneSetFields; }
namespace od::port::ruin_spawners_state { struct DropFields; }
namespace od::port::ruin_spawners_state { struct EmitterFields; }
namespace od::port::ruin_spawners_state { struct HopperSetFields; }
namespace od::port::ruin_spawners_state { struct RespawnFields; }
namespace od::port::ruin_spawners_state { struct SpawnerFields; }
namespace od::port::ruin_spawners_state { struct St14RespawnFields; }
namespace od::port::ruin_swimmers_state { struct BigSwimmerFields; }
namespace od::port::ruin_swimmers_state { struct SmallSwimmerFields; }
namespace od::port::ruin_swimmers_state { struct TorpedoFields; }
namespace od::port::sand_burrower_state { struct BurrowerFields; }
namespace od::port::sand_burrower_state { struct BurrowerRecord; }
namespace od::port::sand_trap_state { struct TrapFields; }
namespace od::port::screen_ripple_state { struct RayStarFields; }
namespace od::port::shining_laser_state { struct LaserWallSparkFields; }
namespace od::port::shockwave_state { struct ShockRingFxFields; }
namespace od::port::shockwave_state { struct ShockwaveFields; }
namespace od::port::sign_state { struct SignFields; }
namespace od::port::sky_arc_state { struct SkyArcFields; }
namespace od::port::smoke_puff_state { struct PuffFields; }
namespace od::port::snow_reaverbot_state { struct SnowReaverbotFields; }
namespace od::port::snow_state { struct SnowFields; }
namespace od::port::spark_emitter_state { struct SparkEmitterFields; }
namespace od::port::spread_buster_state { struct SparkleFields; }
namespace od::port::spread_buster_state { struct SpreadFields; }
namespace od::port::square_gunship_state { struct SquareGunshipFields; }
namespace od::port::st00_figures_state { struct TargetFigureFields; }
namespace od::port::st00_figures_state { struct WatchingFigureFields; }
namespace od::port::st02_attract_cast_state { struct FaceTracksFields; }
namespace od::port::st02_attract_cast_state { struct HoldBlock; }
namespace od::port::st02_attract_cast_state { struct HoldOnlyFields; }
namespace od::port::st02_attract_cast_state { struct PartTrackFields; }
namespace od::port::st03_attract_cast_state { struct CarrierFields; }
namespace od::port::st03_attract_cast_state { struct CraftFields; }
namespace od::port::st03_attract_cast_state { struct FlutterFields; }
namespace od::port::st03_attract_cast_state { struct PartBlinkerFields; }
namespace od::port::st09_kick_ball_state { struct BallFields; }
namespace od::port::st09_push_block_state { struct BlockFields; }
namespace od::port::st0a_fx_state { struct StageEffectFields; }
namespace od::port::st0d_critter_state { struct CritterFields; }
namespace od::port::st0e_boss_vehicle_state { struct FireballFields; }
namespace od::port::st0e_boss_vehicle_state { struct VehicleBlock; }
namespace od::port::st0e_boss_vehicle_state { struct VehicleFields; }
namespace od::port::st0e_carrier_crate_state { struct CrateFields; }
namespace od::port::st0e_debug_beam_state { struct DebugBeamFields; }
namespace od::port::st0e_defended_vehicle_state { struct DefendedFields; }
namespace od::port::st0e_servbots_state { struct CrateServbotFields; }
namespace od::port::st0e_servbots_state { struct FlungFields; }
namespace od::port::st0e_servbots_state { struct RiderFields; }
namespace od::port::st0e_spawner_state { struct EncounterSwitchFields; }
namespace od::port::st0e_turret_state { struct BombFields; }
namespace od::port::st0e_turret_state { struct GlowFields; }
namespace od::port::st0e_turret_state { struct TurretBlock; }
namespace od::port::st0e_turret_state { struct TurretFields; }
namespace od::port::st10_cls_state { struct RollerFields; }
namespace od::port::st11_boss_state { struct FighterFields; }
namespace od::port::st11_script2a_state { struct ArrivalCastFields; }
namespace od::port::st12_14_state { struct BlockDustParams; }
namespace od::port::st12_14_state { struct BoulderFields; }
namespace od::port::st12_14_state { struct TumbleFields; }
namespace od::port::st12_14_state { struct TurretFields; }
namespace od::port::st12_14_state { struct WalkerFields; }
namespace od::port::st13_spin_blade_state { struct BladeFields; }
namespace od::port::st14_enemy_state { struct HoverFields; }
namespace od::port::st15_cls_state { struct MemberFields; }
namespace od::port::st15_props_state { struct HoverMachineFields; }
namespace od::port::st15_props_state { struct St19TargetFields; }
namespace od::port::st15_spray_state { struct SprayFields; }
namespace od::port::st15_spray_state { struct SprayPuff; }
namespace od::port::st16_crane_state { struct CraneFields; }
namespace od::port::st18_bomber_birdbots_state { struct BomberFields; }
namespace od::port::st18_gunships_state { struct FightGunshipFields; }
namespace od::port::st18_props_state { struct SunFields; }
namespace od::port::st19_state { struct MachineFields; }
namespace od::port::st1c_cast_state { struct CueBlock; }
namespace od::port::st1c_cast_state { struct GlowBlock; }
namespace od::port::st1c_cast_state { struct RisingPropBlock; }
namespace od::port::st1c_cast_state { struct ShipFields; }
namespace od::port::st1f_birdbot_tank_state { struct BirdbotTankFields; }
namespace od::port::st20_props_state { struct PropFields; }
namespace od::port::st21_props_state { struct LurePieceFields; }
namespace od::port::st21_props_state { struct RisingModelFields; }
namespace od::port::st22_burrower_state { struct BurrowerFields; }
namespace od::port::st22_fire_shots_state { struct FireShotFields; }
namespace od::port::st24_ruin_gate_state { struct GateGuardianFields; }
namespace od::port::st24_script_walker_state { struct WalkerFields; }
namespace od::port::st25_tron_scenes_state { struct FigureFields; }
namespace od::port::st25_tron_scenes_state { struct TankLandingFields; }
namespace od::port::st25_tron_scenes_state { struct WreckFields; }
namespace od::port::st26_patrol_guard_state { struct GuardFields; }
namespace od::port::st27_lava_valve_state { struct ValveFields; }
namespace od::port::st27_mission_state { struct MissionFields; }
namespace od::port::st27_respawner_state { struct RespawnerFields; }
namespace od::port::st27_scroll_glow_state { struct GlowFields; }
namespace od::port::st28_bomb_ribbon_state { struct RibbonFields; }
namespace od::port::st28_cast_props_state { struct ThrownPropFields; }
namespace od::port::st28_fire_bomb_state { struct BombFields; }
namespace od::port::st28_flyer_state { struct FlyerFields; }
namespace od::port::st28_guard_shot_state { struct ShotFields; }
namespace od::port::st28_guard_state { struct GuardFields; }
namespace od::port::st28_gunner_beam_state { struct BeamOwnFields; }
namespace od::port::st28_gunner_bomb_state { struct BombFields; }
namespace od::port::st28_gunner_bomb_state { struct FlameSeed; }
namespace od::port::st28_gunner_boss_state { struct BossFields; }
namespace od::port::st28_gunner_helpers_state { struct HelperFields; }
namespace od::port::st28_lava_boss_state { struct BossFields; }
namespace od::port::st2a_slide_block_state { struct BlockFields; }
namespace od::port::st2b_cannon_boss_state { struct BossFields; }
namespace od::port::st2b_cannon_shell_state { struct ShellFields; }
namespace od::port::st2b_props_state { struct PlateFields; }
namespace od::port::st2b_props_state { struct PotFields; }
namespace od::port::st32_quiz_state { struct QuizCastFields; }
namespace od::port::st33_falling_rock_state { struct RockFields; }
namespace od::port::st33_key_script_state { struct KeyItemFields; }
namespace od::port::st34_flyer_shell_state { struct ShellFields; }
namespace od::port::st34_ruin_flyer_state { struct FlyerFields; }
namespace od::port::st34_ruin_sentry_state { struct SentryFields; }
namespace od::port::st3b_bomb_turret_state { struct TurretFields; }
namespace od::port::st3b_flame_wall_state { struct FlameFields; }
namespace od::port::st3b_glow_state { struct GlowFields; }
namespace od::port::st3c_boss_shot_state { struct HomingFields; }
namespace od::port::st3c_boss_shot_state { struct PieceFields; }
namespace od::port::st3c_gold_boss_state { struct BossBlock; }
namespace od::port::st3c_gold_boss_state { struct BossFields; }
namespace od::port::st3c_gold_shockwave_state { struct WaveFields; }
namespace od::port::st3c_minion_spawner_state { struct SpawnerFields; }
namespace od::port::st3c_minion_state { struct MinionFields; }
namespace od::port::st3c_pirate_cast_state { struct PirateFields; }
namespace od::port::st3c_statue_state { struct PartFields; }
namespace od::port::st3c_statue_state { struct StatueFields; }
namespace od::port::st3e_reports_state { struct SpeakerFields; }
namespace od::port::st40_fx_state { struct RingFields; }
namespace od::port::st40_head_npc_state { struct NpcFields; }
namespace od::port::st41_carrier_pod_state { struct PodFields; }
namespace od::port::st41_enemy_generator_state { struct GeneratorFields; }
namespace od::port::st41_gate_line_state { struct LineFields; }
namespace od::port::st41_hover_gunner_state { struct GunnerFields; }
namespace od::port::st41_pirate_shot_state { struct ShotFields; }
namespace od::port::st41_sliding_door_state { struct DoorFields; }
namespace od::port::st43_roller_state { struct RollerFields; }
namespace od::port::st45_boss_state { struct BlobFields; }
namespace od::port::st45_boss_state { struct BossFields; }
namespace od::port::st45_boss_state { struct MouthShotFields; }
namespace od::port::st45_boss_state { struct PuffFields; }
namespace od::port::st45_boss_state { struct SplashDrop; }
namespace od::port::st45_boss_state { struct SplashFields; }
namespace od::port::st45_boss_state { struct WaveArm; }
namespace od::port::st45_boss_state { struct WaveFields; }
namespace od::port::st45_spawner_state { struct SpawnerFields; }
namespace od::port::st48_charger_state { struct ChargerFields; }
namespace od::port::st48_gunship_state { struct ShipFields; }
namespace od::port::st49_dropship_state { struct CableFields; }
namespace od::port::st49_dropship_state { struct CargoFields; }
namespace od::port::st49_dropship_state { struct DropshipFields; }
namespace od::port::st4a_event_triggers_state { struct EventTriggerFields; }
namespace od::port::st4b_cast_state { struct TalkerFields; }
namespace od::port::st4b_fx_state { struct EyeTrailFields; }
namespace od::port::st4b_fx_state { struct PieceFields; }
namespace od::port::st4b_fx_state { struct VolleyFields; }
namespace od::port::st4b_scripts_state { struct StillFigureFields; }
namespace od::port::st4b_state { struct BossFields; }
namespace od::port::st4b_state { struct BossFightBlock; }
namespace od::port::st4b_state { struct FaceRowCastFields; }
namespace od::port::st4b_state { struct PieceFields; }
namespace od::port::st4b_state { struct WalkInCastFields; }
namespace od::port::st4c_boss_state { struct BeamShotFields; }
namespace od::port::st4c_boss_state { struct BoltFields; }
namespace od::port::st4c_boss_state { struct BossFields; }
namespace od::port::st4c_boss_state { struct ChargeFields; }
namespace od::port::st4c_boss_state { struct FogFields; }
namespace od::port::st4c_boss_state { struct HomingShotFields; }
namespace od::port::st4c_boss_state { struct OrbitShotFields; }
namespace od::port::st4c_boss_state { struct ShotCommon; }
namespace od::port::st4c_boss_state { struct Spark; }
namespace od::port::st4c_boss_state { struct Sparkle; }
namespace od::port::st4c_boss_state { struct SparklesFields; }
namespace od::port::st4c_boss_state { struct SparksFields; }
namespace od::port::st4c_story_state { struct BoneTrailFields; }
namespace od::port::st4c_story_state { struct DefeatedBossFields; }
namespace od::port::st4c_story_state { struct Mote; }
namespace od::port::st4c_story_state { struct MotesFields; }
namespace od::port::st4c_story_state { struct RayBurstFields; }
namespace od::port::st4c_story_state { struct SpriteFields; }
namespace od::port::st53_story_state { struct St53CastFields; }
namespace od::port::st54_cast_state { struct MoverFields; }
namespace od::port::st54_puffs_state { struct Puff; }
namespace od::port::st54_puffs_state { struct PuffEmitterFields; }
namespace od::port::st54_takeoff_state { struct TakeoffFields; }
namespace od::port::st59_state { struct RaceActorFields; }
namespace od::port::st5c_fx2_state { struct BallFxFields; }
namespace od::port::st5c_fx2_state { struct BeamFields; }
namespace od::port::st5c_fx2_state { struct BlastFields; }
namespace od::port::st5c_fx2_state { struct LandingDustFields; }
namespace od::port::st5c_fx3_state { struct ImpactFields; }
namespace od::port::st5c_fx3_state { struct ShockRingFields; }
namespace od::port::st5c_fx_state { struct BallFields; }
namespace od::port::st5c_fx_state { struct BladeFields; }
namespace od::port::st5c_fx_state { struct ChargeFields; }
namespace od::port::st5c_fx_state { struct RockFields; }
namespace od::port::st5c_fx_state { struct StompShotFields; }
namespace od::port::st5c_state { struct BossFields; }
namespace od::port::st5c_state { struct BurstFields; }
namespace od::port::st5c_state { struct BurstSpark; }
namespace od::port::st5c_state { struct DefeatFields; }
namespace od::port::st5c_state { struct IntroFields; }
namespace od::port::status_markers_state { struct DrainBurnMarkerFields; }
namespace od::port::status_markers_state { struct DrainParticle; }
namespace od::port::status_markers_state { struct SlowMarkerFields; }
namespace od::port::subruin_spawners_state { struct DropSpawnerFields; }
namespace od::port::subruin_spawners_state { struct GroupSpawnerFields; }
namespace od::port::subruin_spawners_state { struct LineTriggerFields; }
namespace od::port::talking_npc_state { struct TalkingNpcFields; }
namespace od::port::tank_shell_state { struct TankShellFields; }
namespace od::port::throwable_state { struct ThrowableFields; }
namespace od::port::town_props_state { struct St09PropFields; }
namespace od::port::town_side_scripts_state { struct FlyerFields; }
namespace od::port::town_story_cast_state { struct RunningRollFields; }
namespace od::port::trapped_miner_state { struct MinerFields; }
namespace od::port::travel_state { struct TravelIconFields; }
namespace od::port::treasure_chest_state { struct ChestFields; }
namespace od::port::tron_boss_state { struct TronFields; }
namespace od::port::tron_boss_state { struct TronFightBlock; }
namespace od::port::turret_bullet_state { struct BulletTrailEntry; }
namespace od::port::turret_bullet_state { struct TurretBulletFields; }
namespace od::port::two_form_reaverbot_state { struct FormFields; }
namespace od::port::walker_shot_state { struct WalkerShotFields; }
namespace od::port::wall_debris_state { struct DebrisPiece; }
namespace od::port::wall_debris_state { struct WallDebrisFields; }
namespace od::port::window_giant_state { struct GiantFields; }
namespace od::test {
void layoutValue(LayoutVisitor&, uint32_t, od::port::Actor&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ActorHeader&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::AnimBlend&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::AnimChannel&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::AnimatedActor&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::AppoDahFightFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::AppoDahMouth&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::BirdbotBody&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::BladeFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::BladeTrailFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::BlizzardLayerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::BoxCarrierGrip&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::BoxSpawnerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::BurningFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::BurstFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::BusterShotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::CastFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ChargeGlowParams&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ChipFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::CloudRingFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ControllerActor&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::CraftFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::CrashFlashFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::DroppedPropParams&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::DustPuff&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::DustPuffFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::EffectActor&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ElectricSparkFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::EngineFlameFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ExplosionFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ExplosionFlash&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ExtinguisherFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::FallingPieceFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::FireEffectFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::FlameFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::FlameTongue&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::FlyPointFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::FootstepDustFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::FuseFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::GateDebrisFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::GlowTrailFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::GrowingRippleFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::GuardianFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::GunshipFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::HitSparkFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::HurtSparksFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::IdleTrack&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::LaserBeamFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::LaserBlock&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::MeshActor&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::MessageTriggerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::MissileTrailFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::OwnerPuffFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::PartnerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::PickupFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::Player&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::PlayerBonePoint&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::PodCarrierFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::PoseMatrix&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ProjectileActor&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::PuppetFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::RiderFollowerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::RiderSparkFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::RingFlashFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ServbotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ShotActor&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ShotStats&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::SkidSparkFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::SlidingLeafFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::SmokeColumnFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::SparkEmitterParams&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::SparkRingParams&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::StaticActor&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::SteeredPropFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::StormRibbonFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::StreamPartnerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::SuctionFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::SwingLeafFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ThrownPropParams&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ThrownPuffFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::TownNpcFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::VacuumShotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::VariantEffectFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::WallCrumbleFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::WarningRingParams&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::WaterSplashFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::abandoned_mine_relic_state::RuinRelicFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::abandoned_mine_roll_state::RuinRollFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::appo_dah_a_state::GatePuppetFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::appo_dah_a_state::GuideFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::appo_dah_b_state::AppoDahBombFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::appo_dah_b_state::ScriptPuppetFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::attract_effects_state::BoltPoint&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::attract_effects_state::LightningFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::bars_and_gate_state::BarFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::bars_and_gate_state::GateFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::base_alarm_state::AlarmFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::birdbot_down_state::DownFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::birdbot_soldier_state::SoldierFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::blade_trap_state::TrapFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::blob_boss_shots_state::FallingShotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::blob_boss_shots_state::LobbedBlobFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::blob_boss_state::BlobFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::blob_boss_state::BlobFightBlock&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::bomb_critter_state::CritterFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::bomb_state::BombFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::burning_data_state::DataFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::buster_cannon_state::CannonFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::buster_cannon_state::FlashBurstFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::calbania_cast_state::SittingPuppetFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::calbania_cast_state::StandingCastFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::calbania_cast_state::TownBirdFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::calbania_cast_state::WalkingPuppetFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::calbania_spawners_state::St1fSpawnerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::carried_npc_state::CarriedNpcFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::carrier_boat_state::BoatFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ch2_field_cast_state::CrashCarrierFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ch2_field_cast_state::HoverFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ch2_field_cast_state::WalkerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ch2_script_cast_state::FlashFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ch2_script_cast_state::TakeOffCastFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ch2_script_cast_state::TalkingCastFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ch3_script_cast_state::Script20AppoDahFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ch3_script_cast_state::Script3cCastFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ch4_story_state::SmokeColumnFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ch4_story_state::SmokePuff&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::charge_glow_state::ChargeGlowFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::charge_state::ChargeMarkerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::charge_state::ShockWaveFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::clut_animation_state::ClutAnimationFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::clut_animation_state::RoomFlagClutFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::crash_landing_cast_state::TrailingPropFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::crash_landing_state::SkyShipFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::crusher_state::CrusherFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::crusher_state::EnergyOrbFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::drill_arm_state::DrillConeFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::drill_arm_state::DrillFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::dropped_item_state::ItemFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::elephant_boss_state::BossFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::elephant_boss_state::BoulderFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::elephant_boss_state::CrystalFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::elephant_boss_state::DustRingFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::elephant_boss_state::ExitLightFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::elephant_boss_state::PillarFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::elephant_boss_state::RockFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::elephant_boss_state::TrailFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::elephant_boss_state::TuskBlock&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::end_cast_state::BobbingPropFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::end_cast_state::EndCastExtra&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::end_cast_state::GlowDiscFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::end_cast_state::GlowFanFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::end_cast_state::PuffFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::end_cast_state::PulseGlowFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::end_cast_state::RingBandFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::end_cast_state::TrailFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::enemy_spawners_state::SpawnerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::escape_timer_state::EscapeTimerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::exam_enemies_a_state::gunner::Fields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::exam_enemies_a_state::gunnerShot::Fields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::exam_enemies_a_state::hopper::Fields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::exam_enemies_a_state::shield::Fields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::exam_enemies_a_state::tumbler::Fields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::exam_enemies_b_state::AmbushFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::exam_enemies_b_state::DiggerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::exam_enemies_b_state::DiggerHitBlock&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::exam_enemies_b_state::DroneFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::exam_enemies_b_state::FlyerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::exam_enemies_b_state::HatchFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::exam_enemies_b_state::MineFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::exam_spawner_state::ExamSpawnerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::exit_transitions_state::TeleporterSparkFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::explosions_state::BlastFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::extinguisher_state::MistFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::extinguisher_state::MistMotion&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::extinguisher_state::MistParticle&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::field_enemies_state::CarrierFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::field_enemies_state::CritterFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::field_hoverbot_state::FieldHoverbotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::field_reaverbot_state::FieldReaverbotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::field_spawners_state::St15ClockFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::field_spawners_state::St15GroupFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::field_spawners_state::St15WaveFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::field_spawners_state::St16CarrierFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::field_spawners_state::St16ClockFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::field_spawners_state::St16GroupFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::field_spawners_state::St17ClockFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::field_spawners_state::St17WalkerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::field_spawners_state::St18FightFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::field_spawners_state::St18RepairFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::field_spawners_state::St18ReturnFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::field_spawners_state::St18TimerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::field_spawners_state::WalkerEntry&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::fire_ruin_bon_cast_state::BonCastFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::fire_ruin_cutscene_cast_state::DropperFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::fire_ruin_cutscene_cast_state::HolderFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::fire_ruin_cutscene_cast_state::TickCastFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::fire_ruin_servbot_cast_state::ServbotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::flame_tongues_state::TongueFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::fleeing_reaverbot_state::ReaverbotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::floating_device_state::FloatingDeviceFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::floor_switch_state::SwitchFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::flutter_cabin_state::CabinNpcFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::flutter_cabin_state::FlutterRollFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::gauge_reaverbots_state::CastFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::gauge_reaverbots_state::GaugeFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::gauge_reaverbots_state::MineFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::glyde_attack_cast_state::BlinkerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::glyde_attack_cast_state::DescenderFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::glyde_attack_cast_state::RiserFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::glyde_attack_cast_state::Script39BirdbotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::glyde_attack_cast_state::Script3bBirdbotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::glyde_attack_talkers_state::TalkerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::glyde_base_bombers_state::GlydeBomberFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::glyde_base_gun_state::GunFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::glyde_base_key_state::GlydeKeyFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::glyde_base_scene_birdbots_state::SceneBirdbotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::glyde_base_tank_state::GlydeTankFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::glyde_base_turret_state::TurretFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::glyde_box_walkers_state::BombFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::glyde_box_walkers_state::WalkerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::glyde_missiles_state::GlowFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::glyde_missiles_state::MissileFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::glyde_missiles_state::MissileShipFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ground_crawler_state::CrawlerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::guardian_cast_state::CastFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::guardian_reaverbot_state::GuardianFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::gun_turret_state::TurretFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::homing_missile_state::MissileFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::homing_shot_state::ShotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::hopping_drone_state::DroneFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::hopping_reaverbot_state::HopFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::hunter_seeker_state::CrystalFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::hunter_seeker_state::HunterFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::hyper_shell_state::DarkTrailFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::hyper_shell_state::ShellFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::hyper_shell_state::TrailPoint&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ice_ruin_blade_barrier_state::BarrierFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ice_ruin_bubble_trail_state::TrailFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ice_ruin_burrowing_shooter_state::ShooterFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ice_ruin_burrowing_shooter_state::ShotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ice_ruin_critters_state::CritterFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ice_ruin_herded_creature_state::HerdFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ice_ruin_shrink_block_state::ShrinkFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::island_visit_state::PackFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::island_visit_state::RecordPointFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::late_cast_state::CirclerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::late_cast_state::FlyerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::late_cast_state::HoldingTalkerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::late_cast_state::IdleCastFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::late_cast_state::MouthTalkerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::late_cast_state::PacerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::late_cast_state::SceneWalkerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::late_cast_state::St4bCastFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::late_cast_state::St54CastFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::lava_pit_state::CreatureFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::lava_pit_state::FlameFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::lava_pit_state::PitFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::lava_vents_state::VentsFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::leaping_critter_state::CritterFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::lens_flare_state::DroppingRingFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::lens_flare_state::PointedFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::lens_flare_state::PulsingDiscFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::lens_flare_state::RayFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::lens_flare_state::ShrinkingDiscFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::lift_box_state::LiftBoxFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::light_column_state::GlowFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::line_of_sight_state::LineOfSightFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::lobbed_shot_state::LobbedShotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::machine_gun_state::CasingFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::machine_gun_state::MachineGunFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::machine_gun_state::MuzzleFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_boss_state::BeamHeadFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_boss_state::BeamSegmentFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_boss_state::DebrisFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_boss_state::FightFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_boss_state::FloaterFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_boss_state::GuardianFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_boss_state::GuardianHitBoxes&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_boss_state::GuardianFightBlock&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_boss_state::PuppetFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_guardian_state::BladeFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_guardian_state::ChestFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_guardian_state::CloudFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_guardian_state::FallingBladeFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_guardian_state::GuardianFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_guardian_state::HopperFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_guardian_state::ShotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_guardian_state::ShotTrailFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_ruin_burrower_state::BurrowerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_ruin_props_state::PlatformFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_ruin_scripts_state::BurningWreckFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::manda_ruin_scripts_state::GroundedFigureFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::mission_gunships_state::MissionGunshipFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::mission_tank_state::TankFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::mission_targets_state::SmokeColumnsFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::nino_return_scripts_state::FlyPointFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::nino_ruin_spawners_state::RuinSpawnerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::opening_cast_state::CrewFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::opening_cast_state::OpeningFlutterFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::opening_cast_state::TalkerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::particle_burst_state::BurstFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::particle_burst_state::BurstParticle&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::pickup_eater_state::EaterFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::pirate_servbots_state::BomberFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::pirate_servbots_state::ServbotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::pirate_tank_state::TankFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::placed_models_state::PlacedModelFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::placed_models_state::TurningModelFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::platform_boss_state::BoltFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::platform_boss_state::BossFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::platform_boss_state::ShotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::platform_boss_state::SparkFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::platform_boss_state::SparkParticle&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::quicksand_state::PitParams&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::race_bonus_state::BonusFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::reaverbot_turret_state::TurretFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::reaverbot_walker_state::WalkerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::reflector_state::ReflectorFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::reflector_state::SparkSprayFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::reflector_state::SpraySpark&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::repair_machine_state::MachineFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::repair_machine_state::PartFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::repair_machine_state::PropFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::repair_machine_state::RollFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::roll_follower_state::RollFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::roll_follower_state::ShedRollFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::roll_spawners_state::RollSpawnerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ruin_blocks_state::BlockFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ruin_effects_state::BlobFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ruin_effects_state::FlashFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ruin_fading_figure_state::FigureFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ruin_guardian_state::BeamFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ruin_guardian_state::GuardianFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ruin_guardian_state::OrbFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ruin_spawners_state::DroneSetFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ruin_spawners_state::DropFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ruin_spawners_state::EmitterFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ruin_spawners_state::HopperSetFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ruin_spawners_state::RespawnFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ruin_spawners_state::SpawnerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ruin_spawners_state::St14RespawnFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ruin_swimmers_state::BigSwimmerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ruin_swimmers_state::SmallSwimmerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::ruin_swimmers_state::TorpedoFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::sand_burrower_state::BurrowerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::sand_burrower_state::BurrowerRecord&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::sand_trap_state::TrapFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::screen_ripple_state::RayStarFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::shining_laser_state::LaserWallSparkFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::shockwave_state::ShockRingFxFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::shockwave_state::ShockwaveFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::sign_state::SignFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::sky_arc_state::SkyArcFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::smoke_puff_state::PuffFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::snow_reaverbot_state::SnowReaverbotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::snow_state::SnowFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::spark_emitter_state::SparkEmitterFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::spread_buster_state::SparkleFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::spread_buster_state::SpreadFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::square_gunship_state::SquareGunshipFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st00_figures_state::TargetFigureFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st00_figures_state::WatchingFigureFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st02_attract_cast_state::FaceTracksFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st02_attract_cast_state::HoldBlock&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st02_attract_cast_state::HoldOnlyFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st02_attract_cast_state::PartTrackFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st03_attract_cast_state::CarrierFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st03_attract_cast_state::CraftFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st03_attract_cast_state::FlutterFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st03_attract_cast_state::PartBlinkerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st09_kick_ball_state::BallFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st09_push_block_state::BlockFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st0a_fx_state::StageEffectFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st0d_critter_state::CritterFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st0e_boss_vehicle_state::FireballFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st0e_boss_vehicle_state::VehicleBlock&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st0e_boss_vehicle_state::VehicleFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st0e_carrier_crate_state::CrateFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st0e_debug_beam_state::DebugBeamFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st0e_defended_vehicle_state::DefendedFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st0e_servbots_state::CrateServbotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st0e_servbots_state::FlungFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st0e_servbots_state::RiderFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st0e_spawner_state::EncounterSwitchFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st0e_turret_state::BombFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st0e_turret_state::GlowFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st0e_turret_state::TurretBlock&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st0e_turret_state::TurretFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st10_cls_state::RollerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st11_boss_state::FighterFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st11_script2a_state::ArrivalCastFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st12_14_state::BlockDustParams&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st12_14_state::BoulderFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st12_14_state::TumbleFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st12_14_state::TurretFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st12_14_state::WalkerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st13_spin_blade_state::BladeFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st14_enemy_state::HoverFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st15_cls_state::MemberFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st15_props_state::HoverMachineFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st15_props_state::St19TargetFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st15_spray_state::SprayFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st15_spray_state::SprayPuff&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st16_crane_state::CraneFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st18_bomber_birdbots_state::BomberFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st18_gunships_state::FightGunshipFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st18_props_state::SunFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st19_state::MachineFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st1c_cast_state::CueBlock&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st1c_cast_state::GlowBlock&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st1c_cast_state::RisingPropBlock&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st1c_cast_state::ShipFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st1f_birdbot_tank_state::BirdbotTankFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st20_props_state::PropFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st21_props_state::LurePieceFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st21_props_state::RisingModelFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st22_burrower_state::BurrowerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st22_fire_shots_state::FireShotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st24_ruin_gate_state::GateGuardianFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st24_script_walker_state::WalkerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st25_tron_scenes_state::FigureFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st25_tron_scenes_state::TankLandingFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st25_tron_scenes_state::WreckFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st26_patrol_guard_state::GuardFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st27_lava_valve_state::ValveFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st27_mission_state::MissionFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st27_respawner_state::RespawnerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st27_scroll_glow_state::GlowFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st28_bomb_ribbon_state::RibbonFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st28_cast_props_state::ThrownPropFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st28_fire_bomb_state::BombFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st28_flyer_state::FlyerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st28_guard_shot_state::ShotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st28_guard_state::GuardFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st28_gunner_beam_state::BeamOwnFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st28_gunner_bomb_state::BombFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st28_gunner_bomb_state::FlameSeed&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st28_gunner_boss_state::BossFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st28_gunner_helpers_state::HelperFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st28_lava_boss_state::BossFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st2a_slide_block_state::BlockFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st2b_cannon_boss_state::BossFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st2b_cannon_shell_state::ShellFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st2b_props_state::PlateFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st2b_props_state::PotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st32_quiz_state::QuizCastFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st33_falling_rock_state::RockFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st33_key_script_state::KeyItemFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st34_flyer_shell_state::ShellFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st34_ruin_flyer_state::FlyerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st34_ruin_sentry_state::SentryFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st3b_bomb_turret_state::TurretFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st3b_flame_wall_state::FlameFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st3b_glow_state::GlowFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st3c_boss_shot_state::HomingFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st3c_boss_shot_state::PieceFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st3c_gold_boss_state::BossBlock&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st3c_gold_boss_state::BossFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st3c_gold_shockwave_state::WaveFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st3c_minion_spawner_state::SpawnerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st3c_minion_state::MinionFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st3c_pirate_cast_state::PirateFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st3c_statue_state::PartFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st3c_statue_state::StatueFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st3e_reports_state::SpeakerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st40_fx_state::RingFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st40_head_npc_state::NpcFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st41_carrier_pod_state::PodFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st41_enemy_generator_state::GeneratorFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st41_gate_line_state::LineFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st41_hover_gunner_state::GunnerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st41_pirate_shot_state::ShotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st41_sliding_door_state::DoorFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st43_roller_state::RollerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st45_boss_state::BlobFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st45_boss_state::BossFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st45_boss_state::MouthShotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st45_boss_state::PuffFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st45_boss_state::SplashDrop&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st45_boss_state::SplashFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st45_boss_state::WaveArm&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st45_boss_state::WaveFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st45_spawner_state::SpawnerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st48_charger_state::ChargerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st48_gunship_state::ShipFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st49_dropship_state::CableFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st49_dropship_state::CargoFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st49_dropship_state::DropshipFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4a_event_triggers_state::EventTriggerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4b_cast_state::TalkerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4b_fx_state::EyeTrailFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4b_fx_state::PieceFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4b_fx_state::VolleyFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4b_scripts_state::StillFigureFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4b_state::BossFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4b_state::BossFightBlock&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4b_state::FaceRowCastFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4b_state::PieceFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4b_state::WalkInCastFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4c_boss_state::BeamShotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4c_boss_state::BoltFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4c_boss_state::BossFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4c_boss_state::ChargeFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4c_boss_state::FogFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4c_boss_state::HomingShotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4c_boss_state::OrbitShotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4c_boss_state::ShotCommon&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4c_boss_state::Spark&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4c_boss_state::Sparkle&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4c_boss_state::SparklesFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4c_boss_state::SparksFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4c_story_state::BoneTrailFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4c_story_state::DefeatedBossFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4c_story_state::Mote&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4c_story_state::MotesFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4c_story_state::RayBurstFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st4c_story_state::SpriteFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st53_story_state::St53CastFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st54_cast_state::MoverFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st54_puffs_state::Puff&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st54_puffs_state::PuffEmitterFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st54_takeoff_state::TakeoffFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st59_state::RaceActorFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st5c_fx2_state::BallFxFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st5c_fx2_state::BeamFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st5c_fx2_state::BlastFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st5c_fx2_state::LandingDustFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st5c_fx3_state::ImpactFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st5c_fx3_state::ShockRingFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st5c_fx_state::BallFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st5c_fx_state::BladeFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st5c_fx_state::ChargeFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st5c_fx_state::RockFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st5c_fx_state::StompShotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st5c_state::BossFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st5c_state::BurstFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st5c_state::BurstSpark&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st5c_state::DefeatFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::st5c_state::IntroFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::status_markers_state::DrainBurnMarkerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::status_markers_state::DrainParticle&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::status_markers_state::SlowMarkerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::subruin_spawners_state::DropSpawnerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::subruin_spawners_state::GroupSpawnerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::subruin_spawners_state::LineTriggerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::talking_npc_state::TalkingNpcFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::tank_shell_state::TankShellFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::throwable_state::ThrowableFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::town_props_state::St09PropFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::town_side_scripts_state::FlyerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::town_story_cast_state::RunningRollFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::trapped_miner_state::MinerFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::travel_state::TravelIconFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::treasure_chest_state::ChestFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::tron_boss_state::TronFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::tron_boss_state::TronFightBlock&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::turret_bullet_state::BulletTrailEntry&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::turret_bullet_state::TurretBulletFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::two_form_reaverbot_state::FormFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::walker_shot_state::WalkerShotFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::wall_debris_state::DebrisPiece&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::wall_debris_state::WallDebrisFields&);
void layoutValue(LayoutVisitor&, uint32_t, od::port::window_giant_state::GiantFields&);
bool classes_calbania(LayoutVisitor&, uint32_t, od::port::ActorClassState&);
bool classes_forbidden_island(LayoutVisitor&, uint32_t, od::port::ActorClassState&);
bool classes_game(LayoutVisitor&, uint32_t, od::port::ActorClassState&);
bool classes_game_shots(LayoutVisitor&, uint32_t, od::port::ActorClassState&);
bool classes_intro(LayoutVisitor&, uint32_t, od::port::ActorClassState&);
bool classes_late_game(LayoutVisitor&, uint32_t, od::port::ActorClassState&);
bool classes_manda(LayoutVisitor&, uint32_t, od::port::ActorClassState&);
bool classes_nino(LayoutVisitor&, uint32_t, od::port::ActorClassState&);
bool classes_saul_kada(LayoutVisitor&, uint32_t, od::port::ActorClassState&);
bool classes_subruins(LayoutVisitor&, uint32_t, od::port::ActorClassState&);
bool classes_yosyonke(LayoutVisitor&, uint32_t, od::port::ActorClassState&);
} // namespace od::test
