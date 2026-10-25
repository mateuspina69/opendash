// The actor classes of content/calbania in the class registry (game/content_registry), registered by
// content/registry.cpp. Moved from the registry table of game/actors.cpp (same entries, same order).
#include "content/registry.hpp"

#include "content/calbania/calbania_spawners.hpp"

#include "content/calbania/carried_npc.hpp"
#include "content/calbania/ch3_script_cast.hpp"
#include "content/calbania/ch3_story.hpp"
#include "content/calbania/glyde_base_bombers.hpp"
#include "content/calbania/glyde_base_gun.hpp"
#include "content/calbania/glyde_base_key.hpp"
#include "content/calbania/glyde_base_scene_birdbots.hpp"
#include "content/calbania/glyde_base_tank.hpp"
#include "content/calbania/glyde_base_turret.hpp"
#include "content/calbania/st1f_birdbot_tank.hpp"
#include "content/calbania/st21_props.hpp"
#include "content/calbania/st22_burrower.hpp"
#include "content/calbania/st22_fire_shots.hpp"
#include "content/calbania/st24_npc.hpp"
#include "content/calbania/st24_structure.hpp"
#include "content/calbania/st32_quiz.hpp"
#include "content/calbania/talking_npc.hpp"
#include "content/calbania/tron_boss.hpp"

namespace od::content {

using namespace od::port;

void registerCalbaniaClasses(ContentRegistry& r) {
    r.addClasses({
        // the pool-e0 type-0xa enemy spawners of the area's stages
        {RoutineId::SpawnerSt1fUpdate, &spawnerSt1fUpdate},
        {RoutineId::SpawnerSt21Update, &spawnerSt21Update},
        {RoutineId::SpawnerSt22Update, &spawnerSt22Update},
        {RoutineId::AlarmFlashUpdate, &alarmFlashUpdate},
        {RoutineId::EscapeTimerUpdate, &escapeTimerUpdate},
        {RoutineId::TronBossUpdate, &tronBossUpdate},
        {RoutineId::St24RouteNpcUpdate, &st24RouteNpcUpdate},
        {RoutineId::AppoDahFightUpdate, &appoDahFightUpdate},
        {RoutineId::Script1dAppoDahUpdate, &script1dAppoDahUpdate},
        {RoutineId::Script1fAppoDahUpdate, &script1fAppoDahUpdate},
        {RoutineId::AppoDahBombUpdate, &appoDahBombUpdate},
        {RoutineId::AppoDahPuppetUpdate, &appoDahPuppetUpdate},
        {RoutineId::SittingPuppetUpdate, &sittingPuppetUpdate},
        {RoutineId::TownBirdUpdate, &townBirdUpdate},
        {RoutineId::TownStandingCastUpdate, &townStandingCastUpdate},
        {RoutineId::AppoDahGuideUpdate, &appoDahGuideUpdate},
        {RoutineId::AppoDahGatePuppetUpdate, &appoDahGatePuppetUpdate},
        {RoutineId::St24StructureUpdate, &st24StructureUpdate},
        {RoutineId::TalkingNpcUpdate, &talkingNpcUpdate},
        {RoutineId::TalkingNpcSt17Update, &talkingNpcSt17Update},
        {RoutineId::CarriedNpcUpdate, &carriedNpcUpdate},
        {RoutineId::St1fBirdbotTankUpdate, &st1fBirdbotTankUpdate},
        {RoutineId::QuizCastUpdate, &quizCastUpdate},
        {RoutineId::LurePieceUpdate, &lurePieceUpdate},
        {RoutineId::St25RisingModelUpdate, &st25RisingModelUpdate},
        {RoutineId::BurrowerUpdate, &burrowerUpdate},
        {RoutineId::Script69CastUpdate, &script69CastUpdate},
        {RoutineId::Script20AppoDahUpdate, &script20AppoDahUpdate},
        {RoutineId::Script3cCastUpdate, &script3cCastUpdate},
        {RoutineId::St22FireShotUpdate, &st22FireShotUpdate},
        // Glyde's base (ST30 / ST31)
        {RoutineId::GlydeBaseTurretUpdate, &glydeBaseTurretUpdate},
        {RoutineId::GlydeBaseGunUpdate, &glydeBaseGunUpdate},
        {RoutineId::GlydeBaseTankUpdate, &glydeBaseTankUpdate},
        {RoutineId::GlydeBaseBomberUpdate, &glydeBaseBomberUpdate},
        {RoutineId::CellSceneBirdbotUpdate, &cellSceneBirdbotUpdate},
        {RoutineId::GlydeBaseSceneBirdbotUpdate, &glydeBaseSceneBirdbotUpdate},
        {RoutineId::GlydeBaseKeyUpdate, &glydeBaseKeyUpdate},
    });
}

} // namespace od::content
