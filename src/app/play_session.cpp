#include "app/play_session.hpp"
#include "app/lang_sfx.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <tuple>

#include "content/calbania/ch3_story.hpp"
#include "content/late_game/st59.hpp"
#include "content/calbania/st32_quiz.hpp"
#include "content/yosyonke/st05_entry_list.hpp"
#include "content/manda/exam_spawner.hpp"
#include "content/nino/field_spawners.hpp"
#include "engine/dat.hpp"
#include "engine/data_source.hpp"
#include "engine/room.hpp"
#include "engine/vram.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/breakable_walls.hpp"
#include "game/camera.hpp"
#include "game/minimap.hpp"
#include "game/collision.hpp"
#include "content/yosyonke/dev_room.hpp"
#include "game/encounter.hpp"
#include "game/hit_list.hpp"
#include "game/game_init.hpp"
#include "game/game_state.hpp"
#include "game/input_bindings.hpp"
#include "game/menu_banks.hpp"
#include "game/save_image.hpp"
#include "game/save_menus.hpp"
#include "game/status_screen.hpp"
#include "game/message.hpp"
#include "game/event.hpp"
#include "game/language_pack.hpp"
#include "game/story.hpp"
#include "game/text_script.hpp"
#include "game/message_text.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_model.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_update.hpp"
#include "game/shop.hpp"
#include "game/room_scripts.hpp"
#include "game/spawn.hpp"
#include "game/stage_setup.hpp"
#include "game/sky_layers.hpp"
#include "game/stage_music.hpp"
#include "game/vram_moves.hpp"
#include "game/actor_sweeps.hpp"
#include "game/display.hpp"
#include "game/fade.hpp"
#include "game/room_params.hpp"
#include "game/transition.hpp"
#include "game/actors.hpp"
#include "game/audio_logic.hpp"
#include "game/camera_state.hpp"
#include "game/event_state.hpp"
#include "game/fade_state.hpp"
#include "game/message_state.hpp"
#include "game/player/player_pose.hpp"
#include "game/player/player_weapons.hpp"
#include "game/projection_state.hpp"
#include "game/room_grid.hpp"
#include "game/save_slots.hpp"
#include "game/script_state.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"
#include "game/transition_state.hpp"
#include "game/door_actor.hpp"
#include "game/clut_animation_state.hpp"


namespace od::app {

void PlayerView::playerFiles() {
    for (uint16_t i : od::playerDatRequest()) {
        const std::string name = od::datFileName(i);
        if (name.empty()) continue;
        try {
            const od::GameFile f = od::dataSource().gameFile(name);
            files.mapCode(f);
            // the container loader 0x40eb90 hands each type-8 segment to Maybe_LoadSfxBank 0x5d5010 (0x40ed1d): a
            // special weapon file's sounds (slot 3, its bank id)
            if (mixer)
                for (const od::FilePart& seg : f.parts)
                    if (seg.kind == od::FilePart::Kind::WavSfxBank)
                        od::lang_sfx::load(pack.get(), name, seg.bytes, seg.arg, seg.extra);
        } catch (const std::exception& e) {
            SDL_Log("player file %s: %s", name.c_str(), e.what());
            continue;
        }
        if (name.rfind("PL00P", 0) == 0) g_playerBodyDat = name;
        else if (name.rfind("PL00R", 0) == 0) g_playerWeaponDat = name;
        SDL_Log("player file %s (index %u) mapped", name.c_str(), i);
    }
}

void PlayerView::init(const od::Config& cfg) {
    discData = cfg.data == "disc";
    try {
        files.mapCode(od::dataSource().gameFile("PL00P000.DAT"));
        od::port::registerBuiltData(kFrameByte, frameKey);
        od::port::registerBuiltData(kStageParams, stageParams);
        files.zero(od::port::kMessageBank, 0x4000); // the stage's message bank (type-6 segment, <= 0x351e B)
        od::port::messageInit();
        od::port::inputConfigDefaults(); // the exe's input set-up 0x409a0c (the bindings' defaults)
        od::port::buildSin64Tables(); // the 64-step sin / cos tables the exe builds at boot
        actors.log = [](const std::string& msg) { SDL_Log("%s", msg.c_str()); };
        for (int k = 0; k + 1 < 42; ++k)
            frames.push_back(int(od::port::playerFrameOffset(uint32_t(k + 1)) - od::port::playerFrameOffset(uint32_t(k))) / 64);
        ok = true;
    } catch (const std::exception& e) {
        SDL_Log("player view disabled: %s", e.what());
    }
}

void PlayerView::tick() {
    if (ok && playing && !frames.empty() && !play) frame = (frame + 1) % std::max(frames[size_t(anim)], 1);
}

void PlayerView::loadMessageBank() {
    if (!bankRoomFile) return;
    std::vector<uint8_t> bank(0x4000, 0);
    const std::string name = "msg/" + std::filesystem::path(bankRoomName).stem().string() + ".bin";
    if (pack && pack->has(name)) {
        const auto b = pack->file(name);
        std::copy_n(b.begin(), std::min(b.size(), bank.size()), bank.begin());
    } else {
        if (pack) SDL_Log("lang: %s missing in %s, native bank", name.c_str(), pack->id().c_str());
        for (const od::FilePart& seg : bankRoomFile->parts)
            if (seg.kind == od::FilePart::Kind::PcMessages || seg.kind == od::FilePart::Kind::Ps1Messages)
                std::copy_n(seg.bytes.begin(), std::min(seg.bytes.size(), bank.size()), bank.begin());
    }
    files.map(od::port::kMessageBank, bank);
    bankLanguage = language;
}

std::unique_ptr<od::lang::LangPack> PlayerView::makePack(od::lang::Language l) const {
    if (l == od::lang::Language::Japanese) return nullptr;
    if (l == od::lang::Language::English && disc) {
        try {
            return od::lang::discPack(disc, "disc:" + std::string(od::lang::code(l)));
        } catch (const std::exception& e) {
            SDL_Log("lang: the disc image has no English pack: %s", e.what());
            return nullptr;
        }
    }
    if (!od::lang::packAvailable(langDir, l)) return nullptr;
    return od::lang::folderPack(od::lang::packDir(langDir, l));
}

bool PlayerView::packOffered(od::lang::Language l) const {
    if (l == od::lang::Language::Japanese) return !discData;
    if (const auto it = packOfferedCache.find(l); it != packOfferedCache.end()) return it->second;
    return packOfferedCache[l] = makePack(l) != nullptr;
}

const od::lang::LangPack* PlayerView::labelPack() {
    if (!labelsTried) {
        labelsTried = true;
        labels = makePack(od::lang::Language::Portuguese);
        if (!labels) labels = makePack(od::lang::Language::English);
    }
    return labels.get();
}

bool PlayerView::restoredAvailable() const {
    const auto dir = pack ? pack->folder() : std::filesystem::path();
    std::error_code ec;
    return !dir.empty() && std::filesystem::is_directory(dir / "snd_restored", ec);
}

void PlayerView::applyVoiceDirs() {
    if (!mixer) return;
    const auto dir = pack ? pack->folder() : std::filesystem::path();
    std::vector<std::filesystem::path> dirs;
    if (!dir.empty()) {
        if (restoredVoices && restoredAvailable()) dirs.push_back(dir / "snd_restored");
        dirs.push_back(dir / "snd");
    }
    mixer->setVoiceDirs(std::move(dirs));
    if (pack && dir.empty()) // the disc's voices (engine/lang_pack): the clips of its XA files
        mixer->setVoiceSource([p = pack](const std::string& f, uint32_t t) { return p->voiceTrack(f, t); });
    else if (discData) {
        std::shared_ptr<const od::lang::LangPack> fallback = makePack(od::lang::Language::English);
        mixer->setVoiceSource([p = std::move(fallback)](const std::string& f, uint32_t t) {
            return p ? p->voiceTrack(f, t) : std::vector<uint8_t>();
        }, true);
    }
    else
        mixer->setVoiceSource(nullptr);
}

void PlayerView::setLanguage(od::lang::Language l) {
    if (discData && l == od::lang::Language::Japanese) l = od::lang::Language::English;
    pack = makePack(l);
    if (l != od::lang::Language::Japanese && !pack) {
        SDL_Log("lang: no %s pack (disc image or %s)", od::lang::code(l), langDir.string().c_str());
        l = discData ? od::lang::Language::English : od::lang::Language::Japanese;
        pack = makePack(l);
    }
    language = l;
    applyVoiceDirs();
    if (mixer) od::lang_sfx::reloadAll(pack.get()); // the voice barks (app/lang_sfx)
    loadExeBanks();
    if (bankStage >= 0 && bankLanguage != l) loadMessageBank();
    SDL_Log("lang: %s", od::lang::code(l));
}

void PlayerView::loadExeBanks() {
    od::port::MenuBanks b = od::port::exeMenuBanks();
    std::vector<std::pair<uint32_t, uint32_t>> redirects;
    if (pack) {
        if (!files.mapped(kLangBanks, 0x10000)) files.zero(kLangBanks, 0x10000);
        uint32_t at = kLangBanks;
        auto put = [&](const char* name, uint32_t& field) {
            const std::string f = "exe/" + std::string(name) + ".bin";
            if (!pack->has(f)) {
                SDL_Log("lang: %s missing in %s: Japanese", f.c_str(), pack->id().c_str());
                return;
            }
            const auto d = pack->file(f);
            if (at + d.size() > kLangBanks + 0x10000) return;
            files.map(at, d);
            field = at;
            at = (at + uint32_t(d.size()) + 3u) & ~3u;
        };
        put("item_names", b.names);
        put("item_descs", b.descs);
        put("status_help", b.help);
        put("items_help", b.itemsHelp);
        put("item_use", b.use);
        put("status_misc", b.misc);
        put("areas", b.areas);
        put("map_help", b.mapHelp);
        put("map_display", b.mapDisplay);
        put("opt_help", b.optHelp);
        put("opt_camera", b.optCamera);
        put("opt_lock", b.optLock);
        put("opt_lockmode", b.optLockMode);
        put("opt_vibration", b.optVibration);
        put("opt_sound", b.optSound);
        put("opt_volume", b.optVolume);
        put("opt_defaults", b.optDefaults);
        put("equip_help", b.equipHelp);
        put("equip_weapon", b.equipWeapon);
        put("equip_buster", b.equipBuster);
        put("equip_sort", b.equipSort);
        put("equip_body", b.equipBody);
        put("shop", b.shop);
        put("shop_prices", b.shopPrices);
        // the load / save menus (game/save_menus) and fb 44's place names: the PS1 memory-card screens' banks
        uint32_t sys = 0, load = 0, save = 0, places = 0;
        put("system", sys);
        put("load_menu", load);
        put("save_menu", save);
        put("places", places);
        uint32_t dev = 0; // Roll's development room (content/yosyonke/dev_room): the PS1 Flutter overlay's bank
        put("dev_room", dev);
        redirects = {{od::stateAddress(od::TableId::SaveMenusCardMessageBank), sys},
                     {od::stateAddress(od::TableId::SaveMenusLoadTextBank), load},
                     {od::stateAddress(od::TableId::SaveMenusSaveTextBank), save},
                     {od::stateAddress(od::TableId::TextScriptPlaceNameBank), places},
                     {od::stateAddress(od::TableId::DevRoomBank), dev}};
    }
    od::port::clearBankRedirects();
    for (auto [exe, pack] : redirects) od::port::setBankRedirect(exe, pack);
    od::port::setMenuBanks(b);
    od::port::setPs1Text(pack != nullptr);
    { // the layout pass's advance = the pack font's (game/message)
        std::vector<uint8_t> w;
        if (pack && pack->has("widths.bin")) w = pack->file("widths.bin");
        od::port::setPs1Widths(w.size() >= 256 ? w.data() : nullptr, w.size() > 256 ? w[256] : 0x6b);
    }
    od::port::setNameBank(b.names);
}

void PlayerView::reportUnported() {
    static constexpr struct {
        uint32_t base, stride, count;
    } kPools[] = {{0xa6e3a0, 0x16c, 24}, {0xa78420, 0xdc, 32}, {0xa67860, 0xcc, 64}, {0xa6c6e0, 0x28, 64},
                  {0xa6bfa0, 0x20, 32}};
    for (const auto& pd : kPools)
        for (uint32_t i = 0; i < pd.count; ++i) {
            const uint32_t a = pd.base + i * pd.stride;
            const od::port::ActorHeader& actor = od::port::actorHeaderAt(a);
            if ((actor.flags & od::port::kActorLive) && !actorLive(a))
                SDL_Log("PORT GAP: actor %06x (slot %02x type %02x sub %02x) of ST%02X sub %d has no ported class", a,
                        unsigned(actor.slot), unsigned(actor.type), unsigned(actor.subtype),
                        unsigned(od::port::gameState().stage), int(od::port::gameState().room));
        }
}

void PlayerView::spawnNative(const RoomMesh& mesh, int param) {
    {
        if (param >= 0) od::port::killAllActors();
        if (spawnStage != mesh.stage) {
            od::port::stageSetup(); // FUN_005d5a80's [0x8d3808 + stage * 4]
            spawnStage = mesh.stage;
        }
        if (param >= 0) od::port::actorSystem().boneBlocksUsed = 0; // every pool-0x20 actor is gone: no bone block in use
        od::port::roomSetupActors(param);
        reportUnported();
        int live = 0;
        for (uint32_t i = 0; i < 24; ++i)
            live += od::port::actorHeaderAt(od::port::kAnimatedPool + i * 0x16c).flags & od::port::kActorLive;
        for (uint32_t i = 0; i < 64; ++i)
            live += od::port::actorHeaderAt(od::port::kStaticPool + i * 0xcc).flags & od::port::kActorLive;
        SDL_Log("actors: ST%02X sub %d spawned by the port (story %u): %d actors in pools 0x20 / 0x60", mesh.stage,
                mesh.sub, unsigned(od::port::gameState().story), live);
        if (std::getenv("OD_CLASSAUDIT")) { // dev: every handler of the stage's class tables vs the port registry
            const auto code = [](uint32_t h) { return h >= 0x401000 && h < 0x700000; };
            const auto scan = [&](const char* pool, uint32_t t, bool twoLevel) {
                if (!t || !od::port::dataMapped(t, 4)) return;
                // a class table or a table of class tables
                for (uint32_t ty = 0; ty < 0x80 && od::port::dataMapped(t + ty * 4, 4); ++ty) {
                    const uint32_t e = od::port::classTableEntry(t, ty);
                    if (!twoLevel) {
                        if (code(e)) SDL_Log("AUDIT ST%02X %s type %02x -> %06x %s", mesh.stage, pool, ty, e,
                                             od::port::portedActorClass(od::stateRoutine(e).id()) ? "ported"
                                                                                                    : "NOT_PORTED");
                        continue;
                    }
                    if (!e || code(e) || !od::port::dataMapped(e, 4)) continue;
                    for (uint32_t sb = 0; sb < 0x10 && od::port::dataMapped(e + sb * 4, 4); ++sb) {
                        const uint32_t h = od::port::classTableEntry(e, sb);
                        if (!code(h)) break;
                        SDL_Log("AUDIT ST%02X %s type %02x sub %x -> %06x %s", mesh.stage, pool, ty, sb, h,
                                od::port::portedActorClass(od::stateRoutine(h).id()) ? "ported" : "NOT_PORTED");
                    }
                }
            };
            const od::port::StageClassTables& tables = od::port::stageClassTables();
            scan("p20", tables.animated.value(), true);
            scan("p60", tables.statics.value(), true);
            scan("p40", tables.projectiles.value(), false);
            scan("a0", tables.effects.value(), false);
            scan("e0", tables.controllers.value(), false);
            scan("g10/40", od::port::globalShotClasses.stateAddress(), false);
            scan("gA0", od::port::globalEffectClasses.stateAddress(), false);
            scan("g60", od::port::globalStaticClasses.stateAddress(), false);
        }
    }
}

void PlayerView::weaponPolicy() {
    od::port::Player& pl = od::port::player();
    const uint32_t w = uint8_t(pl.activeSpecial);
    if (w == 0) return;
    const od::RoutineRef shot = od::port::globalShotClasses[od::port::weaponShotType(w)];
    if (!od::port::specialArmPorted(w) || !od::port::portedActorClass(shot.id())) {
        SDL_Log("special weapon %u not ported (arm 0x%06x shot 0x%06x): disabled in play mode (the buster works)",
                w, od::port::specialArmHandlerOf(w).pointerValue(), shot.pointerValue());
        pl.activeSpecial = 0;
    }
}

void PlayerView::clutAnimationMirror() {
    std::vector<uint8_t> rows(0x2000); // 0x40edb0: mode 1 VRAM y 240.. -> 0xa74400, mode 2 y 496.. -> 0xa75400
    g_roomVram.readImage(0, 240, 256, 8, rows.data());
    g_roomVram.readImage(0, 496, 256, 8, rows.data() + 0x1000);
    od::port::clut_animation_state::ClutTables& tables = od::port::clut_animation_state::clutTables();
    for (size_t i = 0; i < tables.stage.size(); ++i) {
        const uint32_t w = uint32_t(rows[i * 4]) | uint32_t(rows[i * 4 + 1]) << 8 | uint32_t(rows[i * 4 + 2]) << 16 |
                           uint32_t(rows[i * 4 + 3]) << 24;
        tables.stage[i] = w;
        tables.live[i] = w;
    }
    clutShown = rows;
}

namespace {

bool overlaps(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}
// a move kept for the rebuilds (g_keptVramMoves); an earlier one it writes over whole goes, when nothing after that
// one read what it wrote and this one does not read it either (the replay's VRAM is the same without it)
void keepVramMove(const KeptVramMove& m) {
    const od::port::VramMove& v = m.move;
    auto& kept = g_keptVramMoves;
    for (size_t i = 0; i < kept.size();) {
        const od::port::VramMove& k = kept[i].move;
        bool drop = kept[i].afterDats == m.afterDats && k.dx >= v.dx && k.dy >= v.dy && k.dx + k.w <= v.dx + v.w &&
                    k.dy + k.h <= v.dy + v.h && !overlaps(v.sx, v.sy, v.w, v.h, k.dx, k.dy, k.w, k.h);
        for (size_t j = i + 1; drop && j < kept.size(); ++j) {
            const od::port::VramMove& later = kept[j].move;
            if (overlaps(later.sx, later.sy, later.w, later.h, k.dx, k.dy, k.w, k.h)) drop = false;
        }
        if (drop) kept.erase(kept.begin() + std::ptrdiff_t(i));
        else ++i;
    }
    kept.push_back(m);
}

} // namespace

void PlayerView::vramMovesApply(const RoomMesh& mesh, od::Renderer& r) {
    const std::vector<od::port::VramMove> moves = od::port::takeVramMoves();
    if (!play || moves.empty()) return;
    for (const od::port::VramMove& v : moves) keepVramMove({g_stageDats.size(), v});
    std::vector<uint8_t> buf;
    std::vector<char> dirty(mesh.sliceTex.size(), 0);
    for (const od::port::VramMove& v : moves) {
        buf.resize(size_t(v.w) * size_t(v.h) * 2);
        g_roomVram.readImage(v.sx, v.sy, v.w, v.h, buf.data());
        g_roomVram.loadImage(v.dx, v.dy, v.w, v.h, buf);
        for (size_t s = 0; s < mesh.sliceTex.size(); ++s) {
            if (!(mesh.sliceTex[s] >> 32)) continue;
            const uint32_t tpage = uint32_t(mesh.sliceTex[s] >> 16) & 0xFFFF, clut = uint32_t(mesh.sliceTex[s]) & 0xFFFF;
            const int px = int(tpage & 0xf) * 64, py = int((tpage >> 4) & 1) * 256, pw = 64 << ((tpage >> 7) & 3);
            const int cx = int(clut & 0x3f) * 16, cy = int(clut >> 6), cw = ((tpage >> 7) & 3) ? 256 : 16;
            auto hit = [&](int x, int y, int w, int h) {
                return x < v.dx + v.w && v.dx < x + w && y < v.dy + v.h && v.dy < y + h;
            };
            if (hit(px, py, pw, 256) || hit(cx, cy, cw, 1)) dirty[s] = 1;
        }
    }
    for (size_t s = 0; s < dirty.size(); ++s)
        if (dirty[s])
            r.updateSlice(int(s), g_roomVram.decode(uint16_t(mesh.sliceTex[s] >> 16), uint16_t(mesh.sliceTex[s] & 0xFFFF), true));
}

void PlayerView::clutAnimationSync(const RoomMesh& mesh, od::Renderer& r) {
    if (!play || clutShown.empty()) return;
    std::vector<uint8_t> cur(0x2000); // the live tables (0xa76400) as the frame uploads them
    {
        const od::port::clut_animation_state::ClutTables& tables = od::port::clut_animation_state::clutTables();
        for (size_t i = 0; i < tables.live.size(); ++i)
            for (size_t b = 0; b < 4; ++b) cur[i * 4 + b] = uint8_t(tables.live[i] >> (8 * b));
    }
    const bool lowRows = od::port::display().maskTicks == 0 && od::port::display().mode == 0;
    if (std::equal(cur.begin(), cur.end(), clutShown.begin())) return;
    bool changedLow = false, changedHigh = false;
    for (size_t i = 0; i < 0x2000; ++i)
        if (cur[i] != clutShown[i]) (i < 0x1000 ? changedLow : changedHigh) = true;
    clutShown.assign(cur.begin(), cur.end());
    if (changedLow && lowRows) g_roomVram.loadImage(0, 240, 256, 8, std::span<const uint8_t>(clutShown.data(), 0x1000));
    if (changedHigh) g_roomVram.loadImage(0, 496, 256, 8, std::span<const uint8_t>(clutShown.data() + 0x1000, 0x1000));
    for (size_t s = 0; s < mesh.sliceTex.size(); ++s) {
        if (!(mesh.sliceTex[s] >> 32)) continue;
        const uint16_t clut = uint16_t(mesh.sliceTex[s] & 0xFFFF), tpage = uint16_t(mesh.sliceTex[s] >> 16);
        const int cy = clut >> 6;
        if ((changedLow && lowRows && cy >= 240 && cy < 248) || (changedHigh && cy >= 496 && cy < 504))
            r.updateSlice(int(s), g_roomVram.decode(tpage, clut, true));
    }
}

void PlayerView::bindRoomData(const RoomMesh& mesh, int param) {
    od::port::actorCollisionReset();
    od::port::GameState& game = od::port::gameState();
    game.stage = uint8_t(mesh.stage);
    game.room = uint8_t(mesh.sub);
    if (const od::port::SpawnCoverage cov = od::port::spawnCoverage(); !cov.all())
        SDL_Log("PORT GAP: ST%02X sub %d (story %u): %s", mesh.stage, mesh.sub, unsigned(game.story), cov.why.c_str());
    clutAnimationMirror(); // the CLUT rows the animated CLUTs start from (0xa74400 / 0xa76400)
    // combat (game/hit_list, game/lock_on_targets, the shots and effects): the effects pool 0xa0 and its masks, the hit
    // list, the lock-on projection
    // state (the renderer sets it), the wall-hit list, the enemy tables (*0xa714c0 attack table, *0xa71808 territories)
    // a pool's slot ids (set at boot: the pool's class + the index; the allocators keep them)
    const auto slotIds = [](uint32_t pool, uint32_t stride, uint32_t count, uint32_t cls) {
        for (uint32_t i = 0; i < count; ++i) {
            od::port::ActorHeader& slot = od::port::actorHeaderAt(pool + i * stride);
            if (slot.slot == 0) slot.slot = uint8_t(cls + i);
        }
    };
    slotIds(od::port::kEffectPool, 0x28, 64, 0xa0);
    slotIds(od::port::kShotPool, 0x100, 16, 0x10);
    slotIds(od::port::kAnimatedPool, 0x16c, 24, 0x20);
    slotIds(od::port::kProjectilePool, 0xdc, 32, 0x40);
    slotIds(od::port::kStaticPool, 0xcc, 64, 0x60);
    slotIds(od::port::kControllerPool, 0x20, 32, 0xe0);
    od::port::actorCollisionReset();
    od::port::stage().coordinateShift = uint8_t(mesh.shift);
    if (bankStage != mesh.stage) { // message bank: reloaded at stage load like the game (see loadMessageBank)
        bankRoomFile = mesh.roomFile;
        bankRoomName = mesh.roomFileName;
        loadMessageBank();
        bankStage = mesh.stage;
    }
    game.stage = uint8_t(mesh.stage);
    game.room = uint8_t(mesh.sub);
    // the per-stage setup's Maybe_SetupRoomParams: the exe's param struct of the room (fog distance, far plane
    // 0x6d0012 = the cull of the lock-on reticle / cloud puffs, LOD distances); a stand-in with the shift only when
    // the struct is unknown or disagrees with the loaded shift
    if (mesh.roomParams && od::port::dataMapped(mesh.roomParams, od::port::RoomParams::kBytes) &&
        od::port::dataRecord<od::port::RoomParams>(mesh.roomParams, od::port::RoomParams::kBytes).coordinateShift ==
            uint32_t(mesh.shift)) {
        od::port::roomParamsSetup(mesh.roomParams);
    } else {
        stageParams.fill(0);
        stageParams[3] = uint8_t(mesh.shift); // +3: the coordinate shift
        od::port::roomDraw().params.pointAt(kStageParams);
    }
    // the room model bank (*0x6d0074: Maybe_BindActorModel, the animation / skeleton pointers of the actors), at
    // OpenDash's own address (the native spawn points the actors into it)
    if (!mesh.bank.empty()) {
        const uint32_t at = kBank;
        if (!mesh.bankSegment.empty() && at >= mesh.bankOffset) {
            // FUN_005f3200: the segment's leading records sit before the bank -- the player's stage animations
            // (ids >= 0x80, cutscene poses: player +0x298 = 0xa71fb8) and the 0xa71fbc table
            const uint32_t segBase = at - mesh.bankOffset;
            files.map(segBase, mesh.bankSegment);
            if (mesh.playerAnims >= 0) od::port::player().stageAnims = segBase + uint32_t(mesh.playerAnims);
            if (mesh.bankExtra >= 0) od::port::player().stageAnimsExtra = segBase + uint32_t(mesh.bankExtra) + 4;
        } else {
            files.map(at, mesh.bank);
        }
        od::port::stage().modelBank = at;
        bankAt = at;
    }
    const auto block = od::roomBlock(*mesh.roomFile);
    const od::RoomGrid grid = od::roomGrids(block).at(size_t(mesh.grid));
    roomBlock = od::FileDataBlock(block);
    files.mapShared(kRoomBlock, roomBlock.share());
    env.room = od::collisionRoom(roomBlock.share(), grid);
    od::port::RoomGridState& activeGrid = od::port::roomGrid();
    activeGrid.block = kRoomBlock;
    activeGrid.pool = kRoomBlock + uint32_t(env.room.pool);
    activeGrid.cells = kRoomBlock + uint32_t(env.room.cells);
    activeGrid.segments = kRoomBlock + uint32_t(env.room.segments);
    activeGrid.originX = uint8_t(env.room.x0);
    activeGrid.originZ = uint8_t(env.room.z0);
    activeGrid.lastX = uint8_t(grid.x1);
    activeGrid.lastZ = uint8_t(grid.z1);
    activeGrid.width = uint8_t(env.room.w);
    activeGrid.depth = uint8_t(env.room.h);
    // 0x6d009b: the room struct's +7 (Maybe_SetupRoomParams; bit 0x80 = switchable segments: breakable walls /
    // floors, FUN_0058d570) stays as roomParamsSetup set it; 0 with the stand-in struct
    if (od::port::roomDraw().params.value() == kStageParams) activeGrid.roomFlags = 0;
    roomStage = mesh.stage;
    roomSub = mesh.sub;
    spawnNative(mesh, param); // a branch the port does not cover throws: play stops with the error
    weaponPolicy();
    od::port::actorCollisionReset();
    audioBindRoom(mesh);
}

void PlayerView::audioInit(const od::Config& cfg, bool device) {
    try {
        mixer = std::make_unique<od::audio::Mixer>(discData ? std::filesystem::path() : cfg.datDir.parent_path() / "snd");
        if (device) mixer->open();
        mixer->setMasterGain(audioVolume);
        soundRec.next = mixer.get();
        od::port::setSoundBackend(&soundRec);
        od::port::soundMasters().sfx = od::port::kSoundMasterFull; // SFX / BGM masters (save +0x244, FUN_0054a790)
        od::port::soundMasters().bgm = od::port::kSoundMasterFull;
        od::port::soundReset();
        const od::GameFile init = od::dataSource().gameFile("INIT.DAT");
        for (const od::FilePart& seg : init.parts)
            if (seg.kind == od::FilePart::Kind::WavSfxBank)
                od::lang_sfx::load(pack.get(), "INIT.DAT", seg.bytes, seg.arg, seg.extra);
        SDL_Log("audio: %s, INIT bank loaded", mixer->hasDevice() ? "device" : "silent");
    } catch (const std::exception& e) {
        SDL_Log("audio disabled: %s", e.what());
        od::port::setSoundBackend(nullptr);
        mixer.reset();
    }
}

void PlayerView::audioBindRoom(const RoomMesh& mesh) {
    if (!mixer) return;
    try {
        if (mesh.stage == audioStage) return;
        audioStage = mesh.stage;
        od::port::soundFadeOut(0xf, 0x111, 0); // Maybe_GameModeStageTransition substate 2
        int banks = 0, mp3 = 0;
        for (const od::FilePart& seg : mesh.roomFile->parts)
            if (seg.kind == od::FilePart::Kind::WavSfxBank) {
                od::lang_sfx::load(pack.get(), mesh.roomFileName, seg.bytes, seg.arg, seg.extra);
                ++banks;
            }
        if (od::dataSource().hasFile(mesh.textureFile)) {
            const od::GameFile tex = od::dataSource().gameFile(mesh.textureFile);
            for (const od::FilePart& seg : tex.parts)
                if (seg.kind == od::FilePart::Kind::Mp3Music || seg.kind == od::FilePart::Kind::Ps1Music)
                    od::port::loadMp3Slot(seg.bytes, uint32_t(mp3++), seg.arg);
        }
        od::port::stageMusic(); // the stage setup's Maybe_StageSoundStart / ambience calls
        SDL_Log("audio: ST%02X: %d SFX bank(s), %d music slot(s)", mesh.stage, banks, mp3);
    } catch (const std::exception& e) {
        SDL_Log("audio: stage bind failed: %s", e.what());
    }
}

void PlayerView::audioTick() {
    if (!mixer) return;
    try {
        od::port::SoundQueue& queue = od::port::soundQueue();
        for (uint32_t i = 0, n = queue.count; i < n && i < od::port::kQueueEntries; ++i) {
            const od::port::SoundRequest q = queue.entries[i];
            ++requests;
            if (audioLog)
                SDL_Log("sound: request kind %u id 0x%03x mode %u pos %d %d %d", unsigned(q.kind), unsigned(q.id),
                        unsigned(q.mode), int(q.x), int(q.y), int(q.z));
        }
        soundRec.calls.clear();
        od::port::soundUpdate();
        for (const od::port::SoundCall& c : soundRec.calls) {
            if (c.op == od::port::SoundCall::Play) ++voiceStarts;
            if (c.op == od::port::SoundCall::StreamPlay) ++streamStarts;
            if (audioLog && (c.op == od::port::SoundCall::Play || c.op == od::port::SoundCall::StreamPlay ||
                             c.op == od::port::SoundCall::Stop || c.op == od::port::SoundCall::StreamStop ||
                             c.op == od::port::SoundCall::Volume || c.op == od::port::SoundCall::Pan))
                SDL_Log("sound: %s", od::port::soundCallText(c).c_str());
        }
        if (!mixer->hasDevice()) mixer->advance(1.0 / 30.0);
        mixer->setMasterGain(audioMute ? 0.0f : audioVolume);
        if (++audioTicks % 30 == 0 && audioLog) {
            const auto pk = mixer->takePeak();
            const auto st = mixer->stats();
            SDL_Log("audio: tick %u peak L %.3f R %.3f | buffers playing %d/%d, streams playing %d/%d | requests %d,"
                    " voice starts %d, stream starts %d", audioTicks, pk[0], pk[1], st.playingBuffers, st.buffers,
                    st.playingStreams, st.streams, requests, voiceStarts, streamStarts);
        }
    } catch (const std::exception& e) {
        SDL_Log("audio: %s -- sound logic stopped", e.what());
        od::port::setSoundBackend(nullptr);
        mixer.reset();
    }
}

void PlayerView::audioSilence() {
    if (!mixer) return;
    od::port::soundStopAll();
}

void PlayerView::bindRoom(const RoomMesh& mesh) {
    bindRoomData(mesh);
    od::port::messageInit();
    od::port::GameState& game = od::port::gameState();
    od::port::eventState().active = 0; // no event running
    game.setMode(od::port::GameMode::Field); // game mode 3 / 2 = field play (FUN_005413a0); FUN_00543bb0 only starts
    game.modeStep = uint8_t(od::port::FieldStep::Playing); // events in modes 3..4 (oracle: 0xa6d180 = 03 02 during play)
    // the game kind GAME.DAT's main-task mode sets (0x5433f0: 3 = a normal game; 2 the demo, 1 the title): a
    // start outside the title flow (--load / --play / F1) is a normal game -- the field menus need 3
    if (game.frontEnd == od::port::kFrontEndNone) game.frontEnd = od::port::kFrontEndGame;
    lastPage = 0;
    od::port::transitionRequest().clearBytes(); // no transition pending
    od::port::scriptState().clearBlockBytes();  // no script
    od::port::fadeState().covered = 0;          // no fade
    od::port::fadeState().busy = 0;
    fadeTask = {};
}

// presentation: black while a cutscene script already runs but the field loop has not started yet (mode 3 sub 0 / 1,
// the arrival fade-in). The exe fades in the room through the default camera there -- the attract's ST02 showed
// a grey-blue frame of sky / fog for half a second -- and the script's first step cuts to black anyway
bool PlayerView::cutsceneHold() const {
    const od::port::GameState& game = od::port::gameState();
    return play && game.gameMode() == od::port::GameMode::Field && game.modeStep < 2 && od::port::scriptState().id != 0;
}

float PlayerView::fade() const {
    if (cutsceneHold()) return 1.0f;
    return play ? od::port::fadeLevel(fadeTask) : 0.0f;
}

bool PlayerView::fadeWhite() const { return play && !cutsceneHold() && od::port::fadeAdditive(); }

void PlayerView::enterRoomOfState(int param) {
    const int st = od::port::gameState().stage, sb = od::port::gameState().room;
    if (gameStarted && (!room || room->stage != st)) playerFiles(); // the stage load's FUN_005d5c40
    // an in-stage room switch loads nothing on the game's side: the type-1 buffers a script's DAT loads filled
    // (g_altBankDat: their player stage animations 0xa71fb8, model bank 0x6d0074) stay as they are -- OpenDash's
    // room load re-maps the stage bank over them, so the buffers and the pointers are kept across the switch
    std::array<std::vector<uint8_t>, 2> keptBank;
    uint32_t keptPtrs[3] = {};
    bool keepBank = false;
    for (size_t k = 0; k < keptBank.size(); ++k) {
        if (!room || room->stage != st || g_altBankDat[k].empty() || !files.mapped(kAltBankBuffers[k], 0x18000)) continue;
        const auto v = files.view(kAltBankBuffers[k], 0x18000);
        keptBank[k].assign(v.begin(), v.end());
        keepBank = true;
    }
    if (keepBank) {
        keptPtrs[0] = od::port::stage().modelBank;
        keptPtrs[1] = od::port::player().stageAnims;
        keptPtrs[2] = od::port::player().stageAnimsExtra;
    }
    // the same room again through the in-stage warp (request 0xff: FUN_005e08d0 moves every actor by the exit delta,
    // then roomInit(-1); the ST0E train track wraps so every ~128 ticks): seamless in the exe, so the orbit
    // camera keeps its yaw / pitch instead of being placed behind the player like after an arrival
    const bool warpSameRoom = param < 0 && room && room->stage == st && room->sub == sb;
    if (warpSameRoom && warpPending) {
        warpPending = false;
        // the warp keeps the room: only the exe's room function [0x8d397c + stage](-1) (its actor part), not the
        // OpenDash room bind (bank / player animation pointers re-mapped: Megaman's walk / shot
        // animation fell back to the default pose on every wrap)
        try {
            od::port::roomSetupActors(param);
        } catch (const std::exception& e) {
            SDL_Log("warp: room setup failed (%s)", e.what());
        }
        return;
    }
    if (room && (room->stage != st || room->sub != sb) && loadRoom) {
        g_inStageSwitch = room->stage == st;
        loadRoom(st, sb);
        g_inStageSwitch = false;
    }
    if (!room || room->stage != st || room->sub != sb) {
        SDL_Log("transition: ST%02X sub %d could not be loaded", st, sb);
        return;
    }
    for (float& o : renderOffset) o = 0.0f; // a real arrival: the game's own frame again
    warpPending = false;
    bindRoomData(*room, param);
    if (keepBank) {
        for (size_t k = 0; k < keptBank.size(); ++k)
            if (!keptBank[k].empty()) files.map(kAltBankBuffers[k], keptBank[k]);
        od::port::stage().modelBank = keptPtrs[0];
        od::port::player().stageAnims = keptPtrs[1];
        od::port::player().stageAnimsExtra = keptPtrs[2];
    }
    const od::port::Player& pl = od::port::player();
    SDL_Log("transition: room ST%02X sub %d, player %d %d %d yaw %d", st, sb, int(pl.pos.x.whole),
            int(pl.pos.y.whole), int(pl.pos.z.whole), int(pl.rotation.y));
    snap = true; // no interpolation across the room change
    trackPosition(true);
    if (warpSameRoom) return;
    orbitBehind();
    arrivalPending = true; // the orbit camera is placed once the game is back in field play (orbitArrival)
}

void PlayerView::warpActorLog(const char* when) const {
    for (uint32_t i = 0; i < 24; ++i) {
        const uint32_t a = od::port::kAnimatedPool + i * 0x16c;
        const od::port::Actor& actor = od::port::actorAt(a);
        if (!(actor.flags & od::port::kActorLive)) continue;
        SDL_Log("  %s tick %u p20 slot %u class %06x x %d +1 %02x type %02x", when, logicTicks, i,
                od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::Animated).pointerValue(),
                int(actor.pos.x.whole), unsigned(actor.runFlags), unsigned(actor.type));
    }
    for (uint32_t i = 0; i < 64; ++i) {
        const uint32_t a = od::port::kStaticPool + i * 0xcc;
        const od::port::Actor& actor = od::port::actorAt(a);
        if (!(actor.flags & od::port::kActorLive)) continue;
        SDL_Log("  %s tick %u p60 slot %u class %06x x %d +1 %02x type %02x", when, logicTicks, i,
                od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::Statics).pointerValue(),
                int(actor.pos.x.whole), unsigned(actor.runFlags), unsigned(actor.type));
    }
}

void PlayerView::setupHooks() {
    // fb 3c (a transition from a message) starts the game's screen fade (Maybe_StartScreenFade 0x53f6b0)
    od::port::setMessageFadeHook([this](uint8_t type) { od::port::fadeStart(fadeTask, type, 0); });
    // the talking NPC's save menu (Maybe_WriteSaveFile 0x5493b0): OpenDash's saves dir, never the game's
    od::port::setSaveWriteHook([this](uint8_t slot, std::span<const uint8_t> img) { return writeSaveSlot(slot, img); });
    hooks.frame = [this] { // FUN_00542280 after the script runner
        using od::port::ActorPool;
        od::port::encounterTick(env); // FUN_0050f350: the enemy encounter groups (game/encounter)
        od::port::breakableWallsTick(); // exe 0x58cc10: the breakable-wall hits of the last frame
                                        // (game/breakable_walls)
        od::port::updatePool(env, actors, ActorPool::Controllers);
        od::port::playerInputAndDispatch(env); // the player queues itself as a mover (FUN_004140b0)
        od::port::updatePool(env, actors, ActorPool::Animated);
        od::port::updatePool(env, actors, ActorPool::Projectiles);
        od::port::updatePool(env, actors, ActorPool::Effects);
        od::port::updatePool(env, actors, ActorPool::Statics);
        od::port::actorCollision(); // FUN_004140e0, at 0x5422ad between the two player steps
        od::port::playerUpdate(env);
        od::port::updatePool(env, actors, ActorPool::Shots);
        od::port::cameraUpdate();
        od::port::updatePool(env, actors, ActorPool::EffectsLate);
        od::port::updatePool(env, actors, ActorPool::StaticsLate);
        od::port::updatePool(env, actors, ActorPool::ControllersLate);
        od::port::fieldHudFrame(); // exe 0x415560: the radar's beep, the minimap's slide (game/minimap)
        od::port::hitListPass(); // exe 0x5e4260: lock-on + pair tests (game/hit_list)
        od::port::skyLayersTick();
    };
    hooks.roomSetup = [this](int param) {
        enterRoomOfState(param);
        // the per-stage room setup's camera part: FUN_0040e290(table + sub * 12) when param >= 0 (stage / room
        // load; an in-stage switch passes < 0)
        if (param >= 0 && room && room->camera) od::port::cameraRoomInit(*room->camera);
    };
    hooks.camera = [this] { od::port::cameraUpdate(); };
    // the in-stage warp (the ST0E train track wraps every ~128 ticks): the game moved everything by (dx, 0, dz), so
    // the interpolation's previous positions / camera follow -- no cut, no snap, no AA history reset
    hooks.warp = [this](int dx, int dz) {
        if (std::getenv("OD_TRAVELLOG")) {
            SDL_Log("warp d %d %d", dx, dz);
            warpLogTicks = 4;
        }
        const float d[3] = {float(dx), 0.0f, float(dz)};
        for (int c = 0; c < 3; ++c) {
            posPrev[c] += d[c];
            posCur[c] += d[c];
            tgtPrev[c] += d[c];
            tgtCur[c] += d[c];
            camPrev.eye[c] += d[c];
            camCur.eye[c] += d[c];
        }
        const float s = room ? float(1 << room->shift) : 1.0f;
        for (int c = 0; c < 3; ++c) renderOffset[c] -= d[c] * s;
        // past the strip's far end (a fight of ~9 minutes): back by a whole number of periods -- the strip is
        // periodic, so the picture is the same; only the frame-to-frame blend / AA history restart once
        if (room && room->trackAxis >= 0) {
            const int a = room->trackAxis;
            const float span = room->trackPeriod * float(RoomMesh::kTrackTilesAhead - 8);
            if (std::abs(renderOffset[a]) > span) {
                renderOffset[a] -= std::copysign(span, renderOffset[a]);
                snap = true;
            }
        }
        warpPending = true;
    };
    // Maybe_TickRoomScripts' per-stage handler (story spawns)
    hooks.roomHandler = [this] {
        od::port::RoomScriptState& roomScript = od::port::roomScriptState();
        const uint32_t before = roomScript.actorMask;
        if (!od::port::roomScriptHandler()) return;
        if (roomScript.actorMask != before) {
            SDL_Log("actors: room script spawned (0xa71564 %08x)", unsigned(roomScript.actorMask));
            reportUnported();
        }
    };
}

bool PlayerView::actorLive(uint32_t a, bool updating) const {
    const uint32_t cls = od::port::actorSlotClass(od::port::actorHeaderAt(a).slot);
    od::RoutineRef h;
    if (cls == 0x20) {
        // the exe runs no field frame / draw pass in the status sub-screen (the field picture is the last
        // frame's): this draw-side query resolves the field actors by their own table
        h = od::port::fieldActorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::Animated, updating);
    } else if (cls == 0x60) {
        h = od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::Statics, updating);
        if (!h) h = od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::StaticsLate, updating);
    } else if (cls == 0x10) {
        h = od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::Shots, updating);
    } else if (cls == 0x40) {
        h = od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::Projectiles, updating);
    } else if (cls == 0xa0) { // effects (heat / water wave 0x431e90 ...)
        h = od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::Effects, updating);
        if (!h) h = od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::EffectsLate, updating);
    } else if (cls == 0xe0) { // controllers (animated VRAM tiles, spawners ...)
        h = od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::Controllers, updating);
        if (!h) h = od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::ControllersLate, updating);
    }
    return h && !actors.disabled.count(h.id()) && od::port::portedActorClass(h.id());
}

bool PlayerView::flagSet(uint32_t n) const { return od::port::storyFlag(n); }

void PlayerView::setFlag(uint32_t n, bool on) {
    if (on) od::port::setStoryFlag(n);
    else od::port::clearStoryFlag(n);
}

void PlayerView::logMessages() {
    const od::port::MessageWindow& w = od::port::messageWindow(0);
    const uint32_t page = (w.flags & 0x80) ? uint32_t(w.pageStart) : 0;
    if (page != lastPage && page != 0) {
        const uint32_t bank = w.bank.value();
        const od::port::MessageBank messages(bank);
        int id = -1; // the message starting at this page (pages reached through fd continue one)
        for (int k = 0; k < 0x100 && bank && messages.entryMapped(k); ++k)
            if (messages.message(k) == page) {
                id = k;
                break;
            }
        if (id >= 0) SDL_Log("message %d: %s", id, od::port::messageText(bank, id).c_str());
        else SDL_Log("message page %06x", page);
    }
    lastPage = page;
    if ((w.flags & 0x20) && lastChoice != w.readAt) { // fb 39 / fb 10 waiting
        lastChoice = w.readAt;
        SDL_Log("choice: arrows move, circle confirms, cross cancels");
    }
}

bool PlayerView::chooseSpawn(const RoomMesh& mesh, int32_t pos[3], int16_t& yaw) {
    const auto block = od::roomBlock(*mesh.roomFile);
    const od::CollisionRoom croom = od::collisionRoom(block, od::roomGrids(block).at(size_t(mesh.grid)));
    od::CollisionState cst;
    auto none = [](int32_t g) { return g == 0x7fff || g == -0x4000; };
    auto ground = [&](int x, int y, int z) -> int32_t {
        if (x < -0x7f00 || x > 0x7f00 || z < -0x7f00 || z > 0x7f00) return 0x7fff;
        return od::probeGround(croom, cst, int16_t(x), int16_t(std::clamp(y, -0x7f00, 0x7f00)), int16_t(z),
                               nullptr, 0, 1);
    };
    // floor at (x, z) reached from height y (Y down: probed from a bit above), flat on a ring; or 0x7fff
    auto standable = [&](int x, int y, int z) -> int32_t {
        const int32_t g = ground(x, y - 0x60, z);
        if (none(g) || g < y - 0x60 || g > y + 0x100) return 0x7fff;
        for (int a = 0; a < 8; ++a) {
            const int rx = x + int(std::lround(64.0 * std::cos(a * 0.7853982)));
            const int rz = z + int(std::lround(64.0 * std::sin(a * 0.7853982)));
            const int32_t r = ground(rx, g - 0x40, rz);
            if (none(r) || std::abs(r - g) > 0x30) return 0x7fff;
        }
        return g;
    };
    // 1. arrivals: the exit lists of every room (the room functions' stores), the same stage first
    struct ExitList {
        int stage, room;
        const std::vector<od::port::RoomExit>* exits;
    };
    std::vector<ExitList> tables;
    for (const auto& [st, subs] : stageSubs)
        for (int sb = 0; sb < subs; ++sb)
            if (const std::vector<od::port::RoomExit>& exits = od::port::roomExits(st, sb); !exits.empty()) {
                if (st == mesh.stage) tables.insert(tables.begin(), {st, sb, &exits});
                else tables.push_back({st, sb, &exits});
            }
    for (const int d : {192, 256, 128, 320, 96, 384, 64, 448}) {
        for (const ExitList& t : tables) {
            for (size_t k = 0; k < t.exits->size(); ++k) {
                const od::port::RoomExit& exit = (*t.exits)[k];
                if (exit.destStage != mesh.stage || exit.destRoom != mesh.sub) continue;
                const int ax = exit.destination.x, ay = exit.destination.y, az = exit.destination.z;
                const int16_t ayaw = exit.destination.yaw;
                const float a = float(ayaw) * 6.2831853f / 4096.0f;
                const int x = ax + int(std::lround(-std::sin(a) * d)), z = az + int(std::lround(-std::cos(a) * d));
                const int32_t g = standable(x, ay, z);
                if (none(g)) continue;
                pos[0] = x * 65536;
                pos[1] = g * 65536;
                pos[2] = z * 65536;
                yaw = ayaw;
                char why[64];
                std::snprintf(why, sizeof why, "arrival@ST%02X_%d#%zu+%d", t.stage, t.room, k, d);
                spawnFrom = why;
                return true;
            }
        }
    }
    // 2. grid floor nearest to the room centre (render bounds -> game units, Y down)
    const float scale = float(1 << mesh.shift);
    const int cx = int((mesh.lo[0] + mesh.hi[0]) * 0.5f / scale), cz = int((mesh.lo[2] + mesh.hi[2]) * 0.5f / scale);
    const int ytop = int(-mesh.hi[1] / scale) - 0x100;
    const int x0 = int(mesh.lo[0] / scale), x1 = int(mesh.hi[0] / scale);
    const int z0 = int(mesh.lo[2] / scale), z1 = int(mesh.hi[2] / scale);
    long best = -1;
    for (int z = z0 + 32; z < z1; z += 64)
        for (int x = x0 + 32; x < x1; x += 64) {
            const long dist = long(x - cx) * (x - cx) + long(z - cz) * (z - cz);
            if (best >= 0 && dist >= best) continue;
            for (int y = ytop, n = 0; n < 8; ++n) { // every floor level of this column, top down
                const int32_t g = ground(x, y, z);
                if (none(g) || g < y) break;
                if (!none(standable(x, g, z))) {
                    best = dist;
                    pos[0] = x * 65536;
                    pos[1] = g * 65536;
                    pos[2] = z * 65536;
                    break;
                }
                y = g + 0x40;
            }
        }
    if (best < 0) return false;
    yaw = 0;
    spawnFrom = "grid";
    return true;
}

void PlayerView::logCheck(const RoomMesh& mesh) const {
    const od::port::Player& pl = od::port::player();
    const int x = int32_t(pl.pos.x) >> 16, y = int32_t(pl.pos.y) >> 16, z = int32_t(pl.pos.z) >> 16;
    od::CollisionState cst;
    const int32_t g = od::probeGround(env.room, cst, int16_t(x), int16_t(y - 0x40), int16_t(z), nullptr, 0, 1);
    // standing = not falling (sub-state 7; 8 = landing counts once the feet are on the floor) and some
    // floor right under the feet
    const uint8_t sub = pl.subState;
    const bool onFloor = pl.playerState() == od::port::PlayerState::Playing && sub != 7 && g != 0x7fff && g != -0x4000 &&
                         std::abs(g - y) <= 0x20;
    SDL_Log("playcheck ST%02X %d %s pos %d %d %d state %d/%d ground %d room %02X/%d %s", mesh.stage, mesh.sub,
            spawnFrom.c_str(), x, y, z, int(pl.state), int(pl.subState), g, unsigned(od::port::gameState().stage),
            int(od::port::gameState().room),
            mesh.semiStart[3] + mesh.segQuads.size() * 6 < 8 * 6 ? "EMPTY" /* fewer than 8 quads: cutscene / dummy grid */ : onFloor ? "OK" : "FALL");
}

void PlayerView::drawMiss(uint32_t a, od::port::ActorPool pool, const char* why, uint32_t model) const {
    // field play only (mode 3 / 2): the room-load frames see actors still bound to the previous room's bank
    const od::port::GameState& game = od::port::gameState();
    if (game.gameMode() != od::port::GameMode::Field || game.fieldStep() != od::port::FieldStep::Playing) return;
    const uint32_t h = od::port::fieldActorHandler(od::port::actorHeaderAt(a), pool).pointerValue();
    const od::port::ActorHeader& actor = od::port::actorHeaderAt(a);
    char key[96];
    std::snprintf(key, sizeof key, "%06x %s %02x", h, why, unsigned(actor.type));
    if (!drawMissLogged.insert(key).second) return;
    SDL_Log("drawcheck: class %06x (pool slot %08x type %02x variant %u) not drawn: %s (model %08x) ST%02X/%u",
            h, a, unsigned(actor.type), unsigned(actor.variant), why, model, unsigned(game.stage), unsigned(game.room));
}

void PlayerView::logEffects() {
    int n[6] = {};
    for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t a = od::port::kShotPool + i * 0x100;
        if (od::port::actorHeaderAt(a).live() &&
            od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::Shots).is(od::RoutineId::BusterShotUpdate))
            ++n[0];
    }
    for (uint32_t i = 0; i < 64; ++i) {
        uint32_t a = od::port::kEffectPool + i * 0x28;
        if (od::port::actorHeaderAt(a).live()) {
            od::RoutineRef h = od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::EffectsLate);
            if (!h) h = od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::Effects);
            n[1] += h.is(od::RoutineId::HitSparkUpdate);
            n[2] += h.is(od::RoutineId::BurstUpdate);
        }
        a = od::port::kStaticPool + i * 0xcc;
        if (od::port::actorHeaderAt(a).live()) {
            od::RoutineRef h = od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::Statics);
            if (!h) h = od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::StaticsLate);
            n[3] += h.is(od::RoutineId::ExplosionUpdate);
            n[4] += h.is(od::RoutineId::HurtSparksUpdate);
            n[5] += h.is(od::RoutineId::PickupUpdate);
        }
    }
    int tanks = 0, tankHp = 0; // the Nino mission tanks (MissionTankUpdate) and their summed hit points
    int walkers = 0; // pool-0x20 walkers / Birdbots, knocked-down Birdbots and the bomber Birdbots alive
    for (uint32_t i = 0; i < 24; ++i) {
        const uint32_t a = od::port::kAnimatedPool + i * 0x16c;
        const od::RoutineRef h = od::port::fieldActorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::Animated);
        walkers += h.is(od::RoutineId::BirdbotSoldierUpdate) || h.is(od::RoutineId::BirdbotDownUpdate) ||
                   h.is(od::RoutineId::St18BomberBirdbotUpdate);
        if (h.is(od::RoutineId::MissionTankUpdate)) tanks += 1, tankHp += od::port::actorAt(a).hitPoints;
    }
    const od::hud::Element& life = hud.elements()[0];
    char buf[300];
    std::snprintf(buf, sizeof buf,
                  "shots %d sparks %d bursts %d explosions %d hurt %d pickups %d | hud life %d/%d fill %u trail %u x %d"
                  " | zenny %d energy %d | kills %u walkers %d tanks %d hp %d",
                  n[0], n[1], n[2], n[3], n[4], n[5], int(od::port::player().hitPoints),
                  int(od::port::player().maxHitPoints), unsigned(life.fill), unsigned(life.trail), life.x,
                  int(od::port::gameState().zenny), int(od::port::player().energyPool),
                  unsigned(od::port::gameState().defeats), walkers, tanks, tankHp);
    if (fxLast != buf) {
        SDL_Log("fxcheck tick %u: %s (%zu prims)", fx.tick, buf, fxPrims.size());
        fxLast = buf;
    }
    // the live actor classes of the pools 0x20 / 0x60 / 0xe0 (handler x count) whenever the set changes
    std::map<uint32_t, int> classes;
    const struct {
        uint32_t base, stride, count;
        od::port::ActorPool pool;
    } pools[] = {{od::port::kAnimatedPool, 0x16c, 24, od::port::ActorPool::Animated},
                 {od::port::kStaticPool, 0xcc, 64, od::port::ActorPool::Statics},
                 {od::port::kControllerPool, 0x20, 32, od::port::ActorPool::Controllers}};
    for (const auto& pl : pools)
        for (uint32_t i = 0; i < pl.count; ++i) {
            const uint32_t a = pl.base + i * pl.stride;
            const od::port::ActorHeader& actor = od::port::actorHeaderAt(a);
            if (actor.live())
                ++classes[od::port::fieldActorHandler(od::port::actorHeaderAt(a), pl.pool).pointerValue() |
                          uint32_t(actor.variant) << 24];
        }

    std::string cl;
    for (const auto& [h, n] : classes) {
        char b[32];
        std::snprintf(b, sizeof b, " %06x/v%x x%d", h & 0xffffff, h >> 24, n);
        cl += b;
    }
    if (cl != actLast) {
        SDL_Log("actcheck tick %u:%s", fx.tick, cl.c_str());
        actLast = cl;
    }
}

bool PlayerView::enterPlay(const RoomMesh& mesh) {
    playError.clear();
    leavePlay(); // entered while playing (a dev restart): the live state is the session's
    try {
        if (!ok || !mesh.roomFile) throw std::runtime_error("no player data / room");
        // the state a started game has: the session's (F2 out of a game: sessionSave), else --story-save N's, else
        // a new game (difficulty 1, as the title's new game); the room is the browsed one. Then the game start the
        // title's Continue runs (startGameFromState: equipment files, stage load, stage setup with its encounters)
        const std::vector<uint8_t>& from = sessionSave.empty() ? storySave : sessionSave;
        if (!from.empty()) {
            if (from.size() != od::port::kSaveSize || od::port::saveChecksumBad(from))
                throw std::runtime_error(sessionSave.empty() ? "--story-save: bad save" : "session state: bad image");
            od::port::saveImageAt(od::port::kSaveBuf).setBytes(from);
            od::port::gameState().loadSave = 1;
        } else {
            od::port::GameState& game = od::port::gameState();
            game.loadSave = 0;
            game.entryMode = uint8_t(od::port::EntryMode::NewGame);
            game.clearRoomToNextStory();
            game.difficulty = 1;
        }
    } catch (const std::exception& e) {
        playError = e.what();
        SDL_Log("free play failed: %s", e.what());
        return false;
    }
    SDL_Log("free play: ST%02X sub %d (%s)", mesh.stage, mesh.sub,
            !sessionSave.empty() ? "session" : storySave.empty() ? "new game" : "--story-save");
    od::port::gameState().frontEnd = od::port::kFrontEndGame; // a normal game, as startGame
    return startGameFromState(mesh.stage, mesh.sub);
}

// F2 / --play: Megaman in the browsed room -- the room's spawn (chooseSpawn), --doortest k in front of exit k,
// --teleport x y z -- after the game start bound it
void PlayerView::placeInBrowsedRoom(const RoomMesh& mesh) {
    od::port::Player& pl = od::port::player();
    int32_t spawn[3] = {mesh.playerSpawn[0], mesh.playerSpawn[1], mesh.playerSpawn[2]};
    int16_t yaw = 0;
    spawnFrom = "centre";
    if (chooseSpawn(mesh, spawn, yaw)) pl.rotation.y = yaw;
    if (doorTest >= 0) { // --doortest k: in front of exit k's trigger, facing it (door sweep)
        if (size_t(doorTest) >= mesh.exits.size()) throw std::runtime_error("doortest: no such exit");
        const od::port::RoomExit& exit = mesh.exits[size_t(doorTest)];
        const int16_t tx = exit.trigger.x, ty = exit.trigger.y, tz = exit.trigger.z;
        od::CollisionState cst;
        const auto block = od::roomBlock(*mesh.roomFile);
        const od::CollisionRoom croom = od::collisionRoom(block, od::roomGrids(block).at(size_t(mesh.grid)));
        // approach along the entry yaw (door volumes, flag 0x8000, answer an actor facing the door: verified
        // with the exe's 0x413c80 at ST09 exit 2). The volume report tries all four facings at the centre.
        yaw = exit.trigger.yaw;
        uint32_t found = 0;
        od::TriggerHit hit;
        for (int q = 0; q < 4 && !found; ++q) {
            const int16_t yq = int16_t((yaw + q * 0x400) & 0xfff);
            od::CollisionState cs2;
            found = od::triggerScan(croom, cs2, {tx, ty, tz}, yq, yq, hit);
        }
        SDL_Log("doortest volume %s type %d", found ? "yes" : "no", found ? hit.type : 0);
        for (uint32_t i = 0; i < 64 && !found; ++i) { // a door actor there: face it (FUN_0059ce50's test)
            const od::port::StaticActor& door = od::port::staticActorAt(od::port::kStaticPool + i * 0xcc);
            if ((door.flags & od::port::kActorLive) && door.type == od::port::kDoorActorType &&
                door.pos.x.whole == tx && door.pos.z.whole == tz) {
                yaw = int16_t((uint16_t(door.rotation.y) - 0x800u) & 0xfff);
                SDL_Log("doortest door actor slot %u", i);
            }
        }
        // stand on the first floor at the trigger's height going back from it (a door can face a gap:
        // ST13/0's ledge starts ~250 units out, checked with the exe's probe 0x40f1c0)
        const float a = float(yaw) * 6.2831853f / 4096.0f;
        int x = 0, z = 0;
        int32_t g = 0x7fff;
        for (int d = 64; d <= 448; d += 32) {
            x = tx + int(std::lround(std::sin(a) * float(d)));
            z = tz + int(std::lround(std::cos(a) * float(d)));
            g = od::probeGround(croom, cst, int16_t(x), int16_t(ty - 0x100), int16_t(z), nullptr, 0, 1);
            if (g != 0x7fff && g != -0x4000 && std::abs(g - ty) <= 0x80) break;
            g = 0x7fff;
        }
        if (g == 0x7fff) { // no floor at that height: 64 units out, at the trigger height
            x = tx + int(std::lround(std::sin(a) * 64.0f));
            z = tz + int(std::lround(std::cos(a) * 64.0f));
            g = ty;
        }
        spawn[0] = x * 65536;
        spawn[1] = g * 65536;
        spawn[2] = z * 65536;
        pl.rotation.y = yaw;
        SDL_Log("doortest exit %d (id %d kind %d -> ST%02X sub %d) trigger %d %d %d yaw %d", doorTest, int(exit.id),
                int(exit.byte1), unsigned(exit.destStage), int(exit.destRoom), int(tx), int(ty), int(tz), yaw);
    }
    if (teleport) { // --teleport x y z
        for (int i = 0; i < 3; ++i) spawn[i] = teleportPos[i] * 65536;
        spawnFrom = "teleport";
        if (teleportYaw >= 0) { // --yaw
            yaw = int16_t(teleportYaw & 0xfff);
            pl.rotation.y = yaw;
        }
    }
    SDL_Log("Megaman start %d %d %d yaw %d (%s)", spawn[0] / 65536, spawn[1] / 65536, spawn[2] / 65536, yaw,
            spawnFrom.c_str());
    pl.pos.x = spawn[0];
    pl.pos.y = spawn[1];
    pl.pos.z = spawn[2];
    pl.prevPos.x = spawn[0];
    pl.prevPos.y = spawn[1];
    pl.prevPos.z = spawn[2];
    od::port::soundSetListener(od::port::ListenerRef::playerPosition(),
                               od::port::ListenerRef::playerRotation(), 0); // Maybe_SetSoundListener: the player (0x573fd0)
}

void PlayerView::scanSaveSlots() {
    uint32_t bits = 0;
    for (uint8_t slot = 0; slot < 32; ++slot) {
        char nm[32];
        std::snprintf(nm, sizeof nm, "DASH2-%02d.DAT", slot);
        std::error_code ec;
        if (savesDir.empty() || !std::filesystem::is_regular_file(savesDir / nm, ec)) continue;
        bits |= 1u << slot;
        if (slot < 5) od::port::saveSlotTableFill(slot, od::readFile(savesDir / nm));
    }
    od::port::saveSlotBits().present = bits;
}

int PlayerView::writeSaveSlot(uint8_t slot, std::span<const uint8_t> img) {
    try {
        if (savesDir.empty()) throw std::runtime_error("no saves dir");
        char nm[32];
        std::snprintf(nm, sizeof nm, "DASH2-%02d.DAT", slot);
        const std::filesystem::path file = savesDir / nm;
        std::filesystem::create_directories(savesDir);
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(img.data()), std::streamsize(img.size()));
            if (!out) throw std::runtime_error("write failed");
        }
        SDL_Log("save menu: slot %u -> %s", slot, file.string().c_str());
        return 1;
    } catch (const std::exception& e) {
        SDL_Log("save menu: slot %u not written: %s", slot, e.what());
        return -1;
    }
}

void PlayerView::setProjectionConstants() {
    od::port::ProjectionState& projection = od::port::projection();
    projection.focal = 0x180;
    projection.nearDepth = 0xc0;
    projection.centreX = 160;
    projection.centreY = 120;
}

void PlayerView::beginPlay() {
    setProjectionConstants();
    scanSaveSlots();
    hud.init(); // exe 0x415f40
    play = true;
    camPrev = camCur = readCamera();
    trackPosition(true);
    orbitBehind();
}

void PlayerView::leavePlay() {
    if (play) {
        od::port::saveSerialise(od::port::kSaveBuf); // the state only: the memory card's title / icon are not state
        const auto img = od::port::saveImageAt(od::port::kSaveBuf).bytes();
        sessionSave.assign(img.begin(), img.end());
        SDL_Log("F2: session state kept (story %u, ST%02X sub %d)", unsigned(od::port::gameState().story), img[0x128],
                img[0x129]);
    }
    play = false;
}

bool PlayerView::startGame(const std::vector<uint8_t>* save, uint8_t difficulty) {
    playError.clear();
    play = false;
    sessionSave.clear(); // another game: the session's state is not this one's
    try {
        if (!ok) throw std::runtime_error("no player data");
        if (save && (save->size() != od::port::kSaveSize || od::port::saveChecksumBad(*save)))
            throw std::runtime_error("save invalido (tamanho ou checksum)");
        if (save) {
            od::port::saveImageAt(od::port::kSaveBuf).setBytes(*save);
            od::port::gameState().loadSave = 1;
        } else {
            od::port::GameState& game = od::port::gameState();
            game.loadSave = 0;
            game.entryMode = uint8_t(od::port::EntryMode::NewGame);
            game.stage = newGameStage;
            game.clearRoomToNextStory();
            game.room = newGameSub;
            game.difficulty = difficulty;
        }
    } catch (const std::exception& e) {
        playError = e.what();
        SDL_Log("start game failed: %s", e.what());
        return false;
    }
    od::port::gameState().frontEnd = od::port::kFrontEndGame; // a normal game: GAME.DAT's main-task mode 4 (0x5433f0)
                                                              // writes 3 (the field menus need it)
    return startGameFromState();
}

void PlayerView::gameModeInit() {
    // FUN_005e00e0(1): the player struct 0xa71d20..0xa7222f is cleared (the other pools: bindRoomData)
    od::port::resetPlayer();

    od::port::cameraReset(); // FUN_0040e170 (Maybe_GameModeInit, before FUN_00540d10)
    // the far / depth-range table of the main mode 0x543010 (before the game mode loop, not run by OpenDash):
    // 0xa72278 / 0xa72280 [shift] = 0xe00, 0x700, 0x380; the stage load (FUN_005f5df0) copies [shift] to the
    // far plane 0x6d0012 / the depth range 0x6d0010 the draw functions cull with (content/intro/crash_landing_draw)
    for (size_t k = 0; k < 3; ++k) {
        od::port::roomDraw().effectsFar[k] = uint16_t(0xe00u >> k);
        od::port::roomDraw().depthRanges[k] = uint16_t(0xe00u >> k);
    }
    od::port::gameModeInitState();
}

bool PlayerView::startGameFromState(int stage, int sub) {
    playError.clear();
    play = false;
    try {
        if (!ok) throw std::runtime_error("no player data");
        od::port::GameState& game = od::port::gameState();
        const bool save = game.loadSave != 0;
        gameModeInit();
        gameStarted = true;
        if (stage >= 0) { // a browsed room (F2 / --play): it, and no arrival request (placeInBrowsedRoom places)
            game.stage = uint8_t(stage);
            game.room = uint8_t(sub);
            od::port::transitionRequest().clearBytes();
        }
        spawnStage = -1; // a stage load: its stage setup (FUN_005d5a80's, the encounter slots) runs whatever ran before
        const int st = game.stage, sb = game.room;
        playerFiles(); // the stage load's FUN_005d5c40
        starting = true;
        if (loadRoom) loadRoom(st, sb);
        starting = false;
        if (!room || room->stage != st || room->sub != sb || !room->roomFile) {
            char msg[64];
            std::snprintf(msg, sizeof msg, "ST%02X sub %d nao carregou", st, sb);
            throw std::runtime_error(msg);
        }
        // a quiet game state (as bindRoom, but the request bytes stay: 0xa6d2e1 != 0 keeps the loaded position)
        od::port::messageInit();
        od::port::eventState().active = 0;
        lastPage = 0;
        od::port::scriptState().clearBlockBytes();
        // the title's Continue / New Game leaves its fade task (task 2: the load menu's wipe 0x26) running into the
        // game loop; the exe's stage load 0x5411c0 holds substate 4 until that task is over (its last phase restores
        // the display bytes 0x6d0009 / 0x6d000a / 0x6d000b and the draw environments), so the wipe runs on and the
        // stage setup waits for it (gameFrame, mode 1). Cutting the task here left 0x6d000a = 1 for good, and the
        // room scripts of the door / lift events never started. A start outside the title flow has no task running.
        const bool fadeRunning = fadeTask.active;
        if (!fadeRunning) {
            od::port::fadeState().covered = 0;
            od::port::fadeState().busy = 0;
            fadeTask = {};
        }
        setupHooks();
        game.setMode(od::port::GameMode::StageLoad); // mode 1 (stage load) -> stageLoaded: FUN_00542370 + stage bytes
        game.modeStep = 0;                           // -> mode 2 / 1
        if (!fadeRunning) {
            od::port::stageLoaded();
            bindRoomData(*room);
        }
        spawnFrom = save ? "save" : "new game";
        if (stage >= 0) placeInBrowsedRoom(*room);
        const od::port::Player& pl = od::port::player();
        SDL_Log("%s: ST%02X sub %d pos %d %d %d yaw %d HP %d/%d difficulty %d", spawnFrom.c_str(), st, sb,
                int(pl.pos.x.whole), int(pl.pos.y.whole), int(pl.pos.z.whole), int(pl.rotation.y),
                int(pl.hitPoints), int(pl.maxHitPoints), int(game.difficulty));

        beginPlay();
        snap = true;
    } catch (const std::exception& e) {
        playError = e.what();
        play = false;
        gameStarted = false;
        SDL_Log("start game failed: %s", e.what());
    }
    return play;
}

bool PlayerView::saveGame(const std::filesystem::path& file, uint8_t slot) {
    try {
        if (!play) throw std::runtime_error("nao esta jogando");
        od::port::saveBuildImage(slot);
        const auto img = od::port::saveImageAt(od::port::kSaveBuf).bytes();
        if (od::port::saveChecksumBad(img)) throw std::runtime_error("checksum");
        std::filesystem::create_directories(file.parent_path());
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(img.data()), std::streamsize(img.size()));
            if (!out) throw std::runtime_error("escrita falhou");
        }
        const auto back = od::readFile(file);
        if (back.size() != img.size() || std::memcmp(back.data(), img.data(), img.size()) != 0 ||
            od::port::saveChecksumBad(back))
            throw std::runtime_error("verificacao falhou");
        char msg[160];
        std::snprintf(msg, sizeof msg, "salvo: %s (ST%02X sub %d)", file.filename().string().c_str(), back[0x128],
                      back[0x129]);
        saveMessage = msg;
        SDL_Log("%s -> %s", msg, file.string().c_str());
        return true;
    } catch (const std::exception& e) {
        saveMessage = std::string("falha ao salvar: ") + e.what();
        SDL_Log("%s", saveMessage.c_str());
        return false;
    }
}

void PlayerView::gameFrame() {
    od::port::GameState& game = od::port::gameState();
    const uint8_t mode = game.mode;
    if (mode != 5) menuPrims.clear();
    shopPrims.clear();
    od::port::setShopDraw(&shopPrims);
    devPrims.clear();
    od::content::setDevRoomDraw(&devPrims);
    entryPrims.clear();
    od::port::setEntryListDraw(&entryPrims);
    quizPrims.clear();
    od::port::setQuizDraw(&quizPrims);
    minimapPrims.clear();
    od::port::setMinimapDraw(&minimapPrims);
    // Maybe_RequestPlayerFiles (the sub-screen's close, the development room's end): the equipment's body (helmet /
    // shoes) and special weapon files mapped like the stage load's, Megaman's model rebuilt with the room
    // (assetsDirty)
    od::port::setMenuPlayerFiles([this]() {
        const std::string body = g_playerBodyDat, weapon = g_playerWeaponDat;
        playerFiles();
        if (body != g_playerBodyDat || weapon != g_playerWeaponDat) assetsDirty = true;
    });
    if (mode != 5) inStatus = false;
    if (mode == 1) {
        // Maybe_GameModeStageTransition 0x5411c0's waits (the file loads themselves are OpenDash's loader, done at
        // once): substate 2 (sound fade-out + arrival placement; both done by stageLoaded / the room bind here) ->
        // 3 (flip of the type-1 buffers, file requests) -> 4 holds -- no field logic runs -- until the fade / wipe
        // task is over (0xa72269 == 0), then the stage setup -> mode 2 / 1. Without the wait a wipe started by the
        // exit (e.g. 0x22 at ST24's gate) ran on into field mode, whose sub-state 0 runs the frame (room scripts,
        // RNG) while it waits. Substate 4 also waits for the sound fades (Maybe_SoundFadesDone(0xf) 0x5d5900): the
        // oracle forces them done in mode 1 / 4, so this does not wait for them either.
        const uint8_t sub = game.modeStep;
        if (sub <= 2) {
            game.modeStep = 3;
        } else if (sub == 3) {
            game.modeStep = 4;
        } else if (od::port::fadeState().busy == 0) {
            od::port::stageLoaded(); // FUN_00542370 + stage bytes -> mode 2 / 1
            enterRoomOfState();
        }
    } else if (mode == 2 || mode == 3 || mode == 6 || mode == 7) { // 6 / 7: the field prompts (game/transition)
        od::port::gameTick(env, hooks, fadeTask);
    } else if (mode == 8 && gameOver) { // the player died: game/title gameOverModeFrame (main(): the title)
        gameOver();
    } else if (mode == 5) { // the status sub-screen (game/status_screen: Maybe_GameModeStatusMenu 0x541bf0)
        if (!menuVram.ready) menuVram.reset();
        if (!inStatus) menuVram.setFieldBase(g_roomVram); // the field's VRAM under the sub-screen's
        inStatus = true;
        g_menuVram = &menuVram;
        menuPrims.clear();
        od::port::statusModeFrame(&menuPrims);
    } else {
        od::port::notPortedAt("game modes", "game mode", mode);
    }
    od::port::fadeTick(fadeTask); // task 2
    od::port::displayMaskTick(); // 0x53f500: fade type 0 keeps the display off a few frames
}

void PlayerView::processDatQueue() {
    od::port::DatLoadQueue& queue = od::port::datLoadQueue();
    if (queue.requests[0].kind == 0) return;
    for (size_t q = 0; q < queue.requests.size() && queue.requests[q].kind != 0; ++q) {
        const std::string name = od::datFileName(queue.requests[q].fileId);
        try {
            od::GameFile f = od::dataSource().gameFile(name);
            bool vramSeg = false;
            uint32_t mp3 = 0;
            for (const od::FilePart& sg : f.parts) {
                const std::span<const uint8_t> d(sg.bytes);
                if (sg.kind == od::FilePart::Kind::Models) {
                    const uint32_t at = od::port::stage().bankBuffer ? kAltBankBuffers[0] : kAltBankBuffers[1];
                    if (!files.mapped(at, 0x18000)) files.zero(at, 0x18000);
                    files.map(at, d.first(std::min<size_t>(d.size(), 0x18000)));
                    if (od::port::gameState().gameMode() != od::port::GameMode::StatusMenu) { // SUBSCN03's models are
                        // the equipment page's (its weapon model, not drawn)
                        g_altBankDat[at == kAltBankBuffers[0] ? 0 : 1] = name;
                        assetsDirty = true;
                    }
                } else if (sg.kind == od::FilePart::Kind::PcMessages || sg.kind == od::FilePart::Kind::Ps1Messages) { // the fight / scene DAT's bank: through the
                    // language pack like the room's
                    bankRoomFile = std::make_shared<const od::GameFile>(f);
                    bankRoomName = name;
                    loadMessageBank();
                } else if (sg.kind == od::FilePart::Kind::Vram) {
                    vramSeg = true;
                } else if (sg.kind == od::FilePart::Kind::WavSfxBank) { // 0x40ed0e: Maybe_LoadSfxBank 0x5d5010(data, size, TOC arg, extra)
                    od::lang_sfx::load(pack.get(), name, d, sg.arg, sg.extra);
                } else if (sg.kind == od::FilePart::Kind::Mp3Music || sg.kind == od::FilePart::Kind::Ps1Music) {
                    od::port::loadMp3Slot(d, mp3++, sg.arg);
                }
            }
            if (vramSeg && od::port::gameState().gameMode() == od::port::GameMode::StatusMenu) { // a page DAT of the
                // sub-screen (FUN_005e7d30): its VRAM
                if (!menuVram.ready) menuVram.reset();
                menuVram.applyDat(name, std::move(f));
            } else if (vramSeg && std::find(g_stageDats.begin(), g_stageDats.end(), name) == g_stageDats.end()) {
                g_stageDats.push_back(name);
                assetsDirty = true;
            }
            SDL_Log("dat load: %s (queued by the script)", name.c_str());
        } catch (const std::exception& e) {
            SDL_Log("dat load %s failed: %s", name.c_str(), e.what());
        }
    }
    queue.clear();
    od::port::fileLoads().loading = 0;
}

void PlayerView::playTick(uint16_t ps1Buttons, uint8_t stickX, uint8_t stickY, float lx, float ly) {
    if (!play) return;
    ++logicTicks;
    logicNs = SDL_GetTicksNS();
    camPrev = camCur;
    try {
        // the d-pad goes to the game as is while an event or a script runs (the shop's menus run in windows 1..4
        // under the script block) or a window shows a choice (+0 & 0x20, the hand): an open window alone is not
        // a dialogue (the ruins keep an info window 4 up, flags 0x2e083, and the player strafed with a fixed yaw)
        od::port::GameState& game = od::port::gameState();
        bool talking =
            od::port::eventState().active != 0 || (od::port::scriptState().flags & od::port::kScriptRunning) != 0;
        for (uint32_t w = 0; w < 5 && !talking; ++w) talking = (od::port::messageWindow(w).flags & 0xa0) == 0xa0;
        const bool menu = game.gameMode() == od::port::GameMode::StatusMenu; // the status sub-screen reads the d-pad
                                                                             // as is (game/status_screen)
        if (modern && (talking || menu)) { // message windows read the d-pad as is (choice cursor)
            if (!menu || (ps1Buttons & 0xf0u) == 0) ps1Buttons &= uint16_t(~0xf0u);
            if (lx > 0.5f) ps1Buttons |= 0x20;
            if (lx < -0.5f) ps1Buttons |= 0x80;
            if (ly < -0.5f) ps1Buttons |= 0x10;
            if (ly > 0.5f) ps1Buttons |= 0x40;
            stickX = stickY = 0x80;
        } else if (modern) { // the game never sees the stick: it would turn the body like a tank
            steerStickX = steerStickY = 0x80;
            ps1Buttons = steer(ps1Buttons, lx, ly);
            stickX = steerStickX; // 0x80 except on the jet skates (steer: the stick as the game's analog pad)
            stickY = steerStickY;
        }
        // Start opens the status sub-screen: the game opens it on pad bit 0x1 (select) in field play
        // (FUN_005413a0); OpenDash also sends 0x1 on the frame Start goes down there (only that frame: start +
        // select held is the soft reset, FUN_005410b0)
        if ((ps1Buttons & 0x8) && !startWasDown && game.gameMode() == od::port::GameMode::Field) ps1Buttons |= 0x1;
        startWasDown = (ps1Buttons & 0x8) != 0;
        od::port::padUpdate(od::port::pad0(), ps1Buttons, stickX, stickY);
        if (afterPad) afterPad(); // the game loop's FUN_00542530 (the demo's pad replay, game/title)
        od::dc::resetCursor(); // 0x54a489: the draw-command cursor back to this frame's buffer
        od::port::padVibrationTick(); // 0x54a4cd: the pad vibration's tick (FUN_0055cc40)
        ++game.frame; // 0x54a4dc: the frame counter (u16), before the mode's frame
        gameFrame();
        if (afterFrame) afterFrame(); // FUN_005410b0 (the soft reset, game/title)
        od::port::messageTick(); // FUN_005578a0 (WinMain, after the game task): window logic + layout
        dcPrims.clear(); // the frame's draw commands (Maybe_ProcessDrawCommands 0x5f0580), before the buffer flip
        dcStats = od::dc::decode(g_fxTextures, dcPrims);
        processDatQueue();
        logMessages();
        audioTick();
        // the double-buffer index the render step flips every frame: the field mode's sub-state 3 / the room
        // load wait for it, the cutscene skip reads it
        od::port::display().drawBuffer ^= 1u;
        // the field frame body / draw pass: not in game mode 5 (Maybe_GameModeStatusMenu 0x541bf0 runs only the
        // sub-screen, Maybe_ProcessDrawCommands and the play time); the field picture stays the last frame's. Nor in
        // mode 1 (the stage's files loading): the pools still hold the last stage's actors, the class tables are
        // already the next stage's (a game over's way to the title backdrop, ST51)
        if (game.gameMode() != od::port::GameMode::StatusMenu && game.gameMode() != od::port::GameMode::StageLoad) {
            hud.tick(); // FUN_00415fe0 (presentation: own state, reads the game's)
            hud.tickEnemyGauges(); // its type-3 elements (boss gauges) live in the game state
            overlayPrims = od::hud::overlayDrawPass(); // FUN_005f2690 (draw pass): the scripted sprite banner
            for (const od::hud::Prim& q : od::port::escapeTimerDraw()) overlayPrims.push_back(q); // 0x4270a0's packets
            for (const od::hud::Prim& q : od::port::alarmFlashDraw()) overlayPrims.push_back(q); // 0x428de0's packet
            for (const od::hud::Prim& q : od::port::examTimerDraw()) overlayPrims.push_back(q); // 0x422d60's packets
            for (const od::hud::Prim& q : od::port::ninoGaugeDraw()) overlayPrims.push_back(q); // 0x41ecd0's packets
            for (const od::hud::Prim& q : od::port::st59HudDraw()) overlayPrims.push_back(q); // 0x427b90's packets
            for (const od::hud::Prim& q : od::hud::scriptWhiteDraw()) overlayPrims.push_back(q); // FUN_005f30e0
            fxPrims.clear();
            od::fx::collect(fx, g_fxTextures, fxPrims);
            // the script-6 storm (content/intro/crash_landing_draw): 2D backdrop / panorama, cloud puffs (into fxPrims), flash rings
            od::storm::collect(g_fxTextures, int(od::fx::gradients().size()), storm, fxPrims);
            // the weather overlays (game/weather_draw): the blizzard layer of 0x431af0, the towns' snow sprites;
            // continued left / right of the 320-pixel frame for wider windows
            od::weather::collect(weather, std::max(0, int(std::ceil((240.0f * viewAspect - 320.0f) * 0.5f))));
            for (const od::weather::Quad& q : weather) od::weather::texture(g_fxTextures, q);
            if (weather.size() != weatherLast) {
                SDL_Log("weather: %zu quads (first: source %06x OT slot %u) ST%02X/%d", weather.size(),
                        weather.empty() ? 0u : od::routineStateValue(weather[0].source),
                        weather.empty() ? 0u : unsigned(weather[0].ot),
                        unsigned(game.stage), int(game.room));
                weatherLast = weather.size();
            }
            fxPrims.insert(fxPrims.end(), dcPrims.begin(), dcPrims.end());
        }
        if (dcStats.records != dcLast.records || dcStats.f8 != dcLast.f8 || dcStats.f4 != dcLast.f4 ||
            dcStats.unknown != dcLast.unknown)
            SDL_Log("dccheck: %d draw commands (0x2c %d, 0xf0 %d, 0xf4 %d not drawn, 0xf8 %d, %d culled quads, "
                    "%d unknown) -> %zu prims", dcStats.records, dcStats.b2c, dcStats.f0, dcStats.f4, dcStats.f8,
                    dcStats.culled, dcStats.unknown, dcPrims.size());
        dcLast = dcStats;
        if (g_fxTextures.takeDirty()) { // an effect sampled a texture the room has no slice for: rebuild them
            SDL_Log("fxtex: %zu effect textures, rebuilding the room's slices", g_fxTextures.entries().size());
            assetsDirty = true;
        }
        if (game.gameMode() != od::port::GameMode::StageLoad) logEffects(); // (the last stage's actors: see above)
        { // scripted-run evidence: the storm's element counts whenever they change
            ++stormLogTick;
            size_t n = 0;
            for (const od::fx::Prim& q : fxPrims) n += (q.object & 0xff000u) >= 0x70000u && (q.object & 0xff000u) <= 0x76000u;
            size_t depth = 0, slot = 0;
            for (const od::storm::DepthChain& d : storm.depth) {
                depth += d.sprites.size();
                slot = size_t(uint16_t(d.slot));
            }
            const std::array<size_t, 5> k{storm.back.size(), depth, slot, storm.front.size(), n};
            if (k != stormLast)
                SDL_Log("stormcheck tick %u: back sprites %zu, depth sprites %zu (OT slot %zu), front triangles %zu, 3D "
                        "prims %zu",
                        stormLogTick, k[0], k[1], k[2], k[3], k[4]);
            stormLast = k;
        }
        camCur = readCamera();
        if (warpLogTicks > 0) {
            --warpLogTicks;
            warpActorLog("after warp");
        }
        if (std::getenv("OD_TRAVELLOG")) { // dev: the aim and the live shots / effects every tick
            auto live = [&](uint32_t base, uint32_t n, uint32_t stride) {
                int c = 0;
                for (uint32_t i = 0; i < n; ++i) c += od::port::actorHeaderAt(base + i * stride).flags & od::port::kActorLive;
                return c;
            };
            const od::port::Player& pl = od::port::player();
            SDL_Log("aim tick %u: state %u/%u/%u yaw %d target %08x orbit %d held %04x shots10 %d shots40 %d fxA0 %d e0 %d "
                    "aimpitch %d look %d campitch %d orbitpitch %d",
                    logicTicks, unsigned(pl.state), unsigned(pl.subState), unsigned(pl.step),
                    int(pl.rotation.y) & 0xfff, unsigned(od::port::slotIdentityWord(pl.lockedActor)),
                    int(std::lround(orbitYaw * 4096.0f / 6.2831853f)) & 0xfff, ps1Buttons,
                    live(od::port::kShotPool, 16, 0x100), live(od::port::kProjectilePool, 32, 0xdc),
                    live(od::port::kEffectPool, 64, 0x28), live(od::port::kControllerPool, 32, 0x20), int(pl.rotation.x),
                    int(od::port::cameraState().lookPitch), int(od::port::cameraState().pose.pitch),
                    int(std::lround(orbitPitch * 4096.0f / 6.2831853f)));
        }
        {
            // a cut inside the room (a script starting / ending or skipped, the player placed by a script, a new
            // camera mode): no blend with the previous tick and no AA history, or the old pose / old view lingers
            // for a few frames (the room bind already snaps)
            float d2 = 0.0f;
            const od::port::Player& pl = od::port::player();
            const int32_t at[3] = {pl.pos.x, pl.pos.y, pl.pos.z};
            for (int i = 0; i < 3; ++i) {
                const float d = float(at[i]) / 65536.0f - posCur[i];
                d2 += d * d;
            }
            const uint8_t script = od::port::scriptState().flags & od::port::kScriptRunning,
                          camMode = uint8_t(od::port::cameraState().mode());
            const bool cut = d2 > 512.0f * 512.0f || script != cutScript || camMode != cutCamMode;
            cutScript = script;
            cutCamMode = camMode;
            if (cut) {
                snap = true;
                camPrev = camCur;
            }
            trackPosition(cut);
        }
        orbitArrival();
        const bool fieldPlay =
            game.gameMode() == od::port::GameMode::Field && game.fieldStep() == od::port::FieldStep::Playing;
        if (logLock && (!fieldPlay || camLogTicks > 0)) { // scripted: camera evidence
            const od::port::CameraState& camera = od::port::cameraState();
            const od::port::Player& pl = od::port::player();
            SDL_Log("camcheck: ST%02X/%d gm %u/%u cam m%u lazy%u sub%d h%d d%d yaw%d pit%d base %d %d %d %d %d eye %d %d %d "
                    "| pl %d %d %d yaw %d | orbit %d pitch %.2f %s", unsigned(game.stage), int(game.room),
                    unsigned(game.mode), unsigned(game.modeStep), unsigned(camera.mode()), unsigned(camera.follow()),
                    int(int8_t(camera.followStyle())), int(camera.pose.height), int(camera.pose.distance),
                    int(camera.pose.yaw), int(camera.pose.pitch), int(camera.basePose.height),
                    int(camera.basePose.distance), int(camera.basePose.yaw), int(camera.basePose.pitch),
                    int(camera.basePose.roll), int(camera.eye.x), int(camera.eye.y), int(camera.eye.z),
                    int(pl.pos.x.whole), int(pl.pos.y.whole), int(pl.pos.z.whole), int(pl.rotation.y),
                    int(std::lround(orbitYaw * 4096.0f / 6.2831853f)), orbitPitch, gameCameraActive() ? "game" : "orbit");
            camLogTicks = !fieldPlay ? 40 : camLogTicks - 1;
        }
        if (logLock && lockedOn() && (++lockTicks % 5) == 0) { // scripted runs: the strafe evidence
            const od::port::Player& pl = od::port::player();
            const uint32_t t = od::port::slotIdentityWord(pl.lockedActor);
            const od::port::Actor& target = od::port::actorAt(t);
            const int dx = target.pos.x.whole - pl.pos.x.whole, dz = target.pos.z.whole - pl.pos.z.whole;
            SDL_Log("lockcheck: player %d %d %d yaw %d sub %d | target %06x %d %d %d dist %d | pad %04x",
                    int(pl.pos.x.whole), int(pl.pos.y.whole), int(pl.pos.z.whole), int(pl.rotation.y),
                    int(pl.subState), t, int(target.pos.x.whole), int(target.pos.y.whole), int(target.pos.z.whole),
                    int(std::lround(std::hypot(dx, dz))), ps1Buttons);
        }
    } catch (const std::exception& e) {
        playError = e.what();
        play = false;
        SDL_Log("play mode stopped: %s", e.what());
    }
}

void PlayerView::logTownNpcs() const {
    // the sub-screen's class table is in 0xa6d300 (game/status_screen statusScreenOpen)
    if (od::port::gameState().gameMode() == od::port::GameMode::StatusMenu) return;
    int n = 0;
    for (uint32_t i = 0; i < 24; ++i) {
        const uint32_t a = od::port::kAnimatedPool + i * 0x16c;
        const od::port::Actor& npc = od::port::actorAt(a);
        if (!(npc.flags & od::port::kActorLive) ||
            !od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::Animated).is(od::RoutineId::TownNpcUpdate))
            continue;
        ++n;
        SDL_Log("townnpc slot %u type %u.%u pos %d %d %d yaw %d state %u.%u", i, unsigned(npc.type),
                unsigned(npc.subtype), int(npc.pos.x.whole), int(npc.pos.y.whole), int(npc.pos.z.whole),
                int(npc.rotation.y), unsigned(npc.state), unsigned(npc.subState));
    }
    SDL_Log("townnpc: %d walker actor(s) live, story %u", n, unsigned(od::port::gameState().story));

}

} // namespace od::app
