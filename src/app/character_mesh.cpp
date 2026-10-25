// The player model's meshes and their posed draw (see character_mesh.hpp).
#include "app/character_mesh.hpp"

#include <algorithm>
#include <cmath>

#include "engine/room.hpp"
#include "game/player/precise_pose.hpp"
#include "game/stage_state.hpp"

namespace od::app {

bool g_modelsBothSides = false;

CharacterMesh characterMesh(const od::PlayerModel& pm, const od::port::PlayerLook& look,
                            const std::function<int(uint32_t)>& slice) {
    CharacterMesh cm;
    cm.look = look;
    auto sliceOf = [&](uint32_t key) {
        const int sl = slice(key);
        cm.sliceOfTex[key] = sl;
        return sl;
    };
    for (size_t k = 0; k < pm.model.parts.size(); ++k) {
        const od::ModelPart& part = pm.model.parts[k];
        std::vector<od::RenderVertex> tris;
        std::vector<uint32_t> keys;
        const bool face = k == 7 || k == 8; // the eyes / mouth cells (0xa71ec0 / 0xa71ec1)
        for (const od::ModelFace& f : part.faces) {
            const uint32_t t = f.material < pm.model.textures.size() ? pm.model.textures[f.material] : 0;
            const uint32_t key = ((t & 0xFFFF) << 16) | (t >> 16);
            const int sl = sliceOf(key); // table: (clut << 16) | tpage
            if (face) {
                for (uint32_t pg = 1; pg < 4; ++pg) sliceOf(key + (pg << 16)); // cell n / 20 = the page offset
                for (int i = 0; i < (f.count == 3 ? 3 : 6); ++i) keys.push_back(key);
            }
            od::RenderVertex v[4]{};
            for (int c = 0; c < f.count; ++c) {
                const auto& p = part.verts[f.idx[size_t(c)]];
                for (int i = 0; i < 3; ++i) v[c].pos[i] = float(p[size_t(i)]);
                v[c].uv[0] = (float(f.uvs[size_t(c)][0]) + 0.5f) / 256.0f;
                v[c].uv[1] = (float(f.uvs[size_t(c)][1]) + 0.5f) / 256.0f;
                v[c].slice = float(sl);
                v[c].shade = 1.0f;
                v[c].cull = (f.doubleSided || g_modelsBothSides) ? 0.0f : 1.0f; // 0x5f3820's NCLIP
                v[c].layer = 0.0f;
            }
            const int n = f.count == 3 ? 3 : 6;
            for (int i = 0; i < n; ++i) {
                od::RenderVertex q = v[od::kQuadTris[i]];
                if (n == 6 && q.cull > 0.5f) q.cull = i < 3 ? 4.0f : 5.0f; // a quad: the first half decides
                tris.push_back(q);
            }
        }
        cm.parts.push_back(std::move(tris));
        cm.partTex.push_back(std::move(keys));
        cm.bones.push_back(pm.partBone[k]);
        cm.blocks.push_back(pm.partBlock[k]);
    }
    return cm;
}

void applyTextureCell(const std::map<uint32_t, int>& sliceOfTex, const std::vector<uint32_t>& keys, size_t first,
                      uint32_t uvOff, uint32_t pageOff, od::RenderVertex& v, size_t vi) {
    (void)first;
    if (vi >= keys.size()) return;
    v.uv[0] += float(uvOff & 0xff) / 256.0f;
    v.uv[1] += float((uvOff >> 8) & 0xff) / 256.0f;
    if (pageOff != 0) {
        const auto it = sliceOfTex.find(keys[vi] + (pageOff << 16));
        if (it != sliceOfTex.end()) v.slice = float(it->second);
    }
}

constexpr uint32_t kNeutralLight = 0x808080; // the player's light colour with no tint (the vertex colours as they are)

std::vector<od::RenderVertex> buildCharacter(int roomShift, const CharacterMesh& cm, const od::port::Player& pl) {
    std::vector<od::RenderVertex> out;
    // the bone translations are << the live shift 0x6d00b5 (a camera script may switch it, FUN_005118a0)
    const int d = roomShift - int(od::port::stage().coordinateShift);
    const float shiftScale = d >= 0 ? float(1 << d) : 1.0f / float(1 << -d);
    for (size_t k = 0; k < cm.parts.size(); ++k) {
        if (k < cm.blocks.size() && !od::playerPartShown(cm.blocks[k], pl.activeSpecial, pl.activeBuster))
            continue; // Maybe_DrawPlayerModel 0x5f3440: hand or buster arm by the weapon slots
        const od::port::PoseMatrix& bone = pl.bones[size_t(cm.bones[k])];
        float m[9], t[3];
        if (!od::precise::matrix(bone.rotation.data(), m)) // the unrounded pose when on (F1 "animacoes suaves")
            for (size_t i = 0; i < 9; ++i) m[i] = float(bone.rotation[i]);
        // bone world position = the bone point (+0x490: (pos * 7 + 4 + t) >> 3 -- the matrix translation t
        // accumulates the skeleton offsets unscaled, the 1/8 model scale is folded into the rotation)
        const od::port::ShortVec3& point = pl.bonePoints[size_t(cm.bones[k])];
        if (!od::precise::point(&point.x, t)) {
            const int16_t fixed[3] = {point.x, point.y, point.z};
            for (int i = 0; i < 3; ++i) t[i] = float(fixed[i]);
        }
        // Maybe_DrawPlayerModel 0x5f3440, head block parts 1 / 2: the eyes / mouth cell n = (s8) +0x1a0 / +0x1a1
        // (the face tracks of the cutscenes, FUN_005e3410): cell n % 20 of a 4 x 5 grid of 64 x 51 texels
        // (u + (r & 3) * 64, v + (r >> 2) * 51), page + n / 20
        uint32_t uvOff = 0, pageOff = 0;
        const bool face = (k == 7 || k == 8) && k < cm.partTex.size();
        if (face) {
            const int n = pl.idleFaces[k == 7 ? 0 : 1];
            const int r = n % 20;
            uvOff = uint32_t(((r >> 2) * 0x3300) | ((r % 4) << 6)) & 0xffffu;
            pageOff = uint32_t(n / 20) & 3u;
        }
        size_t vi = 0;
        for (od::RenderVertex v : cm.parts[k]) {
            if (face && (uvOff | pageOff)) applyTextureCell(cm.sliceOfTex, cm.partTex[k], 0, uvOff, pageOff, v, vi);
            ++vi;
            // Maybe_DrawPlayerModel 0x5f3440: the model's vertex colours shifted by the player's light colour (+0x2a8,
            // a status tint: game/status_markers) less 0x80, each channel clamped to 0 .. 0xff
            if (pl.ambientColour != kNeutralLight)
                for (int c = 0; c < 3; ++c) {
                    const int shift = int((pl.ambientColour >> (8 * c)) & 0xff) - 0x80;
                    const int byte = std::clamp(int(std::lround(v.color[c] * 128.0f)) + shift, 0, 0xff);
                    v.color[c] = float(byte) / 128.0f;
                }
            const float x = v.pos[0], y = v.pos[1], z = v.pos[2];
            float w[3];
            for (int i = 0; i < 3; ++i)
                w[i] = (t[i] + (m[3 * i] * x + m[3 * i + 1] * y + m[3 * i + 2] * z) / 4096.0f) * shiftScale;
            v.pos[0] = w[0];
            v.pos[1] = -w[1];
            v.pos[2] = w[2];
            out.push_back(v);
        }
    }
    return out;
}

} // namespace od::app
