// The actor classes of content/intro in the class registry (game/content_registry), registered by
// content/registry.cpp: the attract / ST00 classes (moved from the registry table of game/actors.cpp, same entries,
// same order), the opening's ST1E / ST3A classes (moved from game/combat in Task 1.5) and the opening's cutscene cast
// and effects of ST39 / ST3A (moved from game/story in Task 1.7).
#include "content/registry.hpp"

#include "content/intro/st4a_event_triggers.hpp"

#include "content/intro/crash_landing.hpp"
#include "content/intro/fire_mission.hpp"
#include "content/intro/opening.hpp"
#include "content/intro/opening_effects.hpp"
#include "content/intro/st00_figures.hpp"
#include "content/intro/st03_props.hpp"
#include "content/intro/st39_script68.hpp"
#include "content/intro/attract_effects.hpp"
#include "content/intro/st02_attract_cast.hpp"
#include "content/intro/st03_attract_cast.hpp"

namespace od::content {

using namespace od::port;

void registerIntroClasses(ContentRegistry& r) {
    r.addClasses({
        // the tutorial's event triggers (ST4A pool e0 type 0xa)
        {RoutineId::St4aEventTriggerUpdate, &st4aEventTriggerUpdate},
        {RoutineId::St02PropUpdate, &st02PropUpdate},
        {RoutineId::St02DriftingStaticUpdate, &st02DriftingStaticUpdate},
        {RoutineId::St02RollUpdate, &st02RollUpdate},
        {RoutineId::St02BarrelUpdate, &st02BarrelUpdate},
        {RoutineId::St02PlacedStaticUpdate, &st02PlacedStaticUpdate},
        {RoutineId::St02WatcherUpdate, &st02WatcherUpdate},
        {RoutineId::St02TalkerUpdate, &st02TalkerUpdate},
        {RoutineId::St02FaceUpdate, &st02FaceUpdate},
        {RoutineId::St02SpeakerUpdate, &st02SpeakerUpdate},
        {RoutineId::St02ScreenUpdate, &st02ScreenUpdate},
        {RoutineId::St03PieceCarrierUpdate, &st03PieceCarrierUpdate},
        {RoutineId::St03WalkerUpdate, &st03WalkerUpdate},
        {RoutineId::St03FlutterUpdate, &st03FlutterUpdate},
        {RoutineId::St03PlacedModelUpdate, &st03PlacedModelUpdate},
        {RoutineId::AttractVramTileUpdate, &attractVramTileUpdate},
        {RoutineId::St00FigureUpdate, &st00FigureUpdate},
        {RoutineId::St02StillModelUpdate, &st02StillModelUpdate},
        {RoutineId::AttractLightningUpdate, &attractLightningUpdate},
        {RoutineId::St02SceneryUpdate, &st02SceneryUpdate},
        {RoutineId::AttractRingFlashUpdate, &attractRingFlashUpdate},
        {RoutineId::St03StillModelUpdate, &st03StillModelUpdate},
        {RoutineId::St03SceneryUpdate, &st03SceneryUpdate},
        {RoutineId::St03RollUpdate, &st03RollUpdate},
        {RoutineId::St03BarrelUpdate, &st03BarrelUpdate},
        {RoutineId::St03WatcherUpdate, &st03WatcherUpdate},
        {RoutineId::St03CraftBodyUpdate, &st03CraftBodyUpdate},
        {RoutineId::St03LightsUpdate, &st03LightsUpdate},
        {RoutineId::St02BlinkerUpdate, &st02BlinkerUpdate},
        {RoutineId::St02StanderUpdate, &st02StanderUpdate},
        {RoutineId::St03BlinkerUpdate, &st03BlinkerUpdate},
        {RoutineId::St03StanderUpdate, &st03StanderUpdate},
        {RoutineId::St03PartBlinkerUpdate, &st03PartBlinkerUpdate},
        {RoutineId::St03TalkerUpdate, &st03TalkerUpdate},
        {RoutineId::St03FaceUpdate, &st03FaceUpdate},
        {RoutineId::St03SpeakerUpdate, &st03SpeakerUpdate},
        {RoutineId::St03ScreenUpdate, &st03ScreenUpdate},
        {RoutineId::St03CraftUpdate, &st03CraftUpdate},
        // the opening's playable part: the Flutter's fire mission (ST1E) and its crash landing (ST3A, script 6)
        {RoutineId::FireUpdate, &fireUpdate},
        {RoutineId::BurningDataUpdate, &burningDataUpdate},
        {RoutineId::ResultSceneDataUpdate, &resultSceneDataUpdate},
        {RoutineId::WallBlastUpdate, &wallBlastUpdate},
        {RoutineId::FlameUpdate, &flameUpdate},
        {RoutineId::FireEffectUpdate, &fireEffectUpdate},
        {RoutineId::FallingPieceUpdate, &fallingPieceUpdate},
        {RoutineId::SkyShipUpdate, &skyShipUpdate},
        {RoutineId::CloudRingUpdate, &cloudRingUpdate},
        {RoutineId::PiecesDropperUpdate, &piecesDropperUpdate},
        {RoutineId::BoneRiderUpdate, &boneRiderUpdate},
        {RoutineId::CrashFlashUpdate, &crashFlashUpdate},
        // the opening's cutscene cast (ST39, script 5) and effect class (ST39 / ST3A)
        {RoutineId::OpeningFlutterUpdate, &openingFlutterUpdate},
        {RoutineId::OpeningCrewUpdate, &openingCrewUpdate},
        {RoutineId::ScriptAnimatedUpdate, &scriptAnimatedUpdate},
        {RoutineId::OpeningTalkerUpdate, &openingTalkerUpdate},
        {RoutineId::OpeningEffectUpdate, &openingEffectUpdate},
        // the crash landing's cutscene cast (ST3A, script 6)
        {RoutineId::CrashPropUpdate, &crashPropUpdate},
        {RoutineId::CrashPieceUpdate, &crashPieceUpdate},
        {RoutineId::CrashRollUpdate, &crashRollUpdate},
        {RoutineId::CrashBarrelUpdate, &crashBarrelUpdate},
        {RoutineId::CrashSecondBodyUpdate, &crashSecondBodyUpdate},
        {RoutineId::CrashTalkerUpdate, &crashTalkerUpdate},
        {RoutineId::CrashCraftUpdate, &crashCraftUpdate},
        {RoutineId::CrashJetUpdate, &crashJetUpdate},
        {RoutineId::CrashWatcherUpdate, &crashWatcherUpdate},
        {RoutineId::CrashFaceUpdate, &crashFaceUpdate},
        {RoutineId::CrashSpeakerUpdate, &crashSpeakerUpdate},
        {RoutineId::Script68CastUpdate, &script68CastUpdate},
        {RoutineId::St03TiltedModelUpdate, &st03TiltedModelUpdate},
    });
    // their draws (game/effects_draw): the fire mission's (content/intro/fire_mission_draw), the opening effect class's
    // (content/intro/opening_effects_draw); the textures in this order (the fire mission's, then the opening's)
    r.effectDraws.emplace(RoutineId::FireUpdate, &fireDraw);
    r.effectDraws.emplace(RoutineId::FlameUpdate, &flameDraw);
    r.effectDraws.emplace(RoutineId::FireEffectUpdate, &fireEffectDraw);
    r.effectDraws.emplace(RoutineId::OpeningEffectUpdate, &openingEffectDraw);
    r.effectTextures.push_back(&fireMissionTextures);
    r.effectTextures.push_back(&openingEffectTextures);
    r.effectGradientRows.push_back({kExhaustGlow, 0});
}

} // namespace od::content
