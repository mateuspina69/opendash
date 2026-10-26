#include "app/room_mesh.hpp"

#include <cstdlib>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <span>
#include <stdexcept>

#include "app/dat_cache.hpp"
#include "app/sky.hpp"
#include "engine/dat.hpp"
#include "engine/data_source.hpp"
#include "game/camera.hpp"
#include "game/collision.hpp"
#include "game/player/player_model.hpp"
#include "game/stage_setup.hpp"
#include "content/intro/crash_landing_draw.hpp"
#include "game/door_leaves.hpp"
#include "game/room_exits.hpp"
#include "game/room_params.hpp"
#include "game/stage_state.hpp"
#include "game/stage_files.hpp"

namespace od::app {

std::string g_playerBodyDat = "PL00P000.DAT", g_playerWeaponDat;
od::fx::TextureCache g_fxTextures;
std::vector<std::string> g_stageDats;
std::array<std::string, 2> g_altBankDat;
int g_stageFilesStage = -1;
std::string g_stageFilesGeo, g_stageFilesTex;
bool g_inStageSwitch = false;
od::Vram g_roomVram;
std::vector<KeptVramMove> g_keptVramMoves;
uint32_t g_roomVramGen = 0;
int g_stageDatsStage = -1;
od::port::LoadedDataIdentity loadedDataIdentity() { return {g_stageFilesGeo, g_stageFilesTex, g_altBankDat}; }

namespace {

// the room / stage tables of the room build (engine/data_source)

// Decode model-space geometry from the stage bank. Placement and poses come only
// from the actors spawned and updated by game/stage_setup and the actor pools.
template <class SliceFn>
void loadBankGeometry(RoomMesh& mesh, const od::GameFile& st, int shift, SliceFn&& slice) {
    const auto bank = od::modelBank(st);
    const auto entries = od::bankEntries(bank);
    // one face list -> triangles in world space (game Y-down -> port Y-up), PS1 quad split
    float partLayer = 0.0f; // actor flags & 0x30 == 0x20: OT depth 1 = drawn over everything
    std::vector<od::RenderVertex>* target = &mesh.tris;
    PartSource* targetSrc = nullptr; // set with an actor part's target: the source vertex of every emitted vertex
    bool toPortAxes = true; // false: keep game axes (animated part geometry, posed per tick)
    auto emitPart = [&](const od::BankModel& model, const od::ModelPart& part, auto&& toWorld) {
        for (const od::ModelFace& f : part.faces) {
            const uint32_t t = f.material < model.textures.size() ? model.textures[f.material] : 0;
            const int sl = slice(((t & 0xFFFF) << 16) | (t >> 16)); // bank: (clut << 16) | tpage
            od::RenderVertex v[4];
            for (int c = 0; c < f.count; ++c) {
                float w[3];
                toWorld(part.verts[f.idx[c]], w);
                v[c].pos[0] = w[0];
                v[c].pos[1] = toPortAxes ? -w[1] : w[1];
                v[c].pos[2] = w[2];
                v[c].uv[0] = (float(f.uvs[c][0]) + 0.5f) / 256.0f;
                v[c].uv[1] = (float(f.uvs[c][1]) + 0.5f) / 256.0f;
                v[c].slice = float(sl);
                v[c].shade = 1.0f;
                v[c].cull = (f.doubleSided || g_modelsBothSides) ? 0.0f : 1.0f; // FUN_005f4980's NCLIP (bank.hpp)
                v[c].layer = partLayer;
                // actor parts (FUN_005f4980): bit 30's OT pull of 1 << shift slots = 4 << shift view-depth units (OTZ =
                // SZ / 4; the actors are drawn << the stage shift, the room's units)
                if (targetSrc && f.otNearer) v[c].layer += float(4 << shift) / od::kLayerStep;
            }
            const int n = f.count == 3 ? 3 : 6;
            for (int i = 0; i < n; ++i) {
                od::RenderVertex q = v[od::kQuadTris[i]];
                if (n == 6 && q.cull > 0.5f) q.cull = i < 3 ? 2.0f : 3.0f; // a quad: drawn when either half faces
                target->push_back(q);
                if (targetSrc) {
                    targetSrc->index.push_back(uint16_t(f.idx[size_t(od::kQuadTris[i])]));
                    targetSrc->flatColour.push_back(f.flatColour ? 1 : 0);
                }
            }
        }
        if (targetSrc) targetSrc->verts = uint32_t(part.verts.size());
    };
    std::map<int, od::BankModel> models;
    auto model = [&](int entry) -> const od::BankModel& {
        auto it = models.find(entry);
        if (it == models.end()) it = models.emplace(entry, od::decodeModel(bank, entries[entry], 0)).first;
        return it->second;
    };
    for (const od::BankEntry& e : entries) {
        if ((e.cls & 0xf0) != 0x20) continue;
        const od::BankModel& md = model(e.index);
        // the part table (model +0x14, 4 bytes per part, copied to the actor's +0xec by Maybe_BindActorModel):
        // byte 3 & 3 selects the texture cell word of the part
        const size_t mo = e.modelOfs;
        const size_t pt = mo + 0x18 <= bank.size() ? od::rd32(bank.data() + mo + 0x14) : bank.size();
        for (size_t pi = 0; pi < md.parts.size(); ++pi) {
            // Texture cells come from the model's part descriptors.
            auto [geom, fresh] = mesh.partGeom.try_emplace({e.index, int(pi)});
            if (fresh) {
                partLayer = 0.0f;
                target = &geom->second;
                targetSrc = &mesh.partSrc[{e.index, int(pi)}];
                toPortAxes = false;
                emitPart(md, md.parts[pi], [](const std::array<int16_t, 3>& q, float* w) {
                    for (int i = 0; i < 3; ++i) w[i] = q[i];
                });
                targetSrc = nullptr;
            }
            if (pt + pi * 4 + 3 < bank.size() && (bank[pt + pi * 4 + 3] & 3) &&
                !mesh.partTex.contains({e.index, int(pi)})) {
                std::vector<uint32_t>& keys = mesh.partTex[{e.index, int(pi)}];
                for (const od::ModelFace& f : md.parts[pi].faces) {
                    const uint32_t t = f.material < md.textures.size() ? md.textures[f.material] : 0;
                    const uint32_t key = ((t & 0xFFFF) << 16) | (t >> 16);
                    for (uint32_t pg = 1; pg < 4; ++pg) slice(key + (pg << 16));
                    for (int i = 0; i < (f.count == 3 ? 3 : 6); ++i) keys.push_back(key);
                }
            }
        }
    }
    for (size_t slot = 0; slot < g_altBankDat.size(); ++slot) { // the alternate banks (script DAT loads)
        if (g_altBankDat[slot].empty()) continue;
        RoomMesh::AltBank& alt = mesh.altBanks[slot];
        try {
            const auto altFile = od::dataSource().gameFile(g_altBankDat[slot]);
            const auto abank = od::modelBank(altFile);
            const od::BankSegment abs = od::modelBankSegment(altFile);
            alt.index = od::bankEntries(abank);
            alt.at = kAltBankBuffers[slot] + uint32_t(abs.bankOffset);
            alt.size = uint32_t(abank.size());
            for (const od::BankEntry& e : alt.index) {
                if ((e.cls & 0xf0) != 0x20) continue;
                const od::BankModel md = od::decodeModel(abank, e, 0);
                const size_t mo = e.modelOfs;
                const size_t pt = mo + 0x18 <= abank.size() ? od::rd32(abank.data() + mo + 0x14) : abank.size();
                for (size_t pi = 0; pi < md.parts.size(); ++pi) {
                    partLayer = 0.0f;
                    target = &alt.partGeom[{e.index, int(pi)}];
                    targetSrc = &alt.partSrc[{e.index, int(pi)}];
                    toPortAxes = false;
                    emitPart(md, md.parts[pi], [](const std::array<int16_t, 3>& q, float* w) {
                        for (int i = 0; i < 3; ++i) w[i] = q[i];
                    });
                    targetSrc = nullptr;
                    if (pt + pi * 4 + 3 < abank.size() && (abank[pt + pi * 4 + 3] & 3)) {
                        std::vector<uint32_t>& keys = alt.partTex[{e.index, int(pi)}];
                        for (const od::ModelFace& f : md.parts[pi].faces) {
                            const uint32_t t = f.material < md.textures.size() ? md.textures[f.material] : 0;
                            const uint32_t key = ((t & 0xFFFF) << 16) | (t >> 16);
                            for (uint32_t pg = 1; pg < 4; ++pg) slice(key + (pg << 16));
                            for (int i = 0; i < (f.count == 3 ? 3 : 6); ++i) keys.push_back(key);
                        }
                    }
                }
            }
            for (const od::BankEntry& e : alt.index) {
                const uint32_t c = e.cls & 0xf0u;
                if (c != 0x60 && c != 0x40 && c != 0x10) continue;
                const od::BankModel md = od::decodeModel(abank, e, 0);
                partLayer = 0.0f;
                target = &alt.entryGeom[e.index];
                toPortAxes = false;
                for (const od::ModelPart& part : md.parts)
                    emitPart(md, part, [](const std::array<int16_t, 3>& p, float* w) {
                        for (int i = 0; i < 3; ++i) w[i] = p[i];
                    });
            }
            SDL_Log("%s: alternate bank at %08x, %zu entries", g_altBankDat[slot].c_str(), alt.at,
                    alt.index.size());
        } catch (const std::exception& e) {
            SDL_Log("alternate bank %s: %s", g_altBankDat[slot].c_str(), e.what());
        }
    }
    target = &mesh.tris;
    toPortAxes = true;
    // All static, projectile and shot entries are available to the live pools.
    std::set<int> live;
    for (const od::BankEntry& e : entries)
        if ((e.cls & 0xf0) == 0x60 || (e.cls & 0xf0) == 0x40 || (e.cls & 0xf0) == 0x10)
            live.insert(e.index);
    for (const int entry : live) {
        partLayer = 0.0f;
        target = &mesh.entryGeom[entry];
        toPortAxes = false;
        const od::BankModel& md = model(entry);
        for (const od::ModelPart& part : md.parts)
            emitPart(md, part, [](const std::array<int16_t, 3>& p, float* w) {
                for (int i = 0; i < 3; ++i) w[i] = p[i];
            });
    }
    target = &mesh.tris;
    toPortAxes = true;
    mesh.bank.assign(bank.begin(), bank.end());
    {
        const od::BankSegment bs = od::modelBankSegment(st);
        mesh.bankSegment.assign(bs.seg.begin(), bs.seg.end());
        mesh.bankOffset = uint32_t(bs.bankOffset);
        mesh.playerAnims = bs.playerAnims;
        mesh.bankExtra = bs.extra;
    }
    mesh.bankIndex = entries;
}

} // namespace

namespace {
// OpenDash's fog for the rooms the disc gives no range (the PS1 has none outside its depth cue): per room, the look
// the remaster has on the PC data -- a grey haze outdoors, tinted by the place (the sea, the ice, the sand), none
// indoors, the snowing Yosyonke a cold haze as thick as the snowfall. Start / end: fractions of the depth range
struct RoomFog {
    int stage;
    uint32_t rooms; // bit r: sub-room r
    uint8_t colour[3];
    float start, end;
};
constexpr RoomFog kRoomFog[] = {
    {0x00, 0x0001, {192, 192, 192}, 0.10f, 0.60f},
    {0x01, 0x0001, {192, 192, 192}, 0.10f, 0.60f},
    {0x08, 0x0001, {182, 186, 198}, 0.00f, 0.32f},
    {0x09, 0x0001, {182, 186, 198}, 0.00f, 0.32f},
    {0x0b, 0x0001, {182, 186, 198}, 0.00f, 0.32f},
    {0x0c, 0x0007, {192, 192, 192}, 0.10f, 0.60f},
    {0x0d, 0x0003, {176, 176, 184}, 0.10f, 0.60f},
    {0x0e, 0x0007, {176, 176, 184}, 0.00f, 0.50f},
    {0x10, 0x0003, {160, 208, 224}, 0.10f, 0.60f},
    {0x11, 0x0001, {160, 208, 224}, 0.00f, 0.35f},
    {0x15, 0x0001, {192, 192, 192}, 0.10f, 0.60f},
    {0x16, 0x0001, {208, 208, 208}, 0.10f, 0.60f},
    {0x17, 0x0001, {192, 192, 192}, 0.10f, 0.60f},
    {0x18, 0x0002, {192, 192, 192}, 0.00f, 0.35f},
    {0x18, 0x0009, {192, 192, 192}, 0.10f, 0.60f},
    {0x19, 0x0007, {192, 192, 192}, 0.10f, 0.60f},
    {0x1c, 0x0007, {192, 192, 192}, 0.10f, 0.60f},
    {0x1d, 0x0007, {160, 208, 224}, 0.10f, 0.60f},
    {0x1f, 0x0003, {208, 240, 248}, 0.10f, 0.60f},
    {0x20, 0x0001, {192, 192, 192}, 0.00f, 0.50f},
    {0x21, 0x0001, {192, 192, 192}, 0.10f, 0.60f},
    {0x22, 0x0001, {208, 240, 248}, 0.10f, 0.60f},
    {0x23, 0x0001, {208, 240, 248}, 0.10f, 0.60f},
    {0x24, 0x0003, {184, 232, 216}, 0.10f, 0.60f},
    {0x25, 0x0041, {184, 232, 216}, 0.20f, 0.60f},
    {0x27, 0x0001, {192, 192, 192}, 0.10f, 0.60f},
    {0x28, 0x0001, {0, 0, 0}, 0.10f, 0.60f},
    {0x29, 0x0003, {192, 192, 192}, 0.10f, 0.60f},
    {0x2a, 0x001f, {192, 192, 192}, 0.10f, 0.60f},
    {0x2b, 0x000f, {192, 192, 192}, 0.10f, 0.60f},
    {0x30, 0x001f, {192, 192, 192}, 0.10f, 0.60f},
    {0x31, 0x0003, {192, 192, 192}, 0.10f, 0.60f},
    {0x3b, 0x0003, {192, 192, 192}, 0.10f, 0.60f},
    {0x3c, 0x001f, {192, 192, 192}, 0.10f, 0.60f},
    {0x40, 0x3ffd, {192, 192, 192}, 0.10f, 0.60f},
    {0x46, 0x000d, {192, 192, 192}, 0.10f, 0.60f},
    {0x46, 0x0002, {192, 192, 192}, 0.50f, 0.90f},
    {0x47, 0x0007, {192, 192, 192}, 0.10f, 0.60f},
    {0x48, 0x0003, {222, 206, 153}, 0.20f, 0.60f},
    {0x49, 0x0008, {192, 192, 192}, 0.10f, 0.60f},
    {0x4f, 0x4000, {160, 0, 30}, 0.00f, 0.00f},
    {0x52, 0x0008, {192, 192, 192}, 0.10f, 0.60f},
    {0x53, 0x0001, {192, 192, 192}, 0.00f, 0.50f},
    {0x54, 0x0007, {192, 192, 192}, 0.20f, 0.80f},
    {0x56, 0x0001, {192, 192, 192}, 0.10f, 0.60f},
    {0x57, 0x0001, {192, 192, 192}, 0.10f, 0.60f},
    {0x59, 0x0001, {192, 192, 192}, 0.10f, 0.60f},
    {0x5a, 0x0001, {192, 192, 192}, 0.10f, 0.60f},
    {0x5b, 0x0001, {192, 192, 192}, 0.10f, 0.60f},
};
} // namespace

od::Fog fogFor(const RoomMesh& mesh, od::FogMode mode, float distance, FogSource source) {
    od::Fog f;
    for (int c = 0; c < 3; ++c) f.color[c] = mesh.clear[c];
    if (mesh.depthCue && mode == od::FogMode::Classic) {
        // the PS1's depth cue (the original look only: the remaster has no darkening with distance): IR0 = view depth >> the coordinate shift (SLUS 0x80025b74's mtc2 before each DPCS), the
        // far colour the game's (the room's record, a script's)
        const od::port::RoomDrawState& d = od::port::roomDraw();
        f.mode = od::FogMode::DepthCue;
        for (int c = 0; c < 3; ++c) f.color[c] = float(d.fogColour[size_t(c)]) / 255.0f;
        f.end = 4096.0f * float(1u << (od::port::stage().coordinateShift & 0x1f));
        return f;
    }
    float s = mesh.fogStart, e = mesh.fogEnd;
    if (e <= s && source == FogSource::OpenDash && !mesh.d3dFog)
        for (const RoomFog& t : kRoomFog)
            if (t.stage == mesh.stage && mesh.sub >= 0 && mesh.sub < 32 && (t.rooms >> mesh.sub & 1u)) {
                for (int c = 0; c < 3; ++c) f.color[c] = float(t.colour[c]) / 255.0f;
                s = t.start * 16384.0f;
                e = t.end * 16384.0f;
            }
    if (e <= s || mode == od::FogMode::Off) return f;
    f.mode = mode;
    f.start = s;
    f.end = e;
    if (mode != od::FogMode::Remaster) return f;
    const float k = std::max(distance, 0.1f);
    constexpr float kLn2 = 0.6931472f;
    if (e <= 0.35f * 16384.0f) { // weather fog: half at 0.45 end, ~80% at the original's end
        f.density = kLn2 / (0.45f * e * k);
        f.heightFalloff = 1.0f / (1.5f * e);
        f.maxOpacity = 0.97f;
    } else { // haze: half at 1.5 end
        f.start = s * 1.5f;
        f.density = kLn2 / (1.5f * e * k);
        f.heightFalloff = 1.0f / (0.35f * e);
        f.maxOpacity = 0.8f;
    }
    return f;
}

// Endless track rooms (presentation): a room whose exit table holds an exit to itself is a looping track -- the
// game wraps the action back by the exit's delta (ST0E sub 2, the train fight: trigger x 0xb00 -> x -0x900, period
// 0x1400). Instead of showing the room the PS1 way (the scenery jumping under the camera on every wrap), the room
// is cut to the window the action lives in and that window repeated along the track: a continuous strip the trains
// run along, periodic by construction, so the wrap (render side: hooks.warp) is invisible. Logic untouched.
// Only a room whose grid holds a warp trigger wraps: an exit to itself behind a door (ST40 sub 2, the residential
// area's west door, trigger type 0xd) is a room load with a fade, and that room is drawn once.
static void buildTrackStrip(RoomMesh& mesh, int roomShift, bool warps) {
    if (!warps) return;
    int axis = -1, ahead = 1;
    float wa = 0.0f, period = 0.0f;
    for (const od::port::RoomExit& e : mesh.exits) {
        if (e.destStage != uint32_t(mesh.stage) || e.destRoom != uint32_t(mesh.sub)) continue;
        const int dx = e.destination.x - e.trigger.x, dz = e.destination.z - e.trigger.z;
        const int a = std::abs(dx) >= std::abs(dz) ? 0 : 2, dv = a == 0 ? dx : dz;
        if (std::abs(dv) < 0x400) continue;
        axis = a;
        ahead = dv < 0 ? 1 : -1; // the game wraps back by dv: the trains travel the other way
        const float s = float(1 << roomShift);
        const int16_t from = a == 0 ? e.trigger.x : e.trigger.z, to = a == 0 ? e.destination.x : e.destination.z;
        wa = float(std::min(from, to)) * s;
        period = float(std::abs(dv)) * s;
        break;
    }
    if (axis < 0) return;
    for (uint32_t r : mesh.semiStart)
        if (r % 6 != 0) return; // not whole quads (6 vertices each): keep the room
    constexpr int kBehind = 4; // periods behind the window (>= 20k units, past the fog); RoomMesh::kTrackTilesAhead ahead
    std::vector<od::RenderVertex> out;
    std::array<uint32_t, 4> starts{};
    std::unordered_map<uint32_t, uint32_t> firstOf; // a kept quad's first vertex: old -> its copy in the middle tile
    uint32_t from = 0;
    for (int r = 0; r < 4; ++r) { // opaque, then the three semi ranges, kept contiguous
        if (r > 0) starts[size_t(r - 1)] = uint32_t(out.size());
        const uint32_t to = mesh.semiStart[size_t(r)] < from ? from : (r < 3 ? mesh.semiStart[size_t(r)] : mesh.semiStart[3]);
        for (int k = -kBehind * ahead; k != (RoomMesh::kTrackTilesAhead + 1) * ahead; k += ahead)
            for (uint32_t q = from; q + 5 < to; q += 6) { // whole quads: the window by the quad's centre
                float c = 0.0f;
                for (uint32_t v = 0; v < 6; ++v) c += mesh.tris[q + v].pos[axis];
                c /= 6.0f;
                if (c < wa || c >= wa + period) continue; // the window only: the copies do not overlap
                if (k == 0) firstOf[q] = uint32_t(out.size());
                for (uint32_t v = 0; v < 6; ++v) {
                    od::RenderVertex x = mesh.tris[q + v];
                    x.pos[axis] += float(k) * period;
                    out.push_back(x);
                }
            }
        from = to;
    }
    starts[3] = uint32_t(out.size());
    SDL_Log("ST%02X sub %d: endless track strip -- window %.0f..%.0f on %c, %d tiles, %zu -> %zu triangles", mesh.stage,
            mesh.sub, wa, wa + period, axis == 0 ? 'x' : 'z', kBehind + 1 + RoomMesh::kTrackTilesAhead,
            mesh.tris.size() / 3, out.size() / 3);
    mesh.trackAxis = axis;
    mesh.trackPeriod = period;
    mesh.tris = std::move(out);
    mesh.semiStart = starts;
    mesh.uvAnim.remapStatic(firstOf); // UV animation patches the middle tile's faces
}

RoomMesh loadRoom(int stage, int sub) {
    char name[8];
    std::snprintf(name, sizeof name, "ST%02X", stage);
    // the files the stage load requests for the story state (FUN_005d5cd0's selector over the stages' file ids:
    // game/stage_files stageDatFiles); an in-stage switch keeps the stage's files (no stage load)
    std::string geometryFile, textureFile;
    if (g_inStageSwitch && g_stageFilesStage == stage && !g_stageFilesGeo.empty()) {
        geometryFile = g_stageFilesGeo;
        textureFile = g_stageFilesTex;
    } else {
        od::port::stageDatFiles(stage, geometryFile, textureFile);
    }
    if (geometryFile.empty() || textureFile.empty()) throw std::runtime_error(std::string(name) + ": no stage files");
    g_stageFilesStage = stage;
    g_stageFilesGeo = geometryFile;
    g_stageFilesTex = textureFile;
    SDL_Log("%s sub %d: story-state files %s + %s", name, sub, geometryFile.c_str(), textureFile.c_str());
    const od::GameFile st = od::dataSource().gameFile(geometryFile);

    const auto block = od::roomBlock(st);
    od::RoomStats stats;
    // Maybe_SetupRoomParams 0x5f59d0 with the sub-room's param struct (the room function's [table + sub * 0x2c]):
    // +3 = the coordinate shift (0x6d00b5; a camera script may switch it at run time, FUN_005118a0 -- the render
    // follows the live byte), +0..+2 = the D3D fog colour, floats +0x24 / +0x28 = its start / end (FUN_00401760)
    const uint32_t paramsIndex = od::roomIndex(stage, sub < 0 ? 0 : sub);
    if (!od::port::roomParamsRecords.has(paramsIndex)) throw std::runtime_error(std::string(name) + ": no room params");
    const od::port::RoomParamsRecord& roomParams = od::port::roomParamsRecords[paramsIndex];
    const int roomShift = roomParams.coordinateShift;
    auto quads = od::parseRoom(block, roomShift, sub, &stats, 0, true);
    const size_t sceneryQuads = quads.size();
    const auto floor = od::parseFloor(block, od::port::floorUvTables[uint32_t(stage)].uv, od::port::floorOrientationTable->orient,
                                      roomShift, sub < 0 ? 0 : sub);
    quads.insert(quads.end(), floor.begin(), floor.end());
    SDL_Log("%s sub %d/%zu: shift %d, %zu scenery quads (%d segments, %d skipped faces, %d unowned), %zu floor quads",
            name, sub, od::roomGrids(block).size(), roomShift, sceneryQuads, stats.segments, stats.skippedFaces,
            stats.ambiguousGrid, floor.size());

    // the exe never clears VRAM: a play load starts from the live VRAM (every earlier stage / script / player file,
    // the image moves and CLUT animation) and its files overwrite only the rects they write. Stage texture files
    // count on it: ST0E02T.DAT (the train fight after the cutscene) and ST5301T.DAT (that cutscene) upload no room /
    // cast pages at all -- ST0E01T.DAT's and ST53T.DAT's, loaded stages earlier, are still there. INIT / GAME once,
    // at the bottom (boot).
    od::Vram vram;
    if (g_roomVramGen != 0) vram = g_roomVram;
    else for (const char* f : {"INIT.DAT", "GAME.DAT"}) vram.apply(od::dataSource().gameFile(f));
    if (g_stageDatsStage != stage) {
        g_stageDats.clear();
        for (std::string& d : g_altBankDat) d.clear();
        g_keptVramMoves.clear();
        g_stageDatsStage = stage;
    }
    // the stage load's own files: the player files (Maybe_RequestPlayerFiles 0x5d5c40, reloaded by every stage
    // load) and the stage's texture file overwrite what they cover
    vram.apply(datCache().file("PL00T.DAT"));
    // the special weapon's PL00R## (Maybe_RequestPlayerFiles 0x5d5c40) carries a type-3 CLUT at VRAM (192, 240)
    // = clut 0x3c0c of the player texture table entry 3 (seg0 +0x6c), which the weapon arm's forearm (block 3
    // part 1) is drawn with; no other file writes that rect (all 15 PL00R02..10 do), so without it the forearm
    // decodes fully transparent
    if (!g_playerWeaponDat.empty()) vram.apply(datCache().file(g_playerWeaponDat));
    vram.apply(od::dataSource().gameFile(textureFile));
    std::vector<uint8_t> moved;
    auto replayMoves = [&](size_t afterDats) { // the image moves made with that many script DATs applied, again
        for (const KeptVramMove& k : g_keptVramMoves) {
            if (k.afterDats != afterDats) continue;
            const od::port::VramMove& v = k.move;
            moved.resize(size_t(v.w) * size_t(v.h) * 2);
            vram.readImage(v.sx, v.sy, v.w, v.h, moved.data());
            vram.loadImage(v.dx, v.dy, v.w, v.h, moved);
        }
    };
    replayMoves(0);
    for (size_t i = 0; i < g_stageDats.size(); ++i) {
        vram.apply(od::dataSource().gameFile(g_stageDats[i]));
        replayMoves(i + 1);
    }
    g_roomVram = vram;
    ++g_roomVramGen;

    RoomMesh mesh;
    {
        for (int c = 0; c < 3; ++c) mesh.clear[c] = float(roomParams.fogColour[size_t(c)]) / 255.0f;
        // XYZRHW z = camera depth / 16384 (FUN_00404090 & co.), table fog linear on z: start / end are fractions
        // of the depth range 16384 (_DAT_00a7b26c)
        constexpr float kDepthRange = 16384.0f;
        mesh.fogStart = roomParams.fogStart * kDepthRange;
        mesh.fogEnd = roomParams.fogEnd * kDepthRange;
        mesh.depthCue = !roomParams.d3dFog && roomParams.depthCue;
        mesh.d3dFog = roomParams.d3dFog;
        SDL_Log("ST%02X_%d: fog %.0f..%.0f colour %u %u %u", stage, sub < 0 ? 0 : sub, mesh.fogStart, mesh.fogEnd,
                roomParams.fogColour[0], roomParams.fogColour[1], roomParams.fogColour[2]);
    }
    loadSky(mesh, stage, sub < 0 ? 0 : sub);
    loadSkyGradient(mesh, stage, sub < 0 ? 0 : sub);
    std::map<uint32_t, int> sliceOf;
    auto slice = [&](uint32_t tex) { // tex = (tpage << 16) | clut
        auto [it, added] = sliceOf.try_emplace(tex, mesh.sliceCount);
        if (added) {
            const auto rgba = vram.decode(uint16_t(tex >> 16), uint16_t(tex & 0xFFFF), true);
            if (std::getenv("OD_SLICEPROBE")) { // dev: pages / CLUTs that decode fully transparent (missing VRAM)
                size_t opaque = 0;
                for (size_t i = 3; i < rgba.size(); i += 4) opaque += rgba[i] != 0;
                if (opaque == 0) SDL_Log("sliceprobe: %s sub %d tpage %04x clut %04x fully transparent", name, sub,
                                         unsigned(tex >> 16), unsigned(tex & 0xFFFF));
            }
            mesh.slices.insert(mesh.slices.end(), rgba.begin(), rgba.end());
            mesh.sliceTex.resize(size_t(mesh.sliceCount) + 1, 0);
            mesh.sliceTex[size_t(mesh.sliceCount)] = (1ull << 32) | tex;
            ++mesh.sliceCount;
        }
        return it->second;
    };
    std::vector<od::RenderVertex> semiTris[3]; // PS1 blend modes avg / add / sub, appended after opaque
    for (const od::RoomQuad& q : quads) {
        const int sl = slice(q.tex);
        od::RenderVertex v[4];
        for (int k = 0; k < 4; ++k) {
            const float p[3] = {float(q.verts[k][0]), -float(q.verts[k][1]), float(q.verts[k][2])};
            for (int c = 0; c < 3; ++c) {
                v[k].pos[c] = p[c];
                mesh.lo[c] = std::min(mesh.lo[c], p[c]);
                mesh.hi[c] = std::max(mesh.hi[c], p[c]);
            }
            // texel centres; 256 = full page width
            v[k].uv[0] = (float(q.uvs[k][0]) + 0.5f) / 256.0f;
            v[k].uv[1] = (float(q.uvs[k][1]) + 0.5f) / 256.0f;
            v[k].slice = float(sl);
            v[k].shade = float(q.shades[k]) / 128.0f;
            v[k].cull = q.doubleSided ? 0.0f : 1.0f;
            // PS1 OT priority among coplanar faces: avg (3) > avg+max (0) ~ max-16 (2) > max (1)
            static constexpr float kLayerOfMode[4] = {1.0f, 0.0f, 1.0f, 2.0f};
            v[k].layer = q.segment < 0 ? 0.0f : kLayerOfMode[q.otMode & 3];
        }
        if (q.variant >= 0) { // switchable segment: drawn per tick (PlayerView::liveScene)
            SegQuad sq{q, {}};
            for (int i = 0; i < 6; ++i) sq.v[size_t(i)] = v[od::kQuadTris[i]];
            mesh.uvAnim.addSeg(q, uint32_t(mesh.segQuads.size()));
            mesh.segQuads.push_back(sq);
            continue;
        }
        if (q.segment < 0) { // a floor tile: skipped when both halves face away (0x5f6c50 / 0x5f7270)
            SegQuad sq{q, {}};
            for (int i = 0; i < 6; ++i) {
                sq.v[size_t(i)] = v[od::kQuadTris[i]];
                sq.v[size_t(i)].cull = i < 3 ? 2.0f : 3.0f;
            }
            if (q.farBothSides) { // the near / far variant per tick (PlayerView::liveScene)
                mesh.lodFloorQuads.push_back(sq);
                continue;
            }
            mesh.tris.insert(mesh.tris.end(), sq.v.begin(), sq.v.end());
            continue;
        }
        auto& dst = q.semi ? semiTris[(q.semi - 1) % 3] : mesh.tris;
        mesh.uvAnim.addStatic(q, q.semi ? 1 + (q.semi - 1) % 3 : 0, uint32_t(dst.size()));
        for (int idx : od::kQuadTris) dst.push_back(v[idx]);
    }
    for (const od::RoomSegment& sg : od::roomSegments(block)) mesh.segFileFlags[sg.index] = uint8_t(sg.flags);
    if (od::hasModelBank(st)) loadBankGeometry(mesh, st, roomShift, slice); // none (ST57): a stage without models
    if (mesh.tris.empty() && semiTris[0].empty() && semiTris[1].empty() && semiTris[2].empty())
        mesh.tris.resize(3); // all segments switchable (ST04 sub 0): one degenerate triangle keeps the buffer valid
    for (int m = 0; m < 3; ++m) {
        mesh.semiStart[m] = uint32_t(mesh.tris.size());
        mesh.tris.insert(mesh.tris.end(), semiTris[m].begin(), semiTris[m].end());
    }
    mesh.semiStart[3] = uint32_t(mesh.tris.size());
    mesh.uvAnim.finalize(mesh.semiStart);
    for (SkyLayer& layer : mesh.sky)
        for (auto& [clut, sl] : layer.sliceOfClut) sl = slice((uint32_t(layer.tpage) << 16) | clut);
    if (stage == 1) { // the travel map's 2D layer (buildTravelMap)
        for (uint32_t i = 0; i < 4; ++i)
            mesh.travelMapSlice[i] = slice(((0x80u | (0x15u + 2u * i)) << 16) | ((0x1f0u + i) * 0x40u));
        for (uint32_t i = 0; i < 2; ++i) mesh.travelMarkerSlice[i] = slice((0x1eu << 16) | (0x7fc0u + i));
    }
    { // combat effects (PlayerView::liveEffects): texture pages modulated by the PS1 colour, the gradient page
        // the cache keeps what play met (any stage); the known emitters' pages are registered up front
        for (const od::fx::Texture& t : od::fx::textures()) g_fxTextures.index(t.tpage, t.clut, t.tint);
        for (const od::fx::Texture& t : od::storm::textures(stage)) g_fxTextures.index(t.tpage, t.clut, t.tint);
        g_fxTextures.takeDirty();
        for (const od::fx::Texture& t : g_fxTextures.entries()) {
            auto rgba = vram.decode(t.tpage, t.clut, true);
            if (t.tint != 0x808080)
                for (size_t i = 0; i + 3 < rgba.size(); i += 4)
                    for (int c = 0; c < 3; ++c)
                        rgba[i + size_t(c)] = uint8_t(std::min(255u, uint32_t(rgba[i + size_t(c)]) * ((t.tint >> (8 * c)) & 0xff) / 128u));
            mesh.fxSlices.push_back(mesh.sliceCount++);
            mesh.slices.insert(mesh.slices.end(), rgba.begin(), rgba.end());
        }
        std::vector<uint8_t> grad(256 * 256 * 4, 0);
        auto rows = od::fx::gradients();
        for (const auto& r : od::storm::gradients()) rows.push_back(r); // after the fx rows (content/intro/crash_landing_draw)
        for (size_t r = 0; r < 256; ++r)
            for (size_t u = 0; u < 256; ++u) {
                uint8_t* px = &grad[(r * 256 + u) * 4];
                px[3] = od::Vram::kStpAlpha; // stands for untextured gouraud: blends whole on a semi polygon
                if (r >= rows.size()) continue;
                for (int c = 0; c < 3; ++c) {
                    const float a = float((rows[r][0] >> (8 * c)) & 0xff), b = float((rows[r][1] >> (8 * c)) & 0xff);
                    px[c] = uint8_t(a + (b - a) * float(u) / 255.0f + 0.5f);
                }
            }
        mesh.fxGradSlice = mesh.sliceCount++;
        mesh.slices.insert(mesh.slices.end(), grad.begin(), grad.end());
    }
    try { // Megaman: model-space parts (game axes, double sided like the props)
        const od::GameFile* weaponFile = g_playerWeaponDat.empty() ? nullptr : &datCache().file(g_playerWeaponDat);
        const od::PlayerModel pm = od::decodePlayerModel(datCache().file(g_playerBodyDat), weaponFile);
        mesh.player = characterMesh(pm, od::port::PlayerLook{}, slice);
        // spawn: room centre, standing on the ground the game's own probe finds
        mesh.shift = roomShift;
        mesh.roomParams = od::dataSource().stateAddress({od::port::roomParamsRecords.id(), paramsIndex}); // the record's address the game state keeps
        mesh.roomFile = std::make_shared<const od::GameFile>(st);
        mesh.roomFileName = geometryFile;
        mesh.textureFile = textureFile;
        mesh.stage = stage;
        mesh.sub = sub < 0 ? 0 : sub;
        mesh.camera = od::port::roomCamera(stage, mesh.sub);
        mesh.exits = od::port::roomExits(stage, mesh.sub);
        const auto grids = od::roomGrids(block);
        const int gi = sub < 0 ? 0 : std::min(sub, int(grids.size()) - 1);
        mesh.grid = gi;
        if (!grids.empty()) {
            const od::CollisionRoom croom = od::collisionRoom(block, grids[size_t(gi)]);
            int cx = int((mesh.lo[0] + mesh.hi[0]) * 0.5f) >> roomShift;
            int cz = int((mesh.lo[2] + mesh.hi[2]) * 0.5f) >> roomShift;
            od::CollisionState cst;
            auto probe = [&](int x, int z) { return od::probeGround(croom, cst, int16_t(x), int16_t(-0x3000), int16_t(z), nullptr, 0, 1); };
            int32_t ground = probe(cx, cz);
            // the bounding-box centre can be outside the walkable area (ST12 sub 1): nearest floor on rings of 64
            const int ext = int(std::max(mesh.hi[0] - mesh.lo[0], mesh.hi[2] - mesh.lo[2])) >> roomShift;
            for (int r = 64; (ground == 0x7fff || ground == -0x4000) && r < ext; r += 64)
                for (int a = 0; a < 32 && (ground == 0x7fff || ground == -0x4000); ++a) {
                    const int x = cx + int(std::lround(r * std::cos(a * 0.19634954f)));
                    const int z = cz + int(std::lround(r * std::sin(a * 0.19634954f)));
                    if (x < -0x7fff || x > 0x7fff || z < -0x7fff || z > 0x7fff) continue;
                    const int32_t g = probe(x, z);
                    if (g != 0x7fff && g != -0x4000) { cx = x; cz = z; ground = g; }
                }
            mesh.playerSpawn[0] = cx * 65536;
            mesh.playerSpawn[1] = (ground == 0x7fff || ground == -0x4000 ? 0 : ground) * 65536;
            mesh.playerSpawn[2] = cz * 65536;
            SDL_Log("Megaman spawn %d %d %d (ground probe %d)", cx, ground, cz, ground);
        }
    } catch (const std::exception& e) {
        SDL_Log("Megaman not loaded: %s", e.what());
    }
    mesh.sliceOfTex = sliceOf;
    bool warps = false;
    if (const auto grids = od::roomGrids(block); !grids.empty()) {
        const int gi = sub < 0 ? 0 : std::min(sub, int(grids.size()) - 1);
        warps = od::roomHasTrigger(od::collisionRoom(block, grids[size_t(gi)]), od::kTriggerWarpType);
    }
    buildTrackStrip(mesh, roomShift, warps); // the room's exits (the data source's)
    SDL_Log("%s: %zu triangles, %d texture slices", name, mesh.tris.size() / 3, mesh.sliceCount);
    return mesh;
}

} // namespace od::app
