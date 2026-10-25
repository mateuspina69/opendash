// The keyframe tracks of the cutscenes (see keyframe_tracks.hpp).
#include "game/actor_state.hpp"
#include "game/keyframe_tracks.hpp"

#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/player/player_state.hpp"

namespace od::port {

namespace {

// the texture-page word of the actor that texture track `track` drives
uint32_t& textureWordOf(AnimatedActor& actor, FaceTrack track) {
    return track == FaceTrack::Eyes ? actor.texturePage : actor.secondTexturePage;
}

} // namespace

Keyframe KeyframeTrack::currentKey() const {
    const uint32_t keys = dataScalar<uint32_t>(table.value() + uint32_t(int32_t(sequence)) * 4);
    return dataRecord<Keyframe>(keys + uint32_t(int32_t(key)) * Keyframe::kBytes, Keyframe::kBytes);
}

void KeyframeTracks::init(uint32_t firstTable, uint32_t secondTable) {
    first.table.pointAt(firstTable);
    first.clearCursorWord();
    second.clearCursorWord();
    second.table.pointAt(secondTable);
    first.stopped = 1;
    second.stopped = 1;
}

void KeyframeTracks::clearWords() {
    first.table = nullptr;
    first.clearCursorWord();
    second.table = nullptr;
    second.clearCursorWord();
}

void faceTracksTick(KeyframeTracks& tracks) {
    Player& p = player();
    keyframeTrackTick(tracks.first, [&](const Keyframe& key) { p.idleFaces[size_t(FaceTrack::Eyes)] = key.value; });
    keyframeTrackTick(tracks.second, [&](const Keyframe& key) { p.idleFaces[size_t(FaceTrack::Mouth)] = key.value; });
}

void faceTrackStop(KeyframeTracks& tracks, FaceTrack track, uint8_t value) {
    tracks.track(track).stop();
    player().idleFaces[size_t(track)] = int8_t(value);
}

void textureTracksTick(AnimatedActor& actor, KeyframeTracks& tracks) {
    keyframeTrackTick(tracks.first, [&](const Keyframe& key) {
        setTexturePageCell(actor.texturePage, uint32_t(int32_t(key.value)));
    });
    keyframeTrackTick(tracks.second, [&](const Keyframe& key) {
        setTexturePageCell(actor.secondTexturePage, uint32_t(int32_t(key.value)));
    });
}

void textureTrackStop(AnimatedActor& actor, KeyframeTracks& tracks, FaceTrack track, uint32_t cell) {
    tracks.track(track).stop();
    setTexturePageCell(textureWordOf(actor, track), cell);
}


} // namespace od::port
