#include "game/player/player_input.hpp"
#include "app/app.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

#include "engine/lang.hpp"
#include "game/game_state.hpp"
#include "game/story_flags.hpp"
#include "game/player/precise_pose.hpp"
#include "render/render_logic.hpp"
namespace od::app {

void App::drawDebugMenu() {
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330, 470), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("OpenDash  (F1)", &showMenu) && ImGui::BeginTabBar("f1tabs")) {
        if (ImGui::BeginTabItem("Cenarios")) {
            ImGui::Text("Atual: ST%02X  sub %d", stage, sub);
            ImGui::Text("banco: %zu modelos", current.bankIndex.size());
            if (!current.sky.empty()) ImGui::Text("ceu: %zu camadas", current.sky.size());
            if (!loadError.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", loadError.c_str());
            ImGui::TextDisabled("PgUp/PgDn estagio   [ ] sub-sala");
            ImGui::Separator();
            const bool locked = netplay.isClient() && netplay.world().active(); // the host's room (app/netplay_world)
            if (locked) ImGui::TextDisabled("só o host muda o cenário");
            ImGui::BeginDisabled(locked);
            ImGui::BeginChild("stages", ImVec2(120, 0), ImGuiChildFlags_Borders);
            for (const auto& si : stages) {
                char label[32];
                std::snprintf(label, sizeof label, "ST%02X  (%d)", si.stage, si.subs);
                if (ImGui::Selectable(label, si.stage == browseStage)) browseStage = si.stage;
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) reload(si.stage, 0);
            }
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("subs", ImVec2(0, 0));
            ImGui::Text("ST%02X", browseStage);
            for (int i = 0; i < subCount(browseStage); ++i) {
                char label[32];
                std::snprintf(label, sizeof label, "sub-sala %d", i);
                const bool active = browseStage == stage && i == sub;
                if (ImGui::Selectable(label, active)) reload(browseStage, i);
            }
            ImGui::Separator();
            ImGui::TextDisabled("cam %.0f %.0f %.0f", cam.eye[0], cam.eye[1], cam.eye[2]);
            ImGui::EndChild();
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Jogo")) {
            if (playerView.ok) { // new game / load / save (game/game_init, game/save_image)
                ImGui::SeparatorText("Jogo");
                // dev cheat: the owned flags (0xa73cc0) the menus list. Equipment lists FUN_005ec180(kind):
                // flag = [0x91b3c4 + kind * 2] + 0x380 + i, i from 1 (special weapons 3) below [0x91b3b8 + kind
                // * 2]: special weapons 0x383..0x391, buster parts 0x395..0x3b3, helmets 0x3b9..0x3ba, armours
                // 0x3bd..0x3c3, shoes 0x3c9..0x3cd; items 0x400..0x451 (kind 0 of the items page), the third
                // buster slot 0x3d9, and the zenny 0xa6d1a8 at its maximum (Maybe_AddZenny 0x5e51a0). Other key
                // items (0x3d0..0x3ff) stay off: the story reads them.
                if (ImGui::Button("cheat: todas as armas e itens")) {
                    auto own = [&](uint32_t from, uint32_t to) {
                        for (uint32_t n = from; n <= to; ++n)
                            od::port::setStoryFlag(n);
                    };
                    own(0x383, 0x391); // special weapons
                    own(0x395, 0x3b3); // buster parts
                    own(0x3b9, 0x3ba); // helmets
                    own(0x3bd, 0x3c3); // armours
                    own(0x3c9, 0x3cd); // shoes (jet skates ...)
                    own(0x400, 0x451); // items
                    own(0x3d9, 0x3d9); // third buster slot
                    od::port::gameState().zenny = 9999999;
                    SDL_Log("cheat: special weapons, buster parts, helmets, armours, shoes, items, slot 3, zenny");
                }
                // the title menu offers difficulties 1..1 + min(0xa6d19c, 3) only when that record is set
                // (0x4292cb / 0x4295b0); else a new game is difficulty 1 (0x4292e5)
                uint16_t records = 0;
                for (const SaveEntry& e : saveList)
                    if (e.valid) records = std::max(records, e.records);
                static int newDifficulty = 1;
                if (records != 0) {
                    const int top = 1 + std::min<int>(records, 3);
                    ImGui::SetNextItemWidth(120);
                    ImGui::SliderInt("dificuldade", &newDifficulty, 1, top);
                    newDifficulty = std::clamp(newDifficulty, 1, top);
                } else {
                    newDifficulty = 1;
                }
                if (ImGui::Button("Novo jogo")) {
                    leaveTitle();
                    playerView.startGame(nullptr, uint8_t(newDifficulty));
                }
                if (records == 0) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("dificuldade 1 (o jogo so oferece escolha apos zerar)");
                }
                if (ImGui::TreeNode("Carregar save")) {
                    for (const SaveEntry& e : saveList) {
                        ImGui::BeginDisabled(!e.valid);
                        if (ImGui::Selectable(e.label.c_str())) {
                            leaveTitle();
                            loadSave(e.path);
                        }
                        ImGui::EndDisabled();
                    }
                    if (saveList.empty()) ImGui::TextDisabled("nenhum save encontrado");
                    if (ImGui::SmallButton("atualizar lista")) refreshSaves();
                    ImGui::TreePop();
                }
                ImGui::SetNextItemWidth(220);
                if (ImGui::BeginCombo("historia do jogo livre", storySaveLabel.c_str())) {
                    if (ImGui::Selectable("nenhum (historia 0)", storySavePath.empty())) setStorySave("nenhum");
                    for (const SaveEntry& e : saveList) {
                        if (!e.valid) continue;
                        if (ImGui::Selectable(e.label.c_str(), e.path == storySavePath)) setStorySave(e.path.string());
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("F2 / jogar numa sala do navegador usa a historia e as flags deste save "
                                      "(so leitura; opendash.ini story_save=). Vale na proxima entrada em F2.");
                static int saveSlot = 0;
                ImGui::BeginDisabled(!playerView.play);
                ImGui::SetNextItemWidth(80);
                ImGui::Combo("##slot", &saveSlot, "slot 0\0slot 1\0slot 2\0");
                ImGui::SameLine();
                if (ImGui::Button("Salvar")) {
                    char nm[24];
                    std::snprintf(nm, sizeof nm, "DASH2-%02d.DAT", saveSlot);
                    playerView.saveGame(savesDir / nm, uint8_t(saveSlot)); // never the game's directory
                    refreshSaves();
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("-> saves/ do OpenDash");
                if (!playerView.saveMessage.empty()) ImGui::TextWrapped("%s", playerView.saveMessage.c_str());
            }
            if (playerView.ok && !playerView.frames.empty()) {
                ImGui::SeparatorText("Megaman");
                ImGui::SliderInt("animacao", &playerView.anim, 0, int(playerView.frames.size()) - 1);
                playerView.anim = std::clamp(playerView.anim, 0, int(playerView.frames.size()) - 1);
                const int nf = std::max(playerView.frames[size_t(playerView.anim)], 1);
                playerView.frame %= nf;
                ImGui::SliderInt("frame", &playerView.frame, 0, nf - 1);
                ImGui::Checkbox("tocar", &playerView.playing);
                if (bool on = playerView.play; ImGui::Checkbox("jogar (F2)", &on)) { // as the F2 key
                    if (!on) {
                        playerView.leavePlay();
                    } else {
                        leaveTitle();
                        playerView.enterPlay(current);
                    }
                }
                ImGui::Checkbox("controles classicos (camera do jogo, tanque)", &classicControls);
                if (!classicControls && ImGui::TreeNode("jet skates (camera)")) {
                    auto& st = playerView.skateTune;
                    ImGui::SliderFloat("atraso da camera (s)", &st.chaseSmooth, 0.05f, 1.0f);
                    ImGui::SliderFloat("espera apos mouse (s)", &st.manualHold, 0.0f, 3.0f);
                    ImGui::SliderFloat("inclinacao (graus, - = contra)", &st.rollDeg, -15.0f, 15.0f);
                    ImGui::SliderFloat("FOV extra (graus)", &st.fovDeg, 0.0f, 20.0f);
                    ImGui::TreePop();
                }
                ImGui::TextDisabled("teclado: WASD/setas mover, E (ou K) usar/falar, espaco pular,");
                ImGui::TextDisabled("J atirar/chutar, I triangulo, Q L1, R R1, Z L2 olhar, C R2,");
                ImGui::TextDisabled("Enter start, Backspace select. Mouse: mover = camera,");
                ImGui::TextDisabled("botao esquerdo = atirar/chutar, botao direito = mirar/travar (R2).");
                ImGui::TextDisabled("Esc / F1 soltam o mouse.");
                playerView.modern = !classicControls;
                if (!playerView.playError.empty())
                    ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", playerView.playError.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%d frames", nf);
            }
            if (!playerView.ok) ImGui::TextDisabled("jogo indisponivel");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Video")) {
            {
                const od::AaBackend active = renderer->activeBackend();
                if (ImGui::BeginCombo("anti-aliasing", od::aaBackendName(active))) {
                    // probed when the combo is first opened (the probe initialises every backend: NGX ...)
                    static std::vector<od::AaBackend> avail = renderer->availableBackends();
                    for (od::AaBackend b : avail)
                        if (ImGui::Selectable(od::aaBackendName(b), b == active)) {
                            aaBackend = b;
                            renderer->setAntiAliasing(aaBackend, aaQuality);
                        }
                    ImGui::EndCombo();
                }
                static const char* kQualities[] = {"native", "quality", "balanced", "performance"};
                int q = int(aaQuality);
                if (ImGui::Combo("qualidade AA", &q, kQualities, 4)) {
                    aaQuality = od::AaQuality(q);
                    renderer->setAntiAliasing(aaBackend, aaQuality);
                }
            }
            {
                static const char* kViews[] = {"final", "cor", "movimento", "normais", "profundidade", "reativo"};
                if (ImGui::Combo("debug view", &debugView, kViews, 6))
                    renderer->setDebugView(od::DebugView(debugView));
            }
            if (renderer->rayTracingSupported()) { // experiment: ray-traced sun shadows
                ImGui::SeparatorText("Sol (ray tracing)");
                bool changed = ImGui::Checkbox("sombras do sol", &sunOn);
                changed |= ImGui::SliderFloat("direcao", &sunAzimuth, 0.0f, 360.0f, "%.0f graus");
                changed |= ImGui::SliderFloat("altura", &sunElevation, 5.0f, 90.0f, "%.0f graus");
                changed |= ImGui::SliderFloat("forca", &sunStrength, 0.0f, 1.0f, "%.2f");
                if (changed) applySun();
            }
            else
                ImGui::TextDisabled("Sol: GPU sem ray tracing");
            if (!current.sky.empty()) ImGui::Checkbox("ceu 3D (desmarcado = 2D original)", &skyDome);
            {
                bool precisePoses = od::precise::enabled();
                if (ImGui::Checkbox("animacoes suaves (desmarcado = tremido original do PS1)", &precisePoses))
                    od::precise::setEnabled(precisePoses);
            }
            ImGui::Checkbox("texto HD (desmarcado = fonte original do PS1)", &messageUi.hdText);
            ImGui::Checkbox("paineis HD (desmarcado = pixel art original)", &hdArtArg);
            {
                bool snappy = od::port::menuSnappy();
                if (ImGui::Checkbox("menu responsivo (desmarcado = cursor original do PS1)", &snappy))
                    od::port::setMenuSnappy(snappy);
            }
            {
                ImGui::SeparatorText("Neblina");
                static const char* kFogModes[] = {"desligada", "original (PS1)", "remaster"};
                int fm = int(fogMode);
                bool changed = ImGui::Combo("neblina", &fm, kFogModes, 3);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("original: a do PS1, opaca no fim da distancia de desenho dele.\n"
                                      "remaster: neblina suave por distancia e altura; pesada so com a nevasca "
                                      "de Yosionke.");
                ImGui::BeginDisabled(fm != int(od::FogMode::Remaster));
                changed |= ImGui::SliderFloat("distancia", &fogDistance, 0.25f, 4.0f, "%.2fx");
                ImGui::EndDisabled();
                if (changed) {
                    fogMode = od::FogMode(fm);
                    renderer->setFog(fogFor(current, fogMode, fogDistance));
                }
                if (ImGui::IsItemDeactivatedAfterEdit() || (changed && !ImGui::IsItemActive())) {
                    od::saveConfigValue("fog", fogMode == od::FogMode::Off       ? "off"
                                               : fogMode == od::FogMode::Classic ? "classic"
                                                                                 : "remaster");
                    char d[16];
                    std::snprintf(d, sizeof d, "%.2f", fogDistance);
                    od::saveConfigValue("fog_distance", d);
                }
                if (current.fogEnd <= current.fogStart) ImGui::TextDisabled("esta sala nao tem neblina");
            }
            {
                ImGui::SeparatorText("Agua");
                static const char* kWaterModes[] = {"original (PS1)", "remaster"};
                int wm = waterRemaster ? 1 : 0;
                if (ImGui::Combo("embaixo d'agua", &wm, kWaterModes, 2)) {
                    waterRemaster = wm == 1;
                    od::saveConfigValue("water", waterRemaster ? "remaster" : "classic");
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("original: a ondulacao de linhas da tela do PS1 / PSP.\n"
                                      "remaster: ondulacao suave, cor mais funda embaixo, causticas e raios de luz.");
            }
            ImGui::EndTabItem();
        }
        if (playerView.ok && ImGui::BeginTabItem("Som e idioma")) {
            if (playerView.mixer) {
                ImGui::SeparatorText("Som");
                ImGui::Checkbox("mudo", &playerView.audioMute);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(160);
                ImGui::SliderFloat("volume", &playerView.audioVolume, 0.0f, 1.0f, "%.2f");
                ImGui::SameLine();
                ImGui::Checkbox("log", &playerView.audioLog);
                if (!playerView.mixer->hasDevice()) ImGui::TextDisabled("sem dispositivo de audio (silencioso)");
            }
            {
                ImGui::SeparatorText("Idioma");
                int cur = int(playerView.language);
                ImGui::SetNextItemWidth(220);
                if (ImGui::BeginCombo("texto e vozes", od::lang::displayName(playerView.language))) {
                    for (int k = 0; k < 3; ++k) {
                        const auto l = od::lang::Language(k);
                        const bool have = playerView.packOffered(l);
                        if (!have) ImGui::BeginDisabled();
                        if (ImGui::Selectable(od::lang::displayName(l), cur == k)) {
                            playerView.setLanguage(l);
                            od::saveConfigValue("lang", od::lang::code(playerView.language));
                        }
                        if (!have) ImGui::EndDisabled();
                    }
                    ImGui::EndCombo();
                }
                if (playerView.restoredAvailable() &&
                    ImGui::Checkbox("vozes restauradas (IA)", &playerView.restoredVoices))
                    playerView.applyVoiceDirs();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Netplay")) { // app/netplay: the session, the players, why each ghost is (not) shown
            const od::net::SessionStatus st = netplay.status(SDL_GetTicksNS());
            if (netplay.sessionActive()) {
                ImGui::Text("papel: %s  (%s)", st.role == od::net::Role::Host ? "host" : "cliente",
                            od::net::connectionName(st.connection));
                ImGui::Text("porta UDP: %u   meu id: %u", unsigned(st.port), unsigned(st.localId));
            } else {
                ImGui::TextDisabled("netplay desligado (net= / --host / --join)");
            }
            if (!netplay.error().empty())
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "erro: %s", netplay.error().c_str());
            if (!st.text.empty()) ImGui::TextWrapped("%s", st.text.c_str());
            if (!st.players.empty() && ImGui::BeginTable("netpeers", 6, ImGuiTableFlags_Borders)) {
                ImGui::TableSetupColumn("id");
                ImGui::TableSetupColumn("nome");
                ImGui::TableSetupColumn("RTT");
                ImGui::TableSetupColumn("perda");
                ImGui::TableSetupColumn("ouvido");
                ImGui::TableSetupColumn("slot");
                ImGui::TableHeadersRow();
                for (const od::net::PlayerStatus& p : st.players) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", unsigned(p.playerId));
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", p.name.c_str());
                    if (!p.address.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.address.c_str());
                    ImGui::TableNextColumn();
                    if (p.rttMs >= 0) ImGui::Text("%d ms", p.rttMs);
                    else ImGui::TextDisabled(p.address.empty() ? "via host" : "-");
                    ImGui::TableNextColumn();
                    if (p.lossPercent >= 0) ImGui::Text("%.1f %%", p.lossPercent);
                    else ImGui::TextDisabled("-");
                    ImGui::TableNextColumn();
                    if (p.lastHeardMs >= 0) ImGui::Text("%lld ms", (long long)p.lastHeardMs);
                    else ImGui::TextDisabled("-");
                    ImGui::TableNextColumn();
                    if (const int slot = netplay.slotOf(p.playerId); slot >= 0) ImGui::Text("%d", slot);
                    else ImGui::TextDisabled("nenhum");
                }
                ImGui::EndTable();
            }
            ImGui::SeparatorText("Combate"); // the host's (app/netplay_world): a client shows them
            {
                const bool client = netplay.isClient() && netplay.world().active();
                NetCombatSettings c = netplay.world().combat();
                ImGui::BeginDisabled(client);
                const bool ff = ImGui::Checkbox("fogo amigo", &c.friendlyFire);
                const bool scale = ImGui::SliderInt("dano entre jogadores %", &c.scale, 25, 100);
                ImGui::EndDisabled();
                if (client) ImGui::TextDisabled("definido pelo host");
                if (!client && (ff || scale)) {
                    netplay.world().setCombat(c);
                    if (ff) od::saveConfigValue("net_friendly_fire", c.friendlyFire ? "1" : "0");
                    if (scale) od::saveConfigValue("net_ff_scale", std::to_string(c.scale));
                }
            }
            if (netplay.world().active()) {
                ImGui::SeparatorText("Mundo");
                const NetWorld::Stats ws = netplay.world().stats();
                ImGui::Text("%s: %d atores  golpes %d enviados, %d aplicados, %d descartados",
                            netplay.world().host() ? "host" : "cliente", ws.puppets, ws.claimsSent, ws.claimsApplied,
                            ws.claimsDropped);
                if (netplay.world().holdTicks())
                    ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "aguardando %s", netplay.world().waitingFor().c_str());
            } else if (netplay.hostGone()) {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "host saiu");
            }
            ImGui::SeparatorText("fantasmas");
            for (int slot = 0; slot < od::ghost::kMaxGhosts; ++slot) {
                const std::string source = netplay.slotSource(slot);
                if (source.empty()) continue;
                ImGui::Text("slot %d  %s: %s", slot, source.c_str(), ghosts.status(slot).c_str());
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void App::drawNetOverlay() {
    if (!netplay.world().holdTicks()) return;
    char text[256];
    std::snprintf(text, sizeof text, "aguardando %s", netplay.world().waitingFor().c_str());
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 view = ImGui::GetIO().DisplaySize;
    ImGui::GetForegroundDrawList()->AddText(ImVec2((view.x - size.x) * 0.5f, 24.0f), IM_COL32(255, 220, 120, 255),
                                            text);
}

} // namespace od::app
