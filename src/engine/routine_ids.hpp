// Do not edit. The named C++ routines a table's function pointer can name (engine/data_source
// RoutineRef): None for a null pointer and for a routine OpenDash has no C++ routine for.
#pragma once

#include <cstdint>

namespace od {

enum class RoutineId : uint16_t {
    None,
    AbandonedMineBackInHallTurnStep,
    AbandonedMineBackInHallWalkStep,
    AbandonedMineDeviceApproachStep,
    AbandonedMineDeviceMessageStep,
    AbandonedMineDeviceScript, // script 0x54 (states 0x8aa258)
    AbandonedMineDeviceTalkStep,
    AbandonedMineEndScript, // script 0x53 (states 0x8aa100)
    AbandonedMineEntranceScript, // script 0x52 (states 0x8aa098)
    AbandonedMineExitScript,
    AbandonedMineExitTalkStep,
    AbandonedMineGuardianGoneStep,
    AbandonedMineGuardianWakeStep,
    AbandonedMineHallLookStep,
    AbandonedMineHallScript, // script 0x51 (states 0x8a9fe0)
    AbandonedMineHallTalkStep,
    AbandonedMineHallWalkInStep,
    AbandonedMineHallWalkOnStep,
    AbandonedMineLiftArrivalStep,
    AbandonedMineLiftRideStep,
    AbandonedMineLiftScript, // script 0x50 (states 0x8a9e80)
    AbandonedMineRelicUpdate, // ST0F pool 0x20 type 0x55: the relic of the hall (sub 11; content/yosyonke/abandoned_mine_relic)
    AbandonedMineRollTalkStep,
    AbandonedMineRollUpdate, // ST0F pool 0x20 type 0x60: Roll at the ruin's entrance (path walk, talk; content/yosyonke/abandoned_mine_roll)
    AbandonedMineTurnToLiftStep,
    AbandonedMineTurnToRollStep,
    AbandonedMineWalkOntoLiftStep,
    ActorGlowUpdate, // pool 0xa0 stage type 0x10: a glow sprite on an actor (the missile's flare; content/nino/glyde_missiles)
    AimedShotUpdate, // pool 0x40 stage type 0 (ST38): the aimed shot, draw command 0x2c (content/nino/platform_boss)
    AlarmFlashUpdate, // ST31 pool e0 type 0x2a: the base's alarm, siren + red flash (content/calbania/base_alarm; picture alarmFlashDraw)
    AnimatedSpriteEffectUpdate, // ST4C pool a0 type 0x22: animated sprite following its pool-0x20 parent (draw cmd 0x2c) (content/late_game/st4c_story)
    AppearingBlockUpdate, // static ST2F type 0x1f sub 2: a hidden block appearing by flag 0x8ced90[variant] (cell switched, fades in; content/yosyonke/ice_ruin_appearing_block)
    AppoDahBombUpdate, // pool 0x40 ST1F stage type 0x15: the bomb Appo / Dah throw (content/calbania/appo_dah_b)
    AppoDahFightUpdate, // pool 0x20 ST1F type 0x43 sub 0: Appo / Dah in the fight outside Glyde's base (content/calbania/appo_dah_b)
    AppoDahGatePuppetUpdate, // pool 0x20 type 0x43 sub 2 (ST1F): Appo / Dah puppets of scripts 0x1e / 0x1f (content/calbania/appo_dah_a)
    AppoDahGuideUpdate, // pool 0x20 type 0x43 sub 0 (ST21 / ST22, story 8): Appo / Dah guiding through the fields (content/calbania/appo_dah_a)
    AppoDahPuppetUpdate, // pool 0x20 ST20 type 0 sub 1: Appo / Dah as puppets of scripts 0x10 / 0x11 (content/calbania/calbania_cast)
    AppoDahScript1dAppo,
    AppoDahScript1dDah,
    AppoDahScript1fPuppet,
    ArrivalCabinDelayedMessageStep,
    ArrivalCabinScript,
    ArrivalCabinStep1,
    ArrivalCabinStep3,
    ArrivalDescentScript,
    AttractBolt,
    AttractEmitter,
    AttractFlash,
    AttractFlashingEmitter,
    AttractLightningUpdate, // static type 0x15: lightning bolts: attract, content/intro/attract_effects
    AttractRing,
    AttractRingFlashInit,
    AttractRingFlashUpdate, // pool e0 type 0xe: ring / flash: attract, content/intro/attract_effects
    AttractScript3CoverStep,
    AttractScript3FadeInStep,
    AttractScript3FadingQuadStep,
    AttractScript3FinalRingsStep,
    AttractScript3FlippingQuadStep,
    AttractScript3LastRingsStep,
    AttractScript3SceneryStep,
    AttractScript3ScrollStep,
    AttractScript3SecondCastStep,
    AttractScript3SecondFileStep,
    AttractScript3Step10,
    AttractScript3Step11,
    AttractScript3Step8,
    AttractScript3Step9,
    AttractScript3SweepSceneryStep,
    AttractScript3SwitchCastStep,
    AttractScript3ThirdFileStep,
    AttractScript3ToRoom1Step,
    AttractScript3ToRoom2Step,
    AttractScript4ChapterStep,
    AttractScript4ChipsStep,
    AttractScript4DipVoiceStep,
    AttractScript4EngineStep,
    AttractScript4FadeOutStep,
    AttractScript4FirstFileStep,
    AttractScript4FourthFileStep,
    AttractScript4LastBankStep,
    AttractScript4LastFileStep,
    AttractScript4RingWarpStep,
    AttractScript4SceneryKindsStep,
    AttractScript4SecondFileStep,
    AttractScript4ShowActorStep,
    AttractScript4SlowSceneryStep,
    AttractScript4SparklesStep,
    AttractScript4Step17,
    AttractScript4Step18,
    AttractScript4Step19,
    AttractScript4Step22,
    AttractScript4SweepShowStep,
    AttractScript4TexturesStep,
    AttractScript4ThirdFileStep,
    AttractScript4WarpAfterStep,
    AttractStartsScript3,
    AttractVramTileUpdate, // pool e0 type 5: VRAM tile animation: attract, content/intro/attract_effects
    BallChargeUpdate, // ST5C static type 0x5b: attack 10's charge rays (draw records + OT G4; content/late_game/st5c_fx)
    BallExplosionUpdate, // ST5C pool 0x40 type 2: the ball's explosion (0xf0 records; content/late_game/st5c_fx2)
    BallVisualsUpdate, // ST5C static type 0x5e: attack 10's ball visuals (0xf4 records, OT G3 rays; content/late_game/st5c_fx2)
    BaseCameraScript,
    BaseEntranceScript,
    BaseExplosionScript,
    BaseExplosionScriptStepBlowUp,
    BeamChargeUpdate, // ST5C static type 0x60: attack 7's charge rays (draw records + OT G4; content/late_game/st5c_fx)
    BeamGlowUpdate, // ST0E pool 0xa0 stage type 0x1b: glow billboard (variant 1 = the beam glow; content/yosyonke/st0e_turret)
    BeamImpactUpdate, // ST5C static type 0x61: the beam's impact (0xf4 records; content/late_game/st5c_fx3)
    BirdbotDownUpdate, // pool 0x20 type 0x2d sub 0: the knocked-down Birdbot (game/birdbot_down)
    BirdbotSoldierUpdate, // pool 0x20 type 0x2d (ST15..ST17, ST30 / ST31): the Birdbot soldier (game/birdbot_soldier)
    BladeArmShotUpdate, // pool 0x10 type 10: weapon 11, the Blade Arm's swing (game/shots/blade_arm)
    BladeBarrierUpdate, // static type 0x30 (ST2D): spinning blade barrier -- hub / 4 blade segments (hp, respawn) / player blocker by quadrant / debris (content/yosyonke/ice_ruin_blade_barrier)
    BladeTrailUpdate, // pool 0xa0 global type 5: the Blade Arm's trail (game/shots/blade_arm)
    BladeTrapUpdate, // static type 0x45 (ST36..ST38): the patrolling blade trap (content/nino/blade_trap)
    BlastUpdate, // static type 7: a blast ring (game/explosions)
    BlizzardLayerUpdate, // pool a0 stage type 9 (ST02 / ST03 / ST10 / ST11 / ST1D / ST49): the scrolling sky / blizzard layer (drawn by game/weather_draw; game/blizzard_layer)
    BlobBossUpdate, // pool 0x20 type 0x4d (ST2F sub 7 / ST58 sub 1): the blob boss -- scaling body + steered prop (static 0x39), fight / tutorial messages 0x30..0x38 (content/subruins/blob_boss)
    BobberUpdate, // static ST4C type 0x40: a bobbing, spinning object with a hit capsule (content/saul_kada/st4c_bobber)
    BobbingPropUpdate, // ST52/ST1F pool 0x20 types 0x71/0x75: bobbing prop (content/late_game/end_cast)
    BombCritterUpdate, // ST48 pool 0x20 type 0x13: a hovering bomb critter (pops out of the ground, follows the player, fuse + blast; liftable / throwable; content/saul_kada/bomb_critter)
    BombRibbonUpdate, // ST28 pool 0xa0 type 0x15: the flame ribbon between the two orbiting bombs (two OT FT4 quads, dc::otQuadFT4) (content/saul_kada/st28_bomb_ribbon)
    BombServbotUpdate, // pool 0x20 type 0xb sub 1 (ST2A): the bomb-lobbing Servbot, pops out a sub-0 Servbot when beaten (content/saul_kada/pirate_servbots)
    BombTurretUpdate, // ST3B pool 0x20 type 0x12 sub 0: the pirate base's bomb turret (sweeps, tracks the player, lobs 3-bomb volleys; content/saul_kada/st3b_bomb_turret)
    BombUpdate, // static type 0x22: the bomb (ST15 gunships 0x59d310 / ST30 0x59f880 drop it, ST18 bombers carry it; game/bomb)
    BonPartnerUpdate, // pool 0x20 type 0x51 sub 1 (ST27 sub 8): Bon Bonne, picks / carries / throws Reaverbots into the lava (content/saul_kada/st27_bon)
    BonRetreatCastUpdate, // pool 0x20 type 0x51 (ST28 sub 5, story 13): a cast member of script 0x42 (content/saul_kada/fire_ruin_bon_cast)
    BonScriptCastUpdate, // pool 0x20 type 0x51 sub 2 (ST27, story 13): a cast member of scripts 0x40 / 0x41 (cue-driven turns / animation chains; content/saul_kada/fire_ruin_bon_cast)
    BoneRiderUpdate, // ST3A static type 0x47: rides its parent's bone (content/intro/crash_landing)
    BoneTrailUpdate, // ST4C static type 0x5f: 16-point bone trail (ribbon 0x5bdd00 not drawn) (content/late_game/st4c_story)
    BossAfterImageUpdate, // ST5C pool 0x20 type 0x83 sub 2: the boss's after-image (content/late_game/st5c)
    BossBeamUpdate, // ST5C pool 0x40 type 0x2b: attack 7's beam (head + 24 segments; content/late_game/st5c_fx2)
    BossBladeUpdate, // ST5C static type 0x66: the boss's blades, attack 6 (draw records + OT G4; content/late_game/st5c_fx)
    BossDefeatCastUpdate, // ST5C pool 0x20 type 0x83 sub 3: script 0x6d's cast 0x8ad590 (lift / drop; content/late_game/st5c)
    BossEscapeCastUpdate, // ST4B pool 0x20 type 0x3f sub 3: script 0x5b cast, static type-3 trail every 4th frame (content/late_game/st4b_scripts)
    BossFallingRockUpdate, // ST5C pool 0x20 type 0x86: the boss's falling rocks, attack 9 (content/late_game/st5c_fx)
    BossIntroCastUpdate, // ST5C pool 0x20 type 0x83 sub 1: script 0x75's cast 0x8ae5a0 (fade-in, sparks; content/late_game/st5c)
    BossRetreatCastUpdate, // ST4B pool 0x20 type 0x3f sub 2: script 0x5a cast, step-driven, pool-0x40 type-0x30 sparks (content/late_game/st4b_scripts)
    BossShellUpdate, // ST28 pool 0x40 type 0x1c: the lava boss's lobbed shell (ballistic, blast on contact) (content/saul_kada/st28_boss_shell)
    BossShotUpdate, // ST3C pool 0x40 type 0x1f: sub 0 attack 3's homing projectile, sub 1 phase one's thrown pieces (content/saul_kada/st3c_boss_shot)
    BossSparkUpdate, // pool 0x60 type 0x5a (ST38): the boss's spark effects / shot spawners (draw commands; content/nino/platform_boss)
    BossTurretUpdate, // ST0E pool 0x20 type 0x7b: the boss turret (arms / cannons / beam / core; content/yosyonke/st0e_turret)
    BossVehicleUpdate, // ST0E pool 0x20 type 0x7a: the boss vehicle (spawns the turret 0x7b; content/yosyonke/st0e_boss_vehicle)
    BoxCarrierUpdate, // pool 0x20 type 0x49 sub 0 (ST16, spawner 0x41d320): the box carrier -- two Birdbots, fetches/throws type-0x25 boxes, grabs/throws the player (content/nino/field_enemies)
    BoxWalkerUpdate, // pool 0x20 type 0x38 (ST31 sub 0): the grid walkers of Glyde's box room (content/nino/glyde_box_walkers)
    BrakingTrainUpdate, // stage static 0x5b6a60: a model solid of script 0x62 (content/late_game/ch4_story)
    BreakableUpdate, // static ST3B type 1: a breakable crate of the pirate base (HP, blast when destroyed, solid; content/saul_kada/st3b_crate)
    BrotherFreedScript,
    BrotherHeldScript,
    BubbleTrailUpdate, // static ST2F stage type 0x68: the torpedo 0x5cac60 bubble trail (content/yosyonke/ice_ruin_bubble_trail)
    BurningDataSceneBurnt,
    BurningDataSceneHop,
    BurningDataUpdate, // ST1E pool 0x20 type 0x63: Data in the kitchen (content/intro/burning_data)
    BurrowerUpdate, // pool 0x20 type 8 (ST22): the burrower -- rises, walks at / around, throws, flinches, sinks, explodes + drops (content/calbania/st22_burrower)
    BurrowingShooterUpdate, // ST2F pool 0x20 type 0x17: the burrowing shooter (pops up, hops, fires 0x5c5980, liftable; content/yosyonke/ice_ruin_burrowing_shooter)
    BurstUpdate, // pool 0xa0 type 1: a shot's burst (game/hit_effects)
    BusterCannonShotUpdate, // pool 0x10 type 3: weapon 4, the buster cannon (game/shots/buster_cannon)
    BusterShotUpdate, // pool 0x10 type 1: the buster shot (game/shots/buster)
    BystanderScript,
    CabinAnimatedUpdate, // ST04 pool 0x20 type 0x47: the animation by +0xc (content/yosyonke/flutter_cabin)
    CabinLaterVisitsHandler, // ST04 story 0 sub 0's later visits (content/yosyonke/town_rooms cabinVisit)
    CabinModelUpdate, // ST04 static type 1: a placed model (content/yosyonke/flutter_cabin)
    CabinNpcUpdate, // ST04 pool 0x20 type 0x60: an NPC turning its head (content/yosyonke/flutter_cabin)
    CalbaniaArrivalScript,
    CalbaniaDepartureScript,
    CalbaniaDepartureScriptStepBoard,
    CalbaniaDepartureScriptStepFlyOff,
    CalbaniaDepartureScriptStepPlace,
    CalbaniaDepartureScriptStepRise,
    CalbaniaDepartureScriptStepTurn,
    CalbaniaRoomsPokteRoomCallback,
    CalbaniaRoomsSquareRoomCallback,
    CalbaniaRoomsSquareSecondCallback,
    CalbaniaStoryRoomsDockFirst,
    CalbaniaStoryRoomsDockLater,
    CalbaniaStoryRoomsSquareAfterBase,
    CalbaniaStoryRoomsSquareDefence,
    CalbaniaStoryRoomsSquareDeparture,
    CalbaniaStoryRoomsTownLate,
    CalbaniaStoryRoomsTownMiddle,
    CalbaniaStoryRoomsTownStory8,
    CalbaniaStoryRoomsTownStory9,
    CalbaniaTownScriptsFirstVisitStepHello,
    CalbaniaTownScriptsFirstVisitStepTalk,
    CalbaniaTownScriptsFirstVisitStepWait,
    CalbaniaTownScriptsStepFirstLine,
    CalbaniaTownScriptsStepFourthLine,
    CalbaniaTownScriptsStepLastLine,
    CalbaniaTownScriptsStepSecondLine,
    CalbaniaTownScriptsStepThirdLine,
    CameraEaseCosine,
    CameraEaseIn,
    CameraEaseLinear,
    CameraEaseOut,
    CannonBossUpdate, // ST2B pool 0x20 type 0x14 sub 0: the Saul Kada mine boss turret (aimed / lob cannon, boss gauge; content/saul_kada/st2b_cannon_boss)
    CannonShellUpdate, // ST2B pool 0x40 type 8: the boss turret's cannon shell (content/saul_kada/st2b_cannon_shell)
    CarriedNpcUpdate, // pool 0x20 type 0x71 (ST30 sub 2 variant 0x81: the one carried out in the escape; content/calbania/carried_npc)
    CarrierBoatUpdate, // ST17 static type 0x21: the mission's carrier boat (content/nino/carrier_boat)
    CarrierCrashUpdate, // pool 0x20 type 0x49 sub 1 (ST16): the crashing carrier -- drifts until flag 0x690, crashes (content/nino/ch2_field_cast)
    CarrierCrateUpdate, // ST0E static type 0x24: the carrier crate dropped by the boss vehicle (holds a Servbot, liftable / throwable at the vehicle weak point; content/yosyonke/st0e_carrier_crate)
    CarrierPodUpdate, // static ST41 / ST42 type 0x4f: the carrier 0x506d10 pod (fall, sway, lunge with 0xf0 glow, back to the carrier bone; hit entry; content/saul_kada/st41_carrier_pod)
    CastBarrelStill,
    CastBarrelStillB, // the last two alike
    CastBarrelTalks,
    CatchableCritterUpdate, // ST2F pool 0x20 type 0x4a sub 0: the catchable critter (lift / carry / talk -> flags 0x3f1..0x3f3; content/yosyonke/ice_ruin_critters)
    CellBlockerUpdate, // ST10 static type 0x1d: sub 1's cell-blocking statics (content/forbidden_island/island_visit)
    CellSceneBirdbotUpdate, // pool 0x20 type 0x2d sub 2 (ST31): the Birdbots of scripts 0x23 / 0x24 (content/calbania/glyde_base_scene_birdbots)
    Ch2CannonDoneScript,
    Ch2CityGateScript,
    Ch2CraneDoneScript,
    Ch2GunshipDownScript,
    Ch2LandingScript,
    Ch2LookAroundScript,
    Ch2ScriptsCannonLeave,
    Ch2ScriptsCannonTalk,
    Ch2ScriptsCannonTurn,
    Ch2ScriptsCraneCast,
    Ch2ScriptsCraneEnd,
    Ch2ScriptsCraneFace,
    Ch2ScriptsFightBurst,
    Ch2ScriptsFightCast,
    Ch2ScriptsFightLoad,
    Ch2ScriptsFightVoice,
    Ch2ScriptsGateBank,
    Ch2ScriptsGateFade,
    Ch2ScriptsGateSkippable,
    Ch2ScriptsGateWarp,
    Ch2ScriptsGunshipFade,
    Ch2ScriptsLandingLeave,
    Ch2ScriptsLandingStand,
    Ch2ScriptsLandingWalk,
    Ch2ScriptsShipsQuiet,
    Ch2ScriptsShipsTalk,
    Ch2ScriptsTakeOffEngines,
    Ch2ScriptsTakeOffFly,
    Ch2ScriptsTakeOffPuffs,
    Ch2ScriptsTakeOffStart,
    Ch2ShipFightScript,
    Ch2ShipsDoneScript,
    Ch2TakeOffScript,
    Ch4ArrivalEnd,
    Ch4ArrivalScript,
    Ch4ArrivalSetUp,
    Ch4ArrivalStep1,
    Ch4ArrivalStep2,
    Ch4ArrivalStep3,
    Ch4ArrivalStep4,
    Ch4ArrivalStep5,
    Ch4ArrivalStep6,
    Ch4RidingEnd,
    Ch4RidingScript,
    Ch4RidingSetUp,
    Ch4RidingStep1, // the warp to ST53 sub 0
    Ch4RidingStep2, // the column of blasts, the voices, the banner
    Ch4StepRunner,
    Ch4StoryExamEntrance,
    Ch4TrainBrake,
    ChargeBallUpdate, // ST5C pool 0x40 type 0x33: attack 10's ball (content/late_game/st5c_fx)
    ChargeGlowUpdate, // pool a0 stage type 4: the turret's charge glow (game/charge_glow)
    ChargeMarkerUpdate, // static global type 0xe: the charge's marker (game/shots/charge)
    ChargedShotUpdate, // ST2F pool 0x40 type 0xc: 0x479130's charged shot (0xf0 rings / 0x2c billboard; content/yosyonke/ice_ruin_burrowing_shooter)
    ChargerUpdate, // pool 0x20 type 6 sub 0 (ST48): the charger of the Saul Kada pirate attack (spins / boosts with two jets / charges the player, hops, skids, wall bounces, knocked over by heavy hits, dies in blasts + drops; content/saul_kada/st48_charger)
    ClearScreenClosing,
    ClearScreenOpening,
    ClearScreenScript,
    CloudRingUpdate, // ST3A static type 0x12: the clouds (content/intro/crash_landing)
    ClutAnimationUpdate, // pool 0xa0 global type 2: a CLUT animation (game/clut_animation)
    ContentCallbackScript, // script 0x30: the stage callback StageState::script30Callback (the content's)
    CrashBarrelUpdate, // ST3A pool 0x20 type 0x1a: Barrel (content/intro/crash_landing_cast)
    CrashCastCraftLeaving,
    CrashCastCraftParked,
    CrashCastFaceFirst,
    CrashCastFaceLater,
    CrashCastJetCompanion,
    CrashCastJetFighter,
    CrashCastJetLeader,
    CrashCastPropDrifting,
    CrashCastPropFalling,
    CrashCastPropHuge,
    CrashCastPropHugeB,
    CrashCastPropLarge,
    CrashCastPropSmall,
    CrashCastPropTrailing,
    CrashCastRollLate,
    CrashCastRollLater,
    CrashCastRollSub3,
    CrashCastRollTalk,
    CrashCastSecondBodyBlink,
    CrashCastSecondBodyIdle,
    CrashCastSecondBodySpeech,
    CrashCastSecondBodyStill,
    CrashCastSecondBodyTalk,
    CrashCastSpeakerFirst,
    CrashCastSpeakerLater,
    CrashCastTalker,
    CrashCastWatcher,
    CrashCloudPuffs,
    CrashCloudPuffsSmall,
    CrashCloudQuad,
    CrashCloudScroll,
    CrashCloudSprites,
    CrashCloudYawFollow,
    CrashCloudYawFollowOne,
    CrashCraftArriving,
    CrashCraftDiving,
    CrashCraftLanding,
    CrashCraftUpdate, // ST3A pool 0x20 type 0x3f: the flying craft (content/intro/crash_landing_cast)
    CrashFaceUpdate, // ST3A pool 0x20 type 0x21 (content/intro/crash_landing_cast)
    CrashFlashUpdate, // ST3A effect type 7: a flash (content/intro/crash_landing)
    CrashJetUpdate, // ST3A pool 0x20 type 0x74: the jets (content/intro/crash_landing_cast)
    CrashLandingAlarmOffStep,
    CrashLandingAlarmStep,
    CrashLandingBackdropOffStep,
    CrashLandingBurnStep,
    CrashLandingClearStep,
    CrashLandingDebrisStep,
    CrashLandingEscapeStep,
    CrashLandingFlutterStreaksStep,
    CrashLandingScript,
    CrashLandingSecondFileStep,
    CrashLandingShakeStep,
    CrashLandingStep2,
    CrashLandingStep3,
    CrashLandingStreaksStep,
    CrashLandingWaitStep,
    CrashLandingWalkStep,
    CrashPieceDives,
    CrashPieceFalls,
    CrashPieceUpdate, // ST3A pool 0x60: the Flutter's falling pieces (content/intro/crash_landing_cast)
    CrashPropUpdate, // ST3A pool 0x20: the props of script 6 (content/intro/crash_landing_cast)
    CrashRollUpdate, // ST3A pool 0x20 type 0x16: Roll (content/intro/crash_landing_cast)
    CrashSecondBodyUpdate, // ST3A pool 0x20 type 0x2f: a craft's second body (content/intro/crash_landing_cast)
    CrashSpeakerUpdate, // ST3A pool 0x20 type 0x28 (content/intro/crash_landing_cast)
    CrashTalkerUpdate, // ST3A pool 0x20 type 0x39 (content/intro/crash_landing_cast)
    CrashWatcherUpdate, // ST3A pool 0x20 type 0x1d (content/intro/crash_landing_cast)
    CrateServbotUpdate, // ST0E pool 0x20 type 0xb sub 0: the crate Servbot (variant 0 inside, 1 jumps out; content/yosyonke/st0e_servbots)
    CritterUpdate, // pool 0x20 type 0x32 sub 0 (ST15/16): the harmless critter -- hits cost the balance 0xa6d1c0, liftable/throwable, leaves off screen (content/nino/field_enemies)
    CrumblingPlatformUpdate, // static ST41 / ST42 type 0x1f sub 1: crumbling platform after flag 0x155 (3 falling pieces; content/saul_kada/st41_crumbling_platform)
    CrusherShotUpdate, // pool 0x10 type 2: weapon 3, the crusher (game/shots/crusher)
    CrystalShotUpdate, // static global type 0x18: the hunter seeker's crystal (game/shots/hunter_seeker)
    CutsceneGunshipUpdate, // ST18 static type 0x1b sub 2: the scripted gunship of the ST18 cutscenes (content/nino/st18_gunships)
    DarkTrailUpdate, // static global type 5: the hyper shell's dark trail (draw commands; game/shots/hyper_shell)
    DebugBeamUpdate, // pool 0x40 ST0E stage type 0x28: pad-steered debug beam (content/yosyonke/st0e_debug_beam)
    DecorationUpdate, // ST05 / ST06 statics: swaying / spinning decorations by variant (content/yosyonke/st05_st06_props)
    DefendedVehicleUpdate, // ST0E static type 0x4e: the defended vehicle (keeps its gap behind the boss vehicle, signals the turret, target gauge, flags 0x584 / 0x6f0; content/yosyonke/st0e_defended_vehicle)
    DepthsCastUpdate, // pool 0x20 type 0x48 sub 2 (ST28, scripts 0x44 / 0x47): cue-timed walk / turns / rise (content/saul_kada/fire_ruin_cutscene_cast)
    DevRoomNinoDevRoomCallback,
    DevRoomNinoEnd,
    DevRoomNinoMenuStep,
    DevRoomNinoSetUp,
    DevRoomNinoStep0,
    DevRoomRollDevRoomCallback,
    DevRoomRollEnd,
    DevRoomRollMenuStep,
    DevRoomRollSetUp,
    DevRoomRollStep0,
    DoorActorStart,
    DoorActorTransition, // TransitionKind::DoorActor
    DoorActorUpdate, // door actor (talk -> transition kind 0xe)
    DoorLeafDoubleUpdate, // door leaf, double hinged (transition kind 1)
    DoorLeafHingedUpdate, // door leaf, hinged (transition kind 0)
    DoorLeafSlidingUpdate, // door leaf, sliding (transition kind 2)
    DoubleDoorEnd,
    DoubleDoorStart,
    DoubleDoorTransition, // TransitionKind::DoubleDoor
    DrainBurnMarkerUpdate, // static global type 0x12: the player's drain / burn status marker (draw commands; game/status_markers)
    DrillArmShotUpdate, // pool 0x10 type 0x10: weapon 17, the drill arm (game/shots/drill_arm)
    DrillConeUpdate, // static global type 0x19: the drill arm's cone (game/shots/drill_arm)
    DropInWalkerUpdate, // pool 0x20 type 0x1b (ST12): the walker that drops in (walks until blocked, falls bouncing off walls, dies blinking + drops; content/manda/st12_14)
    DroppedItemUpdate, // ST2B / ST3B static type 8: a dropped item (tossed / floating; touched -> flag +0xc, message s8 +0xf; content/saul_kada/dropped_item)
    DroppedPropUpdate, // ST28 static type 0x40 sub 2: the object dropped by cast 0x4d14b0 (+0xc4 carrier; hop, two bounces, spin, content/saul_kada/st28_cast_props)
    DustPuffUpdate, // pool 0xa0 global type 7: a dust puff (game/dust_puff)
    EffectMoverUpdate, // pool 0x20 type 0x85 (ST54): effect-spawning mover (sub 0 rings of type-3 effects, sub 1 type-0x67 emitters / sparks / blast; content/late_game/st54_cast)
    ElectricSparkUpdate, // pool 0xe0 type 6: the explosion's electric spark (OT G4; content/late_game/st5c_fx3)
    ElephantBossUpdate, // ST1D pool 0x20 type 0x34 sub 2: the elephant boss (content/forbidden_island/elephant_boss)
    ElephantCrystalFloat,
    ElephantCrystalInit,
    ElephantCrystalInitPlaced,
    ElephantCutsceneUpdate, // ST1D pool 0x20 type 0x34 sub 3: the elephant of script 0x28 (content/forbidden_island/elephant_boss)
    ElephantPedestalUse,
    ElephantScript28,
    ElephantScript29,
    ElephantScriptArenaStep,
    ElephantScriptCrystalReactsStep,
    ElephantScriptElephantWakesStep,
    ElephantScriptFloorGivesWayStep,
    ElephantScriptStep5, // script 0x29's only step
    ElysiumBeamUpdate, // ST4C pool 0x40 type 0x2d: boss beam at bone 8 (draw cmds 0xf8) (content/late_game/st4c_boss)
    ElysiumBoltUpdate, // ST4C pool 0x40 type 0x2c: boss bolt (capsule; light column 0x11 v2 + blast on impact) (content/late_game/st4c_boss)
    ElysiumBossUpdate, // pool 0x20 type 0x7d sub 0 (ST4C): the Elysium boss -- fade/teleport, rays+homing shots, beam, orbit shots+slam, bolt volley; gauge 0x415d30; death flag 0x681 (content/late_game/st4c_boss)
    ElysiumChargeUpdate, // ST4C static type 0x55: boss charge (v0 glow 0xf4 + disc, v1 rays OT G4) (content/late_game/st4c_boss)
    ElysiumDefeatCastUpdate, // ST4C pool 0x20 type 0x7d sub 2: defeated boss of script 0x6c (sparks 0x22, trails 0x5f, bursts 0x63, motes 0x65, e0 0xf pillars, blast, bone fade) (content/late_game/st4c_story)
    ElysiumFlashUpdate, // pool 0xa0 type 0x1a (ST4C): teleport flash, 0xc0 additive G4 pairs/tick (content/late_game/st4c_boss)
    ElysiumFogUpdate, // pool e0 type 0x26 (ST4C): fog darkens while the boss attacks, then fades back (content/late_game/st4c_boss)
    ElysiumHomingShotUpdate, // ST4C pool 0x40 type 0x27: boss homing shot from its hands (content/late_game/st4c_boss)
    ElysiumIntroCastUpdate, // ST4C pool 0x20 type 0x7d sub 1: boss intro cast (script 0x6b; variant 0x508800, teleport ring 0x43adb0 v2) (content/late_game/st4c_story)
    ElysiumMoteUpdate, // pool 0xa0 type 0x1d (ST4C): rising mote (draw cmds 0xf0) (content/late_game/st4c_boss)
    ElysiumOrbitShotUpdate, // ST4C pool 0x40 type 0x25: boss orbiting shot, then homes (shootable) (content/late_game/st4c_boss)
    ElysiumSparklesUpdate, // ST4C static type 0x57: sparkles around boss bone 8 (content/late_game/st4c_boss)
    ElysiumSparksUpdate, // ST4C static type 0x54: 16 rising sparks (draw cmds 0xf0) (content/late_game/st4c_boss)
    EmptyRoomHandler, // a room handler that is a bare return
    EmptyRoutine, // does nothing
    EncounterSlotsSetup, // the encounter slots' setup (the stage setup's call with the encounter kinds)
    EncounterSwitchUpdate, // ST0E e0 type 0xa: field-encounter formation / screen flag 2 by the player's x (content/yosyonke/st0e_spawner)
    EndDefeatStep1, // the bank swap, the cast 0x8ad590, Megaman at (0x80, 0, 0x208); debris at fixed ticks, a dust
    EndDefeatStep2, // Megaman at (0x80, 0, 0x97) driven by the walk (anims 0x83 / 0x84), then anims 0x85..0x87 + face
    EndDefeatStep3,
    EndDefeatStep4,
    EndDefeatStep5, // the cast 1 / 2 placed (x -0x38 / -0x70, z -0x60), Megaman moved at 0 / 0x3d4
    EndDefeatStep6,
    EndDefeatStep7,
    EndDefeatStep8,
    EndDefeatStep9, // anims 0x8c / 0x8d + face keys; 0x15e: fade 0x1c; then -> the wrap-up
    EndReturnStep1, // the message window 0 at 0x1c8, closed at 0x244; 0x4ce: fade 0x12; -> ST52 sub 1
    EndReturnStep2, // the VRAM flip-book from 0x1f4; 0xb00: fade 0x14; -> ST52 sub 2 at (0, -0x78, 0)
    EndReturnStep3, // Megaman (0, -0x78, 0): 0xdc the casts 0x8a7a20 / 0x8a7a34, anims 0x80..0x82; 0x316 hidden;
    EndReturnStep4, // the cast 4 placed at 0x26c; 0x960: fade 0x14; -> ST52 sub 2 at (0x80, -0x78, 0xdc)
    EndReturnStep5, // Megaman at (0x80, -0x78, 0xdc) anim 0x83, the casts 0x8a7a20 / 0x8a7a34 (+ 0x8a7994 / 0x8a7a5c /
    EndReturnStep6, // the bank swap, Megaman anim 0x89 sub 0xd; anims 0x8a / 0x8b + face keys, the cast 12 / 13
    EndScript3f,
    EndScript6d,
    EndScript6f,
    EndScript75,
    EndScript75Step1, // the cast 0x8ae5a0, Megaman (anim 0x91) waits; then driven forward (anim 0x92 by the floor
    EndScript75Step2, // Megaman placed (0, 0, 0x34) yaw 0, anim 0x93; 0xdc ticks of shake / vibration (the last one
    EndScript75Step3, // 0xf0: the voice faded; once idle: next
    EndStaffRollStep, // script 0x6e's one step (the staff roll)
    EndTakeoffStep1,
    EndTakeoffStep10,
    EndTakeoffStep11,
    EndTakeoffStep12,
    EndTakeoffStep13,
    EndTakeoffStep14,
    EndTakeoffStep15, // before 0x96 the grey ramps up by 2 to 0x80; then fade 0x12 and next. Every tick the sprite 0xb
    EndTakeoffStep2, // the cast 0's phase 1 / 2 with the attachments 0 / 1, 2 / 4, 3; past 0xc8 fade 0x11; -> ST54 sub
    EndTakeoffStep3, // the voice 4; once it plays: next, the cast 1's phase 1
    EndTakeoffStep4,
    EndTakeoffStep5, // 0x28: sound 1; the casts 5 / 4 / 1 by tick; from 0x14e: next with the cast 1..3 placed
    EndTakeoffStep6,
    EndTakeoffStep7, // the casts 2 / 3 by tick; past 0x16d: the voice idle, fade 0x11, -> ST54 sub 2 (fades 0xff / 0x12),
    EndTakeoffStep8, // the voice 5; once it plays: next
    EndTakeoffStep9,
    EndingEffectUpdate, // pool 0xa0 type 0x21: ring band / glow disc / swirl / bone sparkles / trail / puff, variants 0-5 (content/late_game/end_cast)
    EndingGlowCastUpdate, // ST52 pool 0x20 type 0x68: script 0x3f cast + glow ring, variants 0-1 (content/late_game/end_cast)
    EndingGroupCastUpdate, // ST52 pool 0x20 type 0x4c: script 0x3f cast, variants 0-2 (content/late_game/end_cast)
    EndingMouthTalkerUpdate, // ST52 pool 0x20 type 0x1a: script 0x3f cast, talking mouth (content/late_game/end_cast)
    EndingTalkerAUpdate, // ST52 pool 0x20 type 2: script 0x3f cast (content/late_game/end_cast)
    EndingTalkerBUpdate, // ST52 pool 0x20 type 0x16: script 0x3f cast (content/late_game/end_cast)
    EndingWalkerUpdate, // ST52 pool 0x20 type 0x2f: script 0x3f cast (content/late_game/end_cast)
    EnemyGeneratorUpdate, // pool 0x20 type 0x10 (ST41 / ST42): enemy generator (releases type 5 v4 while < 3 alive; HP, explosion, drops; content/saul_kada/st41_enemy_generator)
    EnemyShotUpdate, // pool 0x40 type 9: the enemy shot (the ST3A sky ships', the ST28 guns'; game/enemy_shot)
    EnergyOrbUpdate, // static global type 0x10: the crusher's energy orb (game/shots/crusher)
    EntranceCastUpdate, // pool 0x20 type 0x48 sub 2 (ST27, scripts 0x40 / 0x41): cue-timed turns / walk (content/saul_kada/fire_ruin_cutscene_cast)
    EscapeTimerUpdate, // ST30 pool e0 type 0x22: the escape timer 0xa5a6c8 on screen (content/calbania/escape_timer; picture escapeTimerDraw)
    EventItemGet,
    EventPose, // Pose and PoseThenItem
    EventStandThenItem,
    EventTalk,
    ExamAmbushFlyerUpdate, // ST55 pool 0x20 type 0x67 (0): the ambush flyer of 0x426df0 (content/manda/exam_enemies_b)
    ExamAmbushUpdate, // ST55 pool e0 type 0x21: the type-0x67 ambush records woken by the player (content/manda/exam_enemies_b)
    ExamDiggerUpdate, // ST55 pool 0x20 type 0x53: the burrowing digger (content/manda/exam_enemies_b)
    ExamDoorsScript,
    ExamDroneUpdate, // ST55 pool 0x60 type 0x2b: the exam drone released by the hatches (content/manda/exam_enemies_b)
    ExamGunnerShotUpdate, // pool 0x40 ST55 stage type 0x22: the shield gunner's shot (content/manda/exam_enemies_a)
    ExamHatchUpdate, // ST55 pool 0x20 type 0x4f: the drone hatch (content/manda/exam_enemies_b)
    ExamHopperUpdate, // pool 0x20 type 0x2a (ST55 / ST12..14): the small liftable reaverbot (content/manda/exam_enemies_a)
    ExamMineUpdate, // ST55 pool 0x60 type 4: the floating mine (content/manda/exam_enemies_b)
    ExamRoom1Handler, // the licence exam's room 1 (groups 0 / 7)
    ExamRoom2Handler, // the licence exam's room 2 (groups 2 / 8)
    ExamRoom3Handler, // the licence exam's room 3 (groups 3 / 9)
    ExamRoom4Handler, // the licence exam's room 4 (groups 4 / 0xa)
    ExamRoom5Handler, // the licence exam's room 5 (groups 5 / 0xb)
    ExamRoom6Handler, // the licence exam's room 6 (groups 6 / 0xc)
    ExamShieldGunnerUpdate, // pool 0x20 type 4 (ST55 / ST12..14): the shield-bearing gunner reaverbot (content/manda/exam_enemies_a)
    ExamShieldUpdate, // ST55 static type 5 sub 0: the shield knocked off the gunner (content/manda/exam_enemies_a)
    ExamTumblerUpdate, // pool 0x20 type 0x19 (ST55 / ST12..14): the shelled reaverbot that flips over (content/manda/exam_enemies_a)
    ExitDoorActorSinkStep,
    ExitDoorPushStep,
    ExitDoubleWalkThroughStep,
    ExitHingedWalkThroughStep,
    ExitLeafFollowsHandStep,
    ExitLeafSwingsOnStep,
    ExitLeavesCloseStep,
    ExitLeavesHoldWalkOutStep,
    ExitLeavesOpenStep,
    ExitWalkOffStep,
    ExplosionUpdate, // static type 8: the death explosion (game/explosions)
    ExtinguisherMistUpdate, // static type 0x16: the extinguisher's mist (game/shots/extinguisher)
    ExtinguisherShotUpdate, // pool 0x10 type 14: weapon 15, the extinguisher's stream (game/shots/extinguisher)
    EyeTrailUpdate, // ST4B static type 0x52 sub 0: the boss's eye glow trail (OT ribbon; freed by the boss's eye flags; content/late_game/st4b_fx)
    FadingCastUpdate, // pool 0x20 type 0x24 sub 1 (ST35): script 0x19's cast fading in / out (content/nino/guardian_cast)
    FadingFigureUpdate, // ST27 pool 0x20 type 0x53 sub 1: room-script model (0x8a5618) that waits 0x1e ticks and fades in (colour / blend +0xeb, animation 5 -> 0; content/saul_kada/ruin_fading_figure)
    FallingPieceUpdate, // ST3A pool 0x40 type 0x21: a falling piece of script 6 (content/intro/crash_landing)
    FallingRockUpdate, // static ST33 type 0x3b sub 1: the falling rocks of the guardian boss 0x4c78e0 (stomp rain / quake; shatters into 4, sound 0x14a; content/saul_kada/st33_falling_rock)
    FallingShotUpdate, // ST2F / ST58 pool 0x40 type 0xd: the blob boss's falling shot (0x28 billboard, gravity, spark burst; content/subruins/blob_boss_shots)
    FieldHoverbotUpdate, // pool 0x20 stage type 3 sub 0 (ST0E/10/22): the field hoverbot (game/field_hoverbot)
    FieldReaverbotUpdate, // pool 0x20 stage type 5 sub 0 (ST0D/0E/10/1D/22/2F): the field reaverbot (game/field_reaverbot)
    FieldRollUpdate, // ST0D pool 0x20 type 0x60: Roll following in the field (hit points, lift, the tracks; content/yosyonke/roll_follower)
    FinaleCastAUpdate, // ST5C pool 0x20 type 0x2f: script 0x75 cast, scene states 0-5 (content/late_game/end_cast)
    FinaleCastBUpdate, // ST5C pool 0x20 type 0x68: script 0x75 cast, scene states 0-5 (content/late_game/end_cast)
    FireBombUpdate, // ST28 pool 0x40 type 0x17: the lava boss's fire bomb (v0 homing + liftable, v1 invisible centre of two orbiting v2 bombs; blast on death, the boss may throw again) (content/saul_kada/st28_fire_bomb)
    FireEffectUpdate, // ST1E effect type 0x12: the fire mission's effects (content/intro/flutter_fire)
    FireMissionRoom0,
    FireMissionRoom1,
    FireMissionRoom2,
    FireRuinBeatenScript,
    FireRuinDepthsScript,
    FireRuinDepthsScriptsBeatenSignal,
    FireRuinDepthsScriptsBeatenSkip,
    FireRuinDepthsScriptsBeatenWalk,
    FireRuinDepthsScriptsEdgeCountdown,
    FireRuinDepthsScriptsEdgeStagger,
    FireRuinDepthsScriptsEdgeStepBack,
    FireRuinDepthsScriptsEdgeTurn,
    FireRuinDepthsScriptsEdgeWalkBack,
    FireRuinDepthsScriptsLeavePose,
    FireRuinDepthsScriptsLeaveWait,
    FireRuinDepthsScriptsReturnFaces,
    FireRuinDepthsScriptsReturnStop,
    FireRuinDepthsScriptsReturnWalkBack,
    FireRuinEdgeScript,
    FireRuinEntranceScript,
    FireRuinEntranceScriptsDepthsBlink,
    FireRuinEntranceScriptsDepthsPause,
    FireRuinEntranceScriptsDepthsWalkIn,
    FireRuinEntranceScriptsEntranceEmbers,
    FireRuinEntranceScriptsEntranceFaces,
    FireRuinEntranceScriptsEntranceMouth,
    FireRuinEntranceScriptsEntranceStepUp,
    FireRuinEntranceScriptsEntranceTalk,
    FireRuinEntranceScriptsEntranceWalkIn,
    FireRuinEntranceScriptsGateLook,
    FireRuinEntranceScriptsGateOpen,
    FireRuinEntranceScriptsGatePlaceA,
    FireRuinEntranceScriptsGatePlaceB,
    FireRuinEntranceScriptsGateShake,
    FireRuinEntranceScriptsGateTalk,
    FireRuinEntranceScriptsRubbleCast,
    FireRuinEntranceScriptsRubbleFaces,
    FireRuinEntranceScriptsRubbleFall,
    FireRuinEntranceScriptsRubblePlace,
    FireRuinEntranceScriptsRubbleWalkIn,
    FireRuinEscapeEveScript,
    FireRuinGateScript,
    FireRuinHallScript,
    FireRuinLeaveScript,
    FireRuinReturnScript,
    FireRuinRubbleScript,
    FireRuinVisitScript,
    FireRuinVisitScriptsEveTalk,
    FireRuinVisitScriptsEveWait,
    FireRuinVisitScriptsVisitDrive,
    FireRuinVisitScriptsVisitPadLook,
    FireRuinVisitScriptsVisitPlace,
    FireUpdate, // ST1E static type 0x36: a fire to put out (content/intro/flutter_fire)
    FlagBarrierUpdate, // static ST2D / ST2E type 0x2a: a semi-transparent barrier (solid) with a guard pool-0x20 type 5, gone by flag +0xe (content/yosyonke/ice_ruin_flag_barrier)
    FlameUpdate, // ST1E static type 0x3e: the flames of a fire (content/intro/flutter_fire)
    FlameWallUpdate, // static ST3B type 0x1c: a burning flame wall (timed, stacked hit capsules; OT polygons not drawn; content/saul_kada/st3b_flame_wall)
    FlashBurstUpdate, // static global type 0xb: the buster cannon's flash burst (draw commands, OT spikes; game/shots/buster_cannon)
    FleeingReaverbotUpdate, // pool 0x20 type 0x42 (ST36 / ST38): the small reaverbot that flees and pops up when hit (content/nino/fleeing_reaverbot)
    FlipBookTilesUpdate, // pool e0 type 1 (ST00): 6 flip-book VRAM tiles (rows 0x88be88; game/vram_moves)
    FloatingDeviceUpdate, // pool 0x20 type 0x6f (ST0F / ST4F / ST50 / ST51): the floating device (game/floating_device)
    FloatingMineUpdate, // pool 0x20 type 0x76 sub 1: the floating mine the gauge reaverbot lays (content/nino/gauge_reaverbots)
    FloorSwitchUpdate, // ST36 static type 1: the floor switch (variant 0) and its gates (variants 1 / 2) (content/nino/floor_switch)
    FlungServbotUpdate, // ST0E pool 0x20 type 0x2d: the Servbot flung out of a broken turret part (variant 0x41; runs around, liftable / throwable; content/yosyonke/st0e_servbots)
    FlutterCabinCabinNpc,
    FlutterRollUpdate, // ST04 sub 0 type 0x60: Roll in the Flutter (content/yosyonke/flutter_cabin)
    FlyPointUpdate, // pool 0xe0 type 0x1c: a point flying to a target leaving a0 type-0 actors; at the end a static type 7 + flag +0xe (end-of-mission scripts; content/nino/fly_point)
    FlyerShellUpdate, // ST27 / ST34 pool 0x20 type 0x67 sub 0: dormant ruin enemy (drops when the player is within 0x700 or hit, circles, lobs pool-0x40 type-5 shots; liftable / throwable; dying releases the 0x4edff0 flyer + sparks; content/saul_kada/st34_flyer_shell)
    FlyerUpdate, // pool 0x20 type 0x10 sub 0 (ST28): the flyer -- appears behind the player, bobs, stunned drops pickups / a type-5 reaverbot, explodes (content/saul_kada/st28_flyer)
    FootstepDustUpdate, // static type 0x32 (ST45 / ST33): the footstep dust of exe 0x5ad170 (game/footstep_dust)
    FrozenTransition, // TransitionKind::Frozen
    GateDebrisUpdate, // pool 0xa0 ST3B stage type 0x1c: script 0x5c gate debris FT4 (content/saul_kada/st3b_script5c_cast)
    GateFigureUpdate, // ST3B pool 0x20 type 0 sub 1: script 0x5c cast (record 0x8ab634), cue animations, follows script actor 0 (content/saul_kada/st3b_script5c_cast)
    GateLineUpdate, // pool e0 type 0x2a (ST41 / ST42): a gate line of 6 posts (static 0x56 v2); crossing it within 0x200 sets flag 0x155, sound 0x2bf (content/saul_kada/st41_gate_line)
    GaugeCastUpdate, // pool 0x20 type 0x76 sub 2 (ST37): script 0x5f's cast (content/nino/gauge_reaverbots)
    GaugeReaverbotUpdate, // pool 0x20 type 0x76 sub 0 (ST37 / ST44 / ST50): the gauge reaverbot of ST37 sub 6 (content/nino/gauge_reaverbots)
    GlowBlobUpdate, // pool 0x40 stage type 0x20 (ST36 / ST38 / ST50): a falling glow blob, draw command 0x2c (content/nino/ruin_effects)
    GlowTrailUpdate, // pool a0 stage type 6: a glow riding an actor / its trail (game/glow_trails)
    GlydeBaseBomberUpdate, // pool 0x20 type 0x2d sub 3 (ST30): the bomb-carrying Birdbots of the base's waves (content/calbania/glyde_base_bombers)
    GlydeBaseGunUpdate, // pool 0x20 type 0x41 (ST30 sub 1) / 0x65: the gun emplacement -- lobbed shells and shots (content/calbania/glyde_base_gun)
    GlydeBaseGunshipUpdate, // ST30 static type 0x1b sub 3: the gunships of the wave over Glyde's base (the 8th down -> flag 0x20 + the key; content/nino/mission_gunships)
    GlydeBaseKeyUpdate, // ST30 static type 0x42: the base's key items (flags 0x3e5..0x3e7; content/calbania/glyde_base_key)
    GlydeBaseRoomsBoxRoom,
    GlydeBaseRoomsCell,
    GlydeBaseRoomsEntrance,
    GlydeBaseRoomsGate,
    GlydeBaseRoomsSub0,
    GlydeBaseRoomsSub1,
    GlydeBaseSceneBirdbotUpdate, // pool 0x20 type 0x2d sub 2 (ST30): the Birdbots of the base's scripted scenes (content/calbania/glyde_base_scene_birdbots)
    GlydeBaseScriptsStepAlarm,
    GlydeBaseScriptsStepAlarmNear,
    GlydeBaseScriptsStepCastLeaves,
    GlydeBaseScriptsStepCastTalks,
    GlydeBaseScriptsStepCastWalksIn,
    GlydeBaseScriptsStepFound,
    GlydeBaseScriptsStepGuardsCue,
    GlydeBaseScriptsStepHeard,
    GlydeBaseScriptsStepInside,
    GlydeBaseScriptsStepPlayerAnswers,
    GlydeBaseScriptsStepRunOff,
    GlydeBaseScriptsStepTalk,
    GlydeBaseScriptsStepThanks,
    GlydeBaseScriptsStepTowerFalls,
    GlydeBaseScriptsStepWalkIn,
    GlydeBaseScriptsStepWalkOut,
    GlydeBaseTankUpdate, // pool 0x20 type 0x40 sub 1 (ST30 sub 2): Glyde's tanks; the last one -> flag 0x22 + the key (content/calbania/glyde_base_tank)
    GlydeBaseTurretUpdate, // pool 0x20 type 0x57 (ST30 sub 2): the wall turrets of Glyde's base (content/calbania/glyde_base_turret)
    GlydeFlyInShipUpdate, // pool 0x20 type 0x54 (ST17 script 0x39): Glyde's ship flying in, cutscene cast (content/nino/glyde_ship_cast)
    GlydeHoverShipUpdate, // pool 0x20 type 0x54 (ST18 scripts 0x3a / 0x3b): Glyde's ship, still / effects / blasts / destroyed / hover (content/nino/glyde_ship_cast)
    GlydeMissileUpdate, // ST18 stage static type 0x24: Glyde's missile, caught and thrown back (content/nino/glyde_missiles)
    GlydeShipArrivalUpdate, // ST30 sub 2 static type 0x1b during script 0x25: Glyde's ship arriving (content/nino/glyde_ship_arrival)
    GoldPieceUpdate, // static ST3C type 0x53: sub 0 the boss's thrown gold, sub 1 the electric ring + lightning of a thrown piece (dc::otQuadG4 / otLineG2; content/saul_kada/st3c_gold_piece)
    GoldShockwaveUpdate, // static ST3C type 0x5d: sub 0 the dust / shock spawner (variants 0..2), subs 1 / 2 the growing shock-wave models (content/saul_kada/st3c_gold_shockwave)
    GoldStatueBossUpdate, // ST3C pool 0x20 type 0x73 sub 0: Teisel's gold-statue boss (story 12, ST3C sub 2; two phases, sets flag 0x6f at its end; content/saul_kada/st3c_gold_boss)
    GreyFigureUpdate, // pool 0x20 type 0xd sub 1 (ST3B): still grey model, hp 1 (content/saul_kada/st3b_grey_figure)
    GroundCrawlerShotUpdate, // pool 0x10 type 6: weapon 7, the ground crawler (game/shots/ground_crawler)
    GroupMemberUpdate, // pool 0x20 type 0x3d (ST15 / ST16 sub 0): group member that walks the node graph, sprays at the variant's points (pool-0x40 type 0x13), carries the group's boxes / rides the leader, knock-back on hits, lifted / carried / thrown, runs off when hp is out (content/nino/st15_cls)
    GuardShotUpdate, // ST28 pool 0x40 type 0x1b: the fire-ruin guard's lobbed second shot (content/saul_kada/st28_guard_shot)
    GuardianBeamUpdate, // pool 0x60 type 0x56: a beam (draw commands + OT quads; content/nino/ruin_guardian)
    GuardianBeatenUpdate, // pool 0x20 type 0x3a sub 2 (ST35): the guardian in script 0x16 (content/nino/guardian_cast)
    GuardianDissolvesUpdate, // pool 0x20 type 0x3a sub 4 (ST35): the guardian dissolving, script 0x19 (OT star; content/nino/guardian_cast)
    GuardianDropsInUpdate, // pool 0x20 type 0x3a sub 3 (ST35): the guardian dropping in, script 0x17 (content/nino/guardian_cast)
    GuardianOrbUpdate, // pool 0x40 type 0x26 (ST35): the guardian's orbs / mines (draw commands; content/nino/ruin_guardian)
    GuardianReaverbotUpdate, // pool 0x20 stage type 7 (ST0F sub 12 / ST51): the guardian reaverbot (game/guardian_reaverbot)
    GuardianUpdate, // pool 0x20 type 0x45 (ST33 sub 0, story 13): the ruin's guardian figures (stomp + shake, glowing eyes, animation chains; content/saul_kada/st33_guardian)
    GuardianWakesUpdate, // pool 0x20 type 0x3a sub 1 (ST35): the guardian in script 0x15 (content/nino/guardian_cast)
    GunnerBeamUpdate, // ST28 pool 0x40 type 0x19: the fire-ruin gunner boss's beam (variant 0 aiming ray of 0xf0 dots, 1 homing shot + caught effect; content/saul_kada/st28_gunner_beam)
    GunnerBombUpdate, // ST28 pool 0x40 type 0x18: the fire-ruin gunner boss's bomb (flight, then 13 flame billboards, 0x2c draw commands + hit entries; content/saul_kada/st28_gunner_bomb)
    GunnerBossUpdate, // ST28 pool 0x20 type 0x51 sub 0: the fire ruin's boss (advance / retreat home / shot volley / spray / beam / bombs; content/saul_kada/st28_gunner_boss)
    GunnerHelperUpdate, // ST28 pool 0x20 type 0xb sub 1: the fire-ruin gunner boss's two helpers (idle beside it, lobbed shot throws, attack when the beam catches the player, knocked out / liftable / throwable; content/saul_kada/st28_gunner_helpers)
    HangarDropshipUpdate, // ST07 sub 1 type 0x6d: the dropship in the Flutter's hangar (content/yosyonke/st07_hangar_dropship)
    HeadNpcUpdate, // pool 0x20 type 0 sub 1 (ST40): a standing NPC whose head turns toward the player / a point (content/saul_kada/st40_head_npc)
    HeldItemUpdate, // static ST4B type 0x40: the item Megaman holds (variant 0 placed at his hand each tick; content/saul_kada/st4b_cast)
    HerdedCreatureUpdate, // ST2F pool 0x20 type 0x34 sub 5: the herded creature (pens 0x275..0x277; content/yosyonke/ice_ruin_herded_creature)
    HiddenReaverbotUpdate, // ST1D pool e0 type 0x24: a reaverbot hidden in the snow (content/forbidden_island/island_visit)
    HingedDoorEnd,
    HingedDoorStart,
    HingedDoorTransition, // TransitionKind::HingedDoor
    HitMuteUpdate, // pool 0xe0 global type 0x18: hit classes muted for a while (content/manda/manda_guardian)
    HitSparkUpdate, // pool 0xa0 type 0: the hit spark (game/hit_effects)
    HomingMarkerUpdate, // pool 0xa0 type 6 (ST55 / Nino): the ring marker at the homing point (content/manda/exam_enemies_b)
    HomingMissileShotUpdate, // pool 0x10 type 5: weapon 6, the homing missile (game/shots/homing_missile)
    HomingOrbUpdate, // pool 0x40 stage type 0x2f (ST38): the homing orb, draw command 0xf0 (content/nino/platform_boss)
    HomingShotUpdate, // pool 0x40 stage type 0x23: the gauge reaverbot's homing shot, draw command 0x2c (content/nino/homing_shot)
    HoppingDroneUpdate, // ST13: the hopping drone (pool 0x20 type 0x31 sub 1, content/manda/hopping_drone)
    HoppingReaverbotUpdate, // Saul Kada hopping Reaverbot (pool 0x20 type 0x64: hops/chases/jump-attacks, liftable+throwable, content/saul_kada/hopping_reaverbot)
    HoverDrifterUpdate, // pool 0x20 type 0x30 sub 0 (ST46): the hovering drifter (variants 0..2) (content/nino/ch2_field_cast)
    HoverGunnerUpdate, // pool 0x20 type 0x7d (ST41 sub 12 / ST42 subs 7, 10): enemy that aims / closes in on the player, 3 attack routines spawning pool-0x40 type-0x31 shots, rise / fall-land routines, dies + drops (content/saul_kada/st41_hover_gunner)
    HoverMachineUpdate, // static ST17: hovering machine (3 capsules, fires, rocking, explosion) (content/nino/st15_props)
    HoverReaverbotUpdate, // pool 0x20 type 3 (ST14): the hovering reaverbot (drifts around home, dives at the player, returns; liftable / throwable; falls + explodes; content/manda/st14_enemy)
    HoverTargetUpdate, // pool 0x20 type 0x30 sub 1 (ST17): the hovering target -- capsules, smoke, falls (id 0xd) (content/nino/ch2_field_cast)
    HunterSeekerShotUpdate, // pool 0x10 type 15: weapon 16, the hunter seeker (game/shots/hunter_seeker)
    HurtSparksUpdate, // static type 13: the player's hurt sparks (game/hit_effects)
    HyperShellShotUpdate, // pool 0x10 type 4: weapon 5, the hyper shell (game/shots/hyper_shell)
    IcePlayerMarkerUpdate, // pool e0 type 1 (ST2D / 2E / 2F): marks the player (+0xbd = 1, FUN_00571ee0) on the stage / on type-0x1f statics (content/saul_kada/ice_floor_marker)
    IceRuinCritterScene, // 0x49
    IceRuinScriptsCueStep,
    IceRuinScriptsTutorialSceneStep,
    IceRuinScriptsTutorialStep,
    IceRuinScriptsWalkStep,
    IceRuinShrinkScript, // 0x48
    IceRuinTutorialScene, // 0x4b
    IceRuinTutorialScript, // 0x4a
    IslandBlownScript,
    IslandBlownScriptStepBlasts,
    IslandBlownScriptStepWarp,
    IslandDecorationUpdate, // ST10 type 0x37: sub 1's two-part decoration (content/forbidden_island/island_visit)
    IslandRoomsSt3eRoomCallback,
    IslandVisitRecordsRoomHandler, // every tick (no state test in the exe)
    IslandVisitStory2Sub2RoomHandler, // story 2.. sub 2
    JoesLabCastUpdate, // ST08 sub 1 pool 0x20 type 0 sub 3: Joe's-lab scene cast (script 0x4f, content/yosyonke/town_story_cast)
    JoesLabRoomHandler,
    JoesLabScript,
    JunkShopCastUpdate, // ST0A pool 0x20 type 0 sub 1: the junk-shop scene cast (script 0x4d, content/yosyonke/town_story_cast)
    JunkShopPropUpdate, // ST0A static type 1 sub 1 (variant 0 slides in during script 0x4d, content/yosyonke/town_story_cast)
    JunkShopRoomHandler,
    JunkShopScript,
    KeyItemUpdate, // static ST33 type 0x40: the key item of script 0x2f; touched -> item-get event msg 0x1e + flag 0x3ed (door locks off; content/saul_kada/st33_key_script)
    KeyScriptFigureUpdate, // pool 0x20 type 0x45 sub 2 (ST33): script 0x2f's guardian (content/saul_kada/st33_key_script)
    KickBallUpdate, // ST09 static: the kick ball of the goal game (kicks cost zenny, goals pay + message; content/yosyonke/st09_kick_ball)
    LadderTransition, // TransitionKind::Ladder
    LaserWallSparkUpdate, // pool 0xa0 global type 9: the laser's wall spark (game/shots/shining_laser)
    LateRoomsSt40SoundHelper,
    LavaBossUpdate, // pool 0x20 type 0x48 sub 0 (ST28 sub 0, story 13/14): the fire ruin's lava boss -- walk/charge/stomp, fire bombs (0x40 type 0x17), lobbed shells (0x40 type 0x1c), liftable/throwable, lava sinks it, boss gauge, death flags 0x55/0x53 (content/saul_kada/st28_lava_boss)
    LavaFlowUpdate, // pool e0 type 0x1a (ST27 sub 8): the fire ruin's flowing lava texture (VRAM image moves, stops with flag 0x683; game/vram_moves)
    LavaPlumeUpdate, // the lava plume (pool 0x60 type 0x2e: 8 rising puffs, the mission targets' smoke states; content/nino/mission_targets)
    LavaValveMissionUpdate, // pool e0 type 0x17 (ST27 sub 8, story 13): the lava-valve mission controller -- spawns the valve, Tron / Bon fighting classes, up to 6 Reaverbots; valve down -> flag 0x4c (content/saul_kada/st27_mission)
    LavaVentsUpdate, // Maybe_LavaVentsUpdate (pool e0 type 0x16: ST27 / 28 / 34 lava floor burn + plumes, content/saul_kada/lava_vents)
    LeapingCritterUpdate, // pool 0x20 (ST2D sub 1 / ST2E sub 1 / ST4F sub 7): the leaping critter -- wanders / leaps at the player / follows a path, liftable + throwable, dies + drops or times out (content/subruins/leaping_critter)
    LensFlareUpdate, // ST1C/ST54 pool a0 stage type 0x19: sun lens flare (var 0) + rings/discs/rays/tint/sprite vars 1-6 (dc::emit 0xf4/0xf0/0x2c, otQuadG4; var 5 TILE not drawn; game/lens_flare)
    LiftBoxUpdate, // static type 0x25: the box you lift / carry / throw and stand on (ST16 ...; game/lift_box)
    LightColumnUpdate, // static stage type 0x11: a light column (game/light_column)
    LightGlowUpdate, // pool a0 stage type 0x13: a light column's glow / its motes (game/light_column)
    LightRingUpdate, // pool 0xa0 type 0x14 (ST26 / 27 / 34 / 40 / 41 / 42): a rising 0xf0 ring record, 8 ticks (dc::emit; content/saul_kada/ruin_light_ring)
    LightningBoltUpdate, // pool 0xe0 type 0xf (ST38): a lightning bolt on its parent (OT quads; content/nino/platform_boss)
    LineOfSightUpdate, // pool 0xe0 type 7: an actor's line-of-sight probe (game/line_of_sight)
    LobbedBlobUpdate, // ST2F / ST58 pool 0x40 type 0x1d: the blob boss's lobbed blob / spray (model variant 0, lands, flattens + sinks; content/subruins/blob_boss_shots)
    LobbedShotUpdate, // pool 0x40 stage type 7: a lobbed shot / bomb (game/lobbed_shot)
    LungingSwimmerUpdate, // pool 0x20 type 0x77 sub 1 (ST37 / ST38): the small lunging swimmer (content/nino/ruin_swimmers)
    LurePieceUpdate, // static ST21 / ST22: bouncing liftable / throwable lure piece (content/calbania/st21_props)
    MachineGunImpactUpdate, // pool 0xa0 global type 11: the machine gun's impact (game/shots/machine_gun)
    MachineGunMuzzleUpdate, // pool 0xa0 global type 10: the machine gun's muzzle flash (game/shots/machine_gun)
    MachineGunShotUpdate, // pool 0x10 type 12: weapon 13, the machine gun (game/shots/machine_gun)
    MachinePartPropUpdate, // pool 0x20 type 2 sub 0 (ST18, from 0x4e4930): the prop riding a machine part, script-timed animation (content/nino/repair_machine)
    MachinePartUpdate, // pool 0x20 type 0x5e sub 1 (ST18 sub 2): the machine parts (variant 1 sinks with a static type 0x3d; content/nino/repair_machine)
    MandaBladeUpdate, // ST12 sub 5: the arena's spinning blades (static type 0x17 sub 1, content/manda/manda_guardian)
    MandaBurrowerUpdate, // pool 0x20 type 0x22 sub 0 (ST12/13/14): the burrowing Manda-ruin reaverbot (v0 crawl/leap/bounce, v1 guard firing pool 0x40 type 0), liftable/throwable (content/manda/manda_ruin_burrower)
    MandaCast2GuardianUpdate, // ST12 sub 5: the guardian of scripts 0xb / 0xc (pool 0x20 type 0x24 sub 1, content/manda/manda_guardian)
    MandaCastGuardianUpdate, // ST13 sub 1: the guardian of script 9 (pool 0x20 type 0x24 sub 1, content/manda/manda_guardian)
    MandaCloudUpdate, // ST13 effect type 0xc: the guardian's spin cloud (content/manda/manda_guardian)
    MandaDebrisPieceUpdate, // static ST14: spinning debris piece (content/manda/manda_ruin_props)
    MandaEntranceHallRoomHandler,
    MandaFallingBladeUpdate, // ST12 sub 5: the blade falling in script 0xb (static type 0x17 sub 2, content/manda/manda_guardian)
    MandaGateUpdate, // static ST12: script 1 / 4 gates and lifts (content/manda/manda_ruin_props)
    MandaGuardianHallRoomHandler,
    MandaGuardianUpdate, // ST13 sub 1 / ST12 sub 5: the Manda ruin guardians (pool 0x20 type 0x24, content/manda/manda_guardian)
    MandaHopperUpdate, // ST13: the guardian's hoppers (pool 0x20 type 0x31 sub 0, content/manda/manda_guardian)
    MandaOpenedChestUpdate, // ST12 sub 5: the opened chest of script 0xb (pool 0x20 type 0x15 sub 1, content/manda/manda_guardian)
    MandaOutroGuardianUpdate, // ST13 sub 1: the guardian of script 10 (pool 0x20 type 0x24 sub 2, content/manda/manda_guardian)
    MandaPlatformUpdate, // static ST12 / ST13: moving platform (companion type 0x17, rider 0x22) / spark wheel (variant & 4) (content/manda/manda_ruin_props)
    MandaRuinBurningWreckUpdate, // pool 0x20 type 0x2b sub 2 (ST14): the burning wreck of script 0xe -- random blasts, static 0x11 (content/manda/manda_ruin_scripts)
    MandaRuinGroundedFigureUpdate, // pool 0x20 type 0x24 sub 2 (ST12): a grounded figure, its animation asked from outside (content/manda/manda_ruin_scripts)
    MandaRuinKeyItemUpdate, // static type 0x40: a key item on its pedestal (ST14 sub 4 after script 0xe, content/manda/manda_ruin_scripts)
    MandaRuinScript1a,
    MandaRuinScript9,
    MandaRuinScriptA,
    MandaRuinScriptB,
    MandaRuinScriptC,
    MandaRuinScriptE,
    MandaRuinScriptF,
    MandaScript1aLastHallEndStep,
    MandaScript1aLastHallVoiceStep,
    MandaScript1aLastHallWalkStep,
    MandaScript1aStep2,
    MandaScript1aStep3,
    MandaScript9FirstHallEndStep,
    MandaScript9FirstHallLookStep,
    MandaScript9FirstHallPoseStep,
    MandaScript9FirstHallSoundStep,
    MandaScript9FirstHallWalkInStep,
    MandaScriptAAfterGuardianSoundStep,
    MandaScriptAAfterGuardianWalkStep,
    MandaScriptABlockStep,
    MandaScriptAFreeAndLoadStep,
    MandaScriptBSecondGuardianBladeStep,
    MandaScriptBSecondGuardianFadeStep,
    MandaScriptBSecondGuardianPlaceStep,
    MandaScriptBSecondGuardianTurnStep,
    MandaScriptCAfterSecondSoundStep,
    MandaScriptCAfterSecondWalkStep,
    MandaScriptCFreeAndLoadStep,
    MandaScriptEGuardianJumpStep,
    MandaScriptEGuardianWreckStep,
    MandaScriptFBackToTerminalStep,
    MandaScriptFPlatformRisesStep,
    MandaScriptFPlatformSparksStep,
    MandaScriptFPlatformUpStep,
    MandaScriptFTerminalWalkStep,
    MandaSecondDoorRoomHandler,
    MandaShotUpdate, // ST13 pool 0x40 type 0xe: the guardian's shot (content/manda/manda_guardian)
    MessageTriggerUpdate, // pool e0 type 0x1b: a message trigger (game/message_triggers)
    MinerUpdate, // ST2B pool 0x20 type 0: the trapped miner (idles, faces the player, calls for help until flag 0x583; content/saul_kada/trapped_miner)
    MinionSpawnerUpdate, // ST3C pool 0xe0 type 0x27: spawns up to 3 pool-0x20 ST3C type 0x78 (0x4ff7d0) near the player on a timer (content/saul_kada/st3c_minion_spawner)
    MissileShipUpdate, // ST18 stage static type 0x38: Glyde's ship launching the missiles (content/nino/glyde_missiles)
    MissileTrailUpdate, // static type 6: a missile trail (game/missile_trail)
    MissionGunshipUpdate, // ST15 static type 0x1b sub 0: the mission gunships (content/nino/mission_gunships)
    MissionResultFadeOutStep,
    MissionResultScript,
    MissionResultSetUpState,
    MissionResultStep2,
    MissionResultStep4,
    MissionResultStepsState,
    MissionResultSuccessWarpStep,
    MissionResultWrapUpState,
    MissionTankUpdate, // pool 0x20 type 0x33 (ST15 / ST16 missions): the tanks (content/nino/mission_tank)
    MissionTargetUpdate, // ST15 / ST16 static type 0x2d: the mission targets (content/nino/mission_targets)
    NinoAttackEndScript,
    NinoReturnRoomsAttack,
    NinoReturnRoomsMachine,
    NinoReturnRoomsReturn,
    NinoReturnRoomsShore,
    NinoReturnScript,
    NinoReturnScriptsStepArrive,
    NinoReturnScriptsStepBack,
    NinoReturnScriptsStepClear,
    NinoReturnScriptsStepDive,
    NinoReturnScriptsStepFaceSea,
    NinoReturnScriptsStepFlyPoints,
    NinoReturnScriptsStepHide,
    NinoReturnScriptsStepLand,
    NinoReturnScriptsStepLast,
    NinoReturnScriptsStepLook,
    NinoReturnScriptsStepRipples,
    NinoReturnScriptsStepRise,
    NinoReturnScriptsStepRun,
    NinoReturnScriptsStepSink,
    NinoReturnScriptsStepSurface,
    NinoReturnScriptsStepToShore,
    NinoReturnScriptsStepWalkAway,
    NinoReturnScriptsStepWatch,
    NinoRoomsSt18RoomCallback,
    NoRoomListClearHelper, // the room helper of a stage with no room list (the word cleared)
    NoRoomListHelper, // the room helper of a stage with no room list (the room-script handler word kept)
    OpeningAfterChoiceStep,
    OpeningBackStep,
    OpeningCastCrewScript68, // variant 2: script 0x68's (content/intro/st39_script68)
    OpeningCastCrewWalker, // variant 1: turns and walks off at step sub 6
    OpeningCastCrewWatcher, // variant 0: keyed by the step sub and tick, placed for the talk
    OpeningCastTalkerArriving, // variant 0: arrives, keyed on the step tick
    OpeningCastTalkerTalking, // variant 1: the long talk, the choice, the end
    OpeningChoiceStep,
    OpeningCrewStep,
    OpeningCrewUpdate, // ST39 type 0x63: the crew of the opening (content/intro/opening_cast)
    OpeningEffectUpdate, // ST39 / ST3A effect type 0xf: flames, streaks, puffs (content/intro/opening_effects; drawn by content/intro/opening_effects_draw)
    OpeningFlutterUpdate, // ST39 type 0x30: the Flutter of the opening (content/intro/opening_cast)
    OpeningLongTalkStep,
    OpeningReturnStep,
    OpeningScript,
    OpeningSeaStep,
    OpeningSetUpState,
    OpeningShakeStep,
    OpeningSkyStep,
    OpeningTalkerUpdate, // ST39 type 0x39: the talking character (content/intro/opening_cast)
    OpeningWalkOffStep,
    OpeningWrapUpState,
    OwnerGlowUpdate, // static ST3B: spinning additive glow bound to its owner (content/saul_kada/st3b_glow)
    ParkedDropshipUpdate, // ST10 type 0x2c: the dropship parked on the Forbidden Island (content/forbidden_island/island_visit)
    ParticleBurstUpdate, // static type 0x4c sub 0 (ST27 / 28 / 34 / 42 / 55): a burst of 10 particles, the ruin flyers' shot (0xf0 discs + hit entry; content/saul_kada/particle_burst)
    PassageEnd,
    PassageStart,
    PassageTransition, // TransitionKind::Passage
    PatrolDroneUpdate, // static ST15: patrol drone circling 0x8cebc0, crash on flag 0x690 (content/nino/st15_props)
    PatrolGuardUpdate, // ST26 pool 0x20 type 0x3b sub 1: patrolling guard (home lane +0x160, chase, wall turn, gives way to neighbours, knock-back, blast + drops; content/saul_kada/st26_patrol_guard)
    PedestalItemUpdate, // pool 0x60 type 0x40 (ST35 sub 5): the item on its pedestal, use -> flag +0xe (content/nino/pedestal_item)
    PickupEaterUpdate, // pool 0x20 (ST2D / ST2E / ST4F): the pickup eater -- loops around a drifting centre, shies from the player, runs to and eats the pickups; hit reactions, liftable / throwable, dies with a burst + drops (content/subruins/pickup_eater)
    PickupUpdate, // static type 0: a pickup (zenny / energy / life drop; game/pickups)
    PiecesDropperUpdate, // ST3A static type 0x46: the falling pieces' dropper (content/intro/crash_landing)
    PirateBaseArrivalScript,
    PirateBaseCastUpdate, // ST3C pool 0x20 type 0x72 sub 0: a cast member of the pirate-base script (block 0xa725a0 cues 1..4; variant 1 drops spark emitters at bones 3 / 6; content/saul_kada/pirate_base_cast)
    PirateBaseGateScript,
    PirateBaseScriptsArrivalLoad,
    PirateBaseScriptsArrivalPlace,
    PirateBaseScriptsArrivalReact,
    PirateBaseScriptsArrivalReset,
    PirateBaseScriptsArrivalWalk,
    PirateBaseScriptsGateBreak,
    PirateBaseScriptsGateGuards,
    PirateBaseScriptsGateLook,
    PirateBaseScriptsGateSweep,
    PirateBaseScriptsGateTurn,
    PirateBaseScriptsVictoryFade,
    PirateBaseVictoryScript,
    PirateCarrierUpdate, // static ST3B type 0x49 (story 12): the scripted carrier holding the player through the base's cutscene (cue 0xa725a0+3; smoke / sparks, bursts and frees itself), variant != 0 a talk prop (msg 0x28) (content/saul_kada/st3b_pirate_carrier)
    PirateCastUpdate, // pool 0x20 type 0xc (ST3C sub 2): the pirates of the base's cutscenes (cues of the script block, face pages, smoke; content/saul_kada/st3c_pirate_cast)
    PirateGunTurretUpdate, // Maybe_PirateTurretUpdate (pool 0x20 type 0xf: ST29 / ST2B gun turret, aims / fires pool-0x40 type 9, a Kobun jumps out when destroyed; content/saul_kada/gun_turret)
    PirateServbotUpdate, // pool 0x20 type 0xb sub 0 (ST29 / 2A / 2B / 3B / 3C / 48): the Servbots on foot of the Saul Kada pirate attack (run around the player, knocked down, liftable / throwable; content/saul_kada/pirate_servbots)
    PirateShotUpdate, // pool 0x40 type 0x31 (ST41 / ST42): projectile (v0 falling bomb, v1 / v2 homing shots; 0x2c billboards; content/saul_kada/st41_pirate_shot)
    PirateTankUpdate, // pool 0x20 type 0xd (ST29 / ST3B): the Saul Kada pirates' tank (drives around the player, tracking turret, lobbed shells / machine-gun bursts, rideable; a Servbot jumps out when destroyed; content/saul_kada/pirate_tank)
    PitControllerUpdate, // Maybe_LavaPitControllerUpdate (pool e0 type 0x1e: ST28 sub 3 flames + the creature's spots, content/saul_kada/lava_pit)
    PitCreatureMoverUpdate, // pool e0 type 0x1f (ST28, story 13): moves its static type 0x40 sub 3 between random points away from the player (content/saul_kada/lava_pit)
    PitCreatureUpdate, // Maybe_LavaPitCreatureUpdate (pool 0x60 type 0x40 sub 3: ST28 sub 3 surfacing creature, event 0x28 + flag 0x56 when hit, content/saul_kada/lava_pit)
    PitFlameUpdate, // Maybe_LavaPitFlameUpdate (pool 0x60 type 0x44: ST28 sub 3 flame, draw command 0x2e, content/saul_kada/lava_pit)
    PitStart,
    PitTransition, // TransitionKind::Pit
    PitWait,
    PlacedModelUpdate, // static global type 4: a model put by the player (game/placed_models)
    PlatformBossUpdate, // pool 0x20 type 0x79 (ST38 sub 11): the flying platform boss (content/nino/platform_boss)
    PlayerBarHangInput,
    PlayerBarHangMotion,
    PlayerBladeArm, // weapon 11
    PlayerBusterArm, // the buster's arm (every buster)
    PlayerCannonArm, // weapon 4 (the Buster Cannon)
    PlayerDeadState,
    PlayerEventPoseMotion,
    PlayerExtinguisherArm, // weapon 15
    PlayerGenericSpecialArm, // weapons 1..3, 5..7, 9, 13, 14, 16
    PlayerHeldInput,
    PlayerHeldMotion,
    PlayerIdleInput,
    PlayerIdleMotion,
    PlayerInitState,
    PlayerItemGetMotion,
    PlayerJumpInput,
    PlayerJumpMotion,
    PlayerKnockBackInput,
    PlayerKnockBackMotion,
    PlayerKnockDownMotion,
    PlayerLedgeHangInput,
    PlayerLedgeHangMotion,
    PlayerLiftMotion,
    PlayerLookInput,
    PlayerLookMotion,
    PlayerPlayingState,
    PlayerQuickTurnMotion,
    PlayerRunInput,
    PlayerRunMotion,
    PlayerRunningJumpMotion,
    PlayerScriptedUpdate,
    PlayerSideHopMotion,
    PlayerSideStepInput,
    PlayerSideStepMotion,
    PlayerSkateFallMotion,
    PlayerSkateInput,
    PlayerSkateMotion,
    PlayerTalkMotion,
    PlayerTryLedgeGrab,
    PlayerTurnInPlaceInput,
    PlayerTurnInPlaceMotion,
    PlayerWalkInput,
    PlayerWalkMotion,
    PodCarrierUpdate, // pool 0x20 type 0x7c (ST41): the pod carrier (bobs, drops up to 4 pods when the player is near, dies + drops; content/saul_kada/st41_carrier)
    PokteAfterTronRoomHandler,
    PokteRecordRoomHandlerA,
    PokteRecordRoomHandlerB,
    PokteRecordRoomHandlerC,
    PokteTankFightRoomHandler,
    PokteVillagersRoomHandler,
    PosedFigureUpdate, // ST05 / ST06 pool 0x20: a posed figure (bind, idle, animation; content/yosyonke/st05_st06_props)
    PressurePlateUpdate, // ST37 static type 1: the pressure plate (flags 0x96 / 0x97) (content/nino/floor_switch)
    PropDropperUpdate, // pool 0x20 type 0x48 sub 3 (ST28, script 0x46): sparks + static at bones, rises, dust (content/saul_kada/fire_ruin_cutscene_cast)
    PropHolderUpdate, // pool 0x20 type 0x48 sub 1 (ST28, story 13): holds a static type 0x40 sub 0 at bone 3 (content/saul_kada/fire_ruin_cutscene_cast)
    PuffEmitterUpdate, // ST54 static: the puff emitter (18 smoke / dust billboards, faded by a script cue; content/late_game/st54_puffs)
    PushBlockUpdate, // ST09 static type 0x4b sub 0: an object moved by hits (slides / hops back, falls off edges, solid at rest; its variant-1 copy follows it; content/yosyonke/st09_push_block)
    PushedOutTransition, // TransitionKind::PushedOut
    QuicksandUpdate, // Maybe_QuicksandUpdate (pool e0 type 0x23: ST48 sub 1 / ST5B sand pit pull + sprays, content/saul_kada/quicksand)
    QuizCastUpdate, // ST32 pool 0x20 types 0x58/0x5f/0x82: quiz host / contestants, talk event, quiz cues +0xc, head look, hit list, collision queues (content/calbania/st32_quiz)
    QuizShowScript,
    RaceActorUpdate, // ST59/5A/5B pool e0 type 0x29: the time-trial race (v0 controller, v1 speed gauge, v2 3-2-1-GO, v3 floor-cell watcher; HUD sprites not drawn; content/late_game/st59)
    RaceBonusUpdate, // static ST59 / 5A / 5B type 0x62: a floating time-bonus pickup of the race (freezes the clock 0x3c ticks, sound 0x30b; content/saul_kada/race_bonus)
    RayBurstUpdate, // ST4C static type 0x63: ray burst (LINE_G2, dc::otLineG2) (content/late_game/st4c_story)
    ReaverbotPackUpdate, // ST1D pool e0 type 0x11: the timed reaverbot pack (content/forbidden_island/island_visit)
    ReaverbotTurretUpdate, // pool 0x20 stage type 8: the reaverbot turret (game/reaverbot_turret)
    ReaverbotWalkerUpdate, // pool 0x20 stage type 5: the walking reaverbot (game/reaverbot_walker)
    ReflectorShotUpdate, // pool 0x10 type 8: weapon 9, the reflector (game/shots/reflector)
    RepairMachineUpdate, // pool 0x20 type 0x5e sub 0 (ST18 story 9): the machine Roll repairs in Glyde's last attack (content/nino/repair_machine)
    RepairRollUpdate, // pool 0x20 type 0x60 sub 0 (ST18 story 9): Roll at the machine (content/nino/repair_machine)
    RescueScript,
    RespawnerUpdate, // static ST27 type 0x27 (story 13): the room's enemy respawner (spawn within 0xc1c, despawn beyond; content/saul_kada/st27_respawner)
    ResultSceneDataUpdate, // ST1E pool 0x20 type 0x63 sub 1: Data in the mission's result (content/intro/burning_data)
    RiderFollowUpdate, // pool 0xe0 type 0x14: an actor standing on a platform follows it (game/carry)
    RiderServbotUpdate, // ST0E pool 0x20 type 0xb sub 1: the Servbots riding the boss vehicle, lobbing bombs 0x24 at the defended vehicle (content/yosyonke/st0e_servbots)
    RisingSmokeColumnUpdate, // stage static type 0x26: a smoke column of 8 puffs (draw commands, content/late_game/ch4_story)
    RockBurstUpdate, // ST1D pool e0 type 9: rock burst spawner (4 type-0x20 statics + 8 hit sparks; content/forbidden_island/st1d_rock_burst)
    RollRunsOffScript,
    RollRunsOffUpdate, // ST09 pool 0x20 type 0 sub 2: Roll running off (script 0x4e, content/yosyonke/town_story_cast)
    RollSpawnerST08Update, // ST08 pool e0 type 0xa: Roll's spawner by the entrance and the story flags (content/yosyonke/roll_spawners)
    RollSpawnerST09Update, // ST09 pool e0 type 0xa: Roll's spawner of the ST09 introduction (content/yosyonke/roll_spawners)
    RollSpawnerST0DUpdate, // ST0D pool e0 type 0xa: the tracks' cell, Roll by the entrance, region flags (content/yosyonke/roll_spawners)
    RollToMachineScript,
    RollUpdate, // ST08 / ST09 pool 0x20 type 0 sub 1: Roll following the player (content/yosyonke/roll_follower)
    RollerUpdate, // ST43 static type 7: spawner child (rises in, wanders / turns to the player / rolls over, liftable: carried / thrown / dropped, hits, dies with a blast + drops; content/late_game/st43_roller)
    RollingBoulderUpdate, // ST14 static type 0x18: the rolling boulder (back and forth between two points, camera shake on walls; content/manda/st12_14)
    RoomDoorScript, // runs the transition script on flag 0x700 and nothing else
    RoomFlagClutAnimationsUpdate, // pool e0 type 0x15: the room-flag CLUT animations (game/clut_animation)
    RoomUvAnimationUpdate, // pool 0xe0 type 4 (ST00 / ST0A): the room's texture animations (game/room_uv_animation)
    RuinBlocksUpdate, // static type 0x1f sub 0 (ST2D / 2E / 2F / 58): crumbling platforms, hit-breakable blocks / walls, object-state blocks, their debris (content/subruins/ruin_blocks)
    RuinEntranceRoomHandler,
    RuinFlashUpdate, // pool 0xa0 stage type 8: a flash, billboards / rings (draw commands; content/nino/ruin_effects)
    RuinFlyerUpdate, // ST34 pool 0x20 type 0x67 sub 1: the ruin flyer (patrol / hover shots / dive / home; content/saul_kada/st34_ruin_flyer)
    RuinGuardianUpdate, // pool 0x20 type 0x3a sub 0 (ST35 subs 5 / 0): the ruin's guardian, death -> flag 0x94 / 0x95 (content/nino/ruin_guardian)
    RuinSentryUpdate, // pool 0x20 type 0x15 sub 1 (ST34 sub 3, story 12+): the Saul Kada ruin's dormant reaverbot (woken by use / object state; marches, lob shots, drop throw; staggers; explodes with drops; content/saul_kada/st34_ruin_sentry)
    RuinVariantEffectUpdate, // pool 0xa0 stage type 0xa: per-variant effect, variants 0..7 (content/nino/ruin_effects)
    SandBurrowerUpdate, // Saul Kada burrowing sand enemy (pool 0x20 type 0x35: ST27/ST34, buried ambusher -- surfaces, paces between two home points, leaps / charges / spits shots, burrows home, content/saul_kada/sand_burrower)
    SandFlowUpdate, // pool e0 type 0x1a (ST48 sub 1 / ST5B): the sand flowing into the pit (VRAM image moves, game/vram_moves)
    SandTrapUpdate, // Maybe_SandTrapUpdate (pool 0x20 type 0x61: ST27 / 34 / 40..42 grabs the player, drains HP, mash to escape; content/saul_kada/sand_trap)
    SaulKadaRoomsLavaGlowRoutine,
    SaulKadaRoomsSt48RearmHelper,
    SceneCritterUpdate, // ST2F pool 0x20 type 0x4a sub 1: script 0x49's critter (idle / hop / leap on cue; content/yosyonke/ice_ruin_critters)
    ScreenRippleUpdate, // pool 0xa0 type 0x16 (0x52b180, variant 0; variants 1 / 2 rings / ray star): a growing screen ripple (frame-buffer warp not drawn; content/nino/screen_ripple)
    ScreenShockRingUpdate, // pool 0xa0 type 2: frame-buffer shock rings + spawner of the ball's explosion (warp not drawn; content/late_game/st5c_fx3)
    Script1dAppoDahUpdate, // pool 0x20 ST1F type 0x43 sub 1: Appo / Dah of script 0x1d (content/calbania/appo_dah_b)
    Script1fAppoDahUpdate, // pool 0x20 ST1F type 0x43 sub 3: Appo / Dah of script 0x1f (content/calbania/appo_dah_b)
    Script20AppoDahUpdate, // ST1F pool 0x20 type 0x43 sub 4: script 0x20 Appo / Dah (records 0x8a3940..0x8a397c), walk in + talk (content/calbania/ch3_script_cast)
    Script2aArrivalCastUpdate, // ST11 pool 0x20 type 0x3b sub 1: script 0x2a cast, jump + walk off (content/forbidden_island/st11_script2a)
    Script32CastUpdate, // ST19 pool 0x20 type 0x2d sub 0: script 0x32 cast (records 0x8a5fe8..0x8a6038), mouth flaps (content/nino/ch2_script_cast)
    Script37CastUpdate, // ST18 pool 0x20 type 0x2d sub 2: script 0x37 cast (records 0x8a6778..), mouth flaps, variant 5 lying + smoke (content/nino/ch2_script_cast)
    Script37FlashUpdate, // pool 0xa0 ST18 stage type 5: script 0x37 flash, 0xf0 discs + G4 spikes (content/nino/ch2_script_cast)
    Script39BirdbotUpdate, // pool 0x20 type 0x2d sub 2 (ST17): the Birdbots of script 0x39, Glyde's attack on Nino (content/nino/glyde_attack_cast)
    Script39BlinkerUpdate, // pool 0x20 type 0x5b (ST17): script 0x39 cast member, Glyde's attack on Nino (content/nino/glyde_attack_cast)
    Script39DescenderUpdate, // pool 0x20 type 0x5c (ST17): script 0x39 cast member, Glyde's attack on Nino (content/nino/glyde_attack_cast)
    Script39RiserUpdate, // pool 0x20 type 2 (ST17, 0x44acc0): script 0x39 cast member, Glyde's attack on Nino (content/nino/glyde_attack_cast)
    Script3bBirdbotUpdate, // pool 0x20 type 0x2d sub 3 (ST18): the Birdbots of script 0x3b, Glyde's attack on Nino (content/nino/glyde_attack_cast)
    Script3cCastUpdate, // ST46 pool 0x20 type 0x5b sub 0: script 0x3c cast (record 0x8a7498) (content/calbania/ch3_script_cast)
    Script59CuedTalkerAUpdate, // ST4B pool 0x20 type 9: script 0x59 cast, cue animations + face rows 0x4fb5d0, variants 0-1 (content/late_game/late_cast)
    Script59CuedTalkerBUpdate, // ST4B pool 0x20 type 0x72: script 0x59 cast, cue animations + face rows, variants 0-1 (content/late_game/late_cast)
    Script59FaceRowCastUpdate, // ST4B pool 0x20 type 0x16: a cast member of script 0x59 (walks in, face rows 0x4fb5d0; content/late_game/st4b)
    Script59FlyerUpdate, // ST4B pool 0x20 type 0x3f sub 0: script 0x59 flyer (waypoints 0x894910, puffs, type-0x1d effects), variants 0-1 (content/late_game/late_cast)
    Script59IdleOrStepperUpdate, // ST4B pool 0x20 type 0x50: script 0x59 cast, variants 0-1 (content/late_game/late_cast)
    Script59LateStepperUpdate, // ST4B pool 0x20 type 0xb: script 0x59 cast, animation steps from tick 0x53b (content/late_game/late_cast)
    Script59LateTalkerUpdate, // ST4B pool 0x20 type 0x75: script 0x59 cast, face rows (content/late_game/late_cast)
    Script59SceneTalkerUpdate, // ST4B pool 0x20 type 0x2f: script 0x59 cast, animations / rows by scene (content/late_game/late_cast)
    Script59SceneWalkerUpdate, // ST4B pool 0x20 type 0x68: script 0x59 cast, animations by scene / tick (content/late_game/late_cast)
    Script59WalkInCastUpdate, // ST4B pool 0x20 type 2: a cast member of script 0x59 (cast 0x8aa3d0; content/late_game/st4b)
    Script60ExtraAUpdate, // ST53 pool 0x20 type 0x7a: script 0x60 extra (no-op steps) (content/late_game/st53_story)
    Script60ExtraBUpdate, // ST53 pool 0x20 type 0x7b: script 0x60 extra (no-op steps) (content/late_game/st53_story)
    Script60FacedTalkerUpdate, // ST53 pool 0x20 type 9 sub 0: script 0x60 cast (content/late_game/st53_story)
    Script60FadingFigureUpdate, // ST53 pool 0x20 type 0x24: script 0x60 cast, bone fade + spark rings (content/late_game/st53_story)
    Script60LeadTalkerUpdate, // ST53 pool 0x20 type 0x72 sub 0: script 0x60 cast (content/late_game/st53_story)
    Script60MaskedTalkerUpdate, // ST53 pool 0x20 type 0x69 sub 0: script 0x60 cast (content/late_game/st53_story)
    Script60WalkerUpdate, // ST53 pool 0x20 type 0x3a: script 0x60 cast, walks / yaw ramp (content/late_game/st53_story)
    Script61FacedTalkerUpdate, // ST53 pool 0x20 type 9 sub 1: script 0x61 cast (content/late_game/st53_story)
    Script61LeadTalkerUpdate, // ST53 pool 0x20 type 0x72 sub 1: script 0x61 cast (content/late_game/st53_story)
    Script61MaskedTalkerUpdate, // ST53 pool 0x20 type 0x69 sub 1: script 0x61 cast (content/late_game/st53_story)
    Script61StandingFigureUpdate, // ST53 pool 0x20 type 0x2d: script 0x61 cast (content/late_game/st53_story)
    Script62TalkerAUpdate, // ST53 pool 0x20 type 9 sub 2: script 0x62's cast (content/late_game/ch4_story)
    Script62TalkerBUpdate, // ST53 pool 0x20 type 0x72 sub 2: script 0x62's cast (content/late_game/ch4_story)
    Script68CastUpdate, // ST39 pool 0x20 type 2 sub 0: script 0x68 cast, texture tracks, anim 1 at block +0x1c == 0x81 (content/intro/st39_script68)
    Script69CastUpdate, // ST1F pool 0x20 type 0x2f sub 0: script 0x69 cast (record 0x8acd70), texture-track cues (content/calbania/ch3_script_cast)
    Script6fActionTalkerUpdate, // ST54 pool 0x20 type 2: script 0x6f cast slot 1, actions 1-7 (content/late_game/late_cast)
    Script6fCirclerUpdate, // ST54 pool 0x20 type 0x63: script 0x6f cast slot 2 (circles, walks to 0x896568), actions 0-5 (content/late_game/late_cast)
    Script6fHoldingTalkerUpdate, // ST54 pool 0x20 type 9: script 0x6f cast slot 0, actions 1-7 (content/late_game/late_cast)
    Script6fMouthTalkerUpdate, // ST54 pool 0x20 type 0x1a: script 0x6f talking cast slot 2 + bone sparks (content/late_game/late_cast)
    Script6fPacerUpdate, // ST54 pool 0x20 type 0xb: script 0x6f walker, actions 0-2 by +0xc (content/late_game/late_cast)
    Script6fRowTalkerUpdate, // ST54 pool 0x20 type 0x16: script 0x6f cast slot 3, actions 0-2 (content/late_game/late_cast)
    Script6fSparkTalkerUpdate, // ST54 pool 0x20 type 0x72: script 0x6f cast slot 1, actions 0-2 + bone sparks (content/late_game/late_cast)
    ScriptAnimatedUpdate, // ST39 type 0x47: the script's animation (content/intro/opening_cast)
    ScriptCastStepsState, // the steps runner with the cast's copy of the timeline (shared by the scripts)
    ScriptFallingPieceUpdate, // ST4B static type 6 sub 2: the script-driven falling piece (= 0x595d80 states 3..5; content/late_game/st4b_fx)
    ScriptSequenceState, // a script's camera and step sequence
    ScriptStatueUpdate, // ST3C pool 0x20 type 0x73 variant 1: a cast member of script 0x5d (follows the block's cue byte; content/saul_kada/st3c_statue)
    ScriptStepsState, // the steps runner (shared by the scripts)
    ScriptWalkerUpdate, // pool 0x20 type 0x3a (ST24): a cast member walking through its script's steps (content/saul_kada/st24_script_walker)
    ScriptedMessageUpdate, // pool e0 type 0x1d: the scripted message (game/message_triggers)
    ScriptedWalkerUpdate, // pool 0x20 type 0x63 sub 0 (ST46): the scripted walker (content/nino/ch2_field_cast)
    ScrollGlowUpdate, // pool 0xa0 (ST27 sub 2, story 14): a glowing billboard with a scrolling texture (0x2c draw command; content/saul_kada/st27_scroll_glow)
    ServbotScriptCastUpdate, // pool 0x20 type 0xb sub 2 (ST28 sub 5, story 13): a cast member of the fire-ruin scripts (cues, head turn, face pages / blinks; content/saul_kada/fire_ruin_servbot_cast)
    ServbotUpdate, // Maybe_ServbotUpdate (servbots, pool 0x20 type 0xb: route / stand / talk, hit reactions)
    ShedRollUpdate, // ST08 pool 0x20 type 0 sub 2: Roll waiting at Joe's lab (content/yosyonke/roll_follower)
    ShellCasingUpdate, // static global type 0x11: the machine gun's shell casing (draw command; game/shots/machine_gun)
    ShiningLaserBeamUpdate, // pool 0xa0 global type 8: the laser's charge / beam / fade (game/shots/shining_laser)
    ShiningLaserShotUpdate, // pool 0x10 type 11: weapon 12, the Shining Laser (game/shots/shining_laser)
    ShipFightGunshipUpdate, // ST18 static type 0x1b sub 0: the gunships of the ship fight (content/nino/st18_gunships)
    ShockRingUpdate, // pool a0 stage type 3: a shockwave's ring (game/shockwave)
    ShockWaveUpdate, // static global type 0x17: the charge's shock wave (game/shots/charge)
    ShockwaveUpdate, // pool 0x40 stage type 3: a ground shockwave (game/shockwave)
    ShopBuyMenu,
    ShopGoodbye,
    ShopListMenu,
    ShopMainMenu,
    ShopMenuCallback, // the shop stages' menu (script 8's stage callback)
    ShopMenus,
    ShopPageMenu,
    ShopSellMenu,
    ShopSetUp,
    ShopUpgradesMenu,
    ShotTrailUpdate, // pool 0xe0 global type 0x10: a shot's trail segment (content/manda/manda_guardian)
    ShrinkBlockUpdate, // pool 0x20 type 0x52 (ST2D / ST2E): a solid block with a scrolling texture (variant 0 gone by flag, others shrink away; content/yosyonke/ice_ruin_shrink_block)
    ShyCritterUpdate, // ST0D pool 0x20 type 0x7e: a shy critter (wanders, flees, knocked away; content/yosyonke/st0d_critter)
    SignUpdate, // sign / notice you read (static pool type 0: ST09, ST19, ST20 ...)
    SittingPuppetUpdate, // pool 0x20 ST20 type 0 sub 2: the sitting puppets of script 0x11 (content/calbania/calbania_cast)
    SkidSparksUpdate, // pool 0xa0 global type 3: the skid sparks (game/skid_sparks)
    SkyArcDraw,
    SkyArcStart,
    SkyArcUpdate, // ST00/ST20 pool e0 type 2: screen-space sky arc, subtractive + additive G4 passes (dc::otQuadG4; game/sky_arc)
    SkyShipUpdate, // ST3A static type 6: a ship in the sky (content/intro/crash_landing)
    SlideBlockUpdate, // ST2A static type 0xc: the block sliding in line with the player, breakable (content/saul_kada/st2a_slide_block)
    SlidingDoorUpdate, // static ST41 / ST42 type 0x58: sliding door pair over a grid cell (lock flags 0x8d0c1c, sounds 0xba / 0xbd; content/saul_kada/st41_sliding_door)
    SlidingPairEnd,
    SlidingPairStart,
    SlidingPairTransition, // TransitionKind::SlidingPair
    SlowMarkerUpdate, // static global type 0x13: the player's slow status marker (OT lightning; game/status_markers)
    SmokeColumnUpdate, // static type 0x14: a smoke column (a fire put out, the player's orbit status; game/smoke_column)
    SmokePuffUpdate, // ST48 pool 0x40 type 0xb: a rising puff of smoke (two fading billboards, hit-list entry; content/saul_kada/smoke_puff)
    SnowParticleDraw, // a snow particle's sprite (run for every particle the snow's move loop drew)
    SnowReaverbotUpdate, // pool 0x20 (ST1D / ST2D / ST4F): the snow reaverbot (game/snow_reaverbot)
    SnowUpdate, // static type 2: the snow of the towns (ST08 / ST09 / ST0B / ST1C / ST5A; game/snow)
    SolidRingUpdate, // static ST41 / ST42 type 0x31: solid 4-piece ring (v0 spawns 1..3; content/saul_kada/st41_ring)
    SparkBurstUpdate, // ST5C static type 0x64: spark bursts (draw commands; content/late_game/st5c)
    SparkEmitterUpdate, // static stage type 3: a spark / smoke emitter (game/spark_emitter)
    SparkRingsUpdate, // pool a0 stage type 0xd: spark rings (game/spark_rings)
    SparkSprayUpdate, // static global type 0xf: the reflector's spray of sparks (draw commands; game/shots/reflector)
    SparkleUpdate, // static global type 0x15: the spread buster's sparkle (game/shots/spread_buster)
    SparklerUpdate, // ST3C pool 0xe0 type 0x28: sparkles (pool 0xa0 global type 3) at the player below a height (content/saul_kada/st3c_sparkler)
    SpawnerSt12Update, // ST12 e0 type 0xa: groups 0 / 2..5, type-3 respawns (ids 1..3), type-0x1b emitters, type-0x2a drops
    SpawnerSt13Update, // ST13 e0 type 0xa: hopper groups (all variants 0..5)
    SpawnerSt14Update, // ST14 e0 type 0xa: script group 1, type-3 respawns (ids 8 / 9)
    SpawnerSt15Update, // ST15 e0 type 0xa: pool-0x60 type-0x1b waves, flags 0x680 / 0x681 / 0x28
    SpawnerSt16Update, // ST16 e0 type 0xa: type-0x49 respawns, pool-0x60 type-0x25 blocks, timers 0x10..0x14
    SpawnerSt17Update, // ST17 e0 type 0xa: type-0x2d walkers on routes, grid-cell flags 0x32..0x34
    SpawnerSt18Update, // ST18 e0 type 0xa: type-0x2d / pool-0x60 type-0x1b, repair gauge (picture od::hud::ninoGaugeDraw)
    SpawnerSt1fUpdate, // ST1F e0 type 0xa: story-8 fight (flags 0x580..0x586, 0x599)
    SpawnerSt21Update, // ST21 e0 type 0xa: story-8 records, two RNG-placed type-6 actors
    SpawnerSt22Update, // ST22 e0 type 0xa: story-8 records
    SpawnerSt26Update, // ST26 e0 type 0xa: encounter groups by variant (flags 0x3ed / 0x56), type 0x45
    SpawnerSt27Update, // ST27 e0 type 0xa: encounter groups by variant (flags 0x43..0x45 / 0x56)
    SpawnerSt28Update, // ST28 e0 type 0xa: encounter group 0
    SpawnerSt2bUpdate, // ST2B e0 type 0xa: grid cell (0x48, 0x40) -> flag 0x58a, pool-0x60 type 7, group 1
    SpawnerSt34Update, // ST34 e0 type 0xa: encounter groups by variant (flags 0x45 / 0x56)
    SpawnerSt36Update, // ST36 e0 type 0xa: room records, pool-0x60 type-0x48 countdown, script group 4
    SpawnerSt37Update, // ST37 e0 type 0xa: room records, type-0x48 bursts / countdown, push-back trap
    SpawnerSt38Update, // ST38 e0 type 0xa: room records, type-0x48 countdown (flag 0x92)
    SpawnerSt3bUpdate, // ST3B e0 type 0xa: type-0x1c emitter / breakable block (grid segment 9)
    SpawnerSt45Update, // ST45 e0 type 0xa: flag 0x176 -> fades 0x1a / 0xa, classes 0x494 freed, flag 0x174
    SpawnerSt55Update, // ST55 e0 type 0xa: the licence exam timer (picture od::hud::examTimerDraw)
    SpinBladeUpdate, // pool 0x20 type 0x20 (ST13): a spinning blade / fan (spins up, stops when hit, restarts; hit capsules; content/saul_kada/st13_spin_blade)
    SpinPropUpdate, // static ST00 type 1: a prop (variant 2 +0 |= 0x40, variant 4 spins; content/saul_kada/st00_spin_prop)
    SpreadBusterShotUpdate, // pool 0x10 type 13: weapon 14, the spread buster (game/shots/spread_buster)
    SquareDefenceScriptsStepAnswer,
    SquareDefenceScriptsStepArrive,
    SquareDefenceScriptsStepCross,
    SquareDefenceScriptsStepEnd,
    SquareDefenceScriptsStepFly,
    SquareDefenceScriptsStepGather,
    SquareDefenceScriptsStepHold,
    SquareDefenceScriptsStepLand,
    SquareDefenceScriptsStepLast,
    SquareDefenceScriptsStepListen,
    SquareDefenceScriptsStepPause,
    SquareDefenceScriptsStepReply,
    SquareDefenceScriptsStepRunUp,
    SquareDefenceScriptsStepTalk,
    SquareDefenceScriptsStepThrow,
    SquareDefenceScriptsStepWalk,
    SquareDefenceScriptsStepWatch,
    SquareGunshipUpdate, // ST18 static type 0x1b sub 1: the gunship after script 0x37 (shot down -> flag 0x2b; content/nino/square_gunship)
    SquareOpeningScript,
    SquareSecondPhaseScript,
    SquareThirdPhaseScript,
    SquareWallLiftScript,
    St00FigureUpdate, // pool 0x20 type 2 (ST00): the display figures (head tracking, random / pad-cycled animations, blinking; content/intro/st00_figures)
    St00RoomSetup, // the room setup of ST00 (exe table 0x8d397c)
    St00StageSetup, // the stage setup of ST00 (exe table 0x8d3808)
    St01RoomSetup, // the room setup of ST01 (exe table 0x8d397c)
    St01StageSetup, // the stage setup of ST01 (exe table 0x8d3808)
    St02AttractScript,
    St02BarrelUpdate, // ST02 pool 0x20 type 0x1a: attract script 3 cast, content/intro/st02_attract_cast
    St02BlinkerUpdate, // ST02 pool 0x20 type 0x23: attract, content/intro/st02_attract_cast
    St02CastBarrelFirst,
    St02CastBarrelSecond,
    St02CastBlinkerBlinks,
    St02CastDrifts,
    St02CastFaceLines,
    St02CastPlaced,
    St02CastPropDrifts,
    St02CastRollFirst,
    St02CastRollSecond,
    St02CastScreenShows,
    St02CastSpeakerLines,
    St02CastStanderStands,
    St02CastTalkerLines,
    St02CastWatcherLooks,
    St02CastWatcherOnce,
    St02CastWatcherWalks,
    St02CastWatcherWalksLater,
    St02DriftingStaticUpdate, // ST02 static type 0x1a: attract script 3, content/intro/st02_attract_cast
    St02FaceUpdate, // ST02 pool 0x20 type 0x21: attract, content/intro/st02_attract_cast
    St02PlacedStaticUpdate, // ST02 static type 0x14: attract script 3, content/intro/st02_attract_cast
    St02PropUpdate, // ST02 pool 0x20 type 0x27: attract script 3, content/intro/st02_attract_cast
    St02RollUpdate, // ST02 pool 0x20 type 0x16: attract script 3 cast, content/intro/st02_attract_cast
    St02RoomSetup, // the room setup of ST02 (exe table 0x8d397c)
    St02SceneryUpdate, // ST02 static type 0x16: attract, content/intro/st02_attract_cast
    St02ScreenUpdate, // ST02 pool 0x20 type 0x3c: attract, content/intro/st02_attract_cast
    St02Script3SetUpState,
    St02Script3WrapUpState,
    St02SpeakerUpdate, // ST02 pool 0x20 type 0x28: attract, content/intro/st02_attract_cast
    St02StageSetup, // the stage setup of ST02 (exe table 0x8d3808)
    St02StanderUpdate, // ST02 pool 0x20 type 0x25: attract, content/intro/st02_attract_cast
    St02StillModelUpdate, // ST02 static type 0x34: attract, content/intro/st02_attract_cast
    St02TalkerUpdate, // ST02 pool 0x20 type 0x1e: attract, content/intro/st02_attract_cast
    St02WatcherUpdate, // ST02 pool 0x20 type 0x1d: attract, content/intro/st02_attract_cast
    St03AttractScript,
    St03BarrelUpdate, // ST03 pool 0x20 type 0x1a: attract, content/intro/st03_attract_cast
    St03BlinkerUpdate, // ST03 pool 0x20 type 0x23: attract, content/intro/st03_attract_cast
    St03CastBarrelPlaced,
    St03CastBarrelTalksLate,
    St03CastBarrelTurns,
    St03CastBlinkerMoves,
    St03CastBodyHops,
    St03CastBodyRides,
    St03CastBodyRidesLater,
    St03CastBodyWalks,
    St03CastCarrierTalks,
    St03CastCarrierWalksIn,
    St03CastCraftFlies,
    St03CastCraftReturns,
    St03CastCraftSinks,
    St03CastFaceLines,
    St03CastFlutterBobs,
    St03CastFlutterLifts,
    St03CastPartBlinks,
    St03CastPlacedMovedA,
    St03CastPlacedMovedB,
    St03CastPlacedStill,
    St03CastPlacedTurning,
    St03CastRollLast,
    St03CastRollLater,
    St03CastRollTalks,
    St03CastScreenStill,
    St03CastSpeakerLines,
    St03CastStanderMoves,
    St03CastTalkerLines,
    St03CastWalkerWaits,
    St03CastWalkerWalks,
    St03CastWatcherCarries,
    St03CastWatcherComes,
    St03CastWatcherHoldsA,
    St03CastWatcherHoldsB,
    St03CastWatcherSparks,
    St03CastWatcherStill,
    St03CraftBodyUpdate, // ST03 pool 0x20 type 0x2f: attract, content/intro/st03_attract_cast
    St03CraftUpdate, // ST03 pool 0x20 type 0x3f: attract, content/intro/st03_attract_cast
    St03FaceUpdate, // ST03 pool 0x20 type 0x21: attract, content/intro/st03_attract_cast
    St03FlutterUpdate, // ST03 pool 0x20 type 0x30: attract, content/intro/st03_attract_cast
    St03LightsUpdate, // ST03 pool 0x20 type 0x4e: attract, content/intro/st03_attract_cast
    St03PartBlinkerUpdate, // ST03 pool 0x20 type 0x2e: attract, content/intro/st03_attract_cast
    St03PieceCarrierUpdate, // ST03 pool 0x20 type 0x1c: attract, content/intro/st03_attract_cast
    St03PlacedModelUpdate, // ST03 static type 1: attract, content/intro/st03_attract_cast
    St03RollUpdate, // ST03 pool 0x20 type 0x16: attract, content/intro/st03_attract_cast
    St03RoomSetup, // the room setup of ST03 (exe table 0x8d397c)
    St03SceneryUpdate, // ST03 static type 0x16: attract, content/intro/st03_attract_cast
    St03ScreenUpdate, // ST03 pool 0x20 type 0x3c: attract, content/intro/st03_attract_cast
    St03Script4SetUpState,
    St03Script4StepsState,
    St03Script4WrapUpState,
    St03SpeakerUpdate, // ST03 pool 0x20 type 0x28: attract, content/intro/st03_attract_cast
    St03StageSetup, // the stage setup of ST03 (exe table 0x8d3808)
    St03StanderUpdate, // ST03 pool 0x20 type 0x25: attract, content/intro/st03_attract_cast
    St03StillModelUpdate, // ST03 static type 0x34: attract, content/intro/st03_attract_cast
    St03TalkerUpdate, // ST03 pool 0x20 type 0x1e: attract, content/intro/st03_attract_cast
    St03TiltedModelUpdate, // static ST03: a tilted still model (content/intro/st03_props)
    St03WalkerUpdate, // ST03 pool 0x20 type 0x1f: attract, content/intro/st03_attract_cast
    St03WatcherUpdate, // ST03 pool 0x20 type 0x1d: attract, content/intro/st03_attract_cast
    St04RoomHandler, // subs 0 / 3, stories 1..: exe 0x57be10
    St04RoomHandlerA,
    St04RoomHelper, // ST04 / ST06: no room list, the doors unlocked
    St04RoomHelperA, // ST04: the Flutter's door
    St04RoomSetup, // the room setup of ST04 (exe table 0x8d397c)
    St04SecondRoomCallback, // the callback the stage setup of ST04 sets (0xa72230)
    St04StageSetup, // the stage setup of ST04 (exe table 0x8d3808)
    St04Story0Sub0RoomHandler, // story 0 sub 0 (after the crash landing): the Flutter and, the first time, script 0x55 (it comes
    St04Story0Sub0RoomHandlerA, // story 0 sub 0: the first arrival: its flags, script 0x4c; later visits run exe 0x57be10's other
    St04Story2OnSub1RoomHandler, // sub 1 (stories 2..): the guests' flags by the tool kit tier
    St04Sub1RoomHandler, // sub 1: the door lock while the Flutter is docked at the island 1
    St05EntryListEntryMenuList,
    St05EntryListEntryMenuText,
    St05EntryListEntryScriptMenu,
    St05EntryListEntryScriptSetUp,
    St05EntryListEntryScriptWrapUp,
    St05EntryListScript,
    St05RoomHandler,
    St05RoomHandlerA,
    St05RoomHandlerB,
    St05RoomHandlerC,
    St05RoomHelper, // ST05: no room list (the handler word cleared after it); sub 1 with its patch flag: the VRAM copy
    St05RoomSetup, // the room setup of ST05 (exe table 0x8d397c)
    St05StageSetup, // the stage setup of ST05 (exe table 0x8d3808)
    St06RoomHandler,
    St06RoomHandlerA,
    St06RoomHandlerB,
    St06RoomHandlerC,
    St06RoomSetup, // the room setup of ST06 (exe table 0x8d397c)
    St06StageSetup, // the stage setup of ST06 (exe table 0x8d3808)
    St06VoiceScript,
    St07RoomHelper, // ST07 (the Flutter's hangar): no room list (the handler word cleared first), the doors unlocked
    St07RoomSetup, // the room setup of ST07 (exe table 0x8d397c)
    St07StageSetup, // the stage setup of ST07 (exe table 0x8d3808)
    St07Sub1RoomHandler, // sub 1: from story 1 on, once per visit, the dropship (record 0x8c51e8: pool 0x20 type 0x6d at (0,
    St08PropUpdate, // ST08 static type 1 (variant 4: a prop you talk to, message 0x5f; content/yosyonke/town_props)
    St08RoomHelper, // ST08: two doors unlocked, the town's list; sub 1 from story 1 one cell switched
    St08RoomSetup, // the room setup of ST08 (exe table 0x8d397c)
    St08SecondRoomCallback, // the callback the stage setup of ST08 sets (0xa72230)
    St08StageSetup, // the stage setup of ST08 (exe table 0x8d3808)
    St08Story1Sub0RoomHandler, // story 1 sub 0: the Flutter and its cast
    St08Story1Sub1RoomHandler, // story 1 sub 1
    St09BystanderUpdate, // ST09 pool 0x20 type 0 sub 3: script 0x72 bystander (content/yosyonke/town_side_scripts)
    St09PropUpdate, // ST09 static type 1 (variant 5: a prop you talk to, message 0x13; 0 breakable, 1 kicked / thrown, 4 a solid; content/yosyonke/town_props)
    St09RoomHandler,
    St09RoomHandlerA,
    St09RoomHandlerB,
    St09RoomHandlerC, // the walkers, once script 0x72
    St09RoomHelper, // ST09: two doors unlocked, the town's list; past story 0xe (exe 0x57cb40) the repaired bridge
    St09RoomSetup, // the room setup of ST09 (exe table 0x8d397c)
    St09StageSetup, // the stage setup of ST09 (exe table 0x8d3808)
    St09StillModelBind,
    St09Story0Sub0RoomHandler, // story 0 sub 0: after the junk shop the walkers and, once, script 0x4e; the door lock
    St09Story1Sub0RoomHandler, // story 1 sub 0: the walkers, the door lock
    St0ARoomHandler,
    St0ARoomHandlerA,
    St0ARoomHandlerB,
    St0ARoomHandlerC,
    St0ARoomHandlerD,
    St0ARoomHandlerE, // story 1 sub 8 / later sub 3: the same
    St0ARoomHandlerF, // story 1 sub 9 / later sub 4: the records 0x8c58f0 x 1 + 0x8c5948 x 3, once
    St0ARoomHandlerG,
    St0ARoomHandlerH,
    St0ARoomHandlerI,
    St0ARoomHelper, // ST0A (the junk shop): the town's list; the shop bell unless sub 0 at story 0
    St0ARoomSetup, // the room setup of ST0A (exe table 0x8d397c)
    St0AStageSetup, // the stage setup of ST0A (exe table 0x8d3808)
    St0AStory0Sub3RoomHandler, // story 0 sub 3: the record 0x8c58d8, once
    St0AStory1Sub3RoomHandler, // story 1 sub 3
    St0BRoomHandler,
    St0BRoomHandlerA, // story 0..1 sub 3 / 2..0xf sub 1: the same
    St0BRoomHandlerB, // story 0..1 sub 2 / 2..0xf sub 0: the records 0x8c5bd0 x 2, once
    St0BRoomHandlerC, // sub 4 / 2..0xf sub 2 / 0x10.. sub 0: the same
    St0BRoomHandlerD, // sub 5 / 2..0xf sub 3 / 0x10.. sub 1: the records 0x8c5bf8 x 3 + 0x8c5cb0 x 7 once; the office
    St0BRoomHelper, // ST0B: the town's list; sub 1 from story 0x10 opens the cell (0x3f, 0x40)
    St0BRoomSetup, // the room setup of ST0B (exe table 0x8d397c)
    St0BStageSetup, // the stage setup of ST0B (exe table 0x8d3808)
    St0BStory0To1Sub0RoomHandler, // story 0..1 sub 0: the record 0x8c5bd0, once
    St0BStory0To1Sub1RoomHandler, // story 0..1 sub 1: the records 0x8c5bf8 x 3 + 0x8c5c38 x 6 once; the office (exe 0x57d220)
    St0CRoomHandler, // story 0..1 sub 3 / later sub 0: the same
    St0CRoomHandlerA, // story 0..1 sub 4 / later sub 1: the same
    St0CRoomHandlerB, // story 0..1 sub 5 / later sub 2: the records 0x8c5fc8 x 2 + 0x8c6030 x 3, once
    St0CRoomHelper, // ST0C: the handler word cleared, the town's list
    St0CRoomSetup, // the room setup of ST0C (exe table 0x8d397c)
    St0CStageSetup, // the stage setup of ST0C (exe table 0x8d3808)
    St0CStory0To1Sub0RoomHandler, // story 0..1 sub 0: the record 0x8c5f88, once
    St0CStory0To1Sub1RoomHandler, // story 0..1 sub 1: the records 0x8c5fa0 x 2 + 0x8c5ff0 x 3, once
    St0CStory0To1Sub2RoomHandler, // story 0..1 sub 2: the records 0x8c5fc8 x 1 + 0x8c6030 x 3, once
    St0DRoomCallback, // the callback the stage setup of ST0D sets (0xa72584)
    St0DRoomHelper, // ST0D
    St0DRoomSetup, // the room setup of ST0D (exe table 0x8d397c)
    St0DStageSetup, // the stage setup of ST0D (exe table 0x8d3808)
    St0DStory0Sub0RoomHandler, // ST0D story 0 sub 0: the walker 0x8c62d8
    St0DStoryFOnSub0RoomHandler, // ST0D stories 0xf.. (sub 0 from 0xf, then later subs): the walker 0x8c62ec x 1
    St0DWalkerRoomHandler, // ... 0x8c6314 x 1
    St0DWalkersRoomHandler, // ... 0x8c62ec x 2
    St0ERoomHelper, // ST0E: sub 2 no room list, else the town's list; sub 1 from story 0x10 opens the cell (0x29, 0x45)
    St0ERoomSetup, // the room setup of ST0E (exe table 0x8d397c)
    St0EStageSetup, // the stage setup of ST0E (exe table 0x8d3808)
    St0FRoomCallback, // the callback the stage setup of ST0F sets (0xa72584)
    St0FRoomSetup, // the room setup of ST0F (exe table 0x8d397c)
    St0FSecondRoomCallback, // the callback the stage setup of ST0F sets (0xa72230)
    St0FStageSetup, // the stage setup of ST0F (exe table 0x8d3808)
    St0FStory0Sub10RoomHandler, // ST0F story 0 sub 10: the music back (exe 0x5d58a0)
    St0FStory0Sub11RoomHandler, // ST0F story 0 sub 11: the music dipped; script 0x51 the first time, later the device / glow / relic;
    St0FStory0Sub12RoomHandler, // ST0F story 0 sub 12: script 0x52 the first time (the doors locked); its guardian's hit points
    St0FStory0Sub1RoomHandler, // ST0F story 0 sub 1: Roll by the lift; the player from x 0xc00 -> the ride (script 0x50)
    St0FStory0Sub8RoomHandler, // ST0F story 0 sub 8: the switch already set -> group 8 at once; else wait for it with no event
    St0FStory1Sub0RoomHandler, // ST0F story 1 sub 0: the ruin's doors unlocked once
    St0FStory1Sub11RoomHandler, // ST0F story 1 sub 11: the music channels dipped once
    St10FirstDecorationsRoomHandler,
    St10FirstLandingRoomHandler,
    St10LaterDecorationsRoomHandler,
    St10LaterLandingRoomHandler,
    St10RollerUpdate, // pool 0x20 ST10 sub 0: two-form enemy (wobbling roller, morphs when hit into a chaser that dies + drops; content/forbidden_island/st10_cls)
    St10RoomCallback, // the callback the stage setup of ST10 sets (0xa72584)
    St10RoomHelper, // ST10: the island's list by the story; from story 1, sub 0 only: the cells (0x44, 0x25) /
    St10RoomSetup, // the room setup of ST10 (exe table 0x8d397c)
    St10SecondRoomCallback, // the callback the stage setup of ST10 sets (0xa72230)
    St10StageSetup, // the stage setup of ST10 (exe table 0x8d3808)
    St11FighterUpdate, // pool 0x20 type 0x3b sub 0 (ST11 sub 0, story 1): script 0x2a fight actor -- approach / circle hops / leap / knockback, gauge 0x415d30, death explosion + drops (content/forbidden_island/st11_boss)
    St11FirstVisitRoomHandler,
    St11RoomHelper, // ST11: the island's list by the story
    St11RoomSetup, // the room setup of ST11 (exe table 0x8d397c)
    St11Script2a,
    St11Script2aFlashesStep,
    St11Script2aLooksStep,
    St11Script2aWalkInStep,
    St11StageSetup, // the stage setup of ST11 (exe table 0x8d3808)
    St12RoomCallback, // the callback the stage setup of ST12 sets (0xa72584)
    St12RoomHelper, // ST12: no room list; sub 0's map flag
    St12RoomSetup, // the room setup of ST12 (exe table 0x8d397c)
    St12SecondRoomCallback, // the callback the stage setup of ST12 sets (0xa72230)
    St12StageSetup, // the stage setup of ST12 (exe table 0x8d3808)
    St12Sub0RoomHandler, // sub 0: also the platform up
    St12Sub4RoomHandler, // sub 4
    St12Sub5RoomHandler, // sub 5
    St13GunTurretUpdate, // pool 0x20 type 0x26 (ST13): the gun turret (aims at the player, fires bursts of pool-0x40 type-0xd shots, staggers, sparks + dies; content/manda/st12_14)
    St13RoomHandler,
    St13RoomHandlerA,
    St13RoomHandlerB, // ST13 story 4 subs 2 / 6: their passage's flag once the breakable segment of its cell (column
    St13RoomHandlerC,
    St13RoomHelper, // ST13: no room list; sub 2 / sub 6 with their passage opened: two cells switched each
    St13RoomSetup, // the room setup of ST13 (exe table 0x8d397c)
    St13StageSetup, // the stage setup of ST13 (exe table 0x8d3808)
    St13Sub0RoomHandler, // sub 0: the exit open every tick; the door test
    St13Sub1RoomHandler, // sub 1
    St14BeamUpdate, // ST14 static type 0x2c: the Manda guardian's beam (content/manda/manda_boss)
    St14ControllerUpdate, // ST14 pool e0 type 0xc: the Manda guardian fight's controller (content/manda/manda_boss)
    St14DebrisUpdate, // ST14 pool 0x40 type 0x12: the Manda guardian's homing debris (content/manda/manda_boss)
    St14DroppingPuppetUpdate, // ST14 sub 4: script 0x1a's guardian dropping in (type 0x2b sub 1, content/manda/manda_boss)
    St14FloaterUpdate, // ST14 pool 0x20 type 0x3e: the floating target of the Manda guardian's beam (content/manda/manda_boss)
    St14GuardianUpdate, // ST14 sub 4: the Manda ruin guardian, the boss after script 0x1a (content/manda/manda_boss)
    St14RisingPuppetUpdate, // ST14 sub 4: script 0x1a's rising puppet (content/manda/manda_boss)
    St14RoomSetup, // the room setup of ST14 (exe table 0x8d397c)
    St14StageSetup, // the stage setup of ST14 (exe table 0x8d3808)
    St14Story4Sub2RoomHandler, // ST14 story 4 sub 2: once: the exit open, the third music channel restored
    St14Story4Sub3RoomHandler, // ST14 story 4 sub 3: once: the exit open, the third channel dipped, the guardian's record until it
    St15RoomHandler, // Start: the mission flag; Opening: the mission opening (message 0x10); Running: the win (flag 0x28,
    St15RoomHelper, // the room helper of ST15 (its room setup calls it)
    St15RoomSetup, // the room setup of ST15 (exe table 0x8d397c)
    St15SecondRoomCallback, // the callback the stage setup of ST15 sets (0xa72230)
    St15SprayUpdate, // pool 0x40 type 0x13 (ST15 / ST16): the spray of 0x4b6280 (three puffs, hit-list entries, 0x2c billboards; content/nino/st15_spray)
    St15StageSetup, // the stage setup of ST15 (exe table 0x8d3808)
    St15StillModelUpdate, // static ST15: still model (content/nino/st15_props)
    St16BarUpdate, // ST16 static type 1: the bars you hang from (variant 3 = the mission lever, flag 0x27; content/nino/bars_and_gate)
    St16CatchScript,
    St16CraneUpdate, // pool 0x20 type 0x44 (ST16 mission): the crane bringing a box (content/nino/st16_crane)
    St16RoomHandler, // Start: the mission flag, the opening's step cleared; Opening: the mission opening (message 0x13);
    St16RoomHelper, // the room helper of ST16 (its room setup calls it)
    St16RoomSetup, // the room setup of ST16 (exe table 0x8d397c)
    St16Script33GripStep,
    St16StageSetup, // the stage setup of ST16 (exe table 0x8d3808)
    St17GateUpdate, // ST17 static type 0x2f: the gate its lever opens (content/nino/bars_and_gate)
    St17LeverUpdate, // ST17 static type 1: the gate's lever (variant 1, content/nino/bars_and_gate)
    St17RoomHandler, // ST17 sub 0 at stories 7 / 8 (sub 3 at story 6): once 0x710 cleared, records 0x8c7468 x 2 +
    St17RoomHelper, // ST17: the island's list; after story 6 sub 0: the cell (0x3c, 0x3e) to variant 2
    St17RoomSetup, // the room setup of ST17 (exe table 0x8d397c)
    St17StageSetup, // the stage setup of ST17 (exe table 0x8d3808)
    St17Story6Sub0RoomHandler, // ST17 sub 0, story 6
    St17Sub0RoomHandler, // ST17 sub 0: once 0x710 cleared, records 0x8c7468 x 2 + 0x8c75d8 x 1, the handler's word 1
    St17TalkerUpdate, // pool 0x20 ST17 type 0x69: script 0x39 cast puppet (records 0x8a6a9c / 0x8a6ab0; content/nino/glyde_attack_talkers)
    St18BomberBirdbotUpdate, // pool 0x20 type 0x2d sub 1 (ST18 mission): the bomber Birdbots (content/nino/st18_bomber_birdbots)
    St18NpcUpdate, // pool 0x20 type 0 (variant 6, ST18 story 9 / 0xa): a standing NPC that talks (content/nino/st18_npc)
    St18RoomHandler,
    St18RoomHelper, // the room helper of ST18 (its room setup calls it)
    St18RoomSetup, // the room setup of ST18 (exe table 0x8d397c)
    St18SquarePropUpdate, // ST18 static type 0x2d subs 1 / 3: a still model (after script 0x37; content/nino/st18_props)
    St18StageSetup, // the stage setup of ST18 (exe table 0x8d3808)
    St18StillModelUpdate, // ST18 static type 1 sub 0: a still solid model, rooms 0 / 1 (content/nino/st18_props)
    St18Story6Sub2RoomHandler, // ST18 sub 2, story 6: once 0x710 cleared, records 0x8c7778 x 2, step 1
    St18Story7OnSub0RoomHandler, // ST18 sub 0, stories 7..: once the floor's 4 cells to variant 0, 0x710 cleared, record 0x8c77a0 x 1
    St18SunUpdate, // ST18 pool e0 type 1: the sun and its lens flares (content/nino/st18_props)
    St18TalkerUpdate, // pool 0x20 ST18 type 0x69: scripts 0x3a / 0x3b cast puppet, variants 0..2 (content/nino/glyde_attack_talkers)
    St19CutsceneDroneUpdate, // static ST19: cutscene drone (climb / script block) (content/nino/st15_props)
    St19MachineUpdate, // pool 0x20 (ST19): the scripted machinery (drops / slides / rises on script cues, opens with bursts; content/nino/st19)
    St19RoomHandler, // subs 1 (stories 6..8) / 5: step 0 -> 1 (2 with flag 0x25; 0x710 cleared); 1: flags 0x580 + 0x581,
    St19RoomHelper, // the room helper of ST19 (its room setup calls it)
    St19RoomSetup, // the room setup of ST19 (exe table 0x8d397c)
    St19StageSetup, // the stage setup of ST19 (exe table 0x8d3808)
    St19Story6Sub0RoomHandler, // sub 0, story 6: once records 0x8c7b18 / 0x8c7b2c / 0x8c7b40, the locks 0x711..0x713 set (0x710 /
    St19Story6Sub3RoomHandler, // sub 3, story 6: without flag 0x25 -> flag 0x25 + script 0x32 once
    St19Story7OnSub5RoomHandler, // ST19 sub 5, stories 7..: once 0x710 cleared (step 1); flag 0x38 -> flag 0x39
    St19Story7To9Sub0RoomHandler, // sub 0, stories 7..9: once 0x710 / 0x713..0x715 cleared, 0x711 / 0x712 set, the third lock's light
    St19Sub0RoomHandler, // ST19 sub 0: once 0x710 / 0x713 / 0x715 cleared, 0x711 / 0x712 / 0x714 set, the lock light at
    St19Sub1RoomHandler, // ST19 sub 1: once, with flag 0x3da: 0x710 cleared, the 4 cells (0x40 / 0x3f, 0x40 / 0x3f) to their
    St19TargetUpdate, // static ST19: still solid / hittable target (content/nino/st15_props)
    St1ARoomHelper, // the room helper of ST1A (its room setup calls it)
    St1ARoomSetup, // the room setup of ST1A (exe table 0x8d397c)
    St1AStageSetup, // the stage setup of ST1A (exe table 0x8d3808)
    St1BRoomHelper, // the room helper of ST1B (its room setup calls it)
    St1BRoomSetup, // the room setup of ST1B (exe table 0x8d397c)
    St1BStageSetup, // the stage setup of ST1B (exe table 0x8d3808)
    St1BStory6Sub2RoomHandler, // sub 2, story 6 (the arrival from script 0x32): once records 0x8c7ea8 x 2 (0x710 cleared); then
    St1BStory6Sub3RoomHandler, // sub 3, story 6: once 0x710 cleared, with flag 0x39 record 0x8c7ed0
    St1BSub1RoomHandler, // ST1B sub 1: the digger's licence exam (the handler's step)
    St1BSub2RoomHandler, // ST1B sub 2: once records 0x8c7ea8 x 2 (0x710 cleared)
    St1CRoomSetup, // the room setup of ST1C (exe table 0x8d397c)
    St1CStageSetup, // the stage setup of ST1C (exe table 0x8d3808)
    St1DFirstVisitRoomHandler,
    St1DRoomHelper, // ST1D: the island's list by the story; sub 0: three cells switched to variant 0
    St1DRoomSetup, // the room setup of ST1D (exe table 0x8d397c)
    St1DStageSetup, // the stage setup of ST1D (exe table 0x8d3808)
    St1DStory1Sub1RoomHandler, // story 1 sub 1: step 0 spawns the cast (and its flags); 1: the elephant's chapter done -> 2, else
    St1DStory1Sub2RoomHandler, // story 1 sub 2 (the elephant's arena): step 0 -> 1; 1: the boss down -> the exit light (the room's
    St1ERoomSetup, // the room setup of ST1E (exe table 0x8d397c)
    St1EStageSetup, // the stage setup of ST1E (exe table 0x8d3808)
    St1FRoomHelper, // ST1F (the square): Calbania's list by the story with the cell (0x40, 0x3b) to 1 / 0 and (0x3b,
    St1FRoomSetup, // the room setup of ST1F (exe table 0x8d397c)
    St1FStageSetup, // the stage setup of ST1F (exe table 0x8d3808)
    St1aMoverUpdate, // ST1A moving platform (turning / sliding + bobbing, stand-on solid; content/nino/st1a_movers)
    St1cCuedFigureCue1,
    St1cCuedFigureCue2,
    St1cCuedFigureCue3,
    St1cCuedFigureUpdate, // ST1C pool 0x20 type 0x1d: script 0x13 cast, content/forbidden_island/st1c_cast
    St1cFaceFigureCue1,
    St1cFaceFigureCue2,
    St1cFaceFigureCue3,
    St1cFaceFigureCue4,
    St1cFaceFigureCue5, // walks (z -0x33) until tick 0x78 and key 0x14, then stops (animation 0x10)
    St1cFaceFigureCue6,
    St1cFaceFigureCue7,
    St1cFaceFigureCue8,
    St1cFaceFigureUpdate, // ST1C pool 0x20 type 0x16: a figure of script 0x13 (content/forbidden_island/st1c_cast)
    St1cFallingFigureCue1,
    St1cFallingFigureCue10,
    St1cFallingFigureCue2,
    St1cFallingFigureCue3, // falls (y +0xc0 << 12 a tick) to the ground: animation 9, at its end 10
    St1cFallingFigureCue4, // no keys
    St1cFallingFigureCue5,
    St1cFallingFigureCue6,
    St1cFallingFigureCue7,
    St1cFallingFigureCue8,
    St1cFallingFigureCue9,
    St1cFallingFigureUpdate, // ST1C pool 0x20 type 0x5a: script 0x13 cast, content/forbidden_island/st1c_cast
    St1cFidgetingFigureCue1,
    St1cFidgetingFigureCue2,
    St1cFidgetingFigureCue4,
    St1cFidgetingFigureCue5,
    St1cFidgetingFigureCue6,
    St1cFidgetingFigureFidgetCue,
    St1cFidgetingFigureUpdate, // ST1C pool 0x20 type 0x28: script 0x13 cast, content/forbidden_island/st1c_cast
    St1cFlyerFlightCue, // the ST1C flyer's flight cue
    St1cFlyerUpdate, // ST1C pool 0x20 type 0x3f: script 0x13 cast (flight), content/forbidden_island/st1c_cast
    St1cGlowingFigureCue1,
    St1cGlowingFigureCue10,
    St1cGlowingFigureCue2,
    St1cGlowingFigureCue3, // rises (velocity y << 12 a tick)
    St1cGlowingFigureCue4,
    St1cGlowingFigureCue5,
    St1cGlowingFigureCue6,
    St1cGlowingFigureCue7,
    St1cGlowingFigureCue8,
    St1cGlowingFigureCue9,
    St1cGlowingFigureUpdate, // ST1C pool 0x20 type 0x5d: script 0x13 cast + effect a0/0x19 sub 3, content/forbidden_island/st1c_cast
    St1cGroundedHopperCue1,
    St1cGroundedHopperCue2,
    St1cGroundedHopperCue3,
    St1cGroundedHopperUpdate, // ST1C pool 0x20 type 0x59: script 0x13 cast, content/forbidden_island/st1c_cast
    St1cHopperCue1,
    St1cHopperCue2,
    St1cHopperShortHopCue,
    St1cHopperUpdate, // ST1C pool 0x20 type 0x68: script 0x13 cast, content/forbidden_island/st1c_cast
    St1cHoppingFlyerCue1,
    St1cHoppingFlyerCue2,
    St1cHoppingFlyerCue3, // the hop: animation 7 (z -0x100), at its end animation 8, up; at 0x3c stopped, hidden
    St1cHoppingFlyerCue4,
    St1cHoppingFlyerUpdate, // ST1C pool 0x20 type 0x2f: script 0x13 cast, content/forbidden_island/st1c_cast
    St1cIdleFigureCue1,
    St1cIdleFigureCue2,
    St1cIdleFigureCue3,
    St1cIdleFigureCue4,
    St1cIdleFigureCue5, // animation 0 at tick 0 (the cursors are not cleared)
    St1cIdleFigureUpdate, // ST1C pool 0x20 type 0x6b: script 0x13 cast, content/forbidden_island/st1c_cast
    St1cMeshFlipFigureCue1,
    St1cMeshFlipFigureCue2,
    St1cMeshFlipFigureCue3,
    St1cMeshFlipFigureCue4,
    St1cMeshFlipFigureCue5,
    St1cMeshFlipFigureCue6,
    St1cMeshFlipFigureCue7,
    St1cMeshFlipFigureUpdate, // ST1C pool 0x20 type 0x1a: a figure of script 0x13 (content/forbidden_island/st1c_cast)
    St1cPosingFigureCue1,
    St1cPosingFigureCue2,
    St1cPosingFigureCue3,
    St1cPosingFigureCue4, // jump table 0x482c1c / 0x482c30
    St1cPosingFigureCue5,
    St1cPosingFigureCue6,
    St1cPosingFigureCue7,
    St1cPosingFigureCue8,
    St1cPosingFigureUpdate, // ST1C pool 0x20 type 0x21: script 0x13 cast, content/forbidden_island/st1c_cast
    St1cRisingPropCue1, // rises: speed z -0xc0, sparks every other frame
    St1cRisingPropCue2, // pitched 0x100, moves -0x10 along its own z, sparks every fourth frame
    St1cRisingPropCue3, // thrown up (0x380, -4 a tick), then it bobs on the sine table
    St1cRisingPropCue4, // bobs from here (phase 0, the base its height)
    St1cRisingPropUpdate, // ST1C pool 0x20 type 0x27: a prop of script 0x13 that rises, sparks and bobs (content/forbidden_island/st1c_cast)
    St1cScript13,
    St1cScript13EffectAgainStep,
    St1cScript13EffectEndsStep,
    St1cScript13FadeInToRoom4Step,
    St1cScript13FadeToRoom5Step,
    St1cScript13FastFadeOutStep,
    St1cScript13FinalFadeStep,
    St1cScript13GlowStep,
    St1cScript13GroundPoseStep,
    St1cScript13HiddenCue,
    St1cScript13LongFadeInStep,
    St1cScript13LookUpStep,
    St1cScript13OpeningCuesStep,
    St1cScript13OpeningWaitStep,
    St1cScript13PausedPoseStep,
    St1cScript13SecondCastStep,
    St1cScript13SecondVoiceStep,
    St1cScript13Step13,
    St1cScript13Step14,
    St1cScript13Step15,
    St1cScript13Step16,
    St1cScript13Step17,
    St1cScript13Step18,
    St1cScript13Step19,
    St1cScript13Step21,
    St1cScript13Step22,
    St1cScript13Step23,
    St1cScript13Step30,
    St1cScript13Step31,
    St1cScript13Step32,
    St1cScript13Step33,
    St1cScript13Step34,
    St1cScript13Step35,
    St1cScript13Step37,
    St1cScript13Step38,
    St1cScript13Step39,
    St1cScript13Step40,
    St1cScript13Step41,
    St1cScript13Step42,
    St1cScript13Step6,
    St1cScript13Step7,
    St1cScript13ThirdCastStep,
    St1cScript13ThirdVoiceStep,
    St1cScript13TwoPosesStep,
    St1cSecondStillModelUpdate, // static ST1C: still model publishing 0xa5a924 (content/forbidden_island/st1c_cast)
    St1cShipCue1, // turns slowly
    St1cShipCue2, // turns, slowing from 0x78
    St1cShipCue3, // the flare at 0x78; from 0x3c it turns the other way, faster, smoking
    St1cShipCue4, // turning fast, low, smoking every tick
    St1cShipUpdate, // static ST1C: the bobbing ship of script 0x13 (smoke / flare, 0xa5a900; content/forbidden_island/st1c_cast)
    St1cStillModelUpdate, // static ST1C: still model publishing 0xa5a920 (content/forbidden_island/st1c_cast)
    St1cVanishingWalkerCue1,
    St1cVanishingWalkerCue2,
    St1cVanishingWalkerCue3,
    St1cVanishingWalkerCue4, // walks (z -0x31)
    St1cVanishingWalkerUpdate, // ST1C pool 0x20 type 0x3c: script 0x13 cast, content/forbidden_island/st1c_cast
    St1cWalkingFigureCue1,
    St1cWalkingFigureCue2,
    St1cWalkingFigureCue3,
    St1cWalkingFigureCue4, // walks (z -0x31)
    St1cWalkingFigureUpdate, // ST1C pool 0x20 type 0x1e: script 0x13 cast, content/forbidden_island/st1c_cast
    St1dBoulderTrailUpdate, // ST1D static type 0x20 sub 2: a boulder's trail (content/forbidden_island/elephant_boss)
    St1dBoulderUpdate, // ST1D pool 0x40 type 0x14: the boss's boulder (sub 0) / shock ring (sub 1), content/forbidden_island/elephant_boss
    St1dCrystalUpdate, // ST1D static type 1: the crystal (variant 0) and its pedestal (1, use -> flag 0x681), content/forbidden_island/elephant_boss
    St1dDustRingUpdate, // ST1D effect type 0x11: a boulder's dust ring (content/forbidden_island/elephant_boss)
    St1dExitLightUpdate, // ST1D static type 0x28: the arena's exit light (content/forbidden_island/elephant_boss)
    St1dPillarUpdate, // ST1D static type 0x20 sub 1: an arena pillar (content/forbidden_island/elephant_boss)
    St1dRockUpdate, // ST1D static type 0x20 sub 0: a falling / thrown rock (content/forbidden_island/elephant_boss)
    St1fBirdbotTankUpdate, // pool 0x20 type 0x40 sub 0 (ST1F): the Birdbot tank -- turret head, keeps distance, bursts of pool 0x40 type 9, slope tilt, rideable (content/calbania/st1f_birdbot_tank)
    St20PropUpdate, // static ST20 type 1: props (variant 0 solid, rings when hit (sound 0xc7), 1 spins, 2 swings; content/saul_kada/st20_props)
    St20RoomHelper, // ST20 (Calbania's town): Calbania's list by the story, two doors unlocked
    St20RoomSetup, // the room setup of ST20 (exe table 0x8d397c)
    St20StageSetup, // the stage setup of ST20 (exe table 0x8d3808)
    St21RisingModelBob,
    St21RisingModelDrift,
    St21RisingModelRise,
    St21RisingModelSink,
    St21RoomCase, // ST21 (the stage case): only the door test (game/transition)
    St21RoomHelper, // the room helper of ST21 (its room setup calls it)
    St21RoomSetup, // the room setup of ST21 (exe table 0x8d397c)
    St21StageSetup, // the stage setup of ST21 (exe table 0x8d3808)
    St22FireShotUpdate, // pool 0x40 ST22 stage type 1: fire projectiles, variants 0-4 (content/calbania/st22_fire_shots)
    St22RoomCase, // ST22 (the stage case): step 0: the door locked, step 1; 1: one of the keys -> unlocked, step 2
    St22RoomHelper, // the room helper of ST22 (its room setup calls it)
    St22RoomSetup, // the room setup of ST22 (exe table 0x8d397c)
    St22StageSetup, // the stage setup of ST22 (exe table 0x8d3808)
    St23RoomHelper, // ST23: Calbania's list by the story; sub 0: the cell (0x40, 0x49) to variant 0 before story 0x11
    St23RoomSetup, // the room setup of ST23 (exe table 0x8d397c)
    St23StageSetup, // the stage setup of ST23 (exe table 0x8d3808)
    St24GateGuardianUpdate, // ST24 pool 0x20 type 0x24 sub 0: the guardian at the ruin gate of script 0x14 (record 0x8a1e2c), fades (content/manda/st24_ruin_gate)
    St24GateScript,
    St24GateStep1,
    St24GateStep2,
    St24RoomHelper, // ST24: Pokte's list; story 3 sub 0 before the first visit: a cell switched; the doors unlocked
    St24RoomSetup, // the room setup of ST24 (exe table 0x8d397c)
    St24RouteNpcUpdate, // ST24 talking route-walker NPC (sibling of 0x441950: route / stationary / talk / face the player; content/calbania/st24_npc)
    St24Script56, // script 0x56
    St24SecondRoomCallback, // the callback the stage setup of ST24 sets (0xa72230)
    St24StageSetup, // the stage setup of ST24 (exe table 0x8d3808)
    St24Story3Sub0RoomHandler, // ST24 story 3 sub 0: the first visit: one actor of the records and script 0x56; later the six
    St24Story4OnSub5RoomHandler, // ST24 sub 5 (story 4..): its actor, once per visit
    St24Story4Sub1RoomHandler, // ST24 story 4 sub 1: back from the ruin gate -> script 0x14 once
    St24Story4Sub2RoomHandler, // ST24 story 4 sub 2 (the ruin gate): its actor; until the cutscene the gate's exit (its id 1)
    St24StructureUpdate, // ST24 type 0x30 story structure (animated stand-on solid; content/calbania/st24_structure)
    St25RisingModelUpdate, // static ST25: script-driven rise / bob / sink with dust (content/calbania/st21_props)
    St25RoomHelper, // ST25: Pokte's list
    St25RoomSetup, // the room setup of ST25 (exe table 0x8d397c)
    St25Script1cFigureUpdate, // ST25 pool 0x20 type 9 sub 1: the figure of script 0x1c (record 0x8a303c): hops off, sparks, drives (content/manda/st25_tron_scenes)
    St25StageSetup, // the stage setup of ST25 (exe table 0x8d3808)
    St25Story3Sub5RoomHandler, // ST25 story 3 sub 5: once (its flag) script 0x1b, Tron at the gate
    St25Story4OnSub1To4RoomHandler, // ST25 story 4.. subs 1..4 (the houses' insides): from story 5 or after the ruin gate one villager
    St25TankLandingUpdate, // ST25 sub 6: the tank as it lands, the cast of script 0x1b (content/manda/st25_tron_scenes)
    St25TankTankDropCue,
    St25TankTankOnGroundCue,
    St25TankTankStandsCue,
    St25TankWreckUpdate, // ST25 sub 6 type 0x17: the tank wreck of script 0x1c (content/manda/st25_tron_scenes)
    St25TronSceneAnswerStep,
    St25TronSceneAtGateStep,
    St25TronSceneEndStep,
    St25TronSceneFacesStopStep,
    St25TronSceneFirstWordsStep,
    St25TronSceneGateOpensStep,
    St25TronSceneLastWordsStep,
    St25TronSceneListenStep,
    St25TronSceneLongTalkStep,
    St25TronSceneRunInStep,
    St25TronSceneSecondWordsStep,
    St25TronSceneSlowDownStep,
    St25TronSceneStep10,
    St25TronSceneStep5,
    St25TronSceneStep7,
    St25TronSceneTronBeatenStep,
    St25TronSceneTronLeavesStep,
    St25TronSceneTronTalksStep,
    St25TronSceneTurnAndWalkStep,
    St25TronSceneTurnToVillageStep,
    St25TronSceneVillagersTalkStep,
    St25TronSceneWalkToTronStep,
    St25TronSceneWreckStep,
    St25TronScript1b,
    St25TronScript1c,
    St25TronTronCue1Cue,
    St25TronTronCue2Cue,
    St25TronTronCue3Cue,
    St25TronTronCue4Cue,
    St25TronTronCue5Cue,
    St25TronUpdate, // ST25 sub 5: Tron at the gate, the cast of script 0x1b (content/manda/st25_tron_scenes)
    St26RoomCallback, // the callback the stage setup of ST26 sets (0xa72584)
    St26RoomHandler,
    St26RoomHandlerA,
    St26RoomHandlerB,
    St26RoomHelper, // ST26: no room list; sub 0's map flag; sub 5 opens its two passages' cells
    St26RoomSetup, // the room setup of ST26 (exe table 0x8d397c)
    St26SecondRoomCallback, // the callback the stage setup of ST26 sets (0xa72230)
    St26StageSetup, // the stage setup of ST26 (exe table 0x8d3808)
    St26Sub2RoomHandler, // ST26 sub 2
    St26Sub3RoomHandler, // ST26 sub 3
    St26Sub6RoomHandler, // sub 6: its record unless flag 0x56
    St27RoomHandler,
    St27RoomHandlerA,
    St27RoomHandlerB,
    St27RoomHelper, // ST27: no room list; sub 8 with its floor fallen opens five cells (the exe opens two of them
    St27RoomSetup, // the room setup of ST27 (exe table 0x8d397c)
    St27StageSetup, // the stage setup of ST27 (exe table 0x8d3808)
    St27Sub0RoomHandler, // sub 0: with flag 0x56 or without 0x52 its records (by the player's side)
    St27Sub0RoomHandlerA, // ST27 sub 0
    St27Sub1RoomHandler, // sub 1
    St27Sub2RoomHandler, // ST27 sub 2
    St27Sub7RoomHandler, // sub 7
    St27Sub8RoomHandler, // ST27 sub 8
    St28GuardUpdate, // ST28 pool 0x20 type 0x6c sub 0: the fire ruin's guard (watches, shoots, charges within 0x708 of home; content/saul_kada/st28_guard)
    St28LavaBossFall, // the ST28 lava boss's fall (a gap's message names it)
    St28LavaBossLanding, // the ST28 lava boss's landing (a gap's message names it)
    St28RoomHandler,
    St28RoomHandlerA,
    St28RoomHandlerB,
    St28RoomHandlerC,
    St28RoomHelper, // ST28: no room list; outside sub 0 the field encounters suspended (exe 0x5e6870)
    St28RoomSetup, // the room setup of ST28 (exe table 0x8d397c)
    St28StageSetup, // the stage setup of ST28 (exe table 0x8d3808)
    St28Sub0RoomHandler, // ST28 sub 0: the fight's flags cleared
    St28Sub2RoomHandler, // sub 2
    St29RoomHandler,
    St29RoomHandlerA,
    St29RoomHandlerB, // story 12 sub 2 / later sub 0
    St29RoomHandlerC, // story 12 sub 3 / later sub 1
    St29RoomHelper, // ST29: the island's list by the story; sub 1 after story 0xc opens the cell (0x40, 0x49)
    St29RoomSetup, // the room setup of ST29 (exe table 0x8d397c)
    St29SecondRoomCallback, // the callback the stage setup of ST29 sets (0xa72230)
    St29StageSetup, // the stage setup of ST29 (exe table 0x8d3808)
    St2ARoomHandler,
    St2ARoomHandlerA,
    St2ARoomHandlerB,
    St2ARoomHelper, // the room helper of ST2A (its room setup calls it)
    St2ARoomSetup, // the room setup of ST2A (exe table 0x8d397c)
    St2AStageSetup, // the stage setup of ST2A (exe table 0x8d3808)
    St2BRoomHandler,
    St2BRoomHandlerA, // story 12 subs 2 / 3
    St2BRoomHelper, // the room helper of ST2B (its room setup calls it)
    St2BRoomSetup, // the room setup of ST2B (exe table 0x8d397c)
    St2BStageSetup, // the stage setup of ST2B (exe table 0x8d3808)
    St2BStoryCSub0RoomHandler, // story 12 sub 0
    St2CRoomHelper, // the room helper of ST2C (its room setup calls it)
    St2CRoomSetup, // the room setup of ST2C (exe table 0x8d397c)
    St2CStageSetup, // the stage setup of ST2C (exe table 0x8d3808)
    St2CStoryCSub0RoomHandler, // story 12 sub 0
    St2DRoomCallback, // the callback the stage setup of ST2D sets (0xa72584)
    St2DRoomHelper, // ST2D / ST2E: the script slot spawns the group of the sub-room and the flag (exe 0x5e66b0), then
    St2DRoomSetup, // the room setup of ST2D (exe table 0x8d397c)
    St2DSecondRoomCallback, // the callback the stage setup of ST2D sets (0xa72230)
    St2DStageSetup, // the stage setup of ST2D (exe table 0x8d3808)
    St2DSub0RoomHandler, // sub 0: the door first; the hints of the entrance
    St2DSub2RoomHandler, // sub 2: the door first; before the ruin is cleared, standing on an actor -> its hint once
    St2DSub5RoomHandler, // sub 5
    St2DSub6RoomHandler, // sub 6: the door first
    St2DSub7RoomHandler, // sub 7: the door first; before the ruin is cleared its hint once
    St2ERoomSetup, // the room setup of ST2E (exe table 0x8d397c)
    St2EStageSetup, // the stage setup of ST2E (exe table 0x8d3808)
    St2ESub2RoomHandler, // sub 2: the door first; before the ruin is cleared: its hint once; on the floor (y 0) another once
    St2ESub3RoomHandler, // sub 3: the door first; before the ruin is cleared its hint once
    St2ESub7RoomHandler, // sub 7: the door first; before the ruin is cleared its hint once
    St2FRoomHelper, // no room list
    St2FRoomHelperA, // ST2F: the field encounters suspended in its sub-rooms of 0 (exe 0x5e6870), else resumed; the
    St2FRoomSetup, // the room setup of ST2F (exe table 0x8d397c)
    St2FStageSetup, // the stage setup of ST2F (exe table 0x8d3808)
    St2FSub0RoomHandler, // sub 0: the door first; its hint once
    St2FSub1RoomHandler, // sub 1
    St2FSub5RoomHandler, // sub 5: the door first; once the music back and, up to story 0x10 before the reward, its record
    St2FSub6RoomHandler, // sub 6: the door first
    St2FSub7RoomHandler, // sub 7: the door first
    St2FSub8RoomHandler, // sub 8: the door first
    St2bPropPlateBind,
    St2bPropPlateRun,
    St2bPropPotBreak,
    St2bPropPotCarried,
    St2bPropPotFlight,
    St2bPropPotMain,
    St2bPropPotRest,
    St2bPropPotWait,
    St2bPropRebindInPlace,
    St2bPropSwitchRun,
    St2bPropUpdate, // ST2B static type 1: pots (lift / throw / break), plates, floor switches of flags 0x96 / 0x97 (content/saul_kada/st2b_props)
    St2eRoom4Handler,
    St30RoomHelper, // ST30 (Glyde's base): no room list; sub 0's map flag; with its boxes open six cells to variant 1
    St30RoomSetup, // the room setup of ST30 (exe table 0x8d397c)
    St30StageSetup, // the stage setup of ST30 (exe table 0x8d3808)
    St31RoomHelper, // ST31 (Glyde's base, inside): no room list; its room scripts' counters cleared
    St31RoomSetup, // the room setup of ST31 (exe table 0x8d397c)
    St31StageSetup, // the stage setup of ST31 (exe table 0x8d3808)
    St32PlateUpdate, // ST32 statics: plates (sink under the player, move by +0xc, floor switches -> flags 0x96 / 0x97; content/saul_kada/st2b_props)
    St32QuizStep, // script 0x2b's one step (the quiz)
    St32RoomHandler, // step 0: unless story 4 without flag 0x581, the three contestants tracked into the stage block
    St32RoomHelper, // ST32: Pokte's list; the quiz's block marked empty
    St32RoomSetup, // the room setup of ST32 (exe table 0x8d397c)
    St32StageSetup, // the stage setup of ST32 (exe table 0x8d3808)
    St33KeyDropScript, // script 0x2f's handler
    St33KeyScriptDropStep,
    St33KeyScriptFallStep,
    St33KeyScriptPlaceStep,
    St33KeyScriptTurnStep,
    St33RoomHandler,
    St33RoomSetup, // the room setup of ST33 (exe table 0x8d397c)
    St33StageSetup, // the stage setup of ST33 (exe table 0x8d3808)
    St33StripScrollUpdate, // ST33 pool e0 type 0x1a: 16x64 VRAM strip scroller (0x630,0x180)->(0x630,0x140) (vramMove; content/saul_kada/st33_strip_scroll)
    St34RoomSetup, // the room setup of ST34 (exe table 0x8d397c)
    St34StageSetup, // the stage setup of ST34 (exe table 0x8d3808)
    St34Sub5RoomHandler, // ST34 sub 5
    St35BeatenScript,
    St35BeatenStep2,
    St35BeatenStep3,
    St35BeatenStep4,
    St35BeatenVoiceStep,
    St35DissolvesScript,
    St35DissolvesStep2,
    St35DissolvesStep3,
    St35DissolvesStep4,
    St35DissolvesStep5,
    St35DissolvesVoiceStep,
    St35DropsInScript,
    St35DropsInStep1,
    St35DropsInStep2,
    St35DropsInStep3,
    St35DropsInStep4,
    St35RoomCallback, // the callback the stage setup of ST35 sets (0xa72584)
    St35RoomHandler,
    St35RoomHandlerA,
    St35RoomHelper, // ST35 (the Nino ruin): no room list; sub 0's map flag; the doors unlocked
    St35RoomSetup, // the room setup of ST35 (exe table 0x8d397c)
    St35SecondRoomCallback, // the callback the stage setup of ST35 sets (0xa72230)
    St35StageSetup, // the stage setup of ST35 (exe table 0x8d3808)
    St35Sub1RoomHandler, // sub 1: step 0 -> 1 once the first guardian is beaten
    St35Sub3RoomHandler, // sub 3: once, before the guardian woke, the record 0x8cb1c8 (pool 0x20 type 1)
    St35Sub4RoomHandler, // sub 4: once, without both keys the lock 0x711
    St35WakesScript,
    St35WakesStep2,
    St35WakesStep3,
    St35WakesStep4,
    St35WakesStep5,
    St35WakesStep6,
    St35WakesVoiceStep,
    St36RoomHandler, // ST36 / ST37 / ST38 sub 1: step 0 -> the zone-slot group 0, step 1
    St36RoomHelper, // ST36: no room list, then the cells / sound (st36_rooms)
    St36RoomSetup, // the room setup of ST36 (exe table 0x8d397c)
    St36StageSetup, // the stage setup of ST36 (exe table 0x8d3808)
    St36Sub2RoomHandler, // ST36 sub 2: step 0 -> the zone-slot group 1, step 1
    St36Sub4RoomHandler, // ST36 sub 4: flag 0x9a once the floor cell (0x3b, 0x3c) is open; step 0 -> the group 3 when
    St36Sub4RoomHandlerB, // flooded, else 2. Sub 10: the same with flag 0x9b, the cell (0x3f, 0x3f), the groups 8 / 7
    St36Sub5RoomHandler, // ST36 sub 5: step 0 -> the group 6 when flooded, else 5
    St37OpeningScript,
    St37OpeningStep1, // the player walks in
    St37OpeningStep2,
    St37OpeningStep3,
    St37OpeningStep4,
    St37OpeningStep5,
    St37RoomHandler,
    St37RoomHelper, // ST37: likewise (st37_rooms)
    St37RoomSetup, // the room setup of ST37 (exe table 0x8d397c)
    St37StageSetup, // the stage setup of ST37 (exe table 0x8d3808)
    St37StoryASub13RoomHandler, // sub 13, story 10: the group 7 (0xa drained); the near hint by the player's x
    St37StoryASub2RoomHandler, // sub 2, story 10: the group 1 (9 drained); the two hints by the player's z
    St37StoryASub4RoomHandler, // sub 4, story 10: the group 2; the far hint marked seen unless drained
    St37StoryBOnSub13RoomHandler, // sub 13, stories 11..: the group 7 (0xa drained)
    St37StoryBOnSub2RoomHandler, // sub 2, stories 11..: the group 1 (9 drained)
    St37StoryBOnSub4RoomHandler, // sub 4, stories 11..
    St37Sub14RoomHandler, // sub 14
    St37Sub5RoomHandler, // sub 5: the group 3
    St37Sub8RoomHandler, // sub 8: the group 5 (0xb drained)
    St37Sub9RoomHandler, // sub 9: the group 6
    St38RoomHandler, // subs 10 / 12: 0: step 2, or 1 when the boss music latch was set (cleared); 1: the music faded
    St38RoomHelper, // ST38 (the Nino ruin's lowest floor): no room list; sub 5 with its cell opened: the cell (0x41,
    St38RoomSetup, // the room setup of ST38 (exe table 0x8d397c)
    St38StageSetup, // the stage setup of ST38 (exe table 0x8d3808)
    St38Sub11RoomHandler, // sub 11 (the boss room): 0: not drained -> records 0x8cb638 x 0x11 (the boss + 16 type 0x42), the
    St38Sub2RoomHandler, // sub 2: once: drained -> the group 9; else the group 1 + records 0x8cb548 x 4 (type 0x42)
    St38Sub3RoomHandler, // sub 3: once: the group 2 (drained) / 3
    St38Sub5RoomHandler, // sub 5: every tick: the cell (0x41, 0x3e) broken open -> flag 0x9f; once: the group 4 / 5
    St38Sub6RoomHandler, // sub 6: once: the group 6 (drained) / 7
    St38Sub7RoomHandler, // sub 7: once: not drained -> records 0x8cb548 x 4
    St38Sub8RoomHandler, // sub 8: once: the group 8; not drained -> records 0x8cb598 x 8
    St39CastMember,
    St39RoomSetup, // the room setup of ST39 (exe table 0x8d397c)
    St39Script68,
    St39SetUpState,
    St39StageSetup, // the stage setup of ST39 (exe table 0x8d3808)
    St39TimerState,
    St39WrapUpState,
    St3ARoomSetup, // the room setup of ST3A (exe table 0x8d397c)
    St3AStageSetup, // the stage setup of ST3A (exe table 0x8d3808)
    St3BRoomHandler,
    St3BRoomHandlerA,
    St3BRoomHelper, // the room helper of ST3B (its room setup calls it)
    St3BRoomSetup, // the room setup of ST3B (exe table 0x8d397c)
    St3BStageSetup, // the stage setup of ST3B (exe table 0x8d3808)
    St3CRoomHandler,
    St3CRoomHandlerA,
    St3CRoomHelper, // ST3C: the island's list by the story; sub 3: the cell (0x40, 0x40), variant 1 before story 0xd,
    St3CRoomSetup, // the room setup of ST3C (exe table 0x8d397c)
    St3CStageSetup, // the stage setup of ST3C (exe table 0x8d3808)
    St3CSub0RoomHandler, // sub 0
    St3CSub1RoomHandler, // sub 1: flags 0x710 / 0x711 cleared, 0x712 until the key 0x3ec, its records
    St3CSub2RoomHandler, // sub 2
    St3CSub3RoomHandler, // sub 3
    St3DRoomHandler, // stories 0..1 sub 4 / 2..4 sub 0
    St3DRoomHandlerA, // stories 0..1 sub 5 / 2..4 sub 1
    St3DRoomHandlerB, // stories 0..1 sub 7 / 2..4 sub 3
    St3DRoomHelper, // ST3D: no room list; the room flags and a door cleared; by sub: 0 the door lock 1 before story 0x11
    St3DRoomSetup, // the room setup of ST3D (exe table 0x8d397c)
    St3DSecondRoomCallback, // the callback the stage setup of ST3D sets (0xa72230)
    St3DStageSetup, // the stage setup of ST3D (exe table 0x8d3808)
    St3DStory5OnSub0RoomHandler, // stories 5.. sub 0
    St3DStory5OnSub0RoomHandlerB, // stories 5.. sub 0
    St3DStory5OnSub0RoomHandlerC, // stories 5.. sub 0
    St3DStory5OnSub1RoomHandler, // stories 5.. sub 1
    St3DStory5OnSub1RoomHandlerB, // stories 5.. sub 1
    St3DStory5OnSub1RoomHandlerC, // stories 5.. sub 1
    St3DStory5ToASub3RoomHandler, // stories 5..0xa sub 3
    St3DStoryBToDSub2RoomHandler, // stories 0xb..0xd sub 2
    St3ERoomHelper, // ST3E: no room list; the room flags of subs 4 / 5; the music channels dipped in sub 2, else
    St3ERoomSetup, // the room setup of ST3E (exe table 0x8d397c)
    St3EStageSetup, // the stage setup of ST3E (exe table 0x8d3808)
    St3ESub0RoomHandler, // sub 0: story 2 after its event and before the talk -> script 0x64 (the step stays 0), else on
    St3ESub1RoomHandler, // sub 1
    St3ESub2RoomHandler, // sub 2: below story 0x11 the visitors of the story (from 5, 0xb, 0xe), once
    St3ESub3RoomHandler, // sub 3: story 2 before its event
    St3ESub4RoomHandler, // sub 4: story 0xb..0xd
    St3ESub5RoomHandler, // sub 5: story 2..4 / 5..0xa
    St3ESub6RoomHandler, // sub 6, every tick (no step): the story's talk -- story 2 before its event, 4 / 0xa / 0xd with a
    St3FRoomHandler,
    St3FRoomHelper, // ST3F: no room list, the door lock 0 cleared
    St3FRoomSetup, // the room setup of ST3F (exe table 0x8d397c)
    St3FStageSetup, // the stage setup of ST3F (exe table 0x8d3808)
    St3FStory11OnSub1RoomHandler, // stories 0x11.. sub 1
    St3FStory2OnSub3RoomHandler, // sub 3 (stories 2..)
    St3FStoryBToDSub1RoomHandler, // stories 0xb..0xd sub 1
    St3FStoryETo10Sub1RoomHandler, // stories 0xe..0x10 sub 1
    St3FSub1RoomHandler, // sub 1
    St3cPropUpdate, // ST3C static type 1: a placed model (content/saul_kada/st3c_props)
    St3cRisingModelUpdate, // static ST3C: rising / bobbing or sinking model (content/saul_kada/st3c_rising_model)
    St3eFloatingModelUpdate, // static ST3E: floating bobbing model (content/forbidden_island/st3e_reports)
    St3eKeyReport65, // script 0x65: a ruin key reported
    St3eKeyReport66, // script 0x66
    St3eKeyReport67, // script 0x67
    St3eReport64Step10,
    St3eReport64Step2,
    St3eReport64Step3,
    St3eReport64Step4,
    St3eReport64Step5,
    St3eReport64Step6,
    St3eReport64Step7,
    St3eReport64Step8,
    St3eReport64Step9,
    St3eReport64WalkInStep,
    St3eReport65FirstSpeakerStep,
    St3eReport65LastStep,
    St3eReport65SecondSpeakerStep,
    St3eReport65WalkInStep,
    St3eReport66FirstSpeakerStep,
    St3eReport66LastStep,
    St3eReport66SecondSpeakerStep,
    St3eReport66WalkInStep,
    St3eReport67FirstSpeakerStep,
    St3eReport67LastStep,
    St3eReport67SecondSpeakerStep,
    St3eReport67WalkInStep,
    St3eScript64,
    St3eSpeakerUpdate, // pool 0x20 type 0: a speaker of the ST3E talks (content/forbidden_island/st3e_reports)
    St40RoomCallback, // the callback the stage setup of ST40 sets (0xa72584)
    St40RoomHandlerSub10, // ST40 sub 10: the teleporter pads, the music dipped
    St40RoomHandlerSub11, // ST40 sub 11: the teleporter pads, the music dipped
    St40RoomHandlerSub12, // ST40 sub 12: the teleporter pads, the music dipped
    St40RoomHandlerSub13, // ST40 sub 13: the teleporter pads, the music dipped
    St40RoomHandlerSub3, // ST40 sub 3: the teleporter pads (panel 3 still), extra records, the music restored
    St40RoomHandlerSub4, // ST40 sub 4: the teleporter pads, the music dipped
    St40RoomHandlerSub5, // ST40 sub 5: the teleporter pads (panel 0 still), extra records, the music restored
    St40RoomHandlerSub6, // ST40 sub 6: the teleporter pads, the music dipped
    St40RoomHandlerSub7, // ST40 sub 7: the teleporter pads (panel 0 still), extra records, the music restored
    St40RoomHandlerSub8, // ST40 sub 8: the teleporter pads, the music dipped
    St40RoomHandlerSub9, // ST40 sub 9: the teleporter pads, the music dipped
    St40RoomHelper, // ST40: no room list; the switches' locks and the room flags cleared; by sub the switches, the
    St40RoomSetup, // the room setup of ST40 (exe table 0x8d397c)
    St40SecondRoomCallback, // the callback the stage setup of ST40 sets (0xa72230)
    St40StageSetup, // the stage setup of ST40 (exe table 0x8d3808)
    St40Sub0RoomHandler, // ST40 sub 0: 0: pads 0x8cc710 x 4 + e0 0x8cc6e4 x 2, the music dipped unless flag 0x2f6 without
    St40Sub1RoomHandler, // ST40 sub 1 (script 0x69's arrival): 0: music dipped -> 1 (same tick); 1: without flag 0x150 that
    St40Sub2RoomHandler, // ST40 sub 2: pads 0x8cc694 x 3 (the room's actors 1..3: still / animated / animated) + e0 0x8cc6d0,
    St41RoomCallback, // the callback the stage setup of ST41 sets (0xa72584)
    St41RoomHelper, // ST41 / ST42: no room list, the script slot's group of the sub-room (exe 0x5e66b0), 16 doors
    St41RoomSetup, // the room setup of ST41 (exe table 0x8d397c)
    St41SecondRoomCallback, // the callback the stage setup of ST41 sets (0xa72230)
    St41StageSetup, // the stage setup of ST41 (exe table 0x8d3808)
    St41Sub0RoomHandler, // sub 0: the door; 0x14 ticks after entry, once (flag 0x590), the message 0x28
    St41Sub12RoomHandler, // sub 12: the door; the pair 0x8cc980; the room actors 1 / 2 both down -> done, the door unlocked
    St41Sub1RoomHandler, // sub 1: the same after 0xa ticks (flag 0x591, message 0x29)
    St41Sub2RoomHandler, // sub 2: the door; the player past x 0x600 -> once (flag 0x592) the message 0x2a
    St41Sub3RoomHandler, // sub 3: the door; the guards 0x8cc9a8 x 3 until flag 0x682 (flag 0x680 gates the spawn)
    St41Sub5RoomHandler, // sub 5: the door; the guards 0x8cc940 x 3 until flag 0x680
    St41Sub7RoomHandler, // sub 7: the door; on entry the lock 0x713 = !flag 0x153
    St42RoomHelper, // unlocked
    St42RoomSetup, // the room setup of ST42 (exe table 0x8d397c)
    St42StageSetup, // the stage setup of ST42 (exe table 0x8d3808)
    St42Sub10RoomHandler, // sub 10: the door; the pair 0x8ccb50; both down -> the door unlocked
    St42Sub14RoomHandler, // sub 14: the door; with flag 0x3e1 and without 0x683 the guards 0x8ccb00 x 4 until flag 0x683
    St42Sub1RoomHandler, // sub 1: the door; the guards 0x8cca48 x 2 until flag 0x680
    St42Sub4RoomHandler, // sub 4: the door; the guards 0x8ccab0 x 4 until flag 0x681
    St42Sub6RoomHandler, // sub 6: the door; on entry, without flag 0x59d, flag 0x684
    St42Sub7RoomHandler, // sub 7: the door; the pair 0x8cca88; both down -> (the step stays 1) flag 0x59d, flags 0x684 / 0x711
    St42Sub8RoomHandler, // sub 8: the door; every tick the lock 0x714 = !flag 0x153
    St43RoomCallback, // the callback the stage setup of ST43 sets (0xa72584)
    St43RoomSetup, // the room setup of ST43 (exe table 0x8d397c)
    St43SecondRoomCallback, // the callback the stage setup of ST43 sets (0xa72230)
    St43StageSetup, // the stage setup of ST43 (exe table 0x8d3808)
    St43Sub0RoomHandler, // ST43 sub 0: 0 -> 1 with the countdown 0x14; 1: it counts down, at 0 -> 2 and (once, flag 0x5a0)
    St43Sub2RoomHandler, // ST43 sub 2: 0 -> 1, without flag 0x170 the door lock 0x710 + records 0x8ccbb0 x 5 (4 statics
    St44RoomHelper, // ST44: no room list; the loads' flags cleared
    St44RoomSetup, // the room setup of ST44 (exe table 0x8d397c)
    St44StageSetup, // the stage setup of ST44 (exe table 0x8d3808)
    St44Sub2RoomHandler, // sub 2: the door; first time (no flag 0x172): flags 0x155 / 0x710, the script slot's group 0 and the
    St45BossUpdate, // pool 0x20 type 0x45 (ST45 sub 1): the area boss; its death sets flag 0x176 (content/late_game/st45_boss)
    St45BreathPuffUpdate, // pool 0x40 type 5 (ST45): the breath puff of the area boss 0x4c78e0 (content/late_game/st45_boss)
    St45MouthShotUpdate, // pool 0x40 type 0xf (ST45): the mouth shot of the area boss 0x4c78e0 and its splash (content/late_game/st45_boss)
    St45RoomSetup, // the room setup of ST45 (exe table 0x8d397c)
    St45ShockWaveUpdate, // pool 0x60 type 0x3c sub 0 (ST45): the shock wave of the area boss 0x4c78e0 (content/late_game/st45_boss)
    St45SpitBlobUpdate, // pool 0x40 type 0x1a (ST45): the spit blob of the area boss 0x4c78e0 (content/late_game/st45_boss)
    St45StageSetup, // the stage setup of ST45 (exe table 0x8d3808)
    St45Sub1RoomHandler, // ST45 sub 1: 0 -> 1, without flag 0x174 the door lock 0x710 + the script slot's group 0 (the
    St46RoomSetup, // the room setup of ST46 (exe table 0x8d397c)
    St46StageSetup, // the stage setup of ST46 (exe table 0x8d3808)
    St46Story5To9Sub0RoomHandler, // ST46 sub 0, stories 5..9: once (flag 0x24) the story + 1 (exe 0x512800) and script 0x31
    St46Story5To9Sub3RoomHandler, // ST46 sub 3, stories 5..9: the player's hit points 0; step 0 -> script 0x3c (not ported), step 1
    St47FlyerUpdate, // ST47 pool 0x60 type 0x59: script 0x73 flyer (content/yosyonke/town_side_scripts)
    St47PersonUpdate, // ST47 pool 0x20 type 0 sub 1: script 0x12's cast (content/yosyonke/town_story_cast)
    St47RoomHandler,
    St47RoomHandlerA,
    St47RoomHandlerB,
    St47RoomHandlerC,
    St47RoomHandlerD, // story 0 sub 7 / 1 sub 4 / 2..0xe sub 1: the record 0x8ccd60, once
    St47RoomHandlerE, // story 0 sub 8 / 1 sub 5 / 2..0xe sub 2: the records 0x8ccd78 x 3, once
    St47RoomHandlerF, // story 1 sub 4 / 2..0xe sub 3 / 0xf.. sub 0: the record 0x8ccd48 + the records 0x8ccdb8 x 3, once
    St47RoomHandlerG, // story 1 sub 5 / 2..0xe sub 4 / 0xf.. sub 1: the record 0x8ccd60, once
    St47RoomHandlerH,
    St47RoomHandlerI,
    St47RoomHandlerJ,
    St47RoomHelper, // ST47 (where the first ruin's device leads): the town's list, two doors unlocked
    St47RoomSetup, // the room setup of ST47 (exe table 0x8d397c)
    St47StageSetup, // the stage setup of ST47 (exe table 0x8d3808)
    St47VisitorUpdate, // ST47 pool 0x20 type 0 sub 2: script 0x73 cast, variants 0x4494a0 / 0x449530 (content/yosyonke/town_side_scripts)
    St48GunshipUpdate, // ST48 static type 6 sub 0 / type 3 sub 1: the field-encounter gunships (content/saul_kada/st48_gunship)
    St48RoomCallback, // the callback the stage setup of ST48 sets (0xa72584)
    St48RoomHandler,
    St48RoomHandlerA, // story 12 sub 3 / later sub 1
    St48RoomHelper, // ST48: the island's list by the story; sub 0: the gate cell (0x45, 0x3c), shut (0) at story 0xc
    St48RoomSetup, // the room setup of ST48 (exe table 0x8d397c)
    St48SandScript,
    St48StageSetup, // the stage setup of ST48 (exe table 0x8d3808)
    St48StoryCSub1RoomHandler, // story 12 sub 1 (the sand pit): its encounters back
    St49DropshipCableUpdate, // pool 0xa0 ST49 stage type 0x17: the dropship cable LINE_G2 to its cargo (content/forbidden_island/st49_dropship)
    St49DropshipCargoUpdate, // pool 0x20 type 0x6e (ST49): the cargo hanging under the dropship (content/forbidden_island/st49_dropship)
    St49DropshipUpdate, // ST49 type 0x6d: the dropship of script 0x18 (content/forbidden_island/st49_dropship)
    St49FlutterUpdate, // ST49 type 0x30: the Flutter of the landing on the Forbidden Island (content/forbidden_island/st49_landing)
    St49LandingDescentVoiceStep,
    St49LandingDroppedStep,
    St49LandingFarBelowStep,
    St49LandingHatchOpensStep,
    St49LandingLandedStep,
    St49LandingOpeningStep,
    St49LandingReturnFadeStep,
    St49LandingReturnHatchStep,
    St49LandingReturnLandedStep,
    St49LandingReturnVoiceStep,
    St49LandingRideStep,
    St49LandingScript,
    St49RoomCase,
    St49RoomSetup, // the room setup of ST49 (exe table 0x8d397c)
    St49StageSetup, // the stage setup of ST49 (exe table 0x8d3808)
    St49StillModelUpdate, // static ST49: still model (content/forbidden_island/st49_landing)
    St4ARoomCallback, // the callback the stage setup of ST4A sets (0xa72584)
    St4ARoomSetup, // the room setup of ST4A (exe table 0x8d397c)
    St4ASecondRoomCallback, // the callback the stage setup of ST4A sets (0xa72230)
    St4AStageSetup, // the stage setup of ST4A (exe table 0x8d3808)
    St4BRoomSetup, // the room setup of ST4B (exe table 0x8d397c)
    St4BStageSetup, // the stage setup of ST4B (exe table 0x8d3808)
    St4BSub0RoomHandler, // sub 0: script 0x59, once
    St4BSub5RoomHandler, // sub 5: flag 0x680 -> script 0x5a (step 1); then flag 0x681 -> script 0x5b (step 2)
    St4CRoomSetup, // the room setup of ST4C (exe table 0x8d397c)
    St4CStageSetup, // the stage setup of ST4C (exe table 0x8d3808)
    St4CSub3RoomHandler, // sub 3: records 0x8cd5c0 x 5, flag 0x711 until flag 0x177; the door: north of z -0x400 the
    St4CSub4RoomHandler, // sub 4: flags 0x680 / 0x681 cleared; flag 0x680 (no event) -> script 0x6b; then flag 0x681 ->
    St4DRoomHelper, // ST4D: sub 0 -> its visit flag
    St4DRoomSetup, // the room setup of ST4D (exe table 0x8d397c)
    St4DStageSetup, // the stage setup of ST4D (exe table 0x8d3808)
    St4ERoomHelper, // ST4E: sub 4 with its cell's flag -> the cell to variant 1; sub 0 -> its visit flag
    St4ERoomSetup, // the room setup of ST4E (exe table 0x8d397c)
    St4EStageSetup, // the stage setup of ST4E (exe table 0x8d3808)
    St4FRoomCallback, // the callback the stage setup of ST4F sets (0xa72584)
    St4FRoomHelper, // ST4F: sub 0 -> its visit flag
    St4FRoomSetup, // the room setup of ST4F (exe table 0x8d397c)
    St4FSecondRoomCallback, // the callback the stage setup of ST4F sets (0xa72230)
    St4FStageSetup, // the stage setup of ST4F (exe table 0x8d3808)
    St4aEventTriggerUpdate, // ST4A e0 type 0xa: message / event triggers (13 variants; content/intro/st4a_event_triggers)
    St4aRoomsLessonHandler,
    St4aRoomsSecondFightHandler,
    St4aRoomsStartRoomHandler,
    St4aRoomsUnlockHandler,
    St4aRoomsZoneFightHandler,
    St4bCastFree,
    St4bCirclingPieceUpdate, // ST4B static type 6: a circling flyer / script-driven piece (content/late_game/st4b)
    St4bEscapeBind,
    St4bEscapeEnd,
    St4bEscapeOpening,
    St4bEscapeRun,
    St4bEscapeScript,
    St4bEscapeSteps,
    St4bFlyingBossUpdate, // ST4B pool 0x20 type 0x3f sub 1: the boss (dives, strafes, drop rain, shot bursts; ground phase at 1/4 HP; flags 0x680 / 0x681; content/late_game/st4b)
    St4bRetreatBind,
    St4bRetreatEnd,
    St4bRetreatOpening,
    St4bRetreatRun,
    St4bRetreatScript,
    St4bRetreatSteps,
    St4bScript59,
    St4bScript59Step1, // the walk in (the prop turning)
    St4bScript59Step10, // the collapse: trails, billboards and smoke, then the blast, a DAT load and sub 4
    St4bScript59Step11,
    St4bScript59Step12, // the cast's actor 2 counted down at five ticks
    St4bScript59Step13,
    St4bScript59Step14,
    St4bScript59Step15,
    St4bScript59Step16,
    St4bScript59Step17,
    St4bScript59Step18,
    St4bScript59Step19,
    St4bScript59Step2, // the walk in again, the poses, then sub 1
    St4bScript59Step20, // the face keys, then the DAT load and sub 4 (the end without its own load)
    St4bScript59Step21,
    St4bScript59Step22, // the walk to the boss
    St4bScript59Step23, // the stage's debris by tick (the point (0x68, -0x6c8, 0x935))
    St4bScript59Step24,
    St4bScript59Step25,
    St4bScript59Step26, // the sparks, the cast's actor 3 fading in, the fade, actor 1 placed
    St4bScript59Step27,
    St4bScript59Step28,
    St4bScript59Step3, // the cast's actors 3..5 released at 0x50, then sub 0
    St4bScript59Step4,
    St4bScript59Step5,
    St4bScript59Step6, // the quake: the shake dies down by 0x40 every 4 ticks
    St4bScript59Step7,
    St4bScript59Step8,
    St4bScript59Step9,
    St4bScriptsEscapeBlasts,
    St4bScriptsEscapeLook,
    St4bScriptsEscapeVoice,
    St4bScriptsRetreatRoll,
    St4bScriptsRetreatVoice,
    St4bSpinningPieceUpdate, // ST4B static type 6 sub 1: a spinning flyer with a hit entry (content/late_game/st4b)
    St4bStillBind,
    St4cIntro0,
    St4cIntro2,
    St4cIntroScript,
    St4cIntroStep1, // step 0
    St4cIntroStep2, // step 1
    St4cIntroStep3, // step 2: voice 1 after the current one fades (from tick 0x28, not before tick 0x78)
    St4cIntroStep4, // step 3
    St4cIntroStep5, // step 4: fade 0x12 at tick 0x280, then the wrap-up
    St4cOutro0,
    St4cOutro2,
    St4cOutroScript,
    St4cOutroStep1, // step 0: the defeated boss for 1000 ticks
    St4cOutroStep2, // step 1: the flash (8 ticks from 0xf5), a shake at 0xfa, the rumble from 0x10e; 400 ticks
    St4dCellAWatch, // ST4D sub 4: its cell and flag
    St4dCellBWatch, // ST4D sub 7: its cell and flag
    St4dLockCleared, // ST4D subs 1..3 / 5: the lock flag cleared
    St4dSub0Locks, // ST4D sub 0: the lock flags 0 / 1 cleared
    St4dSub6Group, // ST4D sub 6 (layouts A / C): the lock flag cleared then group 10 once the object group is marked
    St4dSub8Lock, // ST4D sub 8 (and sub 6 of layout B): the lock flag cleared
    St4eGroupSpawnerUpdate, // ST4E pool e0 type 0xa: the sub-ruin's group spawner (content/subruins/subruin_spawners)
    St4eRoom4Handler,
    St4fSub11Records, // ST4F sub 11: its 2 records with the doors locked
    St4fSub12Record, // ST4F sub 12: its record
    St4fSub7Roll, // ST4F sub 7: group 7 and the stage's roll record
    St50GroupSpawnerUpdate, // ST50 pool e0 type 0xa: the sub-ruin's group spawner (content/subruins/subruin_spawners)
    St50RoomHelper, // ST50: sub 0 -> its visit flag; records 0x8cd778 x 2 (pool 0xa0 type 0xa, pool 0xe0 type 0x20)
    St50RoomSetup, // the room setup of ST50 (exe table 0x8d397c)
    St50StageSetup, // the stage setup of ST50 (exe table 0x8d3808)
    St50Sub10Record, // ST50 sub 10: its record
    St50Sub9Group, // ST50 sub 9: groups 10 / 9 with the doors locked
    St51Room1Handler,
    St51Room2Handler,
    St51RoomCallback, // the callback the stage setup of ST51 sets (0xa72584)
    St51RoomSetup, // the room setup of ST51 (exe table 0x8d397c)
    St51SecondRoomCallback, // the callback the stage setup of ST51 sets (0xa72230)
    St51StageSetup, // the stage setup of ST51 (exe table 0x8d3808)
    St52RoomRoutine, // ST52 (the stage's own room function, the room-script tick's case 0x52): sub 0, step 0 -> script
    St52RoomSetup, // the room setup of ST52 (exe table 0x8d397c)
    St52StageSetup, // the stage setup of ST52 (exe table 0x8d3808)
    St53CastStep1,
    St53CastStep10,
    St53CastStep11,
    St53CastStep12,
    St53CastStep13,
    St53CastStep14,
    St53CastStep15,
    St53CastStep16,
    St53CastStep17,
    St53CastStep18,
    St53CastStep19,
    St53CastStep2,
    St53CastStep3,
    St53CastStep4,
    St53CastStep5,
    St53CastStep6,
    St53CastStep7,
    St53CastStep8,
    St53CastStep9,
    St53RideEnd,
    St53RideOpening,
    St53RideScript,
    St53RideStep1,
    St53RideStep2,
    St53RideStep3,
    St53RoomSetup, // the room setup of ST53 (exe table 0x8d397c)
    St53SnowTrackEnd,
    St53SnowTrackOpening,
    St53SnowTrackScript,
    St53SnowTrackStep1,
    St53SnowTrackStep2,
    St53SnowTrackStep3,
    St53StageSetup, // the stage setup of ST53 (exe table 0x8d3808)
    St53StoryFRoomHandler, // ST53 sub 0 at story 0xf (table 0x8cd800): script 0x60 once
    St53StoryFSub1RoomHandler, // ST53 sub 1 at story 0xf: step 0: without flag 0x5a1 that flag + script 0x61, else without flag
    St54BindRoutine,
    St54ClearScreenSaveMenu,
    St54ClearScreenShowTime,
    St54RoomCase, // ST54 (the stage case): step 0: its first flag -> script 0x6f (the step kept), else on; step 1: its
    St54RoomSetup, // the room setup of ST54 (exe table 0x8d397c)
    St54StageSetup, // the stage setup of ST54 (exe table 0x8d3808)
    St54SwingRoutine,
    St55RoomRoutine, // ST55 (the stage's own room function, the room-script tick's case 0x55): the licence exam
    St55RoomSetup, // the room setup of ST55 (exe table 0x8d397c)
    St55StageSetup, // the stage setup of ST55 (exe table 0x8d3808)
    St56RoomCase, // ST56 (the stage case): its record once
    St56RoomSetup, // the room setup of ST56 (exe table 0x8d397c)
    St56StageSetup, // the stage setup of ST56 (exe table 0x8d3808)
    St57RoomCase, // ST57 (the stage case): script 0x6e once (no door test)
    St57RoomSetup, // the room setup of ST57 (exe table 0x8d397c)
    St57StageSetup, // the stage setup of ST57 (exe table 0x8d3808)
    St58RoomSetup, // the room setup of ST58 (exe table 0x8d397c)
    St58StageSetup, // the stage setup of ST58 (exe table 0x8d3808)
    St58Sub1RoomHandler, // sub 1: the door; step 0 -> 1: shut (its lock flag and record) until its flag; the flag clears
    St59RoomCallback, // the callback the stage setup of ST59 sets (0xa72584)
    St59RoomHelper, // ST59 / ST5A / ST5B: no room list; the courses' flags
    St59RoomSetup, // the room setup of ST59 (exe table 0x8d397c)
    St59StageSetup, // the stage setup of ST59 (exe table 0x8d3808)
    St5ARoomSetup, // the room setup of ST5A (exe table 0x8d397c)
    St5AStageSetup, // the stage setup of ST5A (exe table 0x8d3808)
    St5BRoomSetup, // the room setup of ST5B (exe table 0x8d397c)
    St5BStageSetup, // the stage setup of ST5B (exe table 0x8d3808)
    St5CRoomSetup, // the room setup of ST5C (exe table 0x8d397c)
    St5CStageSetup, // the stage setup of ST5C (exe table 0x8d3808)
    St5cBossUpdate, // ST5C pool 0x20 type 0x83 sub 0: the boss (record 0x8cd8c0; flag 0x680 on death; content/late_game/st5c)
    StaffRollScript,
    StageCallbackScript, // script 8: the stage callback StageState::script8Callback
    StageDustEffectUpdate, // ST5C pool 0xa0 type 0x24: variant 2 dust puffs of script 0x6d (content/late_game/st5c)
    StageEffectFromVariant2Update, // ST4B pool 0xa0 type 0x20: script 0x59 effects (0x43bff0 variants + 2; content/late_game/late_cast)
    StageEffectUpdate, // pool 0xa0 stage type 0x1f (ST0A / ST0B / ST0C / ST47): cosmetic effect, 9 variants by +6 (0x2c billboards, 0xf0 rings, OT G4 star, OT G3 glows, full-screen tile; content/yosyonke/st0a_fx)
    StageMusicCall, // the stage setup's music call (its two arguments; game/stage_music)
    StandingCastUpdate, // pool 0x20 type 0x5a (ST52): a standing cast member with face tracks (content/saul_kada/st52_cast)
    StaticFreedState, // the static freed (shared by the static classes)
    StatueMinionUpdate, // ST3C pool 0x20 type 0x78: the gold-statue fight's minions (rise from the floor, chase / charge / leap at the player, bounce off walls; dies into a blast + a Servbot variant 3; content/saul_kada/st3c_minion)
    StatuePartUpdate, // static ST3C type 0x50: the hit part riding 0x4fa800's bone 0 (hits, HP, flag 0x68, blast; content/saul_kada/st3c_statue)
    SteeredPropUpdate, // static ST2F / ST58 type 0x39: a prop steered toward target yaw / pitch (+0xbc / +0xbe), colour +0xc..+0xe (content/saul_kada/steered_prop)
    StillFigureCastUpdate, // ST4B pool 0x20 type 0x27 sub 0: script 0x5b still cast (content/late_game/st4b_scripts)
    StillModelUpdate, // static ST09: still model by +6 (content/yosyonke/st09_props)
    StillSpriteEffectUpdate, // ST4C pool a0 type 0x23: still sprite (draw cmd 0x2c) (content/late_game/st4c_story)
    StompShotUpdate, // ST5C pool 0x40 type 0x32: the stomp's shots (content/late_game/st5c_fx)
    StormRibbonUpdate, // pool 0xa0 stage type 0xb (ST10 / ST11): the storm ribbon, vertical colour bands (game/storm_ribbon)
    StoryFlagClearCall, // a story flag cleared (a helper column's entry, not a helper)
    StoryFlagTestCall, // a story flag tested (a helper column's entry, not a helper)
    StripScrollUpdate, // pool e0 type 0x1a (ST4C): a scrolling 16 x 64 texture strip (VRAM image moves; game/vram_moves)
    SubruinDropSpawnerUpdate, // pool e0 type 0x12: the drop spawner (content/subruins/subruin_spawners)
    SubruinGroup0, // encounter group 0 in the zone slot, the door test
    SubruinGroup1, // encounter group 1 in the zone slot, the door test
    SubruinGroup10, // encounter group 10 in the zone slot, the door test
    SubruinGroup2, // encounter group 2 in the zone slot, the door test
    SubruinGroup3, // encounter group 3 in the zone slot, the door test
    SubruinGroup4, // encounter group 4 in the zone slot, the door test
    SubruinGroup5, // encounter group 5 in the zone slot, the door test
    SubruinGroup6, // encounter group 6 in the zone slot, the door test
    SubruinGroup7, // encounter group 7 in the zone slot, the door test
    SubruinGroup8, // encounter group 8 in the zone slot, the door test
    SubruinGroup9, // encounter group 9 in the zone slot, the door test
    SubruinLineTriggerUpdate, // pool e0 type 0x13: the encounter line trigger (content/subruins/subruin_spawners)
    SuctionUpdate, // pool 0xa0 global type 4: the vacuum arm's suction (game/suction_effect)
    SwingingModelUpdate, // static ST54: swinging model (content/late_game/st54_props)
    SwirlingMotesUpdate, // ST4C static type 0x65: swirling motes (draw cmds 0xf0 / 0x2c) (content/late_game/st4c_story)
    TakeOffCastUpdate, // ST46 pool 0x20 type 2 sub 0: script 0x31 cast (record 0x8a5cec), texture-track cues + type 0x47 child (content/nino/ch2_script_cast)
    TakeoffArcUpdate, // pool 0x20 type 0x72 sub 1 (ST54): the take-off arc (content/late_game/st54_takeoff)
    TalkPropUpdate, // static ST3C type 0x50 variant 1 (story 13): a solid prop that talks when used (message 0x6f; content/saul_kada/st3c_talk_prop)
    TalkerUpdate, // pool 0x20 type 0x1a (ST4B): a talking cast member of script 0x59 (sequence-timed animations, mouth flaps; content/saul_kada/st4b_cast)
    TalkingNpcSt17Update, // standing NPC you talk to (pool 0x20, ST17 type 1: the twin of 0x449620 with the hints from the stage bank; content/calbania/talking_npc)
    TalkingNpcUpdate, // standing NPC you talk to (pool 0x20, ST24 type 1; content/calbania/talking_npc)
    TankShellUpdate, // pool 0x40 stage type 0x11 (ST15 / ST16, ST46): the tank shell (game/tank_shell)
    TeleporterEffectUpdate, // ST40 pool a0 stage type 0x1e: teleporter pad panel (sub 0) / teleport ring (sub 2) / rising sparks (sub 3, LINE_G2) (dc::otQuadG4 / otLineG2; content/late_game/st40_fx)
    TeleporterTransition, // TransitionKind::Teleporter
    TextLayoutChoiceOption, // fb 0f i: an option's colour
    TextLayoutColour, // fb 0a c: the text colour
    TextLayoutColourBack, // fb 0b: the previous colour
    TextLayoutInsertItemName, // fb 3d
    TextLayoutInsertKeyName, // fb 22
    TextLayoutInsertNumber, // fb 21
    TextLayoutInsertPlace, // fb 44
    TextLayoutInsertString, // fb 20
    TextLayoutInsertTime, // fb 23
    TextLayoutLoadRecordTime, // fb 48 k: a best-time record's time into the window
    TextLayoutLoadSaveSlot, // fb 45 slot: a save slot's summary into the window
    TextLayoutNewLine, // fc
    TextLayoutPenShift, // fb 2e dx: the pen moved
    TextLayoutPenShiftY, // fb 2f dy: the pen moved down (one pixel less without ruby)
    TextLayoutReturn, // fb 1f
    TextLayoutRubyGlyph, // fe: the glyph and its ruby
    TextLayoutSkipChoiceFlags, // fb 43 n flags: past the n flags
    TextLayoutSkipEight,
    TextLayoutSkipFive,
    TextLayoutSkipFour,
    TextLayoutSkipFourteen,
    TextLayoutSkipJumps, // fb 11: past the jump table
    TextLayoutSkipNine,
    TextLayoutSkipOne,
    TextLayoutSkipOptions, // fb 10 / 39: past the option table
    TextLayoutSkipSeven,
    TextLayoutSkipSix,
    TextLayoutSkipThirteen,
    TextLayoutSkipThree,
    TextLayoutSkipTwo,
    TextLayoutSkipWindows, // fb 13 / 38: past the id list
    TextLayoutWriteZenny, // fb 3b: the zenny's decimal written over the text after the code, then laid out
    TextLogicAddCounter, // fb 37 k v: a counter or the zenny
    TextLogicAskInWindow, // fb 38 id n targets: a question in another window, then by its answer
    TextLogicBalance, // fb 3e n: the good / bad balance
    TextLogicChoice, // fb 39: a choice, cross cancels
    TextLogicChoiceNoCancel, // fb 10: a choice cross does not cancel
    TextLogicClearFlag, // fb 27 flag
    TextLogicClearRecordName, // fb 4b k
    TextLogicCloseAfter, // fb 04 n: the window closed after n ticks
    TextLogicFastForward, // fb 29 b: circle / start may speed the text up (0: not)
    TextLogicFrameClose, // fb 0c: the frame closed, the text goes on once it is
    TextLogicFrameOpen, // fb 05 style: the frame opened, the text goes on once it is open
    TextLogicGeometry, // fb 06 x y columns lines: the window's place and size
    TextLogicHitPoints, // fb 34 n: the player's hit points
    TextLogicHold, // fb 1e: nothing; the read pointer stays on it
    TextLogicInsertItemName, // fb 3d: the window's item's name
    TextLogicInsertKeyName, // fb 22 k: a key's name
    TextLogicInsertNumber, // fb 21 k: a number
    TextLogicInsertPlace, // fb 44: a place's name
    TextLogicInsertString, // fb 20 id: a system string
    TextLogicInsertTime, // fb 23 k: a time
    TextLogicJump, // fb 0e id: to another message
    TextLogicJumpByBalanceTier, // fb 3f t0 t1 t2
    TextLogicJumpByChoice, // fb 11 n targets: to the message of the answer
    TextLogicJumpByCounter, // fb 42 k v ge lt: by a counter or the zenny
    TextLogicJumpByDifficulty, // fb 2a t0..t4
    TextLogicJumpByDockCounter, // fb 47 i v ge lt
    TextLogicJumpByFlag, // fb 28 flag set clear: by an event flag
    TextLogicJumpByStory, // fb 16 s lt eq gt: by the story's progress
    TextLogicJumpByToolKitTier, // fb 41 t0 t1 t2
    TextLogicLayer, // fb 30 n: the OT layer
    TextLogicLayoutBits, // fb 25 k: the system layout bits
    TextLogicLoadRecordTime, // fb 48 k: a best time into the window
    TextLogicNameRecord, // fb 4a k: a best time's name entered
    TextLogicNewLine, // fc
    TextLogicOpenWindow, // fb 12 win id: another window opened
    TextLogicOpenWindowByChoice, // fb 13 win n ids: another window opened on the answer's message
    TextLogicOpenWindowOnChoice, // fb 14 answer win id: another window opened for one answer
    TextLogicPause, // fb 08 n: a pause of n ticks
    TextLogicPlayerImmune, // fb 2d b: the player's immunity held or not
    TextLogicPlayerShown, // fb 2c b: the player drawn or not
    TextLogicReturn, // fb 1f: back from an inserted text
    TextLogicRubyGlyph, // fe: a glyph with ruby, typed as one
    TextLogicScrollPage, // fb 0d n: the text scrolled up, then the next page
    TextLogicSetFlag, // fb 26 / 36 flag: an event flag set
    TextLogicSetFlagByChoice, // fb 43 n flags: the answer's event flag
    TextLogicSkipOne, // fb 03: nothing
    TextLogicSkipThree, // fb 0a / 0f / 2e / 2f / 45 / 49: nothing
    TextLogicSkipTwo, // fb 0b / 17 / 31 / 3b / 46: nothing (the layout pass's codes)
    TextLogicSmallLayout, // fb 3a
    TextLogicSound, // fb 19 id: a sound
    TextLogicSpeaker, // fb 33 b: the speaker byte
    TextLogicTalkCamera, // fb 15: the talk camera's pose
    TextLogicToolKitBalance, // fb 40 n
    TextLogicTypeDelay, // fb 09 n: the typing delay
    TextLogicTypingSoundOff, // fb 1d
    TextLogicTypingSoundOn, // fb 1c
    TextLogicVoice, // fb 1a id: a voice clip, the text goes on once it plays
    TextLogicWaitButton, // fb 18: wait for circle / start (no "more" arrow)
    TextLogicWaitButtonArrow, // fb 24: wait for circle / start with the "more" arrow
    TextLogicWaitEvent, // fb 2b: wait for the event runner
    TextLogicWaitOrHelp, // fb 32 id: circle / start goes on; triangle opens a help window first
    TextLogicWaitPage, // fd hi lo: a wait, then the next page
    TextLogicWaitVoice, // fb 1b: wait until the voice clip is over
    TextLogicWaitWindow, // fb 07 win closed: wait for another window
    TextLogicWarp, // fb 3c: a warp to a stage / room
    TextLogicWeaponEnergy, // fb 35 n: the special weapon's energy
    ThrowableUpdate, // static global type 9: a falling / liftable / throwable object (game/throwable)
    ThrownPropUpdate, // ST28 static type 0x40 sub 1: the object thrown by cast 0x4d1040 variant 2 (+0xbc carrier; emitters + dust, content/saul_kada/st28_cast_props)
    TimedCastUpdate, // pool 0x20 type 0x75 (ST52): a cast member timed by the script's sequence tick (face tracks, turns and walks; content/saul_kada/st52_cast)
    TitleModelUpdate, // the title screen's model (pool 0x20 type 0, table 0x88c3fc; game/title_model)
    TonguesUpdate, // pool 0xa0: a pair of flame / spark tongues thrown outward (OT F3 via dc::otTriG3; content/saul_kada/flame_tongues)
    TorpedoSwimmerUpdate, // pool 0x20 type 0x77 sub 0 (ST37 / ST38 / ST50): the swimming reaverbot, torpedoes (content/nino/ruin_swimmers)
    TorpedoUpdate, // pool 0x40 stage type 0x1e: the swimming reaverbot's torpedo, draw command 0xf0 (content/nino/ruin_swimmers)
    TownBirdUpdate, // pool 0x20 type 0xa sub 0 (ST20 and others): the town birds -- wander, flee, lifted / thrown (content/calbania/calbania_cast)
    TownBystanderStep1,
    TownBystanderStep2,
    TownBystanderStep3,
    TownBystanderStep4,
    TownCastJoesLabMaria,
    TownCastJoesLabRoll,
    TownCastJunkShopClerk,
    TownCastJunkShopRoll,
    TownCastJunkShopThird,
    TownCastRunningRoll,
    TownCastSt47Person,
    TownChapterClerkTalksStep,
    TownChapterDoorKnockStep,
    TownChapterLabFarewellStep,
    TownChapterLabMessageStep,
    TownChapterLabReplyStep,
    TownChapterLookAroundLabStep,
    TownChapterRollExplainsStep,
    TownChapterRollGreetsStep,
    TownChapterRollLeavesStep,
    TownChapterShopTalkStep,
    TownChapterTurnToDoorStep,
    TownChapterWalkIntoLabStep,
    TownChapterWalkToCounterStep,
    TownChapterWatchRollLeaveStep,
    TownExamDoorsStep1,
    TownExamDoorsWalkOutStep,
    TownFirstVisitScript,
    TownKickUpdate, // pool 0x10 type 0: the town's kick (game/shots/buster)
    TownNpcUpdate, // Maybe_TownWalkerUpdate (town NPC: route walker / stationary, talk)
    TownReturnScript,
    TownRoomCallback, // the towns' room callback
    TownSideBystanderSetUp,
    TownSideBystanderSteps,
    TownSideBystanderWrapUp,
    TownSideExamDoorsSetUp,
    TownSideExamDoorsSteps,
    TownSideExamDoorsWrapUp,
    TownSideSt06VoiceSetUp,
    TownSideSt06VoiceWrapUp,
    TownSideVisitorSetUp,
    TownSideVisitorWrapUp,
    TownSt06VoiceStep1,
    TownSt06VoiceStep2,
    TownSt06VoiceStep3,
    TownStandingCastUpdate, // pool 0x20 ST20 type 0xa sub 1: a standing cast member (content/calbania/calbania_cast)
    TownTalkPropUpdate, // ST05 / ST06 statics: talk props (use -> message [0x8cd99c + variant]) and the hanging bar (variant 2) (content/yosyonke/st05_st06_props)
    TownVisitorStep1,
    TownVisitorStep2,
    TownVisitorStep3,
    TownVisitorStep4,
    TownVisitorStep5,
    TownVisitorStep6,
    TownVisitorStep7,
    TownVisitorStep8,
    TrainAttackRoomHandler,
    TrainLostScript,
    TransitionHandshakeEnd,
    TransitionSequence, // the camera and step sequence of a transition
    TravelIconUpdate, // ST01 static type 0x33: the travel map's icons (game/travel)
    TravelPickFirstDrop, // on map 1 the first drop to ST49 sub 2
    TravelPickNextOnMap2, // the next story +1 on map 2
    TravelPickNextOnMapB, // the next story +1 on map 0xb
    TravelPickNextOnMapE, // the next story +1 on map 0xe
    TravelPickNextOrSt39, // the next story on map 7 / ST39 on map 0x11
    TravelPickNoFlight, // no flight
    TravelPickSt46, // on map 5 to ST46
    TravelScript, // the travel map's script (game/travel)
    TreasureChestUpdate, // Maybe_TreasureChestUpdate (treasure chest: event mode 4 -> item get, content flag)
    TriggerAllFlags, // the three flags params[0..2] set
    TriggerAtOnce, // fires at once
    TriggerCountdown, // params[0] counted down to 0
    TriggerCrossedXDown, // the player's x crossed the line params[0] downward
    TriggerCrossedXUp, // the player's x crossed the line params[0] upward
    TriggerCrossedZUp, // the player's z crossed the line params[2] upward
    TriggerFlagsByMask, // the flags set where the mask has the bit, clear elsewhere
    TriggerInPlanarReach, // the player within range * 0x100 on x / z (the first tick arms)
    TriggerInReach, // the player within range * 0x100 in 3D
    TriggerReachedZ, // z <= the line and the previous z below it (as the exe compares)
    TriggerSpecialWeapon, // the saved special weapon == params[0]
    TronBossUpdate, // ST25 sub 6: Tron Bonne's tank, the boss after script 0x1b (content/calbania/tron_boss)
    TronPartnerUpdate, // pool 0x20 type 0x48 sub 1 (ST27 sub 8): Tron, picks / leaps to / carries / throws Reaverbots into the lava (content/saul_kada/st27_tron)
    TumblingBlockUpdate, // ST12 static type 0x13: the tumbling block (flag-started slides + rocking, dust puffs; content/manda/st12_14)
    TurningModelUpdate, // static global type 0xa: a turning model put by the player (game/placed_models)
    TurretBombUpdate, // ST0E pool 0x40 stage type 0x24: the turret lobbed bomb (content/yosyonke/st0e_turret)
    TurretBulletHoming,
    TurretBulletUpdate, // pool 0x40 stage type 4: the turret's homing bullet (game/turret_bullet)
    TurretShotUpdate, // ST0E pool 0x40 stage type 0x2e: the turret cannon shot (content/yosyonke/st0e_turret)
    TwoFormReaverbotUpdate, // Saul Kada two-form enemy (pool 0x20 type 0x29: rises, beat/stand by variant, turns into the wandering form B on hit, content/saul_kada/two_form_reaverbot)
    UvAnimationListSetup, // the room's UV-animation list set up (a helper column's entry, not a helper)
    VacuumArmShotUpdate, // pool 0x10 type 7: weapon 8, the vacuum arm (game/shots/vacuum_arm)
    ValveUpdate, // static ST27 type 0x3b (story 13): the fire ruin's lava valve (regenerating HP through a hit fraction, shakes on the script's cue, sinks when destroyed; content/saul_kada/st27_lava_valve)
    VehicleFireballUpdate, // ST0E pool 0x60 stage type 0x5c: the vehicle homing fireball (content/yosyonke/st0e_boss_vehicle)
    VisitorScript,
    VolleyShotUpdate, // ST4B pool 0x40 type 0x30: the boss's volley shot (capsule hit, 0x2c/0xf0 draw commands; content/late_game/st4b_fx)
    WalkOffEnd,
    WalkOffStart,
    WalkOffTransition, // TransitionKind::WalkOff
    WalkerBombUpdate, // pool 0x40 ST31 stage type 0x16: the walkers' rolling bomb (content/nino/glyde_box_walkers)
    WalkerShotUpdate, // pool 0x40 stage type 0x10: the Birdbot soldier's shot (game/walker_shot)
    WallBlastUpdate, // ST1E static type 0x36 sub 1: the fire bursting from the wall (content/intro/flutter_fire)
    WallBreakBurstUpdate, // ST13 pool e0 type 9: wall-break burst (debris + explosion statics, segment variant 2, 0x2c rings; content/manda/st13_wall_break)
    WallCrumbleUpdate, // pool e0 type 9: a breakable wall crumbling (the breakable segments' reaction; game/wall_crumble)
    WallDebrisUpdate, // static global type 0xc: the debris of a broken wall (OT FT4; game/wall_debris)
    WaterBubbleControllerUpdate, // pool 0xe0 type 0x20 (ST36..ST38 / ST50): the underwater player's bubbles (content/nino/water)
    WaterSplashUpdate, // pool 0x60 type 0x48: a water splash / bubble burst (FUN_0057b990 / 0x57ba60 / 0x426d10; content/nino/water)
    WaveTileUpdate, // pool e0 type 1 (ST20): a VRAM tile scrolled by a sine of the frame (row 0x88beb8; game/vram_moves)
    WeaponModelUpdate, // pool 0x20 type 0x65 (ST04/ST19): the spinning weapon model (content/yosyonke/weapon_model)
    WindowGiantUpdate, // pool 0x20 type 0x45 sub 2 (ST26 sub 5, story 13): the giant seen through the window -- walks, strikes the wall (cell 0x44/0x3c,0x40 variant 1, debris, flags 0x62/0x63), hit list on its bones (content/saul_kada/window_giant)
};

// the number of RoutineIds (None included)
inline constexpr uint16_t kRoutineIds = 2515;

inline const char* routineName(RoutineId id) {
    static constexpr const char* kNames[] = {
        "None",
        "AbandonedMineBackInHallTurnStep",
        "AbandonedMineBackInHallWalkStep",
        "AbandonedMineDeviceApproachStep",
        "AbandonedMineDeviceMessageStep",
        "AbandonedMineDeviceScript",
        "AbandonedMineDeviceTalkStep",
        "AbandonedMineEndScript",
        "AbandonedMineEntranceScript",
        "AbandonedMineExitScript",
        "AbandonedMineExitTalkStep",
        "AbandonedMineGuardianGoneStep",
        "AbandonedMineGuardianWakeStep",
        "AbandonedMineHallLookStep",
        "AbandonedMineHallScript",
        "AbandonedMineHallTalkStep",
        "AbandonedMineHallWalkInStep",
        "AbandonedMineHallWalkOnStep",
        "AbandonedMineLiftArrivalStep",
        "AbandonedMineLiftRideStep",
        "AbandonedMineLiftScript",
        "AbandonedMineRelicUpdate",
        "AbandonedMineRollTalkStep",
        "AbandonedMineRollUpdate",
        "AbandonedMineTurnToLiftStep",
        "AbandonedMineTurnToRollStep",
        "AbandonedMineWalkOntoLiftStep",
        "ActorGlowUpdate",
        "AimedShotUpdate",
        "AlarmFlashUpdate",
        "AnimatedSpriteEffectUpdate",
        "AppearingBlockUpdate",
        "AppoDahBombUpdate",
        "AppoDahFightUpdate",
        "AppoDahGatePuppetUpdate",
        "AppoDahGuideUpdate",
        "AppoDahPuppetUpdate",
        "AppoDahScript1dAppo",
        "AppoDahScript1dDah",
        "AppoDahScript1fPuppet",
        "ArrivalCabinDelayedMessageStep",
        "ArrivalCabinScript",
        "ArrivalCabinStep1",
        "ArrivalCabinStep3",
        "ArrivalDescentScript",
        "AttractBolt",
        "AttractEmitter",
        "AttractFlash",
        "AttractFlashingEmitter",
        "AttractLightningUpdate",
        "AttractRing",
        "AttractRingFlashInit",
        "AttractRingFlashUpdate",
        "AttractScript3CoverStep",
        "AttractScript3FadeInStep",
        "AttractScript3FadingQuadStep",
        "AttractScript3FinalRingsStep",
        "AttractScript3FlippingQuadStep",
        "AttractScript3LastRingsStep",
        "AttractScript3SceneryStep",
        "AttractScript3ScrollStep",
        "AttractScript3SecondCastStep",
        "AttractScript3SecondFileStep",
        "AttractScript3Step10",
        "AttractScript3Step11",
        "AttractScript3Step8",
        "AttractScript3Step9",
        "AttractScript3SweepSceneryStep",
        "AttractScript3SwitchCastStep",
        "AttractScript3ThirdFileStep",
        "AttractScript3ToRoom1Step",
        "AttractScript3ToRoom2Step",
        "AttractScript4ChapterStep",
        "AttractScript4ChipsStep",
        "AttractScript4DipVoiceStep",
        "AttractScript4EngineStep",
        "AttractScript4FadeOutStep",
        "AttractScript4FirstFileStep",
        "AttractScript4FourthFileStep",
        "AttractScript4LastBankStep",
        "AttractScript4LastFileStep",
        "AttractScript4RingWarpStep",
        "AttractScript4SceneryKindsStep",
        "AttractScript4SecondFileStep",
        "AttractScript4ShowActorStep",
        "AttractScript4SlowSceneryStep",
        "AttractScript4SparklesStep",
        "AttractScript4Step17",
        "AttractScript4Step18",
        "AttractScript4Step19",
        "AttractScript4Step22",
        "AttractScript4SweepShowStep",
        "AttractScript4TexturesStep",
        "AttractScript4ThirdFileStep",
        "AttractScript4WarpAfterStep",
        "AttractStartsScript3",
        "AttractVramTileUpdate",
        "BallChargeUpdate",
        "BallExplosionUpdate",
        "BallVisualsUpdate",
        "BaseCameraScript",
        "BaseEntranceScript",
        "BaseExplosionScript",
        "BaseExplosionScriptStepBlowUp",
        "BeamChargeUpdate",
        "BeamGlowUpdate",
        "BeamImpactUpdate",
        "BirdbotDownUpdate",
        "BirdbotSoldierUpdate",
        "BladeArmShotUpdate",
        "BladeBarrierUpdate",
        "BladeTrailUpdate",
        "BladeTrapUpdate",
        "BlastUpdate",
        "BlizzardLayerUpdate",
        "BlobBossUpdate",
        "BobberUpdate",
        "BobbingPropUpdate",
        "BombCritterUpdate",
        "BombRibbonUpdate",
        "BombServbotUpdate",
        "BombTurretUpdate",
        "BombUpdate",
        "BonPartnerUpdate",
        "BonRetreatCastUpdate",
        "BonScriptCastUpdate",
        "BoneRiderUpdate",
        "BoneTrailUpdate",
        "BossAfterImageUpdate",
        "BossBeamUpdate",
        "BossBladeUpdate",
        "BossDefeatCastUpdate",
        "BossEscapeCastUpdate",
        "BossFallingRockUpdate",
        "BossIntroCastUpdate",
        "BossRetreatCastUpdate",
        "BossShellUpdate",
        "BossShotUpdate",
        "BossSparkUpdate",
        "BossTurretUpdate",
        "BossVehicleUpdate",
        "BoxCarrierUpdate",
        "BoxWalkerUpdate",
        "BrakingTrainUpdate",
        "BreakableUpdate",
        "BrotherFreedScript",
        "BrotherHeldScript",
        "BubbleTrailUpdate",
        "BurningDataSceneBurnt",
        "BurningDataSceneHop",
        "BurningDataUpdate",
        "BurrowerUpdate",
        "BurrowingShooterUpdate",
        "BurstUpdate",
        "BusterCannonShotUpdate",
        "BusterShotUpdate",
        "BystanderScript",
        "CabinAnimatedUpdate",
        "CabinLaterVisitsHandler",
        "CabinModelUpdate",
        "CabinNpcUpdate",
        "CalbaniaArrivalScript",
        "CalbaniaDepartureScript",
        "CalbaniaDepartureScriptStepBoard",
        "CalbaniaDepartureScriptStepFlyOff",
        "CalbaniaDepartureScriptStepPlace",
        "CalbaniaDepartureScriptStepRise",
        "CalbaniaDepartureScriptStepTurn",
        "CalbaniaRoomsPokteRoomCallback",
        "CalbaniaRoomsSquareRoomCallback",
        "CalbaniaRoomsSquareSecondCallback",
        "CalbaniaStoryRoomsDockFirst",
        "CalbaniaStoryRoomsDockLater",
        "CalbaniaStoryRoomsSquareAfterBase",
        "CalbaniaStoryRoomsSquareDefence",
        "CalbaniaStoryRoomsSquareDeparture",
        "CalbaniaStoryRoomsTownLate",
        "CalbaniaStoryRoomsTownMiddle",
        "CalbaniaStoryRoomsTownStory8",
        "CalbaniaStoryRoomsTownStory9",
        "CalbaniaTownScriptsFirstVisitStepHello",
        "CalbaniaTownScriptsFirstVisitStepTalk",
        "CalbaniaTownScriptsFirstVisitStepWait",
        "CalbaniaTownScriptsStepFirstLine",
        "CalbaniaTownScriptsStepFourthLine",
        "CalbaniaTownScriptsStepLastLine",
        "CalbaniaTownScriptsStepSecondLine",
        "CalbaniaTownScriptsStepThirdLine",
        "CameraEaseCosine",
        "CameraEaseIn",
        "CameraEaseLinear",
        "CameraEaseOut",
        "CannonBossUpdate",
        "CannonShellUpdate",
        "CarriedNpcUpdate",
        "CarrierBoatUpdate",
        "CarrierCrashUpdate",
        "CarrierCrateUpdate",
        "CarrierPodUpdate",
        "CastBarrelStill",
        "CastBarrelStillB",
        "CastBarrelTalks",
        "CatchableCritterUpdate",
        "CellBlockerUpdate",
        "CellSceneBirdbotUpdate",
        "Ch2CannonDoneScript",
        "Ch2CityGateScript",
        "Ch2CraneDoneScript",
        "Ch2GunshipDownScript",
        "Ch2LandingScript",
        "Ch2LookAroundScript",
        "Ch2ScriptsCannonLeave",
        "Ch2ScriptsCannonTalk",
        "Ch2ScriptsCannonTurn",
        "Ch2ScriptsCraneCast",
        "Ch2ScriptsCraneEnd",
        "Ch2ScriptsCraneFace",
        "Ch2ScriptsFightBurst",
        "Ch2ScriptsFightCast",
        "Ch2ScriptsFightLoad",
        "Ch2ScriptsFightVoice",
        "Ch2ScriptsGateBank",
        "Ch2ScriptsGateFade",
        "Ch2ScriptsGateSkippable",
        "Ch2ScriptsGateWarp",
        "Ch2ScriptsGunshipFade",
        "Ch2ScriptsLandingLeave",
        "Ch2ScriptsLandingStand",
        "Ch2ScriptsLandingWalk",
        "Ch2ScriptsShipsQuiet",
        "Ch2ScriptsShipsTalk",
        "Ch2ScriptsTakeOffEngines",
        "Ch2ScriptsTakeOffFly",
        "Ch2ScriptsTakeOffPuffs",
        "Ch2ScriptsTakeOffStart",
        "Ch2ShipFightScript",
        "Ch2ShipsDoneScript",
        "Ch2TakeOffScript",
        "Ch4ArrivalEnd",
        "Ch4ArrivalScript",
        "Ch4ArrivalSetUp",
        "Ch4ArrivalStep1",
        "Ch4ArrivalStep2",
        "Ch4ArrivalStep3",
        "Ch4ArrivalStep4",
        "Ch4ArrivalStep5",
        "Ch4ArrivalStep6",
        "Ch4RidingEnd",
        "Ch4RidingScript",
        "Ch4RidingSetUp",
        "Ch4RidingStep1",
        "Ch4RidingStep2",
        "Ch4StepRunner",
        "Ch4StoryExamEntrance",
        "Ch4TrainBrake",
        "ChargeBallUpdate",
        "ChargeGlowUpdate",
        "ChargeMarkerUpdate",
        "ChargedShotUpdate",
        "ChargerUpdate",
        "ClearScreenClosing",
        "ClearScreenOpening",
        "ClearScreenScript",
        "CloudRingUpdate",
        "ClutAnimationUpdate",
        "ContentCallbackScript",
        "CrashBarrelUpdate",
        "CrashCastCraftLeaving",
        "CrashCastCraftParked",
        "CrashCastFaceFirst",
        "CrashCastFaceLater",
        "CrashCastJetCompanion",
        "CrashCastJetFighter",
        "CrashCastJetLeader",
        "CrashCastPropDrifting",
        "CrashCastPropFalling",
        "CrashCastPropHuge",
        "CrashCastPropHugeB",
        "CrashCastPropLarge",
        "CrashCastPropSmall",
        "CrashCastPropTrailing",
        "CrashCastRollLate",
        "CrashCastRollLater",
        "CrashCastRollSub3",
        "CrashCastRollTalk",
        "CrashCastSecondBodyBlink",
        "CrashCastSecondBodyIdle",
        "CrashCastSecondBodySpeech",
        "CrashCastSecondBodyStill",
        "CrashCastSecondBodyTalk",
        "CrashCastSpeakerFirst",
        "CrashCastSpeakerLater",
        "CrashCastTalker",
        "CrashCastWatcher",
        "CrashCloudPuffs",
        "CrashCloudPuffsSmall",
        "CrashCloudQuad",
        "CrashCloudScroll",
        "CrashCloudSprites",
        "CrashCloudYawFollow",
        "CrashCloudYawFollowOne",
        "CrashCraftArriving",
        "CrashCraftDiving",
        "CrashCraftLanding",
        "CrashCraftUpdate",
        "CrashFaceUpdate",
        "CrashFlashUpdate",
        "CrashJetUpdate",
        "CrashLandingAlarmOffStep",
        "CrashLandingAlarmStep",
        "CrashLandingBackdropOffStep",
        "CrashLandingBurnStep",
        "CrashLandingClearStep",
        "CrashLandingDebrisStep",
        "CrashLandingEscapeStep",
        "CrashLandingFlutterStreaksStep",
        "CrashLandingScript",
        "CrashLandingSecondFileStep",
        "CrashLandingShakeStep",
        "CrashLandingStep2",
        "CrashLandingStep3",
        "CrashLandingStreaksStep",
        "CrashLandingWaitStep",
        "CrashLandingWalkStep",
        "CrashPieceDives",
        "CrashPieceFalls",
        "CrashPieceUpdate",
        "CrashPropUpdate",
        "CrashRollUpdate",
        "CrashSecondBodyUpdate",
        "CrashSpeakerUpdate",
        "CrashTalkerUpdate",
        "CrashWatcherUpdate",
        "CrateServbotUpdate",
        "CritterUpdate",
        "CrumblingPlatformUpdate",
        "CrusherShotUpdate",
        "CrystalShotUpdate",
        "CutsceneGunshipUpdate",
        "DarkTrailUpdate",
        "DebugBeamUpdate",
        "DecorationUpdate",
        "DefendedVehicleUpdate",
        "DepthsCastUpdate",
        "DevRoomNinoDevRoomCallback",
        "DevRoomNinoEnd",
        "DevRoomNinoMenuStep",
        "DevRoomNinoSetUp",
        "DevRoomNinoStep0",
        "DevRoomRollDevRoomCallback",
        "DevRoomRollEnd",
        "DevRoomRollMenuStep",
        "DevRoomRollSetUp",
        "DevRoomRollStep0",
        "DoorActorStart",
        "DoorActorTransition",
        "DoorActorUpdate",
        "DoorLeafDoubleUpdate",
        "DoorLeafHingedUpdate",
        "DoorLeafSlidingUpdate",
        "DoubleDoorEnd",
        "DoubleDoorStart",
        "DoubleDoorTransition",
        "DrainBurnMarkerUpdate",
        "DrillArmShotUpdate",
        "DrillConeUpdate",
        "DropInWalkerUpdate",
        "DroppedItemUpdate",
        "DroppedPropUpdate",
        "DustPuffUpdate",
        "EffectMoverUpdate",
        "ElectricSparkUpdate",
        "ElephantBossUpdate",
        "ElephantCrystalFloat",
        "ElephantCrystalInit",
        "ElephantCrystalInitPlaced",
        "ElephantCutsceneUpdate",
        "ElephantPedestalUse",
        "ElephantScript28",
        "ElephantScript29",
        "ElephantScriptArenaStep",
        "ElephantScriptCrystalReactsStep",
        "ElephantScriptElephantWakesStep",
        "ElephantScriptFloorGivesWayStep",
        "ElephantScriptStep5",
        "ElysiumBeamUpdate",
        "ElysiumBoltUpdate",
        "ElysiumBossUpdate",
        "ElysiumChargeUpdate",
        "ElysiumDefeatCastUpdate",
        "ElysiumFlashUpdate",
        "ElysiumFogUpdate",
        "ElysiumHomingShotUpdate",
        "ElysiumIntroCastUpdate",
        "ElysiumMoteUpdate",
        "ElysiumOrbitShotUpdate",
        "ElysiumSparklesUpdate",
        "ElysiumSparksUpdate",
        "EmptyRoomHandler",
        "EmptyRoutine",
        "EncounterSlotsSetup",
        "EncounterSwitchUpdate",
        "EndDefeatStep1",
        "EndDefeatStep2",
        "EndDefeatStep3",
        "EndDefeatStep4",
        "EndDefeatStep5",
        "EndDefeatStep6",
        "EndDefeatStep7",
        "EndDefeatStep8",
        "EndDefeatStep9",
        "EndReturnStep1",
        "EndReturnStep2",
        "EndReturnStep3",
        "EndReturnStep4",
        "EndReturnStep5",
        "EndReturnStep6",
        "EndScript3f",
        "EndScript6d",
        "EndScript6f",
        "EndScript75",
        "EndScript75Step1",
        "EndScript75Step2",
        "EndScript75Step3",
        "EndStaffRollStep",
        "EndTakeoffStep1",
        "EndTakeoffStep10",
        "EndTakeoffStep11",
        "EndTakeoffStep12",
        "EndTakeoffStep13",
        "EndTakeoffStep14",
        "EndTakeoffStep15",
        "EndTakeoffStep2",
        "EndTakeoffStep3",
        "EndTakeoffStep4",
        "EndTakeoffStep5",
        "EndTakeoffStep6",
        "EndTakeoffStep7",
        "EndTakeoffStep8",
        "EndTakeoffStep9",
        "EndingEffectUpdate",
        "EndingGlowCastUpdate",
        "EndingGroupCastUpdate",
        "EndingMouthTalkerUpdate",
        "EndingTalkerAUpdate",
        "EndingTalkerBUpdate",
        "EndingWalkerUpdate",
        "EnemyGeneratorUpdate",
        "EnemyShotUpdate",
        "EnergyOrbUpdate",
        "EntranceCastUpdate",
        "EscapeTimerUpdate",
        "EventItemGet",
        "EventPose",
        "EventStandThenItem",
        "EventTalk",
        "ExamAmbushFlyerUpdate",
        "ExamAmbushUpdate",
        "ExamDiggerUpdate",
        "ExamDoorsScript",
        "ExamDroneUpdate",
        "ExamGunnerShotUpdate",
        "ExamHatchUpdate",
        "ExamHopperUpdate",
        "ExamMineUpdate",
        "ExamRoom1Handler",
        "ExamRoom2Handler",
        "ExamRoom3Handler",
        "ExamRoom4Handler",
        "ExamRoom5Handler",
        "ExamRoom6Handler",
        "ExamShieldGunnerUpdate",
        "ExamShieldUpdate",
        "ExamTumblerUpdate",
        "ExitDoorActorSinkStep",
        "ExitDoorPushStep",
        "ExitDoubleWalkThroughStep",
        "ExitHingedWalkThroughStep",
        "ExitLeafFollowsHandStep",
        "ExitLeafSwingsOnStep",
        "ExitLeavesCloseStep",
        "ExitLeavesHoldWalkOutStep",
        "ExitLeavesOpenStep",
        "ExitWalkOffStep",
        "ExplosionUpdate",
        "ExtinguisherMistUpdate",
        "ExtinguisherShotUpdate",
        "EyeTrailUpdate",
        "FadingCastUpdate",
        "FadingFigureUpdate",
        "FallingPieceUpdate",
        "FallingRockUpdate",
        "FallingShotUpdate",
        "FieldHoverbotUpdate",
        "FieldReaverbotUpdate",
        "FieldRollUpdate",
        "FinaleCastAUpdate",
        "FinaleCastBUpdate",
        "FireBombUpdate",
        "FireEffectUpdate",
        "FireMissionRoom0",
        "FireMissionRoom1",
        "FireMissionRoom2",
        "FireRuinBeatenScript",
        "FireRuinDepthsScript",
        "FireRuinDepthsScriptsBeatenSignal",
        "FireRuinDepthsScriptsBeatenSkip",
        "FireRuinDepthsScriptsBeatenWalk",
        "FireRuinDepthsScriptsEdgeCountdown",
        "FireRuinDepthsScriptsEdgeStagger",
        "FireRuinDepthsScriptsEdgeStepBack",
        "FireRuinDepthsScriptsEdgeTurn",
        "FireRuinDepthsScriptsEdgeWalkBack",
        "FireRuinDepthsScriptsLeavePose",
        "FireRuinDepthsScriptsLeaveWait",
        "FireRuinDepthsScriptsReturnFaces",
        "FireRuinDepthsScriptsReturnStop",
        "FireRuinDepthsScriptsReturnWalkBack",
        "FireRuinEdgeScript",
        "FireRuinEntranceScript",
        "FireRuinEntranceScriptsDepthsBlink",
        "FireRuinEntranceScriptsDepthsPause",
        "FireRuinEntranceScriptsDepthsWalkIn",
        "FireRuinEntranceScriptsEntranceEmbers",
        "FireRuinEntranceScriptsEntranceFaces",
        "FireRuinEntranceScriptsEntranceMouth",
        "FireRuinEntranceScriptsEntranceStepUp",
        "FireRuinEntranceScriptsEntranceTalk",
        "FireRuinEntranceScriptsEntranceWalkIn",
        "FireRuinEntranceScriptsGateLook",
        "FireRuinEntranceScriptsGateOpen",
        "FireRuinEntranceScriptsGatePlaceA",
        "FireRuinEntranceScriptsGatePlaceB",
        "FireRuinEntranceScriptsGateShake",
        "FireRuinEntranceScriptsGateTalk",
        "FireRuinEntranceScriptsRubbleCast",
        "FireRuinEntranceScriptsRubbleFaces",
        "FireRuinEntranceScriptsRubbleFall",
        "FireRuinEntranceScriptsRubblePlace",
        "FireRuinEntranceScriptsRubbleWalkIn",
        "FireRuinEscapeEveScript",
        "FireRuinGateScript",
        "FireRuinHallScript",
        "FireRuinLeaveScript",
        "FireRuinReturnScript",
        "FireRuinRubbleScript",
        "FireRuinVisitScript",
        "FireRuinVisitScriptsEveTalk",
        "FireRuinVisitScriptsEveWait",
        "FireRuinVisitScriptsVisitDrive",
        "FireRuinVisitScriptsVisitPadLook",
        "FireRuinVisitScriptsVisitPlace",
        "FireUpdate",
        "FlagBarrierUpdate",
        "FlameUpdate",
        "FlameWallUpdate",
        "FlashBurstUpdate",
        "FleeingReaverbotUpdate",
        "FlipBookTilesUpdate",
        "FloatingDeviceUpdate",
        "FloatingMineUpdate",
        "FloorSwitchUpdate",
        "FlungServbotUpdate",
        "FlutterCabinCabinNpc",
        "FlutterRollUpdate",
        "FlyPointUpdate",
        "FlyerShellUpdate",
        "FlyerUpdate",
        "FootstepDustUpdate",
        "FrozenTransition",
        "GateDebrisUpdate",
        "GateFigureUpdate",
        "GateLineUpdate",
        "GaugeCastUpdate",
        "GaugeReaverbotUpdate",
        "GlowBlobUpdate",
        "GlowTrailUpdate",
        "GlydeBaseBomberUpdate",
        "GlydeBaseGunUpdate",
        "GlydeBaseGunshipUpdate",
        "GlydeBaseKeyUpdate",
        "GlydeBaseRoomsBoxRoom",
        "GlydeBaseRoomsCell",
        "GlydeBaseRoomsEntrance",
        "GlydeBaseRoomsGate",
        "GlydeBaseRoomsSub0",
        "GlydeBaseRoomsSub1",
        "GlydeBaseSceneBirdbotUpdate",
        "GlydeBaseScriptsStepAlarm",
        "GlydeBaseScriptsStepAlarmNear",
        "GlydeBaseScriptsStepCastLeaves",
        "GlydeBaseScriptsStepCastTalks",
        "GlydeBaseScriptsStepCastWalksIn",
        "GlydeBaseScriptsStepFound",
        "GlydeBaseScriptsStepGuardsCue",
        "GlydeBaseScriptsStepHeard",
        "GlydeBaseScriptsStepInside",
        "GlydeBaseScriptsStepPlayerAnswers",
        "GlydeBaseScriptsStepRunOff",
        "GlydeBaseScriptsStepTalk",
        "GlydeBaseScriptsStepThanks",
        "GlydeBaseScriptsStepTowerFalls",
        "GlydeBaseScriptsStepWalkIn",
        "GlydeBaseScriptsStepWalkOut",
        "GlydeBaseTankUpdate",
        "GlydeBaseTurretUpdate",
        "GlydeFlyInShipUpdate",
        "GlydeHoverShipUpdate",
        "GlydeMissileUpdate",
        "GlydeShipArrivalUpdate",
        "GoldPieceUpdate",
        "GoldShockwaveUpdate",
        "GoldStatueBossUpdate",
        "GreyFigureUpdate",
        "GroundCrawlerShotUpdate",
        "GroupMemberUpdate",
        "GuardShotUpdate",
        "GuardianBeamUpdate",
        "GuardianBeatenUpdate",
        "GuardianDissolvesUpdate",
        "GuardianDropsInUpdate",
        "GuardianOrbUpdate",
        "GuardianReaverbotUpdate",
        "GuardianUpdate",
        "GuardianWakesUpdate",
        "GunnerBeamUpdate",
        "GunnerBombUpdate",
        "GunnerBossUpdate",
        "GunnerHelperUpdate",
        "HangarDropshipUpdate",
        "HeadNpcUpdate",
        "HeldItemUpdate",
        "HerdedCreatureUpdate",
        "HiddenReaverbotUpdate",
        "HingedDoorEnd",
        "HingedDoorStart",
        "HingedDoorTransition",
        "HitMuteUpdate",
        "HitSparkUpdate",
        "HomingMarkerUpdate",
        "HomingMissileShotUpdate",
        "HomingOrbUpdate",
        "HomingShotUpdate",
        "HoppingDroneUpdate",
        "HoppingReaverbotUpdate",
        "HoverDrifterUpdate",
        "HoverGunnerUpdate",
        "HoverMachineUpdate",
        "HoverReaverbotUpdate",
        "HoverTargetUpdate",
        "HunterSeekerShotUpdate",
        "HurtSparksUpdate",
        "HyperShellShotUpdate",
        "IcePlayerMarkerUpdate",
        "IceRuinCritterScene",
        "IceRuinScriptsCueStep",
        "IceRuinScriptsTutorialSceneStep",
        "IceRuinScriptsTutorialStep",
        "IceRuinScriptsWalkStep",
        "IceRuinShrinkScript",
        "IceRuinTutorialScene",
        "IceRuinTutorialScript",
        "IslandBlownScript",
        "IslandBlownScriptStepBlasts",
        "IslandBlownScriptStepWarp",
        "IslandDecorationUpdate",
        "IslandRoomsSt3eRoomCallback",
        "IslandVisitRecordsRoomHandler",
        "IslandVisitStory2Sub2RoomHandler",
        "JoesLabCastUpdate",
        "JoesLabRoomHandler",
        "JoesLabScript",
        "JunkShopCastUpdate",
        "JunkShopPropUpdate",
        "JunkShopRoomHandler",
        "JunkShopScript",
        "KeyItemUpdate",
        "KeyScriptFigureUpdate",
        "KickBallUpdate",
        "LadderTransition",
        "LaserWallSparkUpdate",
        "LateRoomsSt40SoundHelper",
        "LavaBossUpdate",
        "LavaFlowUpdate",
        "LavaPlumeUpdate",
        "LavaValveMissionUpdate",
        "LavaVentsUpdate",
        "LeapingCritterUpdate",
        "LensFlareUpdate",
        "LiftBoxUpdate",
        "LightColumnUpdate",
        "LightGlowUpdate",
        "LightRingUpdate",
        "LightningBoltUpdate",
        "LineOfSightUpdate",
        "LobbedBlobUpdate",
        "LobbedShotUpdate",
        "LungingSwimmerUpdate",
        "LurePieceUpdate",
        "MachineGunImpactUpdate",
        "MachineGunMuzzleUpdate",
        "MachineGunShotUpdate",
        "MachinePartPropUpdate",
        "MachinePartUpdate",
        "MandaBladeUpdate",
        "MandaBurrowerUpdate",
        "MandaCast2GuardianUpdate",
        "MandaCastGuardianUpdate",
        "MandaCloudUpdate",
        "MandaDebrisPieceUpdate",
        "MandaEntranceHallRoomHandler",
        "MandaFallingBladeUpdate",
        "MandaGateUpdate",
        "MandaGuardianHallRoomHandler",
        "MandaGuardianUpdate",
        "MandaHopperUpdate",
        "MandaOpenedChestUpdate",
        "MandaOutroGuardianUpdate",
        "MandaPlatformUpdate",
        "MandaRuinBurningWreckUpdate",
        "MandaRuinGroundedFigureUpdate",
        "MandaRuinKeyItemUpdate",
        "MandaRuinScript1a",
        "MandaRuinScript9",
        "MandaRuinScriptA",
        "MandaRuinScriptB",
        "MandaRuinScriptC",
        "MandaRuinScriptE",
        "MandaRuinScriptF",
        "MandaScript1aLastHallEndStep",
        "MandaScript1aLastHallVoiceStep",
        "MandaScript1aLastHallWalkStep",
        "MandaScript1aStep2",
        "MandaScript1aStep3",
        "MandaScript9FirstHallEndStep",
        "MandaScript9FirstHallLookStep",
        "MandaScript9FirstHallPoseStep",
        "MandaScript9FirstHallSoundStep",
        "MandaScript9FirstHallWalkInStep",
        "MandaScriptAAfterGuardianSoundStep",
        "MandaScriptAAfterGuardianWalkStep",
        "MandaScriptABlockStep",
        "MandaScriptAFreeAndLoadStep",
        "MandaScriptBSecondGuardianBladeStep",
        "MandaScriptBSecondGuardianFadeStep",
        "MandaScriptBSecondGuardianPlaceStep",
        "MandaScriptBSecondGuardianTurnStep",
        "MandaScriptCAfterSecondSoundStep",
        "MandaScriptCAfterSecondWalkStep",
        "MandaScriptCFreeAndLoadStep",
        "MandaScriptEGuardianJumpStep",
        "MandaScriptEGuardianWreckStep",
        "MandaScriptFBackToTerminalStep",
        "MandaScriptFPlatformRisesStep",
        "MandaScriptFPlatformSparksStep",
        "MandaScriptFPlatformUpStep",
        "MandaScriptFTerminalWalkStep",
        "MandaSecondDoorRoomHandler",
        "MandaShotUpdate",
        "MessageTriggerUpdate",
        "MinerUpdate",
        "MinionSpawnerUpdate",
        "MissileShipUpdate",
        "MissileTrailUpdate",
        "MissionGunshipUpdate",
        "MissionResultFadeOutStep",
        "MissionResultScript",
        "MissionResultSetUpState",
        "MissionResultStep2",
        "MissionResultStep4",
        "MissionResultStepsState",
        "MissionResultSuccessWarpStep",
        "MissionResultWrapUpState",
        "MissionTankUpdate",
        "MissionTargetUpdate",
        "NinoAttackEndScript",
        "NinoReturnRoomsAttack",
        "NinoReturnRoomsMachine",
        "NinoReturnRoomsReturn",
        "NinoReturnRoomsShore",
        "NinoReturnScript",
        "NinoReturnScriptsStepArrive",
        "NinoReturnScriptsStepBack",
        "NinoReturnScriptsStepClear",
        "NinoReturnScriptsStepDive",
        "NinoReturnScriptsStepFaceSea",
        "NinoReturnScriptsStepFlyPoints",
        "NinoReturnScriptsStepHide",
        "NinoReturnScriptsStepLand",
        "NinoReturnScriptsStepLast",
        "NinoReturnScriptsStepLook",
        "NinoReturnScriptsStepRipples",
        "NinoReturnScriptsStepRise",
        "NinoReturnScriptsStepRun",
        "NinoReturnScriptsStepSink",
        "NinoReturnScriptsStepSurface",
        "NinoReturnScriptsStepToShore",
        "NinoReturnScriptsStepWalkAway",
        "NinoReturnScriptsStepWatch",
        "NinoRoomsSt18RoomCallback",
        "NoRoomListClearHelper",
        "NoRoomListHelper",
        "OpeningAfterChoiceStep",
        "OpeningBackStep",
        "OpeningCastCrewScript68",
        "OpeningCastCrewWalker",
        "OpeningCastCrewWatcher",
        "OpeningCastTalkerArriving",
        "OpeningCastTalkerTalking",
        "OpeningChoiceStep",
        "OpeningCrewStep",
        "OpeningCrewUpdate",
        "OpeningEffectUpdate",
        "OpeningFlutterUpdate",
        "OpeningLongTalkStep",
        "OpeningReturnStep",
        "OpeningScript",
        "OpeningSeaStep",
        "OpeningSetUpState",
        "OpeningShakeStep",
        "OpeningSkyStep",
        "OpeningTalkerUpdate",
        "OpeningWalkOffStep",
        "OpeningWrapUpState",
        "OwnerGlowUpdate",
        "ParkedDropshipUpdate",
        "ParticleBurstUpdate",
        "PassageEnd",
        "PassageStart",
        "PassageTransition",
        "PatrolDroneUpdate",
        "PatrolGuardUpdate",
        "PedestalItemUpdate",
        "PickupEaterUpdate",
        "PickupUpdate",
        "PiecesDropperUpdate",
        "PirateBaseArrivalScript",
        "PirateBaseCastUpdate",
        "PirateBaseGateScript",
        "PirateBaseScriptsArrivalLoad",
        "PirateBaseScriptsArrivalPlace",
        "PirateBaseScriptsArrivalReact",
        "PirateBaseScriptsArrivalReset",
        "PirateBaseScriptsArrivalWalk",
        "PirateBaseScriptsGateBreak",
        "PirateBaseScriptsGateGuards",
        "PirateBaseScriptsGateLook",
        "PirateBaseScriptsGateSweep",
        "PirateBaseScriptsGateTurn",
        "PirateBaseScriptsVictoryFade",
        "PirateBaseVictoryScript",
        "PirateCarrierUpdate",
        "PirateCastUpdate",
        "PirateGunTurretUpdate",
        "PirateServbotUpdate",
        "PirateShotUpdate",
        "PirateTankUpdate",
        "PitControllerUpdate",
        "PitCreatureMoverUpdate",
        "PitCreatureUpdate",
        "PitFlameUpdate",
        "PitStart",
        "PitTransition",
        "PitWait",
        "PlacedModelUpdate",
        "PlatformBossUpdate",
        "PlayerBarHangInput",
        "PlayerBarHangMotion",
        "PlayerBladeArm",
        "PlayerBusterArm",
        "PlayerCannonArm",
        "PlayerDeadState",
        "PlayerEventPoseMotion",
        "PlayerExtinguisherArm",
        "PlayerGenericSpecialArm",
        "PlayerHeldInput",
        "PlayerHeldMotion",
        "PlayerIdleInput",
        "PlayerIdleMotion",
        "PlayerInitState",
        "PlayerItemGetMotion",
        "PlayerJumpInput",
        "PlayerJumpMotion",
        "PlayerKnockBackInput",
        "PlayerKnockBackMotion",
        "PlayerKnockDownMotion",
        "PlayerLedgeHangInput",
        "PlayerLedgeHangMotion",
        "PlayerLiftMotion",
        "PlayerLookInput",
        "PlayerLookMotion",
        "PlayerPlayingState",
        "PlayerQuickTurnMotion",
        "PlayerRunInput",
        "PlayerRunMotion",
        "PlayerRunningJumpMotion",
        "PlayerScriptedUpdate",
        "PlayerSideHopMotion",
        "PlayerSideStepInput",
        "PlayerSideStepMotion",
        "PlayerSkateFallMotion",
        "PlayerSkateInput",
        "PlayerSkateMotion",
        "PlayerTalkMotion",
        "PlayerTryLedgeGrab",
        "PlayerTurnInPlaceInput",
        "PlayerTurnInPlaceMotion",
        "PlayerWalkInput",
        "PlayerWalkMotion",
        "PodCarrierUpdate",
        "PokteAfterTronRoomHandler",
        "PokteRecordRoomHandlerA",
        "PokteRecordRoomHandlerB",
        "PokteRecordRoomHandlerC",
        "PokteTankFightRoomHandler",
        "PokteVillagersRoomHandler",
        "PosedFigureUpdate",
        "PressurePlateUpdate",
        "PropDropperUpdate",
        "PropHolderUpdate",
        "PuffEmitterUpdate",
        "PushBlockUpdate",
        "PushedOutTransition",
        "QuicksandUpdate",
        "QuizCastUpdate",
        "QuizShowScript",
        "RaceActorUpdate",
        "RaceBonusUpdate",
        "RayBurstUpdate",
        "ReaverbotPackUpdate",
        "ReaverbotTurretUpdate",
        "ReaverbotWalkerUpdate",
        "ReflectorShotUpdate",
        "RepairMachineUpdate",
        "RepairRollUpdate",
        "RescueScript",
        "RespawnerUpdate",
        "ResultSceneDataUpdate",
        "RiderFollowUpdate",
        "RiderServbotUpdate",
        "RisingSmokeColumnUpdate",
        "RockBurstUpdate",
        "RollRunsOffScript",
        "RollRunsOffUpdate",
        "RollSpawnerST08Update",
        "RollSpawnerST09Update",
        "RollSpawnerST0DUpdate",
        "RollToMachineScript",
        "RollUpdate",
        "RollerUpdate",
        "RollingBoulderUpdate",
        "RoomDoorScript",
        "RoomFlagClutAnimationsUpdate",
        "RoomUvAnimationUpdate",
        "RuinBlocksUpdate",
        "RuinEntranceRoomHandler",
        "RuinFlashUpdate",
        "RuinFlyerUpdate",
        "RuinGuardianUpdate",
        "RuinSentryUpdate",
        "RuinVariantEffectUpdate",
        "SandBurrowerUpdate",
        "SandFlowUpdate",
        "SandTrapUpdate",
        "SaulKadaRoomsLavaGlowRoutine",
        "SaulKadaRoomsSt48RearmHelper",
        "SceneCritterUpdate",
        "ScreenRippleUpdate",
        "ScreenShockRingUpdate",
        "Script1dAppoDahUpdate",
        "Script1fAppoDahUpdate",
        "Script20AppoDahUpdate",
        "Script2aArrivalCastUpdate",
        "Script32CastUpdate",
        "Script37CastUpdate",
        "Script37FlashUpdate",
        "Script39BirdbotUpdate",
        "Script39BlinkerUpdate",
        "Script39DescenderUpdate",
        "Script39RiserUpdate",
        "Script3bBirdbotUpdate",
        "Script3cCastUpdate",
        "Script59CuedTalkerAUpdate",
        "Script59CuedTalkerBUpdate",
        "Script59FaceRowCastUpdate",
        "Script59FlyerUpdate",
        "Script59IdleOrStepperUpdate",
        "Script59LateStepperUpdate",
        "Script59LateTalkerUpdate",
        "Script59SceneTalkerUpdate",
        "Script59SceneWalkerUpdate",
        "Script59WalkInCastUpdate",
        "Script60ExtraAUpdate",
        "Script60ExtraBUpdate",
        "Script60FacedTalkerUpdate",
        "Script60FadingFigureUpdate",
        "Script60LeadTalkerUpdate",
        "Script60MaskedTalkerUpdate",
        "Script60WalkerUpdate",
        "Script61FacedTalkerUpdate",
        "Script61LeadTalkerUpdate",
        "Script61MaskedTalkerUpdate",
        "Script61StandingFigureUpdate",
        "Script62TalkerAUpdate",
        "Script62TalkerBUpdate",
        "Script68CastUpdate",
        "Script69CastUpdate",
        "Script6fActionTalkerUpdate",
        "Script6fCirclerUpdate",
        "Script6fHoldingTalkerUpdate",
        "Script6fMouthTalkerUpdate",
        "Script6fPacerUpdate",
        "Script6fRowTalkerUpdate",
        "Script6fSparkTalkerUpdate",
        "ScriptAnimatedUpdate",
        "ScriptCastStepsState",
        "ScriptFallingPieceUpdate",
        "ScriptSequenceState",
        "ScriptStatueUpdate",
        "ScriptStepsState",
        "ScriptWalkerUpdate",
        "ScriptedMessageUpdate",
        "ScriptedWalkerUpdate",
        "ScrollGlowUpdate",
        "ServbotScriptCastUpdate",
        "ServbotUpdate",
        "ShedRollUpdate",
        "ShellCasingUpdate",
        "ShiningLaserBeamUpdate",
        "ShiningLaserShotUpdate",
        "ShipFightGunshipUpdate",
        "ShockRingUpdate",
        "ShockWaveUpdate",
        "ShockwaveUpdate",
        "ShopBuyMenu",
        "ShopGoodbye",
        "ShopListMenu",
        "ShopMainMenu",
        "ShopMenuCallback",
        "ShopMenus",
        "ShopPageMenu",
        "ShopSellMenu",
        "ShopSetUp",
        "ShopUpgradesMenu",
        "ShotTrailUpdate",
        "ShrinkBlockUpdate",
        "ShyCritterUpdate",
        "SignUpdate",
        "SittingPuppetUpdate",
        "SkidSparksUpdate",
        "SkyArcDraw",
        "SkyArcStart",
        "SkyArcUpdate",
        "SkyShipUpdate",
        "SlideBlockUpdate",
        "SlidingDoorUpdate",
        "SlidingPairEnd",
        "SlidingPairStart",
        "SlidingPairTransition",
        "SlowMarkerUpdate",
        "SmokeColumnUpdate",
        "SmokePuffUpdate",
        "SnowParticleDraw",
        "SnowReaverbotUpdate",
        "SnowUpdate",
        "SolidRingUpdate",
        "SparkBurstUpdate",
        "SparkEmitterUpdate",
        "SparkRingsUpdate",
        "SparkSprayUpdate",
        "SparkleUpdate",
        "SparklerUpdate",
        "SpawnerSt12Update",
        "SpawnerSt13Update",
        "SpawnerSt14Update",
        "SpawnerSt15Update",
        "SpawnerSt16Update",
        "SpawnerSt17Update",
        "SpawnerSt18Update",
        "SpawnerSt1fUpdate",
        "SpawnerSt21Update",
        "SpawnerSt22Update",
        "SpawnerSt26Update",
        "SpawnerSt27Update",
        "SpawnerSt28Update",
        "SpawnerSt2bUpdate",
        "SpawnerSt34Update",
        "SpawnerSt36Update",
        "SpawnerSt37Update",
        "SpawnerSt38Update",
        "SpawnerSt3bUpdate",
        "SpawnerSt45Update",
        "SpawnerSt55Update",
        "SpinBladeUpdate",
        "SpinPropUpdate",
        "SpreadBusterShotUpdate",
        "SquareDefenceScriptsStepAnswer",
        "SquareDefenceScriptsStepArrive",
        "SquareDefenceScriptsStepCross",
        "SquareDefenceScriptsStepEnd",
        "SquareDefenceScriptsStepFly",
        "SquareDefenceScriptsStepGather",
        "SquareDefenceScriptsStepHold",
        "SquareDefenceScriptsStepLand",
        "SquareDefenceScriptsStepLast",
        "SquareDefenceScriptsStepListen",
        "SquareDefenceScriptsStepPause",
        "SquareDefenceScriptsStepReply",
        "SquareDefenceScriptsStepRunUp",
        "SquareDefenceScriptsStepTalk",
        "SquareDefenceScriptsStepThrow",
        "SquareDefenceScriptsStepWalk",
        "SquareDefenceScriptsStepWatch",
        "SquareGunshipUpdate",
        "SquareOpeningScript",
        "SquareSecondPhaseScript",
        "SquareThirdPhaseScript",
        "SquareWallLiftScript",
        "St00FigureUpdate",
        "St00RoomSetup",
        "St00StageSetup",
        "St01RoomSetup",
        "St01StageSetup",
        "St02AttractScript",
        "St02BarrelUpdate",
        "St02BlinkerUpdate",
        "St02CastBarrelFirst",
        "St02CastBarrelSecond",
        "St02CastBlinkerBlinks",
        "St02CastDrifts",
        "St02CastFaceLines",
        "St02CastPlaced",
        "St02CastPropDrifts",
        "St02CastRollFirst",
        "St02CastRollSecond",
        "St02CastScreenShows",
        "St02CastSpeakerLines",
        "St02CastStanderStands",
        "St02CastTalkerLines",
        "St02CastWatcherLooks",
        "St02CastWatcherOnce",
        "St02CastWatcherWalks",
        "St02CastWatcherWalksLater",
        "St02DriftingStaticUpdate",
        "St02FaceUpdate",
        "St02PlacedStaticUpdate",
        "St02PropUpdate",
        "St02RollUpdate",
        "St02RoomSetup",
        "St02SceneryUpdate",
        "St02ScreenUpdate",
        "St02Script3SetUpState",
        "St02Script3WrapUpState",
        "St02SpeakerUpdate",
        "St02StageSetup",
        "St02StanderUpdate",
        "St02StillModelUpdate",
        "St02TalkerUpdate",
        "St02WatcherUpdate",
        "St03AttractScript",
        "St03BarrelUpdate",
        "St03BlinkerUpdate",
        "St03CastBarrelPlaced",
        "St03CastBarrelTalksLate",
        "St03CastBarrelTurns",
        "St03CastBlinkerMoves",
        "St03CastBodyHops",
        "St03CastBodyRides",
        "St03CastBodyRidesLater",
        "St03CastBodyWalks",
        "St03CastCarrierTalks",
        "St03CastCarrierWalksIn",
        "St03CastCraftFlies",
        "St03CastCraftReturns",
        "St03CastCraftSinks",
        "St03CastFaceLines",
        "St03CastFlutterBobs",
        "St03CastFlutterLifts",
        "St03CastPartBlinks",
        "St03CastPlacedMovedA",
        "St03CastPlacedMovedB",
        "St03CastPlacedStill",
        "St03CastPlacedTurning",
        "St03CastRollLast",
        "St03CastRollLater",
        "St03CastRollTalks",
        "St03CastScreenStill",
        "St03CastSpeakerLines",
        "St03CastStanderMoves",
        "St03CastTalkerLines",
        "St03CastWalkerWaits",
        "St03CastWalkerWalks",
        "St03CastWatcherCarries",
        "St03CastWatcherComes",
        "St03CastWatcherHoldsA",
        "St03CastWatcherHoldsB",
        "St03CastWatcherSparks",
        "St03CastWatcherStill",
        "St03CraftBodyUpdate",
        "St03CraftUpdate",
        "St03FaceUpdate",
        "St03FlutterUpdate",
        "St03LightsUpdate",
        "St03PartBlinkerUpdate",
        "St03PieceCarrierUpdate",
        "St03PlacedModelUpdate",
        "St03RollUpdate",
        "St03RoomSetup",
        "St03SceneryUpdate",
        "St03ScreenUpdate",
        "St03Script4SetUpState",
        "St03Script4StepsState",
        "St03Script4WrapUpState",
        "St03SpeakerUpdate",
        "St03StageSetup",
        "St03StanderUpdate",
        "St03StillModelUpdate",
        "St03TalkerUpdate",
        "St03TiltedModelUpdate",
        "St03WalkerUpdate",
        "St03WatcherUpdate",
        "St04RoomHandler",
        "St04RoomHandlerA",
        "St04RoomHelper",
        "St04RoomHelperA",
        "St04RoomSetup",
        "St04SecondRoomCallback",
        "St04StageSetup",
        "St04Story0Sub0RoomHandler",
        "St04Story0Sub0RoomHandlerA",
        "St04Story2OnSub1RoomHandler",
        "St04Sub1RoomHandler",
        "St05EntryListEntryMenuList",
        "St05EntryListEntryMenuText",
        "St05EntryListEntryScriptMenu",
        "St05EntryListEntryScriptSetUp",
        "St05EntryListEntryScriptWrapUp",
        "St05EntryListScript",
        "St05RoomHandler",
        "St05RoomHandlerA",
        "St05RoomHandlerB",
        "St05RoomHandlerC",
        "St05RoomHelper",
        "St05RoomSetup",
        "St05StageSetup",
        "St06RoomHandler",
        "St06RoomHandlerA",
        "St06RoomHandlerB",
        "St06RoomHandlerC",
        "St06RoomSetup",
        "St06StageSetup",
        "St06VoiceScript",
        "St07RoomHelper",
        "St07RoomSetup",
        "St07StageSetup",
        "St07Sub1RoomHandler",
        "St08PropUpdate",
        "St08RoomHelper",
        "St08RoomSetup",
        "St08SecondRoomCallback",
        "St08StageSetup",
        "St08Story1Sub0RoomHandler",
        "St08Story1Sub1RoomHandler",
        "St09BystanderUpdate",
        "St09PropUpdate",
        "St09RoomHandler",
        "St09RoomHandlerA",
        "St09RoomHandlerB",
        "St09RoomHandlerC",
        "St09RoomHelper",
        "St09RoomSetup",
        "St09StageSetup",
        "St09StillModelBind",
        "St09Story0Sub0RoomHandler",
        "St09Story1Sub0RoomHandler",
        "St0ARoomHandler",
        "St0ARoomHandlerA",
        "St0ARoomHandlerB",
        "St0ARoomHandlerC",
        "St0ARoomHandlerD",
        "St0ARoomHandlerE",
        "St0ARoomHandlerF",
        "St0ARoomHandlerG",
        "St0ARoomHandlerH",
        "St0ARoomHandlerI",
        "St0ARoomHelper",
        "St0ARoomSetup",
        "St0AStageSetup",
        "St0AStory0Sub3RoomHandler",
        "St0AStory1Sub3RoomHandler",
        "St0BRoomHandler",
        "St0BRoomHandlerA",
        "St0BRoomHandlerB",
        "St0BRoomHandlerC",
        "St0BRoomHandlerD",
        "St0BRoomHelper",
        "St0BRoomSetup",
        "St0BStageSetup",
        "St0BStory0To1Sub0RoomHandler",
        "St0BStory0To1Sub1RoomHandler",
        "St0CRoomHandler",
        "St0CRoomHandlerA",
        "St0CRoomHandlerB",
        "St0CRoomHelper",
        "St0CRoomSetup",
        "St0CStageSetup",
        "St0CStory0To1Sub0RoomHandler",
        "St0CStory0To1Sub1RoomHandler",
        "St0CStory0To1Sub2RoomHandler",
        "St0DRoomCallback",
        "St0DRoomHelper",
        "St0DRoomSetup",
        "St0DStageSetup",
        "St0DStory0Sub0RoomHandler",
        "St0DStoryFOnSub0RoomHandler",
        "St0DWalkerRoomHandler",
        "St0DWalkersRoomHandler",
        "St0ERoomHelper",
        "St0ERoomSetup",
        "St0EStageSetup",
        "St0FRoomCallback",
        "St0FRoomSetup",
        "St0FSecondRoomCallback",
        "St0FStageSetup",
        "St0FStory0Sub10RoomHandler",
        "St0FStory0Sub11RoomHandler",
        "St0FStory0Sub12RoomHandler",
        "St0FStory0Sub1RoomHandler",
        "St0FStory0Sub8RoomHandler",
        "St0FStory1Sub0RoomHandler",
        "St0FStory1Sub11RoomHandler",
        "St10FirstDecorationsRoomHandler",
        "St10FirstLandingRoomHandler",
        "St10LaterDecorationsRoomHandler",
        "St10LaterLandingRoomHandler",
        "St10RollerUpdate",
        "St10RoomCallback",
        "St10RoomHelper",
        "St10RoomSetup",
        "St10SecondRoomCallback",
        "St10StageSetup",
        "St11FighterUpdate",
        "St11FirstVisitRoomHandler",
        "St11RoomHelper",
        "St11RoomSetup",
        "St11Script2a",
        "St11Script2aFlashesStep",
        "St11Script2aLooksStep",
        "St11Script2aWalkInStep",
        "St11StageSetup",
        "St12RoomCallback",
        "St12RoomHelper",
        "St12RoomSetup",
        "St12SecondRoomCallback",
        "St12StageSetup",
        "St12Sub0RoomHandler",
        "St12Sub4RoomHandler",
        "St12Sub5RoomHandler",
        "St13GunTurretUpdate",
        "St13RoomHandler",
        "St13RoomHandlerA",
        "St13RoomHandlerB",
        "St13RoomHandlerC",
        "St13RoomHelper",
        "St13RoomSetup",
        "St13StageSetup",
        "St13Sub0RoomHandler",
        "St13Sub1RoomHandler",
        "St14BeamUpdate",
        "St14ControllerUpdate",
        "St14DebrisUpdate",
        "St14DroppingPuppetUpdate",
        "St14FloaterUpdate",
        "St14GuardianUpdate",
        "St14RisingPuppetUpdate",
        "St14RoomSetup",
        "St14StageSetup",
        "St14Story4Sub2RoomHandler",
        "St14Story4Sub3RoomHandler",
        "St15RoomHandler",
        "St15RoomHelper",
        "St15RoomSetup",
        "St15SecondRoomCallback",
        "St15SprayUpdate",
        "St15StageSetup",
        "St15StillModelUpdate",
        "St16BarUpdate",
        "St16CatchScript",
        "St16CraneUpdate",
        "St16RoomHandler",
        "St16RoomHelper",
        "St16RoomSetup",
        "St16Script33GripStep",
        "St16StageSetup",
        "St17GateUpdate",
        "St17LeverUpdate",
        "St17RoomHandler",
        "St17RoomHelper",
        "St17RoomSetup",
        "St17StageSetup",
        "St17Story6Sub0RoomHandler",
        "St17Sub0RoomHandler",
        "St17TalkerUpdate",
        "St18BomberBirdbotUpdate",
        "St18NpcUpdate",
        "St18RoomHandler",
        "St18RoomHelper",
        "St18RoomSetup",
        "St18SquarePropUpdate",
        "St18StageSetup",
        "St18StillModelUpdate",
        "St18Story6Sub2RoomHandler",
        "St18Story7OnSub0RoomHandler",
        "St18SunUpdate",
        "St18TalkerUpdate",
        "St19CutsceneDroneUpdate",
        "St19MachineUpdate",
        "St19RoomHandler",
        "St19RoomHelper",
        "St19RoomSetup",
        "St19StageSetup",
        "St19Story6Sub0RoomHandler",
        "St19Story6Sub3RoomHandler",
        "St19Story7OnSub5RoomHandler",
        "St19Story7To9Sub0RoomHandler",
        "St19Sub0RoomHandler",
        "St19Sub1RoomHandler",
        "St19TargetUpdate",
        "St1ARoomHelper",
        "St1ARoomSetup",
        "St1AStageSetup",
        "St1BRoomHelper",
        "St1BRoomSetup",
        "St1BStageSetup",
        "St1BStory6Sub2RoomHandler",
        "St1BStory6Sub3RoomHandler",
        "St1BSub1RoomHandler",
        "St1BSub2RoomHandler",
        "St1CRoomSetup",
        "St1CStageSetup",
        "St1DFirstVisitRoomHandler",
        "St1DRoomHelper",
        "St1DRoomSetup",
        "St1DStageSetup",
        "St1DStory1Sub1RoomHandler",
        "St1DStory1Sub2RoomHandler",
        "St1ERoomSetup",
        "St1EStageSetup",
        "St1FRoomHelper",
        "St1FRoomSetup",
        "St1FStageSetup",
        "St1aMoverUpdate",
        "St1cCuedFigureCue1",
        "St1cCuedFigureCue2",
        "St1cCuedFigureCue3",
        "St1cCuedFigureUpdate",
        "St1cFaceFigureCue1",
        "St1cFaceFigureCue2",
        "St1cFaceFigureCue3",
        "St1cFaceFigureCue4",
        "St1cFaceFigureCue5",
        "St1cFaceFigureCue6",
        "St1cFaceFigureCue7",
        "St1cFaceFigureCue8",
        "St1cFaceFigureUpdate",
        "St1cFallingFigureCue1",
        "St1cFallingFigureCue10",
        "St1cFallingFigureCue2",
        "St1cFallingFigureCue3",
        "St1cFallingFigureCue4",
        "St1cFallingFigureCue5",
        "St1cFallingFigureCue6",
        "St1cFallingFigureCue7",
        "St1cFallingFigureCue8",
        "St1cFallingFigureCue9",
        "St1cFallingFigureUpdate",
        "St1cFidgetingFigureCue1",
        "St1cFidgetingFigureCue2",
        "St1cFidgetingFigureCue4",
        "St1cFidgetingFigureCue5",
        "St1cFidgetingFigureCue6",
        "St1cFidgetingFigureFidgetCue",
        "St1cFidgetingFigureUpdate",
        "St1cFlyerFlightCue",
        "St1cFlyerUpdate",
        "St1cGlowingFigureCue1",
        "St1cGlowingFigureCue10",
        "St1cGlowingFigureCue2",
        "St1cGlowingFigureCue3",
        "St1cGlowingFigureCue4",
        "St1cGlowingFigureCue5",
        "St1cGlowingFigureCue6",
        "St1cGlowingFigureCue7",
        "St1cGlowingFigureCue8",
        "St1cGlowingFigureCue9",
        "St1cGlowingFigureUpdate",
        "St1cGroundedHopperCue1",
        "St1cGroundedHopperCue2",
        "St1cGroundedHopperCue3",
        "St1cGroundedHopperUpdate",
        "St1cHopperCue1",
        "St1cHopperCue2",
        "St1cHopperShortHopCue",
        "St1cHopperUpdate",
        "St1cHoppingFlyerCue1",
        "St1cHoppingFlyerCue2",
        "St1cHoppingFlyerCue3",
        "St1cHoppingFlyerCue4",
        "St1cHoppingFlyerUpdate",
        "St1cIdleFigureCue1",
        "St1cIdleFigureCue2",
        "St1cIdleFigureCue3",
        "St1cIdleFigureCue4",
        "St1cIdleFigureCue5",
        "St1cIdleFigureUpdate",
        "St1cMeshFlipFigureCue1",
        "St1cMeshFlipFigureCue2",
        "St1cMeshFlipFigureCue3",
        "St1cMeshFlipFigureCue4",
        "St1cMeshFlipFigureCue5",
        "St1cMeshFlipFigureCue6",
        "St1cMeshFlipFigureCue7",
        "St1cMeshFlipFigureUpdate",
        "St1cPosingFigureCue1",
        "St1cPosingFigureCue2",
        "St1cPosingFigureCue3",
        "St1cPosingFigureCue4",
        "St1cPosingFigureCue5",
        "St1cPosingFigureCue6",
        "St1cPosingFigureCue7",
        "St1cPosingFigureCue8",
        "St1cPosingFigureUpdate",
        "St1cRisingPropCue1",
        "St1cRisingPropCue2",
        "St1cRisingPropCue3",
        "St1cRisingPropCue4",
        "St1cRisingPropUpdate",
        "St1cScript13",
        "St1cScript13EffectAgainStep",
        "St1cScript13EffectEndsStep",
        "St1cScript13FadeInToRoom4Step",
        "St1cScript13FadeToRoom5Step",
        "St1cScript13FastFadeOutStep",
        "St1cScript13FinalFadeStep",
        "St1cScript13GlowStep",
        "St1cScript13GroundPoseStep",
        "St1cScript13HiddenCue",
        "St1cScript13LongFadeInStep",
        "St1cScript13LookUpStep",
        "St1cScript13OpeningCuesStep",
        "St1cScript13OpeningWaitStep",
        "St1cScript13PausedPoseStep",
        "St1cScript13SecondCastStep",
        "St1cScript13SecondVoiceStep",
        "St1cScript13Step13",
        "St1cScript13Step14",
        "St1cScript13Step15",
        "St1cScript13Step16",
        "St1cScript13Step17",
        "St1cScript13Step18",
        "St1cScript13Step19",
        "St1cScript13Step21",
        "St1cScript13Step22",
        "St1cScript13Step23",
        "St1cScript13Step30",
        "St1cScript13Step31",
        "St1cScript13Step32",
        "St1cScript13Step33",
        "St1cScript13Step34",
        "St1cScript13Step35",
        "St1cScript13Step37",
        "St1cScript13Step38",
        "St1cScript13Step39",
        "St1cScript13Step40",
        "St1cScript13Step41",
        "St1cScript13Step42",
        "St1cScript13Step6",
        "St1cScript13Step7",
        "St1cScript13ThirdCastStep",
        "St1cScript13ThirdVoiceStep",
        "St1cScript13TwoPosesStep",
        "St1cSecondStillModelUpdate",
        "St1cShipCue1",
        "St1cShipCue2",
        "St1cShipCue3",
        "St1cShipCue4",
        "St1cShipUpdate",
        "St1cStillModelUpdate",
        "St1cVanishingWalkerCue1",
        "St1cVanishingWalkerCue2",
        "St1cVanishingWalkerCue3",
        "St1cVanishingWalkerCue4",
        "St1cVanishingWalkerUpdate",
        "St1cWalkingFigureCue1",
        "St1cWalkingFigureCue2",
        "St1cWalkingFigureCue3",
        "St1cWalkingFigureCue4",
        "St1cWalkingFigureUpdate",
        "St1dBoulderTrailUpdate",
        "St1dBoulderUpdate",
        "St1dCrystalUpdate",
        "St1dDustRingUpdate",
        "St1dExitLightUpdate",
        "St1dPillarUpdate",
        "St1dRockUpdate",
        "St1fBirdbotTankUpdate",
        "St20PropUpdate",
        "St20RoomHelper",
        "St20RoomSetup",
        "St20StageSetup",
        "St21RisingModelBob",
        "St21RisingModelDrift",
        "St21RisingModelRise",
        "St21RisingModelSink",
        "St21RoomCase",
        "St21RoomHelper",
        "St21RoomSetup",
        "St21StageSetup",
        "St22FireShotUpdate",
        "St22RoomCase",
        "St22RoomHelper",
        "St22RoomSetup",
        "St22StageSetup",
        "St23RoomHelper",
        "St23RoomSetup",
        "St23StageSetup",
        "St24GateGuardianUpdate",
        "St24GateScript",
        "St24GateStep1",
        "St24GateStep2",
        "St24RoomHelper",
        "St24RoomSetup",
        "St24RouteNpcUpdate",
        "St24Script56",
        "St24SecondRoomCallback",
        "St24StageSetup",
        "St24Story3Sub0RoomHandler",
        "St24Story4OnSub5RoomHandler",
        "St24Story4Sub1RoomHandler",
        "St24Story4Sub2RoomHandler",
        "St24StructureUpdate",
        "St25RisingModelUpdate",
        "St25RoomHelper",
        "St25RoomSetup",
        "St25Script1cFigureUpdate",
        "St25StageSetup",
        "St25Story3Sub5RoomHandler",
        "St25Story4OnSub1To4RoomHandler",
        "St25TankLandingUpdate",
        "St25TankTankDropCue",
        "St25TankTankOnGroundCue",
        "St25TankTankStandsCue",
        "St25TankWreckUpdate",
        "St25TronSceneAnswerStep",
        "St25TronSceneAtGateStep",
        "St25TronSceneEndStep",
        "St25TronSceneFacesStopStep",
        "St25TronSceneFirstWordsStep",
        "St25TronSceneGateOpensStep",
        "St25TronSceneLastWordsStep",
        "St25TronSceneListenStep",
        "St25TronSceneLongTalkStep",
        "St25TronSceneRunInStep",
        "St25TronSceneSecondWordsStep",
        "St25TronSceneSlowDownStep",
        "St25TronSceneStep10",
        "St25TronSceneStep5",
        "St25TronSceneStep7",
        "St25TronSceneTronBeatenStep",
        "St25TronSceneTronLeavesStep",
        "St25TronSceneTronTalksStep",
        "St25TronSceneTurnAndWalkStep",
        "St25TronSceneTurnToVillageStep",
        "St25TronSceneVillagersTalkStep",
        "St25TronSceneWalkToTronStep",
        "St25TronSceneWreckStep",
        "St25TronScript1b",
        "St25TronScript1c",
        "St25TronTronCue1Cue",
        "St25TronTronCue2Cue",
        "St25TronTronCue3Cue",
        "St25TronTronCue4Cue",
        "St25TronTronCue5Cue",
        "St25TronUpdate",
        "St26RoomCallback",
        "St26RoomHandler",
        "St26RoomHandlerA",
        "St26RoomHandlerB",
        "St26RoomHelper",
        "St26RoomSetup",
        "St26SecondRoomCallback",
        "St26StageSetup",
        "St26Sub2RoomHandler",
        "St26Sub3RoomHandler",
        "St26Sub6RoomHandler",
        "St27RoomHandler",
        "St27RoomHandlerA",
        "St27RoomHandlerB",
        "St27RoomHelper",
        "St27RoomSetup",
        "St27StageSetup",
        "St27Sub0RoomHandler",
        "St27Sub0RoomHandlerA",
        "St27Sub1RoomHandler",
        "St27Sub2RoomHandler",
        "St27Sub7RoomHandler",
        "St27Sub8RoomHandler",
        "St28GuardUpdate",
        "St28LavaBossFall",
        "St28LavaBossLanding",
        "St28RoomHandler",
        "St28RoomHandlerA",
        "St28RoomHandlerB",
        "St28RoomHandlerC",
        "St28RoomHelper",
        "St28RoomSetup",
        "St28StageSetup",
        "St28Sub0RoomHandler",
        "St28Sub2RoomHandler",
        "St29RoomHandler",
        "St29RoomHandlerA",
        "St29RoomHandlerB",
        "St29RoomHandlerC",
        "St29RoomHelper",
        "St29RoomSetup",
        "St29SecondRoomCallback",
        "St29StageSetup",
        "St2ARoomHandler",
        "St2ARoomHandlerA",
        "St2ARoomHandlerB",
        "St2ARoomHelper",
        "St2ARoomSetup",
        "St2AStageSetup",
        "St2BRoomHandler",
        "St2BRoomHandlerA",
        "St2BRoomHelper",
        "St2BRoomSetup",
        "St2BStageSetup",
        "St2BStoryCSub0RoomHandler",
        "St2CRoomHelper",
        "St2CRoomSetup",
        "St2CStageSetup",
        "St2CStoryCSub0RoomHandler",
        "St2DRoomCallback",
        "St2DRoomHelper",
        "St2DRoomSetup",
        "St2DSecondRoomCallback",
        "St2DStageSetup",
        "St2DSub0RoomHandler",
        "St2DSub2RoomHandler",
        "St2DSub5RoomHandler",
        "St2DSub6RoomHandler",
        "St2DSub7RoomHandler",
        "St2ERoomSetup",
        "St2EStageSetup",
        "St2ESub2RoomHandler",
        "St2ESub3RoomHandler",
        "St2ESub7RoomHandler",
        "St2FRoomHelper",
        "St2FRoomHelperA",
        "St2FRoomSetup",
        "St2FStageSetup",
        "St2FSub0RoomHandler",
        "St2FSub1RoomHandler",
        "St2FSub5RoomHandler",
        "St2FSub6RoomHandler",
        "St2FSub7RoomHandler",
        "St2FSub8RoomHandler",
        "St2bPropPlateBind",
        "St2bPropPlateRun",
        "St2bPropPotBreak",
        "St2bPropPotCarried",
        "St2bPropPotFlight",
        "St2bPropPotMain",
        "St2bPropPotRest",
        "St2bPropPotWait",
        "St2bPropRebindInPlace",
        "St2bPropSwitchRun",
        "St2bPropUpdate",
        "St2eRoom4Handler",
        "St30RoomHelper",
        "St30RoomSetup",
        "St30StageSetup",
        "St31RoomHelper",
        "St31RoomSetup",
        "St31StageSetup",
        "St32PlateUpdate",
        "St32QuizStep",
        "St32RoomHandler",
        "St32RoomHelper",
        "St32RoomSetup",
        "St32StageSetup",
        "St33KeyDropScript",
        "St33KeyScriptDropStep",
        "St33KeyScriptFallStep",
        "St33KeyScriptPlaceStep",
        "St33KeyScriptTurnStep",
        "St33RoomHandler",
        "St33RoomSetup",
        "St33StageSetup",
        "St33StripScrollUpdate",
        "St34RoomSetup",
        "St34StageSetup",
        "St34Sub5RoomHandler",
        "St35BeatenScript",
        "St35BeatenStep2",
        "St35BeatenStep3",
        "St35BeatenStep4",
        "St35BeatenVoiceStep",
        "St35DissolvesScript",
        "St35DissolvesStep2",
        "St35DissolvesStep3",
        "St35DissolvesStep4",
        "St35DissolvesStep5",
        "St35DissolvesVoiceStep",
        "St35DropsInScript",
        "St35DropsInStep1",
        "St35DropsInStep2",
        "St35DropsInStep3",
        "St35DropsInStep4",
        "St35RoomCallback",
        "St35RoomHandler",
        "St35RoomHandlerA",
        "St35RoomHelper",
        "St35RoomSetup",
        "St35SecondRoomCallback",
        "St35StageSetup",
        "St35Sub1RoomHandler",
        "St35Sub3RoomHandler",
        "St35Sub4RoomHandler",
        "St35WakesScript",
        "St35WakesStep2",
        "St35WakesStep3",
        "St35WakesStep4",
        "St35WakesStep5",
        "St35WakesStep6",
        "St35WakesVoiceStep",
        "St36RoomHandler",
        "St36RoomHelper",
        "St36RoomSetup",
        "St36StageSetup",
        "St36Sub2RoomHandler",
        "St36Sub4RoomHandler",
        "St36Sub4RoomHandlerB",
        "St36Sub5RoomHandler",
        "St37OpeningScript",
        "St37OpeningStep1",
        "St37OpeningStep2",
        "St37OpeningStep3",
        "St37OpeningStep4",
        "St37OpeningStep5",
        "St37RoomHandler",
        "St37RoomHelper",
        "St37RoomSetup",
        "St37StageSetup",
        "St37StoryASub13RoomHandler",
        "St37StoryASub2RoomHandler",
        "St37StoryASub4RoomHandler",
        "St37StoryBOnSub13RoomHandler",
        "St37StoryBOnSub2RoomHandler",
        "St37StoryBOnSub4RoomHandler",
        "St37Sub14RoomHandler",
        "St37Sub5RoomHandler",
        "St37Sub8RoomHandler",
        "St37Sub9RoomHandler",
        "St38RoomHandler",
        "St38RoomHelper",
        "St38RoomSetup",
        "St38StageSetup",
        "St38Sub11RoomHandler",
        "St38Sub2RoomHandler",
        "St38Sub3RoomHandler",
        "St38Sub5RoomHandler",
        "St38Sub6RoomHandler",
        "St38Sub7RoomHandler",
        "St38Sub8RoomHandler",
        "St39CastMember",
        "St39RoomSetup",
        "St39Script68",
        "St39SetUpState",
        "St39StageSetup",
        "St39TimerState",
        "St39WrapUpState",
        "St3ARoomSetup",
        "St3AStageSetup",
        "St3BRoomHandler",
        "St3BRoomHandlerA",
        "St3BRoomHelper",
        "St3BRoomSetup",
        "St3BStageSetup",
        "St3CRoomHandler",
        "St3CRoomHandlerA",
        "St3CRoomHelper",
        "St3CRoomSetup",
        "St3CStageSetup",
        "St3CSub0RoomHandler",
        "St3CSub1RoomHandler",
        "St3CSub2RoomHandler",
        "St3CSub3RoomHandler",
        "St3DRoomHandler",
        "St3DRoomHandlerA",
        "St3DRoomHandlerB",
        "St3DRoomHelper",
        "St3DRoomSetup",
        "St3DSecondRoomCallback",
        "St3DStageSetup",
        "St3DStory5OnSub0RoomHandler",
        "St3DStory5OnSub0RoomHandlerB",
        "St3DStory5OnSub0RoomHandlerC",
        "St3DStory5OnSub1RoomHandler",
        "St3DStory5OnSub1RoomHandlerB",
        "St3DStory5OnSub1RoomHandlerC",
        "St3DStory5ToASub3RoomHandler",
        "St3DStoryBToDSub2RoomHandler",
        "St3ERoomHelper",
        "St3ERoomSetup",
        "St3EStageSetup",
        "St3ESub0RoomHandler",
        "St3ESub1RoomHandler",
        "St3ESub2RoomHandler",
        "St3ESub3RoomHandler",
        "St3ESub4RoomHandler",
        "St3ESub5RoomHandler",
        "St3ESub6RoomHandler",
        "St3FRoomHandler",
        "St3FRoomHelper",
        "St3FRoomSetup",
        "St3FStageSetup",
        "St3FStory11OnSub1RoomHandler",
        "St3FStory2OnSub3RoomHandler",
        "St3FStoryBToDSub1RoomHandler",
        "St3FStoryETo10Sub1RoomHandler",
        "St3FSub1RoomHandler",
        "St3cPropUpdate",
        "St3cRisingModelUpdate",
        "St3eFloatingModelUpdate",
        "St3eKeyReport65",
        "St3eKeyReport66",
        "St3eKeyReport67",
        "St3eReport64Step10",
        "St3eReport64Step2",
        "St3eReport64Step3",
        "St3eReport64Step4",
        "St3eReport64Step5",
        "St3eReport64Step6",
        "St3eReport64Step7",
        "St3eReport64Step8",
        "St3eReport64Step9",
        "St3eReport64WalkInStep",
        "St3eReport65FirstSpeakerStep",
        "St3eReport65LastStep",
        "St3eReport65SecondSpeakerStep",
        "St3eReport65WalkInStep",
        "St3eReport66FirstSpeakerStep",
        "St3eReport66LastStep",
        "St3eReport66SecondSpeakerStep",
        "St3eReport66WalkInStep",
        "St3eReport67FirstSpeakerStep",
        "St3eReport67LastStep",
        "St3eReport67SecondSpeakerStep",
        "St3eReport67WalkInStep",
        "St3eScript64",
        "St3eSpeakerUpdate",
        "St40RoomCallback",
        "St40RoomHandlerSub10",
        "St40RoomHandlerSub11",
        "St40RoomHandlerSub12",
        "St40RoomHandlerSub13",
        "St40RoomHandlerSub3",
        "St40RoomHandlerSub4",
        "St40RoomHandlerSub5",
        "St40RoomHandlerSub6",
        "St40RoomHandlerSub7",
        "St40RoomHandlerSub8",
        "St40RoomHandlerSub9",
        "St40RoomHelper",
        "St40RoomSetup",
        "St40SecondRoomCallback",
        "St40StageSetup",
        "St40Sub0RoomHandler",
        "St40Sub1RoomHandler",
        "St40Sub2RoomHandler",
        "St41RoomCallback",
        "St41RoomHelper",
        "St41RoomSetup",
        "St41SecondRoomCallback",
        "St41StageSetup",
        "St41Sub0RoomHandler",
        "St41Sub12RoomHandler",
        "St41Sub1RoomHandler",
        "St41Sub2RoomHandler",
        "St41Sub3RoomHandler",
        "St41Sub5RoomHandler",
        "St41Sub7RoomHandler",
        "St42RoomHelper",
        "St42RoomSetup",
        "St42StageSetup",
        "St42Sub10RoomHandler",
        "St42Sub14RoomHandler",
        "St42Sub1RoomHandler",
        "St42Sub4RoomHandler",
        "St42Sub6RoomHandler",
        "St42Sub7RoomHandler",
        "St42Sub8RoomHandler",
        "St43RoomCallback",
        "St43RoomSetup",
        "St43SecondRoomCallback",
        "St43StageSetup",
        "St43Sub0RoomHandler",
        "St43Sub2RoomHandler",
        "St44RoomHelper",
        "St44RoomSetup",
        "St44StageSetup",
        "St44Sub2RoomHandler",
        "St45BossUpdate",
        "St45BreathPuffUpdate",
        "St45MouthShotUpdate",
        "St45RoomSetup",
        "St45ShockWaveUpdate",
        "St45SpitBlobUpdate",
        "St45StageSetup",
        "St45Sub1RoomHandler",
        "St46RoomSetup",
        "St46StageSetup",
        "St46Story5To9Sub0RoomHandler",
        "St46Story5To9Sub3RoomHandler",
        "St47FlyerUpdate",
        "St47PersonUpdate",
        "St47RoomHandler",
        "St47RoomHandlerA",
        "St47RoomHandlerB",
        "St47RoomHandlerC",
        "St47RoomHandlerD",
        "St47RoomHandlerE",
        "St47RoomHandlerF",
        "St47RoomHandlerG",
        "St47RoomHandlerH",
        "St47RoomHandlerI",
        "St47RoomHandlerJ",
        "St47RoomHelper",
        "St47RoomSetup",
        "St47StageSetup",
        "St47VisitorUpdate",
        "St48GunshipUpdate",
        "St48RoomCallback",
        "St48RoomHandler",
        "St48RoomHandlerA",
        "St48RoomHelper",
        "St48RoomSetup",
        "St48SandScript",
        "St48StageSetup",
        "St48StoryCSub1RoomHandler",
        "St49DropshipCableUpdate",
        "St49DropshipCargoUpdate",
        "St49DropshipUpdate",
        "St49FlutterUpdate",
        "St49LandingDescentVoiceStep",
        "St49LandingDroppedStep",
        "St49LandingFarBelowStep",
        "St49LandingHatchOpensStep",
        "St49LandingLandedStep",
        "St49LandingOpeningStep",
        "St49LandingReturnFadeStep",
        "St49LandingReturnHatchStep",
        "St49LandingReturnLandedStep",
        "St49LandingReturnVoiceStep",
        "St49LandingRideStep",
        "St49LandingScript",
        "St49RoomCase",
        "St49RoomSetup",
        "St49StageSetup",
        "St49StillModelUpdate",
        "St4ARoomCallback",
        "St4ARoomSetup",
        "St4ASecondRoomCallback",
        "St4AStageSetup",
        "St4BRoomSetup",
        "St4BStageSetup",
        "St4BSub0RoomHandler",
        "St4BSub5RoomHandler",
        "St4CRoomSetup",
        "St4CStageSetup",
        "St4CSub3RoomHandler",
        "St4CSub4RoomHandler",
        "St4DRoomHelper",
        "St4DRoomSetup",
        "St4DStageSetup",
        "St4ERoomHelper",
        "St4ERoomSetup",
        "St4EStageSetup",
        "St4FRoomCallback",
        "St4FRoomHelper",
        "St4FRoomSetup",
        "St4FSecondRoomCallback",
        "St4FStageSetup",
        "St4aEventTriggerUpdate",
        "St4aRoomsLessonHandler",
        "St4aRoomsSecondFightHandler",
        "St4aRoomsStartRoomHandler",
        "St4aRoomsUnlockHandler",
        "St4aRoomsZoneFightHandler",
        "St4bCastFree",
        "St4bCirclingPieceUpdate",
        "St4bEscapeBind",
        "St4bEscapeEnd",
        "St4bEscapeOpening",
        "St4bEscapeRun",
        "St4bEscapeScript",
        "St4bEscapeSteps",
        "St4bFlyingBossUpdate",
        "St4bRetreatBind",
        "St4bRetreatEnd",
        "St4bRetreatOpening",
        "St4bRetreatRun",
        "St4bRetreatScript",
        "St4bRetreatSteps",
        "St4bScript59",
        "St4bScript59Step1",
        "St4bScript59Step10",
        "St4bScript59Step11",
        "St4bScript59Step12",
        "St4bScript59Step13",
        "St4bScript59Step14",
        "St4bScript59Step15",
        "St4bScript59Step16",
        "St4bScript59Step17",
        "St4bScript59Step18",
        "St4bScript59Step19",
        "St4bScript59Step2",
        "St4bScript59Step20",
        "St4bScript59Step21",
        "St4bScript59Step22",
        "St4bScript59Step23",
        "St4bScript59Step24",
        "St4bScript59Step25",
        "St4bScript59Step26",
        "St4bScript59Step27",
        "St4bScript59Step28",
        "St4bScript59Step3",
        "St4bScript59Step4",
        "St4bScript59Step5",
        "St4bScript59Step6",
        "St4bScript59Step7",
        "St4bScript59Step8",
        "St4bScript59Step9",
        "St4bScriptsEscapeBlasts",
        "St4bScriptsEscapeLook",
        "St4bScriptsEscapeVoice",
        "St4bScriptsRetreatRoll",
        "St4bScriptsRetreatVoice",
        "St4bSpinningPieceUpdate",
        "St4bStillBind",
        "St4cIntro0",
        "St4cIntro2",
        "St4cIntroScript",
        "St4cIntroStep1",
        "St4cIntroStep2",
        "St4cIntroStep3",
        "St4cIntroStep4",
        "St4cIntroStep5",
        "St4cOutro0",
        "St4cOutro2",
        "St4cOutroScript",
        "St4cOutroStep1",
        "St4cOutroStep2",
        "St4dCellAWatch",
        "St4dCellBWatch",
        "St4dLockCleared",
        "St4dSub0Locks",
        "St4dSub6Group",
        "St4dSub8Lock",
        "St4eGroupSpawnerUpdate",
        "St4eRoom4Handler",
        "St4fSub11Records",
        "St4fSub12Record",
        "St4fSub7Roll",
        "St50GroupSpawnerUpdate",
        "St50RoomHelper",
        "St50RoomSetup",
        "St50StageSetup",
        "St50Sub10Record",
        "St50Sub9Group",
        "St51Room1Handler",
        "St51Room2Handler",
        "St51RoomCallback",
        "St51RoomSetup",
        "St51SecondRoomCallback",
        "St51StageSetup",
        "St52RoomRoutine",
        "St52RoomSetup",
        "St52StageSetup",
        "St53CastStep1",
        "St53CastStep10",
        "St53CastStep11",
        "St53CastStep12",
        "St53CastStep13",
        "St53CastStep14",
        "St53CastStep15",
        "St53CastStep16",
        "St53CastStep17",
        "St53CastStep18",
        "St53CastStep19",
        "St53CastStep2",
        "St53CastStep3",
        "St53CastStep4",
        "St53CastStep5",
        "St53CastStep6",
        "St53CastStep7",
        "St53CastStep8",
        "St53CastStep9",
        "St53RideEnd",
        "St53RideOpening",
        "St53RideScript",
        "St53RideStep1",
        "St53RideStep2",
        "St53RideStep3",
        "St53RoomSetup",
        "St53SnowTrackEnd",
        "St53SnowTrackOpening",
        "St53SnowTrackScript",
        "St53SnowTrackStep1",
        "St53SnowTrackStep2",
        "St53SnowTrackStep3",
        "St53StageSetup",
        "St53StoryFRoomHandler",
        "St53StoryFSub1RoomHandler",
        "St54BindRoutine",
        "St54ClearScreenSaveMenu",
        "St54ClearScreenShowTime",
        "St54RoomCase",
        "St54RoomSetup",
        "St54StageSetup",
        "St54SwingRoutine",
        "St55RoomRoutine",
        "St55RoomSetup",
        "St55StageSetup",
        "St56RoomCase",
        "St56RoomSetup",
        "St56StageSetup",
        "St57RoomCase",
        "St57RoomSetup",
        "St57StageSetup",
        "St58RoomSetup",
        "St58StageSetup",
        "St58Sub1RoomHandler",
        "St59RoomCallback",
        "St59RoomHelper",
        "St59RoomSetup",
        "St59StageSetup",
        "St5ARoomSetup",
        "St5AStageSetup",
        "St5BRoomSetup",
        "St5BStageSetup",
        "St5CRoomSetup",
        "St5CStageSetup",
        "St5cBossUpdate",
        "StaffRollScript",
        "StageCallbackScript",
        "StageDustEffectUpdate",
        "StageEffectFromVariant2Update",
        "StageEffectUpdate",
        "StageMusicCall",
        "StandingCastUpdate",
        "StaticFreedState",
        "StatueMinionUpdate",
        "StatuePartUpdate",
        "SteeredPropUpdate",
        "StillFigureCastUpdate",
        "StillModelUpdate",
        "StillSpriteEffectUpdate",
        "StompShotUpdate",
        "StormRibbonUpdate",
        "StoryFlagClearCall",
        "StoryFlagTestCall",
        "StripScrollUpdate",
        "SubruinDropSpawnerUpdate",
        "SubruinGroup0",
        "SubruinGroup1",
        "SubruinGroup10",
        "SubruinGroup2",
        "SubruinGroup3",
        "SubruinGroup4",
        "SubruinGroup5",
        "SubruinGroup6",
        "SubruinGroup7",
        "SubruinGroup8",
        "SubruinGroup9",
        "SubruinLineTriggerUpdate",
        "SuctionUpdate",
        "SwingingModelUpdate",
        "SwirlingMotesUpdate",
        "TakeOffCastUpdate",
        "TakeoffArcUpdate",
        "TalkPropUpdate",
        "TalkerUpdate",
        "TalkingNpcSt17Update",
        "TalkingNpcUpdate",
        "TankShellUpdate",
        "TeleporterEffectUpdate",
        "TeleporterTransition",
        "TextLayoutChoiceOption",
        "TextLayoutColour",
        "TextLayoutColourBack",
        "TextLayoutInsertItemName",
        "TextLayoutInsertKeyName",
        "TextLayoutInsertNumber",
        "TextLayoutInsertPlace",
        "TextLayoutInsertString",
        "TextLayoutInsertTime",
        "TextLayoutLoadRecordTime",
        "TextLayoutLoadSaveSlot",
        "TextLayoutNewLine",
        "TextLayoutPenShift",
        "TextLayoutPenShiftY",
        "TextLayoutReturn",
        "TextLayoutRubyGlyph",
        "TextLayoutSkipChoiceFlags",
        "TextLayoutSkipEight",
        "TextLayoutSkipFive",
        "TextLayoutSkipFour",
        "TextLayoutSkipFourteen",
        "TextLayoutSkipJumps",
        "TextLayoutSkipNine",
        "TextLayoutSkipOne",
        "TextLayoutSkipOptions",
        "TextLayoutSkipSeven",
        "TextLayoutSkipSix",
        "TextLayoutSkipThirteen",
        "TextLayoutSkipThree",
        "TextLayoutSkipTwo",
        "TextLayoutSkipWindows",
        "TextLayoutWriteZenny",
        "TextLogicAddCounter",
        "TextLogicAskInWindow",
        "TextLogicBalance",
        "TextLogicChoice",
        "TextLogicChoiceNoCancel",
        "TextLogicClearFlag",
        "TextLogicClearRecordName",
        "TextLogicCloseAfter",
        "TextLogicFastForward",
        "TextLogicFrameClose",
        "TextLogicFrameOpen",
        "TextLogicGeometry",
        "TextLogicHitPoints",
        "TextLogicHold",
        "TextLogicInsertItemName",
        "TextLogicInsertKeyName",
        "TextLogicInsertNumber",
        "TextLogicInsertPlace",
        "TextLogicInsertString",
        "TextLogicInsertTime",
        "TextLogicJump",
        "TextLogicJumpByBalanceTier",
        "TextLogicJumpByChoice",
        "TextLogicJumpByCounter",
        "TextLogicJumpByDifficulty",
        "TextLogicJumpByDockCounter",
        "TextLogicJumpByFlag",
        "TextLogicJumpByStory",
        "TextLogicJumpByToolKitTier",
        "TextLogicLayer",
        "TextLogicLayoutBits",
        "TextLogicLoadRecordTime",
        "TextLogicNameRecord",
        "TextLogicNewLine",
        "TextLogicOpenWindow",
        "TextLogicOpenWindowByChoice",
        "TextLogicOpenWindowOnChoice",
        "TextLogicPause",
        "TextLogicPlayerImmune",
        "TextLogicPlayerShown",
        "TextLogicReturn",
        "TextLogicRubyGlyph",
        "TextLogicScrollPage",
        "TextLogicSetFlag",
        "TextLogicSetFlagByChoice",
        "TextLogicSkipOne",
        "TextLogicSkipThree",
        "TextLogicSkipTwo",
        "TextLogicSmallLayout",
        "TextLogicSound",
        "TextLogicSpeaker",
        "TextLogicTalkCamera",
        "TextLogicToolKitBalance",
        "TextLogicTypeDelay",
        "TextLogicTypingSoundOff",
        "TextLogicTypingSoundOn",
        "TextLogicVoice",
        "TextLogicWaitButton",
        "TextLogicWaitButtonArrow",
        "TextLogicWaitEvent",
        "TextLogicWaitOrHelp",
        "TextLogicWaitPage",
        "TextLogicWaitVoice",
        "TextLogicWaitWindow",
        "TextLogicWarp",
        "TextLogicWeaponEnergy",
        "ThrowableUpdate",
        "ThrownPropUpdate",
        "TimedCastUpdate",
        "TitleModelUpdate",
        "TonguesUpdate",
        "TorpedoSwimmerUpdate",
        "TorpedoUpdate",
        "TownBirdUpdate",
        "TownBystanderStep1",
        "TownBystanderStep2",
        "TownBystanderStep3",
        "TownBystanderStep4",
        "TownCastJoesLabMaria",
        "TownCastJoesLabRoll",
        "TownCastJunkShopClerk",
        "TownCastJunkShopRoll",
        "TownCastJunkShopThird",
        "TownCastRunningRoll",
        "TownCastSt47Person",
        "TownChapterClerkTalksStep",
        "TownChapterDoorKnockStep",
        "TownChapterLabFarewellStep",
        "TownChapterLabMessageStep",
        "TownChapterLabReplyStep",
        "TownChapterLookAroundLabStep",
        "TownChapterRollExplainsStep",
        "TownChapterRollGreetsStep",
        "TownChapterRollLeavesStep",
        "TownChapterShopTalkStep",
        "TownChapterTurnToDoorStep",
        "TownChapterWalkIntoLabStep",
        "TownChapterWalkToCounterStep",
        "TownChapterWatchRollLeaveStep",
        "TownExamDoorsStep1",
        "TownExamDoorsWalkOutStep",
        "TownFirstVisitScript",
        "TownKickUpdate",
        "TownNpcUpdate",
        "TownReturnScript",
        "TownRoomCallback",
        "TownSideBystanderSetUp",
        "TownSideBystanderSteps",
        "TownSideBystanderWrapUp",
        "TownSideExamDoorsSetUp",
        "TownSideExamDoorsSteps",
        "TownSideExamDoorsWrapUp",
        "TownSideSt06VoiceSetUp",
        "TownSideSt06VoiceWrapUp",
        "TownSideVisitorSetUp",
        "TownSideVisitorWrapUp",
        "TownSt06VoiceStep1",
        "TownSt06VoiceStep2",
        "TownSt06VoiceStep3",
        "TownStandingCastUpdate",
        "TownTalkPropUpdate",
        "TownVisitorStep1",
        "TownVisitorStep2",
        "TownVisitorStep3",
        "TownVisitorStep4",
        "TownVisitorStep5",
        "TownVisitorStep6",
        "TownVisitorStep7",
        "TownVisitorStep8",
        "TrainAttackRoomHandler",
        "TrainLostScript",
        "TransitionHandshakeEnd",
        "TransitionSequence",
        "TravelIconUpdate",
        "TravelPickFirstDrop",
        "TravelPickNextOnMap2",
        "TravelPickNextOnMapB",
        "TravelPickNextOnMapE",
        "TravelPickNextOrSt39",
        "TravelPickNoFlight",
        "TravelPickSt46",
        "TravelScript",
        "TreasureChestUpdate",
        "TriggerAllFlags",
        "TriggerAtOnce",
        "TriggerCountdown",
        "TriggerCrossedXDown",
        "TriggerCrossedXUp",
        "TriggerCrossedZUp",
        "TriggerFlagsByMask",
        "TriggerInPlanarReach",
        "TriggerInReach",
        "TriggerReachedZ",
        "TriggerSpecialWeapon",
        "TronBossUpdate",
        "TronPartnerUpdate",
        "TumblingBlockUpdate",
        "TurningModelUpdate",
        "TurretBombUpdate",
        "TurretBulletHoming",
        "TurretBulletUpdate",
        "TurretShotUpdate",
        "TwoFormReaverbotUpdate",
        "UvAnimationListSetup",
        "VacuumArmShotUpdate",
        "ValveUpdate",
        "VehicleFireballUpdate",
        "VisitorScript",
        "VolleyShotUpdate",
        "WalkOffEnd",
        "WalkOffStart",
        "WalkOffTransition",
        "WalkerBombUpdate",
        "WalkerShotUpdate",
        "WallBlastUpdate",
        "WallBreakBurstUpdate",
        "WallCrumbleUpdate",
        "WallDebrisUpdate",
        "WaterBubbleControllerUpdate",
        "WaterSplashUpdate",
        "WaveTileUpdate",
        "WeaponModelUpdate",
        "WindowGiantUpdate",
    };
    return kNames[static_cast<uint16_t>(id)];
}

} // namespace od
