// The stage's files (see stage_files.hpp).
#include "game/stage_files.hpp"

#include "engine/data_source.hpp"

#include "game/dat_names.hpp"
#include "game/loaded_data.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {
const DataTable<ScalarTable<uint16_t, kStageFileIds>> stageGeometryFiles{TableId::StageGeometryFiles};
const DataTable<ScalarTable<uint16_t, kStageFileIds>> stageTextureFiles{TableId::StageTextureFiles};
} // namespace

StageFileTables& stageFileTables() {
    static StageFileTables s = [] {
        StageFileTables t;
        const auto& geometry = *stageGeometryFiles;
        const auto& texture = *stageTextureFiles;
        for (size_t i = 0; i < kStageFileIds; ++i) {
            t.geometry[i] = geometry[i];
            t.texture[i] = texture[i];
        }
        return t;
    }();
    return s;
}

namespace {

// A record of the type-1 bank: its header word (bit 31: the player's stage animations, bit 30: their second record,
// else the end of the animation records; bits 2..29: the record's length in words)
struct BankRecord {
    explicit BankRecord(uint32_t pointer) : header(loadedScalar<uint32_t>(pointer)) {}
    uint32_t header;
};
constexpr uint32_t kBankAnimations = 0x80000000u, kBankSecondAnimations = 0x40000000u;
constexpr uint32_t kBankLengthMask = 0xfffffffu, kBankLengthShift = 2, kBankWord = 4;
constexpr uint32_t kStageAnimMarkerReset = 0xffffffffu;


// The selectors' file ids (indices of the DAT name table; the DAT a comment names is that id's entry) and the story
// values / flags they test, by stage
// ST0D (exe 0x5d5ce0)
constexpr uint8_t kST0DLaterStory = 0xe;
constexpr DatFile kST0DGeometry{"ST0D.DAT"}, kST0DLaterGeometry{"ST0D01.DAT"};
// ST0E (exe 0x5d5d00)
constexpr uint8_t kST0EVariantStory = 0xf;
constexpr uint32_t kST0EFlagA = 0x5a1, kST0EFlagB = 0x5a0;
constexpr DatFile kST0EGeometryA{"ST0E02.DAT"},
                  kST0ETextureA{"ST0E02T.DAT"},
                  kST0EGeometryB{"ST0E01.DAT"},
                  kST0ETextureB{"ST0E01T.DAT"},
                  kST0EGeometry{"ST0E.DAT"},
                  kST0ETexture{"ST0ET.DAT"};
// ST10 / ST11 / ST1D (exe 0x5d5d70 / 0x5d5d90 / 0x5d6010): the texture after the story's first steps
constexpr uint8_t kEarlyStory = 1;
constexpr DatFile kST10Texture{"ST1001T.DAT"}, kST10EarlyTexture{"ST10T.DAT"};
constexpr DatFile kST11Texture{"ST1101T.DAT"}, kST11EarlyTexture{"ST11T.DAT"};
constexpr DatFile kST1DTexture{"ST1D01T.DAT"}, kST1DEarlyTexture{"ST1DT.DAT"};
// ST13 (exe 0x5d5db0)
constexpr uint32_t kST13Flag = 0x5b1;
constexpr uint8_t kST13LaterStory = 4;
constexpr DatFile kST13Geometry{"ST13.DAT"}, kST13EarlyGeometry{"ST1301.DAT"};
// ST17 (exe 0x5d5de0)
constexpr uint8_t kST17LaterStory = 6, kST17Story9 = 9;
constexpr uint32_t kST17LandedFlag = 0x31, kST17Story9Flag = 0x2c;
constexpr DatFile kST17Geometry{"ST17.DAT"},
                  kST1704Geometry{"ST1704.DAT"},
                  kST17Texture{"ST17T.DAT"},
                  kST1704Texture{"ST1704T.DAT"},
                  kST1703Geometry{"ST1703.DAT"},
                  kST1706Texture{"ST1706T.DAT"},
                  kST1703Texture{"ST1703T.DAT"};
// ST18 (exe 0x5d5e50)
constexpr uint8_t kST18LateStory = 9, kST18Story6 = 6;
constexpr uint32_t kST18LateFlag = 0x2c, kST18Story6Flag = 0x25;
constexpr DatFile kST1802Geometry{"ST1802.DAT"},
                  kST1802Texture{"ST1802T.DAT"},
                  kST18Geometry{"ST18.DAT"},
                  kST18Texture{"ST18T.DAT"},
                  kST1804Texture{"ST1804T.DAT"};
// ST19 (exe 0x5d5eb0)
constexpr uint8_t kST19Story6 = 6, kST19Story9 = 9;
constexpr uint32_t kST19DoneFlag = 0x2b, kST19FlagA = 0x25, kST19FlagB = 0x580, kST19FlagC = 0x581;
constexpr uint32_t kST19Story9Flag = 0x2c, kST19Story9DoneFlag = 0x2e;
constexpr DatFile kST19Geometry{"ST19.DAT"},
                  kST19Texture{"ST19T.DAT"},
                  kST19AltGeometry{"ST1902.DAT"},
                  kST19AltTexture{"ST1902T.DAT"};
// ST1A / ST1B (exe 0x5d5f50 / 0x5d5fb0): the same choice, each its stage's files
constexpr uint8_t kST1AStory6 = 6, kST1AStory9 = 9;
constexpr uint32_t kST1AStory6Flag = 0x25, kST1AStory9Flag = 0x2c;
constexpr DatFile kST1AAltGeometry{"ST1A01.DAT"},
                  kST1AGeometry{"ST1A.DAT"},
                  kST1AAltTexture{"ST1A01T.DAT"},
                  kST1ATexture{"ST1AT.DAT"};
constexpr DatFile kST1BAltGeometry{"ST1B01.DAT"}, kST1BGeometry{"ST1B.DAT"}, kST1BAltTexture{"ST1B01T.DAT"},
                  kST1BTexture{"ST1BT.DAT"};
// ST1F (exe 0x5d6030)
constexpr uint8_t kST1FMiddleStory = 8, kST1FLateStory = 0x10;
constexpr uint32_t kST1FFlag = 0x586;
constexpr DatFile kST1FGeometry{"ST1F.DAT"},
                  kST1FTexture{"ST1FT.DAT"},
                  kST1FMiddleGeometry{"ST1F01.DAT"},
                  kST1FMiddleTexture{"ST1F01T.DAT"},
                  kST1FLateGeometry{"ST1F02.DAT"},
                  kST1FLateTexture{"ST1F02T.DAT"};
// ST25 (exe 0x5d6090)
constexpr uint8_t kST25EarlyStory = 3;
constexpr DatFile kST25EarlyGeometry{"ST25.DAT"},
                  kST25Geometry{"ST2502.DAT"},
                  kST25EarlyTexture{"ST25T.DAT"},
                  kST25Texture{"ST2502T.DAT"};
// ST26 / ST27 (exe 0x5d60c0 / 0x5d6100)
constexpr uint32_t kST26ST27DoneFlag = 0x56, kST26Flag = 0x3ed, kST27FlagA = 0x44, kST27FlagB = 0x45;
constexpr DatFile kST26AltGeometry{"ST2601.DAT"},
                  kST26Geometry{"ST26.DAT"},
                  kST27AltGeometry{"ST2701.DAT"},
                  kST27Geometry{"ST27.DAT"};
// ST29 / ST3C (exe 0x5d6140 / 0x5d6280)
constexpr uint8_t kST29LaterStory = 0xd, kST3CLaterStory = 0xd;
constexpr DatFile kST29Geometry{"ST29.DAT"},
                  kST29LaterGeometry{"ST2901.DAT"},
                  kST29Texture{"ST29T.DAT"},
                  kST29LaterTexture{"ST2901T.DAT"};
constexpr DatFile kST3CGeometry{"ST3C.DAT"},
                  kST3CLaterGeometry{"ST3C01.DAT"},
                  kST3CTexture{"ST3CT.DAT"},
                  kST3CLaterTexture{"ST3C01T.DAT"};
// ST30 (exe 0x5d6170)
constexpr uint32_t kST30Flag = 0x23;
constexpr DatFile kST30AltGeometry{"ST3001.DAT"},
                  kST30Geometry{"ST30.DAT"},
                  kST30AltTexture{"ST3001T.DAT"},
                  kST30Texture{"ST30T.DAT"};
// ST33 (exe 0x5d61b0)
constexpr uint32_t kST33Flag = 0x3ed;
constexpr DatFile kST33Geometry{"ST33.DAT"}, kST33AltTexture{"ST3302T.DAT"}, kST33Texture{"ST33T.DAT"};
// ST35 (exe 0x5d61e0): room 7 has its own files; else after the first guardian until the second the other geometry
constexpr uint8_t kST35OwnFilesRoom = 7;
constexpr uint32_t kST35FirstGuardianFlag = 0x94, kST35SecondGuardianFlag = 0x95;
constexpr DatFile kST3502Geometry{"ST3502.DAT"},
                  kST3501Texture{"ST3501T.DAT"},
                  kST3501Geometry{"ST3501.DAT"},
                  kST35Geometry{"ST35.DAT"},
                  kST35Texture{"ST35T.DAT"};
// ST39 (exe 0x5d6250)
constexpr uint8_t kST39LaterStory = 0x11;
constexpr DatFile kST39Geometry{"ST39.DAT"},
                  kST39LaterGeometry{"ST3901.DAT"},
                  kST39Texture{"ST39T.DAT"},
                  kST39LaterTexture{"ST3901T.DAT"};
// ST53 (exe 0x5d62b0)
constexpr uint8_t kST53VariantStory = 0xf;
constexpr uint32_t kST53FlagA = 0x5a1, kST53FlagB = 0x5a0;
constexpr DatFile kST53GeometryA{"ST5302.DAT"},
                  kST53TextureA{"ST5302T.DAT"},
                  kST53GeometryB{"ST5301.DAT"},
                  kST53TextureB{"ST5301T.DAT"},
                  kST53Geometry{"ST53.DAT"},
                  kST53Texture{"ST53T.DAT"};

} // namespace

void datLoadRequest(uint16_t id) {
    DatLoadQueue& q = datLoadQueue();
    for (size_t i = 0; i < q.requests.size(); ++i) {
        auto&& r = q.requests[i];
        if (r.kind == 0) {
            r.kind = kDatLoadFile;
            r.fileId = id;
            break;
        }
    }
    fileLoads().loading = kFileLoading;
}

void stageBankWalk() {
    StageState& st = stage();
    Player& p = player();
    uint32_t at = st.bankBuffer == 0 ? kBankBufferA : kBankBufferB;
    for (;;) {
        const uint32_t h = BankRecord(at).header;
        if (!(h & kBankAnimations)) {
            if (!(h & kBankSecondAnimations)) break;
            p.stageAnimsExtra = at + kBankWord;
        } else {
            p.stageAnims = at;
        }
        at += ((h >> kBankLengthShift) & kBankLengthMask) * kBankWord;
    }
    st.modelBank = at;
    p.stageAnimMarker = kStageAnimMarkerReset;
    p.handShapes[1] = 0;
    p.handShapes[0] = 0;
}

void switchStageBank() {
    stage().bankBuffer ^= 1u;
    stageBankWalk();
}

// OpenDash's bound on the records walked (the exe walks until the marker); past it the walk finds no bank
constexpr int kStatusBankWalkLimit = 0x10000;

uint32_t statusBankWalk() {
    Player& p = player();
    uint32_t at = stage().bankBuffer == 0 ? kBankBufferA : kBankBufferB;
    for (int n = 0; n < kStatusBankWalkLimit; ++n) {
        if (!loadedFileMapped(at, 4)) return 0;
        const BankRecord record(at);
        const uint32_t h = record.header;
        if (h & kBankAnimations) {
            p.stageAnims = at;
        } else if (h & kBankSecondAnimations) {
            p.stageAnimsExtra = at + kBankWord;
        } else {
            p.stageAnimMarker = kStageAnimMarkerReset;
            p.handShapes[1] = 0;
            p.handShapes[0] = 0;
            return at;
        }
        const uint32_t step = ((h >> kBankLengthShift) & kBankLengthMask) * kBankWord;
        if (step == 0) return 0;
        at += step;
    }
    return 0;
}

bool stageDatFiles(int stage, std::string& geometry, std::string& texture) {
    StageFileTables& t = stageFileTables();
    auto&& geo = t.geometry[size_t(stage)];
    auto&& tex = t.texture[size_t(stage)];
    const uint8_t story = gameState().story;
    switch (stage) {
    case 0x17:
        if (story <= kST17LaterStory) {
            const bool landed = storyFlag(kST17LandedFlag);
            geo = landed ? kST17Geometry : kST1704Geometry;
            tex = landed ? kST17Texture : kST1704Texture;
        } else {
            geo = kST1703Geometry;
            tex = story == kST17Story9 && storyFlag(kST17Story9Flag) ? kST1706Texture : kST1703Texture;
        }
        break;
    case 0x18:
        if (story >= kST18LateStory && storyFlag(kST18LateFlag)) {
            geo = kST1802Geometry;
            tex = kST1802Texture;
        } else {
            geo = kST18Geometry;
            tex = storyFlag(story == kST18Story6 ? kST18Story6Flag : kST18LateFlag) ? kST18Texture : kST1804Texture;
        }
        break;
    case 0x0d: geo = story > kST0DLaterStory ? kST0DLaterGeometry : kST0DGeometry; break;
    case 0x0e:
        if (story == kST0EVariantStory && storyFlag(kST0EFlagA)) {
            geo = kST0EGeometryA;
            tex = kST0ETextureA;
        } else if (story == kST0EVariantStory && storyFlag(kST0EFlagB)) {
            geo = kST0EGeometryB;
            tex = kST0ETextureB;
        } else {
            geo = kST0EGeometry;
            tex = kST0ETexture;
        }
        break;
    case 0x10: tex = story > kEarlyStory ? kST10Texture : kST10EarlyTexture; break;
    case 0x11: tex = story > kEarlyStory ? kST11Texture : kST11EarlyTexture; break;
    case 0x13: geo = storyFlag(kST13Flag) || story > kST13LaterStory ? kST13Geometry : kST13EarlyGeometry; break;
    case 0x19:
        geo = kST19Geometry;
        tex = kST19Texture;
        if ((story == kST19Story6 && !storyFlag(kST19DoneFlag) &&
             (storyFlag(kST19FlagA) || (storyFlag(kST19FlagB) && storyFlag(kST19FlagC)))) ||
            (story == kST19Story9 && storyFlag(kST19Story9Flag) && !storyFlag(kST19Story9DoneFlag))) {
            geo = kST19AltGeometry;
            tex = kST19AltTexture;
        }
        break;
    case 0x1a:
    case 0x1b: {
        const bool alt = (story == kST1AStory6 && storyFlag(kST1AStory6Flag)) ||
                         (story == kST1AStory9 && storyFlag(kST1AStory9Flag));
        if (stage == 0x1a) {
            geo = alt ? kST1AAltGeometry : kST1AGeometry;
            tex = alt ? kST1AAltTexture : kST1ATexture;
        } else {
            geo = alt ? kST1BAltGeometry : kST1BGeometry;
            tex = alt ? kST1BAltTexture : kST1BTexture;
        }
        break;
    }
    case 0x1d: tex = story > kEarlyStory ? kST1DTexture : kST1DEarlyTexture; break;
    case 0x1f:
        if (story < kST1FMiddleStory || (story == kST1FMiddleStory && !storyFlag(kST1FFlag))) {
            geo = kST1FGeometry;
            tex = kST1FTexture;
        } else if (story <= kST1FLateStory) {
            geo = kST1FMiddleGeometry;
            tex = kST1FMiddleTexture;
        } else {
            geo = kST1FLateGeometry;
            tex = kST1FLateTexture;
        }
        break;
    case 0x25:
        geo = story <= kST25EarlyStory ? kST25EarlyGeometry : kST25Geometry;
        tex = story <= kST25EarlyStory ? kST25EarlyTexture : kST25Texture;
        break;
    case 0x26: geo = !storyFlag(kST26ST27DoneFlag) && storyFlag(kST26Flag) ? kST26AltGeometry : kST26Geometry; break;
    case 0x27:
        geo = !storyFlag(kST26ST27DoneFlag) && storyFlag(kST27FlagA) && storyFlag(kST27FlagB) ? kST27AltGeometry
                                                                                            : kST27Geometry;
        break;
    case 0x29:
        geo = story < kST29LaterStory ? kST29Geometry : kST29LaterGeometry;
        tex = story < kST29LaterStory ? kST29Texture : kST29LaterTexture;
        break;
    case 0x30:
        geo = storyFlag(kST30Flag) ? kST30AltGeometry : kST30Geometry;
        tex = storyFlag(kST30Flag) ? kST30AltTexture : kST30Texture;
        break;
    case 0x33:
        geo = kST33Geometry;
        tex = storyFlag(kST33Flag) ? kST33AltTexture : kST33Texture;
        break;
    case 0x35:
        if (gameState().room == kST35OwnFilesRoom) {
            geo = kST3502Geometry;
            tex = kST3501Texture;
        } else {
            geo = storyFlag(kST35FirstGuardianFlag) && !storyFlag(kST35SecondGuardianFlag) ? kST3501Geometry
                                                                                         : kST35Geometry;
            tex = kST35Texture;
        }
        break;
    case 0x39:
        geo = story < kST39LaterStory ? kST39Geometry : kST39LaterGeometry;
        tex = story < kST39LaterStory ? kST39Texture : kST39LaterTexture;
        break;
    case 0x3c:
        geo = story < kST3CLaterStory ? kST3CGeometry : kST3CLaterGeometry;
        tex = story < kST3CLaterStory ? kST3CTexture : kST3CLaterTexture;
        break;
    case 0x53:
        if (story == kST53VariantStory && storyFlag(kST53FlagA)) {
            geo = kST53GeometryA;
            tex = kST53TextureA;
        } else if (story == kST53VariantStory && storyFlag(kST53FlagB)) {
            geo = kST53GeometryB;
            tex = kST53TextureB;
        } else {
            geo = kST53Geometry;
            tex = kST53Texture;
        }
        break;
    default: break; // the selector is the exe's plain ret: the tables' own ids
    }
    geometry = datFileName(geo);
    texture = datFileName(tex);
    return !geometry.empty() && !texture.empty();
}

} // namespace od::port
