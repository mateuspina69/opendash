// Native animation keys and skeleton records from loaded file blocks.
#pragma once
#include "game/loaded_data.hpp"

namespace od::port {

inline LoadedVector skeletonOffset(const FileDataReader& skeleton, size_t bone) {
    return FileDataRef<LoadedVector>(skeleton.sub(bone * 6)).get();
}
struct AnimationKeyData : DataRecord {
    int8_t frame;
    uint8_t ticks, events, flags;
    uint32_t at = 0; // compatibility token while the actor's script cursor is image state
    template <class R> static AnimationKeyData load(const R& r) {
        return {{}, r.template read<int8_t>(0), r.template read<uint8_t>(1), r.template read<uint8_t>(2),
                r.template read<uint8_t>(3)};
    }
};
inline AnimationKeyData animationKey(uint32_t pointer) {
    auto key = dataRecord<AnimationKeyData>(pointer, 4); // a loaded file's, or a key the game builds (the viewer's)
    key.at = pointer;
    return key;
}
template <class Key> void copyAnimationKey(Key& out, const AnimationKeyData& key) {
    out.frame = key.frame;
    out.ticks = key.ticks;
    out.events = key.events;
    out.flags = key.flags;
}


} // namespace od::port
