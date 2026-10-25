#include "app/room_uv_anim.hpp"

#include "game/loaded_data.hpp"
#include "game/room_uv_animation.hpp"
#include "game/room_grid.hpp"
#include "game/stage_state.hpp"


namespace od {

namespace {

constexpr uint8_t kSeg = 4;

// the face's UV pair as the room block stores it (parseRoom: corner k = u | v << 8, corners 0 / 1 in w0, 2 / 3 in w1)
void packUvs(const RoomQuad& q, uint32_t& w0, uint32_t& w1) {
    auto c = [&](int k) { return uint32_t(q.uvs[size_t(k)][0]) | uint32_t(q.uvs[size_t(k)][1]) << 8; };
    w0 = c(0) | c(1) << 16;
    w1 = c(2) | c(3) << 16;
}

// a UV pair -> the 6 vertex UVs of loadRoom's triangle split kQuadTris, texel centres on the 256-wide page
void cornerUvs(uint32_t w0, uint32_t w1, float out[6][2]) {
    const uint16_t uv16[4] = {uint16_t(w0 & 0xFFFF), uint16_t(w0 >> 16), uint16_t(w1 & 0xFFFF), uint16_t(w1 >> 16)};
    for (int i = 0; i < 6; ++i) {
        const uint16_t c = uv16[kQuadTris[i]];
        out[i][0] = (float(c & 0xFF) + 0.5f) / 256.0f;
        out[i][1] = (float(c >> 8) + 0.5f) / 256.0f;
    }
}

} // namespace

void RoomUvAnim::addStatic(const RoomQuad& q, int bucket, uint32_t first) {
    if (q.uvOfs == 0 || bucket < 0 || bucket > 3) return;
    Ref r{first, uint8_t(bucket), 0, 0};
    packUvs(q, r.w0, r.w1);
    refs_.emplace(q.uvOfs, r);
}

void RoomUvAnim::addSeg(const RoomQuad& q, uint32_t segQuad) {
    if (q.uvOfs == 0) return;
    Ref r{segQuad, kSeg, 0, 0};
    packUvs(q, r.w0, r.w1);
    refs_.emplace(q.uvOfs, r);
}

void RoomUvAnim::finalize(const std::array<uint32_t, 4>& semiStart) {
    if (finalized_) return;
    for (auto& [ofs, r] : refs_)
        if (r.bucket >= 1 && r.bucket <= 3) {
            r.first += semiStart[size_t(r.bucket - 1)];
            r.bucket = 0;
        }
    finalized_ = true;
}

void RoomUvAnim::remapStatic(const std::unordered_map<uint32_t, uint32_t>& firstOf) {
    for (auto it = refs_.begin(); it != refs_.end();) {
        if (it->second.bucket == kSeg) {
            ++it;
            continue;
        }
        const auto f = firstOf.find(it->second.first);
        if (f == firstOf.end()) {
            it = refs_.erase(it);
            continue;
        }
        it->second.first = f->second;
        ++it;
    }
}

void RoomUvAnim::patch(Ref& r, uint32_t w0, uint32_t w1, std::vector<StaticUvPatch>& patches,
                       const std::function<std::array<RenderVertex, 6>*(uint32_t)>& segVerts) {
    if (r.w0 == w0 && r.w1 == w1) return;
    float uv[6][2];
    cornerUvs(w0, w1, uv);
    if (r.bucket == kSeg) {
        std::array<RenderVertex, 6>* v = segVerts ? segVerts(r.first) : nullptr;
        if (!v) return;
        for (int i = 0; i < 6; ++i) {
            (*v)[size_t(i)].uv[0] = uv[i][0];
            (*v)[size_t(i)].uv[1] = uv[i][1];
        }
    } else {
        for (int i = 0; i < 6; ++i) patches.push_back({r.first + uint32_t(i), {uv[i][0], uv[i][1]}});
    }
    r.w0 = w0;
    r.w1 = w1;
}

void RoomUvAnim::apply(bool bound, uint32_t blockBase, Renderer& renderer,
                       const std::function<std::array<RenderVertex, 6>*(uint32_t)>& segVerts) {
    const std::vector<port::RoomUvWrite> writes = port::takeRoomUvWrites();
    if (!bound || !finalized_ || refs_.empty()) return;
    // the faces to look at: this tick's writes + the animation list (0x6d00b0, count 0x6d00b4, 0x14-byte entries,
    // +4 = the face's UV pair) -- the list also covers a room rebuilt mid-animation (vertices back at the file's UVs).
    // The UVs are re-read from the room block itself, so a write queued for a previous room cannot leak in.
    std::vector<uint32_t> addrs;
    addrs.reserve(writes.size() + 64);
    for (const port::RoomUvWrite& w : writes) addrs.push_back(w.addr);
    port::StageState& st = port::stage();
    const uint32_t count = st.textureAnimationCount;
    const uint32_t list = count ? st.textureAnimations : 0u;
    for (uint32_t i = 0; list && i < count; ++i) {
        const port::RoomUvAnimation& anim = port::roomUvAnimations().entries[i];
        if (anim.faceTexels != 0) addrs.push_back(anim.faceTexels);
    }
    std::vector<StaticUvPatch> patches;
    for (const uint32_t a : addrs) {
        if (a < blockBase || !od::port::loadedFileMapped(a, 8)) continue;
        const auto texels = od::port::loadedFile(a, 8);
        const auto [lo, hi] = refs_.equal_range(a - blockBase);
        if (lo == hi) continue;
        const uint32_t w0 = texels.read<uint32_t>(0), w1 = texels.read<uint32_t>(4);
        for (auto it = lo; it != hi; ++it) patch(it->second, w0, w1, patches, segVerts);
    }
    if (!patches.empty()) renderer.updateStaticUvs(patches);
}

} // namespace od
