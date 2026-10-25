// The tables of the game's data by what they are (engine/data_source TableKey). Each source knows where its copy of
// a table lives (the PC source: engine/pc_tables.inc, engine/pc_room_tables.inc); the typed struct and loader of a
// table sit with the system that reads it.
#pragma once

#include <cstdint>

namespace od {

// X(name): one table. Per-stage / per-room tables take TableKey::index (the stage / roomIndex(stage, room)).
#define OD_TABLE_IDS(X)                                                                                                \
    /* ---- per stage / per room (index) */                                                                            \
    X(RoomParams)           /* roomIndex: the room's parameter record (0x2c bytes: fog colour, shift, ...) */          \
    X(FloorUvs)             /* stage: the floor quads' UV table (32 u16 pairs) */                                      \
    X(RoomCamera)           /* roomIndex: the room's camera record (12 bytes) */                                       \
    X(RoomExits)            /* roomIndex: the room's exit list (a pointer to 0x18-byte records, 0xff ends it) */       \
    X(RoomSkyLayers)        /* roomIndex: the room setup's sky layer list */                                          \
    X(RoomSkyGradient)      /* roomIndex: the room's sky gradient (a pointer to {u32, u32} bands) */                   \
    X(RoomSkyGradientEarly) /* roomIndex: the same while the story is at its first steps (ST10 / ST11 / ST1D) */       \
    X(RoomSpawnList) /* roomIndex: the room's spawn list (a pointer to its spawn records) */                        \
    X(RoomScriptHandlers) /* stage: the room script's handler table (by sub-room, or by story then sub-room) */     \
    /* ---- per stage (index: the stage): the tables the stage setup points the game at */                          \
    X(StageAnimatedClasses) /* the animated pool's class table */                                                   \
    X(StageStaticClasses) /* the static pool's class table */                                                       \
    X(StageProjectileClasses) /* the projectile pool's class table */                                               \
    X(StageEffectClasses) /* the effect pool's class table */                                                       \
    X(StageControllerClasses) /* the controller pool's class table */                                               \
    X(StageEncounterZones) /* the encounter zone tables (by sub-room) */                                            \
    X(StageEncounterGroups) /* the encounter group tables (by group) */                                             \
    X(StageEncounterField) /* the random field encounter table */                                                   \
    X(StageEncounterEnemyGroups) /* the encounter enemy groups */                                                   \
    X(StageClutAnimations) /* the CLUT animations */                                                                \
    X(StageGroundKinds) /* the ground kinds */                                                                      \
    X(StageTownRoutes) /* the town people's routes */                                                               \
    X(StageTransitionHandlers) /* the transition kinds' handlers */                                                 \
    X(StageVoices) /* the stage's voice table */                                                                    \
    X(StageSecondVoices) /* the stage's second voice table */                                                       \
    X(EncounterLayoutTable) /* index: the encounter layout trees' op that writes it (a zone / group table) */       \
    /* ---- the engine's / the game's math */                                                                          \
    X(FloorOrientations) /* the floor quads' 16 corner orders */                                                       \
    X(TownNpcPauseTicks) /* the town people's arrival pauses by rng & 0xf */ \
    X(TrigTable) /* the sine / cosine of the 4096 angles {s16 sin, s16 cos} */                                         \
    /* ---- files */                                                                                                   \
    X(DatFileNames) /* a pointer to the DAT file's name by load index */                                               \
    X(PlayerBodyFiles) /* the body file's load index by costume * 6 + variant */                                       \
    X(PlayerWeaponFiles) /* the special weapon file's load index by weapon */                                          \
    X(PlayerPartBlocks) /* the model's part blocks by costume * 7 + block (u32 PC addresses of the PS1 RAM image) */   \
    X(PlayerPartRows) /* the block drawn in each part group, a row of 5 by the arms' weapon state */                   \
    X(PlayerPartGroupBones) /* the part groups' bones, group after group */                                            \
    X(PlayerPartGroupCounts) /* the bone count of each part group */                                                   \
    X(StageGeometryFiles) /* the stages' geometry file ids at the start (u16 by stage; the selectors rewrite them) */ \
    X(StageTextureFiles) /* the stages' texture file ids at the start (u16 by stage) */                               \
    /* ---- handler tables (function pointers: engine/routines.csv) */                                                 \
    X(PlayerStateHandlers) /* the player's states */                                                                   \
    X(PlayerInputHandlers) /* the player's sub-states' input */                                                        \
    X(PlayerMotionHandlers) /* the player's sub-states' motion */                                                      \
    X(BusterArmHandlers) /* the buster arm by weapon */                                                                \
    X(SpecialArmHandlers) /* the special weapon arm by weapon */                                                       \
    X(CameraEasingRoutines) /* the script camera's easing curves */                                                    \
    X(EventRunners) /* the event modes' runners */                                                                     \
    X(ShopBlockHandlers) /* the shop block's states */                                                                 \
    X(ShopMenuHandlers) /* the shop's menus */                                                                         \
    X(ShopBuyStepHandlers) /* the buy menu's steps */                                                                  \
    X(ShopSellStepHandlers) /* the sell menu's steps */                                                                \
    X(TextLogicHandlers) /* the message codes' logic pass */                                                           \
    X(TextLayoutHandlers) /* the message codes' layout pass */                                                         \
    /* ---- the effects */                                                                                             \
    X(HitSparkStyles) /* the hit spark's styles (8 bytes) */                                                           \
    X(HurtSparksVariants) /* the hurt sparks' variants (8 bytes) */                                                    \
    X(BusterShotColours) /* the buster shot's colours by size >> 1 */                                                  \
    X(BurstColours) /* the burst's colours */                                                                          \
    X(PickupColours) /* the pickups' colour pairs */                                                                   \
    X(PickupModels) /* the pickups' models (pointers to s16 x / y / z / pad vertices) */                               \
    X(BladeTrailColours) /* the Blade Arm's trail colours */                                                           \
    /* ---- the crash-landing storm (content/intro) */                                                                 \
    X(CloudTextures) /* the cloud texture records (12 bytes) */                                                        \
    X(PuffRingHeights)                                                                                                 \
    X(PuffRingRadii)                                                                                                   \
    X(PuffSizes)                                                                                                       \
    X(PuffU)                                                                                                           \
    X(PuffRingClutX)                                                                                                   \
    X(SeaPanoramaRows)                                                                                                 \
    X(SkyPanoramaRows)                                                                                                 \
    /* ---- the fire mission (content/intro) */                                                                        \
    X(FireFrames) /* a fire size's sprite frames */                                                                    \
    X(FireSizes) /* the fires' sizes */                                                                                \
    X(FireTongueFrames) /* the flame tongues' frames */                                                                \
    X(FireTongueStyles) /* the flame tongues' sprite styles */                                                         \
    X(FireSmokeKeys) /* the effects' key scripts by kind */                                                            \
    X(FireBlastEmberKeys)                                                                                              \
    X(FireDebrisKeys)                                                                                                  \
    X(FireEmberKeys)                                                                                                   \
    X(FireSmokeStyles) /* the effects' sprite styles by kind */                                                        \
    X(FireBlastEmberStyles)                                                                                            \
    X(FireDebrisStyles)                                                                                                \
    X(FireEmberStyles)                                                                                                 \
    /* ---- the opening's effects (content/intro): the sprites' frames */                                             \
    X(EngineFlameFrames)                                                                                               \
    X(StreakFrames)                                                                                                    \
    X(LiftStreakFrames)                                                                                                \
    /* ---- game/birdbot_down */                                                                                       \
    X(BirdbotDownBox)                                                                                                  \
    X(BirdbotDownLiftBox)                                                                                              \
    X(BirdbotDownCarryCries)                                                                                           \
    /* ---- game/birdbot_soldier */                                                                                    \
    X(BirdbotSoldierShootRanges)                                                                                       \
    X(BirdbotSoldierButtRanges)                                                                                        \
    X(BirdbotSoldierBox)                                                                                               \
    X(BirdbotSoldierLiftBox)                                                                                           \
    X(BirdbotSoldierCarryCries)                                                                                        \
    /* ---- game/bomb */                                                                                               \
    X(BombCollisionBox)                                                                                                \
    X(BombLiftBox)                                                                                                     \
    /* ---- game/door_actor */                                                                                         \
    X(DoorActorCollisionBox)                                                                                           \
    X(DoorActorTalkBoxYaw0)                                                                                            \
    X(DoorActorTalkBoxYawQuarter)                                                                                      \
    X(DoorActorTalkBoxYawHalf)                                                                                         \
    X(DoorActorTalkBoxYawOther)                                                                                        \
    /* ---- game/door_leaves */                                                                                        \
    X(DoorLeavesHingedLeafSigns)                                                                                       \
    X(DoorLeavesDoubleLeafSigns)                                                                                       \
    X(DoorLeavesSlidingLeafSigns)                                                                                      \
    /* ---- game/dust_puff */                                                                                          \
    X(DustPuffGroundProbeBox)                                                                                          \
    /* ---- game/encounter */                                                                                          \
    X(EncounterFieldStageCells)                                                                                        \
    X(EncounterFieldOdds)                                                                                              \
    X(EncounterWaveEndOdds)                                                                                            \
    /* ---- game/enemy_shot */                                                                                         \
    X(EnemyShotBox)                                                                                                    \
    /* ---- game/field_hoverbot */                                                                                     \
    X(FieldHoverbotHoverModels)                                                                                        \
    X(FieldHoverbotCircleTicks)                                                                                        \
    X(FieldHoverbotHoverBox)                                                                                           \
    X(FieldHoverbotHoverLiftBox)                                                                                       \
    /* ---- game/field_reaverbot */                                                                                    \
    X(FieldReaverbotModels)                                                                                            \
    X(FieldReaverbotAttackBits)                                                                                        \
    X(FieldReaverbotBehaviours)                                                                                        \
    X(FieldReaverbotNextSubs)                                                                                          \
    X(FieldReaverbotWallTurnRates)                                                                                     \
    X(FieldReaverbotHomeTurnRates)                                                                                     \
    X(FieldReaverbotAttackCooldowns)                                                                                   \
    X(FieldReaverbotBox)                                                                                               \
    X(FieldReaverbotLiftBox)                                                                                           \
    /* ---- game/floating_device */                                                                                    \
    X(FloatingDeviceCollisionBox)                                                                                      \
    X(FloatingDeviceTalkBox)                                                                                           \
    /* ---- game/footstep_dust */                                                                                      \
    X(FootstepDustFrameU)                                                                                              \
    /* ---- game/guardian_reaverbot */                                                                                 \
    X(GuardianReaverbotModels)                                                                                         \
    X(GuardianReaverbotTurnChoices)                                                                                    \
    X(GuardianReaverbotWalkLoopsFirst)                                                                                 \
    X(GuardianReaverbotWalkLoopsSecond)                                                                                \
    X(GuardianReaverbotAttackCooldowns)                                                                                \
    X(GuardianReaverbotSparkBones)                                                                                     \
    X(GuardianReaverbotCollisionBox)                                                                                   \
    X(GuardianReaverbotGlowOffsets)                                                                                    \
    /* ---- game/hud */                                                                                                \
    X(HudCluts)                                                                                                        \
    X(HudParts)                                                                                                        \
    X(HudSlides)                                                                                                       \
    X(HudStyles)                                                                                                       \
    X(HudBannerKinds)                                                                                                  \
    X(HudBannerLayouts)                                                                                                \
    /* ---- game/input_bindings */                                                                                     \
    X(InputBindingsPadDefaults)                                                                                        \
    X(InputBindingsMouseDefaults)                                                                                      \
    /* ---- game/key_config */                                                                                         \
    X(KeyConfigPadActions)                                                                                             \
    X(KeyConfigActions)                                                                                                \
    X(KeyConfigMouseActions)                                                                                           \
    X(KeyConfigPadButtonArts)                                                                                          \
    X(KeyConfigMouseButtonArts)                                                                                        \
    /* ---- game/lift_box */                                                                                           \
    X(LiftBoxCollisionBox)                                                                                             \
    X(LiftBoxBox)                                                                                                      \
    X(LiftBoxHeightRanges)                                                                                             \
    /* ---- game/lobbed_shot */                                                                                        \
    X(LobbedShotBox)                                                                                                   \
    X(LobbedShotSpriteUv)                                                                                              \
    X(LobbedShotSpriteCluts)                                                                                           \
    X(LobbedShotSpriteTpages)                                                                                          \
    /* ---- game/pickups */                                                                                            \
    X(PickupsLaunchSpreadYaws)                                                                                         \
    X(PickupsZennyAmounts)                                                                                             \
    X(PickupsEnergyAmounts)                                                                                            \
    X(PickupsLifeAmounts)                                                                                              \
    X(PickupsPickupBox)                                                                                                \
    /* ---- game/placed_models */                                                                                      \
    X(PlacedModelsSteps)                                                                                               \
    X(PlacedModelsTurningSteps)                                                                                        \
    /* ---- game/player/player_input */                                                                                \
    X(PlayerInputDefaultButtonMap)                                                                                     \
    /* ---- game/player/player_locomotion */                                                                           \
    X(PlayerLocomotionRunStartAnims)                                                                                   \
    /* ---- game/player/player_status */                                                                               \
    X(PlayerStatusOrbitMarkerPhases)                                                                                   \
    /* ---- game/reaverbot_turret */                                                                                   \
    X(ReaverbotTurretWalkRanges)                                                                                       \
    X(ReaverbotTurretModels)                                                                                           \
    X(ReaverbotTurretBox)                                                                                              \
    X(ReaverbotTurretMeleeBoxes)                                                                                       \
    X(ReaverbotTurretRangeBoxes)                                                                                       \
    /* ---- game/reaverbot_walker */                                                                                   \
    X(ReaverbotWalkerModels)                                                                                           \
    X(ReaverbotWalkerBehaviours)                                                                                       \
    X(ReaverbotWalkerFirstSubs)                                                                                        \
    X(ReaverbotWalkerNextSubs)                                                                                         \
    X(ReaverbotWalkerAttackBits)                                                                                       \
    X(ReaverbotWalkerWallTurnRates)                                                                                    \
    X(ReaverbotWalkerHomeTurnRates)                                                                                    \
    X(ReaverbotWalkerAttackCooldowns)                                                                                  \
    X(ReaverbotWalkerBox)                                                                                              \
    X(ReaverbotWalkerLiftBox)                                                                                          \
    /* ---- game/save_image */                                                                                         \
    X(SaveImageIconClut)                                                                                               \
    X(SaveImageIconBitmap)                                                                                             \
    X(SaveImagePlaces)                                                                                                 \
    X(SaveImageTitle)                                                                                                  \
    X(SaveImagePlaceNameTitle)                                                                                         \
    /* ---- game/servbot */                                                                                            \
    X(ServbotPauseTicks)                                                                                               \
    X(ServbotFidgetLooks)                                                                                              \
    X(ServbotFidgetLookTicks)                                                                                          \
    X(ServbotFidgetPatterns)                                                                                           \
    X(ServbotDazedTicks)                                                                                               \
    X(ServbotDownTicks)                                                                                                \
    X(ServbotCollisionBox)                                                                                             \
    X(ServbotTalkBox)                                                                                                  \
    X(ServbotSmallTalkBox)                                                                                             \
    /* ---- game/shop */                                                                                               \
    X(ShopLines)                                                                                                       \
    X(ShopItemPrices)                                                                                                  \
    X(ShopLifePrices)                                                                                                  \
    X(ShopUpgradeOnePrices)                                                                                            \
    X(ShopUpgradeTwoPrices)                                                                                            \
    X(ShopSellableParts)                                                                                               \
    X(ShopIconArts)                                                                                                    \
    X(ShopHandBob)                                                                                                     \
    X(MessageHandBob)                                                                                                  \
    X(ShopPages)                                                                                                       \
    X(ShopStoryStockA)                                                                                                 \
    X(ShopStoryStockB)                                                                                                 \
    /* ---- game/shots/blade_arm */                                                                                    \
    X(BladeArmBox)                                                                                                     \
    /* ---- game/shots/buster */                                                                                       \
    X(BusterBox)                                                                                                       \
    X(BusterKickBox)                                                                                                   \
    /* ---- game/shots/charge */                                                                                       \
    X(ChargeColours)                                                                                                   \
    X(ChargeWaveBox)                                                                                                   \
    /* ---- game/shots/crusher */                                                                                      \
    X(CrusherOrbBox)                                                                                                   \
    X(CrusherBox)                                                                                                      \
    /* ---- game/shots/drill_arm */                                                                                    \
    X(DrillArmBox)                                                                                                     \
    /* ---- game/shots/extinguisher */                                                                                 \
    X(ExtinguisherBox)                                                                                                 \
    /* ---- game/shots/ground_crawler */                                                                               \
    X(GroundCrawlerBox)                                                                                                \
    /* ---- game/shots/homing_missile */                                                                               \
    X(HomingMissileBox)                                                                                                \
    /* ---- game/shots/hunter_seeker */                                                                                \
    X(HunterSeekerRanges)                                                                                              \
    X(HunterSeekerCrystalColours)                                                                                      \
    X(HunterSeekerCrystalSizes)                                                                                        \
    X(HunterSeekerBox)                                                                                                 \
    X(HunterSeekerCrystalBox)                                                                                          \
    /* ---- game/shots/hyper_shell */                                                                                  \
    X(HyperShellBox)                                                                                                   \
    /* ---- game/shots/machine_gun */                                                                                  \
    X(MachineGunCasingBox)                                                                                             \
    X(MachineGunBox)                                                                                                   \
    /* ---- game/shots/reflector */                                                                                    \
    X(ReflectorBox)                                                                                                    \
    /* ---- game/shots/shining_laser */                                                                                \
    X(ShiningLaserBox)                                                                                                 \
    /* ---- game/shots/spread_buster */                                                                                \
    X(SpreadBusterBox)                                                                                                 \
    /* ---- game/sign */                                                                                               \
    X(SignCollisionBox)                                                                                                \
    X(SignTalkBox)                                                                                                     \
    /* ---- game/snow_reaverbot */                                                                                     \
    X(SnowReaverbotWakeRanges)                                                                                         \
    X(SnowReaverbotBox)                                                                                                \
    /* ---- game/spark_emitter */                                                                                      \
    X(SparkEmitterSpriteSizes)                                                                                         \
    /* ---- game/status_controls */                                                                                    \
    X(StatusControlsItemIconArts)                                                                                      \
    X(StatusPageFrames)                                                                                                \
    X(StatusKeyItemEntries)                                                                                            \
    X(StatusUsableItemEntries)                                                                                         \
    X(StatusPageTables)                                                                                                \
    /* ---- game/status_equipment */                                                                                   \
    /* the row tables of the page's levels */                                                                       \
    X(StatusEquipmentListBases)                                                                                        \
    X(StatusEquipmentListEnds)                                                                                         \
    X(StatusEquipmentEquipRowStates)                                                                                   \
    X(StatusEquipmentWeaponRowSubs)                                                                                    \
    X(StatusEquipmentBodyRowSubs)                                                                                      \
    X(StatusEquipmentBusterRowSubs)                                                                                    \
    X(StatusEquipmentBusterSlotSteps)                                                                                  \
    X(StatusEquipmentBusterSortSteps)                                                                                  \
    X(StatusEquipmentArmourHitHeights)                                                                                 \
    X(StatusEquipmentPartBonuses)                                                                                      \
    X(StatusEquipmentBoxPlaces)                                                                                        \
    X(StatusEquipmentBoxTabs)                                                                                          \
    X(StatusEquipmentStatCaptions)                                                                                     \
    /* ---- game/status_items */                                                                                       \
    X(StatusItemsItemRowStates)                                                                                        \
    X(StatusItemsItemRowLists)                                                                                         \
    X(StatusItemsCountText)                                                                                            \
    /* ---- game/status_markers */                                                                                     \
    X(StatusMarkersBoltBones)                                                                                          \
    X(StatusMarkersSparkBones)                                                                                         \
    X(StatusDrainBoneRows)                                                                                             \
    /* ---- game/status_options */                                                                                     \
    X(StatusOptionsLockOnRows)                                                                                         \
    X(StatusOptionsLockOnModes)                                                                                        \
    X(StatusOptionsVolumeRows)                                                                                         \
    /* ---- game/status_screen */                                                                                      \
    /* the sub-screen's pool-0x20 class table (the field's is kept in StageClassTables::fieldAnimated while         \
       it runs) */                                                                                                  \
    X(StatusScreenPanelPages)                                                                                          \
    X(StatusScreenPanelRows)                                                                                           \
    /* ---- game/tank_shell */                                                                                         \
    X(TankShellBox)                                                                                                    \
    /* ---- game/text_script */                                                                                        \
    X(TextScriptKeyNameButtons)                                                                                        \
    X(TextScriptNameGlyphs)                                                                                            \
    /* ---- game/throwable */                                                                                          \
    X(ThrowableKinds)                                                                                                  \
    X(ThrowableLiftBox)                                                                                                \
    X(ThrowableCarryOffsets)                                                                                           \
    X(ThrowableBoxes)                                                                                                  \
    /* ---- game/town_npc */                                                                                           \
    X(TownNpcFidgetLooks)                                                                                              \
    X(TownNpcFidgetLookTicks)                                                                                          \
    X(TownNpcFidgetPatterns)                                                                                           \
    X(TownNpcCollisionBox)                                                                                             \
    X(TownNpcTalkBox)                                                                                                  \
    X(TownNpcSmallTalkBox)                                                                                             \
    X(TownNpcSpeeds)                                                                                                   \
    /* ---- game/transition */                                                                                         \
    X(TransitionStageClasses)                                                                                          \
    X(St2dRoomHandlers)                                                                                                \
    X(St2eRoomHandlers)                                                                                                \
    X(St4eRoomHandlers)                                                                                                \
    X(St51RoomHandlers)                                                                                                \
    /* ---- game/travel */                                                                                             \
    /* the script's camera and step lists, its two icon records (exe data) */                                       \
    X(TravelStageDocks)                                                                                                \
    X(TravelIconBanks)                                                                                                 \
    X(TravelMaps)                                                                                                      \
    X(TravelPicks)                                                                                                     \
    /* ---- game/treasure_chest */                                                                                     \
    X(TreasureChestCollisionBox)                                                                                       \
    X(TreasureChestTalkBox)                                                                                            \
    X(TreasureChestContents)                                                                                           \
    /* ---- game/turret_bullet */                                                                                      \
    X(TurretBulletSizes)                                                                                               \
    X(TurretBulletTexels)                                                                                              \
    X(TurretBulletClasses)                                                                                             \
    /* ---- game/walker_shot */                                                                                        \
    X(WalkerShotBox)                                                                                                   \
    /* ---- content/calbania/appo_dah_b */                                                                             \
    X(AppoDahBGateX)                                                                                                   \
    X(AppoDahBGateZ)                                                                                                   \
    X(AppoDahBScript1fX)                                                                                               \
    X(AppoDahBScript1fZ)                                                                                               \
    X(AppoDahScript1dRoutines)                                                                                         \
    X(AppoDahScript1fRoutines)                                                                                         \
    /* ---- content/calbania/calbania_cast */                                                                          \
    X(CalbaniaCastWanderTicks)                                                                                         \
    X(CalbaniaCastFleeTicks)                                                                                           \
    X(CalbaniaCastIdleTicks)                                                                                           \
    /* ---- content/calbania/calbania_rooms */                                                                         \
    X(CalbaniaRoomsSt1fGateCells)                                                                                      \
    X(CalbaniaRoomsSt24FirstExit)                                                                                      \
    X(CalbaniaRoomsSt24Room2Exit)                                                                                      \
    /* ---- content/calbania/carried_npc */                                                                            \
    X(CarriedNpcCastAnims)                                                                                             \
    X(CarriedNpcWorkerAnims)                                                                                           \
    X(CarriedNpcWanderPoints)                                                                                          \
    X(CarriedNpcBox)                                                                                                   \
    X(CarriedNpcLiftBox)                                                                                               \
    /* ---- content/calbania/glyde_base_gun */                                                                         \
    X(GlydeBaseGunFirstSubByVariant)                                                                                   \
    /* ---- content/calbania/glyde_base_key */                                                                         \
    X(GlydeBaseKeyFlags)                                                                                               \
    /* ---- content/calbania/glyde_base_rooms */                                                                       \
    X(GlydeBaseRoomsWalkGridStart)                                                                                     \
    X(GlydeBaseRoomsBoxVariants)                                                                                       \
    X(GlydeBaseRoomsWalkerVariants)                                                                                    \
    X(GlydeBaseRoomsGatePlaces)                                                                                        \
    X(GlydeBaseRoomsBoxPlaces)                                                                                         \
    X(GlydeBaseRoomsWalkerPlaces)                                                                                      \
    X(GlydeBaseRoomsBoxCells)                                                                                          \
    X(GlydeBaseRoomsCellBirdbotPlaces)                                                                                 \
    /* ---- content/calbania/glyde_base_scene_birdbots */                                                              \
    X(GlydeBaseSceneBirdbotsModels)                                                                                    \
    X(GlydeBaseSceneBirdbotsCellModels)                                                                                \
    X(GlydeBaseSceneBirdbotsCellFirstAnims)                                                                            \
    X(GlydeBaseSceneBirdbotsBlinkPairs)                                                                                \
    /* ---- content/calbania/glyde_base_tank */                                                                        \
    X(GlydeBaseTankAimTicksTable)                                                                                      \
    X(GlydeBaseTankTurnRateTable)                                                                                      \
    X(GlydeBaseTankBurstLengthTable)                                                                                   \
    X(GlydeBaseTankPauseTicksTable)                                                                                    \
    X(GlydeBaseTankBurstTicksTable)                                                                                    \
    X(GlydeBaseTankQuadrantCells)                                                                                      \
    /* ---- content/calbania/minimaps */                                                                               \
    X(CalbaniaSquareMinimapPictures)                                                                                   \
    X(CalbaniaSquareMinimapModes)                                                                                      \
    X(PokteMinimapPictures)                                                                                            \
    /* ---- content/calbania/square_defence_scripts */                                                                 \
    X(SquareDefenceScriptsLiftHeights)                                                                                 \
    /* ---- content/calbania/st1f_birdbot_tank */                                                                      \
    X(St1fBirdbotTankPauseTicksTable)                                                                                  \
    X(St1fBirdbotTankBurstTicksTable)                                                                                  \
    X(St1fBirdbotTankAimTicksTable)                                                                                    \
    X(St1fBirdbotTankTurnRateTable)                                                                                    \
    X(St1fBirdbotTankQuadrantCells)                                                                                    \
    /* ---- content/calbania/st22_burrower */                                                                          \
    X(St22BurrowerModels)                                                                                              \
    /* ---- content/calbania/st24_npc */                                                                               \
    X(St24NpcPauseTicks)                                                                                               \
    X(St24NpcFidgetLooks)                                                                                              \
    X(St24NpcFidgetLookTicks)                                                                                          \
    X(St24NpcFidgetPatterns)                                                                                           \
    /* ---- content/calbania/st24_structure */                                                                         \
    X(St24StructureBox)                                                                                                \
    /* ---- content/calbania/st32_quiz */                                                                              \
    X(St32QuizCourse0Questions)                                                                                        \
    X(St32QuizCourse1Questions)                                                                                        \
    X(St32QuizCourse2Questions)                                                                                        \
    X(St32QuizCourse3Questions)                                                                                        \
    X(St32QuizQuestionLines)                                                                                           \
    X(St32QuizAnswers)                                                                                                 \
    X(St32QuizRankFlags)                                                                                               \
    /* ---- content/calbania/talking_npc */                                                                            \
    X(TalkingNpcTalkerCollisionBox)                                                                                    \
    X(TalkingNpcTalkerTalkBox)                                                                                         \
    X(TalkingNpcTwinCollisionBox)                                                                                      \
    X(TalkingNpcTwinTalkBox)                                                                                           \
    /* ---- content/forbidden_island/minimaps */                                                                       \
    X(St10MinimapPictures)                                                                                             \
    X(St3EMinimapOffsets)                                                                                              \
    X(St3EMinimapSpriteLists)                                                                                          \
    /* ---- content/forbidden_island/st3e_reports */                                                                   \
    X(St3eReportsFloatingModelBox)                                                                                     \
    /* ---- content/forbidden_island/st11_script2a */                                                                  \
    X(St11ArrivalFlashTickCases)                                                                                       \
    X(St11ArrivalCastTickCases)                                                                                        \
    /* ---- content/intro/burning_data */                                                                              \
    X(BurningDataAnimCells)                                                                                            \
    X(BurningDataBox)                                                                                                  \
    X(BurningDataHome)                                                                                                 \
    X(BurningDataPaths)                                                                                                \
    X(BurningDataSceneSubTables)                                                                                       \
    /* ---- content/intro/crash_landing */                                                                             \
    X(CrashLandingSkyShipBox)                                                                                          \
    X(CrashLandingDropperBox)                                                                                          \
    X(CrashLandingRiderBox)                                                                                            \
    X(CrashLandingCloudRoutines)                                                                                       \
    X(CrashLandingPieceRoutines)                                                                                       \
    X(CrashLandingCraftVariants)                                                                                       \
    /* ---- content/intro/flutter_fire */                                                                              \
    X(FlutterFireTongueHeights)                                                                                        \
    X(FlutterFireTongueRise)                                                                                           \
    X(FlutterFireEmitPeriods)                                                                                          \
    X(FlutterFireSmokeSizes)                                                                                           \
    X(FlutterFireSmokeRise)                                                                                            \
    X(FlutterFireBlastEmberSizes)                                                                                      \
    X(FlutterFireDebrisSizes)                                                                                          \
    X(FlutterFireEmberSizes)                                                                                           \
    X(FlutterFireBlastEmberBox)                                                                                        \
    X(FlutterFireEmberBox)                                                                                             \
    X(FlutterFireEmberTarget)                                                                                          \
    /* ---- content/intro/opening_cast */                                                                              \
    X(OpeningCastFlutterBox)                                                                                           \
    X(OpeningCastScriptAnimatedBox)                                                                                    \
    X(OpeningCastCrewBox)                                                                                              \
    X(OpeningCastTalkerBox)                                                                                            \
    X(OpeningCastStreakPoints)                                                                                         \
    X(OpeningCastCrewVariants)                                                                                         \
    X(OpeningCastTalkerVariants)                                                                                       \
    /* ---- content/intro/opening_script */                                                                            \
    X(OpeningScriptChoiceVoices)                                                                                       \
    X(OpeningScriptStates)                                                                                             \
    /* ---- content/intro/st03_attract_cast */                                                                         \
    X(St03AttractCastHopHeights)                                                                                       \
    X(St03AttractCastBlinkSequence)                                                                                    \
    X(St03AttractCastCarrierVariants)                                                                                  \
    X(St03AttractCastWalkerVariants)                                                                                   \
    X(St03AttractCastFlutterVariants)                                                                                  \
    X(St03AttractCastPlacedModelVariants)                                                                              \
    X(St03AttractCastRollVariants)                                                                                     \
    X(St03AttractCastBarrelVariants)                                                                                   \
    X(St03AttractCastWatcherVariants)                                                                                  \
    X(St03AttractCastCraftBodyVariants)                                                                                \
    X(St03AttractCastBlinkerVariants)                                                                                  \
    X(St03AttractCastStanderVariants)                                                                                  \
    X(St03AttractCastPartBlinkerVariants)                                                                              \
    X(St03AttractCastTalkerVariants)                                                                                   \
    X(St03AttractCastFaceVariants)                                                                                     \
    X(St03AttractCastSpeakerVariants)                                                                                  \
    X(St03AttractCastScreenVariants)                                                                                   \
    X(St03AttractCastCraftVariants)                                                                                    \
    X(St03SceneryKinds)                                                                                                \
    /* ---- content/late_game/minimaps */                                                                              \
    X(St40MinimapPictures)                                                                                             \
    X(St43MinimapVisitedBases)                                                                                         \
    X(St43MinimapAreas)                                                                                                \
    X(St59MinimapPictures)                                                                                             \
    /* ---- content/late_game/st40_fx */                                                                               \
    X(St40FxPanelQuadPoints)                                                                                           \
    /* ---- content/late_game/st43_roller */                                                                           \
    X(St43RollerSpeeds)                                                                                                \
    X(St43RollerTurnSteps)                                                                                             \
    /* ---- content/late_game/st4c_story */                                                                            \
    X(St4cStoryDefeatFadeThresholds)                                                                                   \
    X(St4cStoryIntroStates)                                                                                            \
    X(St4cStoryOutroStates)                                                                                            \
    /* ---- content/late_game/st53_story */                                                                            \
    X(St53StoryBoneFadeThresholds)                                                                                     \
    X(St53StorySnowTrackStates)                                                                                        \
    X(St53StoryRideStates)                                                                                             \
    /* ---- content/late_game/st54_cast */                                                                             \
    X(St54CastRingRadii)                                                                                               \
    /* ---- content/late_game/st54_puffs */                                                                            \
    X(St54PuffsSmokeShades)                                                                                            \
    /* ---- content/late_game/st59 */                                                                                  \
    X(St59SpeedCellCodes)                                                                                              \
    X(St59CoursePars)                                                                                                  \
    X(St59CoursePrizes)                                                                                                \
    X(St59TimeJitter)                                                                                                  \
    X(St59CourseRecordCounts)                                                                                          \
    X(St59CourseStarts)                                                                                                \
    X(St59Checkpoints)                                                                                                 \
    /* ---- content/late_game/st5c */                                                                                  \
    X(St5cIntroFadeThresholds)                                                                                         \
    /* ---- content/late_game/st5c_fx */                                                                               \
    X(St5cFxBladeDiscAngles)                                                                                           \
    /* ---- content/late_game/st5c_fx2 */                                                                              \
    X(St5cFx2BlastColours)                                                                                             \
    X(St5cFx2SparkFrames)                                                                                              \
    /* ---- content/manda/exam_enemies_a */                                                                            \
    X(HopperThinkSubs)                                                                                                 \
    X(HopperWallTurnRates)                                                                                             \
    X(HopperThrowSpreads)                                                                                              \
    X(GunnerModels)                                                                                                    \
    X(GunnerIdleAnims)                                                                                                 \
    X(GunnerIdleStartKeys)                                                                                             \
    X(HopperBox)                                                                                                       \
    X(HopperLiftBox)                                                                                                   \
    X(TumblerBox)                                                                                                      \
    X(TumblerLiftBox)                                                                                                  \
    X(GunnerBox)                                                                                                       \
    X(GunnerLiftBox)                                                                                                   \
    X(GunnerShotBox)                                                                                                   \
    X(ShieldBox)                                                                                                       \
    /* ---- content/manda/exam_enemies_b */                                                                            \
    X(ExamEnemiesBDiggerModels)                                                                                        \
    X(ExamEnemiesBDiggerStartOffsets)                                                                                  \
    X(ExamEnemiesBDiggerWalkOffsets)                                                                                   \
    X(ExamEnemiesBDiggerSwipeOffsets)                                                                                  \
    X(ExamEnemiesBDiggerSurfaceAngles)                                                                                 \
    X(ExamEnemiesBDroneBox)                                                                                            \
    X(ExamEnemiesBHatchBox)                                                                                            \
    X(ExamEnemiesBMineBox)                                                                                             \
    X(ExamEnemiesBDiggerBox)                                                                                           \
    X(ExamEnemiesBFlyerBox)                                                                                            \
    X(ExamEnemiesBFlyerLiftBox)                                                                                        \
    X(ExamAmbushLists)                                                                                                 \
    X(ExamAmbushBoxes)                                                                                                 \
    /* ---- content/manda/exam_spawner */                                                                              \
    X(ExamSpawnerDigitX)                                                                                               \
    X(ExamSpawnerPlates)                                                                                               \
    /* ---- content/manda/hopping_drone */                                                                             \
    X(HoppingDroneBox)                                                                                                 \
    X(HoppingDroneLiftBox)                                                                                             \
    /* ---- content/manda/manda_boss */                                                                                \
    X(MandaBossPerchYaws)                                                                                              \
    X(MandaBossBeamWidths)                                                                                             \
    X(MandaBossShortBeamWidths)                                                                                        \
    X(MandaBossPuppetFadeThresholds)                                                                                   \
    X(MandaBossRisingPuppetBox)                                                                                        \
    X(MandaBossDroppingPuppetBox)                                                                                      \
    X(MandaBossDebrisBox)                                                                                              \
    X(MandaBossPerches)                                                                                                \
    /* ---- content/manda/manda_guardian */                                                                            \
    X(MandaGuardianCloudShades)                                                                                        \
    X(MandaGuardianVolleyCounts)                                                                                       \
    X(MandaGuardianCloudPhaseSteps)                                                                                    \
    X(MandaGuardianShotBox)                                                                                            \
    X(MandaGuardianHopperBox)                                                                                          \
    X(MandaGuardianHopperLiftBox)                                                                                      \
    X(MandaGuardianBladeBox)                                                                                           \
    X(MandaGuardianFallingBladeBox)                                                                                    \
    X(MandaGuardianChestBox)                                                                                           \
    X(MandaGuardianFightFade)                                                                                          \
    X(MandaGuardianCastFade)                                                                                           \
    X(MandaGuardianOutroFade)                                                                                          \
    X(MandaGuardianCast2Fade)                                                                                          \
    X(MandaGuardianFightBox)                                                                                           \
    X(MandaGuardianCastBox)                                                                                            \
    X(MandaGuardianOutroBox)                                                                                           \
    X(MandaGuardianCast2Box)                                                                                           \
    X(MandaGuardianHandOffsets)                                                                                        \
    X(MandaGuardianFallingBladeModel)                                                                                  \
    /* ---- content/manda/manda_ruin_burrower */                                                                       \
    X(MandaRuinBurrowerBox)                                                                                            \
    X(MandaRuinBurrowerLiftBox)                                                                                        \
    /* ---- content/manda/manda_ruin_props */                                                                          \
    X(MandaRuinPropsPlatformBox)                                                                                       \
    X(MandaRuinPropsPlatformLiftBox)                                                                                   \
    X(MandaRuinPropsGateBox)                                                                                           \
    /* ---- content/manda/manda_ruin_scripts */                                                                        \
    X(MandaRuinScriptsGroundedFigureBox)                                                                               \
    X(MandaRuinScriptsBurningWreckBox)                                                                                 \
    X(MandaRuinScriptsKeyItemUseBox)                                                                                   \
    /* ---- content/manda/minimaps */                                                                                  \
    X(St12MinimapVisitedBases)                                                                                         \
    X(St12MinimapAreas)                                                                                                \
    X(St12MinimapDots)                                                                                                 \
    /* ---- content/manda/pokte_rooms */                                                                               \
    X(PokteRoomsFirstHouseRewards)                                                                                     \
    X(PokteRoomsSecondHouseRewards)                                                                                    \
    X(PokteRoomsHousesDamaged)                                                                                         \
    X(PokteRoomsHousesRuined)                                                                                          \
    X(PokteRoomsHouseRepairs)                                                                                          \
    /* ---- content/manda/ruin_spawners */                                                                             \
    X(RuinSpawnersSt12DropDistances)                                                                                   \
    X(RuinSpawnersHopperRefillDelays)                                                                                  \
    X(RuinSpawnersSt13DropDistances)                                                                                   \
    X(RuinSpawnersSt12RespawnPoints)                                                                                   \
    X(RuinSpawnersEmitterPoints)                                                                                       \
    X(RuinSpawnersEarlyDroneEntries)                                                                                   \
    X(RuinSpawnersLateDroneEntries)                                                                                    \
    X(RuinSpawnersHopperPoints)                                                                                        \
    X(RuinSpawnersSt14RespawnPointsA)                                                                                  \
    X(RuinSpawnersSt14RespawnPointsB)                                                                                  \
    /* ---- content/manda/st12_14 */                                                                                   \
    X(St1214TumbleStartFlags)                                                                                          \
    X(St1214BoulderBox)                                                                                                \
    X(St1214TumbleBox)                                                                                                 \
    X(St1214WalkerBox)                                                                                                 \
    X(St1214WalkerLiftBox)                                                                                             \
    X(St1214BoulderRows)                                                                                               \
    /* ---- content/manda/st14_enemy */                                                                                \
    X(St14EnemyHoverModels)                                                                                            \
    X(St14EnemyNoticeRanges)                                                                                           \
    X(St14EnemyHoverPauses)                                                                                            \
    X(St14EnemyHoverBox)                                                                                               \
    X(St14EnemyHoverLiftBox)                                                                                           \
    /* ---- content/manda/st24_ruin_gate */                                                                            \
    X(St24RuinGateFadeThresholds)                                                                                      \
    X(St24RuinGateGuardianBox)                                                                                         \
    /* ---- content/manda/st25_tron_scenes */                                                                          \
    X(St25TronScenesBox)                                                                                               \
    X(St25TronScenesTankBox)                                                                                           \
    X(St25TronScenesWreckBox)                                                                                          \
    X(St25TronScenesFigureBox)                                                                                         \
    /* ---- content/nino/bars_and_gate */                                                                              \
    X(BarsAndGateSt16BarModels)                                                                                        \
    X(BarsAndGateWaveParams)                                                                                           \
    X(BarsAndGateSt16BarBox)                                                                                           \
    X(BarsAndGateSt16BarGrabBox)                                                                                       \
    X(BarsAndGateSt17BarBox)                                                                                           \
    X(BarsAndGateSt17LeverGrabBox)                                                                                     \
    X(BarsAndGateSt17LiftBox)                                                                                          \
    X(BarsAndGateBox)                                                                                                  \
    /* ---- content/nino/blade_trap */                                                                                 \
    X(BladeTrapClosedBox)                                                                                              \
    X(BladeTrapArmedBox)                                                                                               \
    /* ---- content/nino/carrier_boat */                                                                               \
    X(CarrierBoatSinkingSpread)                                                                                        \
    X(CarrierBoatWalkerTicks)                                                                                          \
    X(CarrierBoatBox)                                                                                                  \
    X(CarrierBoatGuns)                                                                                                 \
    /* ---- content/nino/ch2_field_cast */                                                                             \
    X(Ch2FieldCastHoverTargetBox)                                                                                      \
    X(Ch2FieldCastHoverDrifterBox)                                                                                     \
    X(Ch2FieldCastCrashCarrierBox)                                                                                     \
    X(Ch2FieldCastCrashCarrierFallBox)                                                                                 \
    X(Ch2FieldCastScriptedWalkerBox)                                                                                   \
    /* ---- content/nino/ch2_script_cast */                                                                            \
    X(Ch2ScriptCastTakeOffCastBox)                                                                                     \
    X(Ch2ScriptCastScript32CastBox)                                                                                    \
    X(Ch2ScriptCastScript37CastBox)                                                                                    \
    /* ---- content/nino/field_enemies */                                                                              \
    X(FieldEnemiesStalkTurns)                                                                                          \
    X(FieldEnemiesStalkTicks)                                                                                          \
    X(FieldEnemiesCritterBox)                                                                                          \
    X(FieldEnemiesCritterLiftBox)                                                                                      \
    X(FieldEnemiesCarrierBox)                                                                                          \
    X(FieldEnemiesCarrierFallBox)                                                                                      \
    /* ---- content/nino/field_spawners */                                                                             \
    X(FieldSpawnersEscortTargets)                                                                                      \
    X(FieldSpawnersSt16PreDefeated)                                                                                    \
    X(FieldSpawnersSt18GunshipVariants)                                                                                \
    X(FieldSpawnersRepairRates)                                                                                        \
    X(FieldSpawnersRepairDigitX)                                                                                       \
    X(FieldSpawnersBoxPoints)                                                                                          \
    X(FieldSpawnersSt18FirstBirdbots)                                                                                  \
    X(FieldSpawnersSt18ReturnPoints)                                                                                   \
    X(FieldSpawnersWalkerStarts)                                                                                       \
    X(FieldSpawnersWalkerSets)                                                                                         \
    X(FieldSpawnersWalkerRoutes)                                                                                       \
    X(FieldSpawnersRoutePoints)                                                                                        \
    X(FieldSpawnersRepairPlates)                                                                                       \
    X(FieldSpawnersWavePoints)                                                                                         \
    X(FieldSpawnersCarrierPoints)                                                                                      \
    X(FieldSpawnersSt18GunshipPoints)                                                                                  \
    /* ---- content/nino/fleeing_reaverbot */                                                                          \
    X(FleeingReaverbotBox)                                                                                             \
    X(FleeingReaverbotLiftBox)                                                                                         \
    /* ---- content/nino/gauge_reaverbots */                                                                           \
    X(GaugeReaverbotsSwimBox)                                                                                          \
    X(GaugeReaverbotsStandBox)                                                                                         \
    X(GaugeReaverbotsMineBox)                                                                                          \
    X(GaugeReaverbotsCastBox)                                                                                          \
    /* ---- content/nino/glyde_attack_cast */                                                                          \
    X(GlydeAttackCastScript39BirdbotModels)                                                                            \
    X(GlydeAttackCastScript39BirdbotAnims)                                                                             \
    X(GlydeAttackCastScript3bBirdbotModels)                                                                            \
    X(GlydeAttackCastScript3bBirdbotAnims)                                                                             \
    X(GlydeAttackCastScript39BirdbotBox)                                                                               \
    X(GlydeAttackCastScript3bBirdbotBox)                                                                               \
    X(GlydeAttackCastDescenderBox)                                                                                     \
    X(GlydeAttackCastBlinkerBox)                                                                                       \
    X(GlydeAttackCastRiserBox)                                                                                         \
    X(GlydeAttackCastScript39BirdbotPairs)                                                                             \
    X(GlydeAttackCastHopYawStepLists)                                                                                  \
    /* ---- content/nino/glyde_attack_talkers */                                                                       \
    X(GlydeAttackTalkersSt18TalkerAnims)                                                                               \
    X(GlydeAttackTalkersSt17TalkerBox)                                                                                 \
    X(GlydeAttackTalkersSt18TalkerBox)                                                                                 \
    /* ---- content/nino/glyde_box_walkers */                                                                          \
    X(GlydeBoxWalkersWalkerModels)                                                                                     \
    X(GlydeBoxWalkersWalkerRoutes)                                                                                     \
    X(GlydeBoxWalkersWalkerBox)                                                                                        \
    X(GlydeBoxWalkersBombBox)                                                                                          \
    /* ---- content/nino/glyde_missiles */                                                                             \
    X(GlydeMissilesMissileVariantCycle)                                                                                \
    X(GlydeMissilesMissileShipBox)                                                                                     \
    X(GlydeMissilesMissileBox)                                                                                         \
    X(GlydeMissilesMissileLiftBox)                                                                                     \
    X(GlydeMissilesMachinePoints)                                                                                      \
    /* ---- content/nino/glyde_ship_arrival */                                                                         \
    X(GlydeShipArrivalBox)                                                                                             \
    /* ---- content/nino/glyde_ship_cast */                                                                            \
    X(GlydeShipCastFlyInAnims)                                                                                         \
    X(GlydeShipCastHoverShipAnims)                                                                                     \
    X(GlydeShipCastFlyInBox)                                                                                           \
    X(GlydeShipCastHoverShipBox)                                                                                       \
    /* ---- content/nino/guardian_cast */                                                                              \
    X(GuardianCastDissolveOrder)                                                                                       \
    X(GuardianCastFadingOrder)                                                                                         \
    /* ---- content/nino/homing_shot */                                                                                \
    X(HomingShotBox)                                                                                                   \
    /* ---- content/nino/minimaps */                                                                                   \
    X(St18MinimapCluts)                                                                                                \
    X(St18MinimapSpriteLists)                                                                                          \
    X(St18MinimapPatchedList)                                                                                          \
    X(St18MinimapExtraList)                                                                                            \
    X(St35MinimapVisitedBases)                                                                                         \
    X(St35MinimapAreas)                                                                                                \
    X(St35MinimapDots)                                                                                                 \
    /* ---- content/nino/mission_tank */                                                                               \
    X(MissionTankLeadShifts)                                                                                           \
    X(MissionTankFireTimers)                                                                                           \
    X(MissionTankShotPauses)                                                                                           \
    X(MissionTankBox)                                                                                                  \
    X(MissionTankSmokeOffsets)                                                                                         \
    /* ---- content/nino/mission_targets */                                                                            \
    X(MissionTargetsSmokeCells)                                                                                        \
    /* ---- content/nino/pedestal_item */                                                                              \
    X(PedestalItemTalkBox)                                                                                             \
    /* ---- content/nino/platform_boss */                                                                              \
    X(PlatformBossBoltZigZag)                                                                                          \
    X(PlatformBossBox)                                                                                                 \
    X(PlatformBossAimedShotBox)                                                                                        \
    X(PlatformBossOrbBox)                                                                                              \
    /* ---- content/nino/repair_machine */                                                                             \
    X(RepairMachineRollWorkLoops)                                                                                      \
    X(RepairMachineRollWorkAnims)                                                                                      \
    X(RepairMachinePartAnims)                                                                                          \
    X(RepairMachinePropAnims)                                                                                          \
    X(RepairMachineBox)                                                                                                \
    X(RepairMachineRollBox)                                                                                            \
    X(RepairMachinePartBox)                                                                                            \
    X(RepairMachinePropBox)                                                                                            \
    X(RepairMachineSmokeColumnPlaces)                                                                                  \
    X(RepairMachineDeathExplosionOffset)                                                                               \
    /* ---- content/nino/ruin_effects */                                                                               \
    X(RuinEffectsBlobBox)                                                                                              \
    /* ---- content/nino/ruin_guardian */                                                                              \
    X(RuinGuardianChoiceA)                                                                                             \
    X(RuinGuardianChoiceB)                                                                                             \
    X(RuinGuardianVolleyCounts)                                                                                        \
    X(RuinGuardianTurnTicks)                                                                                           \
    X(RuinGuardianOrbVoices)                                                                                           \
    X(RuinGuardianHurtVoices)                                                                                          \
    X(RuinGuardianLeadFactors)                                                                                         \
    X(RuinGuardianChaseSpeeds)                                                                                         \
    X(RuinGuardianChaseTurns)                                                                                          \
    X(RuinGuardianBox)                                                                                                 \
    X(RuinGuardianOrbBox)                                                                                              \
    /* ---- content/nino/ruin_swimmers */                                                                              \
    X(RuinSwimmersExhaustSizes)                                                                                        \
    X(RuinSwimmersBigSwimmerBox)                                                                                       \
    X(RuinSwimmersSmallSwimmerBox)                                                                                     \
    X(RuinSwimmersTorpedoBoxes)                                                                                        \
    /* ---- content/nino/square_gunship */                                                                             \
    X(SquareGunshipFlyHeadings)                                                                                        \
    X(SquareGunshipRunTicks)                                                                                           \
    X(SquareGunshipBurstTicks)                                                                                         \
    X(SquareGunshipBox)                                                                                                \
    /* ---- content/nino/st15_cls */                                                                                   \
    X(St15ClsBoxHold)                                                                                                  \
    X(St15ClsStruggleTicks)                                                                                            \
    X(St15ClsMemberBox)                                                                                                \
    X(St15ClsMemberLiftBox)                                                                                            \
    /* ---- content/nino/st15_props */                                                                                 \
    X(St15PropsBlastSpread)                                                                                            \
    X(St15PropsStillBox)                                                                                               \
    X(St15PropsPatrolDroneBox)                                                                                         \
    X(St15PropsCutsceneDroneBox)                                                                                       \
    X(St15PropsHoverMachineBox)                                                                                        \
    X(St15PropsHoverMachineTalkBox)                                                                                    \
    X(St15PropsSt19StillBox)                                                                                           \
    X(St15PropsSt19StillTalkBox)                                                                                       \
    X(St15PatrolCentre)                                                                                                \
    /* ---- content/nino/st15_spray */                                                                                 \
    X(St15SprayKnockRiseSigns)                                                                                         \
    X(St15SprayBox)                                                                                                    \
    /* ---- content/nino/st16_crane */                                                                                 \
    X(St16CraneBox)                                                                                                    \
    /* ---- content/nino/st18_bomber_birdbots */                                                                       \
    X(St18BomberBirdbotsBombHold)                                                                                      \
    X(St18BomberBirdbotsBox)                                                                                           \
    X(St18BomberBirdbotsLiftBox)                                                                                       \
    /* ---- content/nino/st18_gunships */                                                                              \
    X(St18GunshipsFightCruiseTicks)                                                                                    \
    X(St18GunshipsFightHeadings)                                                                                       \
    X(St18GunshipsFightRunTicks)                                                                                       \
    X(St18GunshipsFightBurstTicks)                                                                                     \
    X(St18GunshipsDropCooldowns)                                                                                       \
    X(St18GunshipsCutsceneGunshipBox)                                                                                  \
    X(St18GunshipsFightGunshipBox)                                                                                     \
    /* ---- content/nino/st18_npc */                                                                                   \
    X(St18NpcCollisionBox)                                                                                             \
    X(St18NpcTalkBox)                                                                                                  \
    /* ---- content/nino/st18_props */                                                                                 \
    X(St18PropsStillModelVariants)                                                                                     \
    X(St18PropsStillBox)                                                                                               \
    X(St18FlareChains)                                                                                                 \
    /* ---- content/nino/st19 */                                                                                       \
    X(St19MachineBox)                                                                                                  \
    X(St19BurstOffsetA)                                                                                                \
    X(St19BurstOffsetB)                                                                                                \
    /* ---- content/saul_kada/fire_ruin_cutscene_cast */                                                               \
    X(FireRuinCutsceneCastSparkBones)                                                                                  \
    /* ---- content/saul_kada/fire_ruin_rooms */                                                                       \
    X(FireRuinRoomsEmberAngles)                                                                                        \
    /* ---- content/saul_kada/hopping_reaverbot */                                                                     \
    X(HoppingReaverbotWanderHeadings)                                                                                  \
    X(HoppingReaverbotCircleOffsets)                                                                                   \
    X(HoppingReaverbotPickedHeadings)                                                                                  \
    X(HoppingReaverbotPickedRanges)                                                                                    \
    X(HoppingReaverbotLandingRise)                                                                                     \
    /* ---- content/saul_kada/lava_pit */                                                                              \
    X(LavaPitSpotPicks)                                                                                                \
    X(LavaPitCreatureSpots)                                                                                            \
    /* ---- content/saul_kada/lava_vents */                                                                            \
    X(LavaVentsBurnHeights)                                                                                            \
    X(LavaVentsPlumeAngles)                                                                                            \
    X(LavaVentsPlumeDistances)                                                                                         \
    X(LavaVentKindLists)                                                                                               \
    /* ---- content/saul_kada/minimaps */                                                                              \
    X(St26MinimapVisitedBases)                                                                                         \
    X(St26MinimapAreas)                                                                                                \
    X(St26MinimapDots)                                                                                                 \
    X(St48MinimapPictures)                                                                                             \
    X(St41MinimapVisitedBases)                                                                                         \
    X(St41MinimapAreas)                                                                                                \
    X(St41MinimapDots)                                                                                                 \
    /* ---- content/saul_kada/pirate_servbots */                                                                       \
    X(PirateServbotsRunTimes)                                                                                          \
    X(PirateServbotsLieTimes)                                                                                          \
    X(PirateServbotsLieDownTimes)                                                                                      \
    X(PirateServbotsLobDistances)                                                                                      \
    /* ---- content/saul_kada/pirate_tank */                                                                           \
    X(PirateTankModelVariants)                                                                                         \
    X(PirateTankFireTimers)                                                                                            \
    X(PirateTankTurnTimers)                                                                                            \
    X(PirateTankStepTimers)                                                                                            \
    /* ---- content/saul_kada/sand_burrower */                                                                         \
    X(SandBurrowerLeapRise)                                                                                            \
    X(SandBurrowerLeapAhead)                                                                                           \
    X(SandBurrowerDiveRise)                                                                                            \
    X(SandBurrowerDiveAhead)                                                                                           \
    X(SandBurrowerChargeRise)                                                                                          \
    X(SandBurrowerChargeAhead)                                                                                         \
    /* ---- content/saul_kada/saul_kada_rooms */                                                                       \
    X(SaulKadaRoomsSt41ScriptGroups)                                                                                   \
    X(SaulKadaRoomsSt42ScriptGroups)                                                                                   \
    /* ---- content/saul_kada/st13_spin_blade */                                                                       \
    X(St13SpinBladeTopSpeeds)                                                                                          \
    /* ---- content/saul_kada/st28_bomb_ribbon */                                                                      \
    X(St28BombRibbonQuadOffsets)                                                                                       \
    /* ---- content/saul_kada/st28_flyer */                                                                            \
    X(St28FlyerDropRolls)                                                                                              \
    X(St28FlyerReaverbotSpawnBytes)                                                                                    \
    X(St28FlyerPickupKinds)                                                                                            \
    X(St28FlyerLandingSegments)                                                                                        \
    /* ---- content/saul_kada/st28_gunner_boss */                                                                      \
    X(St28GunnerBossSpraySpread)                                                                                       \
    X(St28GunnerBossHome)                                                                                              \
    /* ---- content/saul_kada/st2a_slide_block */                                                                      \
    X(St2aSlideBlockStillRolls)                                                                                        \
    X(St2aSlideBlockSlidingRolls)                                                                                      \
    /* ---- content/saul_kada/st2b_cannon_shell */                                                                     \
    X(St2bCannonShellGravities)                                                                                        \
    /* ---- content/saul_kada/st34_ruin_flyer */                                                                       \
    X(St34RuinFlyerBindRows)                                                                                           \
    X(St34RuinFlyerDropRows)                                                                                           \
    /* ---- content/saul_kada/st34_ruin_sentry */                                                                      \
    X(St34RuinSentryWakeRoutines)                                                                                      \
    X(St34SentryModelVariant)                                                                                          \
    /* ---- content/saul_kada/st3c_gold_boss */                                                                        \
    X(St3cGoldBossPickRadii)                                                                                           \
    X(St3cGoldBossChainBones)                                                                                          \
    X(St3cGoldBossChaseRadiusPicks)                                                                                    \
    X(St3cGoldBossCrossRadiusPicks)                                                                                    \
    X(St3cGoldBossLungeChances)                                                                                        \
    X(St3cGoldBossHopChances)                                                                                          \
    X(St3cGoldBossHopSpeed)                                                                                            \
    X(St3cGoldBossChainRoot)                                                                                           \
    /* ---- content/saul_kada/st3c_minion */                                                                           \
    X(St3cMinionVoiceOffsets)                                                                                          \
    /* ---- content/saul_kada/st3c_pirate_cast */                                                                      \
    X(St3cPirateCastFirstAnims)                                                                                        \
    /* ---- content/saul_kada/st40_head_npc */                                                                         \
    X(St40HeadNpcModelVariants)                                                                                        \
    /* ---- content/saul_kada/st41_carrier */                                                                          \
    X(St41CarrierFlags)                                                                                                \
    X(St41CarrierLightPoints)                                                                                          \
    /* ---- content/saul_kada/st41_crumbling_platform */                                                               \
    X(St41CrumblingPlatformModels)                                                                                     \
    X(St41CrumblingPlatformPieceModels)                                                                                \
    X(St41CrumblingPlatformBrokenTimers)                                                                               \
    X(St41CrumblingPlatformPieceOffsets)                                                                               \
    /* ---- content/saul_kada/st41_enemy_generator */                                                                  \
    X(St41EnemyGeneratorReaverbotSpawnBytes)                                                                           \
    /* ---- content/saul_kada/st41_hover_gunner */                                                                     \
    X(St41HoverGunnerWallYaws)                                                                                         \
    /* ---- content/saul_kada/st41_sliding_door */                                                                     \
    X(St41SlidingDoorLockFlags)                                                                                        \
    /* ---- content/saul_kada/st48_charger */                                                                          \
    X(St48ChargerWallAngles)                                                                                           \
    X(St48ChargerRunTimes)                                                                                             \
    X(St48ChargerRunOffTimes)                                                                                          \
    X(St48ChargerBrakeSpins)                                                                                           \
    /* ---- content/saul_kada/st48_gunship */                                                                          \
    X(St48GunshipCruiseTurnLimits)                                                                                     \
    X(St48GunshipCruiseBursts)                                                                                         \
    X(St48GunshipStrafeBursts)                                                                                         \
    X(St48GunshipStrafeCooldowns)                                                                                      \
    X(St48GunshipStraightRuns)                                                                                         \
    /* ---- content/saul_kada/trapped_miner */                                                                         \
    X(TrappedMinerIdlePauses)                                                                                          \
    X(TrappedMinerCallCries)                                                                                           \
    X(TrappedMinerPleadCries)                                                                                          \
    X(TrappedMinerCryIntervals)                                                                                        \
    /* ---- content/saul_kada/window_giant */                                                                          \
    X(WindowGiantReactionAnims)                                                                                        \
    X(WindowGiantReturnTurns)                                                                                          \
    X(WindowGiantDebrisOffsets)                                                                                        \
    /* ---- content/subruins/blob_boss */                                                                              \
    X(BlobBossRingYaws)                                                                                                \
    /* ---- content/subruins/leaping_critter */                                                                        \
    X(LeapingCritterPuffOffsetsX)                                                                                      \
    X(LeapingCritterPuffOffsetsZ)                                                                                      \
    X(LeapingCritterPaths)                                                                                             \
    /* ---- content/subruins/minimaps */                                                                               \
    X(St4FMinimapVisitedBases)                                                                                         \
    X(St4FMinimapAreas)                                                                                                \
    X(St51MinimapVisitedBases)                                                                                         \
    X(St51MinimapAreas)                                                                                                \
    X(SubruinMinimapFirstBases)                                                                                        \
    X(St4AMinimapVisitedBases)                                                                                         \
    X(St4AMinimapAreas)                                                                                                \
    X(St4DMinimapVisitedBases)                                                                                         \
    X(St4DMinimapAreas)                                                                                                \
    X(St50MinimapVisitedBases)                                                                                         \
    X(St50MinimapAreas)                                                                                                \
    /* ---- content/subruins/pickup_eater */                                                                           \
    X(PickupEaterBlockedTurns)                                                                                         \
    X(PickupEaterBox)                                                                                                  \
    X(PickupEaterLiftBox)                                                                                              \
    /* ---- content/subruins/ruin_blocks */                                                                            \
    X(RuinBlocksBlockModels)                                                                                           \
    X(RuinBlocksPieceModels)                                                                                           \
    X(RuinBlocksRegrowTicks)                                                                                           \
    X(RuinBlocksCrumbleOffsets)                                                                                        \
    /* ---- content/yosyonke/abandoned_mine_relic */                                                                   \
    X(AbandonedMineRelicCollisionBox)                                                                                  \
    X(AbandonedMineRelicTalkBox)                                                                                       \
    /* ---- content/yosyonke/abandoned_mine_roll */                                                                    \
    X(AbandonedMineRollRuinRollCollisionBox)                                                                           \
    X(AbandonedMineRollRuinRollTalkBox)                                                                                \
    X(AbandonedMineRollFirstPath)                                                                                      \
    X(AbandonedMineRollSecondPath)                                                                                     \
    /* ---- content/yosyonke/dev_room */                                                                               \
    X(DevRoomHandBob)                                                                                                  \
    X(DevRoomRollStates)                                                                                               \
    X(DevRoomNinoStates)                                                                                               \
    /* ---- content/yosyonke/flutter_cabin */                                                                          \
    X(FlutterCabinRollCollisionBox)                                                                                    \
    X(FlutterCabinRollTalkBox)                                                                                         \
    X(FlutterCabinNpcVariants)                                                                                         \
    /* ---- content/yosyonke/ice_ruin_appearing_block */                                                               \
    X(IceRuinAppearingBlockAppearFlags)                                                                                \
    /* ---- content/yosyonke/ice_ruin_blade_barrier */                                                                 \
    X(IceRuinBladeBarrierModels)                                                                                       \
    X(IceRuinBladeBarrierDebrisModelIndex)                                                                             \
    X(IceRuinBladeBarrierDebrisOffsets)                                                                                \
    X(IceRuinBladeBarrierSegmentHitDistance)                                                                           \
    X(IceRuinBladeBarrierSegmentHitRadius)                                                                             \
    /* ---- content/yosyonke/ice_ruin_bubble_trail */                                                                  \
    X(IceRuinBubbleTrailBubbleU)                                                                                       \
    X(IceRuinBubbleTrailBubbleV)                                                                                       \
    /* ---- content/yosyonke/ice_ruin_burrowing_shooter */                                                             \
    X(IceRuinBurrowingShooterBox)                                                                                      \
    /* ---- content/yosyonke/ice_ruin_critters */                                                                      \
    X(IceRuinCrittersSceneStandTicks)                                                                                  \
    X(IceRuinCrittersSceneHopTicks)                                                                                    \
    X(IceRuinCrittersWalkTicks)                                                                                        \
    X(IceRuinCrittersCatchableHopTicks)                                                                                \
    X(IceRuinCrittersCatchableStandTicks)                                                                              \
    X(IceRuinCrittersCaughtFlags)                                                                                      \
    /* ---- content/yosyonke/ice_ruin_herded_creature */                                                               \
    X(IceRuinHerdedCreaturePennedPlaces)                                                                               \
    X(IceRuinHerdedCreaturePostX)                                                                                      \
    X(IceRuinHerdedCreaturePostZ)                                                                                      \
    X(IceRuinHerdedCreatureHerdBox)                                                                                    \
    /* ---- content/yosyonke/ice_ruin_room_helpers */                                                                  \
    X(IceRuinRoomHelpersSt2dScriptGroups)                                                                              \
    X(IceRuinRoomHelpersSt2eScriptGroups)                                                                              \
    X(IceRuinRoomHelpersSt2fScriptGroups)                                                                              \
    X(IceRuinRoomHelpersSt2fFieldRooms)                                                                                \
    /* ---- content/yosyonke/ice_ruin_rooms */                                                                         \
    X(IceRuinRoomsPenFlags)                                                                                            \
    X(IceRuinRoomsHerdStarts)                                                                                          \
    /* ---- content/yosyonke/ice_ruin_shrink_block */                                                                  \
    X(IceRuinShrinkBlockGoneFlags)                                                                                     \
    /* ---- content/yosyonke/minimaps */                                                                               \
    X(St0FMinimapVisitedBases)                                                                                         \
    X(St0FMinimapAreas)                                                                                                \
    X(St0DMinimapPictures)                                                                                             \
    X(TownMinimapPictures)                                                                                             \
    X(TownMinimapTextures)                                                                                             \
    X(St2DMinimapVisitedBases)                                                                                         \
    X(St2DMinimapAreas)                                                                                                \
    X(St2DMinimapDots)                                                                                                 \
    /* ---- content/yosyonke/roll_follower */                                                                          \
    X(RollFollowerCollisionBox)                                                                                        \
    X(RollFollowerTalkBox)                                                                                             \
    X(RollFollowerShedCollisionBox)                                                                                    \
    X(RollFollowerShedTalkBox)                                                                                         \
    X(RollFollowerFieldRollCollisionBox)                                                                               \
    X(RollFollowerFieldRollTalkBox)                                                                                    \
    /* ---- content/yosyonke/st05_entry_list */                                                                        \
    X(St05EntryListHandBob)                                                                                            \
    X(St05EntryListScriptStates)                                                                                       \
    X(St05EntryListMenuStates)                                                                                         \
    /* ---- content/yosyonke/st05_st06_props */                                                                        \
    X(St05St06PropsTalkPropMessages)                                                                                   \
    X(St05St06PropsTalkBoxSmall)                                                                                       \
    X(St05St06PropsTalkBoxBar)                                                                                         \
    X(St05St06PropsTalkBoxDefault)                                                                                     \
    X(St05St06PropsFigureTalkBox)                                                                                      \
    /* ---- content/yosyonke/st09_kick_ball */                                                                         \
    X(St09KickBallCollisionBox)                                                                                        \
    X(St09KickBallLiftBox)                                                                                             \
    /* ---- content/yosyonke/st09_push_block */                                                                        \
    X(St09PushBlockCollisionBox)                                                                                       \
    X(St09PushBlockLiftBox)                                                                                            \
    /* ---- content/yosyonke/st0a_fx */                                                                                \
    X(St0aFxTrailFrames)                                                                                               \
    X(St0aFxCycleFrameU)                                                                                               \
    X(St0aFxCycleFrameV)                                                                                               \
    X(St0aFxBurstRayAngles)                                                                                            \
    X(St0aFxBurstSegmentAngles)                                                                                        \
    X(St0aFxBurstRayLengths)                                                                                           \
    X(St0aFxBurstSegmentRadii)                                                                                         \
    X(St0aFxTrailRowsAlongZ)                                                                                           \
    X(St0aFxTrailRowsAlongY)                                                                                           \
    /* ---- content/yosyonke/st0d_critter */                                                                           \
    X(St0dCritterCollisionBox)                                                                                         \
    X(St0dCritterLiftBox)                                                                                              \
    /* ---- content/yosyonke/st0e_boss_vehicle */                                                                      \
    X(St0eBossVehicleSoldierWalkTicks)                                                                                 \
    X(St0eBossVehicleBurnThresholds)                                                                                   \
    X(St0eBossVehicleBurstAngles)                                                                                      \
    X(St0eBossVehicleBurstShifts)                                                                                      \
    X(St0eBossVehicleBurstRises)                                                                                       \
    X(St0eBossVehicleFireballBox)                                                                                      \
    X(St0eBossVehicleBurnPoints)                                                                                       \
    /* ---- content/yosyonke/st0e_carrier_crate */                                                                     \
    X(St0eCarrierCrateBox)                                                                                             \
    X(St0eCarrierCrateLiftBox)                                                                                         \
    X(St0eCarrierCratePoints)                                                                                          \
    /* ---- content/yosyonke/st0e_debug_beam */                                                                        \
    X(St0eDebugBeamWidths)                                                                                             \
    X(St0eDebugBeamRingColours)                                                                                        \
    X(St0eDebugBeamBodyColours)                                                                                        \
    /* ---- content/yosyonke/st0e_servbots */                                                                          \
    X(St0eServbotsFaceCells)                                                                                           \
    X(St0eServbotsFaceVoices)                                                                                          \
    X(St0eServbotsRiderCries)                                                                                          \
    X(St0eServbotsRiderShouts)                                                                                         \
    X(St0eServbotsCrateServbotBox)                                                                                     \
    X(St0eServbotsRiderBox)                                                                                            \
    X(St0eServbotsFlungBox)                                                                                            \
    X(St0eServbotsFlungLiftBox)                                                                                        \
    X(St0eServbotsWriggleTimers)                                                                                       \
    /* ---- content/yosyonke/st0e_turret */                                                                            \
    X(St0eTurretHurtVoices)                                                                                            \
    X(St0eTurretSweepVoices)                                                                                           \
    X(St0eTurretCoreBlastSounds)                                                                                       \
    X(St0eTurretArmBlastSounds)                                                                                        \
    X(St0eTurretCannonShotBox)                                                                                         \
    X(St0eTurretHeldBombBox)                                                                                           \
    X(St0eTurretBombBox)                                                                                               \
    X(St0eTurretCoreWreckYaws)                                                                                         \
    X(St0eTurretCoreWreckOffsets)                                                                                      \
    X(St0eTurretCoreWreckVelocities)                                                                                   \
    X(St0eTurretArmWreckYaws)                                                                                          \
    X(St0eTurretArmWreckOffsets)                                                                                       \
    X(St0eTurretArmWreckVelocities)                                                                                    \
    X(St0eTurretDyingWreckYaws)                                                                                        \
    X(St0eTurretDyingWreckOffsets)                                                                                     \
    X(St0eTurretDyingWreckVelocities)                                                                                  \
    /* ---- content/yosyonke/town_props */                                                                             \
    X(TownPropsSt08PropCollisionBox)                                                                                   \
    X(TownPropsSt08PropTalkBox)                                                                                        \
    X(TownPropsSt09KickableCollisionBox)                                                                               \
    X(TownPropsSt09KickableTalkBox)                                                                                    \
    X(TownPropsSt09TalkingCollisionBox)                                                                                \
    X(TownPropsSt09TalkingTalkBox)                                                                                     \
    /* ---- content/yosyonke/town_side_scripts */                                                                      \
    X(TownSideScriptsWalkInOffsets)                                                                                    \
    X(TownSideScriptsExamDoorStates)                                                                                   \
    X(TownSideScriptsSt06VoiceStates)                                                                                  \
    X(TownSideScriptsBystanderScriptStates)                                                                            \
    X(TownSideScriptsVisitorScriptStates)                                                                              \
    /* ---- content/calbania/base_explosion_script */                                                                  \
    X(BaseExplosionScriptBlowUpPoints)                                                                                 \
    X(BaseExplosionScriptSparkPoints)                                                                                  \
    /* ---- content/calbania/escape_timer */                                                                           \
    X(EscapeTimerSprites)                                                                                              \
    /* ---- content/calbania/glyde_base_bombers */                                                                     \
    X(GlydeBaseBombersReappearPlaces)                                                                                  \
    /* ---- content/calbania/glyde_base_scripts */                                                                     \
    X(GlydeBaseScriptsTowerEvents)                                                                                     \
    X(GlydeBaseScriptsTowerExplosions)                                                                                 \
    X(GlydeBaseScriptsTowerBlasts)                                                                                     \
    X(GlydeBaseScriptsTowerSmokes)                                                                                     \
    X(GlydeBaseOverviewCameras)                                                                                        \
    X(GlydeBaseOverviewSteps)                                                                                          \
    /* ---- content/calbania/island_blown_script */                                                                    \
    X(IslandBlownScriptBlastRecords)                                                                                   \
    /* ---- content/calbania/nino_return_scripts */                                                                    \
    X(NinoReturnScriptsFlyFrom)                                                                                        \
    X(NinoReturnScriptsFlyTo)                                                                                          \
    /* ---- content/calbania/st22_fire_shots */                                                                        \
    X(St22FireShotsFireballBounceSpeeds)                                                                               \
    /* ---- content/calbania/tron_boss */                                                                              \
    X(TronBossTauntMessages)                                                                                           \
    X(TronBossHitPointMessages)                                                                                        \
    /* ---- content/intro/attract_effects */                                                                           \
    X(AttractEffectsVramTileKeys)                                                                                      \
    X(AttractEffectsLightningVariants)                                                                                 \
    X(AttractEffectsRingFlashStates)                                                                                   \
    /* ---- content/intro/crash_landing_script */                                                                      \
    X(CrashLandingScriptAlarmStreaks)                                                                                  \
    X(CrashLandingScriptFlutterStreaks)                                                                                \
    X(CrashLandingScriptEscapeStreaks)                                                                                 \
    X(CrashLandingScriptDebrisStreaks)                                                                                 \
    X(CrashLandingScriptDebrisPoints)                                                                                  \
    X(CrashLandingScriptBurnRecords)                                                                                   \
    /* ---- content/intro/opening_effects */                                                                           \
    X(OpeningEffectsPuffKinds)                                                                                         \
    X(OpeningStreakKinds)                                                                                              \
    X(OpeningLiftStreakKinds)                                                                                          \
    /* ---- content/nino/mission_gunships */                                                                           \
    X(MissionGunshipsEntryPoints)                                                                                      \
    /* ---- content/saul_kada/dropped_item */                                                                          \
    X(DroppedItemTouches)                                                                                              \
    /* ---- content/saul_kada/fire_ruin_entrance_scripts */                                                            \
    X(FireRuinEntranceScriptsGateEmberPoints)                                                                          \
    X(FireRuinEntranceScriptsRubbleFlamePoints)                                                                        \
    /* ---- content/saul_kada/flame_tongues */                                                                         \
    X(FlameTonguesTongueRows)                                                                                          \
    X(FlameTongueShapeSets)                                                                                            \
    /* ---- content/saul_kada/st28_gunner_beam */                                                                      \
    X(St28GunnerBeamCaughtRings)                                                                                       \
    /* ---- content/saul_kada/st28_gunner_bomb */                                                                      \
    X(St28GunnerBombFlameOffsets)                                                                                      \
    /* ---- content/saul_kada/st3b_flame_wall */                                                                       \
    X(St3bFlameWallRows)                                                                                               \
    /* ---- content/saul_kada/st3b_pirate_carrier */                                                                   \
    X(St3bPirateCarrierEffectPoints)                                                                                   \
    X(St3bPirateCarrierLeaveSpots)                                                                                     \
    X(St3bPirateCarrierBurstRings)                                                                                     \
    X(St3bPirateCarrierBurstColumns)                                                                                   \
    /* ---- content/saul_kada/st3b_script5c_cast */                                                                    \
    X(St3bScript5cCastDebrisCorners)                                                                                   \
    X(St3bScript5cCastDebrisTexels)                                                                                    \
    /* ---- content/yosyonke/st0e_defended_vehicle */                                                                  \
    X(St0eDefendedVehicleLastRoundTimers)                                                                              \
    X(St0eDefendedVehicleFrontTimers)                                                                                  \
    X(St0eDefendedVehicleHoldBackTimers)                                                                               \
    X(St0eDefendedVehicleSwapLeftTimers)                                                                               \
    X(St0eDefendedVehicleSwapRightTimers)                                                                              \
    /* ---- game/menu_draw */                                                                                          \
    X(MenuDrawAsciiMetrics)                                                                                            \
    /* ---- game/player/player_weapons */                                                                              \
    X(PlayerWeaponsWeaponTable)                                                                                        \
    X(PlayerWeaponsGaugeTable)                                                                                         \
    X(PlayerWeaponStatTables)                                                                                          \
    X(WeaponMaxLevels)                                                                                                 \
    /* ---- game/status_weapon_model */                                                                                \
    X(StatusWeaponModelPlaces)                                                                                         \
    X(StatusWeaponModelBoxOffsets)                                                                                     \
    X(StatusWeaponModelBoxAreas)                                                                                       \
    /* ---- game/exit_transitions */                                                                                   \
    X(PassageAxisSigns)                                                                                                \
    X(HingedDoorAxisSigns)                                                                                             \
    X(DoubleDoorAxisSigns)                                                                                             \
    X(SlidingPairAxisSigns)                                                                                            \
    X(WalkOffAxisSigns)                                                                                                \
    X(PassageViews)                                                                                                    \
    X(HingedDoorViews)                                                                                                 \
    X(DoubleDoorViews)                                                                                                 \
    X(SlidingPairViews)                                                                                                \
    X(WalkOffViews)                                                                                                    \
    X(PassageLeafOffsets)                                                                                              \
    X(DoubleDoorLeafOffsets)                                                                                           \
    X(SlidingPairLeafOffsets)                                                                                          \
    X(PassageStates)                                                                                                   \
    X(WalkOffStates)                                                                                                   \
    X(DoorActorStates)                                                                                                 \
    X(HingedDoorStates)                                                                                                \
    X(DoubleDoorStates)                                                                                                \
    X(SlidingPairStates)                                                                                               \
    X(PitStates)                                                                                                       \
    X(DoorActorCameras)                                                                                                \
    /* ---- game/actor_stats */                                                                                        \
    X(DifficultyAnimatedStats)                                                                                         \
    X(DifficultyStaticStats)                                                                                           \
    /* ---- game/audio_logic */                                                                                        \
    X(VoiceFileNames)                                                                                                  \
    /* ---- game/hit_effects */                                                                                        \
    X(HitSparkScripts)                                                                                                 \
    /* ---- game/message_triggers */                                                                                   \
    X(TriggerConditions)                                                                                               \
    /* ---- game/shockwave */                                                                                          \
    X(ShockwaveSequences)                                                                                              \
    /* ---- game/sky_arc */                                                                                            \
    X(SkyArcStates)                                                                                                    \
    X(SkyArcInnerBands)                                                                                                \
    X(SkyArcOuterBands)                                                                                                \
    X(SkyArcSegmentColours)                                                                                            \
    X(SkyArcInnerColours)                                                                                              \
    X(SkyArcOuterColours)                                                                                              \
    /* ---- game/wall_debris */                                                                                        \
    X(WallDebrisShapeSets)                                                                                             \
    /* ---- game/player/player_animation */                                                                            \
    X(PlayerIdleTablesA)                                                                                               \
    X(PlayerIdleTablesB)                                                                                               \
    /* ---- game/actors */                                                                                             \
    X(GlobalShotClasses)                                                                                               \
    X(GlobalStaticClasses)                                                                                             \
    X(GlobalEffectClasses)                                                                                             \
    /* ---- game/hit_list */                                                                                           \
    X(HitGroupPairs)                                                                                                   \
    /* ---- game/matrix */                                                                                             \
    X(IdentityMatrix)                                                                                                  \
    /* ---- game/blizzard_layer */                                                                                     \
    X(BlizzardSpeeds)                                                                                                  \
    /* ---- game/fade_state */                                                                                         \
    X(FadeDirections)                                                                                                  \
    X(FadeSpeeds)                                                                                                      \
    /* ---- game/display */                                                                                            \
    X(DisplayDrawAreas)                                                                                                \
    /* ---- game/vram_moves */                                                                                         \
    X(FlipBookRows)                                                                                                    \
    X(WaveTileRow)                                                                                                     \
    /* ---- game/story */                                                                                              \
    X(ScriptHandlers)                                                                                                  \
    /* ---- content/intro/st02_attract_cast */                                                                         \
    X(St02AttractCastPropVariants)                                                                                     \
    X(St02AttractCastDriftingVariants)                                                                                 \
    X(St02AttractCastRollVariants)                                                                                     \
    X(St02AttractCastBarrelVariants)                                                                                   \
    X(St02AttractCastPlacedVariants)                                                                                   \
    X(St02AttractCastWatcherVariants)                                                                                  \
    X(St02AttractCastTalkerVariants)                                                                                   \
    X(St02AttractCastFaceVariants)                                                                                     \
    X(St02AttractCastSpeakerVariants)                                                                                  \
    X(St02AttractCastScreenVariants)                                                                                   \
    X(St02AttractCastBlinkerVariants)                                                                                  \
    X(St02AttractCastStanderVariants)                                                                                  \
    /* ---- content/intro/crash_landing_cast */                                                                        \
    X(CrashLandingCastPropVariants)                                                                                    \
    X(CrashLandingCastRollVariants)                                                                                    \
    X(CrashLandingCastBarrelVariants)                                                                                  \
    X(CrashLandingCastSecondBodyVariants)                                                                              \
    X(CrashLandingCastTalkerVariants)                                                                                  \
    X(CrashLandingCastJetVariants)                                                                                     \
    X(CrashLandingCastWatcherVariants)                                                                                 \
    X(CrashLandingCastFaceVariants)                                                                                    \
    X(CrashLandingCastSpeakerVariants)                                                                                 \
    /* ---- content/intro/attract */                                                                                   \
    X(AttractSt02RoomHandlers)                                                                                         \
    /* ---- content/intro/st02_attract_script */                                                                       \
    X(St02AttractScriptScript3States)                                                                                  \
    /* ---- content/intro/st03_attract_script */                                                                       \
    X(St03AttractScriptScript4States)                                                                                  \
    /* ---- content/intro/mission_result */                                                                            \
    X(MissionResultStates)                                                                                             \
    X(MissionResultOutcomeCameraScripts)                                                                               \
    X(MissionResultOutcomeSteps)                                                                                       \
    /* ---- content/intro/fire_mission_rooms */                                                                        \
    X(FireMissionRoomsRoomHandlers)                                                                                    \
    /* ---- content/intro/st39_script68 */                                                                             \
    /* the camera script (its op 0x40 spawns the cast: records 0x8accd0 the class 0x44b940, 0x8acce4 the            \
       crew's variant 2, 0x8accf8 the class 0x4cd370) and the steps (one empty step) */                             \
    X(St39Script68States)                                                                                              \
    X(St39Script68CastVariants)                                                                                        \
    /* ---- content/yosyonke/st09_props */                                                                             \
    X(St09PropsStillModelStates)                                                                                       \
    /* ---- content/yosyonke/town_room_cases */                                                                        \
    X(TownRoomCasesSt05Handlers)                                                                                       \
    X(TownRoomCasesSt06Handlers)                                                                                       \
    X(TownRoomCasesSt0bHandlerTables)                                                                                  \
    /* ---- content/yosyonke/town_story_cast */                                                                        \
    X(TownStoryCastJunkShopVariants)                                                                                   \
    X(TownStoryCastRunningRollVariants)                                                                                \
    X(TownStoryCastJoesLabVariants)                                                                                    \
    X(TownStoryCastSt47PersonVariants)                                                                                 \
    /* ---- content/yosyonke/roll_spawners */                                                                          \
    X(RollFollowerRecord)                                                                                              \
    X(RollShedRecord)                                                                                                  \
    X(RollSt09FollowerRecord)                                                                                          \
    X(RollFieldRecord)                                                                                                 \
    X(RollFollowerPoint)                                                                                               \
    X(RollShedPoint)                                                                                                   \
    X(RollSt09FollowerPoint)                                                                                           \
    X(RollFieldPoint)                                                                                                  \
    /* ---- content/calbania/st21_props */                                                                             \
    X(St21RisingModelRoutines)                                                                                         \
    /* ---- content/late_game/ch4_story */                                                                             \
    X(Ch4StoryArrivalStates)                                                                                           \
    X(Ch4StoryRidingStates)                                                                                            \
    X(Ch4TrainSteps)                                                                                                   \
    X(Ch4ExamHandlers)                                                                                                 \
    /* ---- content/late_game/st4b_scripts */                                                                          \
    X(St4bScriptsRetreatStates)                                                                                        \
    X(St4bScriptsEscapeStates)                                                                                         \
    X(St4bScriptsRetreatingBossStates)                                                                                 \
    X(St4bScriptsEscapingBossStates)                                                                                   \
    X(St4bScriptsStillFigureStates)                                                                                    \
    /* ---- content/late_game/st54_clear_screen */                                                                     \
    X(St54ClearScreenStates)                                                                                           \
    /* ---- content/late_game/st54_props */                                                                            \
    X(St54PropsSwingStateRoutines)                                                                                     \
    /* ---- content/saul_kada/particle_burst */                                                                        \
    X(ParticleBurstCutsceneCues)                                                                                       \
    /* ---- content/saul_kada/st27_respawner */                                                                        \
    X(St27RespawnerRecordLists)                                                                                        \
    /* ---- content/saul_kada/st2b_props */                                                                            \
    X(St2bPropVariantRoutines)                                                                                         \
    X(St2bPropPotRoutines)                                                                                             \
    /* ---- content/saul_kada/st33_falling_rock */                                                                     \
    X(St33RockRadii)                                                                                                   \
    /* ---- content/saul_kada/st3c_props */                                                                            \
    X(St3cPropScriptStepTables)                                                                                        \
    /* ---- content/saul_kada/st3c_statue */                                                                           \
    X(St3cStatueRootOffset)                                                                                            \
    /* ---- content/subruins/subruin_rooms */                                                                          \
    X(St4dLayoutAHandlers)                                                                                             \
    X(St4dLayoutBHandlers)                                                                                             \
    X(St4dLayoutCHandlers)                                                                                             \
    X(St4fHandlers)                                                                                                    \
    X(St50Handlers)                                                                                                    \
    /* ---- content/forbidden_island/island_rooms */                                                                   \
    X(IslandRoomsSt3eFirstExit)                                                                                        \
    X(IslandRoomsSt3eExitToRoom1)                                                                                      \
    X(IslandRoomsSt3eExitToRoom6)                                                                                      \
    X(IslandRoomsSt3eExitStory10)                                                                                      \
    X(IslandRoomsSt1dFirstExit)                                                                                        \
    /* ---- content/yosyonke/town_rooms */                                                                             \
    X(TownRoomsFlutterDoor)                                                                                            \
    X(TownRoomsDockPlaces)                                                                                             \
    /* ---- game/shots/buster_cannon */                                                                                \
    X(BusterCannonBox)                                                                                                 \
    /* ---- content/forbidden_island/elephant_boss */                                                                  \
    X(ElephantBossRockBox)                                                                                          \
    /* ---- game/audio_logic */                                                                                     \
    X(AudioSoundDescriptors)                                                                                        \
    X(AudioMusicDescriptors)                                                                                        \
    X(AudioUnusedDescriptor)                                                                                        \
    X(AudioBuiltInVoices) /* the voices 0xff00.. (exe data) */                                                      \
    /* ---- game/exit_transitions */                                                                                \
    X(ExitPassageCamera)                                                                                            \
    X(ExitPassageOutSteps)                                                                                          \
    X(ExitPassageInSteps)                                                                                           \
    X(ExitWalkOffCamera)                                                                                            \
    X(ExitWalkOffSteps)                                                                                             \
    X(ExitDoorActorSteps)                                                                                           \
    X(ExitHingedDoorCamera)                                                                                         \
    X(ExitHingedDoorSteps)                                                                                          \
    X(ExitDoubleDoorCamera)                                                                                         \
    X(ExitDoubleDoorSteps)                                                                                          \
    X(ExitSlidingPairCamera)                                                                                        \
    X(ExitSlidingPairSteps)                                                                                         \
    X(ExitFrozenSteps)                                                                                              \
    X(ExitLadderCamera)                                                                                             \
    X(ExitLadderSteps)                                                                                              \
    X(ExitPitCamera)                                                                                                \
    X(ExitPitSteps)                                                                                                 \
    X(ExitPushedCamera)                                                                                             \
    X(ExitPushedSteps)                                                                                              \
    X(ExitTeleportCamera)                                                                                           \
    X(ExitTeleportSteps)                                                                                            \
    /* ---- game/explosions */                                                                                      \
    X(ExplosionsBlastColours)                                                                                       \
    /* ---- game/game_init */                                                                                       \
    X(PlayerCollisionBox) /* the player's collision box (exe data) */                                               \
    /* ---- game/guardian_reaverbot */                                                                              \
    X(GuardianReaverbotFistWallBoxes)                                                                               \
    /* ---- game/input_bindings */                                                                                  \
    X(InputBindingsKeyNames)                                                                                        \
    /* ---- game/language_pack */                                                                                   \
    /* ---- game/lens_flare */                                                                                      \
    X(LensFlareSets)                                                                                                \
    X(LensFlareColourRows)                                                                                          \
    /* ---- game/light_column */                                                                                    \
    X(LightColumnColours)                                                                                           \
    /* ---- game/pickups */                                                                                         \
    X(PickupsDropTables)                                                                                            \
    /* ---- game/save_image */                                                                                      \
    /* ---- game/save_menus */                                                                                      \
    /* the message banks (exe data): the memory card's messages, the save menu's texts, the load menu's */          \
    X(SaveMenusCardMessageBank)                                                                                     \
    X(SaveMenusSaveTextBank)                                                                                        \
    X(SaveMenusLoadTextBank)                                                                                        \
    /* ---- game/servbot */                                                                                         \
    X(ServbotLookSets) /* u32 per kind: 8 look modes */                                                             \
    /* ---- game/shockwave */                                                                                       \
    X(ShockwaveRingParams)                                                                                          \
    /* ---- game/shop */                                                                                            \
    X(ShopCameraScript)                                                                                             \
    X(ShopCameraSteps) /* the shop's camera script and steps */                                                     \
    X(ShopArrowUp)                                                                                                  \
    X(ShopArrowDown)                                                                                                \
    /* ---- game/skid_sparks */                                                                                     \
    X(SkidSparksEntries)                                                                                            \
    /* ---- game/spark_emitter */                                                                                   \
    X(SparkEmitterVariants)                                                                                         \
    /* ---- game/status_controls */                                                                                 \
    X(StatusControlsFrameStyles)                                                                                    \
    X(StatusControlsCursorTree)                                                                                     \
    /* ---- game/status_equipment */                                                                                \
    X(StatusEquipmentWeaponRows)                                                                                    \
    X(StatusEquipmentBodyRows)                                                                                      \
    X(StatusEquipmentBusterRows)                                                                                    \
    X(StatusEquipmentSortRows)                                                                                      \
    X(StatusEquipmentManualSortRows)                                                                                \
    /* ---- game/status_options */                                                                                  \
    X(StatusOptionsLevelTwoEntries)                                                                                 \
    X(StatusOptionsCaptionRows)                                                                                     \
    X(StatusOptionsCameraChoice)                                                                                    \
    X(StatusOptionsVibrationChoice)                                                                                 \
    X(StatusOptionsFuriganaChoice)                                                                                  \
    X(StatusOptionsMonoChoice)                                                                                      \
    /* ---- game/status_screen */                                                                                   \
    X(StatusScreenClassTables)                                                                                      \
    X(StatusScreenHighlightClutRect) /* the highlight clut 0x7f13: 16 colours at (0x130, 0x1fc) */                  \
    X(StatusScreenAreaNames)                                                                                        \
    /* ---- game/text_script */                                                                                     \
    X(TextScriptWarpBank) /* the bank of the message fb 3c reopens in another stage */                              \
    X(TextScriptPlaceNameBank) /* fb 44's bank (a language pack's copy: game/language_pack) */                      \
    X(TextScriptKeyNameBank) /* fb 22's key names (their first glyph patched with the key) */                       \
    /* ---- game/text_script_state */                                                                               \
    X(TextScriptNumberText) /* the text fb 21 inserts (7 digits, fb 1f) */                                          \
    X(TextScriptTimeText) /* the text fb 23 inserts (8 characters, fb 1f) */                                        \
    /* ---- game/title */                                                                                           \
    /* the title's class tables (no title actors: its spawn list 0x88c400 is empty) and the depth cue it            \
       points at */                                                                                                 \
    X(TitleClassTables)                                                                                             \
    X(TitleDepthCue)                                                                                                \
    /* ---- game/town_npc */                                                                                        \
    X(TownNpcLookSets) /* u32 per kind: 8 look modes to pick from (0: none) */                                      \
    /* ---- game/travel */                                                                                          \
    X(TravelCameraScript)                                                                                           \
    X(TravelCameraSteps)                                                                                            \
    X(TravelFlutterIconRecord)                                                                                      \
    X(TravelSecondIconRecord)                                                                                       \
    X(TravelIconBoxes)                                                                                              \
    /* ---- game/turret_bullet */                                                                                   \
    X(TurretBulletBoxes)                                                                                            \
    /* ---- game/menu_banks */                                                                                      \
    /* the exe banks of the key configuration's windows (the PC-only page; a pack's texts are OpenDash's:           \
       status_options) */                                                                                           \
    X(MenuKeyConfigHelpBank)                                                                                        \
    X(MenuKeyConfigListsBank)                                                                                       \
    /* ---- game/menu_banks */                                                                                      \
    X(MenuBankItemNames)                                                                                            \
    X(MenuBankItemDescriptions)                                                                                     \
    X(MenuBankPanelHelp)                                                                                            \
    X(MenuBankItemsHelp)                                                                                            \
    X(MenuBankItemUse)                                                                                              \
    X(MenuBankWeaponSwitchMessages)                                                                                 \
    X(MenuBankAreaNames)                                                                                            \
    X(MenuBankOptionsHelp)                                                                                          \
    X(MenuBankOptionsCamera)                                                                                        \
    X(MenuBankOptionsLockOn)                                                                                        \
    X(MenuBankOptionsLockOnMode)                                                                                    \
    X(MenuBankOptionsVibration)                                                                                     \
    X(MenuBankOptionsSound)                                                                                         \
    X(MenuBankOptionsVolume)                                                                                        \
    X(MenuBankOptionsDefaults)                                                                                      \
    X(MenuBankEquipmentHelp)                                                                                        \
    X(MenuBankEquipmentWeapon)                                                                                      \
    X(MenuBankEquipmentBuster)                                                                                      \
    X(MenuBankEquipmentSort)                                                                                        \
    X(MenuBankEquipmentBody)                                                                                        \
    X(MenuBankShop)                                                                                                 \
    X(MenuBankShopPriceStrings)                                                                                     \
    /* ---- game/status_options */                                                                                  \
    X(StatusOptionsKeyConfigValues)                                                                                 \
    X(StatusOptionsCameraValues)                                                                                    \
    X(StatusOptionsLockOnFirstValues)                                                                               \
    X(StatusOptionsLockOnSecondValues)                                                                              \
    X(StatusOptionsVibrationValues)                                                                                 \
    X(StatusOptionsFuriganaValues)                                                                                  \
    X(StatusOptionsMonoValues)                                                                                      \
    X(StatusOptionsLockOnLabels)                                                                                    \
    X(StatusOptionsVolumeLabels)                                                                                    \
    /* ---- game/status_screen */                                                                                   \
    X(StatusScreenFieldDatArea)                                                                                     \
    X(StatusScreenPageDatArea)                                                                                      \
    X(StatusScreenPanelTileMap)                                                                                     \
    /* ---- content/yosyonke/dev_room */                                                                            \
    X(DevRoomBank) /* the room's messages (a pack's copy by redirect: app/play_session) */                          \
    /* ---- content/intro/crash_landing_cast */                                                                     \
    X(CrashLandingCastPropBox)                                                                                      \
    X(CrashLandingCastPieceBox)                                                                                     \
    X(CrashLandingCastRollBox)                                                                                      \
    X(CrashLandingCastRollEyes)                                                                                     \
    X(CrashLandingCastRollMouth)                                                                                    \
    X(CrashLandingCastBarrelBox)                                                                                    \
    X(CrashLandingCastSecondBodyBox)                                                                                \
    X(CrashLandingCastSecondBodyEyes)                                                                               \
    X(CrashLandingCastSecondBodyMouth)                                                                              \
    X(CrashLandingCastTalkerBox)                                                                                    \
    X(CrashLandingCastTalkerEyes)                                                                                   \
    X(CrashLandingCastTalkerMouth)                                                                                  \
    X(CrashLandingCastCraftBox)                                                                                     \
    X(CrashLandingCastJetBox)                                                                                       \
    X(CrashLandingCastWatcherBox)                                                                                   \
    X(CrashLandingCastFaceBox)                                                                                      \
    X(CrashLandingCastFaceEyes)                                                                                     \
    X(CrashLandingCastFaceMouth)                                                                                    \
    X(CrashLandingCastSpeakerBox)                                                                                   \
    X(CrashLandingCastSpeakerEyes)                                                                                  \
    X(CrashLandingCastSpeakerMouth)                                                                                 \
    /* ---- content/intro/crash_landing_script */                                                                   \
    X(CrashLandingScriptCameraScript)                                                                               \
    X(CrashLandingScriptSteps)                                                                                      \
    X(CrashLandingScriptFaceEyes)                                                                                   \
    X(CrashLandingScriptFaceMouth)                                                                                  \
    X(CrashLandingScriptEscapeRecord) /* the escaping craft's spawn record (a script actor) */                      \
    /* ---- content/intro/fire_mission_rooms */                                                                     \
    X(FireMissionRoomsRoom0Fires)                                                                                   \
    X(FireMissionRoomsRoom1Fires)                                                                                   \
    X(FireMissionRoomsRoom1LateFire) /* the fire that follows the one before the wall */                            \
    X(FireMissionRoomsRoom2Fires)                                                                                   \
    X(FireMissionRoomsRoom2Data)                                                                                    \
    X(FireMissionRoomsRoom2Message)                                                                                 \
    /* ---- content/intro/intro_rooms */                                                                            \
    /* ST00 / ST03 sub 1's UV animations: the list exe 0x5f5bd0 fills, the room's animation table (exe data) */     \
    X(IntroRoomsSt00UvAnimList)                                                                                     \
    X(IntroRoomsSt00UvAnimTable)                                                                                    \
    X(IntroRoomsSt03UvAnimList)                                                                                     \
    X(IntroRoomsSt03UvAnimTable)                                                                                    \
    /* ---- content/intro/opening_cast */                                                                           \
    X(OpeningCastCrewTracks)                                                                                        \
    X(OpeningCastTalkerEyes)                                                                                        \
    X(OpeningCastTalkerMouth)                                                                                       \
    /* ---- content/intro/opening_script */                                                                         \
    X(OpeningScriptCameraScript)                                                                                    \
    X(OpeningScriptSteps)                                                                                           \
    X(OpeningScriptFaceEyes)                                                                                        \
    X(OpeningScriptFaceMouth)                                                                                       \
    /* ---- content/intro/st02_attract_cast */                                                                      \
    X(St02AttractCastTalkerBox)                                                                                     \
    X(St02AttractCastTalkerEyes)                                                                                    \
    X(St02AttractCastTalkerMouth)                                                                                   \
    X(St02AttractCastFaceBox)                                                                                       \
    X(St02AttractCastFaceEyes)                                                                                      \
    X(St02AttractCastFaceMouth)                                                                                     \
    X(St02AttractCastSpeakerBox)                                                                                    \
    X(St02AttractCastSpeakerEyes)                                                                                   \
    X(St02AttractCastSpeakerMouth)                                                                                  \
    X(St02AttractCastPropBox)                                                                                       \
    X(St02AttractCastDriftingBox)                                                                                   \
    X(St02AttractCastRollBox)                                                                                       \
    X(St02AttractCastRollEyes)                                                                                      \
    X(St02AttractCastRollMouth)                                                                                     \
    X(St02AttractCastBarrelBox)                                                                                     \
    X(St02AttractCastBarrelHeads)                                                                                   \
    X(St02AttractCastPlacedBox)                                                                                     \
    X(St02AttractCastWatcherBox)                                                                                    \
    X(St02AttractCastScreenBox)                                                                                     \
    X(St02AttractCastScreenCells)                                                                                   \
    X(St02AttractCastBlinkerBox)                                                                                    \
    X(St02AttractCastBlinkerParts)                                                                                  \
    X(St02AttractCastStanderBox)                                                                                    \
    X(St02AttractCastStillModelBox)                                                                                 \
    X(St02AttractCastSceneryBox)                                                                                    \
    /* ---- content/intro/st02_attract_script */                                                                    \
    X(St02AttractScriptSecondFileRings)                                                                             \
    X(St02AttractScriptSecondCastRecord0)                                                                           \
    X(St02AttractScriptSecondCastRecord1)                                                                           \
    X(St02AttractScriptSecondCastRecord2)                                                                           \
    X(St02AttractScriptSecondCastRecord3)                                                                           \
    X(St02AttractScriptLastRings)                                                                                   \
    X(St02AttractScriptShutterImage)                                                                                \
    X(St02AttractScriptFinalRings)                                                                                  \
    X(St02AttractScriptScrollRings)                                                                                 \
    X(St02AttractScriptStep8Rings)                                                                                  \
    X(St02AttractScriptStep9Rings)                                                                                  \
    X(St02AttractScriptStep10Rings)                                                                                 \
    X(St02AttractScriptStep11Rings)                                                                                 \
    X(St02AttractScriptCameraScript)                                                                                \
    X(St02AttractScriptSteps)                                                                                       \
    X(St02AttractScriptSceneryA)                                                                                    \
    X(St02AttractScriptSceneryB)                                                                                    \
    X(St02AttractScriptSceneryC)                                                                                    \
    /* ---- content/intro/st03_attract_cast */                                                                      \
    X(St03AttractCastTalkerBox)                                                                                     \
    X(St03AttractCastTalkerEyes)                                                                                    \
    X(St03AttractCastTalkerMouth)                                                                                   \
    X(St03AttractCastFaceBox)                                                                                       \
    X(St03AttractCastFaceEyes)                                                                                      \
    X(St03AttractCastFaceMouth)                                                                                     \
    X(St03AttractCastSpeakerBox)                                                                                    \
    X(St03AttractCastSpeakerEyes)                                                                                   \
    X(St03AttractCastSpeakerMouth)                                                                                  \
    X(St03AttractCastCarrierBox)                                                                                    \
    X(St03AttractCastCarrierEyes)                                                                                   \
    X(St03AttractCastCarrierMouth)                                                                                  \
    X(St03AttractCastWalkerBox)                                                                                     \
    X(St03AttractCastWalkerEyes)                                                                                    \
    X(St03AttractCastWalkerMouth)                                                                                   \
    X(St03AttractCastFlutterBox)                                                                                    \
    X(St03AttractCastPlacedModelBox)                                                                                \
    X(St03AttractCastStillModelBox)                                                                                 \
    X(St03AttractCastSceneryBox)                                                                                    \
    X(St03AttractCastRollBox)                                                                                       \
    X(St03AttractCastRollEyes)                                                                                      \
    X(St03AttractCastRollMouth)                                                                                     \
    X(St03AttractCastBarrelBox)                                                                                     \
    X(St03AttractCastWatcherBox)                                                                                    \
    X(St03AttractCastBodyBox)                                                                                       \
    X(St03AttractCastBodyEyes)                                                                                      \
    X(St03AttractCastBodyMouth)                                                                                     \
    X(St03AttractCastLightsBox)                                                                                     \
    X(St03AttractCastBlinkerBox)                                                                                    \
    X(St03AttractCastStanderBox)                                                                                    \
    X(St03AttractCastPartBlinkerBox)                                                                                \
    X(St03AttractCastScreenBox)                                                                                     \
    X(St03AttractCastCraftBox)                                                                                      \
    /* ---- content/intro/st03_attract_script */                                                                    \
    X(St03AttractScriptChapterCastRecord0)                                                                          \
    X(St03AttractScriptChapterCastRecord1)                                                                          \
    X(St03AttractScriptChapterCastRecord2)                                                                          \
    X(St03AttractScriptChapterCastRecord3)                                                                          \
    X(St03AttractScriptChapterCastRecord4)                                                                          \
    X(St03AttractScriptChapterCastRecord5)                                                                          \
    X(St03AttractScriptChapterCastRecord6)                                                                          \
    X(St03AttractScriptChapterRings)                                                                                \
    X(St03AttractScriptSecondFileFirstCastRecord)                                                                   \
    X(St03AttractScriptSecondFileSecondCastRecord)                                                                  \
    X(St03AttractScriptSceneryKindsRings)                                                                           \
    X(St03AttractScriptThirdFileFreedCastRecord)                                                                    \
    X(St03AttractScriptThirdFileCastRecord)                                                                         \
    X(St03AttractScriptCameraScript)                                                                                \
    X(St03AttractScriptSteps)                                                                                       \
    X(St03AttractScriptCueRecord)                                                                                   \
    X(St03AttractScriptCastRecords)                                                                                 \
    X(St03AttractScriptSparklePathA)                                                                                \
    X(St03AttractScriptSparklePathB)                                                                                \
    X(St03AttractScriptChipPoints)                                                                                  \
    /* ---- content/intro/st03_props */                                                                             \
    X(St03PropsStillModelBox) /* the collision box (exe data) */                                                    \
    /* ---- content/intro/st39_script68 */                                                                          \
    X(St39Script68CameraScript)                                                                                     \
    X(St39Script68Steps)                                                                                            \
    X(St39Script68CastBox)                                                                                          \
    X(St39Script68CastEyes)                                                                                         \
    X(St39Script68CastMouth)                                                                                        \
    /* ---- content/intro/st4a_rooms */                                                                             \
    /* the spawn records (exe data) */                                                                              \
    X(St4aRoomsResumeRecord)                                                                                        \
    X(St4aRoomsStartRecord)                                                                                         \
    X(St4aRoomsSecondFightRecord)                                                                                   \
    /* ---- content/subruins/blob_boss */                                                                           \
    X(BlobBossBlobBox)                                                                                              \
    /* ---- content/subruins/blob_boss_shots */                                                                     \
    X(BlobBossShotsFallingShotBox)                                                                                  \
    X(BlobBossShotsLobbedBlobBox)                                                                                   \
    /* ---- content/subruins/leaping_critter */                                                                     \
    X(LeapingCritterBoxes)                                                                                          \
    X(LeapingCritterLiftBoxes) /* by variant, 12 bytes each */                                                      \
    /* ---- content/subruins/pickup_eater */                                                                        \
    X(PickupEaterProbePoints) /* four per variant */                                                                \
    /* ---- content/subruins/ruin_blocks */                                                                         \
    X(RuinBlocksBlockBox)                                                                                           \
    X(RuinBlocksFallingPieceBox)                                                                                    \
    X(RuinBlocksBouncingPieceBox)                                                                                   \
    /* ---- content/subruins/st51_rooms */                                                                          \
    X(St51RoomsGuardianRecord)                                                                                      \
    X(St51RoomsItemRecord)                                                                                          \
    /* ---- content/subruins/st58_rooms */                                                                          \
    X(St58RoomsDoorRoomRecord) /* the room's record while the door is shut */                                       \
    /* ---- content/subruins/subruin_rooms */                                                                       \
    X(SubruinRoomsST4FSub7Record)                                                                                   \
    X(SubruinRoomsST4FSub11Records)                                                                                 \
    X(SubruinRoomsST4FSub12Record)                                                                                  \
    X(SubruinRoomsST50Sub10Record)                                                                                  \
    X(SubruinRoomsST50Records)                                                                                      \
    /* ---- content/subruins/subruin_spawners */                                                                    \
    X(SubruinSpawnersV1Record)                                                                                      \
    X(SubruinSpawnersV1Wave)                                                                                        \
    X(SubruinSpawnersV3Wave)                                                                                        \
    X(SubruinSpawnersV4Record)                                                                                      \
    X(SubruinSpawnersV7Wave)                                                                                        \
    /* ---- content/manda/manda_guardian */                                                                         \
    X(MandaGuardianBladeCorners)                                                                                    \
    X(MandaGuardianBladeModels)                                                                                     \
    /* ---- content/manda/manda_rooms */                                                                            \
    X(MandaRoomsEntranceHallSpawnsCardUsed)                                                                         \
    X(MandaRoomsEntranceHallSpawns)                                                                                 \
    X(MandaRoomsSt14GuardianSpawns)                                                                                 \
    /* ---- content/manda/manda_ruin_scripts */                                                                     \
    X(MandaRuinScriptsScript9CameraScript)                                                                          \
    X(MandaRuinScriptsScript9Steps)                                                                                 \
    X(MandaRuinScriptsScript9WrapUpSpawns)                                                                          \
    X(MandaRuinScriptsScriptBCameraScript)                                                                          \
    X(MandaRuinScriptsScriptBSteps)                                                                                 \
    X(MandaRuinScriptsScriptBWrapUpSpawns)                                                                          \
    X(MandaRuinScriptsScriptCWrapUpSpawns)                                                                          \
    X(MandaRuinScriptsScript1aCameraScript)                                                                         \
    X(MandaRuinScriptsScript1aSteps)                                                                                \
    X(MandaRuinScriptsScript1aWrapUpSpawns)                                                                         \
    X(MandaRuinScriptsGuardianWreckRecord)                                                                          \
    X(MandaRuinScriptsGuardianJumpRecord)                                                                           \
    X(MandaRuinScriptsScriptECameraScript)                                                                          \
    X(MandaRuinScriptsScriptESteps)                                                                                 \
    X(MandaRuinScriptsScriptEWrapUpSpawns)                                                                          \
    X(MandaRuinScriptsScriptFCameraScript)                                                                          \
    X(MandaRuinScriptsScriptFSteps)                                                                                 \
    X(MandaRuinScriptsScriptACameraScript)                                                                          \
    X(MandaRuinScriptsScriptASteps)                                                                                 \
    X(MandaRuinScriptsScriptCCameraScript)                                                                          \
    X(MandaRuinScriptsScriptCSteps)                                                                                 \
    X(MandaRuinScriptsFirstHallFigureRecord)                                                                        \
    X(MandaRuinScriptsSecondHallFigureRecord) /* the guardian 0x4875e0 variant 1 */                                 \
    /* ---- content/manda/pokte_rooms */                                                                            \
    X(PokteRoomsVillageAfterTronSpawns)                                                                             \
    X(PokteRoomsVillageAfterTronHouseSpawns)                                                                        \
    X(PokteRoomsVillageAfterTronSecondHouseRepairSpawns)                                                            \
    X(PokteRoomsVillageAfterTronFirstHouseRepairSpawns)                                                             \
    X(PokteRoomsVillagersSpawns)                                                                                    \
    X(PokteRoomsVillagersEarlySpawns)                                                                               \
    X(PokteRoomsVillagersByBalanceSpawns)                                                                           \
    X(PokteRoomsHouseVillagerRecordA)                                                                               \
    X(PokteRoomsHouseVillagerRecordB)                                                                               \
    X(PokteRoomsHouseVillagerRecordC)                                                                               \
    X(PokteRoomsHouseVillagerRecordD)                                                                               \
    X(PokteRoomsRuinGateSpawns)                                                                                     \
    X(PokteRoomsTankRecord)                                                                                         \
    /* ---- content/manda/ruin_spawners */                                                                          \
    X(RuinSpawnersSt13DropZoneA)                                                                                    \
    X(RuinSpawnersSt13DropZoneB)                                                                                    \
    /* ---- content/manda/st24_ruin_gate */                                                                         \
    X(St24RuinGateCamera)                                                                                           \
    X(St24RuinGateSteps)                                                                                            \
    /* ---- content/manda/st25_tron_scenes */                                                                       \
    X(St25TronScenesWreckRecord)                                                                                    \
    X(St25TronScenesGateCamera)                                                                                     \
    X(St25TronScenesGateSteps)                                                                                      \
    X(St25TronScenesGateEyesTable)                                                                                  \
    X(St25TronScenesGateMouthTable)                                                                                 \
    X(St25TronScenesAfterCamera)                                                                                    \
    X(St25TronScenesAfterSteps)                                                                                     \
    X(St25TronScenesAfterEyesTable)                                                                                 \
    X(St25TronScenesAfterMouthTable)                                                                                \
    X(St25TronScenesTronEyesTable)                                                                                  \
    X(St25TronScenesTronMouthTable)                                                                                 \
    X(St25TronScenesTronCues)                                                                                       \
    X(St25TronScenesTankCues)                                                                                       \
    X(St25TronScenesFigureEyesTable)                                                                                \
    X(St25TronScenesFigureMouthTable)                                                                               \
    /* ---- content/calbania/appo_dah_a */                                                                          \
    X(AppoDahAShortPath)                                                                                            \
    X(AppoDahALongPath)                                                                                             \
    X(AppoDahAGuideBox)                                                                                             \
    X(AppoDahAGuideLiftBox)                                                                                         \
    X(AppoDahAGatePuppetBox)                                                                                        \
    /* ---- content/calbania/appo_dah_b */                                                                          \
    X(AppoDahBFightBox)                                                                                             \
    X(AppoDahBFightLiftBox)                                                                                         \
    X(AppoDahBWaypoints)                                                                                            \
    X(AppoDahBScript1dPuppetBox)                                                                                    \
    X(AppoDahBScript1fPuppetBox)                                                                                    \
    X(AppoDahBBombBox)                                                                                              \
    /* ---- content/calbania/base_explosion_script */                                                               \
    X(BaseExplosionScriptCameraEscaped)                                                                             \
    X(BaseExplosionScriptCameraNotEscaped)                                                                          \
    X(BaseExplosionScriptSteps)                                                                                     \
    /* ---- content/calbania/calbania_arrival_script */                                                             \
    X(CalbaniaArrivalScriptCameraScript)                                                                            \
    X(CalbaniaArrivalScriptSteps)                                                                                   \
    /* ---- content/calbania/calbania_cast */                                                                       \
    X(CalbaniaCastWalkingPuppetBox)                                                                                 \
    X(CalbaniaCastSittingPuppetBox)                                                                                 \
    X(CalbaniaCastBirdBox)                                                                                          \
    X(CalbaniaCastBirdLiftBox)                                                                                      \
    X(CalbaniaCastStandingCastBox)                                                                                  \
    /* ---- content/calbania/calbania_departure_script */                                                           \
    X(CalbaniaDepartureScriptCamera)                                                                                \
    X(CalbaniaDepartureScriptSteps)                                                                                 \
    X(CalbaniaDepartureScriptRecordFigure)                                                                          \
    X(CalbaniaDepartureScriptRecordShip)                                                                            \
    X(CalbaniaDepartureScriptRecordFlight)                                                                          \
    /* ---- content/calbania/calbania_rooms */                                                                      \
    X(CalbaniaRoomsSt21Rooms)                                                                                       \
    X(CalbaniaRoomsSt21Links)                                                                                       \
    X(CalbaniaRoomsSt21LaterRooms)                                                                                  \
    X(CalbaniaRoomsSt21LaterLinks)                                                                                  \
    X(CalbaniaRoomsSt22Rooms)                                                                                       \
    X(CalbaniaRoomsSt22Links)                                                                                       \
    X(CalbaniaRoomsSt22LaterRooms)                                                                                  \
    X(CalbaniaRoomsSt22LaterLinks)                                                                                  \
    X(CalbaniaRoomsSt1fRooms)                                                                                       \
    X(CalbaniaRoomsSt1fLinks)                                                                                       \
    X(CalbaniaRoomsSt1fLaterRooms)                                                                                  \
    X(CalbaniaRoomsSt1fLaterLinks)                                                                                  \
    X(CalbaniaRoomsSt20Rooms)                                                                                       \
    X(CalbaniaRoomsSt20Links)                                                                                       \
    X(CalbaniaRoomsSt20LaterRooms)                                                                                  \
    X(CalbaniaRoomsSt20LaterLinks)                                                                                  \
    X(CalbaniaRoomsSt23Rooms)                                                                                       \
    X(CalbaniaRoomsSt23Links)                                                                                       \
    X(CalbaniaRoomsSt23LaterRooms)                                                                                  \
    X(CalbaniaRoomsSt23LaterLinks)                                                                                  \
    X(CalbaniaRoomsSt24Rooms)                                                                                       \
    X(CalbaniaRoomsSt24Links)                                                                                       \
    X(CalbaniaRoomsSt25Rooms)                                                                                       \
    X(CalbaniaRoomsSt25Links)                                                                                       \
    X(CalbaniaRoomsSt32Rooms)                                                                                       \
    X(CalbaniaRoomsSt32Links)                                                                                       \
    /* ---- content/calbania/calbania_spawners */                                                                   \
    /* the records of the fight (exe data): the pair, the leader, the guards */                                     \
    X(CalbaniaSpawnersDahRecord)                                                                                    \
    X(CalbaniaSpawnersAppoRecord) /* 0x14 bytes */                                                                  \
    X(CalbaniaSpawnersLeaderRecord) /* 0x18 bytes */                                                                \
    X(CalbaniaSpawnersFirstGuardRecord)                                                                             \
    X(CalbaniaSpawnersLastGuardRecord)                                                                              \
    X(CalbaniaSpawnersSt21AppoRecord)                                                                               \
    X(CalbaniaSpawnersSt21AppoPoint)                                                                                \
    X(CalbaniaSpawnersSt21DahRecord)                                                                                \
    X(CalbaniaSpawnersSt21DahPoint)                                                                                 \
    X(CalbaniaSpawnersSt21BirdRecord)                                                                               \
    X(CalbaniaSpawnersSt21BirdPoints) /* three pairs of points, 0x10 apart */                                       \
    X(CalbaniaSpawnersSt22AppoRecord)                                                                               \
    X(CalbaniaSpawnersSt22AppoPoint)                                                                                \
    X(CalbaniaSpawnersSt22DahRecord)                                                                                \
    X(CalbaniaSpawnersSt22DahPoint)                                                                                 \
    /* ---- content/calbania/calbania_story_rooms */                                                                \
    X(CalbaniaStoryRoomsSquareDefenceWonSpawns)                                                                     \
    X(CalbaniaStoryRoomsTownLastPhaseSpawns)                                                                        \
    X(CalbaniaStoryRoomsTownStory8Spawns)                                                                           \
    X(CalbaniaStoryRoomsTownFolkMovedSpawns)                                                                        \
    X(CalbaniaStoryRoomsTownFolkSpawns)                                                                             \
    X(CalbaniaStoryRoomsTownLateSpawns)                                                                             \
    X(CalbaniaStoryRoomsSquareDepartureSpawns)                                                                      \
    X(CalbaniaStoryRoomsFlutterRecord)                                                                              \
    /* ---- content/calbania/calbania_town_scripts */                                                               \
    X(CalbaniaTownScriptsFirstVisitCamera)                                                                          \
    X(CalbaniaTownScriptsFirstVisitSteps)                                                                           \
    X(CalbaniaTownScriptsFirstVisitEyes)                                                                            \
    X(CalbaniaTownScriptsFirstVisitMouth)                                                                           \
    X(CalbaniaTownScriptsTownReturnCamera)                                                                          \
    X(CalbaniaTownScriptsTownReturnSteps)                                                                           \
    X(CalbaniaTownScriptsTownReturnEyes)                                                                            \
    X(CalbaniaTownScriptsTownReturnMouth)                                                                           \
    /* ---- content/calbania/carried_npc */                                                                         \
    X(CarriedNpcCastTracks0)                                                                                        \
    X(CarriedNpcCastTracks1) /* the cast's keyframe track tables */                                                 \
    /* ---- content/calbania/ch3_script_cast */                                                                     \
    X(Ch3ScriptCastScript69CastBox)                                                                                 \
    X(Ch3ScriptCastScript69Eyes)                                                                                    \
    X(Ch3ScriptCastScript69Mouth)                                                                                   \
    X(Ch3ScriptCastScript20AppoDahBox)                                                                              \
    X(Ch3ScriptCastScript3cCastBox)                                                                                 \
    /* ---- content/calbania/glyde_base_bombers */                                                                  \
    X(GlydeBaseBombersBomberBox)                                                                                    \
    /* ---- content/calbania/glyde_base_gun */                                                                      \
    X(GlydeBaseGunBox)                                                                                              \
    X(GlydeBaseGunInteractionBox)                                                                                   \
    /* ---- content/calbania/glyde_base_key */                                                                      \
    X(GlydeBaseKeyBox)                                                                                              \
    /* ---- content/calbania/glyde_base_scene_birdbots */                                                           \
    X(GlydeBaseSceneBirdbotsSceneBox)                                                                               \
    X(GlydeBaseSceneBirdbotsCellBox)                                                                                \
    /* ---- content/calbania/glyde_base_scripts */                                                                  \
    X(GlydeBaseScriptsEntranceCamera)                                                                               \
    X(GlydeBaseScriptsEntranceSteps)                                                                                \
    X(GlydeBaseScriptsEntranceEyes)                                                                                 \
    X(GlydeBaseScriptsEntranceMouth)                                                                                \
    X(GlydeBaseScriptsEntranceRecord)                                                                               \
    X(GlydeBaseScriptsHeldCamera)                                                                                   \
    X(GlydeBaseScriptsHeldSteps)                                                                                    \
    X(GlydeBaseScriptsFreedCamera)                                                                                  \
    X(GlydeBaseScriptsFreedSteps)                                                                                   \
    X(GlydeBaseScriptsFreedEyes)                                                                                    \
    X(GlydeBaseScriptsFreedMouth)                                                                                   \
    X(GlydeBaseScriptsRescueCamera)                                                                                 \
    X(GlydeBaseScriptsRescueSteps)                                                                                  \
    X(GlydeBaseScriptsRescueEyes)                                                                                   \
    X(GlydeBaseScriptsRescueMouth)                                                                                  \
    X(GlydeBaseScriptsHeardRecord)                                                                                  \
    X(GlydeBaseScriptsFoundRecordA)                                                                                 \
    X(GlydeBaseScriptsFoundRecordB)                                                                                 \
    /* ---- content/calbania/glyde_base_tank */                                                                     \
    X(GlydeBaseTankBox)                                                                                             \
    /* ---- content/calbania/glyde_base_turret */                                                                   \
    X(GlydeBaseTurretBox)                                                                                           \
    /* ---- content/calbania/island_blown_script */                                                                 \
    X(IslandBlownScriptBlownCamera)                                                                                 \
    X(IslandBlownScriptSteps)                                                                                       \
    /* ---- content/calbania/nino_return_rooms */                                                                   \
    X(NinoReturnRoomsReturnRoomSpawns)                                                                              \
    X(NinoReturnRoomsReturnRoomSecondSpawns)                                                                        \
    X(NinoReturnRoomsReturnRoomReturnedSpawns)                                                                      \
    X(NinoReturnRoomsReturnRoomWaitingSpawns)                                                                       \
    X(NinoReturnRoomsAttackRoomSpawns)                                                                              \
    X(NinoReturnRoomsShoreSpawns)                                                                                   \
    X(NinoReturnRoomsMachineSpawns)                                                                                 \
    /* ---- content/calbania/nino_return_scripts */                                                                 \
    X(NinoReturnScriptsReturnCamera)                                                                                \
    X(NinoReturnScriptsReturnSteps)                                                                                 \
    X(NinoReturnScriptsReturnEyes)                                                                                  \
    X(NinoReturnScriptsReturnMouth)                                                                                 \
    X(NinoReturnScriptsMachineCamera)                                                                               \
    X(NinoReturnScriptsMachineSteps)                                                                                \
    X(NinoReturnScriptsMachineEyes)                                                                                 \
    X(NinoReturnScriptsMachineMouth)                                                                                \
    X(NinoReturnScriptsEndCamera)                                                                                   \
    X(NinoReturnScriptsEndSteps)                                                                                    \
    X(NinoReturnScriptsEndEyes)                                                                                     \
    X(NinoReturnScriptsEndMouth)                                                                                    \
    /* ---- content/calbania/square_defence_scripts */                                                              \
    X(SquareDefenceScriptsThirdPhaseDahFirstRecord)                                                                 \
    X(SquareDefenceScriptsThirdPhaseFirstRecord)                                                                    \
    X(SquareDefenceScriptsThirdPhaseDahSecondRecord)                                                                \
    X(SquareDefenceScriptsThirdPhaseSecondRecord)                                                                   \
    X(SquareDefenceScriptsLiftDahFirstRecord)                                                                       \
    X(SquareDefenceScriptsLiftFirstRecord)                                                                          \
    X(SquareDefenceScriptsLiftDahSecondRecord)                                                                      \
    X(SquareDefenceScriptsLiftSecondRecord)                                                                         \
    X(SquareDefenceScriptsOpeningCamera)                                                                            \
    X(SquareDefenceScriptsOpeningSteps)                                                                             \
    X(SquareDefenceScriptsOpeningEyes)                                                                              \
    X(SquareDefenceScriptsOpeningMouth)                                                                             \
    X(SquareDefenceScriptsOpeningAppo)                                                                              \
    X(SquareDefenceScriptsOpeningDah)                                                                               \
    X(SquareDefenceScriptsSecondCamera)                                                                             \
    X(SquareDefenceScriptsSecondSteps)                                                                              \
    X(SquareDefenceScriptsSecondEyes)                                                                               \
    X(SquareDefenceScriptsSecondMouth)                                                                              \
    X(SquareDefenceScriptsSecondWithAppo)                                                                           \
    X(SquareDefenceScriptsSecondWithoutAppo)                                                                        \
    X(SquareDefenceScriptsThirdCamera)                                                                              \
    X(SquareDefenceScriptsThirdSteps)                                                                               \
    X(SquareDefenceScriptsThirdEyes)                                                                                \
    X(SquareDefenceScriptsThirdMouth)                                                                               \
    X(SquareDefenceScriptsLiftCamera)                                                                               \
    X(SquareDefenceScriptsLiftSteps)                                                                                \
    X(SquareDefenceScriptsLiftEyes)                                                                                 \
    X(SquareDefenceScriptsLiftMouth)                                                                                \
    /* ---- content/calbania/st1f_birdbot_tank */                                                                   \
    X(St1fBirdbotTankBox)                                                                                           \
    /* ---- content/calbania/st21_props */                                                                          \
    X(St21PropsLurePieceBox)                                                                                        \
    X(St21PropsLurePieceLiftBox)                                                                                    \
    X(St21PropsRisingModelBox)                                                                                      \
    /* ---- content/calbania/st22_burrower */                                                                       \
    X(St22BurrowerBox)                                                                                              \
    /* ---- content/calbania/st22_fire_shots */                                                                     \
    X(St22FireShotsFireShotBoxes)                                                                                   \
    /* ---- content/calbania/st24_npc */                                                                            \
    X(St24NpcSpeedsByVariant)                                                                                       \
    X(St24NpcLookSets) /* u32 per kind: 8 look modes to pick from (exe 0x445040's table) */                         \
    X(St24NpcRouteNpcBox)                                                                                           \
    X(St24NpcTalkBox)                                                                                               \
    X(St24NpcSmallTalkBox)                                                                                          \
    /* ---- content/calbania/st24_script56 */                                                                       \
    X(St24Script56CameraScript)                                                                                     \
    X(St24Script56Steps)                                                                                            \
    /* ---- content/calbania/st32_quiz */                                                                           \
    X(St32QuizQuestionBank) /* the questions' bank */                                                               \
    X(St32QuizHostRecords) /* the host of each course (0x14 apart) */                                               \
    X(St32QuizSecondHostRecord) /* course 1's second actor */                                                       \
    X(St32QuizStageRecord) /* the stage set */                                                                      \
    X(St32QuizContestantRecords)                                                                                    \
    X(St32QuizPlateRecord)                                                                                          \
    X(St32QuizCameraScript)                                                                                         \
    X(St32QuizSteps)                                                                                                \
    X(St32QuizCastBox)                                                                                              \
    X(St32QuizCastTalkBox)                                                                                          \
    /* ---- content/calbania/st32_rooms */                                                                          \
    X(St32RoomsContestantRecords) /* the three contestants' spawn records (0x14 apart) */                           \
    X(St32RoomsQuizPlateRecord) /* the quiz's plate */                                                              \
    /* ---- content/calbania/talking_npc */                                                                         \
    X(TalkingNpcHintsBank) /* the exe's bank of the hints window (message 0) */                                     \
    /* ---- content/yosyonke/abandoned_mine_exit */                                                                 \
    X(AbandonedMineExitCameraScript)                                                                                \
    X(AbandonedMineExitSteps)                                                                                       \
    X(AbandonedMineExitFaceEyes)                                                                                    \
    X(AbandonedMineExitFaceMouth)                                                                                   \
    X(AbandonedMineExitGuideRecord) /* the script actor freed at the end */                                         \
    /* ---- content/yosyonke/abandoned_mine_rooms */                                                                \
    X(AbandonedMineRoomsRollFirstRecord)                                                                            \
    X(AbandonedMineRoomsRollRecord)                                                                                 \
    X(AbandonedMineRoomsRollAfterRideRecord)                                                                        \
    X(AbandonedMineRoomsHallCastRecords) /* sub 11's device, glow and relic (x 3) */                                \
    /* ---- content/yosyonke/abandoned_mine_scripts */                                                              \
    /* the camera scripts and the step lists (exe data) */                                                          \
    X(AbandonedMineScriptsLiftCamera)                                                                               \
    X(AbandonedMineScriptsLiftSteps)                                                                                \
    X(AbandonedMineScriptsHallCamera)                                                                               \
    X(AbandonedMineScriptsHallSteps)                                                                                \
    X(AbandonedMineScriptsEntranceCamera)                                                                           \
    X(AbandonedMineScriptsEntranceSteps)                                                                            \
    X(AbandonedMineScriptsEndCamera)                                                                                \
    X(AbandonedMineScriptsEndSteps)                                                                                 \
    X(AbandonedMineScriptsDeviceCamera)                                                                             \
    X(AbandonedMineScriptsDeviceSteps)                                                                              \
    X(AbandonedMineScriptsHallCastRecords)                                                                          \
    X(AbandonedMineScriptsDeviceCastRecords)                                                                        \
    /* ---- content/yosyonke/arrival */                                                                             \
    X(ArrivalDescentCameraScript)                                                                                   \
    X(ArrivalDescentSteps)                                                                                          \
    X(ArrivalCabinCameraScript)                                                                                     \
    X(ArrivalCabinSteps)                                                                                            \
    X(ArrivalCabinEyes)                                                                                             \
    X(ArrivalCabinMouth)                                                                                            \
    /* ---- content/yosyonke/dev_room */                                                                            \
    /* the model's places in front of the camera (Roll's 0x8aee88, Nino's copy 0x8af4d8: s16 x / y / z by           \
       weapon << 4) (an s16 triple: dataShortVec3) */                                                               \
    X(DevRoomRollCopyPrices)                                                                                        \
    X(DevRoomNinoCopyPrices)                                                                                        \
    X(DevRoomRecipes)                                                                                               \
    X(DevRoomRollModelPlaces)                                                                                       \
    X(DevRoomNinoModelPlaces)                                                                                       \
    X(DevRoomRollRecord)                                                                                            \
    X(DevRoomRollCamera)                                                                                            \
    X(DevRoomRollSteps)                                                                                             \
    X(DevRoomNinoCamera)                                                                                            \
    X(DevRoomNinoSteps)                                                                                             \
    /* ---- content/yosyonke/flutter_cabin */                                                                       \
    X(FlutterCabinAnimatedBox)                                                                                      \
    X(FlutterCabinNpcBox)                                                                                           \
    /* ---- content/yosyonke/ice_ruin_blade_barrier */                                                              \
    X(IceRuinBladeBarrierBoxes)                                                                                     \
    /* ---- content/yosyonke/ice_ruin_burrowing_shooter */                                                          \
    X(IceRuinBurrowingShooterInteraction)                                                                           \
    X(IceRuinBurrowingShooterShotFlyBox)                                                                            \
    /* ---- content/yosyonke/ice_ruin_critters */                                                                   \
    X(IceRuinCrittersCritterLiftBox)                                                                                \
    X(IceRuinCrittersSceneCritterBox)                                                                               \
    X(IceRuinCrittersCatchableBox)                                                                                  \
    /* ---- content/yosyonke/ice_ruin_flag_barrier */                                                               \
    X(IceRuinFlagBarrierBox)                                                                                        \
    /* ---- content/yosyonke/ice_ruin_rooms */                                                                      \
    X(IceRuinRoomsCritterRecords)                                                                                   \
    X(IceRuinRoomsSub5Record)                                                                                       \
    X(IceRuinRoomsSub7Record)                                                                                       \
    X(IceRuinRoomsSub8Record)                                                                                       \
    /* ---- content/yosyonke/ice_ruin_scripts */                                                                    \
    X(IceRuinScriptsShrinkFirstCameraScript)                                                                        \
    X(IceRuinScriptsShrinkFirstSteps)                                                                               \
    X(IceRuinScriptsShrinkSecondCameraScript)                                                                       \
    X(IceRuinScriptsShrinkSecondSteps)                                                                              \
    X(IceRuinScriptsCritterRecord0)                                                                                 \
    X(IceRuinScriptsCritterRecord1)                                                                                 \
    X(IceRuinScriptsCritterRecord2)                                                                                 \
    X(IceRuinScriptsCritterSceneCamera)                                                                             \
    X(IceRuinScriptsCritterSceneSteps)                                                                              \
    X(IceRuinScriptsTutorialCamera)                                                                                 \
    X(IceRuinScriptsTutorialSteps)                                                                                  \
    X(IceRuinScriptsTutorialCastRecord)                                                                             \
    X(IceRuinScriptsTutorialEndRecord)                                                                              \
    X(IceRuinScriptsTutorialSceneCamera)                                                                            \
    X(IceRuinScriptsTutorialSceneSteps)                                                                             \
    /* ---- content/yosyonke/ice_ruin_shrink_block */                                                               \
    X(IceRuinShrinkBlockShrinkBox)                                                                                  \
    /* ---- content/yosyonke/roll_spawners */                                                                       \
    X(RollSpawnersFormationPicks)                                                                                   \
    /* ---- content/yosyonke/st05_entry_list */                                                                     \
    X(St05EntryListEntryTexts) /* the list's windows and the entries' texts (bank) */                               \
    X(St05EntryListEntryNames) /* the entries' names (bank) */                                                      \
    X(St05EntryListEntryCameraScript)                                                                               \
    X(St05EntryListEntrySteps)                                                                                      \
    /* ---- content/yosyonke/st0e_servbots */                                                                       \
    X(St0eServbotsRiderAnchorOffsets)                                                                               \
    /* ---- content/yosyonke/st0e_spawner */                                                                        \
    X(St0eSpawnerEastFormationPicks)                                                                                \
    X(St0eSpawnerWestFormationPicks) /* exe data (u8[32] each) */                                                   \
    /* ---- content/yosyonke/st47_rooms */                                                                          \
    X(St47RoomsLightRecord)                                                                                         \
    X(St47RoomsCastRecords)                                                                                         \
    X(St47RoomsSub1Record)                                                                                          \
    X(St47RoomsSub2Record)                                                                                          \
    /* ---- content/yosyonke/town_room_cases */                                                                     \
    X(TownRoomCasesSt05Sub0FirstStatic)                                                                             \
    X(TownRoomCasesSt05Sub0SecondStatic)                                                                            \
    X(TownRoomCasesSt05Sub1FirstStatic)                                                                             \
    X(TownRoomCasesSt05Sub1SecondStatic)                                                                            \
    X(TownRoomCasesSt05Sub1ThirdStatic)                                                                             \
    X(TownRoomCasesSt05Sub1FourthStatic)                                                                            \
    X(TownRoomCasesSt05Sub1ActorRecord)                                                                             \
    X(TownRoomCasesSt05Sub2FirstStatic)                                                                             \
    X(TownRoomCasesSt05Sub2SecondStatic)                                                                            \
    X(TownRoomCasesSt0BOfficeSpawns)                                                                                \
    X(TownRoomCasesSt0ASub3Spawns)                                                                                  \
    X(TownRoomCasesSt0ASub4Spawns)                                                                                  \
    X(TownRoomCasesSt0ASub4MoreSpawns)                                                                              \
    X(TownRoomCasesSt0BEntranceSpawns)                                                                              \
    X(TownRoomCasesSt0BOfficeEarlySpawns)                                                                           \
    X(TownRoomCasesSt0BOfficeLaterSpawns)                                                                           \
    X(TownRoomCasesSt0CSub0Spawns)                                                                                  \
    X(TownRoomCasesSt0CSub1Spawns)                                                                                  \
    X(TownRoomCasesSt0CSub1MoreSpawns)                                                                              \
    X(TownRoomCasesSt0CSub2Spawns)                                                                                  \
    X(TownRoomCasesSt0CSub2MoreSpawns)                                                                              \
    /* ---- content/yosyonke/town_rooms */                                                                          \
    /* ST0A sub 2's UV animations: the list exe 0x5f5bd0 fills, the room's animation table (exe data) */            \
    X(TownRoomsJunkShopClerkSpawns)                                                                                 \
    X(TownRoomsJunkShopCustomerSpawns)                                                                              \
    X(TownRoomsJunkShopCustomerMoreSpawns)                                                                          \
    X(TownRoomsJunkShopSecondCustomerSpawns)                                                                        \
    X(TownRoomsJunkShopThirdCustomerSpawns)                                                                         \
    X(TownRoomsFlutterCastSpawns)                                                                                   \
    X(TownRoomsJoesLabSpawns)                                                                                       \
    X(TownRoomsHangarDropshipRecord)                                                                                \
    X(TownRoomsSt09WalkerSpawns)                                                                                    \
    X(TownRoomsSt09OtherWalkerSpawns)                                                                               \
    X(TownRoomsSt0cRooms)                                                                                           \
    X(TownRoomsSt0cLinks)                                                                                           \
    X(TownRoomsSt0dRooms)                                                                                           \
    X(TownRoomsSt0dLinks)                                                                                           \
    X(TownRoomsSt0bRooms)                                                                                           \
    X(TownRoomsSt0bLinks)                                                                                           \
    X(TownRoomsSt0eRooms)                                                                                           \
    X(TownRoomsSt0eLinks)                                                                                           \
    X(TownRoomsSt08Rooms)                                                                                           \
    X(TownRoomsSt08Links)                                                                                           \
    X(TownRoomsSt09Rooms)                                                                                           \
    X(TownRoomsSt09Links)                                                                                           \
    X(TownRoomsSt0aRooms)                                                                                           \
    X(TownRoomsSt0aLinks)                                                                                           \
    X(TownRoomsSt47Rooms)                                                                                           \
    X(TownRoomsSt47Links)                                                                                           \
    X(TownRoomsSt0aUvAnimList)                                                                                      \
    X(TownRoomsSt0aUvAnimTable)                                                                                     \
    X(TownRoomsCabinPair)                                                                                           \
    X(TownRoomsCabinGuest)                                                                                          \
    /* ---- content/yosyonke/town_side_scripts */                                                                   \
    X(TownSideScriptsExamDoorsOutCameraScript)                                                                      \
    X(TownSideScriptsExamDoorsSteps)                                                                                \
    X(TownSideScriptsExamDoorsOutRightLeaf)                                                                         \
    X(TownSideScriptsExamDoorsOutLeftLeaf)                                                                          \
    X(TownSideScriptsExamDoorsInCameraScript)                                                                       \
    X(TownSideScriptsExamDoorsInLeftLeaf)                                                                           \
    X(TownSideScriptsExamDoorsInRightLeaf)                                                                          \
    X(TownSideScriptsSt06VoiceCameraScript)                                                                         \
    X(TownSideScriptsSt06VoiceSteps)                                                                                \
    X(TownSideScriptsBystanderCameraScript)                                                                         \
    X(TownSideScriptsBystanderSteps)                                                                                \
    X(TownSideScriptsBystanderEyes)                                                                                 \
    X(TownSideScriptsBystanderMouth)                                                                                \
    X(TownSideScriptsVisitorRecord)                                                                                 \
    X(TownSideScriptsVisitorCameraScript)                                                                           \
    X(TownSideScriptsVisitorSteps)                                                                                  \
    X(TownSideScriptsVisitorEyes)                                                                                   \
    X(TownSideScriptsVisitorMouth)                                                                                  \
    X(TownSideScriptsBystanderBox)                                                                                  \
    X(TownSideScriptsVisitorBox)                                                                                    \
    /* ---- content/yosyonke/town_story */                                                                          \
    X(TownStoryJunkShopSceneRecord)                                                                                 \
    X(TownStoryRollRunsOffRecord)                                                                                   \
    X(TownStoryJoesLabSceneFirstRecord)                                                                             \
    X(TownStoryJoesLabSceneSecondRecord)                                                                            \
    X(TownStoryJoesLabRollRecord)                                                                                   \
    X(TownStoryJoesLabMariaRecord)                                                                                  \
    X(TownStoryTurnToDoorRecord)                                                                                    \
    X(TownStoryRollLeavesRecord)                                                                                    \
    X(TownStoryJunkShopCameraScript)                                                                                \
    X(TownStoryJunkShopSteps)                                                                                       \
    X(TownStoryJunkShopEyes)                                                                                        \
    X(TownStoryJunkShopMouth)                                                                                       \
    X(TownStoryRollRunsOffCameraScript)                                                                             \
    X(TownStoryRollRunsOffSteps)                                                                                    \
    X(TownStoryRollRunsOffEyes)                                                                                     \
    X(TownStoryRollRunsOffMouth)                                                                                    \
    X(TownStoryJoesLabCameraScript)                                                                                 \
    X(TownStoryJoesLabSteps)                                                                                        \
    X(TownStoryJoesLabEyes)                                                                                         \
    X(TownStoryJoesLabMouth)                                                                                        \
    X(TownStoryTrainLostCameraScript)                                                                               \
    X(TownStoryTrainLostSteps)                                                                                      \
    X(TownStoryJoesLabPropSpawns)                                                                                   \
    X(TownStoryJoesLabFirstTiltedProp)                                                                              \
    X(TownStoryJoesLabSecondTiltedProp)                                                                             \
    X(TownStoryJoesLabAfterSceneSpawns)                                                                             \
    X(TownStoryTrainAttackFirstSpawns)                                                                              \
    X(TownStoryTrainAttackSecondSpawns)                                                                             \
    X(TownStoryRuinEntranceSpawns)                                                                                  \
    X(TownStorySt0DWalkerSpawns)                                                                                    \
    X(TownStorySt0DWalkersSpawns)                                                                                   \
    X(TownStorySt0DLoneWalkerSpawns)                                                                                \
    /* ---- content/yosyonke/town_story_cast */                                                                     \
    X(TownStoryCastJunkShopBox)                                                                                     \
    X(TownStoryCastRunningRollBox)                                                                                  \
    X(TownStoryCastJoesLabBox)                                                                                      \
    X(TownStoryCastSt47PersonBox)                                                                                   \
    /* ---- content/nino/bars_and_gate */                                                                           \
    X(BarsAndGateTimedBarDown)                                                                                      \
    X(BarsAndGateTimedBarReady) /* the light settings */                                                            \
    X(BarsAndGateLeverIdle)                                                                                         \
    X(BarsAndGateLeverReady)                                                                                        \
    X(BarsAndGateGateLeverDown)                                                                                     \
    X(BarsAndGateGateLeverReady) /* the light settings */                                                           \
    /* ---- content/nino/carrier_boat */                                                                            \
    X(CarrierBoatFirePlaceA)                                                                                        \
    X(CarrierBoatFirePlaceB)                                                                                        \
    /* ---- content/nino/ch2_rooms */                                                                               \
    X(Ch2RoomsSt19FirstLockRecord)                                                                                  \
    X(Ch2RoomsSt19SecondLockRecord)                                                                                 \
    X(Ch2RoomsSt19ThirdLockRecord)                                                                                  \
    X(Ch2RoomsSt18ShipFightSpawns)                                                                                  \
    X(Ch2RoomsSt17LateSpawns)                                                                                       \
    X(Ch2RoomsSt1BArrivalSpawns)                                                                                    \
    X(Ch2RoomsSt1BPlantSpawns)                                                                                      \
    X(Ch2RoomsLockLightA)                                                                                           \
    X(Ch2RoomsLockLightB)                                                                                           \
    X(Ch2RoomsLockLightC)                                                                                           \
    /* ---- content/nino/ch2_script_cast */                                                                         \
    X(Ch2ScriptCastMouthPartsA)                                                                                     \
    X(Ch2ScriptCastMouthPartsB)                                                                                     \
    X(Ch2ScriptCastScript32CastModels)                                                                              \
    X(Ch2ScriptCastScript32CastAnims)                                                                               \
    X(Ch2ScriptCastScript37Speaker0Table)                                                                           \
    X(Ch2ScriptCastScript37Speaker2Table)                                                                           \
    X(Ch2ScriptCastScript37Speaker3Table)                                                                           \
    X(Ch2ScriptCastScript37Speaker4Table)                                                                           \
    X(Ch2ScriptCastScript37CastModels)                                                                              \
    X(Ch2ScriptCastScript37CastAnims)                                                                               \
    X(Ch2ScriptCastTakeOffTracksA)                                                                                  \
    X(Ch2ScriptCastTakeOffTracksB)                                                                                  \
    /* ---- content/nino/ch2_scripts */                                                                             \
    X(Ch2ScriptsFlightToCityCamera)                                                                                 \
    X(Ch2ScriptsFlightToCitySteps)                                                                                  \
    X(Ch2ScriptsFlightToShipsCamera)                                                                                \
    X(Ch2ScriptsFlightToShipsSteps)                                                                                 \
    X(Ch2ScriptsCityGateCamera)                                                                                     \
    X(Ch2ScriptsCityGateSteps)                                                                                      \
    X(Ch2ScriptsCraneFirstEnemyRecord)                                                                              \
    X(Ch2ScriptsCraneSecondEnemyRecord)                                                                             \
    X(Ch2ScriptsCraneDoneCamera)                                                                                    \
    X(Ch2ScriptsCraneDoneSteps)                                                                                     \
    X(Ch2ScriptsCraneDoneEyes)                                                                                      \
    X(Ch2ScriptsCraneDoneMouth)                                                                                     \
    X(Ch2ScriptsCannonDoneCamera)                                                                                   \
    X(Ch2ScriptsCannonDoneSteps)                                                                                    \
    X(Ch2ScriptsCannonDoneEyes)                                                                                     \
    X(Ch2ScriptsCannonDoneMouth)                                                                                    \
    X(Ch2ScriptsLookAroundCamera)                                                                                   \
    X(Ch2ScriptsLookAroundSteps)                                                                                    \
    X(Ch2ScriptsLookAroundSpawns)                                                                                   \
    X(Ch2ScriptsShipsDoneCamera)                                                                                    \
    X(Ch2ScriptsShipsDoneSteps)                                                                                     \
    X(Ch2ScriptsShipsDoneEyes)                                                                                      \
    X(Ch2ScriptsShipsDoneMouth)                                                                                     \
    X(Ch2ScriptsShipFightFreedRecord)                                                                               \
    X(Ch2ScriptsShipFightCastRecords)                                                                               \
    X(Ch2ScriptsShipFightCamera)                                                                                    \
    X(Ch2ScriptsShipFightSteps)                                                                                     \
    X(Ch2ScriptsShipFightSpawns)                                                                                    \
    X(Ch2ScriptsShipFightMoreSpawns)                                                                                \
    X(Ch2ScriptsGunshipDownCamera)                                                                                  \
    X(Ch2ScriptsGunshipDownSteps)                                                                                   \
    X(Ch2ScriptsPuffPlaceA)                                                                                         \
    X(Ch2ScriptsPuffPlaceB)                                                                                         \
    X(Ch2ScriptsPuffDriftA)                                                                                         \
    X(Ch2ScriptsPuffDriftB)                                                                                         \
    /* ---- content/nino/field_enemies */                                                                           \
    X(FieldEnemiesRiderFirstOffset)                                                                                 \
    X(FieldEnemiesRiderSecondOffset)                                                                                \
    X(FieldEnemiesRiderRows)                                                                                        \
    X(FieldEnemiesHoldOffsets) /* by the grip pose (0: the box) */                                                  \
    /* ---- content/nino/field_spawners */                                                                          \
    X(FieldSpawnersWaveRows)                                                                                        \
    X(FieldSpawnersWaveTargets)                                                                                     \
    X(FieldSpawnersCarrierEntry0)                                                                                   \
    X(FieldSpawnersCarrierEntry1)                                                                                   \
    X(FieldSpawnersCarrierEntry2)                                                                                   \
    X(FieldSpawnersCarrierEntry3)                                                                                   \
    X(FieldSpawnersCarrierTargets)                                                                                  \
    X(FieldSpawnersSt15GroupRecords)                                                                                \
    X(FieldSpawnersEscortTargetRecords)                                                                             \
    X(FieldSpawnersSt16GroupRecords)                                                                                \
    X(FieldSpawnersSt16HolderRecord)                                                                                \
    X(FieldSpawnersSt16CarrierRecords)                                                                              \
    /* ---- content/nino/fleeing_reaverbot */                                                                       \
    X(FleeingReaverbotSizeRows)                                                                                     \
    /* ---- content/nino/glyde_attack_cast */                                                                       \
    X(GlydeAttackCastDescenderCells)                                                                                \
    X(GlydeAttackCastRiserCells)                                                                                    \
    X(GlydeAttackCastRiserSecondCells)                                                                              \
    /* ---- content/nino/glyde_attack_talkers */                                                                    \
    X(GlydeAttackTalkersSt17TalkerEyes)                                                                             \
    X(GlydeAttackTalkersSt17TalkerMouth)                                                                            \
    X(GlydeAttackTalkersSt18TalkerEyes)                                                                             \
    X(GlydeAttackTalkersSt18TalkerMouth)                                                                            \
    /* ---- content/nino/glyde_missiles */                                                                          \
    X(GlydeMissilesLaunchScript)                                                                                    \
    X(GlydeMissilesLaunchLoop)                                                                                      \
    /* ---- content/nino/glyde_ship_cast */                                                                         \
    X(GlydeShipCastFirstBlastOffset)                                                                                \
    X(GlydeShipCastSecondBlastOffset)                                                                               \
    X(GlydeShipCastThirdBlastOffset)                                                                                \
    X(GlydeShipCastExplosions)                                                                                      \
    X(GlydeShipCastDebrisLaunches)                                                                                  \
    /* ---- content/nino/guardian_cast */                                                                           \
    X(GuardianCastGuardianBox)                                                                                      \
    X(GuardianCastGuardianDissolvingBox)                                                                            \
    /* ---- content/nino/mission_gunships */                                                                        \
    X(MissionGunshipsSt15GunshipBox)                                                                                \
    X(MissionGunshipsSt15GunshipProbeBox)                                                                           \
    X(MissionGunshipsSt15GunshipCruiseTicks)                                                                        \
    X(MissionGunshipsSt15GunshipMargins)                                                                            \
    X(MissionGunshipsSt15GunshipRunTicks)                                                                           \
    X(MissionGunshipsSt15GunshipBurstTicks)                                                                         \
    X(MissionGunshipsSt15GunshipReinforcements)                                                                     \
    X(MissionGunshipsWaveGunshipBox)                                                                                \
    X(MissionGunshipsWaveGunshipProbeBox)                                                                           \
    X(MissionGunshipsWaveGunshipCruiseTicks)                                                                        \
    X(MissionGunshipsWaveGunshipMargins)                                                                            \
    X(MissionGunshipsWaveGunshipRunTicks)                                                                           \
    X(MissionGunshipsWaveGunshipBurstTicks)                                                                         \
    X(MissionGunshipsWaveGunshipReinforcements)                                                                     \
    /* ---- content/nino/mission_targets */                                                                         \
    X(MissionTargetsTargetRows)                                                                                     \
    X(MissionTargetsTargetCapsules)                                                                                 \
    /* ---- content/nino/nino_rooms */                                                                              \
    X(NinoRoomsSt15Rooms)                                                                                           \
    X(NinoRoomsSt15Links)                                                                                           \
    X(NinoRoomsSt16Rooms)                                                                                           \
    X(NinoRoomsSt16Links)                                                                                           \
    X(NinoRoomsSt18Rooms)                                                                                           \
    X(NinoRoomsSt18Links)                                                                                           \
    X(NinoRoomsSt19Rooms)                                                                                           \
    X(NinoRoomsSt19Links)                                                                                           \
    X(NinoRoomsSt1ARooms)                                                                                           \
    X(NinoRoomsSt1ALinks)                                                                                           \
    X(NinoRoomsSt1BRooms)                                                                                           \
    X(NinoRoomsSt1BLinks)                                                                                           \
    X(NinoRoomsSt17Rooms)                                                                                           \
    X(NinoRoomsSt17Links)                                                                                           \
    /* ---- content/nino/nino_ruin_spawners */                                                                      \
    X(NinoRuinSpawnersSt36BubbleEntries)                                                                            \
    X(NinoRuinSpawnersSt36BubbleDivisors)                                                                           \
    X(NinoRuinSpawnersSt37BubbleEntries)                                                                            \
    X(NinoRuinSpawnersSt37BubbleDivisors)                                                                           \
    X(NinoRuinSpawnersSt38BubbleEntries)                                                                            \
    X(NinoRuinSpawnersSt38BubbleDivisors)                                                                           \
    X(NinoRuinSpawnersSt36RoomRecord)                                                                               \
    X(NinoRuinSpawnersSt36FirstRoomRecord)                                                                          \
    X(NinoRuinSpawnersSt36SecondRoomRecord)                                                                         \
    X(NinoRuinSpawnersSt36FirstRecord)                                                                              \
    X(NinoRuinSpawnersSt36SecondRecord)                                                                             \
    X(NinoRuinSpawnersSt36ThirdRecord)                                                                              \
    X(NinoRuinSpawnersSt37Sub2FirstRecord)                                                                          \
    X(NinoRuinSpawnersSt37FirstRecord)                                                                              \
    X(NinoRuinSpawnersSt37Sub2SecondRecord)                                                                         \
    X(NinoRuinSpawnersSt37SecondRecord)                                                                             \
    X(NinoRuinSpawnersSt37ThirdRecord)                                                                              \
    X(NinoRuinSpawnersSt37FourthRecord)                                                                             \
    X(NinoRuinSpawnersSt38Record)                                                                                   \
    X(NinoRuinSpawnersSt38FirstRecord)                                                                              \
    X(NinoRuinSpawnersSt38SecondRecord)                                                                             \
    /* ---- content/nino/platform_boss */                                                                           \
    X(PlatformBossWaypoints)                                                                                        \
    /* ---- content/nino/repair_machine */                                                                          \
    X(RepairMachinePropCells)                                                                                       \
    X(RepairMachinePropSecondCells)                                                                                 \
    /* ---- content/nino/ruin_guardian */                                                                           \
    X(RuinGuardianBeamRows)                                                                                         \
    X(RuinGuardianBeamProbeBox)                                                                                     \
    X(RuinGuardianLaunchPoints)                                                                                     \
    /* ---- content/nino/ruin_swimmers */                                                                           \
    X(RuinSwimmersTorpedoLiftBoxes) /* the interaction boxes, 12 bytes per variant */                               \
    /* ---- content/nino/st15_cls */                                                                                \
    X(St15ClsVariantPoints)                                                                                         \
    /* ---- content/nino/st15_props */                                                                              \
    X(St15PropsFirstFirePlace)                                                                                      \
    X(St15PropsSecondFirePlace)                                                                                     \
    X(St15PropsThirdFirePlace)                                                                                      \
    /* ---- content/nino/st16_script33 */                                                                           \
    X(St16Script33CatchCamera)                                                                                      \
    X(St16Script33CatchSteps)                                                                                       \
    /* ---- content/nino/st19 */                                                                                    \
    X(St19DropDust0)                                                                                                \
    X(St19DropDust1)                                                                                                \
    X(St19DropDust2)                                                                                                \
    X(St19DropDust3)                                                                                                \
    /* ---- content/nino/st19_state */                                                                              \
    X(St19StateSparkFrom)                                                                                           \
    X(St19StateDropSparkTo)                                                                                         \
    X(St19StateSlideSparkTo)                                                                                        \
    /* ---- content/nino/st35_rooms */                                                                              \
    X(St35RoomsWayBackSpawns)                                                                                       \
    X(St35RoomsItemSpawns)                                                                                          \
    X(St35RoomsGuardianSpawns)                                                                                      \
    X(St35RoomsGuardianWakesSpawns)                                                                                 \
    X(St35RoomsItemEventPlace) /* the event's point (exe data) */                                                   \
    /* ---- content/nino/st35_scripts */                                                                            \
    X(St35ScriptsFirstSceneCamera)                                                                                  \
    X(St35ScriptsFirstSceneSteps)                                                                                   \
    X(St35ScriptsFirstSceneEyes)                                                                                    \
    X(St35ScriptsFirstSceneMouth)                                                                                   \
    X(St35ScriptsStandingSceneCamera)                                                                               \
    X(St35ScriptsStandingSceneSteps)                                                                                \
    X(St35ScriptsStandingSceneEyes)                                                                                 \
    X(St35ScriptsStandingSceneMouth)                                                                                \
    X(St35ScriptsSecondSceneCamera)                                                                                 \
    X(St35ScriptsSecondSceneSteps)                                                                                  \
    X(St35ScriptsSecondSceneEyes)                                                                                   \
    X(St35ScriptsSecondSceneMouth)                                                                                  \
    X(St35ScriptsLastSceneCamera)                                                                                   \
    X(St35ScriptsLastSceneSteps)                                                                                    \
    X(St35ScriptsLastSceneEyes)                                                                                     \
    X(St35ScriptsLastSceneMouth)                                                                                    \
    /* ---- content/nino/st36_rooms */                                                                              \
    X(St36RoomsEffectSpawns)                                                                                        \
    /* ---- content/nino/st37_rooms */                                                                              \
    X(St37RoomsWaterRecords)                                                                                        \
    /* ---- content/nino/st37_scripts */                                                                            \
    X(St37ScriptsFloodedCast0)                                                                                      \
    X(St37ScriptsFloodedCast1)                                                                                      \
    X(St37ScriptsFloodedCast2)                                                                                      \
    X(St37ScriptsDryCast0)                                                                                          \
    X(St37ScriptsDryCast1)                                                                                          \
    X(St37ScriptsDryCast2)                                                                                          \
    X(St37ScriptsFloodedCamera)                                                                                     \
    X(St37ScriptsFloodedSteps)                                                                                      \
    X(St37ScriptsDryCamera)                                                                                         \
    X(St37ScriptsDrySteps)                                                                                          \
    X(St37ScriptsEyes)                                                                                              \
    X(St37ScriptsMouth)                                                                                             \
    /* ---- content/nino/st38_rooms */                                                                              \
    X(St38RoomsFloodedSpawns)                                                                                       \
    X(St38RoomsFloodedMoreSpawns)                                                                                   \
    X(St38RoomsDrainedSpawns)                                                                                       \
    X(St38RoomsEffectSpawns)                                                                                        \
    /* ---- content/forbidden_island/elephant_boss */                                                               \
    X(ElephantBossCrystalTalkBox)                                                                                   \
    X(ElephantBossCrystalByVariant)                                                                                 \
    X(ElephantBossScript28Camera)                                                                                   \
    X(ElephantBossScript28Steps)                                                                                    \
    X(ElephantBossScript28FirstRecord)                                                                              \
    X(ElephantBossScript28SecondRecord)                                                                             \
    X(ElephantBossScript28Spawns)                                                                                   \
    X(ElephantBossScript29Camera)                                                                                   \
    X(ElephantBossScript29Steps)                                                                                    \
    X(ElephantBossArenaCentre)                                                                                      \
    X(ElephantBossCutsceneBox)                                                                                      \
    X(ElephantBossBox)                                                                                              \
    X(ElephantBossPillarCells)                                                                                      \
    X(ElephantBossBoulderBox)                                                                                       \
    /* ---- content/forbidden_island/island_rooms */                                                                \
    X(IslandRoomsSt3DLateSub0Spawns)                                                                                \
    X(IslandRoomsSt3DLateSub1Spawns)                                                                                \
    X(IslandRoomsSt3DSub3Spawns)                                                                                    \
    X(IslandRoomsSt3DMiddleSub3Spawns)                                                                              \
    X(IslandRoomsSt3DLateSub2Spawns)                                                                                \
    X(IslandRoomsSt3FSub1Spawns)                                                                                    \
    X(IslandRoomsSt3FSub1MoreSpawns)                                                                                \
    X(IslandRoomsSt3FSub3MoreSpawns)                                                                                \
    X(IslandRoomsSt3FMiddleSub1MoreSpawns)                                                                          \
    X(IslandRoomsSt3FLateSub1MoreSpawns)                                                                            \
    X(IslandRoomsSt3FLastSub1MoreSpawns)                                                                            \
    X(IslandRoomsSt3FSpawns)                                                                                        \
    X(IslandRoomsSt3ESub1Spawns)                                                                                    \
    X(IslandRoomsSt3ESub2FirstVisitor)                                                                              \
    X(IslandRoomsSt3ESub2SecondVisitor)                                                                             \
    X(IslandRoomsSt3ESub2ThirdVisitor)                                                                              \
    X(IslandRoomsSt3ESub3Spawns)                                                                                    \
    X(IslandRoomsSt3ESub4Spawns)                                                                                    \
    X(IslandRoomsSt3ESub5EarlySpawns)                                                                               \
    X(IslandRoomsSt3ESub5LaterSpawns)                                                                               \
    X(IslandRoomsSt1DCastSpawns)                                                                                    \
    X(IslandRoomsSt1DCastMoreSpawns)                                                                                \
    X(IslandRoomsSt1DCastGroupSpawns)                                                                               \
    X(IslandRoomsSt1DCastLastSpawns)                                                                                \
    X(IslandRoomsSt1dRoomsStorm)                                                                                    \
    X(IslandRoomsSt1dLinksStorm)                                                                                    \
    X(IslandRoomsSt1dRooms)                                                                                         \
    X(IslandRoomsSt1dLinks)                                                                                         \
    X(IslandRoomsSt10RoomsStorm)                                                                                    \
    X(IslandRoomsSt10LinksStorm)                                                                                    \
    X(IslandRoomsSt10Rooms)                                                                                         \
    X(IslandRoomsSt10Links)                                                                                         \
    X(IslandRoomsSt11RoomsStorm)                                                                                    \
    X(IslandRoomsSt11LinksStorm)                                                                                    \
    X(IslandRoomsSt11Rooms)                                                                                         \
    X(IslandRoomsSt11Links)                                                                                         \
    /* ---- content/forbidden_island/island_visit */                                                                \
    X(IslandVisitBlizzardSpawns)                                                                                    \
    X(IslandVisitSt10DropshipSpawns)                                                                                \
    X(IslandVisitSt10TriggerSpawns)                                                                                 \
    X(IslandVisitSt10DecorationSpawns)                                                                              \
    X(IslandVisitSt10LaterDropshipSpawns)                                                                           \
    X(IslandVisitSt10LaterTriggerSpawns)                                                                            \
    X(IslandVisitSt10LaterDecorationSpawns)                                                                         \
    X(IslandVisitSt10LaterStaticSpawns)                                                                             \
    X(IslandVisitSt11FirstSpawns)                                                                                   \
    X(IslandVisitSt11SecondSpawns)                                                                                  \
    X(IslandVisitSt11ExitLightSpawns)                                                                               \
    X(IslandVisitSt11ReaverbotDepths)                                                                               \
    X(IslandVisitSt11ReaverbotRecords)                                                                              \
    X(IslandVisitSt1dTriggerSpawns)                                                                                 \
    X(IslandVisitSt1dSpawnerSpawns)                                                                                 \
    X(IslandVisitSt1dSpawnerMoreSpawns)                                                                             \
    X(IslandVisitSt1dLaterSpawnerSpawns)                                                                            \
    X(IslandVisitSt1dReaverbotSpawns)                                                                               \
    X(IslandVisitSt1dExitLightSpawns)                                                                               \
    X(IslandVisitParkedDropshipBox)                                                                                 \
    X(IslandVisitDecorationBox)                                                                                     \
    X(IslandVisitCellBlockerBox)                                                                                    \
    X(IslandVisitPackDelays)                                                                                        \
    X(IslandVisitRecordsSpawns)                                                                                     \
    X(IslandVisitStory2Sub2Spawns)                                                                                  \
    /* ---- content/forbidden_island/st10_cls */                                                                    \
    X(St10ClsBox)                                                                                                   \
    /* ---- content/forbidden_island/st11_boss */                                                                   \
    X(St11BossBox)                                                                                                  \
    /* ---- content/forbidden_island/st11_script2a */                                                               \
    X(St11Script2aCamera)                                                                                           \
    X(St11Script2aSteps)                                                                                            \
    X(St11Script2aFightSpawns)                                                                                      \
    X(St11Script2aArrivalCastBox)                                                                                   \
    /* ---- content/forbidden_island/st1c_cast */                                                                   \
    X(St1cCastFaceFigureCues)                                                                                       \
    X(St1cCastFaceFigureCue1FirstKeys)                                                                              \
    X(St1cCastFaceFigureCue1SecondKeys)                                                                             \
    X(St1cCastFaceFigureCue2FirstKeys)                                                                              \
    X(St1cCastFaceFigureCue2SecondKeys)                                                                             \
    X(St1cCastFaceFigureCue3FirstKeys)                                                                              \
    X(St1cCastFaceFigureCue3SecondKeys)                                                                             \
    X(St1cCastFaceFigureCue4FirstKeys)                                                                              \
    X(St1cCastFaceFigureCue4SecondKeys)                                                                             \
    X(St1cCastFaceFigureCue5FirstKeys)                                                                              \
    X(St1cCastFaceFigureCue5SecondKeys)                                                                             \
    X(St1cCastFaceFigureCue6FirstKeys)                                                                              \
    X(St1cCastFaceFigureCue6SecondKeys)                                                                             \
    X(St1cCastFaceFigureCue7FirstKeys)                                                                              \
    X(St1cCastFaceFigureCue7SecondKeys)                                                                             \
    X(St1cCastFaceFigureCue8FirstKeys)                                                                              \
    X(St1cCastFaceFigureCue8SecondKeys)                                                                             \
    X(St1cCastFaceFigureFirstTracks)                                                                                \
    X(St1cCastFaceFigureSecondTracks)                                                                               \
    X(St1cCastMeshFlipFigureCues)                                                                                   \
    X(St1cCastMeshFlipFigureCue1FirstKeys)                                                                          \
    X(St1cCastMeshFlipFigureCue1SecondKeys)                                                                         \
    X(St1cCastMeshFlipFigureCue2FirstKeys)                                                                          \
    X(St1cCastMeshFlipFigureCue2SecondKeys)                                                                         \
    X(St1cCastMeshFlipFigureCue3FirstKeys)                                                                          \
    X(St1cCastMeshFlipFigureCue3SecondKeys)                                                                         \
    X(St1cCastMeshFlipFigureCue4FirstKeys)                                                                          \
    X(St1cCastMeshFlipFigureCue4SecondKeys)                                                                         \
    X(St1cCastMeshFlipFigureCue5FirstKeys)                                                                          \
    X(St1cCastMeshFlipFigureCue5SecondKeys)                                                                         \
    X(St1cCastMeshFlipFigureCue6FirstKeys)                                                                          \
    X(St1cCastMeshFlipFigureCue6SecondKeys)                                                                         \
    X(St1cCastMeshFlipFigureCue7FirstKeys)                                                                          \
    X(St1cCastMeshFlipFigureCue7SecondKeys)                                                                         \
    X(St1cCastMeshFlipFigureFirstTracks)                                                                            \
    X(St1cCastMeshFlipFigureSecondTracks)                                                                           \
    X(St1cCastCuedFigureCues)                                                                                       \
    X(St1cCastCuedFigureCue3FirstKeys)                                                                              \
    X(St1cCastCuedFigureCue3SecondKeys)                                                                             \
    X(St1cCastWalkingFigureCues)                                                                                    \
    X(St1cCastWalkingFigureCue1FirstKeys)                                                                           \
    X(St1cCastWalkingFigureCue1SecondKeys)                                                                          \
    X(St1cCastWalkingFigureCue2FirstKeys)                                                                           \
    X(St1cCastWalkingFigureCue2SecondKeys)                                                                          \
    X(St1cCastWalkingFigureCue3FirstKeys)                                                                           \
    X(St1cCastWalkingFigureCue3SecondKeys)                                                                          \
    X(St1cCastWalkingFigureCue4FirstKeys)                                                                           \
    X(St1cCastWalkingFigureCue4SecondKeys)                                                                          \
    X(St1cCastWalkingFigureFirstTracks)                                                                             \
    X(St1cCastWalkingFigureSecondTracks)                                                                            \
    X(St1cCastPosingFigureCues)                                                                                     \
    X(St1cCastPosingFigureCue1FirstKeys)                                                                            \
    X(St1cCastPosingFigureCue1SecondKeys)                                                                           \
    X(St1cCastPosingFigureCue2FirstKeys)                                                                            \
    X(St1cCastPosingFigureCue2SecondKeys)                                                                           \
    X(St1cCastPosingFigureCue3FirstKeys)                                                                            \
    X(St1cCastPosingFigureCue3SecondKeys)                                                                           \
    X(St1cCastPosingFigureCue4FirstKeys)                                                                            \
    X(St1cCastPosingFigureCue4SecondKeys)                                                                           \
    X(St1cCastPosingFigureCue5FirstKeys)                                                                            \
    X(St1cCastPosingFigureCue5SecondKeys)                                                                           \
    X(St1cCastPosingFigureCue6FirstKeys)                                                                            \
    X(St1cCastPosingFigureCue6SecondKeys)                                                                           \
    X(St1cCastPosingFigureCue7FirstKeys)                                                                            \
    X(St1cCastPosingFigureCue7SecondKeys)                                                                           \
    X(St1cCastPosingFigureCue8FirstKeys)                                                                            \
    X(St1cCastPosingFigureCue8SecondKeys)                                                                           \
    X(St1cCastPosingFigureFirstTracks)                                                                              \
    X(St1cCastPosingFigureSecondTracks)                                                                             \
    X(St1cCastRisingPropCues)                                                                                       \
    X(St1cCastFidgetFirstKeys)                                                                                      \
    X(St1cCastFidgetSecondKeys)                                                                                     \
    X(St1cCastFidgetingFigureCues)                                                                                  \
    X(St1cCastFidgetingFigureCue1FirstKeys)                                                                         \
    X(St1cCastFidgetingFigureCue1SecondKeys)                                                                        \
    X(St1cCastFidgetingFigureCue2FirstKeys)                                                                         \
    X(St1cCastFidgetingFigureCue2SecondKeys)                                                                        \
    X(St1cCastFidgetingFigureCue4FirstKeys)                                                                         \
    X(St1cCastFidgetingFigureCue4SecondKeys)                                                                        \
    X(St1cCastFidgetingFigureCue5FirstKeys)                                                                         \
    X(St1cCastFidgetingFigureCue5SecondKeys)                                                                        \
    X(St1cCastFidgetingFigureCue6FirstKeys)                                                                         \
    X(St1cCastFidgetingFigureCue6SecondKeys)                                                                        \
    X(St1cCastFidgetingFigureFirstTracks)                                                                           \
    X(St1cCastFidgetingFigureSecondTracks)                                                                          \
    X(St1cCastHoppingFlyerCues)                                                                                     \
    X(St1cCastHoppingFlyerCue1FirstKeys)                                                                            \
    X(St1cCastHoppingFlyerCue1SecondKeys)                                                                           \
    X(St1cCastHoppingFlyerCue2FirstKeys)                                                                            \
    X(St1cCastHoppingFlyerCue2SecondKeys)                                                                           \
    X(St1cCastHoppingFlyerFirstTracks)                                                                              \
    X(St1cCastHoppingFlyerSecondTracks)                                                                             \
    X(St1cCastVanishingWalkerCues)                                                                                  \
    X(St1cCastVanishingWalkerCue1FirstKeys)                                                                         \
    X(St1cCastVanishingWalkerCue1SecondKeys)                                                                        \
    X(St1cCastVanishingWalkerCue2FirstKeys)                                                                         \
    X(St1cCastVanishingWalkerCue2SecondKeys)                                                                        \
    X(St1cCastVanishingWalkerCue3FirstKeys)                                                                         \
    X(St1cCastVanishingWalkerCue3SecondKeys)                                                                        \
    X(St1cCastVanishingWalkerCue4FirstKeys)                                                                         \
    X(St1cCastVanishingWalkerCue4SecondKeys)                                                                        \
    X(St1cCastVanishingWalkerFirstTracks)                                                                           \
    X(St1cCastVanishingWalkerSecondTracks)                                                                          \
    X(St1cCastFlyerCues)                                                                                            \
    X(St1cCastGroundedHopperCues)                                                                                   \
    X(St1cCastGroundedHopperCue1FirstKeys)                                                                          \
    X(St1cCastGroundedHopperCue1SecondKeys)                                                                         \
    X(St1cCastGroundedHopperCue2FirstKeys)                                                                          \
    X(St1cCastGroundedHopperCue2SecondKeys)                                                                         \
    X(St1cCastGroundedHopperFirstTracks)                                                                            \
    X(St1cCastGroundedHopperSecondTracks)                                                                           \
    X(St1cCastFallingFigureCues)                                                                                    \
    X(St1cCastFallingFigureCue1FirstKeys)                                                                           \
    X(St1cCastFallingFigureCue1SecondKeys)                                                                          \
    X(St1cCastFallingFigureCue5FirstKeys)                                                                           \
    X(St1cCastFallingFigureCue5SecondKeys)                                                                          \
    X(St1cCastFallingFigureCue8FirstKeys)                                                                           \
    X(St1cCastFallingFigureCue8SecondKeys)                                                                          \
    X(St1cCastFallingFigureCue9FirstKeys)                                                                           \
    X(St1cCastFallingFigureCue9SecondKeys)                                                                          \
    X(St1cCastFallingFigureCue10FirstKeys)                                                                          \
    X(St1cCastFallingFigureCue10SecondKeys)                                                                         \
    X(St1cCastFallingFigureFirstTracks)                                                                             \
    X(St1cCastFallingFigureSecondTracks)                                                                            \
    X(St1cCastGlowingFigureCues)                                                                                    \
    X(St1cCastGlowingFigureCue2FirstKeys)                                                                           \
    X(St1cCastGlowingFigureCue2SecondKeys)                                                                          \
    X(St1cCastGlowingFigureCue4FirstKeys)                                                                           \
    X(St1cCastGlowingFigureCue4SecondKeys)                                                                          \
    X(St1cCastGlowingFigureCue5FirstKeys)                                                                           \
    X(St1cCastGlowingFigureCue5SecondKeys)                                                                          \
    X(St1cCastGlowingFigureCue7FirstKeys)                                                                           \
    X(St1cCastGlowingFigureCue7SecondKeys)                                                                          \
    X(St1cCastGlowingFigureCue8FirstKeys)                                                                           \
    X(St1cCastGlowingFigureCue8SecondKeys)                                                                          \
    X(St1cCastGlowingFigureCue9FirstKeys)                                                                           \
    X(St1cCastGlowingFigureCue9SecondKeys)                                                                          \
    X(St1cCastGlowingFigureCue10FirstKeys)                                                                          \
    X(St1cCastGlowingFigureCue10SecondKeys)                                                                         \
    X(St1cCastGlowingFigureBindActor)                                                                               \
    X(St1cCastGlowingFigureBindF)                                                                                   \
    X(St1cCastHopperCues)                                                                                           \
    X(St1cCastHopperShortHopCueFirstKeys)                                                                           \
    X(St1cCastHopperShortHopCueSecondKeys)                                                                          \
    X(St1cCastHopperFirstTracks)                                                                                    \
    X(St1cCastHopperSecondTracks)                                                                                   \
    X(St1cCastIdleFigureCues)                                                                                       \
    X(St1cCastIdleFigureCue5FirstKeys)                                                                              \
    X(St1cCastIdleFigureCue5SecondKeys)                                                                             \
    X(St1cCastIdleFigureFirstTracks)                                                                                \
    X(St1cCastIdleFigureSecondTracks)                                                                               \
    X(St1cCastStillModelCues)                                                                                       \
    X(St1cCastSecondStillModelCues)                                                                                 \
    /* ---- content/forbidden_island/st1c_script13 */                                                               \
    /* the player's face tracks and their tables (exe data) */                                                      \
    X(St1cScript13SecondCastRecord0)                                                                                \
    X(St1cScript13SecondCastRecord1)                                                                                \
    X(St1cScript13SecondCastRecord2)                                                                                \
    X(St1cScript13ThirdCastRecord0)                                                                                 \
    X(St1cScript13ThirdCastRecord1)                                                                                 \
    X(St1cScript13ThirdCastRecord2)                                                                                 \
    X(St1cScript13OpeningEyesKeys)                                                                                  \
    X(St1cScript13OpeningMouthKeys)                                                                                 \
    X(St1cScript13OpeningWaitEyesKeys)                                                                              \
    X(St1cScript13OpeningWaitMouthKeys)                                                                             \
    X(St1cScript13GlowEyesKeys)                                                                                     \
    X(St1cScript13GlowMouthKeys)                                                                                    \
    X(St1cScript13CameraScript)                                                                                     \
    X(St1cScript13Steps)                                                                                            \
    X(St1cScript13FaceEyesTable)                                                                                    \
    X(St1cScript13FaceMouthTable)                                                                                   \
    X(St1cScript13FirstCastRecord)                                                                                  \
    X(St1cScript13FirstCastFreed)                                                                                   \
    /* ---- content/forbidden_island/st3e_reports */                                                                \
    X(St3eReportsReport65Camera)                                                                                    \
    X(St3eReportsReport65Steps)                                                                                     \
    X(St3eReportsReport66Camera)                                                                                    \
    X(St3eReportsReport66Steps)                                                                                     \
    X(St3eReportsReport67Camera)                                                                                    \
    X(St3eReportsReport67Steps)                                                                                     \
    X(St3eReportsFirstTalkCamera)                                                                                   \
    X(St3eReportsFirstTalkSteps)                                                                                    \
    X(St3eReportsSecondTalkCamera)                                                                                  \
    X(St3eReportsSecondTalkSteps)                                                                                   \
    X(St3eReportsFirstTalkLateCamera) /* the camera script step 0x538a40 switches to */                             \
    X(St3eReportsSpeakerModels)                                                                                     \
    X(St3eReportsSpeakerBoxes)                                                                                      \
    /* ---- content/forbidden_island/st49_landing */                                                                \
    X(St49LandingFirstCamera)                                                                                       \
    X(St49LandingFirstSteps)                                                                                        \
    X(St49LandingReturnCamera)                                                                                      \
    X(St49LandingReturnSteps)                                                                                       \
    X(St49LandingFaceEyesTable)                                                                                     \
    X(St49LandingFaceMouthTable)                                                                                    \
    /* ---- content/late_game/ch4_story */                                                                          \
    X(Ch4StoryArrivalCamera)                                                                                        \
    X(Ch4StoryArrivalSteps)                                                                                         \
    X(Ch4StoryArrivalRecord)                                                                                        \
    X(Ch4StorySt40Sub3ExtraSpawns)                                                                                  \
    X(Ch4StorySt40Sub5ExtraSpawns)                                                                                  \
    X(Ch4StorySt40Sub7ExtraSpawns)                                                                                  \
    X(Ch4StoryTalkerABox)                                                                                           \
    X(Ch4StoryTalkerAFirstTracks)                                                                                   \
    X(Ch4StoryTalkerASecondTracks)                                                                                  \
    X(Ch4StoryTalkerBBox)                                                                                           \
    X(Ch4StoryTalkerBFirstTracks)                                                                                   \
    X(Ch4StoryTalkerBSecondTracks)                                                                                  \
    X(Ch4StoryColumnBlasts)                                                                                         \
    X(Ch4StoryColumnSmokes)                                                                                         \
    X(Ch4StoryRidingBlastPoint)                                                                                     \
    X(Ch4StoryRidingSecondBlastPoint)                                                                               \
    X(Ch4StoryRidingCamera)                                                                                         \
    X(Ch4StoryRidingSteps)                                                                                          \
    X(Ch4StoryRidingFirstSparkPoint)                                                                                \
    X(Ch4StoryRidingSecondSparkPoint)                                                                               \
    X(Ch4StoryRidingThirdSparkPoint)                                                                                \
    X(Ch4StorySt40PadSpawns)                                                                                        \
    X(Ch4StorySt40Sub0Spawns)                                                                                       \
    X(Ch4StorySt40Sub0MoreSpawns)                                                                                   \
    X(Ch4StorySt40Sub0LateSpawns)                                                                                   \
    X(Ch4StorySt40Sub1Spawns)                                                                                       \
    X(Ch4StorySt43Sub2Spawns)                                                                                       \
    X(Ch4StorySt40Sub2Spawns)                                                                                       \
    X(Ch4StorySt40Sub2MoreSpawns)                                                                                   \
    X(Ch4StoryPuffFrames) /* 0x14 bytes per frame */                                                                \
    /* ---- content/late_game/end_cast */                                                                           \
    X(EndCastTalkerABox)                                                                                            \
    X(EndCastTalkerAFirstTracks)                                                                                    \
    X(EndCastTalkerASecondTracks)                                                                                   \
    X(EndCastTalkerBBox)                                                                                            \
    X(EndCastTalkerBFirstTracks)                                                                                    \
    X(EndCastTalkerBSecondTracks)                                                                                   \
    X(EndCastMouthTalkerBox)                                                                                        \
    X(EndCastWalkerBox)                                                                                             \
    X(EndCastWalkerFirstTracks)                                                                                     \
    X(EndCastWalkerSecondTracks)                                                                                    \
    X(EndCastGroupFirstTracks)                                                                                      \
    X(EndCastGroupSecondTracks)                                                                                     \
    X(EndCastGroupBox)                                                                                              \
    X(EndCastGlowFirstTracks)                                                                                       \
    X(EndCastGlowSecondTracks)                                                                                      \
    X(EndCastGlowBox)                                                                                               \
    X(EndCastFinaleABox)                                                                                            \
    X(EndCastFinaleAFirstTracks)                                                                                    \
    X(EndCastFinaleASecondTracks)                                                                                   \
    X(EndCastFinaleBBox)                                                                                            \
    X(EndCastFinaleBFirstTracks)                                                                                    \
    X(EndCastFinaleBSecondTracks)                                                                                   \
    X(EndCastPropBox)                                                                                               \
    /* ---- content/late_game/end_scripts */                                                                        \
    X(EndScriptsScript75Record)                                                                                     \
    X(EndScriptsScript75Eyes)                                                                                       \
    X(EndScriptsScript75Mouth)                                                                                      \
    X(EndScriptsScript75Camera)                                                                                     \
    X(EndScriptsScript75Steps)                                                                                      \
    X(EndScriptsDefeatRecord)                                                                                       \
    X(EndScriptsScript6dEyes)                                                                                       \
    X(EndScriptsScript6dMouth)                                                                                      \
    X(EndScriptsScript6dCamera)                                                                                     \
    X(EndScriptsScript6dSteps)                                                                                      \
    X(EndScriptsReturnFirstRecord)                                                                                  \
    X(EndScriptsReturnSecondRecord)                                                                                 \
    X(EndScriptsReturnThirdRecord)                                                                                  \
    X(EndScriptsReturnFourthRecord)                                                                                 \
    X(EndScriptsReturnFifthRecord)                                                                                  \
    X(EndScriptsScript3fEyes)                                                                                       \
    X(EndScriptsScript3fMouth)                                                                                      \
    X(EndScriptsScript3fCamera)                                                                                     \
    X(EndScriptsScript3fSteps)                                                                                      \
    X(EndScriptsScript6fCamera)                                                                                     \
    X(EndScriptsScript6fSteps)                                                                                      \
    X(EndScriptsStaffRollCamera)                                                                                    \
    X(EndScriptsStaffRollSteps)                                                                                     \
    X(EndScriptsSt5cSpawns)                                                                                         \
    X(EndScriptsFaderRecord)                                                                                        \
    X(EndScriptsStaffPages)                                                                                         \
    /* ---- content/late_game/late_cast */                                                                          \
    X(LateCastTalkerAFirstRows)                                                                                     \
    X(LateCastTalkerAFirstRowsSecond)                                                                               \
    X(LateCastTalkerASecondRows)                                                                                    \
    X(LateCastTalkerASecondRowsSecond)                                                                              \
    X(LateCastCuedTalkerAFirstTracks)                                                                               \
    X(LateCastCuedTalkerASecondTracks)                                                                              \
    X(LateCastSceneTalkerScene0First)                                                                               \
    X(LateCastSceneTalkerScene0Second)                                                                              \
    X(LateCastSceneTalkerScene1First)                                                                               \
    X(LateCastSceneTalkerScene1Second)                                                                              \
    X(LateCastSceneTalkerScene2First)                                                                               \
    X(LateCastSceneTalkerScene2Second)                                                                              \
    X(LateCastSceneTalkerScene4First)                                                                               \
    X(LateCastSceneTalkerScene4Second)                                                                              \
    X(LateCastSceneTalkerScene6First)                                                                               \
    X(LateCastSceneTalkerScene6Second)                                                                              \
    X(LateCastSceneTalkerScene7First)                                                                               \
    X(LateCastSceneTalkerScene7Second)                                                                              \
    X(LateCastSceneTalkerScene8First)                                                                               \
    X(LateCastSceneTalkerScene8Second)                                                                              \
    X(LateCastSceneTalkerFirstTracks)                                                                               \
    X(LateCastSceneTalkerSecondTracks)                                                                              \
    X(LateCastSceneWalkerFirstTracks)                                                                               \
    X(LateCastSceneWalkerSecondTracks)                                                                              \
    X(LateCastTalkerBFirstRows)                                                                                     \
    X(LateCastTalkerBFirstRowsSecond)                                                                               \
    X(LateCastTalkerBSecondRows)                                                                                    \
    X(LateCastTalkerBSecondRowsSecond)                                                                              \
    X(LateCastCuedTalkerBFirstTracks)                                                                               \
    X(LateCastCuedTalkerBSecondTracks)                                                                              \
    X(LateCastLateTalkerRows)                                                                                       \
    X(LateCastLateTalkerRowsSecond)                                                                                 \
    X(LateCastLateTalkerFirstTracks)                                                                                \
    X(LateCastLateTalkerSecondTracks)                                                                               \
    X(LateCastActionTalkerAct2First)                                                                                \
    X(LateCastActionTalkerAct2Second)                                                                               \
    X(LateCastActionTalkerAct3First)                                                                                \
    X(LateCastActionTalkerAct3Second)                                                                               \
    X(LateCastActionTalkerAct4First)                                                                                \
    X(LateCastActionTalkerAct4Second)                                                                               \
    X(LateCastActionTalkerAct5First)                                                                                \
    X(LateCastActionTalkerAct5Second)                                                                               \
    X(LateCastActionTalkerAct6First)                                                                                \
    X(LateCastActionTalkerAct6Second)                                                                               \
    X(LateCastActionTalkerAct7First)                                                                                \
    X(LateCastActionTalkerAct7Second)                                                                               \
    X(LateCastActionTalkerFirstTracks)                                                                              \
    X(LateCastActionTalkerSecondTracks)                                                                             \
    X(LateCastHoldingTalkerAct1First)                                                                               \
    X(LateCastHoldingTalkerAct1Second)                                                                              \
    X(LateCastHoldingTalkerAct2First)                                                                               \
    X(LateCastHoldingTalkerAct2Second)                                                                              \
    X(LateCastHoldingTalkerAct3First)                                                                               \
    X(LateCastHoldingTalkerAct3Second)                                                                              \
    X(LateCastHoldingTalkerAct4First)                                                                               \
    X(LateCastHoldingTalkerAct4Second)                                                                              \
    X(LateCastHoldingTalkerAct6First)                                                                               \
    X(LateCastHoldingTalkerAct6Second)                                                                              \
    X(LateCastHoldingTalkerAct7First)                                                                               \
    X(LateCastHoldingTalkerAct7Second)                                                                              \
    X(LateCastHoldingTalkerFirstTracks)                                                                             \
    X(LateCastHoldingTalkerSecondTracks)                                                                            \
    X(LateCastPacerRows)                                                                                            \
    X(LateCastPacerTracks)                                                                                          \
    X(LateCastRowTalkerAct1First)                                                                                   \
    X(LateCastRowTalkerAct1Second)                                                                                  \
    X(LateCastRowTalkerFirstTracks)                                                                                 \
    X(LateCastRowTalkerSecondTracks)                                                                                \
    X(LateCastMouthTalkerRows)                                                                                      \
    X(LateCastCirclerRows)                                                                                          \
    X(LateCastCirclerTracks)                                                                                        \
    X(LateCastSparkTalkerActFirst)                                                                                  \
    X(LateCastSparkTalkerAct1Second)                                                                                \
    X(LateCastSparkTalkerAct2Second)                                                                                \
    X(LateCastSparkTalkerFirstTracks)                                                                               \
    X(LateCastSparkTalkerSecondTracks)                                                                              \
    X(LateCastWaypoints)                                                                                            \
    X(LateCastCirclerGoal) /* s16 x, y, z */                                                                        \
    /* ---- content/late_game/late_rooms */                                                                         \
    X(LateRoomsSt56Spawns)                                                                                          \
    /* ---- content/late_game/st43_roller */                                                                        \
    X(St43RollerBox)                                                                                                \
    X(St43RollerLiftBox)                                                                                            \
    /* ---- content/late_game/st45_boss */                                                                          \
    X(St45BossArenaBox)                                                                                             \
    X(St45BossRetreatBox)                                                                                           \
    X(St45BossSecondArenaBox)                                                                                       \
    X(St45BossArenaCells) /* 9 cell boxes: the player's ninth of the arena */                                       \
    X(St45BossAnchorPoints)                                                                                         \
    X(St45BossNearPoints)                                                                                           \
    X(St45BossFootAnims) /* pairs (front, back) of animations by kind */                                            \
    X(St45BossBreathSpread) /* s8 x 4 */                                                                            \
    X(St45BossDeathBones) /* u8 x 8 */                                                                              \
    X(St45BossBox)                                                                                                  \
    X(St45BossHitPointRows)                                                                                         \
    X(St45BossRunRows)                                                                                              \
    X(St45BossRoarRows)                                                                                             \
    X(St45BossSecondWalkRows)                                                                                       \
    X(St45BossPuffBox)                                                                                              \
    X(St45BossBlobBox)                                                                                              \
    X(St45BossMouthShotBox)                                                                                         \
    X(St45BossSecondStayBox)                                                                                        \
    X(St45BossSecondRetreatBox)                                                                                     \
    X(St45BossBreathRows)                                                                                           \
    X(St45BossWaveRows)                                                                                             \
    X(St45BossMouthShotRows)                                                                                        \
    /* ---- content/late_game/st4b */                                                                               \
    X(St4bFaceRowFirstRows)                                                                                         \
    X(St4bFaceRowFirstRowsSecond)                                                                                   \
    X(St4bFaceRowSecondRows)                                                                                        \
    X(St4bFaceRowSecondRowsSecond)                                                                                  \
    X(St4bFaceRowFirstTracks)                                                                                       \
    X(St4bFaceRowSecondTracks)                                                                                      \
    X(St4bWalkInFirstTracks)                                                                                        \
    X(St4bWalkInSecondTracks)                                                                                       \
    X(St4bTauntMessages)                                                                                            \
    X(St4bBossMovePicks)                                                                                            \
    /* ---- content/late_game/st4b_fx */                                                                            \
    X(St4bFxVolleyBox)                                                                                              \
    /* ---- content/late_game/st4b_scripts */                                                                       \
    X(St4bScriptsRetreatCamera)                                                                                     \
    X(St4bScriptsRetreatSteps)                                                                                      \
    X(St4bScriptsRetreatSpawns)                                                                                     \
    X(St4bScriptsEscapeCamera)                                                                                      \
    X(St4bScriptsEscapeSteps)                                                                                       \
    /* ---- content/late_game/st4b_st4c_rooms */                                                                    \
    X(St4bSt4cRoomsStep2FaceRows)                                                                                   \
    X(St4bSt4cRoomsStep2FaceRowsSecond)                                                                             \
    X(St4bSt4cRoomsCollapseRecord)                                                                                  \
    X(St4bSt4cRoomsStep13FaceRows)                                                                                  \
    X(St4bSt4cRoomsStep13FaceRowsSecond)                                                                            \
    X(St4bSt4cRoomsStep14FaceRows)                                                                                  \
    X(St4bSt4cRoomsStep14FaceRowsSecond)                                                                            \
    X(St4bSt4cRoomsStep16FaceRows)                                                                                  \
    X(St4bSt4cRoomsStep16FaceRowsSecond)                                                                            \
    X(St4bSt4cRoomsStep20FaceRows)                                                                                  \
    X(St4bSt4cRoomsStep20FaceRowsSecond)                                                                            \
    X(St4bSt4cRoomsScript59Eyes)                                                                                    \
    X(St4bSt4cRoomsScript59Mouth)                                                                                   \
    X(St4bSt4cRoomsScript59Camera)                                                                                  \
    X(St4bSt4cRoomsScript59Steps)                                                                                   \
    X(St4bSt4cRoomsScript59Record)                                                                                  \
    X(St4bSt4cRoomsScript59Spawns)                                                                                  \
    X(St4bSt4cRoomsSt4CSub3Spawns)                                                                                  \
    X(St4bSt4cRoomsScenePoints)                                                                                     \
    /* ---- content/late_game/st4c_boss */                                                                          \
    X(St4cBossBox)                                                                                                  \
    X(St4cBossBoneTints) /* s8 per bone 0..10: the fade threshold */                                                \
    X(St4cBossOrbitShotBox)                                                                                         \
    X(St4cBossHomingShotBox)                                                                                        \
    X(St4cBossBoltBox)                                                                                              \
    X(St4cBossFlashColours) /* u32 x 8: the spokes' colours */                                                      \
    /* ---- content/late_game/st4c_story */                                                                         \
    X(St4cStoryIntroEyes)                                                                                           \
    X(St4cStoryIntroMouth)                                                                                          \
    X(St4cStoryIntroCamera)                                                                                         \
    X(St4cStoryIntroSteps)                                                                                          \
    X(St4cStoryIntroBossRecord)                                                                                     \
    X(St4cStoryOutroBossRecord)                                                                                     \
    X(St4cStoryOutroEyes)                                                                                           \
    X(St4cStoryOutroMouth)                                                                                          \
    X(St4cStoryOutroCamera)                                                                                         \
    X(St4cStoryOutroSteps)                                                                                          \
    X(St4cStoryIntroCastBox)                                                                                        \
    X(St4cStoryIntroCastFirstTracks)                                                                                \
    X(St4cStoryIntroCastSecondTracks)                                                                               \
    X(St4cStorySparkPoints)                                                                                         \
    X(St4cStoryDefeatTrails)                                                                                        \
    X(St4cStoryDefeatBursts)                                                                                        \
    X(St4cStoryDefeatedBossBox)                                                                                     \
    X(St4cStoryDefeatedBossFirstTracks)                                                                             \
    X(St4cStoryDefeatedBossSecondTracks)                                                                            \
    X(St4cStoryRayBurstRows)                                                                                        \
    /* ---- content/late_game/st53_story */                                                                         \
    X(St53StoryFacedTalker60States)                                                                                 \
    X(St53StoryFacedTalker60Box)                                                                                    \
    X(St53StoryFacedTalker60Steps)                                                                                  \
    X(St53StoryFacedTalker60FirstTracks)                                                                            \
    X(St53StoryFacedTalker60SecondTracks)                                                                           \
    X(St53StoryFacedTalker61States)                                                                                 \
    X(St53StoryFacedTalker61Box)                                                                                    \
    X(St53StoryFacedTalker61Steps)                                                                                  \
    X(St53StoryFacedTalker61FirstTracks)                                                                            \
    X(St53StoryFacedTalker61SecondTracks)                                                                           \
    X(St53StoryFadingFigureStates)                                                                                  \
    X(St53StoryFadingFigureBox)                                                                                     \
    X(St53StoryFadingFigureSteps)                                                                                   \
    X(St53StoryStandingFigureStates)                                                                                \
    X(St53StoryStandingFigureBox)                                                                                   \
    X(St53StoryStandingFigureSteps)                                                                                 \
    X(St53StoryWalkerStates)                                                                                        \
    X(St53StoryWalkerBox)                                                                                           \
    X(St53StoryWalkerSteps)                                                                                         \
    X(St53StoryMaskedTalker60States)                                                                                \
    X(St53StoryMaskedTalker60Box)                                                                                   \
    X(St53StoryMaskedTalker60Steps)                                                                                 \
    X(St53StoryMaskedTalker60FirstTracks)                                                                           \
    X(St53StoryMaskedTalker60SecondTracks)                                                                          \
    X(St53StoryMaskedTalker61States)                                                                                \
    X(St53StoryMaskedTalker61Box)                                                                                   \
    X(St53StoryMaskedTalker61Steps)                                                                                 \
    X(St53StoryMaskedTalker61FirstTracks)                                                                           \
    X(St53StoryMaskedTalker61SecondTracks)                                                                          \
    X(St53StoryLeadTalker60States)                                                                                  \
    X(St53StoryLeadTalker60Box)                                                                                     \
    X(St53StoryLeadTalker60Steps)                                                                                   \
    X(St53StoryLeadTalker60FirstTracks)                                                                             \
    X(St53StoryLeadTalker60SecondTracks)                                                                            \
    X(St53StoryLeadTalker61States)                                                                                  \
    X(St53StoryLeadTalker61Box)                                                                                     \
    X(St53StoryLeadTalker61Steps)                                                                                   \
    X(St53StoryLeadTalker61FirstTracks)                                                                             \
    X(St53StoryLeadTalker61SecondTracks)                                                                            \
    X(St53StoryExtraAStates)                                                                                        \
    X(St53StoryExtraABox)                                                                                           \
    X(St53StoryExtraASteps)                                                                                         \
    X(St53StoryExtraBStates)                                                                                        \
    X(St53StoryExtraBBox)                                                                                           \
    X(St53StoryExtraBSteps)                                                                                         \
    X(St53StorySnowTrackCamera)                                                                                     \
    X(St53StorySnowTrackSteps)                                                                                      \
    X(St53StoryRideCamera)                                                                                          \
    X(St53StoryRideSteps)                                                                                           \
    /* ---- content/late_game/st54_clear_screen */                                                                  \
    X(St54ClearScreenSprites)                                                                                       \
    X(St54ClearScreenCameraScript)                                                                                  \
    X(St54ClearScreenStepList) /* the camera script is empty */                                                     \
    /* ---- content/late_game/st54_puffs */                                                                         \
    X(St54PuffsPuffRows)                                                                                            \
    /* ---- content/late_game/st59 */                                                                               \
    X(St59CourseRecords)                                                                                            \
    /* ---- content/late_game/st5c */                                                                               \
    X(St5cIntroEyes)                                                                                                \
    X(St5cIntroMouth)                                                                                               \
    X(St5cDefeatEyes)                                                                                               \
    X(St5cDefeatMouth)                                                                                              \
    X(St5cBossBox)                                                                                                  \
    X(St5cStompProbeBox)                                                                                            \
    X(St5cIntroBox)                                                                                                 \
    X(St5cDefeatBox)                                                                                                \
    /* ---- content/late_game/st5c_fx */                                                                            \
    X(St5cFxBladeBox)                                                                                               \
    X(St5cFxStompShotBox)                                                                                           \
    /* ---- content/late_game/st5c_fx2 */                                                                           \
    X(St5cFx2BeamBox)                                                                                               \
    X(St5cFx2BlastBox)                                                                                              \
    /* ---- content/saul_kada/bomb_critter */                                                                       \
    X(BombCritterBox)                                                                                               \
    X(BombCritterLiftBox)                                                                                           \
    /* ---- content/saul_kada/dropped_item */                                                                       \
    X(DroppedItemBox)                                                                                               \
    /* ---- content/saul_kada/enemy_spawners */                                                                     \
    X(EnemySpawnersFirstWallBlast)                                                                                  \
    X(EnemySpawnersSecondWallBlast)                                                                                 \
    X(EnemySpawnersThirdWallBlast)                                                                                  \
    X(EnemySpawnersGiantBoxes)                                                                                      \
    X(EnemySpawnersGiantPoints)                                                                                     \
    X(EnemySpawnersFlamePointA)                                                                                     \
    X(EnemySpawnersFlamePointB)                                                                                     \
    /* ---- content/saul_kada/fire_ruin_cutscene_cast */                                                            \
    X(FireRuinCutsceneCastDropperBox)                                                                               \
    /* ---- content/saul_kada/fire_ruin_depths_scripts */                                                           \
    X(FireRuinDepthsScriptsEdgeCamera)                                                                              \
    X(FireRuinDepthsScriptsEdgeSteps)                                                                               \
    X(FireRuinDepthsScriptsEdgeSpawns)                                                                              \
    X(FireRuinDepthsScriptsEdgeMoreSpawns)                                                                          \
    X(FireRuinDepthsScriptsEdgeLastSpawns)                                                                          \
    X(FireRuinDepthsScriptsReturnCamera)                                                                            \
    X(FireRuinDepthsScriptsReturnSteps)                                                                             \
    X(FireRuinDepthsScriptsReturnFirstTracks)                                                                       \
    X(FireRuinDepthsScriptsReturnSecondTracks)                                                                      \
    X(FireRuinDepthsScriptsReturnSpawns)                                                                            \
    X(FireRuinDepthsScriptsReturnMoreSpawns)                                                                        \
    X(FireRuinDepthsScriptsBeatenCamera)                                                                            \
    X(FireRuinDepthsScriptsBeatenSteps)                                                                             \
    X(FireRuinDepthsScriptsBeatenRecord0)                                                                           \
    X(FireRuinDepthsScriptsBeatenRecord1)                                                                           \
    X(FireRuinDepthsScriptsBeatenRecord2)                                                                           \
    X(FireRuinDepthsScriptsBeatenRecord3)                                                                           \
    X(FireRuinDepthsScriptsBeatenRecord4)                                                                           \
    X(FireRuinDepthsScriptsBeatenRecord5)                                                                           \
    X(FireRuinDepthsScriptsLeaveRecords)                                                                            \
    X(FireRuinDepthsScriptsLeaveRecord1)                                                                            \
    X(FireRuinDepthsScriptsLeaveRecord2)                                                                            \
    X(FireRuinDepthsScriptsLeaveRecord3)                                                                            \
    X(FireRuinDepthsScriptsLeaveCamera)                                                                             \
    X(FireRuinDepthsScriptsLeaveSteps)                                                                              \
    X(FireRuinDepthsScriptsLeaveSpawns)                                                                             \
    X(FireRuinDepthsScriptsLeaveMoreSpawns)                                                                         \
    /* ---- content/saul_kada/fire_ruin_entrance_scripts */                                                         \
    X(FireRuinEntranceScriptsEntranceFirstTracks)                                                                   \
    X(FireRuinEntranceScriptsEntranceSecondTracks)                                                                  \
    X(FireRuinEntranceScriptsEntranceCamera)                                                                        \
    X(FireRuinEntranceScriptsEntranceSteps)                                                                         \
    X(FireRuinEntranceScriptsEntranceRecord0)                                                                       \
    X(FireRuinEntranceScriptsEntranceRecord1)                                                                       \
    X(FireRuinEntranceScriptsEntranceRecord2)                                                                       \
    X(FireRuinEntranceScriptsEntranceRecord3)                                                                       \
    X(FireRuinEntranceScriptsEntranceSpawns)                                                                        \
    X(FireRuinEntranceScriptsEntranceEndSpawns)                                                                     \
    X(FireRuinEntranceScriptsGateFirstTracks)                                                                       \
    X(FireRuinEntranceScriptsGateSecondTracks)                                                                      \
    X(FireRuinEntranceScriptsGateCamera)                                                                            \
    X(FireRuinEntranceScriptsGateSteps)                                                                             \
    X(FireRuinEntranceScriptsGateRecord0)                                                                           \
    X(FireRuinEntranceScriptsGateRecord1)                                                                           \
    X(FireRuinEntranceScriptsGateRecord2)                                                                           \
    X(FireRuinEntranceScriptsGateRecord3)                                                                           \
    X(FireRuinEntranceScriptsGateRecord4)                                                                           \
    X(FireRuinEntranceScriptsGateRecord5)                                                                           \
    X(FireRuinEntranceScriptsGateEndSpawns)                                                                         \
    X(FireRuinEntranceScriptsGateEndMoreSpawns)                                                                     \
    X(FireRuinEntranceScriptsDepthsFirstTracks)                                                                     \
    X(FireRuinEntranceScriptsDepthsSecondTracks)                                                                    \
    X(FireRuinEntranceScriptsDepthsCamera)                                                                          \
    X(FireRuinEntranceScriptsDepthsSteps)                                                                           \
    X(FireRuinEntranceScriptsDepthsEndSpawns)                                                                       \
    X(FireRuinEntranceScriptsDepthsEndMoreSpawns)                                                                   \
    X(FireRuinEntranceScriptsRubbleFirstTracks)                                                                     \
    X(FireRuinEntranceScriptsRubbleSecondTracks)                                                                    \
    X(FireRuinEntranceScriptsRubbleCamera)                                                                          \
    X(FireRuinEntranceScriptsRubbleSteps)                                                                           \
    X(FireRuinEntranceScriptsRubbleEndSpawns)                                                                       \
    /* ---- content/saul_kada/fire_ruin_rooms */                                                                    \
    X(FireRuinRoomsSt26EntrySpawns)                                                                                 \
    X(FireRuinRoomsSt26HallSpawns)                                                                                  \
    X(FireRuinRoomsSt27CrossingSpawns)                                                                              \
    X(FireRuinRoomsSt27CrossingFirstBridgeSpawns)                                                                   \
    X(FireRuinRoomsSt27CrossingSecondBridgeSpawns)                                                                  \
    X(FireRuinRoomsSt27CrossingMoreSpawns)                                                                          \
    X(FireRuinRoomsSt27LavaPassageSpawns)                                                                           \
    X(FireRuinRoomsSt27EntranceSpawns)                                                                              \
    X(FireRuinRoomsSt27EntranceMoreSpawns)                                                                          \
    X(FireRuinRoomsSt28BossRoomSpawns)                                                                              \
    X(FireRuinRoomsSt28BossRoomMoreSpawns)                                                                          \
    X(FireRuinRoomsSt28ApproachSpawns)                                                                              \
    X(FireRuinRoomsSt28LiftSpawns)                                                                                  \
    X(FireRuinRoomsSt28LiftMoreSpawns)                                                                              \
    X(FireRuinRoomsSt33EntrySpawns)                                                                                 \
    X(FireRuinRoomsSt33EntrySecondSpawns)                                                                           \
    X(FireRuinRoomsSt33EntrySecondMoreSpawns)                                                                       \
    X(FireRuinRoomsSt33EntryLastSpawns)                                                                             \
    X(FireRuinRoomsSt33EntryLastMoreSpawns)                                                                         \
    X(FireRuinRoomsSt26Sub6Spawns)                                                                                  \
    X(FireRuinRoomsSt27Sub0NearSpawns)                                                                              \
    X(FireRuinRoomsSt27Sub0FarSpawns)                                                                               \
    X(FireRuinRoomsSt27Sub1Spawns)                                                                                  \
    X(FireRuinRoomsSt27Sub7Spawns)                                                                                  \
    X(FireRuinRoomsSt28Sub2Spawns)                                                                                  \
    X(FireRuinRoomsSt34Sub5Spawns)                                                                                  \
    X(FireRuinRoomsEmberPointA)                                                                                     \
    X(FireRuinRoomsEmberPointB)                                                                                     \
    /* ---- content/saul_kada/fire_ruin_servbot_cast */                                                             \
    X(FireRuinServbotCastBlinkRows)                                                                                 \
    /* ---- content/saul_kada/fire_ruin_visit_scripts */                                                            \
    X(FireRuinVisitScriptsEscapeEveSpawns)                                                                          \
    X(FireRuinVisitScriptsEscapeEveCamera)                                                                          \
    X(FireRuinVisitScriptsEscapeEveSteps)                                                                           \
    X(FireRuinVisitScriptsEscapeEveEscapedSpawns)                                                                   \
    X(FireRuinVisitScriptsEscapeEveStayedSpawns)                                                                    \
    X(FireRuinVisitScriptsHallSpawns)                                                                               \
    X(FireRuinVisitScriptsHallCamera)                                                                               \
    X(FireRuinVisitScriptsHallSteps)                                                                                \
    X(FireRuinVisitScriptsHallEndSpawns)                                                                            \
    X(FireRuinVisitScriptsRuinVisitRecord)                                                                          \
    X(FireRuinVisitScriptsRuinVisitCamera)                                                                          \
    X(FireRuinVisitScriptsRuinVisitSteps)                                                                           \
    X(FireRuinVisitScriptsRuinVisitSpawns)                                                                          \
    X(FireRuinVisitScriptsRuinVisitMoreSpawns)                                                                      \
    /* ---- content/saul_kada/hopping_reaverbot */                                                                  \
    X(HoppingReaverbotHopBox)                                                                                       \
    X(HoppingReaverbotLiftBox)                                                                                      \
    /* ---- content/saul_kada/lava_pit */                                                                           \
    X(LavaPitFlameBox)                                                                                              \
    X(LavaPitCreatureBox)                                                                                           \
    /* ---- content/saul_kada/lava_vents */                                                                         \
    X(LavaVentsPlumeProbeBox)                                                                                       \
    /* ---- content/saul_kada/pirate_base_cast */                                                                   \
    X(PirateBaseCastBox)                                                                                            \
    X(PirateBaseCastEyesTable)                                                                                      \
    X(PirateBaseCastMouthTable)                                                                                     \
    /* ---- content/saul_kada/pirate_base_rooms */                                                                  \
    X(PirateBaseRoomsSt3bBaseSpawns)                                                                                \
    X(PirateBaseRoomsSt3bBaseMoreSpawns)                                                                            \
    X(PirateBaseRoomsSt3bBaseLaterSpawns)                                                                           \
    X(PirateBaseRoomsSt3bBaseLaterMoreSpawns)                                                                       \
    X(PirateBaseRoomsSt3bHallSpawns)                                                                                \
    X(PirateBaseRoomsSt3bHallMoreSpawns)                                                                            \
    X(PirateBaseRoomsSt3bHallLastSpawns)                                                                            \
    X(SaulKadaRoomsSt3cLaterRooms)                                                                                  \
    X(SaulKadaRoomsSt3cLaterLinks)                                                                                  \
    X(PirateBaseRoomsSt3cStatueHallSpawns)                                                                          \
    X(PirateBaseRoomsSt3cRaceStartSpawns)                                                                           \
    X(PirateBaseRoomsSt3cRaceStartMoreSpawns)                                                                       \
    X(PirateBaseRoomsSt3cSub1Spawns)                                                                                \
    X(PirateBaseRoomsSt3cSub3Spawns)                                                                                \
    X(PirateBaseRoomsBaseDefeats) /* the base's defeat rows (DefeatRow), 0x12 of them ... */                        \
    X(PirateBaseRoomsMarkedDefeats) /* ... of which these 0xb: the walls of the base's group records */             \
    /* ---- content/saul_kada/pirate_base_scripts */                                                                \
    X(PirateBaseScriptsGateDebrisRows)                                                                              \
    X(PirateBaseScriptsGateFirstGuardRecord)                                                                        \
    X(PirateBaseScriptsGateSecondGuardRecord)                                                                       \
    X(PirateBaseScriptsGateThirdGuardRecord)                                                                        \
    X(PirateBaseScriptsGateLeaderRecord)                                                                            \
    X(PirateBaseScriptsGateFirstExitRecord)                                                                         \
    X(PirateBaseScriptsGateSecondExitRecord)                                                                        \
    X(PirateBaseScriptsGateCamera)                                                                                  \
    X(PirateBaseScriptsGateSteps)                                                                                   \
    X(PirateBaseScriptsArrivalBossRecord)                                                                           \
    X(PirateBaseScriptsArrivalSparklerRecord)                                                                       \
    X(PirateBaseScriptsArrivalCamera)                                                                               \
    X(PirateBaseScriptsArrivalSteps)                                                                                \
    X(PirateBaseScriptsVictoryCamera)                                                                               \
    X(PirateBaseScriptsVictorySteps)                                                                                \
    X(PirateBaseScriptsDebrisTop)                                                                                   \
    X(PirateBaseScriptsDebrisSides)                                                                                 \
    /* ---- content/saul_kada/pirate_servbots */                                                                    \
    X(PirateServbotsServbotBox)                                                                                     \
    X(PirateServbotsServbotLiftBox)                                                                                 \
    X(PirateServbotsBomberBox)                                                                                      \
    X(PirateServbotsBomberLiftBox)                                                                                  \
    X(PirateServbotsLobVoices)                                                                                      \
    X(PirateServbotsPopVoices) /* u16 sounds by its voice */                                                        \
    X(PirateServbotsTauntAnims) /* two pairs (start, end) */                                                        \
    /* ---- content/saul_kada/pirate_tank */                                                                        \
    X(PirateTankBox)                                                                                                \
    X(PirateTankBurstSequence) /* the bursts' tick counts (0 ends), by count */                                     \
    X(PirateTankRecoilTimers) /* the recoil's timers, read backwards by the shots left */                           \
    /* ---- content/saul_kada/race_bonus */                                                                         \
    X(RaceBonusBox)                                                                                                 \
    /* ---- content/saul_kada/sand_burrower */                                                                      \
    X(SandBurrowerBox)                                                                                              \
    /* ---- content/saul_kada/sand_trap */                                                                          \
    X(SandTrapBox)                                                                                                  \
    /* ---- content/saul_kada/saul_kada_rooms */                                                                    \
    X(SaulKadaRoomsSt2ARooms)                                                                                       \
    X(SaulKadaRoomsSt2ALinks)                                                                                       \
    X(SaulKadaRoomsSt2ALaterRooms)                                                                                  \
    X(SaulKadaRoomsSt2ALaterLinks)                                                                                  \
    X(SaulKadaRoomsSt2BRooms)                                                                                       \
    X(SaulKadaRoomsSt2BLinks)                                                                                       \
    X(SaulKadaRoomsSt2BLaterRooms)                                                                                  \
    X(SaulKadaRoomsSt2BLaterLinks)                                                                                  \
    X(SaulKadaRoomsSt2CRooms)                                                                                       \
    X(SaulKadaRoomsSt2CLinks)                                                                                       \
    X(SaulKadaRoomsSt2CLaterRooms)                                                                                  \
    X(SaulKadaRoomsSt2CLaterLinks)                                                                                  \
    X(SaulKadaRoomsSt3BRooms)                                                                                       \
    X(SaulKadaRoomsSt3BLinks)                                                                                       \
    X(SaulKadaRoomsSt3BLaterRooms)                                                                                  \
    X(SaulKadaRoomsSt3BLaterLinks)                                                                                  \
    X(SaulKadaRoomsSt29Rooms)                                                                                       \
    X(SaulKadaRoomsSt29Links)                                                                                       \
    X(SaulKadaRoomsSt29LaterRooms)                                                                                  \
    X(SaulKadaRoomsSt29LaterLinks)                                                                                  \
    X(SaulKadaRoomsSt3cRooms)                                                                                       \
    X(SaulKadaRoomsSt3cLinks)                                                                                       \
    X(SaulKadaRoomsSt48Rooms)                                                                                       \
    X(SaulKadaRoomsSt48Links)                                                                                       \
    X(SaulKadaRoomsSt48LaterRooms)                                                                                  \
    X(SaulKadaRoomsSt48LaterLinks)                                                                                  \
    /* ---- content/saul_kada/saul_kada_town_rooms */                                                               \
    X(SaulKadaTownRoomsSt29SquareSpawns)                                                                            \
    X(SaulKadaTownRoomsSt29StreetRows)                                                                              \
    X(SaulKadaTownRoomsSt2aStreetRows)                                                                              \
    X(SaulKadaTownRoomsSt2bMineSpawns)                                                                              \
    X(SaulKadaTownRoomsSt2bMineFirstSpawns)                                                                         \
    X(SaulKadaTownRoomsSt2bMineSecondSpawns)                                                                        \
    X(SaulKadaTownRoomsSt2bMineThirdSpawns)                                                                         \
    X(SaulKadaTownRoomsSt48DesertSpawns)                                                                            \
    X(SaulKadaTownRoomsSt48DesertMoreSpawns)                                                                        \
    X(SaulKadaTownRoomsSt29CrowdSpawns)                                                                             \
    X(SaulKadaTownRoomsSt29LargeCrowdSpawns)                                                                        \
    X(SaulKadaTownRoomsSt2CSpawns)                                                                                  \
    X(SaulKadaTownRoomsSt48TownSpawns)                                                                              \
    X(SaulKadaTownRoomsSt48TownMoreSpawns)                                                                          \
    /* ---- content/saul_kada/smoke_puff */                                                                         \
    X(SmokePuffBox)                                                                                                 \
    /* ---- content/saul_kada/st13_spin_blade */                                                                    \
    X(St13SpinBladeBox)                                                                                             \
    /* ---- content/saul_kada/st24_script_walker */                                                                 \
    X(St24ScriptWalkerBox)                                                                                          \
    /* ---- content/saul_kada/st26_patrol_guard */                                                                  \
    X(St26PatrolGuardBox)                                                                                           \
    /* ---- content/saul_kada/st27_bon */                                                                           \
    X(St27BonBox)                                                                                                   \
    /* ---- content/saul_kada/st27_mission */                                                                       \
    X(St27MissionOpeningCorners)                                                                                    \
    X(St27MissionRunCorners)                                                                                        \
    /* ---- content/saul_kada/st27_tron */                                                                          \
    X(St27TronBox)                                                                                                  \
    /* ---- content/saul_kada/st28_bomb_ribbon */                                                                   \
    X(St28BombRibbonTexelsEven)                                                                                     \
    X(St28BombRibbonTexelsOdd) /* per quad 4 texels (s16 u, v) */                                                   \
    /* ---- content/saul_kada/st28_boss_shell */                                                                    \
    X(St28BossShellBox)                                                                                             \
    /* ---- content/saul_kada/st28_cast_props */                                                                    \
    X(St28CastPropsDroppedBox)                                                                                      \
    /* ---- content/saul_kada/st28_fire_bomb */                                                                     \
    X(St28FireBombBox)                                                                                              \
    X(St28FireBombLiftBox)                                                                                          \
    /* ---- content/saul_kada/st28_guard */                                                                         \
    X(St28GuardAnimLists) /* by variant: the address of its animation list */                                       \
    X(St28GuardShotAnims) /* the shot's animation entries, by its index */                                          \
    X(St28GuardBox)                                                                                                 \
    X(St28GuardGlowPoints) /* its two glow trails' points on its head (s16 x, y, z) */                              \
    /* ---- content/saul_kada/st28_guard_shot */                                                                    \
    X(St28GuardShotBoxes) /* by variant (12 bytes each) */                                                          \
    /* ---- content/saul_kada/st28_gunner_beam */                                                                   \
    X(St28GunnerBeamBox)                                                                                            \
    /* ---- content/saul_kada/st28_gunner_bomb */                                                                   \
    X(St28GunnerBombBox)                                                                                            \
    /* ---- content/saul_kada/st28_gunner_boss */                                                                   \
    X(St28GunnerBossBox)                                                                                            \
    X(St28GunnerBossHelperOffsets)                                                                                  \
    /* ---- content/saul_kada/st28_gunner_helpers */                                                                \
    X(St28GunnerHelpersHelperBox)                                                                                   \
    X(St28GunnerHelpersHelperLiftBox)                                                                               \
    X(St28GunnerHelpersSlotOffsets) /* by side: the slot point beside the boss (s16 x / z) */                       \
    /* ---- content/saul_kada/st28_lava_boss */                                                                     \
    X(St28LavaBossStandingBox)                                                                                      \
    X(St28LavaBossLyingBox)                                                                                         \
    X(St28LavaBossLiftBox)                                                                                          \
    X(St28LavaBossSpinningLiftBox)                                                                                  \
    X(St28LavaBossArenaBox)                                                                                         \
    X(St28LavaBossArenaPoints) /* the walk's box and points (by in the box) */                                      \
    X(St28LavaBossBombBox)                                                                                          \
    X(St28LavaBossBombPoints) /* the bomb's walk back */                                                            \
    X(St28LavaBossPairBox)                                                                                          \
    X(St28LavaBossPairPoints) /* the orbiting bombs' walk back */                                                   \
    /* ---- content/saul_kada/st2a_slide_block */                                                                   \
    X(St2aSlideBlockBox)                                                                                            \
    /* ---- content/saul_kada/st2b_cannon_boss */                                                                   \
    X(St2bCannonBossBox)                                                                                            \
    X(St2bCannonBossSmokePoints) /* the barrel's smoke points (s16 x / y / z << 4, 8 bytes each) */                 \
    /* ---- content/saul_kada/st2b_cannon_shell */                                                                  \
    X(St2bCannonShellBox)                                                                                           \
    /* ---- content/saul_kada/st2b_props */                                                                         \
    X(St2bPropsPropBox)                                                                                             \
    X(St2bPropsPotLiftBox)                                                                                          \
    /* ---- content/saul_kada/st33_falling_rock */                                                                  \
    X(St33FallingRockBox)                                                                                           \
    /* ---- content/saul_kada/st33_key_script */                                                                    \
    X(St33KeyScriptKeyDropCamera)                                                                                   \
    X(St33KeyScriptKeyDropSteps)                                                                                    \
    X(St33KeyScriptKeyDropFigureRecord)                                                                             \
    X(St33KeyScriptKeyDropEndSpawns)                                                                                \
    X(St33KeyScriptKeyDropEndMoreSpawns)                                                                            \
    X(St33KeyScriptKeyItemBox)                                                                                      \
    /* ---- content/saul_kada/st34_flyer_shell */                                                                   \
    X(St34FlyerShellBox)                                                                                            \
    X(St34FlyerShellLiftBox)                                                                                        \
    /* ---- content/saul_kada/st34_ruin_flyer */                                                                    \
    X(St34RuinFlyerBox)                                                                                             \
    X(St34RuinFlyerLiftBox)                                                                                         \
    /* ---- content/saul_kada/st34_ruin_sentry */                                                                   \
    X(St34RuinSentryBox)                                                                                            \
    X(St34RuinSentryTalkBox)                                                                                        \
    /* ---- content/saul_kada/st3b_bomb_turret */                                                                   \
    X(St3bBombTurretBox)                                                                                            \
    /* ---- content/saul_kada/st3b_crate */                                                                         \
    X(St3bCrateBox)                                                                                                 \
    /* ---- content/saul_kada/st3b_glow */                                                                          \
    X(St3bGlowBox)                                                                                                  \
    /* ---- content/saul_kada/st3b_grey_figure */                                                                   \
    X(St3bGreyFigureBox)                                                                                            \
    /* ---- content/saul_kada/st3b_pirate_carrier */                                                                \
    X(St3bPirateCarrierBox)                                                                                         \
    X(St3bPirateCarrierTalkBox)                                                                                     \
    /* ---- content/saul_kada/st3b_script5c_cast */                                                                 \
    X(St3bScript5cCastFigureBox)                                                                                    \
    /* ---- content/saul_kada/st3c_gold_boss */                                                                     \
    X(St3cGoldBossBox)                                                                                              \
    X(St3cGoldBossArenaFloorBox)                                                                                    \
    X(St3cGoldBossChaseBox)                                                                                         \
    X(St3cGoldBossRingCells) /* four cell boxes (4 bytes each) */                                                   \
    X(St3cGoldBossRingCentres) /* s16 x, y, z (8 bytes each) */                                                     \
    X(St3cGoldBossArenaCells)                                                                                       \
    X(St3cGoldBossLeapScript)                                                                                       \
    X(St3cGoldBossReleapScript)                                                                                     \
    X(St3cGoldBossAttack1Script)                                                                                    \
    X(St3cGoldBossFloorY16)                                                                                         \
    X(St3cGoldBossAttack2Script)                                                                                    \
    X(St3cGoldBossAttack3Script)                                                                                    \
    X(St3cGoldBossLungeBackScript)                                                                                  \
    X(St3cGoldBossLungeDiveScript)                                                                                  \
    /* ---- content/saul_kada/st3c_gold_piece */                                                                    \
    X(St3cGoldPieceGoldBox)                                                                                         \
    /* ---- content/saul_kada/st3c_minion */                                                                        \
    X(St3cMinionBox)                                                                                                \
    /* ---- content/saul_kada/st3c_minion_spawner */                                                                \
    X(St3cMinionSpawnerPlaceBox)                                                                                    \
    X(St3cMinionSpawnerNearBox)                                                                                     \
    X(St3cMinionSpawnerMinionBox)                                                                                   \
    /* ---- content/saul_kada/st3c_pirate_cast */                                                                   \
    X(St3cPirateCastPirateBox)                                                                                      \
    /* ---- content/saul_kada/st3c_props */                                                                         \
    X(St3cPropsPropBox)                                                                                             \
    /* ---- content/saul_kada/st3c_rising_model */                                                                  \
    X(St3cRisingModelBox)                                                                                           \
    /* ---- content/saul_kada/st3c_statue */                                                                        \
    X(St3cStatueBox)                                                                                                \
    X(St3cStatuePartBox)                                                                                            \
    /* ---- content/saul_kada/st3c_talk_prop */                                                                     \
    X(St3cTalkPropTalkBox)                                                                                          \
    /* ---- content/saul_kada/st40_head_npc */                                                                      \
    X(St40HeadNpcBoxes) /* 12 bytes by variant */                                                                   \
    /* ---- content/saul_kada/st41_carrier */                                                                       \
    X(St41CarrierBox)                                                                                               \
    /* ---- content/saul_kada/st41_carrier_pod */                                                                   \
    X(St41CarrierPodBox)                                                                                            \
    X(St41CarrierPodLungeTicks) /* u16 by the player's distance >> 6 */                                             \
    X(St41CarrierPodLungeClasses) /* by the live pods: the lunge chances' row */                                    \
    X(St41CarrierPodLungeChances) /* the row + 0..7: 0 = no lunge */                                                \
    /* ---- content/saul_kada/st41_crumbling_platform */                                                            \
    X(St41CrumblingPlatformBox)                                                                                     \
    X(St41CrumblingPlatformPieceBox)                                                                                \
    /* ---- content/saul_kada/st41_hover_gunner */                                                                  \
    X(St41HoverGunnerGroundBox)                                                                                     \
    X(St41HoverGunnerAirBox)                                                                                        \
    /* ---- content/saul_kada/st41_pirate_shot */                                                                   \
    X(St41PirateShotBoxes) /* by variant (12 bytes each) */                                                         \
    /* ---- content/saul_kada/st41_ring */                                                                          \
    X(St41RingBox)                                                                                                  \
    /* ---- content/saul_kada/st41_sliding_door */                                                                  \
    X(St41SlidingDoorBox)                                                                                           \
    /* ---- content/saul_kada/st41_st44_rooms */                                                                    \
    X(St41St44RoomsSt41GuardsA)                                                                                     \
    X(St41St44RoomsSt41GuardsB)                                                                                     \
    X(St41St44RoomsSt41Pair)                                                                                        \
    X(St41St44RoomsSt42GuardsA)                                                                                     \
    X(St41St44RoomsSt42GuardsB)                                                                                     \
    X(St41St44RoomsSt42Pair)                                                                                        \
    X(St41St44RoomsSt42PairB)                                                                                       \
    X(St41St44RoomsSt42GuardsC)                                                                                     \
    /* ---- content/saul_kada/st48_charger */                                                                       \
    X(St48ChargerBox)                                                                                               \
    X(St48ChargerHopAnims) /* the hop's animation, by the current one */                                            \
    X(St48ChargerJetPoints) /* the jets' points (s16 x, y, z; 8 bytes each) */                                      \
    /* ---- content/saul_kada/st48_gunship */                                                                       \
    X(St48GunshipShipBox)                                                                                           \
    X(St48GunshipProbeBox)                                                                                          \
    X(St48GunshipStartSubs) /* by variant (4 bytes each), indexed by its start index */                             \
    /* ---- content/saul_kada/st48_sand_script */                                                                   \
    X(St48SandScriptCameraScript)                                                                                   \
    X(St48SandScriptSteps)                                                                                          \
    /* ---- content/saul_kada/st4c_bobber */                                                                        \
    X(St4cBobberBox)                                                                                                \
    /* ---- content/saul_kada/st52_cast */                                                                          \
    X(St52CastStandingBox)                                                                                          \
    X(St52CastStandingEyes)                                                                                         \
    X(St52CastStandingMouth)                                                                                        \
    X(St52CastTimedBox)                                                                                             \
    X(St52CastTimedEyes)                                                                                            \
    X(St52CastTimedMouth)                                                                                           \
    /* ---- content/saul_kada/trapped_miner */                                                                      \
    X(TrappedMinerBox)                                                                                              \
    /* ---- content/saul_kada/two_form_reaverbot */                                                                 \
    X(TwoFormReaverbotFormBox)                                                                                      \
    /* ---- content/saul_kada/window_giant */                                                                       \
    X(WindowGiantSideBoxes)

enum class TableId : uint16_t {
#define OD_TABLE_ID_ENUM(name) name,
    OD_TABLE_IDS(OD_TABLE_ID_ENUM)
#undef OD_TABLE_ID_ENUM
};
// the number of table ids
inline constexpr uint16_t kTableIds = 0
#define OD_TABLE_ID_COUNT(name) +1
    OD_TABLE_IDS(OD_TABLE_ID_COUNT)
#undef OD_TABLE_ID_COUNT
    ;

inline const char* tableName(TableId id) {
    static constexpr const char* kNames[] = {
#define OD_TABLE_ID_NAME(name) #name,
        OD_TABLE_IDS(OD_TABLE_ID_NAME)
#undef OD_TABLE_ID_NAME
    };
    return kNames[static_cast<uint16_t>(id)];
}

} // namespace od
