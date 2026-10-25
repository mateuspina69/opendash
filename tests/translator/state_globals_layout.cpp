// The game-state globals the systems and the content keep outside the systems' own blocks (their exe addresses in
// the members' comments), in the exe's layout.
#include "content/calbania/calbania_rooms.hpp"
#include "content/calbania/glyde_base_scripts_state.hpp"
#include "content/calbania/st32_quiz_state.hpp"
#include "content/forbidden_island/st1c_cast_state.hpp"
#include "content/intro/st02_attract_script_state.hpp"
#include "content/late_game/end_scripts_state.hpp"
#include "content/late_game/st54_clear_screen_state.hpp"
#include "content/manda/exam_enemies_b_state.hpp"
#include "content/nino/glyde_box_walkers_state.hpp"
#include "content/nino/mission_gunships_state.hpp"
#include "content/saul_kada/st3c_gold_boss_state.hpp"
#include "content/saul_kada/st3c_gold_piece_state.hpp"
#include "content/saul_kada/st3c_gold_shockwave_state.hpp"
#include "content/saul_kada/st3c_minion_spawner_state.hpp"
#include "content/saul_kada/st3c_minion_state.hpp"
#include "content/saul_kada/st3c_props_state.hpp"
#include "content/saul_kada/st3c_statue_state.hpp"
#include "content/yosyonke/abandoned_mine_scripts_state.hpp"
#include "content/yosyonke/ice_ruin_herded_creature_state.hpp"
#include "content/yosyonke/st05_entry_list_state.hpp"
#include "content/yosyonke/st0e_turret_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/breakable_walls_state.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/lens_flare_state.hpp"
#include "game/line_of_sight_state.hpp"
#include "game/player/player_model_state.hpp"
#include "game/room_params.hpp"
#include "game/shop_state.hpp"
#include "game/status_screen.hpp"
#include "game/status_screen_state.hpp"
#include "game/title_state.hpp"
#include "game/visited_rooms_state.hpp"
#include "content/saul_kada/st3c_boss_shot_state.hpp"
#include "game/text_script_state.hpp"
#include "game/draw_commands.hpp"
#include "content/forbidden_island/elephant_boss_state.hpp"
#include "game/clut_animation_state.hpp"
#include "game/sky_layers.hpp"
#include "game/guardian_reaverbot.hpp"
#include "game/visited_rooms.hpp"
#include "content/yosyonke/dev_room_state.hpp"
#include "game/shop.hpp"
#include <array>
#include "game/save_image.hpp"
#include "translator/layout.hpp"

namespace od::test {

void stateGlobalsLayout(LayoutVisitor& v) {
    v.field(0xa5a6c8, od::content::glydeBaseRoomState().escapeTicks);
    v.field(0xa59e10, od::port::guardianWeakPointBox());
    v.field(0xa5a6cc, od::content::glydeBaseRoomState().gateStaticX);
    v.field(0xa5a6ce, od::content::glydeBaseRoomState().gateStaticZ);
    v.field(0xa5a6d0, od::content::glydeBaseRoomState().rescueLatch);
    v.field(0xa5a6d4, od::content::glydeBaseRoomState().carriedActor);
    v.field(0xa5a6d8, od::content::glydeBaseRoomState().gateHintLatch);
    v.field(0xa5a6dc, od::content::glydeBaseRoomState().cellBirdbots);
    v.field(0xa5a6e8, od::content::glydeBaseRoomState().ticks);
    v.field(0xa5a6ec, od::content::glydeBaseRoomState().walkers);
    v.field(0xa5a6f8, od::content::glydeBaseRoomState().boxLatchA);
    v.field(0xa5a6f9, od::content::glydeBaseRoomState().boxLatchB);
    v.field(0xa5a6fa, od::content::glydeBaseRoomState().scriptLatch);
    v.field(0xa7ad40, od::content::glydeBaseRoomState().walkGrid);
    v.field(0xa7ad80, od::content::glydeBaseRoomState().boxes);
    v.field(0xa59f54, od::port::glyde_base_scripts_state::towerFall().smokes);
    v.field(0xa59f94, od::port::glyde_base_scripts_state::towerFall().tick);
    v.field(0xa59f96, od::port::glyde_base_scripts_state::towerFall().nextEvent);
    v.field(0xa59f98, od::port::glyde_base_scripts_state::towerFall().smokeCount);
    v.field(0xa7b02c, od::port::st32_quiz_state::quizRecords().rankFlag);
    v.field(0xa5a900, od::port::st1c_cast_state::propLinks().ship);
    v.field(0xa5a920, od::port::st1c_cast_state::propLinks().stillModel);
    v.field(0xa5a924, od::port::st1c_cast_state::propLinks().secondStillModel);
    v.field(0xa59f48, od::port::st02_attract_script_state::timedRings().cursor);
    v.field(0xa59fa0, od::port::end_scripts_state::staffRoll().segment);
    v.field(0xa59fa4, od::port::end_scripts_state::staffRoll().buffer);
    v.field(0x7ce236, od::port::end_scripts_state::staffRoll().skipped);
    v.field(0xa59fe0, od::port::st54_clear_screen_state::st54ClearScreenGlobals().clearedBytes);
    v.field(0xa59e68, od::port::exam_enemies_b_state::examDroneCount().alive);
    v.field(0xa59e54, od::port::glyde_box_walkers_state::search().targetZ);
    v.field(0xa59e58, od::port::glyde_box_walkers_state::search().targetX);
    v.field(0xa59e5c, od::port::glyde_box_walkers_state::search().depth);
    v.field(0xa5a904, od::port::mission_gunships_state::waveEntrySlots().entering);
    v.field(0xa5a905, od::port::mission_gunships_state::waveEntrySlots().taken);
    v.field(0x8ceaac, od::port::mission_gunships_state::waveEntrySlots().shipsLeft);
    v.field(0xa59e90, od::port::st3c_gold_boss_state::st3cGoldBossGlobals().bossBlock);
    v.field(0xa5a92c, od::port::st3c_gold_piece_state::st3cGoldPieceGlobals().pieceBlock);
    v.field(0xa5a930, od::port::st3c_gold_shockwave_state::st3cGoldShockwaveGlobals().waveBlock);
    v.field(0xa59e98, od::port::st3c_minion_state::st3cMinionGlobals().minionBlock);
    v.field(0xa59dd0, od::port::st3c_minion_spawner_state::st3cMinionSpawnerGlobals().spawnerBlock);
    v.field(0xa5a900, od::port::st3c_props_state::publishedLink().block);
    v.field(0xa5a928, od::port::st3c_statue_state::partLink().hitBox);
    v.field(0xa59f9c, od::port::abandoned_mine_scripts_state::liftArrival().walkStart);
    v.field(0xa59e4c, od::port::ice_ruin_herded_creature_state::iceRuinHerdedCreatureGlobals().initModelVariant);
    v.field(0xa59e50, od::port::ice_ruin_herded_creature_state::iceRuinHerdedCreatureGlobals().initPlaceIndex);
    v.field(0xa59fa8, od::port::st05_entry_list_state::st05EntryListGlobals().entryListRows);
    v.field(0xa59eb0, od::port::st0e_turret_state::turretDebug().counterA);
    v.field(0xa59eb2, od::port::st0e_turret_state::turretDebug().counterB);
    v.field(0xa59eb4, od::port::st0e_turret_state::turretDebug().shotCounter);
    v.field(0xa59eac, od::port::st0e_turret_state::turretDebug().sweepRoll);
    v.field(0xa714c0, od::port::actorStatTables().animated);
    v.field(0xa6bb84, od::port::actorStatTables().statics);
    v.field(0xa6c660, od::port::trig64Tables().sine);
    v.field(0xa72500, od::port::trig64Tables().cosine);
    v.field(0xa5a77c, od::port::breakable_walls_state::state().queuedHits);
    v.field(0xa5a780, od::port::breakable_walls_state::state().queue);
    v.field(0xa5a6fc, od::port::breakable_walls_state::state().damage);
    v.field(0xa5a880, od::port::breakable_walls_state::state().sharedDamage);
    v.field(0xa6c42c, od::port::enemyCells().mask);
    v.field(0xa59e04, od::port::lens_flare_state::tails().sun);
    v.field(0xa59dec, od::port::lens_flare_state::tails().droppingRing);
    v.field(0xa59df0, od::port::lens_flare_state::tails().pulsingDisc);
    v.field(0xa59df4, od::port::lens_flare_state::tails().shrinkingDisc);
    v.field(0xa59df8, od::port::lens_flare_state::tails().ray);
    v.field(0xa59dfc, od::port::lens_flare_state::tails().tint);
    v.field(0xa59e00, od::port::lens_flare_state::tails().sprite);
    v.field(0xa59d38, od::port::line_of_sight_state::sight().box);
    v.field(0xa59d48, od::port::line_of_sight_state::sight().target);
    v.field(0xa59d50, od::port::line_of_sight_state::sight().step);
    v.field(0xa6d1d8, od::port::player_model_state::playerFiles().body);
    v.field(0xa6d1da, od::port::player_model_state::playerFiles().weapon);
    v.field(0xa72280, od::port::roomDraw().depthRanges);
    v.field(0xa72278, od::port::roomDraw().effectsFar);
    v.field(0xa59a00, od::port::roomDraw().fogDistance);
    v.field(0xa59a50, od::port::roomDraw().fogDepth);
    v.field(0xa599e0, od::port::roomDraw().fogColour);
    v.field(0xa5aa40, od::port::shop_state::shopGlobals().ownedParts);
    v.field(0xa5e52c, od::port::status_screen_state::highlightClut().kept);
    v.field(0xa5e54c, od::port::status_screen_state::highlightClut().pulsed);
    v.field(0xa7a020, od::port::statusList().entries);
    v.field(0xa59de4, od::port::title_state::titleKeeps().classTables);
    v.field(0xa59de8, od::port::title_state::titleKeeps().clearBackground);
    v.field(0xa5a5c0, od::port::visited_rooms_state::routeSearch().path);
    v.field(0xa5a5b8, od::port::visited_rooms_state::routeSearch().depth);
    v.field(0xa5a538, od::port::visited_rooms_state::routeSearch().branches);
    v.field(0xa5a6c0, od::port::visited_rooms_state::routeSearch().branchTop);
    v.field(0xa5a640, od::port::visited_rooms_state::routeSearch().best);
    v.field(0xa5a5bc, od::port::visited_rooms_state::routeSearch().bestDepth);
    v.field(0xa6d1d4, od::port::player_model_state::playerFiles().before);
    v.field(0xa6d208, od::port::roomDraw().params);
    // (by hand) the stage's enemy cells, the shop's item list, the text codes' operands, the boss shot's blocks
    for (size_t i = 0; i < od::port::enemyCells().entries.size(); ++i) v.field(0x7de700 + uint32_t(i) * 4, od::port::enemyCells().entries[i]);
    v.field(0xa5a940, od::port::shop_state::shopGlobals().itemList);
    v.field(0x8b171e, od::port::text_script_state::insertedTexts().warpOperands);
    v.field(0xa5a938, od::port::st3c_boss_shot_state::bossShotGlobals().homingBlock);
    v.field(0xa5a934, od::port::st3c_boss_shot_state::bossShotGlobals().pieceBlock);
    v.field(0xa5a912, od::port::elephant_boss_state::elephantGlobals().trailMarker);
    // the stage's and the live colour tables
    v.field(od::port::clut_animation_state::kStageCluts, od::port::clut_animation_state::clutTables().stage);
    v.field(od::port::clut_animation_state::kLiveCluts, od::port::clut_animation_state::clutTables().live);
    // the map's visited-room block
    {
        od::port::VisitedRoomBlock& b = od::port::visitedRooms();
        v.field(0xa71740, b.routeFlags);
        v.field(0xa71741, b.listId);
        v.field(0xa71742, b.targetStage);
        v.field(0xa71743, b.targetRoom);
        v.field(0xa71744, b.nextLink);
        v.field(0xa71745, b.roomCount);
        v.field(0xa71746, b.linkCount);
        v.field(0xa71747, b.unread07);
        v.field(0xa71748, b.flagBase);
        v.field(0xa7174c, b.rooms);
        v.field(0xa71750, b.linkRecords);
    }
    // the shop's menus, the ST05 entry list and the development room's menus (their list cursors inside)
    {
        auto cursor = [&](uint32_t at, od::port::ListCursor& c) {
            v.field(at + 4, c.row);
            v.field(at + 5, c.firstRow);
            v.field(at + 6, c.rows);
            v.field(at + 7, c.visibleRows);
            v.field(at + 8, c.rowStep);
            v.field(at + 9, c.scrollStep);
        };
        od::port::ShopState& sh = od::port::shopState();
        v.field(0xa5aa60, sh.menu);
        v.field(0xa5aa61, sh.step);
        v.field(0xa5aa62, sh.subStep);
        v.field(0xa5aa63, sh.listStep);
        cursor(0xa5aa60, sh.list);
        v.field(0xa5aa6a, sh.unread0a);
        v.field(0xa5aa6b, sh.unread0b);
        v.field(0xa5aa6c, sh.item);
        v.field(0xa5aa6e, sh.kind);
        v.field(0xa5aa6f, sh.page);
        v.field(0xa5aa70, sh.price);
        od::port::st05_entry_list_state::EntryListBlock& el = od::port::st05_entry_list_state::entryListBlock();
        v.field(0xa59fd0, el.menuState);
        v.field(0xa59fd1, el.menuStep);
        v.field(0xa59fd2, el.asking);
        v.field(0xa59fd3, el.unread03);
        cursor(0xa59fd0, el.list);
        v.field(0xa59fda, el.unread0a);
        v.field(0xa59fdb, el.unread0b);
        v.field(0xa59fdc, el.entry);
        for (int k = 0; k < 2; ++k) {
            const uint32_t at = k ? 0xa5a1c8 : 0xa5a0d0;
            od::port::dev_room_state::DevRoomMenu& m = k ? od::port::dev_room_state::devRoomMenus().nino : od::port::dev_room_state::devRoomMenus().roll;
            v.field(at - 0x80, m.listEntries);
            v.field(at + 0x00, m.page);
            v.field(at + 0x01, m.state);
            v.field(at + 0x02, m.step);
            v.field(at + 0x03, m.subStep);
            cursor(at, m.list);
            v.field(at + 0x0a, m.unread0a);
            v.field(at + 0x0b, m.unread0b);
            for (size_t i = 0; i < 5; ++i) v.field(at + 0x0c + uint32_t(i), m.barLength[i]);
            for (size_t i = 0; i < 5; ++i) v.field(at + 0x11 + uint32_t(i), m.barPrevious[i]);
            v.field(at + 0x16, m.barSlide);
            v.field(at + 0x18, m.price);
            v.field(at + 0x1c, m.weaponItem);
            v.field(at + 0x1e, m.itemUnder);
            v.field(at + 0x20, m.chosenWeapon);
            v.field(at + 0x21, m.modelWeapon);
            v.field(at + 0x22, m.stat);
            v.field(at + 0x23, m.recipeKind);
            v.field(at + 0x24, m.recipeOther);
            v.field(at + 0x26, m.recipeResult);
            v.field(at + 0x28, m.recipeMessage);
            v.field(at + 0x2a, m.openingMessage);
            v.field(at + 0x2c, m.fieldPoint);
            v.field(at + 0x32, m.fieldYaw);
            v.field(at + 0x34, m.modelActor);
        }
    }
    // the status sub-screen's block
    {
        od::port::StatusScreen& b = od::port::statusScreen();
        const uint32_t at = 0xa7a140;
        for (uint32_t k = 0; k < 6; ++k) v.field(at + k, b.level(k));
        v.field(at + 0x06, b.switching);
        v.field(at + 0x07, b.savedPace);
        v.field(at + 0x08, b.savedRapidLevel);
        v.field(at + 0x09, b.savedWeapon);
        v.field(at + 0x0a, b.slideIn);
        v.field(at + 0x0b, b.slideX);
        v.field(at + 0x0c, b.manualStep);
        v.field(at + 0x0d, b.unread0d);
        v.field(at + 0x10, b.savedBankBuffer);
        v.field(at + 0x11, b.pageDat);
        v.field(at + 0x12, b.unread12);
        v.field(at + 0x14, b.cursor);
        v.field(at + 0x15, b.cursorKept);
        v.field(at + 0x16, b.cursorShown);
        v.field(at + 0x17, b.unread17);
        v.field(at + 0x18, b.listRow);
        v.field(at + 0x19, b.listScroll);
        v.field(at + 0x1a, b.listRows);
        v.field(at + 0x1b, b.listVisible);
        v.field(at + 0x1c, b.cursorStep);
        v.field(at + 0x1d, b.rowStep);
        v.field(at + 0x1e, b.scrollStep);
        v.field(at + 0x1f, b.answer);
        v.field(at + 0x20, b.switchPage);
        v.field(at + 0x21, b.switchState);
        v.field(at + 0x22, b.switchRow);
        v.field(at + 0x23, b.switchCountdown);
        v.field(at + 0x24, b.savedBusterLevels);
        v.field(at + 0x29, b.modelWeapon);
        v.field(at + 0x2a, b.padKind);
        v.field(at + 0x2b, b.testRumble);
        v.field(at + 0x2c, b.barLength);
        v.field(at + 0x31, b.barPrevious);
        v.field(at + 0x36, b.barSlide);
        v.field(at + 0x38, b.item);
        v.field(at + 0x3a, b.listLength);
        v.field(at + 0x3b, b.listKind);
        v.field(at + 0x3c, b.unread3c);
        v.field(at + 0x3d, b.result);
        v.field(at + 0x3e, b.unread3e);
        v.field(at + 0x40, b.modelActor);
    }
    // the save images (the save buffer, the game-clear image): their 0xc00 bytes
    for (const uint32_t at : {od::port::kSaveBuf, od::port::kClearDataImage}) {
        od::port::SaveImage& img = od::port::saveImageAt(at);
        std::array<uint8_t, od::port::kSaveSize> b = img.bytes();
        if (!v.interprets(at, b.size())) continue;
        v.bytes(at, b.data(), b.size());
        if (v.loading()) img.setBytes(b);
    }
    // the sky layers and their clocks
    {
        od::port::SkyState& sky = od::port::skyState();
        for (size_t i = 0; i < od::port::kSkyLayers; ++i) {
            od::port::SkyLayer& l = sky.layers[i];
            const uint32_t a = 0xa6c440 + uint32_t(i) * 0x30;
            v.field(a + 0x00, l.flags);
            v.field(a + 0x01, l.mode);
            v.field(a + 0x02, l.unread02);
            v.field(a + 0x04, l.scrollX);
            v.field(a + 0x08, l.scrollY);
            v.field(a + 0x0c, l.lastScrollX);
            v.field(a + 0x10, l.lastScrollY);
            v.field(a + 0x14, l.base);
            v.field(a + 0x16, l.horizontalOffset);
            v.field(a + 0x18, l.scrollSpeedX);
            v.field(a + 0x1a, l.scrollSpeedY);
            v.field(a + 0x1c, l.width);
            v.field(a + 0x1e, l.height);
            v.field(a + 0x20, l.clut);
            v.field(a + 0x22, l.tpage);
            v.field(a + 0x24, l.colour);
            v.field(a + 0x28, l.drawMode);
            v.field(a + 0x2a, l.unread2a);
            v.field(a + 0x2c, l.tiles);
        }
        v.field(0xa6bb88, sky.ticks);
        v.field(0xa6c428, sky.parallax);
        v.field(0xa723a0, sky.roomList);
    }
    // the draw-command buffers and their cursor
    v.field(od::dc::kCursor, od::dc::drawCommands().cursor);
    v.field(od::dc::kBufferBase, od::dc::drawCommands().words);
}

} // namespace od::test
