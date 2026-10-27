// The debris of a broken wall segment (see wall_debris.hpp). Its pieces are POLY_FT4 packets linked into the ordering
// table (the presentation side channel, game/draw_commands); the geometry unit's scratch the exe writes for them
// (game/projection_state ProjectionScratch) is kept written.
#include "game/actor_state.hpp"
#include "game/wall_debris_state.hpp"
#include "game/wall_debris.hpp"

#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/draw_commands.hpp"
#include "game/fixed_point.hpp"
#include "game/global_class_types.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/projection_state.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"

namespace od::port {

using namespace wall_debris_state;

namespace {

inline constexpr const char* kDebrisArea = "wall debris";

constexpr uint32_t kAllPieces = (1u << kPieces) - 1;
enum class DebrisState : uint8_t { Start = 0, Fly = 1 };

// the texture list's face entries the pieces cycle through (two words each: the face record's and its texels' dword
// offsets in the pool)
struct FaceEntry {
    explicit FaceEntry(uint32_t pointer) : data(loadedFile(pointer)) {}
    FileDataReader data;
    uint16_t face{data.read<uint16_t>(0)};
    uint16_t texels{data.read<uint16_t>(4)};
};
// a face record of the pool: its second word's top byte & kTextureIndexMask selects the list's texture word
struct PoolFace {
    explicit PoolFace(uint32_t pointer) : data(loadedFile(pointer)) {}
    FileDataReader data;
    uint32_t textureWord{data.read<uint32_t>(4)};
};
constexpr uint32_t kTextureIndexShift = 24, kTextureIndexMask = 0x3f;

// the pieces' shapes: four sets (exe data 0x8c46bc: their addresses) of 16 quads (four s8 x / y / z corners)
struct WallPieceShapes : DataRecord {
    static constexpr size_t kSets = 4, kShapes = 16, kCoords = 12;
    static constexpr uint32_t kBytes = kSets * 4;
    std::array<std::array<std::array<int8_t, kCoords>, kShapes>, kSets> sets{};
    static WallPieceShapes load(const TableReader& r) {
        WallPieceShapes t;
        for (size_t s = 0; s < kSets; ++s) {
            const TableReader set = r.follow(s * 4, kShapes * kCoords);
            for (size_t k = 0; k < kShapes; ++k)
                for (size_t c = 0; c < kCoords; ++c) t.sets[s][k][c] = set.s8(k * kCoords + c);
        }
        return t;
    }
};
const DataTable<WallPieceShapes> shapeSets{TableId::WallDebrisShapeSets};

// the speeds' bias by variant (the drop's / the radius'): the debris of a wall thrown out, of a door, of the rest
constexpr int kDropBias[3] = {0x22, 0x10, 0x22}, kRadiusBias[3] = {0x12, 0x10, 0x22};
constexpr int16_t kFloorBelow = 0x80;
constexpr int16_t kBlinkAfter = 0x3e0, kLifeTicks = 0x400;
constexpr int16_t kGravity = 3;
constexpr int32_t kMaxDepth = 0xfff; // the ordering table's last entry
// a piece's angle around the break: its index * (0xcc + 0x20) -- exe 0x565af0 adds its two loop counters (the angle's,
// 0xcc a piece, and the face entries', 0x20 a piece)
constexpr uint32_t kPieceTurn = 0xcc, kPieceEntryStep = 0x20;
constexpr uint32_t kDebrisRgb = 0x808080;

// exe 0x565ed0(actor, a + 0x10): in front of the camera (the view depth / 4 below the draw depth range): the pieces
// (spread by the segment's height / width << the size shift, the speeds by the variant), the texture list; false = too
// far (freed). The camera and the point are loaded for the geometry unit as the exe does.
bool debrisStart(StaticActor& actor, WallDebrisFields& f) {
    ProjectionScratch& scratch = projectionScratch();
    loadProjectionMatrix(cameraTransform().view);      // exe 0x40c3c0
    loadProjectionTranslation(cameraTransform().view); // exe 0x40c3e0
    auto&& point = scratch.quadCorners[0];
    point.x = actor.pos.x.whole;
    point.y = actor.pos.y.whole;
    point.z = actor.pos.z.whole;
    const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const CameraPoint c = cameraPoint(p);
    scratch.storedViewPoint[0] = c.view[0]; // exe 0x40caa0
    scratch.storedViewPoint[1] = c.view[1];
    const int32_t z = c.view[2] >> 2;
    scratch.storedViewPoint[2] = z;
    if (z >= int32_t(stage().depthRange)) return false;
    const SegmentPoolEntry pe(f.poolEntry);
    const uint32_t sh = ((pe.breakFlags() >> kBreakSizeShift) & 3) + 2;
    const int spreadY = int((uint32_t(pe.breakHeight) << sh) & 0x7fff);
    const int spreadR = int((uint32_t(pe.breakWidth) << sh) & 0x7fff);
    const uint32_t pool = roomGrid().pool;
    f.textureList = pool + uint32_t(VolumeSet(pool + uint32_t(pe.volumeSets) * 4).textureList) * 4;
    const uint32_t variant = actor.variant;
    const uint32_t v = variant < 2 ? variant : 2;
    const int dropBias = kDropBias[v], radiusBias = kRadiusBias[v];
    if (spreadY == 0) notPorted(kDebrisArea, "exe 0x565ed0: a segment of zero height (the exe divides by zero)");
    for (uint32_t k = 0; k < kPieces; ++k) {
        auto&& piece = f.pieces[k];
        const int r = int16_t(uint16_t(rngStep()));
        piece.drop = wrap16(r % spreadY - (spreadY >> 1));
        piece.dropSpeed = wrap16((piece.drop >> 5) + int(rngStep() & 0x1f) + dropBias - 0x10);
        piece.radius = int16_t(uint16_t((rngStep() & 0x1f) + uint32_t(spreadR)));
        piece.radiusSpeed = wrap16((piece.radius >> 5) + int(rngStep() & 0x1f) + radiusBias - 0x10);
    }
    f.livePieces = kAllPieces;
    f.ticks = int16_t(rngStep() & 0xf);
    f.floor = int16_t(uint16_t(uint32_t(uint16_t(actor.pos.y.whole)) + uint32_t(kFloorBelow)));
    f.shapePhase = uint8_t(rngStep() & 0xff);
    return true;
}
// exe 0x565af0: each live piece (blinking past kBlinkAfter ticks) a textured quad (its shape by the phase and the ticks:
// four s8 corners) at its place around the break, POLY_FT4 grey with the texture words of the list's face for it; out of
// the ordering table's range or below the floor the piece is dropped; freed after kLifeTicks or with none left
void debrisFly(StaticActor& actor, WallDebrisFields& f) {
    ProjectionScratch& scratch = projectionScratch();
    const TextureList list(f.textureList);
    const uint32_t pool = roomGrid().pool;
    for (uint32_t i = 0; i < kPieces; ++i) {
        const uint32_t bit = 1u << i;
        if (!(f.livePieces & bit)) continue;
        if (f.ticks > kBlinkAfter && (uint8_t(f.ticks) & 1)) continue;
        if (list.faces == 0) notPorted(kDebrisArea, "exe 0x565af0: an empty texture list (the exe divides by zero)");
        const FaceEntry entry(list.wordAddress(uint32_t(list.firstWord) + (i % list.faces) * 2));
        auto&& piece = f.pieces[i];
        const uint32_t ang = (i * kPieceTurn + i * kPieceEntryStep) & kAngleMask;
        const int r = piece.radius;
        const int16_t X = wrap16(((fixedSin(int(ang)) * r) >> 12) + actor.pos.x.whole);
        const int16_t Z = wrap16((-(fixedCos(int(ang)) * r) >> 12) + actor.pos.z.whole);
        const int16_t Y = wrap16(actor.pos.y.whole - piece.drop);
        const auto& shape = shapeSets->sets[(uint32_t(f.shapePhase) + i) & 3][(uint32_t(uint16_t(f.ticks)) + i) & 0xf];
        int16_t v[4][3];
        for (uint32_t j = 0; j < 4; ++j) {
            v[j][0] = wrap16(X + shape[j * 3]);
            v[j][1] = wrap16(Y + shape[j * 3 + 1]);
            v[j][2] = wrap16(Z + shape[j * 3 + 2]);
        }
        for (uint32_t j = 0; j < 4; ++j) {
            auto&& corner = scratch.quadCorners[j];
            corner.x = v[j][0];
            corner.y = v[j][1];
            corner.z = v[j][2];
        }
        int16_t sxy[4][2];
        const int32_t otz = projectQuad(v, sxy);
        for (uint32_t j = 0; j < 4; ++j) scratch.quadScreenPoints[j] = dc::packXY(sxy[j][0], sxy[j][1]);
        scratch.quadDepth = otz;
        if (otz < 1 || otz > kMaxDepth) {
            f.livePieces &= ~bit;
            continue;
        }
        const PoolFace face(pool + uint32_t(entry.face) * 4);
        scratch.quadTexture = list.word((face.textureWord >> kTextureIndexShift) & kTextureIndexMask);
        const LoadedArray<uint32_t> texels(pool + uint32_t(entry.texels) * 4);
        scratch.quadTexels.setWord(0, texels[0]);
        scratch.quadTexels.setWord(1, texels[1]);
        const uint32_t texture = scratch.quadTexture, uv01 = scratch.quadTexels.word(0), uv23 = scratch.quadTexels.word(1);
        dc::TexturedQuad q;
        q.colour = dc::packetColour(dc::kPolyFT4, kDebrisRgb);
        q.clut = uint16_t(texture);
        q.tpage = uint16_t(texture >> 16);
        q.uv[0] = uint16_t(uv01);
        q.uv[1] = uint16_t(uv01 >> 16);
        q.uv[2] = uint16_t(uv23);
        q.uv[3] = uint16_t(uv23 >> 16);
        dc::otQuadFT4(actor.at, q, v);
        piece.drop += uint32_t(uint16_t(piece.dropSpeed));
        piece.radius += uint32_t(uint16_t(piece.radiusSpeed));
        piece.dropSpeed -= uint32_t(kGravity);
        if (Y > f.floor) f.livePieces &= ~bit;
    }
    ++f.ticks;
    if (f.ticks > kLifeTicks || f.livePieces == 0) freeActor(actor);
}

} // namespace

void placeWallDebris(uint32_t slot, const SegmentPoolEntry& pe, int16_t x, int16_t y, int16_t z) {
    StaticActor& debris = staticActorAt(slot);
    debris.type = typeByte(GlobalStaticType::WallDebris);
    debris.flags |= kActorLive;
    debris.variant = uint8_t(pe.reaction & kSegmentReactionMask);
    debris.loopFlags |= kActorLateLoop;
    debris.pos.x.whole = x;
    debris.pos.y.whole = y;
    debris.pos.z.whole = z;
    classState<WallDebrisFields>(debris).poolEntry = pe.at;
}

void wallDebrisUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    WallDebrisFields& f = classState<WallDebrisFields>(actor);
    if (DebrisState(actor.state) == DebrisState::Start) {
        if (!debrisStart(actor, f)) {
            freeActor(actor);
            return;
        }
        actor.enterState(uint8_t(DebrisState::Fly));
    }
    debrisFly(actor, f);
}

} // namespace od::port
