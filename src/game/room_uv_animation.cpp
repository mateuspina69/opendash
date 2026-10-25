// The room's texture animations (see room_uv_animation.hpp).
#include "game/actor_state.hpp"
#include "game/room_uv_animation.hpp"

#include "engine/data_source.hpp"
#include "engine/room.hpp"
#include "game/actor_pools.hpp"
#include "game/loaded_data.hpp"
#include "game/projection_state.hpp"
#include "game/room_grid.hpp"
#include "game/stage_state.hpp"

namespace od::port {

namespace {

// One record of the stage's table (8 bytes after its 4-byte header)
struct UvAnimationRecord {
    explicit UvAnimationRecord(uint32_t pointer) : data(dataAt(pointer, 8)) {}
    DataReader data;
    uint8_t poolEntry{data.read<uint8_t>(0x0)};    // the segment pool entry whose texture list holds the face
    uint8_t listsToSkip{data.read<uint8_t>(0x1)};  // the face lists before the face's
    uint16_t facesToSkip{data.read<uint16_t>(0x2)}; // the faces before it in its list
    uint8_t flags{data.read<uint8_t>(0x4)};        // RoomUvAnimation::flags
    uint8_t frameCount{data.read<uint8_t>(0x5)};
    uint16_t steps{data.read<uint16_t>(0x6)};       // the frame steps' dword offset in the table
};
struct UvAnimationTable {
    explicit UvAnimationTable(uint32_t pointer) : data(dataAt(pointer, 1)) {}
    DataReader data;
    uint8_t count{data.read<uint8_t>(0x0)};
};
constexpr uint32_t kUvTableHeaderSize = 4, kUvRecordSize = 8;
// One frame step (u16 a frame): the frame shown and its ticks
struct UvFrameStep {
    explicit UvFrameStep(uint32_t pointer) : data(dataAt(pointer, 2)) {}
    DataReader data;
    uint8_t frame{data.read<uint8_t>(0x0)};
    uint8_t ticks{data.read<uint8_t>(0x1)};
};
constexpr uint32_t kUvFrameStepSize = 2;
// One frame: the texels of two corners (u | v << 8 each), stored to the corners its order names
struct UvFrame {
    explicit UvFrame(uint32_t pointer) : data(dataAt(pointer, 4)) {}
    DataReader data;
    uint32_t texels{data.read<uint32_t>(0x0)};
};
constexpr uint32_t kUvFrameSize = 4;
// exe data 0x91bdcc: the corner orders (four corner indices each, as a word), by the flags' order -- the floor's
// orientation table (engine/room FloorOrientations), one record for every reader of it
const DataTable<FloorOrientations> floorOrientations{TableId::FloorOrientations};
uint32_t uvCornerOrder(uint32_t order) {
    const auto& c = floorOrientations->orient[order];
    return uint32_t(c[0]) | uint32_t(c[1]) << 8 | uint32_t(c[2]) << 16 | uint32_t(c[3]) << 24;
}

// The face walk of the room draw (exe globals 0x6d018c..; the setup walks the face lists with them): the vertex cursor
// (a run of faces shares its first face's four vertices, then two more a face: 8 bytes), the texel and shade cursors
struct RoomFaceWalk {
    uint32_t vertices = 0;
    uint32_t texels = 0;
    uint32_t shades = 0;
};
RoomFaceWalk& faceWalk() {
    static RoomFaceWalk w;
    return w;
}
// a run's first face's vertices: the faces after it that share them (the top byte of its third vertex word)
struct RunVertices {
    explicit RunVertices(uint32_t pointer) : data(dataAt(pointer, 0xc)) {}
    DataReader data;
    uint8_t sharedFaces{data.read<uint8_t>(0xb)};
};
constexpr uint32_t kRunFirstVertices = 0x10, kRunNextVertices = 8, kFaceTexelsSize = 8, kFaceShadeSize = 4;

enum class RoomUvAnimationState : uint8_t { Start = 0, Run = 1 }; // exe 0x418370 / 0x418390

std::vector<RoomUvWrite> uvWrites;
constexpr size_t kUvWritesKept = 4096; // the side channel's bound between two host reads

// the walk at the start of face list `list`
void startFaceList(uint32_t pool, const FaceList& list) {
    RoomFaceWalk& w = faceWalk();
    w.vertices = pool + uint32_t(list.vertices) * 4;
    w.texels = pool + uint32_t(list.texels) * 4;
    w.shades = pool + uint32_t(list.shades) * 4;
}
// the walk past n faces
void skipFaces(uint32_t n) {
    RoomFaceWalk& w = faceWalk();
    uint32_t run = 0;
    for (; n != 0; --n) {
        if (run != 0) {
            --run;
            w.vertices += kRunNextVertices;
        } else {
            run = RunVertices(w.vertices).sharedFaces;
            w.vertices += kRunFirstVertices;
        }
        w.texels += kFaceTexelsSize;
        w.shades += kFaceShadeSize;
    }
}

// frame `frame` of the entry written into its face: its two texels to the corners of its order, the other two copied
// by the flips (through the projections' quad scratch, as the exe does)
void writeUvFrame(RoomUvAnimation& e, uint32_t frame) {
    ProjectionScratch& scratch = projectionScratch();
    scratch.frameTexels.set(UvFrame(e.frames + frame * kUvFrameSize).texels);
    scratch.frameCorners.set(uvCornerOrder(e.flags & kUvCornerOrderMask));
    QuadTexels& t = scratch.quadTexels;
    t.setCorner(scratch.frameCorners.corners[0], scratch.frameTexels.corners[0]);
    t.setCorner(scratch.frameCorners.corners[3], scratch.frameTexels.corners[1]);
    auto&& b = t.uvBytes;
    const uint8_t flags = e.flags;
    if (flags & kUvFlipOrder) {
        if (flags & kUvFlipSide) {
            b[2] = b[0];
            b[3] = b[7];
            b[4] = b[6];
            b[5] = b[1];
        } else {
            b[0] = b[4];
            b[1] = b[3];
            b[6] = b[2];
            b[7] = b[5];
        }
    } else if (flags & kUvFlipSide) {
        b[0] = b[2];
        b[1] = b[5];
        b[6] = b[4];
        b[7] = b[3];
    } else {
        b[2] = b[6];
        b[3] = b[1];
        b[4] = b[0];
        b[5] = b[7];
    }
    storeLoaded(e.faceTexels, t.word(0)); // the face's texels in the room block
    storeLoaded(e.faceTexels + 4, t.word(1));
}

// exe 0x4183d0: one entry's tick -- out of ticks, the next frame (its ticks) written into the face
void stepUvAnimation(RoomUvAnimation& e) {
    if (e.ticksLeft == 0) {
        const uint32_t current = e.frame;
        const uint32_t next = current == uint32_t(e.frameCount) - 1u ? 0u : current + 1u;
        e.frame = uint8_t(next);
        const UvFrameStep step(e.steps + (next & 0xff) * kUvFrameStepSize);
        e.ticksLeft = step.ticks;
        writeUvFrame(e, step.frame);
        const QuadTexels& t = projectionScratch().quadTexels;
        if (uvWrites.size() < kUvWritesKept) uvWrites.push_back({e.faceTexels, t.word(0), t.word(1)});
    }
    e.ticksLeft -= 1;
}

} // namespace

RoomUvAnimations& roomUvAnimations() {
    static RoomUvAnimations s;
    return s;
}

void showRoomUvFirstFrame(uint32_t k) {
    RoomUvAnimation& e = roomUvAnimations().entries[k];
    writeUvFrame(e, UvFrameStep(e.steps).frame);
}

void roomUvAnimationSetup(uint32_t list, uint32_t table) {
    StageState& s = stage();
    s.textureAnimations = list;
    s.textureAnimationCount = UvAnimationTable(table).count;
    if (s.textureAnimationCount == 0) return;
    uint32_t record = table + kUvTableHeaderSize;
    for (uint32_t i = 0;;) {
        const UvAnimationRecord r(record);
        RoomUvAnimation& e = roomUvAnimations().entries[i];
        e.flags = r.flags;
        e.frameCount = r.frameCount;
        const uint32_t steps = table + uint32_t(r.steps) * 4;
        e.steps = steps;
        e.ticksLeft = 0;
        e.frame = 0;
        e.frames = steps + ((uint32_t(e.frameCount) + 1) >> 1) * 4;
        const uint32_t pool = roomGrid().pool;
        const VolumeSet set(pool + uint32_t(segmentPoolEntry(r.poolEntry).volumeSets) * 4);
        const TextureList textures(pool + uint32_t(set.textureList) * 4);
        uint32_t faceList = textures.at + kListHeaderSize + uint32_t(textures.firstWord) * 4;
        for (uint32_t k = r.listsToSkip; k != 0; --k, faceList += kFaceListSize) {
            const FaceList skipped(faceList);
            startFaceList(roomGrid().pool, skipped);
            skipFaces(skipped.faces);
        }
        startFaceList(roomGrid().pool, FaceList(faceList));
        skipFaces(r.facesToSkip);
        e.faceTexels = faceWalk().texels;
        e.faceShades = faceWalk().shades;
        record += kUvRecordSize;
        ++i;
        if (i >= s.textureAnimationCount) return;
    }
}

void roomUvAnimationUpdate(const PlayerEnv&, uint32_t actor) {
    ControllerActor& c = controllerActorAt(actor);
    StageState& s = stage();
    if (RoomUvAnimationState(c.state) == RoomUvAnimationState::Start) {
        if (s.textureAnimationCount == 0) {
            freeActor(c); // exe 0x5e15c0
            return;
        }
        c.state = uint8_t(RoomUvAnimationState::Run);
        return;
    }
    uint32_t i = 0;
    do { // the count is not 0 (the start state checked it)
        RoomUvAnimation& e = roomUvAnimations().entries[i];
        if (!(e.flags & kUvAnimationPaused)) stepUvAnimation(e);
        ++i;
    } while (i < s.textureAnimationCount);
}

std::vector<RoomUvWrite> takeRoomUvWrites() {
    std::vector<RoomUvWrite> out;
    out.swap(uvWrites);
    return out;
}

} // namespace od::port
