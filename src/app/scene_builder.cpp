#include "app/play_session.hpp"
#include "app/static_blend.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

#include "engine/bank.hpp"
#include "engine/room.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/door_actor.hpp"
#include "game/model_colour.hpp"
#include "game/room_grid.hpp"
#include "game/stage_state.hpp"
#include "game/player/player_model.hpp"
#include "game/player/player_pose.hpp"
#include "game/player/player_state.hpp"
#include "game/player/precise_pose.hpp"
#include "game/projection.hpp"

namespace od::app {


std::vector<od::RenderVertex> PlayerView::liveScene(const RoomMesh& mesh, size_t* segVerts,
                                                   std::vector<od::DynamicObject>* perActor,
                                                   std::array<std::vector<od::RenderVertex>, 3>* segSemi) const {
    // the exe's whole-model centre test (game/projection modelCentreDrawn) through the game's camera: applied while
    // that camera is the one drawn (a cutscene's shots); OpenDash's own camera in play draws every model
    const bool gameView = play && !freeView(mesh);
    std::vector<od::RenderVertex> out;
    const bool bound = play && roomStage == mesh.stage && roomSub == mesh.sub;
    const uint32_t segs = bound ? uint32_t(od::port::roomGrid().segments) : 0;
    for (const SegQuad& sq : mesh.segQuads) {
        uint8_t flags = 0;
        if (segs)
            flags = od::port::SegmentEntry(segs + od::kListHeaderSize +
                                           uint32_t(sq.q.segment) * od::kSegmentEntrySize)
                        .flags;
        else if (auto it = mesh.segFileFlags.find(sq.q.segment); it != mesh.segFileFlags.end()) flags = it->second;
        if (!od::segmentQuadDrawn(sq.q, flags)) continue;
        // FUN_005fbba0 blends a segment face like any room face (d0 >> 24 & 3): drawn opaque, the ST4B sub 1 key
        // cases were solid yellow
        auto& dst = sq.q.semi && segSemi ? (*segSemi)[size_t(sq.q.semi - 1) % 3] : out;
        dst.insert(dst.end(), sq.v.begin(), sq.v.end());
    }
    // the floor tiles whose far variant draws both sides (RoomQuad::farBothSides): near while the view z of the
    // cell's point >> 2 is below the first LOD distance (exe 0x5f68d0); the viewer (no game camera) draws them near
    for (const SegQuad& sq : mesh.lodFloorQuads) {
        const bool far = bound && (od::port::actorCentreViewZ(sq.q.lodPoint.data()) >> 2) >= od::port::stage().lodDistances[0];
        const size_t first = out.size();
        out.insert(out.end(), sq.v.begin(), sq.v.end());
        if (far)
            for (size_t i = first; i < out.size(); ++i) out[i].cull = 0.0f;
    }
    if (segVerts) *segVerts = out.size();
    const float k = float(1 << mesh.shift);
    auto place = [&](int entry, const float* pos, const float* rot, const float* scl,
                     const RoomMesh::AltBank* alt = nullptr) -> bool {
        const auto& geom = alt ? alt->entryGeom : mesh.entryGeom;
        auto it = geom.find(entry);
        if (it == geom.end()) return false;
        constexpr float kAngle = 6.2831853f / 4096.0f;
        // M = Ry * Rx * Rz (the bank-model transform)
        const float cx = std::cos(rot[0] * kAngle), sx = std::sin(rot[0] * kAngle);
        const float cy = std::cos(rot[1] * kAngle), sy = std::sin(rot[1] * kAngle);
        const float cz = std::cos(rot[2] * kAngle), sz = std::sin(rot[2] * kAngle);
        const float rz[3][3] = {{cz, -sz, 0}, {sz, cz, 0}, {0, 0, 1}};
        const float rx[3][3] = {{1, 0, 0}, {0, cx, -sx}, {0, sx, cx}};
        const float ry[3][3] = {{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}};
        float rxz[3][3]{}, m[3][3]{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int q = 0; q < 3; ++q) rxz[i][j] += rx[i][q] * rz[q][j];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int q = 0; q < 3; ++q) m[i][j] += ry[i][q] * rxz[q][j];
        for (od::RenderVertex v : it->second) {
            const float ps[3] = {v.pos[0] * scl[0], v.pos[1] * scl[1], v.pos[2] * scl[2]};
            float w[3];
            for (int i = 0; i < 3; ++i) w[i] = (pos[i] + m[i][0] * ps[0] + m[i][1] * ps[1] + m[i][2] * ps[2]) * k;
            v.pos[0] = w[0];
            v.pos[1] = -w[1];
            v.pos[2] = w[2];
            out.push_back(v);
        }
        return true;
    };
    auto actorPose = [&](uint32_t a, float* pos, float* rot, float* scl) {
        const od::port::MeshActor& act = od::port::meshActorAt(a);
        const int32_t p[3] = {act.pos.x, act.pos.y, act.pos.z};
        const int16_t r[3] = {act.rotation.x, act.rotation.y, act.rotation.z};
        const int16_t s[3] = {act.scale.x, act.scale.y, act.scale.z};
        for (int i = 0; i < 3; ++i) {
            pos[i] = float(p[i]) / 65536.0f;
            rot[i] = float(r[i]);
            scl[i] = float(s[i]) / 4096.0f;
        }
    };
    // the bank entry of a bound model address (+0x94): an alternate bank a script loaded into a type-1 buffer (alt
    // set; ST18 script 0x37 swaps the buffers before spawning the gunship), else the room's bank. The alternate one
    // first: a script DAT loaded over the buffer the stage bank sits in (the attract's ST0201 at 0x76c800) is what
    // that memory holds now
    auto modelEntry = [&](uint32_t md, const RoomMesh::AltBank*& alt) -> int {
        if ((alt = mesh.altBankOf(md)) != nullptr) {
            for (const od::BankEntry& e : alt->index)
                if (e.modelOfs == md - alt->at) return e.index;
            return -1;
        }
        if (md >= bankAt && md < bankAt + uint32_t(mesh.bank.size()))
            for (const od::BankEntry& e : mesh.bankIndex)
                if (e.modelOfs == md - bankAt) return e.index;
        return -1;
    };
    // one actor's vertices (appended from `first`) into its own object when perActor is given
    auto splitOff = [&](size_t first, uint32_t pool, uint32_t slot, uint8_t blend = 0) {
        if (!perActor || first >= out.size()) return;
        od::DynamicObject o{od::objectId(od::kKindPoolModel, pool << 8 | slot), {}, blend};
        o.tris.assign(out.begin() + std::ptrdiff_t(first), out.end());
        out.resize(first);
        perActor->push_back(std::move(o));
    };
    // Maybe_DrawBankModelActor's colour (0x5f48a7, all three pools): 0x6d0100 = rgb15(+0xb6), flags bit 1 = that
    // colour != 0x808080; FUN_005f4980 then gives every corner that colour unless flags & 3 == 1 (the model's own
    // vertex colours). The travel map's ground shadows (+0xb6 = 0, game/travel) are black, a blinking shot white
    auto bankColour = [&](uint32_t a, size_t first) {
        const od::port::MeshActor& act = od::port::meshActorAt(a);
        const uint32_t colour = od::port::rgb15(act.tint);
        const int16_t centre[3] = {act.pos.x.whole, act.pos.y.whole, act.pos.z.whole};
        const uint32_t flags =
            od::port::modelDrawFlags(act.flags, od::port::modelOtz(od::port::actorCentreViewZ(centre)));
        for (size_t k = first; k < out.size(); ++k) {
            if ((act.flags & 0x30) == 0x20) out[k].layer = 3.0f;
            if ((flags & 1) && colour == 0x808080) continue;
            for (int c = 0; c < 3; ++c) out[k].color[c] = float((colour >> (8 * c)) & 0xff) / 128.0f; // 0x80 = 1
        }
    };
    // Every static-pool actor, including doors, uses its live pose and bound model.
    if (bound && bankAt && files.mapped(bankAt, 4)) {
        for (uint32_t i = 0; i < 64; ++i) {
            const uint32_t a = od::port::kStaticPool + i * 0xcc;
            const od::port::StaticActor& act = od::port::staticActorAt(a);
            if (!(act.flags & act.runFlags & od::port::kActorDrawn) || act.state == 0 || !actorLive(a, false)) continue;
            const int16_t centre[3] = {act.pos.x.whole, act.pos.y.whole, act.pos.z.whole};
            if (gameView && !od::port::modelCentreDrawn(act.flags, centre)) continue; // the whole model culled (exe 0x5f4600..)
            const RoomMesh::AltBank* alt = nullptr;
            const uint32_t model = act.model;
            const int entry = modelEntry(model, alt);
            if (entry < 0) {
                if (model) drawMiss(a, od::port::ActorPool::Statics, "model in no loaded bank", model);
                continue;
            }
            float pos[3], rot[3], scl[3];
            actorPose(a, pos, rot, scl);
            const size_t first = out.size();
            if (!place(entry, pos, rot, scl, alt))
                drawMiss(a, od::port::ActorPool::Statics, "no geometry for the bank entry", model);
            bankColour(a, first);
            splitOff(first, 0x60, i, staticModelBlend(a)); // +0xbb semi-transparency (app/static_blend)
        }
    }
    // projectiles / shots (pool 0x40, 32 x 0xdc) and the player-side pool 0x10 (16 x 0x100) with a bank model:
    // Maybe_DrawActorPools 0x5f4080 draws them with Maybe_DrawBankModelActor when byte 0 & byte 1 & 6 == 6
    // (bit 2 = bound to a bank model, set by Maybe_BindActorModel for classes 0x10 / 0x40), same fields as the
    // static pool (+0x94 model, +0x10 position, +0x28 rotation, +0xb0 scale). The +0x74 matrix composition of
    // byte 0 & 0x80 is not reproduced (rotation only).
    for (const auto [base, count, stride] :
         {std::tuple<uint32_t, uint32_t, uint32_t>{od::port::kProjectilePool, 32, 0xdc}, {od::port::kShotPool, 16, 0x100}}) {
        if (!bound || !bankAt || !files.mapped(bankAt, 4)) break;
        constexpr uint8_t kDrawnMesh = od::port::kActorDrawn | od::port::kActorMeshModel;
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t a = base + i * stride;
            const od::port::MeshActor& act = od::port::meshActorAt(a);
            if ((act.flags & act.runFlags & kDrawnMesh) != kDrawnMesh || !actorLive(a, false))
                continue;
            const int16_t centre[3] = {act.pos.x.whole, act.pos.y.whole, act.pos.z.whole};
            if (gameView && !od::port::modelCentreDrawn(act.flags, centre)) continue; // the whole model culled (exe 0x5f4600..)
            const bool projectile = base == od::port::kProjectilePool;
            const od::port::ActorPool pool = projectile ? od::port::ActorPool::Projectiles : od::port::ActorPool::Shots;
            const RoomMesh::AltBank* alt = nullptr;
            const uint32_t model = act.model;
            const int entry = modelEntry(model, alt);
            if (entry < 0) {
                drawMiss(a, pool, "model in no loaded bank", model);
                continue;
            }
            float pos[3], rot[3], scl[3];
            actorPose(a, pos, rot, scl);
            const size_t first = out.size();
            if (!place(entry, pos, rot, scl, alt)) drawMiss(a, pool, "no geometry for the bank entry", model);
            bankColour(a, first);
            splitOff(first, projectile ? 0x40u : 0x10u, i, staticModelBlend(a)); // same drawer as pool 0x60
        }
    }
    return out;
}

float PlayerView::liveShiftScale(const RoomMesh& mesh) const {
    if (!play) return 1.0f;
    const int d = mesh.shift - int(od::port::stage().coordinateShift);
    return d >= 0 ? float(1 << d) : 1.0f / float(1 << -d);
}

std::vector<od::DynamicObject> PlayerView::liveActors(const RoomMesh& mesh) const {
    // the exe's whole-model centre test (game/projection modelCentreDrawn) through the game's camera: applied while
    // that camera is the one drawn (a cutscene's shots); OpenDash's own camera in play draws every model
    const bool gameView = play && !freeView(mesh);
    std::vector<od::DynamicObject> out;
    if (!play || roomStage != mesh.stage || roomSub != mesh.sub || !bankAt) return out;
    const float shiftScale = liveShiftScale(mesh);
    for (uint32_t i = 0; i < 24; ++i) {
        const uint32_t a = od::port::kAnimatedPool + i * 0x16c;
        const od::port::AnimatedActor& act = od::port::animatedActorAt(a);
        if (!(act.flags & od::port::kActorLive)) continue; // frozen actors too (bit 2 tested below)
        int entry = -1;
        const uint32_t mp = act.model;
        const RoomMesh::AltBank* alt = mesh.altBankOf(mp);
        if (alt) {
            for (const od::BankEntry& e : alt->index)
                if (e.modelOfs == mp - alt->at) entry = e.index;
        } else if (mp != 0) {
            for (const od::BankEntry& e : mesh.bankIndex)
                if (e.modelOfs == act.model - bankAt) entry = e.index;
        } else { // not bound yet (state 0 of an unported class): the entry Maybe_BindActorModel would take
            for (const od::BankEntry& e : mesh.bankIndex)
                if ((e.cls & 0xf0) == 0x20 && (e.cls & 0xf) == ((~uint32_t(act.loopFlags) & 8u) >> 3) &&
                    e.id == act.type && e.variant == act.variant) {
                    entry = e.index;
                    break;
                }
        }
        if (entry < 0) {
            if (mp != 0 && (act.flags & act.runFlags & od::port::kActorDrawn))
                drawMiss(a, od::port::ActorPool::Animated, "model in no loaded bank", mp);
            continue;
        }
        if (!actorLive(a, false)) continue;
        const size_t first = out.size();
        out.push_back(od::DynamicObject{od::objectId(od::kKindActor, i), {}});
        if (!(act.flags & act.runFlags & od::port::kActorDrawn)) continue; // hidden
        const int16_t centre[3] = {act.pos.x.whole, act.pos.y.whole, act.pos.z.whole};
        if (gameView && !od::port::modelCentreDrawn(act.flags, centre)) continue; // the whole model culled (exe 0x5f4160)
        // FUN_005f4160's semi-transparency: draw word 0x6d00e0 |= +0xeb << 20 for the whole actor, or with
        // +0xeb & 1 per part the bone block's byte +0xa3 + (ref & 3) * 4 << 20 (after & 0xfd9fffff); the packet
        // writer 0x5f5500 turns bit 25 into the GP0 semi bit and bits 21..22 into the tpage ABR. The semi parts
        // go to extra objects of the slot (id | blend << 8) drawn with the renderer's PS1 blend pipelines
        const uint8_t actorSemi = act.semiTransparency;
        auto objFor = [&](uint8_t blend) -> od::DynamicObject& {
            if (blend == 0) return out[first];
            const uint32_t id = od::objectId(od::kKindActor, i | uint32_t(blend) << 8);
            for (size_t k = first + 1; k < out.size(); ++k)
                if (out[k].id == id) return out[k];
            out.push_back(od::DynamicObject{id, {}, blend});
            return out.back();
        };
        const uint32_t mask = act.boneBlocks;
        // FUN_005f4160's draw flags (the centre +0x12 / +0x16 / +0x1a through the camera matrix 0x6d0014 and the
        // translation 0x6d003c, 0x40c410; its OTZ) and part colour (+0xe6, or
        // per part the bone block's +0x86): game/model_colour. The semi parts of the Master's dissolve (ST52 sub 2,
        // +0xeb 0x22 with +0xe6 0x18c6) are the texture at 0x30 / 0x80 added, not at full brightness
        const uint32_t drawFlags =
            od::port::modelDrawFlags(act.flags, od::port::modelOtz(od::port::actorCentreViewZ(centre)));
        const uint16_t ownColour = act.tint;
        uint32_t partColour = od::port::rgb15(ownColour);
        // the parts drawn this tick (flags 0x80 = hidden) as the object's layout: the cutscene casts swap parts
        // on the same bone (e.g. Barrel's two heads in script 6, parts 1 / 11 of ST3A type 0x1a, which have the
        // same vertex count), and blending the previous tick's vertices into another part's tore the model apart
        // for one frame at every swap (the "flicker")
        uint32_t layout = 2166136261u ^ uint32_t(entry) * 16777619u;
        // FUN_005f4980's shared vertices: in a part with flags 0x40, the vertex word (+0xbc, the parts' vertex
        // colours in order) top byte 0x80 | s keeps the posed vertex in slot s, 0xc0 | s takes slot s's instead
        // (posed by an earlier part's bone): the stretched polygons joining two bones (Bola's skirt)
        std::array<std::array<float, 3>, 64> shared{};
        const auto& srcOf = alt ? alt->partSrc : mesh.partSrc;
        uint32_t colours = act.colourTables[0];
        for (uint32_t part = 0; part < act.lodParts(0); ++part) {
            const od::port::PartEntry entryOfPart = act.parts[part];
            const uint8_t ref = entryOfPart.boneSlot, flags = entryOfPart.flags;
            const auto src = srcOf.find({entry, int(part)});
            const uint32_t colourBase = colours;
            if (src != srcOf.end()) colours += src->second.verts * 4; // advanced for every part, drawn or not
            if (flags & 0x80) continue;
            layout = (layout ^ (part + 1)) * 16777619u;
            const od::port::BoneBlock* blk = nullptr;
            for (int k = 0, left = (ref >> 2) + 1, bit = 31; bit >= 0; ++k, --bit)
                if ((mask & (1u << bit)) && --left == 0) {
                    blk = &od::port::boneBlockPool()[size_t(k)];
                    break;
                }
            if (!blk) continue;
            const od::port::BoneBlock& block = *blk;
            const size_t bone = ref & 3u;
            if (ownColour == 0) partColour = od::port::rgb15(block.boneColour(bone));
            // FUN_005f4980: flags & 3 == 1 (bit 1 = the part colour != 0x808080) takes the vertex colours
            const bool vertexColours = (drawFlags & 1) && partColour == 0x808080;
            const uint8_t semi = (actorSemi & 1) ? block.semiBits(bone) : actorSemi;
            od::DynamicObject& obj = objFor((semi & 0x20) ? uint8_t(((semi >> 1) & 3) + 1) : uint8_t(0));
            const auto& geomOf = alt ? alt->partGeom : mesh.partGeom;
            auto it = geomOf.find({entry, int(part)});
            if (it == geomOf.end()) continue;
            // FUN_005f4160: part flags & 3 = k selects the texture cell word +0xd4 + k * 4 (FUN_005e32c0: cell
            // u | v << 8 in the low 16 bits, the page offset in bits 30..31) -- the cutscene faces (FUN_005e34f0)
            uint32_t uvOff = 0, pageOff = 0;
            const auto& texOf = alt ? alt->partTex : mesh.partTex;
            const auto tex = (flags & 3) ? texOf.find({entry, int(part)}) : texOf.end();
            if (tex != texOf.end()) {
                const uint32_t word = act.textureCellWord(uint32_t(flags & 3));
                uvOff = word & 0xffff;
                pageOff = (word >> 30) & 3;
            }
            float m[9], t[3];
            if (!od::precise::matrix(block.matrices[bone].e, m)) { // the unrounded pose when on (F1)
                const od::port::Matrix3& fixed = block.matrices[bone];
                for (uint32_t k = 0; k < 9; ++k) m[k] = float(fixed.e[k]);
            }
            const od::port::ShortVec4& point = block.points[bone];
            if (!od::precise::point(&point.x, t)) {
                const int16_t fixed[3] = {point.x, point.y, point.z};
                for (uint32_t k = 0; k < 3; ++k) t[k] = float(fixed[k]);
            }
            auto pose = [&](const float* q, float* w) {
                for (int r = 0; r < 3; ++r) w[r] = t[r] + (m[3 * r] * q[0] + m[3 * r + 1] * q[1] + m[3 * r + 2] * q[2]) / 4096.0f;
            };
            // the part's posed vertices by source index (game axes), then the shared-vertex slots
            std::vector<std::array<float, 3>> posed;
            const bool stitch = src != srcOf.end() && src->second.index.size() == it->second.size();
            if (stitch) {
                posed.assign(src->second.verts, {0.0f, 0.0f, 0.0f});
                for (size_t j = 0; j < it->second.size(); ++j)
                    if (src->second.index[j] < posed.size()) pose(it->second[j].pos, posed[src->second.index[j]].data());
                if ((flags & 0x40) && files.mapped(colourBase, src->second.verts * 4))
                    for (uint32_t k = 0; k < src->second.verts; ++k) {
                        const uint8_t op = od::port::VertexColourWord(colourBase + k * od::port::kVertexColourSize).op;
                        if ((op & 0xc0) == 0x80) shared[op & 0x3f] = posed[k];
                        else if ((op & 0xc0) == 0xc0) posed[k] = shared[op & 0x3f];
                    }
            }
            size_t vi = 0;
            for (od::RenderVertex v : it->second) {
                if (tex != texOf.end() && (uvOff | pageOff))
                    applyTextureCell(mesh.sliceOfTex, tex->second, 0, uvOff, pageOff, v, vi);
                float w[3];
                if (stitch && src->second.index[vi] < posed.size())
                    std::copy(posed[src->second.index[vi]].begin(), posed[src->second.index[vi]].end(), w);
                else
                    pose(v.pos, w);
                uint32_t colour = partColour;
                if (vertexColours && stitch && !src->second.flatColour[vi] &&
                    files.mapped(colourBase + src->second.index[vi] * 4u, 4))
                    colour = od::port::VertexColourWord(colourBase + src->second.index[vi] * od::port::kVertexColourSize)
                                 .word &
                             0xffffff;
                for (int c = 0; c < 3; ++c) v.color[c] = float((colour >> (8 * c)) & 0xff) / 128.0f; // PS1 0x80 = 1
                ++vi;
                v.pos[0] = w[0] * shiftScale;
                v.pos[1] = -w[1] * shiftScale;
                v.pos[2] = w[2] * shiftScale;
                obj.tris.push_back(v);
            }
        }
        for (size_t k = first; k < out.size(); ++k) out[k].layout = layout;
    }
    return out;
}

std::vector<od::DynamicObject> PlayerView::liveEffects(const RoomMesh& mesh, const od::Camera& cam) const {
    std::vector<od::DynamicObject> out;
    if (!play || roomStage != mesh.stage || roomSub != mesh.sub || mesh.fxSlices.empty()) return out;
    const float f[3] = {std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch), std::cos(cam.yaw) * std::cos(cam.pitch)};
    float r[3] = {f[2], 0.0f, -f[0]}; // LookToLH: right = up x forward
    const float rl = std::sqrt(r[0] * r[0] + r[2] * r[2]);
    if (rl < 1e-6f) return out;
    r[0] /= rl;
    r[2] /= rl;
    const float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]}; // fwd x right
    const float tanHalf = std::tan(cam.fovY * 0.5f);
    // the prims are in game units (projection / 2^shift); the room is drawn << mesh.shift (as app/reticle_overlay):
    // unscaled, every effect of a shift-1 room (the town interiors) sat at half its position and size
    const float world = float(1 << mesh.shift);
    std::map<uint32_t, size_t> index;
    for (const od::fx::Prim& p : fxPrims) {
        if (p.front) continue; // the lock-on reticle: per rendered frame (app/reticle_overlay)
        const uint32_t id = od::objectId(od::kKindEffect, p.object | p.blend);
        auto [it, added] = index.try_emplace(id, out.size());
        if (added) out.push_back(od::DynamicObject{id, {}, p.blend});
        od::DynamicObject& obj = out[it->second];
        float c[3] = {p.centre[0] * world, -p.centre[1] * world, p.centre[2] * world};
        if (p.texture >= 0 && size_t(p.texture) >= mesh.fxSlices.size()) continue; // slice not built yet
        const float slice = p.texture == od::fx::kUntextured ? -1.0f // plain gouraud (Vertex::color)
                            : float(p.texture < 0 ? mesh.fxGradSlice : mesh.fxSlices[size_t(p.texture)]);
        od::RenderVertex v[4]{};
        float pos[4][3];
        int n = p.n;
        if (p.line) { // a 1-pixel PS1 line: a thin quad across the view direction (half a 240-line pixel each side)
            float a[3], b[3], d[3], e[3];
            for (int k = 0; k < 3; ++k) {
                a[k] = c[k] + (k == 1 ? -p.v[0].off[k] : p.v[0].off[k]) * world;
                b[k] = c[k] + (k == 1 ? -p.v[1].off[k] : p.v[1].off[k]) * world;
                d[k] = b[k] - a[k];
                e[k] = cam.eye[k] - (a[k] + b[k]) * 0.5f;
            }
            const float dist = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
            float sd[3] = {d[1] * e[2] - d[2] * e[1], d[2] * e[0] - d[0] * e[2], d[0] * e[1] - d[1] * e[0]};
            const float sl = std::sqrt(sd[0] * sd[0] + sd[1] * sd[1] + sd[2] * sd[2]);
            if (sl < 1e-6f) continue;
            const float hw = dist * tanHalf / 240.0f;
            for (int k = 0; k < 3; ++k) {
                sd[k] *= hw / sl;
                pos[0][k] = a[k] - sd[k];
                pos[1][k] = a[k] + sd[k];
                pos[2][k] = b[k] - sd[k];
                pos[3][k] = b[k] + sd[k];
            }
            const od::fx::Vertex* src[4] = {&p.v[0], &p.v[0], &p.v[1], &p.v[1]};
            for (int k = 0; k < 4; ++k) {
                v[k].uv[0] = src[k]->u / 256.0f;
                v[k].uv[1] = src[k]->v / 256.0f;
                v[k].shade = src[k]->shade;
            }
            n = 4;
        } else {
            for (int k = 0; k < n; ++k) {
                const od::fx::Vertex& s = p.v[k];
                for (int q = 0; q < 3; ++q)
                    pos[k][q] = p.screen ? c[q] + ((q == 1 ? -s.woff[q] : s.woff[q]) + r[q] * s.off[0] - u[q] * s.off[1]) * world
                                         : c[q] + (q == 1 ? -s.off[q] : s.off[q]) * world;
                v[k].uv[0] = s.u / 256.0f;
                v[k].uv[1] = s.v / 256.0f;
                v[k].shade = s.shade;
                for (int q = 0; q < 3; ++q) v[k].color[q] = s.color[q];
            }
        }
        for (int k = 0; k < n; ++k) {
            for (int q = 0; q < 3; ++q) v[k].pos[q] = pos[k][q];
            v[k].slice = slice;
            v[k].cull = 0.0f;
            v[k].layer = 0.0f;
        }
        if (n == 3)
            for (int k = 0; k < 3; ++k) obj.tris.push_back(v[k]);
        else
            for (int idx : od::kQuadTris) obj.tris.push_back(v[idx]);
    }
    return out;
}

std::vector<od::DynamicObject> PlayerView::screenOverlays(const RoomMesh& mesh, const od::Camera& cam,
                                                          bool remasterWeather) const {
    std::vector<od::DynamicObject> out;
    if (!play || roomStage != mesh.stage || roomSub != mesh.sub || mesh.fxSlices.empty()) return out;
    const float f[3] = {std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch), std::cos(cam.yaw) * std::cos(cam.pitch)};
    float r[3] = {f[2], 0.0f, -f[0]}; // LookToLH: right = up x forward
    const float rl = std::sqrt(r[0] * r[0] + r[2] * r[2]);
    if (rl < 1e-6f) return out;
    r[0] /= rl;
    r[2] /= rl;
    const float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]}; // fwd x right
    const float tanHalf = std::tan(cam.fovY * 0.5f);
    // the storm's screen glow (content/intro/crash_landing_draw Frame::front, OT slot 0x78 = over the scene, additive): the PS1
    // frame (320 x 240 centred in a viewAspect window, 240 lines) on a plane just beyond the near plane
    if (!storm.front.empty()) {
        od::DynamicObject obj{od::objectId(od::kKindEffect, 0x77000u | 2u), {}, 2};
        constexpr float kDepth = 12.0f; // renderer near plane 8
        const float vw = 240.0f * viewAspect, ox = (vw - 320.0f) * 0.5f;
        for (const od::storm::Tri& t : storm.front) {
            if (t.row < 0) continue;
            for (int k = 0; k < 3; ++k) {
                od::RenderVertex v{};
                const float nx = (t.x[k] + ox) / vw * 2.0f - 1.0f, ny = 1.0f - t.y[k] / 120.0f;
                for (int q = 0; q < 3; ++q)
                    v.pos[q] = cam.eye[q] + kDepth * (f[q] + r[q] * nx * tanHalf * viewAspect + u[q] * ny * tanHalf);
                v.uv[0] = t.colour[k] ? 0.5f / 256.0f : 255.5f / 256.0f;
                v.uv[1] = (float(t.row) + 0.5f) / 256.0f;
                v.slice = float(mesh.fxGradSlice);
                v.shade = t.shade;
                obj.tris.push_back(v);
            }
        }
        if (!obj.tris.empty()) out.push_back(std::move(obj));
    }
    // the storm's depth-OT sprites (Frame::depth, the sea of the cloud rings): opaque, the PS1 frame on a plane at the
    // static's depth along this camera's view (the room is drawn << mesh.shift), so the depth test hides what lies
    // behind the static as the OT order did; OT slot 0 (off range) is never drawn (Frame::depth)
    if (!storm.depth.empty()) {
        od::DynamicObject obj{od::objectId(od::kKindEffect, 0x77000u | 4u), {}, 0};
        constexpr float kNearDepth = 13.0f; // just beyond the renderer's near plane (8)
        const float world = float(1 << mesh.shift);
        const float vw = 240.0f * viewAspect, ox = (vw - 320.0f) * 0.5f;
        for (const od::storm::DepthChain& d : storm.depth) {
            if (d.slot == 0) continue;
            const float c[3] = {d.centre[0] * world, -d.centre[1] * world, d.centre[2] * world};
            float depth = 0.0f;
            for (int q = 0; q < 3; ++q) depth += (c[q] - cam.eye[q]) * f[q];
            if (depth < kNearDepth) depth = kNearDepth;
            for (const od::storm::Sprite& s : d.sprites) {
                if (s.texture < 0 || size_t(s.texture) >= mesh.fxSlices.size()) continue; // slice not built yet
                od::RenderVertex v[4]{};
                for (int k = 0; k < 4; ++k) {
                    const int ix = k & 1, iy = k >> 1;
                    const float nx = (float(s.x + ix * s.w) + ox) / vw * 2.0f - 1.0f;
                    const float ny = 1.0f - float(s.y + iy * s.h) / 120.0f;
                    for (int q = 0; q < 3; ++q)
                        v[k].pos[q] = cam.eye[q] + depth * (f[q] + r[q] * nx * tanHalf * viewAspect + u[q] * ny * tanHalf);
                    v[k].uv[0] = (float(s.u) + float(ix * s.w)) / 256.0f;
                    v[k].uv[1] = (float(s.v) + float(iy * s.h)) / 256.0f;
                    v[k].slice = float(mesh.fxSlices[size_t(s.texture)]);
                    v[k].shade = 1.0f;
                }
                for (int idx : od::kQuadTris) obj.tris.push_back(v[idx]);
            }
        }
        if (!obj.tris.empty()) out.push_back(std::move(obj));
    }
    // the weather overlays (game/weather_draw): textured PS1 quads (additive, tpage abr 1). The blizzard layer is
    // linked at its OT slot +0xc: slot 1 (ST10 / ST11 / ST1D) = in front of everything, drawn over the scene like the
    // storm glow; the back of the OT (ST02 1024, ST03 4095: the sky seen through the airship's windows in the attract)
    // = behind every polygon, a backdrop the room hides (od::weather::overScene). A snow sprite goes into the depth OT
    // at slot z >> 1, z its view depth in game units (the logic moves it by the eye's travel along the view): it is
    // drawn at that depth, behind what is nearer (a wall, a roof's edge). remasterWeather: the quads over the scene
    // are the renderer's precipitation instead
    if (!weather.empty()) {
        od::DynamicObject obj{od::objectId(od::kKindEffect, 0x78000u | 2u), {}, 2};
        od::DynamicObject back{od::objectId(od::kKindEffect, 0x78000u | 3u), {}, 2};
        constexpr float kDepth = 12.5f;       // just behind the storm glow plane
        constexpr float kBackDepth = 120000.0f; // behind the room, in front of the sky dome (140000)
        const float world = float(1 << mesh.shift);
        const float vw = 240.0f * viewAspect, ox = (vw - 320.0f) * 0.5f;
        for (const od::weather::Quad& q : weather) {
            const bool over = od::weather::overScene(q);
            if (over && remasterWeather) continue;
            const float depth = !over ? kBackDepth
                                : q.source == od::RoutineId::SnowParticleDraw ? float(q.ot) * 2.0f * world
                                                                               : kDepth;
            const int t = g_fxTextures.find(q.tpage, q.clut);
            if (t < 0 || size_t(t) >= mesh.fxSlices.size()) continue; // slice not built yet
            // the far texel edge one texel out: the PS1 maps 32 pixels onto u .. u + 31 inclusive
            const int uMax = std::max(q.u[0], q.u[1]), vMax = std::max(q.v[0], q.v[2]);
            od::RenderVertex v[4]{};
            for (int k = 0; k < 4; ++k) {
                const float nx = (float(q.x[k]) + ox) / vw * 2.0f - 1.0f, ny = 1.0f - float(q.y[k]) / 120.0f;
                for (int c = 0; c < 3; ++c) {
                    v[k].pos[c] = cam.eye[c] + depth * (f[c] + r[c] * nx * tanHalf * viewAspect + u[c] * ny * tanHalf);
                    v[k].color[c] = float(q.grey[k]) / 128.0f;
                }
                v[k].uv[0] = float(q.u[k] + (q.u[k] == uMax ? 1 : 0)) / 256.0f;
                v[k].uv[1] = float(q.v[k] + (q.v[k] == vMax ? 1 : 0)) / 256.0f;
                v[k].slice = float(mesh.fxSlices[size_t(t)]);
                v[k].shade = 1.0f;
            }
            for (int idx : od::kQuadTris) (over ? obj : back).tris.push_back(v[idx]);
        }
        if (!back.tris.empty()) out.push_back(std::move(back));
        if (!obj.tris.empty()) out.push_back(std::move(obj));
    }
    return out;
}

std::vector<od::RenderVertex> PlayerView::build(const RoomMesh& mesh) {
    std::vector<od::RenderVertex> out;
    if (!ok || mesh.player.parts.empty()) return out;
    od::port::Player& pl = od::port::player();
    // FUN_005f3290: drawn when (byte 0 & byte 1 & 2) -- the cutscene steps hide / show the player through byte 0
    // (script 5: 0xa71d20 & 0xfd)
    if (play && !(pl.flags & pl.runFlags & od::port::kActorDrawn)) return out;
    if (!play && !starting) { // animation viewer: place and pose directly (in play mode Maybe_UpdatePlayer posed it)
    pl.pos.x = int32_t(mesh.playerSpawn[0]);
    pl.pos.y = int32_t(mesh.playerSpawn[1]);
    pl.pos.z = int32_t(mesh.playerSpawn[2]);
    od::port::stage().coordinateShift = uint8_t(mesh.shift);
    pl.body.anim = 0;                   // animation id < 0x60: player table
    pl.body.frameData = uint8_t(anim);
    pl.upper.anim = 0;
    pl.upper.frameData = uint8_t(anim);
    frameKey[0] = uint8_t(frame); // the viewer's key (kFrameByte): the frame shown
    pl.body.keyAt.pointAt(kFrameByte);
    pl.upper.keyAt.pointAt(kFrameByte);
    od::port::playerPose(pl);
    }
    return buildCharacter(mesh.shift, mesh.player, pl);
}

} // namespace od::app
