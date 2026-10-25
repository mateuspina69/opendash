// The game's globals in the exe's layout (game/game_state, game/story_flags): the game task's mode block, the run,
// the records, the balances, the options, the game loop's save area, the best times and the event flags.
#include "game/game_state.hpp"
#include "game/save_menus.hpp"
#include "game/story_flags.hpp"
#include "translator/layout.hpp"

namespace od::test {

using namespace od::port;

void gameGlobalsLayout(LayoutVisitor& v) {
    GameState& g = gameState();
    v.field(0x6d0006, g.frame);
    v.field(0xa6d180, g.mode);
    v.field(0xa6d181, g.modeStep);
    v.field(0xa6d182, g.modeSpare);
    v.field(0xa6d18a, g.resetHoldTicks);
    v.field(0xa6d18c, g.loadSave);
    v.field(0xa6d18d, g.entryMode);
    v.field(0xa6d18e, g.frontEnd);
    v.field(0xa6d18f, g.stageClass);
    v.field(0xa6d190, g.stage);
    v.field(0xa6d191, g.room);
    v.field(0xa6d192, g.flutterDock);
    v.field(0xa6d193, g.playerKind);
    v.field(0xa6d194, g.story);
    v.field(0xa6d195, g.nextStory);
    v.field(0xa6d196, g.difficulty);
    v.field(0xa6d197, g.savesExist);
    v.field(0xa6d198, g.bestTime);
    v.field(0xa6d19c, g.bestDifficulty);
    v.field(0xa6d19e, g.clears);
    v.field(0xa6d1a0, g.playTime);
    v.field(0xa6d1a4, g.playTimeCopy);
    v.field(0xa6d1a8, g.zenny);
    v.field(0xa6d1ac, g.unreadCounter);
    v.field(0xa6d1b0, g.damageTaken);
    v.field(0xa6d1b4, g.damageShown);
    v.field(0xa6d1b8, g.defeats);
    v.field(0xa6d1bc, g.defeatsTotal);
    v.field(0xa6d1c0, g.balance);
    v.field(0xa6d1c2, g.toolKitBalance);
    v.field(0xa6d1c4, g.balanceTier);
    v.field(0xa6d1c5, g.toolKitTier);
    v.field(0xa6d1cb, g.enemyNear);
    v.field(0xa6d1f8, g.scriptCounters);
    v.field(0xa6d1fc, g.dockCounters);
    v.field(0xa6d204, g.countedDock);
    v.field(0xa6d20c, g.entryLog);
    v.field(0xa7b080, g.clearTime);

    BestTimes& times = bestTimes();
    for (size_t i = 0; i < kBestTimes; ++i) {
        const uint32_t a = 0xa6d22c + uint32_t(i) * 0xc;
        v.field(a, times.records[i].name);
        v.field(a + 8, times.records[i].time);
    }

    GameOptions& o = gameOptions();
    v.field(0xa6d1c8, o.mono);
    v.field(0xa6d1c9, o.minimapShown);
    v.field(0xa6d1ca, o.ruby);
    v.field(0xa6d1cc, o.controlOption);
    v.field(0xa6d1cd, o.controllerLayout);
    v.field(0xa6d1ce, o.lookDirection);
    v.field(0xa6d1cf, o.vibrationOff);
    v.field(0xa6d1d0, o.autoLockSpecial);
    v.field(0xa6d1d1, o.autoLockBuster);

    GameLoopSaves& s = gameLoopSaves();
    v.field(0xa5a010, s.demoPadType);
    v.field(0xa5a014, s.statusOpenBits);
    v.field(0xa5a01c, s.statusClearBackground);
    v.field(0xa5a01d, s.demoControllerLayout);
    v.field(0xa5a01e, s.demoControlOption);
    v.field(0xa5a01f, s.demoAutoLockSpecial);
    v.field(0xa5a020, s.statusWaitBits);
    v.field(0xa5a024, s.demoLookDirection);
    v.field(0xa5a025, s.demoAutoLockBuster);

    v.field(0xa73cc0, storyFlags().bytes);
    v.field(0xa73de0, extraFlags().bytes);

    // the load / clear-data menus' block (game/save_menus) and the byte after it
    LoadMenuBlock& m = loadMenuBlock();
    v.field(0xa5a240, m.state);
    v.field(0xa5a241, m.step);
    v.field(0xa5a242, m.messageStep);
    v.field(0xa5a243, m.unread3);
    v.field(0xa5a244, m.slot);
    v.field(0xa5a246, m.flags);
    v.field(0xa5a247, m.after);
}

} // namespace od::test
