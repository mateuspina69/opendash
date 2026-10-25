// The player's animation (see player_animation.hpp).
#include "game/player/player_animation.hpp"
#include "game/animation_data.hpp"

#include "game/fixed_point.hpp"
#include "game/player/player_pose.hpp"
#include "game/player/player_state.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"

namespace od::port {

namespace {

// The player's animation banks: the scripts' offsets (bytes from the bank's base, & ~3) by animation id; each script is
// preceded by a dword whose low byte is its frame-data index. The player's own bank is PL00P000.DAT's segment 0 (PS1
// 0x110800); a stage's (ids 0x80..) is its record at +0x298, whose table follows at the record's +4 offset.
constexpr uint32_t kPlayerBank = 0x740800;
constexpr uint32_t kPlayerScripts = 0x753000;  // ids 0x00..0x5f
constexpr uint32_t kSpecialArmScripts = 0x7546b0; // ids 0x60..0x6f
constexpr uint32_t kBusterArmScripts = 0x754600;  // ids 0x70..0x7f
constexpr uint32_t kKeySize = 4;
constexpr uint8_t kKeyIndexMask = 0x7f;

// The idle-variation tables (exe 0x8c4ad4 / 0x8c4ae4): the first key of each table, the keys' addresses as the idle
// tracks store them (the address is the record's identity)
const DataTable<ScalarTable<uint32_t, 4>> idleTablesA{TableId::PlayerIdleTablesA};
const DataTable<ScalarTable<uint32_t, 4>> idleTablesB{TableId::PlayerIdleTablesB};

// the script of animation `id`: its first key's address (and the frame-data byte before it)
uint32_t animScript(const Player& p, uint32_t id) {
    uint32_t base = kPlayerBank, table;
    if (id < kSpecialArmAnims) {
        table = kPlayerScripts;
    } else if (id < kBusterArmAnims) {
        id -= kSpecialArmAnims;
        table = kSpecialArmScripts;
    } else if (id < kStageAnims) {
        id -= kBusterArmAnims;
        table = kBusterArmScripts;
    } else {
        base = p.stageAnims;
        id -= kStageAnims;
        table = loadedScalar<uint32_t>(base + 4) + base;
    }
    return base + (loadedScalar<uint32_t>(table + id * 4) & ~3u) + kKeySize;
}

// the shared tail of a key with a blend event (flags & kKeyBlendMask == kKeyBlend): the blend toward the next different
// frame of the script
void startBlend(AnimBlend& blend, uint32_t key, uint8_t flags) {
    blend.steps = uint8_t((flags & 0xf) | 0x80);
    const int8_t frame = animationKey(key).frame;
    do key += kKeySize;
    while (frame == animationKey(key).frame);
    blend.targetFrame = animationKey(key).frame;
}

// a channel started on animation id at step (exe 0x5e1a90 / 0x5e1b60's shared part)
void startChannel(Player& p, AnimChannel& ch, AnimBlend& blend, uint8_t id, int step) {
    const uint32_t script = animScript(p, id);
    ch.script.pointAt(script);
    const uint32_t key = script + uint32_t(step) * kKeySize;
    ch.keyAt.pointAt(key);
    ch.frameData = loadedScalar<uint8_t>(script - kKeySize);
    copyAnimationKey(ch.key, animationKey(key));
    ch.keyIndex = uint8_t(step);
    blend.clear();
    const uint8_t flags = animationKey(key).flags;
    if ((flags & kKeyBlendMask) == kKeyBlend) startBlend(blend, key, flags);
}

// exe 0x5e1cc0 / 0x5e1db0's shared part: the key's ticks counted down; at 0: hold (kKeyHold), jump (kKeyJump | key) or
// the next key (with its blend)
void advanceChannel(Player& p, AnimChannel& ch, AnimBlend& blend) {
    if (p.animPaused == 0) ch.key.ticks = uint8_t(ch.key.ticks - 1u);
    if (ch.key.ticks != 0) return;
    const uint8_t flags = animationKey(ch.keyAt.value()).flags;
    if (flags & kKeyJump) {
        if (flags == kKeyHold) {
            ch.key.ticks = animationKey(ch.keyAt.value()).ticks;
            return;
        }
        const uint32_t target = ch.script.value() + (flags & kKeyIndexMask) * kKeySize;
        ch.keyAt.pointAt(target);
        ch.keyIndex = 0;
        copyAnimationKey(ch.key, animationKey(target));
        return;
    }
    const uint32_t next = ch.keyAt.value() + kKeySize;
    ch.keyAt.pointAt(next);
    copyAnimationKey(ch.key, animationKey(next));
    ch.keyIndex = uint8_t(ch.keyIndex + 1u);
    const uint8_t nextFlags = animationKey(next).flags;
    if ((nextFlags & kKeyBlendMask) == kKeyBlend) startBlend(blend, next, nextFlags);
}

// the walk's frames and the run's: the run step = (the walk step - 4) / 2, the walk step = the run step * 2 + 4 (both
// loop over kWalkLoopFrames)
constexpr int kWalkPhaseOffset = 4;
constexpr int kWalkLoopFrames = 0x1c;
constexpr int kWalkLastStep = 0x1f;

} // namespace

void requestAnimId(Player& p, int8_t id, int16_t step, bool force) {
    const int8_t playing = int8_t(p.body.prevAnim);
    if (!force) {
        if (id == playing) return;
    } else {
        p.body.prevAnim = uint8_t(PlayerAnim::None);
    }
    if (id == int8_t(PlayerAnim::Run)) {
        if (int8_t(p.body.prevAnim) == int8_t(PlayerAnim::Walk)) {
            int16_t s = wrap16(p.body.key.frame - kWalkPhaseOffset);
            if (s < 0) s = wrap16(p.body.key.frame + kWalkLoopFrames);
            step = int16_t(s >> 1);
        }
    } else if (id == int8_t(PlayerAnim::Walk) && int8_t(p.body.prevAnim) == int8_t(PlayerAnim::Run)) {
        const int16_t s = wrap16(p.body.key.frame * 2);
        step = wrap16(s + kWalkPhaseOffset);
        if (step > kWalkLastStep) {
            p.body.anim = uint8_t(PlayerAnim::Walk);
            p.body.key.frame = int8_t(int8_t(s) - kWalkLoopFrames);
            return;
        }
    }
    p.body.anim = uint8_t(id);
    p.body.key.frame = int8_t(step);
}

void requestAnim(Player& p, PlayerAnim id, int16_t step, bool force) { requestAnimId(p, int8_t(id), step, force); }

void startBodyAnimation(Player& p, PlayerAnim id, int step) {
    p.body.anim = uint8_t(id);
    startChannel(p, p.body, p.bodyBlend, uint8_t(id), step);
    playerPose(p, false); // exe 0x5e1fe0 (no blend) + 0x5e25b0
}

void startUpperAnimation(Player& p, uint8_t mixMode, PlayerAnim id, int step) {
    p.upper.anim = uint8_t(id);
    p.channelMix = int8_t(mixMode);
    startChannel(p, p.upper, p.upperBlend, uint8_t(id), step);
}

void advanceBodyAnimation(Player& p) {
    p.bodyBlend.clear();
    advanceChannel(p, p.body, p.bodyBlend);
    playerPose(p, false); // exe 0x5e1fe0 (no blend) + 0x5e25b0
}

void advanceUpperAnimation(Player& p, uint8_t mixMode) {
    p.channelMix = int8_t(mixMode);
    p.upperBlend.clear();
    advanceChannel(p, p.upper, p.upperBlend);
}

void stepIdleTracks(Player& p) {
    if (scriptState().inUse()) return;
    for (size_t k = 0; k < 2; ++k) {
        auto&& track = p.idleTracks[k];
        uint32_t key = track.key.value();
        if (uint8_t(dataRecord<IdleKey>(key, IdleKey::kBytes).next) != 0xff) {
            const int8_t count = int8_t(track.count + 1);
            track.count = count;
            if (track.ticks < count) {
                const int8_t next = dataRecord<IdleKey>(key, IdleKey::kBytes).next;
                if (next < 0)
                    track.key.pointAt(track.start.value() + (uint8_t(next) & kKeyIndexMask) * kKeySize);
                else
                    track.key.pointAt(key + kKeySize);
                const uint32_t r = rngStep();
                key = track.key.value();
                const IdleKey k2 = dataRecord<IdleKey>(key, IdleKey::kBytes);
                const uint8_t extra = k2.ticksMask & uint8_t(r);
                const uint8_t base = k2.baseTicks;
                track.count = 0;
                track.ticks = int8_t(extra + base);
            }
        }
        p.idleFaces[k] = int8_t(dataRecord<IdleKey>(key, IdleKey::kBytes).value);
    }
}

void selectIdleTables(Player& p, uint8_t a, uint8_t b) {
    for (size_t k = 0; k < 2; ++k) {
        const uint8_t v = k ? b : a;
        auto&& track = p.idleTracks[k];
        if (int(track.table) == int(v)) continue;
        track.table = int8_t(v);
        const uint32_t first = k ? idleTablesB[v] : idleTablesA[v];
        track.key.pointAt(first);
        track.start.pointAt(first);
        const uint32_t r = rngStep();
        const uint8_t extra = track.key->ticksMask & uint8_t(r);
        const uint8_t base = track.key->baseTicks;
        track.count = 0;
        track.ticks = int8_t(extra + base);
    }
}

} // namespace od::port
