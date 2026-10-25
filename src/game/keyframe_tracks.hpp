// The keyframe tracks of the cutscenes (exe 0x5e33a0 .. 0x5e3660): a pair of tracks of byte keys that the scripts and
// the cutscene classes key on their timeline -- the player's face (the script block's face tracks: the eyes and the
// mouth, game/script_state) or a cutscene actor's two texture cells (its face, the texture-page words +0xd8 / +0xdc).
// A track plays one sequence of its table: each key shows its value for some ticks, then the track stops, loops, jumps
// or moves to the next key.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port {

struct AnimatedActor;

// What follows a key (its end byte; any other value: the next key)
enum class KeyframeEnd : int8_t {
    Stop = 1, // the track stops on this key
    Loop = 2, // back to the sequence's first key
    Jump = 4, // the key moved by the key's jump
};

// A key of a sequence (exe data, 4 bytes)
struct Keyframe : DataRecord {
    static constexpr uint32_t kBytes = 4;
    int8_t value = 0; // the face / the texture cell shown
    int8_t ticks = 0; // the ticks it shows
    int8_t end = 0;   // KeyframeEnd
    int8_t jump = 0;  // the key offset of KeyframeEnd::Jump
    template <class R> static Keyframe load(const R& r) { return {{}, r.s8(0), r.s8(1), r.s8(2), r.s8(3)}; }
};
// A sequence: its keys (4 bytes each). A track's table: a pointer per sequence to its first key (exe data)

// One track (8 bytes): its table, the sequence and the key played, the tick in the key, stopped
struct KeyframeTrack {
    DataPtr<> table; // the sequences' table (null: no table, the track does not play)
    int8_t sequence = 0;
    int8_t key = 0;
    int8_t tick = 0;
    uint8_t stopped = 0;

    // exe 0x5e33d0 / 0x5e33f0's track: sequence seq from key k (the dword store clears the tick and the stop first)
    void select(uint8_t seq, uint8_t k) {
        clearCursorWord();
        sequence = int8_t(seq);
        key = int8_t(k);
    }
    // the track stopped where it is (the cursor dword cleared, then the stop byte; exe 0x5e35f0 .. 0x5e3660)
    void stop() {
        clearCursorWord();
        stopped = 1;
    }
    // the cursor cleared (the sequence, the key, the tick and the stop byte), as a script start's clear of the block
    void clearCursorWord() {
        sequence = 0;
        key = 0;
        tick = 0;
        stopped = 0;
    }
    // the key the track is on
    Keyframe currentKey() const;
};

// The two tracks of a pair: the face's eyes and mouth (the player's face values / a cast actor's two texture cells)
enum class FaceTrack : uint8_t { Eyes = 0, Mouth = 1 };

// The pair of tracks of a block (16 bytes): track 0 the eyes / the first texture cell, track 1 the mouth / the second
struct KeyframeTracks {
    KeyframeTrack first;
    KeyframeTrack second;

    KeyframeTrack& track(FaceTrack t) { return t == FaceTrack::Eyes ? first : second; }

    // exe 0x5e33a0(tracks, table 0, table 1): both tracks on their tables, stopped
    void init(uint32_t firstTable, uint32_t secondTable);
    // the four dwords cleared (a script start's clear of the face tracks)
    void clearWords();
};

// One tick of a track (the step every track player shares): apply(the current key), the tick counted, and at the end of
// the key's ticks what its end byte says (the tick restarts)
template <class Apply> void keyframeTrackTick(KeyframeTrack& track, Apply apply) {
    if (track.stopped != 0 || !track.table) return;
    const Keyframe key = track.currentKey();
    apply(key);
    const int8_t tick = int8_t(track.tick + 1);
    track.tick = tick;
    if (key.ticks > tick) return;
    switch (KeyframeEnd(key.end)) {
    case KeyframeEnd::Stop: track.stopped = 1; break;
    case KeyframeEnd::Loop: track.key = 0; break;
    case KeyframeEnd::Jump: track.key = int8_t(track.key + key.jump); break;
    default: track.key = int8_t(track.key + 1); break;
    }
    track.tick = 0;
}

// exe 0x5e3410: one tick of the player's face tracks (the eyes and the mouth take their keys' values)
void faceTracksTick(KeyframeTracks& tracks);
// exe 0x5e35f0 / 0x5e3610(tracks, v): the eyes' / the mouth's track stopped and the player's eyes / mouth set to v
void faceTrackStop(KeyframeTracks& tracks, FaceTrack track, uint8_t value);
// exe 0x5e34f0(actor, tracks): one tick of an actor's texture tracks (its two texture-page words on their keys' cells)
void textureTracksTick(AnimatedActor& actor, KeyframeTracks& tracks);
// exe 0x5e3630 / 0x5e3660(actor, tracks, n): the eyes' / the mouth's texture track stopped and the actor's first /
// second texture-page word on cell n
void textureTrackStop(AnimatedActor& actor, KeyframeTracks& tracks, FaceTrack track, uint32_t cell);


} // namespace od::port
