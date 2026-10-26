// The save image (see save_image.hpp): its checksums, its serialisation from the game state, its memory-card header and
// title, and the slot summary taken from it. Transcribed from the exe (Ghidra + disassembly, 2026-10-08); every write
// keeps the exe's width and order.
#include "game/save_image.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "engine/data_source.hpp"
#include "game/audio_logic.hpp"
#include "game/game_state.hpp"
#include "game/loaded_data.hpp"
#include "game/patched_data.hpp"
#include "game/player/player_state.hpp"
#include "game/save_slots.hpp"
#include "game/shop.hpp"
#include "game/visited_rooms.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

// the four checksum blocks: {the first word, the word count, the checksum's word}
constexpr uint32_t kBlocks[4][3] = {{0, 0x5f, 0x5f}, {0x80, 0x1f, 0x9f}, {0xa0, 0x5f, 0xff}, {0x100, 0x1ff, 0x2ff}};
constexpr uint32_t kHeaderChecksum = 0x5f; // the first block's checksum word

// the little-endian dword `word` of an image in host bytes
constexpr uint32_t imageWord(std::span<const uint8_t> b, uint32_t word) {
    const uint32_t o = word * 4;
    return uint32_t(b[o]) | uint32_t(b[o + 1]) << 8 | uint32_t(b[o + 2]) << 16 | uint32_t(b[o + 3]) << 24;
}
// the word sum of block k of an image in host bytes
uint32_t blockSum(std::span<const uint8_t> b, int k) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < kBlocks[k][1]; ++i) s += imageWord(b, kBlocks[k][0] + i);
    return s;
}
// ... of the image in the game state
uint32_t blockSum(const SaveImage& image, int k) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < kBlocks[k][1]; ++i) s += image.word(kBlocks[k][0] + i);
    return s;
}

// The memory-card title the save menu builds (exe .data TableId::SaveImageTitle, 0x40 bytes of Shift-JIS): the slot's
// digit, the play time's digits (two bytes each, written low byte first), then the place name. The place names (exe
// .data TableId::SaveImagePlaceNameTitle: 32-byte Shift-JIS slots, 0-ended) start inside the title: their slot 0 is
// the title's +0x24.. (the copy the title shows), so the record runs to the end of that slot (+0x44). The others are
// read by the place's index + 1. (The exe writes its own data: a patched record.)
constexpr uint32_t kPlaceNameSize = 0x20;
constexpr uint32_t kTitlePlaceName = 0x24; // place name slot 0 in the title
struct SaveTitleText : DataRecord {
    static constexpr uint32_t kTitleBytes = 0x40;
    static constexpr uint32_t kBytes = kTitlePlaceName + kPlaceNameSize;
    std::array<uint8_t, kBytes> bytes{};
    // a glyph's two bytes at +o (the slot's digit +0xc, the time's digits +0x10 .. +0x1e)
    std::span<uint8_t, 2> glyph(size_t o) { return std::span<uint8_t, 2>(bytes.data() + o, 2); }
    // place name slot 0 (the title's own)
    std::span<uint8_t, kPlaceNameSize> placeName() {
        return std::span<uint8_t, kPlaceNameSize>(bytes.data() + kTitlePlaceName, kPlaceNameSize);
    }
    // the whole title as the header copies it (dword i < kTitleBytes / 4)
    uint32_t word(size_t i) const {
        return uint32_t(bytes[i * 4]) | uint32_t(bytes[i * 4 + 1]) << 8 | uint32_t(bytes[i * 4 + 2]) << 16 |
               uint32_t(bytes[i * 4 + 3]) << 24;
    }
    template <class R> static SaveTitleText load(const R& r) {
        SaveTitleText t;
        for (size_t i = 0; i < kBytes; ++i) t.bytes[i] = r.u8(i);
        return t;
    }
    void save(std::span<uint8_t> out) const { std::copy(bytes.begin(), bytes.end(), out.begin()); }
};
constexpr uint32_t kSlotDigit = 0x0c, kHoursTens = 0x10, kHoursOnes = 0x12, kMinutesTens = 0x16, kMinutesOnes = 0x18,
                   kSecondsTens = 0x1c, kSecondsOnes = 0x1e;
// the PS1's title (USA: "MEGAMAN LEGENDS2[*]**:**:**", the save points' overlays, ST04T.BIN 0x800f0008; its builder
// 0x800e9744): the digits 0x16 further on, no place name in it
constexpr uint32_t kPs1TitleShift = 0x16;
uint32_t titleShift() { return dataSource().isPs1() ? kPs1TitleShift : 0; }
PatchedRecord<SaveTitleText> gSaveTitle{TableId::SaveImageTitle};
SaveTitleText& saveTitleText() { return *gSaveTitle; }
// byte i of place name slot `slot` (slot 0: the title's copy)
uint8_t placeNameByte(size_t slot, uint32_t i) {
    if (slot == 0) return saveTitleText().placeName()[i];
    return dataScalar<uint8_t>(stateAddress(TableId::SaveImagePlaceNameTitle) + uint32_t(slot) * kPlaceNameSize + i);
}
// The places (exe table 0x8af940, 25 x 4 bytes: the stage and the room a name belongs to)
struct PlaceEntry : DataRecord {
    uint8_t stage = 0;
    uint8_t room = 0;
    static PlaceEntry load(const TableReader& r) {
        PlaceEntry rec;
        rec.stage = r.u8(0);
        rec.room = r.u8(1);
        return rec;
    }
};
constexpr size_t kPlaces = 0x19;
const DataTable<RecordTable<PlaceEntry, kPlaces, 4>> places{TableId::SaveImagePlaces};
// the save's icon (exe .data): the clut and the 16 x 16 4-bit bitmap
const DataTable<ScalarTable<uint32_t, 8>> saveIconClut{TableId::SaveImageIconClut};
const DataTable<ScalarTable<uint32_t, 0x20>> saveIconBitmap{TableId::SaveImageIconBitmap};
constexpr uint8_t kSaveIconFlag = 0x11, kSaveBlockCount = 1;
// the Shift-JIS of the title: the full-width digits (0x824f + d), the name's end mark and its padding
constexpr uint32_t kSjisDigitZero = 0x824f;
constexpr uint8_t kSjisLead = 0x81, kSjisNameEnd = 0x60, kSjisSpace = 0x40;
constexpr size_t kTitleNameGlyphs = 0xc;
// the play time's units (frames)
constexpr uint32_t kFramesPerHour = 0x34bc0, kFramesPerMinute = 0xe10, kFramesPerSecond = 0x3c;

// a full-width digit d into its two title bytes (the low byte first, as the exe writes them)
void writeDigit(std::span<uint8_t, 2> glyph, uint32_t d) {
    glyph[1] = uint8_t(d + (kSjisDigitZero & 0xff));
    glyph[0] = uint8_t((d + kSjisDigitZero) >> 8);
}

} // namespace

// ---- the image's bytes ------------------------------------------------------------------------------------------------
namespace {
template <class T> void put(std::array<uint8_t, kSaveSize>& b, size_t o, T v) {
    using U = std::make_unsigned_t<T>;
    for (size_t i = 0; i < sizeof(T); ++i) b[o + i] = uint8_t(U(v) >> (8 * i));
}
template <class T> T get(std::span<const uint8_t> b, size_t o) {
    std::make_unsigned_t<T> v = 0;
    for (size_t i = 0; i < sizeof(T); ++i) v |= std::make_unsigned_t<T>(uint32_t(b[o + i]) << (8 * i));
    return T(v);
}
} // namespace

std::array<uint8_t, kSaveSize> SaveImage::bytes() const {
    std::array<uint8_t, kSaveSize> b{};
    put(b, 0x0, magic);
    put(b, 0x2, iconFlag);
    put(b, 0x3, blockCount);
    for (size_t i = 0; i < title.size(); ++i) put(b, 0x4 + i * sizeof(uint32_t), title[i]);
    for (size_t i = 0; i < headerZeros.size(); ++i) put(b, 0x44 + i * sizeof(uint8_t), headerZeros[i]);
    for (size_t i = 0; i < iconClut.size(); ++i) put(b, 0x60 + i * sizeof(uint32_t), iconClut[i]);
    for (size_t i = 0; i < iconBitmap.size(); ++i) put(b, 0x80 + i * sizeof(uint32_t), iconBitmap[i]);
    std::copy(gap100.begin(), gap100.end(), b.begin() + 0x100);
    put(b, 0x128, stage);
    put(b, 0x129, room);
    put(b, 0x12a, flutterDock);
    put(b, 0x12b, playerKind);
    put(b, 0x12c, story);
    put(b, 0x12d, nextStory);
    put(b, 0x12e, difficulty);
    put(b, 0x12f, place);
    put(b, 0x130, bestTime);
    put(b, 0x134, bestDifficulty);
    put(b, 0x136, clears);
    put(b, 0x138, playTime);
    put(b, 0x13c, playTimeCopy);
    std::copy(gap140.begin(), gap140.end(), b.begin() + 0x140);
    for (size_t i = 0; i < counters.size(); ++i) put(b, 0x200 + i * sizeof(uint32_t), counters[i]);
    put(b, 0x218, balance);
    put(b, 0x21a, toolKitBalance);
    put(b, 0x21c, balanceTier);
    put(b, 0x21d, toolKitTier);
    std::copy(gap21e.begin(), gap21e.end(), b.begin() + 0x21e);
    put(b, 0x220, mono);
    put(b, 0x221, minimapShown);
    put(b, 0x222, ruby);
    std::copy(gap223.begin(), gap223.end(), b.begin() + 0x223);
    put(b, 0x224, controlOption);
    put(b, 0x225, controllerLayout);
    put(b, 0x226, lookDirection);
    put(b, 0x227, vibrationOff);
    put(b, 0x228, autoLockSpecial);
    put(b, 0x229, autoLockBuster);
    std::copy(gap22a.begin(), gap22a.end(), b.begin() + 0x22a);
    for (size_t i = 0; i < charges.size(); ++i) put(b, 0x22c + i * sizeof(uint8_t), charges[i]);
    for (size_t i = 0; i < scriptCounters.size(); ++i) put(b, 0x234 + i * sizeof(uint16_t), scriptCounters[i]);
    for (size_t i = 0; i < dockCounters.size(); ++i) put(b, 0x238 + i * sizeof(uint32_t), dockCounters[i]);
    put(b, 0x240, countedDock);
    std::copy(gap241.begin(), gap241.end(), b.begin() + 0x241);
    put(b, 0x244, sfxMaster);
    put(b, 0x245, bgmMaster);
    std::copy(gap246.begin(), gap246.end(), b.begin() + 0x246);
    for (size_t i = 0; i < unreadRecords.size(); ++i) put(b, 0x24c + i * sizeof(uint32_t), unreadRecords[i]);
    std::copy(gap26c.begin(), gap26c.end(), b.begin() + 0x26c);
    put(b, 0x280, int32_t(playerPos.x));
    put(b, 0x284, int32_t(playerPos.y));
    put(b, 0x288, int32_t(playerPos.z));
    put(b, 0x28c, playerYaw);
    std::copy(gap28e.begin(), gap28e.end(), b.begin() + 0x28e);
    put(b, 0x290, hitPoints);
    put(b, 0x292, maxHitPoints);
    put(b, 0x294, status);
    put(b, 0x296, pulled);
    put(b, 0x297, wadeDepth);
    put(b, 0x298, drag);
    put(b, 0x299, slippery);
    put(b, 0x29a, inWater);
    put(b, 0x29b, burden);
    for (size_t i = 0; i < statusTicks.size(); ++i) put(b, 0x29c + i * sizeof(uint16_t), statusTicks[i]);
    put(b, 0x2a4, specialWeapon);
    put(b, 0x2a5, busterWeapon);
    put(b, 0x2a6, specialAllowed);
    std::copy(gap2a7.begin(), gap2a7.end(), b.begin() + 0x2a7);
    for (size_t i = 0; i < gauge.size(); ++i) put(b, 0x2a8 + i * sizeof(uint16_t), gauge[i]);
    put(b, 0x2b4, helmetPart);
    put(b, 0x2b5, legPart);
    put(b, 0x2b6, armourPart);
    std::copy(gap2b7.begin(), gap2b7.end(), b.begin() + 0x2b7);
    put(b, 0x2b8, busterParts);
    for (size_t i = 0; i < ownedParts.size(); ++i) put(b, 0x2bc + i * sizeof(uint8_t), ownedParts[i]);
    for (size_t i = 0; i < weaponLevels.size(); ++i) put(b, 0x2e0 + i * sizeof(int8_t), weaponLevels[i]);
    std::copy(gap380.begin(), gap380.end(), b.begin() + 0x380);
    for (size_t i = 0; i < storyFlags.size(); ++i) put(b, 0x400 + i * sizeof(uint32_t), storyFlags[i]);
    for (size_t i = 0; i < extraFlags.size(); ++i) put(b, 0x520 + i * sizeof(uint32_t), extraFlags[i]);
    for (size_t i = 0; i < visitedRooms.size(); ++i) put(b, 0xb20 + i * sizeof(uint32_t), visitedRooms[i]);
    for (size_t i = 0; i < bestTimes.size(); ++i) put(b, 0xb34 + i * sizeof(uint32_t), bestTimes[i]);
    std::copy(gapba0.begin(), gapba0.end(), b.begin() + 0xba0);
    return b;
}

void SaveImage::setBytes(std::span<const uint8_t> in) {
    if (in.size() < kSaveSize) throw std::invalid_argument("save image: fewer bytes than an image");
    magic = get<uint16_t>(in, 0x0);
    iconFlag = get<uint8_t>(in, 0x2);
    blockCount = get<uint8_t>(in, 0x3);
    for (size_t i = 0; i < title.size(); ++i) title[i] = get<uint32_t>(in, 0x4 + i * sizeof(uint32_t));
    for (size_t i = 0; i < headerZeros.size(); ++i) headerZeros[i] = get<uint8_t>(in, 0x44 + i * sizeof(uint8_t));
    for (size_t i = 0; i < iconClut.size(); ++i) iconClut[i] = get<uint32_t>(in, 0x60 + i * sizeof(uint32_t));
    for (size_t i = 0; i < iconBitmap.size(); ++i) iconBitmap[i] = get<uint32_t>(in, 0x80 + i * sizeof(uint32_t));
    std::copy_n(in.begin() + 0x100, gap100.size(), gap100.begin());
    stage = get<uint8_t>(in, 0x128);
    room = get<uint8_t>(in, 0x129);
    flutterDock = get<uint8_t>(in, 0x12a);
    playerKind = get<uint8_t>(in, 0x12b);
    story = get<uint8_t>(in, 0x12c);
    nextStory = get<uint8_t>(in, 0x12d);
    difficulty = get<uint8_t>(in, 0x12e);
    place = get<uint8_t>(in, 0x12f);
    bestTime = get<uint32_t>(in, 0x130);
    bestDifficulty = get<uint16_t>(in, 0x134);
    clears = get<uint16_t>(in, 0x136);
    playTime = get<uint32_t>(in, 0x138);
    playTimeCopy = get<uint32_t>(in, 0x13c);
    std::copy_n(in.begin() + 0x140, gap140.size(), gap140.begin());
    for (size_t i = 0; i < counters.size(); ++i) counters[i] = get<uint32_t>(in, 0x200 + i * sizeof(uint32_t));
    balance = get<uint16_t>(in, 0x218);
    toolKitBalance = get<uint16_t>(in, 0x21a);
    balanceTier = get<uint8_t>(in, 0x21c);
    toolKitTier = get<uint8_t>(in, 0x21d);
    std::copy_n(in.begin() + 0x21e, gap21e.size(), gap21e.begin());
    mono = get<uint8_t>(in, 0x220);
    minimapShown = get<uint8_t>(in, 0x221);
    ruby = get<uint8_t>(in, 0x222);
    std::copy_n(in.begin() + 0x223, gap223.size(), gap223.begin());
    controlOption = get<uint8_t>(in, 0x224);
    controllerLayout = get<uint8_t>(in, 0x225);
    lookDirection = get<uint8_t>(in, 0x226);
    vibrationOff = get<uint8_t>(in, 0x227);
    autoLockSpecial = get<uint8_t>(in, 0x228);
    autoLockBuster = get<uint8_t>(in, 0x229);
    std::copy_n(in.begin() + 0x22a, gap22a.size(), gap22a.begin());
    for (size_t i = 0; i < charges.size(); ++i) charges[i] = get<uint8_t>(in, 0x22c + i * sizeof(uint8_t));
    for (size_t i = 0; i < scriptCounters.size(); ++i) scriptCounters[i] = get<uint16_t>(in, 0x234 + i * sizeof(uint16_t));
    for (size_t i = 0; i < dockCounters.size(); ++i) dockCounters[i] = get<uint32_t>(in, 0x238 + i * sizeof(uint32_t));
    countedDock = get<uint8_t>(in, 0x240);
    std::copy_n(in.begin() + 0x241, gap241.size(), gap241.begin());
    sfxMaster = get<uint8_t>(in, 0x244);
    bgmMaster = get<uint8_t>(in, 0x245);
    std::copy_n(in.begin() + 0x246, gap246.size(), gap246.begin());
    for (size_t i = 0; i < unreadRecords.size(); ++i) unreadRecords[i] = get<uint32_t>(in, 0x24c + i * sizeof(uint32_t));
    std::copy_n(in.begin() + 0x26c, gap26c.size(), gap26c.begin());
    playerPos.x = get<int32_t>(in, 0x280);
    playerPos.y = get<int32_t>(in, 0x284);
    playerPos.z = get<int32_t>(in, 0x288);
    playerYaw = get<uint16_t>(in, 0x28c);
    std::copy_n(in.begin() + 0x28e, gap28e.size(), gap28e.begin());
    hitPoints = get<uint16_t>(in, 0x290);
    maxHitPoints = get<uint16_t>(in, 0x292);
    status = get<uint16_t>(in, 0x294);
    pulled = get<uint8_t>(in, 0x296);
    wadeDepth = get<uint8_t>(in, 0x297);
    drag = get<uint8_t>(in, 0x298);
    slippery = get<uint8_t>(in, 0x299);
    inWater = get<uint8_t>(in, 0x29a);
    burden = get<uint8_t>(in, 0x29b);
    for (size_t i = 0; i < statusTicks.size(); ++i) statusTicks[i] = get<uint16_t>(in, 0x29c + i * sizeof(uint16_t));
    specialWeapon = get<uint8_t>(in, 0x2a4);
    busterWeapon = get<uint8_t>(in, 0x2a5);
    specialAllowed = get<uint8_t>(in, 0x2a6);
    std::copy_n(in.begin() + 0x2a7, gap2a7.size(), gap2a7.begin());
    for (size_t i = 0; i < gauge.size(); ++i) gauge[i] = get<uint16_t>(in, 0x2a8 + i * sizeof(uint16_t));
    helmetPart = get<uint8_t>(in, 0x2b4);
    legPart = get<uint8_t>(in, 0x2b5);
    armourPart = get<uint8_t>(in, 0x2b6);
    std::copy_n(in.begin() + 0x2b7, gap2b7.size(), gap2b7.begin());
    busterParts = get<uint32_t>(in, 0x2b8);
    for (size_t i = 0; i < ownedParts.size(); ++i) ownedParts[i] = get<uint8_t>(in, 0x2bc + i * sizeof(uint8_t));
    for (size_t i = 0; i < weaponLevels.size(); ++i) weaponLevels[i] = get<int8_t>(in, 0x2e0 + i * sizeof(int8_t));
    std::copy_n(in.begin() + 0x380, gap380.size(), gap380.begin());
    for (size_t i = 0; i < storyFlags.size(); ++i) storyFlags[i] = get<uint32_t>(in, 0x400 + i * sizeof(uint32_t));
    for (size_t i = 0; i < extraFlags.size(); ++i) extraFlags[i] = get<uint32_t>(in, 0x520 + i * sizeof(uint32_t));
    for (size_t i = 0; i < visitedRooms.size(); ++i) visitedRooms[i] = get<uint32_t>(in, 0xb20 + i * sizeof(uint32_t));
    for (size_t i = 0; i < bestTimes.size(); ++i) bestTimes[i] = get<uint32_t>(in, 0xb34 + i * sizeof(uint32_t));
    std::copy_n(in.begin() + 0xba0, gapba0.size(), gapba0.begin());
}

uint32_t SaveImage::word(size_t i) const {
    if (i >= kSaveSize / 4) stateIndexError(i, kSaveSize / 4);
    const std::array<uint8_t, kSaveSize> b = bytes();
    return get<uint32_t>(b, i * 4);
}

void SaveImage::setWord(size_t i, uint32_t v) {
    if (i >= kSaveSize / 4) stateIndexError(i, kSaveSize / 4);
    std::array<uint8_t, kSaveSize> b = bytes();
    put(b, i * 4, v);
    setBytes(b);
}

void saveChecksumWrite(std::span<uint8_t> image) {
    for (int k = 0; k < 4; ++k) {
        const uint32_t o = kBlocks[k][2] * 4;
        for (int i = 0; i < 4; ++i) image[o + i] = 0; // the exe clears the slot first (outside the summed range)
        const uint32_t s = blockSum(image, k);
        for (int i = 0; i < 4; ++i) image[o + i] = uint8_t(s >> (8 * i));
    }
}

bool saveChecksumBad(std::span<const uint8_t> image, bool headerOnly) {
    if (blockSum(image, 0) != imageWord(image, kHeaderChecksum)) return true;
    if (headerOnly) return false;
    for (int k = 1; k < 4; ++k)
        if (blockSum(image, k) != imageWord(image, kBlocks[k][2])) return true;
    return false;
}

SaveImage& saveImageAt(uint32_t address) {
    static SaveImage buffer, clearData; // kSaveBuf, kClearDataImage
    if (address == kSaveBuf) return buffer;
    if (address == kClearDataImage) return clearData;
    throw std::invalid_argument("save image: no image at " + std::to_string(address));
}

void saveChecksumWrite(uint32_t image) {
    SaveImage& img = saveImageAt(image);
    for (int k = 0; k < 4; ++k) img.setWord(kBlocks[k][2], blockSum(img, k));
}

bool saveChecksumBad(uint32_t image, bool headerOnly) {
    const SaveImage& img = saveImageAt(image);
    if (blockSum(img, 0) != img.word(kHeaderChecksum)) return true;
    if (headerOnly) return false;
    for (int k = 1; k < 4; ++k)
        if (blockSum(img, k) != img.word(kBlocks[k][2])) return true;
    return false;
}

void saveSerialise(uint32_t image) {
    SaveImage& b = saveImageAt(image);
    GameState& g = gameState();
    b.stage = g.stage;
    b.room = g.room;
    b.flutterDock = g.flutterDock;
    b.playerKind = g.playerKind;
    b.story = g.story;
    b.nextStory = g.nextStory;
    b.difficulty = g.difficulty;
    b.bestTime = g.bestTime;
    b.bestDifficulty = g.bestDifficulty;
    b.clears = g.clears;
    b.playTime = g.playTime;
    b.playTimeCopy = g.playTimeCopy;
    b.counters[0] = uint32_t(g.zenny);
    b.counters[1] = g.unreadCounter;
    b.counters[2] = g.damageTaken;
    b.counters[3] = g.damageShown;
    b.counters[4] = g.defeats;
    b.counters[5] = g.defeatsTotal;
    b.balance = uint16_t(g.balance);
    b.toolKitBalance = uint16_t(g.toolKitBalance);
    b.balanceTier = uint8_t(g.balanceTier);
    b.toolKitTier = uint8_t(g.toolKitTier);
    GameOptions& o = gameOptions();
    b.mono = o.mono;
    b.minimapShown = o.minimapShown;
    b.ruby = o.ruby;
    b.controlOption = o.controlOption;
    b.controllerLayout = o.controllerLayout;
    b.lookDirection = o.lookDirection;
    b.vibrationOff = o.vibrationOff;
    b.autoLockSpecial = o.autoLockSpecial;
    b.autoLockBuster = o.autoLockBuster;
    ShopUpgrades& charges = shopUpgrades();
    b.charges[0] = uint8_t(charges.first.level);
    b.charges[1] = uint8_t(charges.first.bought);
    b.charges[2] = uint8_t(charges.second.level);
    b.charges[3] = uint8_t(charges.second.bought);
    for (size_t i = 0; i < charges.unreadBytes.size(); ++i) b.charges[4 + i] = charges.unreadBytes[i];
    b.scriptCounters[0] = g.scriptCounters[0];
    b.scriptCounters[1] = g.scriptCounters[1];
    b.dockCounters[0] = g.dockCountersWord(0);
    b.dockCounters[1] = g.dockCountersWord(1);
    b.countedDock = g.countedDock;
    b.sfxMaster = uint8_t(soundMasters().sfx);
    b.bgmMaster = uint8_t(soundMasters().bgm);
    for (size_t i = 0; i < GameState::kEntryLogWords; ++i) b.unreadRecords[i] = g.entryLogWord(i);
    // the player
    Player& p = player();
    b.playerPos = p.pos;
    b.playerYaw = uint16_t(p.rotation.y);
    b.hitPoints = uint16_t(p.hitPoints);
    b.maxHitPoints = uint16_t(p.maxHitPoints);
    b.status = p.status;
    b.pulled = p.pulled;
    b.wadeDepth = uint8_t(p.wadeDepth);
    b.drag = uint8_t(p.drag);
    b.slippery = p.slippery;
    b.inWater = p.inWater;
    b.burden = uint8_t(p.burden);
    b.statusTicks[0] = uint16_t(p.statusTicks[0]);
    b.statusTicks[1] = uint16_t(p.statusTicks[1]);
    b.statusTicks[2] = uint16_t(p.statusTicks[2]);
    b.statusTicks[3] = uint16_t(p.statusTicks[3]);
    b.specialWeapon = uint8_t(p.specialWeapon);
    b.busterWeapon = uint8_t(p.busterWeapon);
    b.specialAllowed = p.specialAllowed;
    b.gauge[0] = uint16_t(p.energyReserve);
    b.gauge[1] = uint16_t(p.energyPool);
    b.gauge[2] = uint16_t(p.gaugeMax);
    b.gauge[3] = uint16_t(p.gaugeRate);
    b.gauge[4] = uint16_t(p.shotCost);
    b.gauge[5] = uint16_t(p.gauge);
    b.helmetPart = uint8_t(p.helmetPart);
    b.legPart = uint8_t(p.legPart);
    b.armourPart = uint8_t(p.armourPart);
    b.busterParts = p.busterPartsWord();
    for (size_t i = 0; i < kOwnedPartSlots; ++i) b.ownedParts[i] = p.ownedParts[i];
    for (size_t i = 0; i < p.unreadPartBytes.size(); ++i) b.ownedParts[kOwnedPartSlots + i] = p.unreadPartBytes[i];
    for (size_t k = 0; k < kSaveWeaponLevels; ++k) // the first bytes of each 8-byte slot
        for (size_t i = 0; i < kSaveLevelBytes; ++i) b.weaponLevels[k * 8 + i] = p.weaponLevels[k * 8 + i];
    StoryFlags& flags = storyFlags();
    for (size_t i = 0; i < b.storyFlags.size(); ++i) b.storyFlags[i] = flags.word(i);
    ExtraFlags& extra = extraFlags();
    for (size_t i = 0; i < b.extraFlags.size(); ++i) b.extraFlags[i] = extra.word(i);
    VisitedRoomBlock& rooms = visitedRooms();
    for (size_t i = 0; i < kVisitedRoomWords; ++i) b.visitedRooms[i] = rooms.word(i);
    BestTimes& times = bestTimes();
    for (size_t i = 0; i < kBestTimeWords; ++i) b.bestTimes[i] = times.word(i);
    saveChecksumWrite(image);
}

void saveTitle(uint8_t slot, uint32_t image, uint32_t time, uint32_t fromImage) {
    SaveImage& b = saveImageAt(image);
    GameState& g = gameState();
    uint32_t t = g.playTime;
    uint8_t room = g.room, st = g.stage;
    if (fromImage != 0) {
        t = time;
        room = b.room;
        st = b.stage;
    }
    for (uint32_t i = 0; i < kPlaces; ++i)
        if (places[i].stage == st && places[i].room == room) {
            b.place = uint8_t(i + 1);
            break;
        }
    SaveTitleText& title = saveTitleText();
    const uint32_t at = titleShift();
    title.bytes[at + kSlotDigit + 1] = uint8_t(slot + 0x50);
    title.bytes[at + kSlotDigit] = uint8_t((slot + 0x8250u) >> 8);
    const uint32_t h = (t / kFramesPerHour) & 0xff;
    const uint32_t mi = uint8_t((t % kFramesPerHour) / kFramesPerMinute);
    const uint32_t s = uint8_t((t % kFramesPerMinute) / kFramesPerSecond);
    writeDigit(title.glyph(at + kHoursTens), h / 10);
    writeDigit(title.glyph(at + kHoursOnes), h % 10);
    writeDigit(title.glyph(at + kMinutesTens), mi / 10);
    writeDigit(title.glyph(at + kMinutesOnes), mi % 10);
    writeDigit(title.glyph(at + kSecondsTens), s / 10);
    writeDigit(title.glyph(at + kSecondsOnes), s % 10);
    if (at != 0) return; // the PS1's title has no place name
    // the place's name into slot 0 (the title's), its end mark, the padding
    const std::span<uint8_t, kPlaceNameSize> to = saveTitleText().placeName();
    const size_t from = b.place;
    uint32_t len = 0;
    while (placeNameByte(from, len) != 0) ++len;
    for (uint32_t i = 0; i < (len & 0xffff); ++i) to[i] = placeNameByte(from, i);
    const uint32_t n = ((len & 0xffff) >> 1) + 1;
    to[len & 0xfffe] = kSjisLead;
    to[(len & 0xfffe) + 1] = kSjisNameEnd;
    for (uint32_t i = n; i < kTitleNameGlyphs; ++i) {
        to[i * 2] = kSjisLead;
        to[i * 2 + 1] = kSjisSpace;
    }
}

void saveHeader(uint32_t image) {
    SaveImage& b = saveImageAt(image);
    const SaveTitleText& text = saveTitleText();
    b.magic = kSaveMagic;
    b.iconFlag = kSaveIconFlag;
    b.blockCount = kSaveBlockCount;
    for (size_t i = 0; i < b.title.size(); ++i) b.title[i] = text.word(i);
    b.headerZeros.fill(0);
    for (size_t i = 0; i < b.iconClut.size(); ++i) b.iconClut[i] = saveIconClut[i];
    for (size_t i = 0; i < b.iconBitmap.size(); ++i) b.iconBitmap[i] = saveIconBitmap[i];
}

void saveBuildImage(uint8_t slot) {
    saveTitle(slot, kSaveBuf, gameState().playTime, 0);
    saveHeader(kSaveBuf);
    saveSerialise(kSaveBuf);
}

void saveSlotSummaryFromImage(uint32_t slot, const SaveImage& image) {
    SaveSlotSummaries& s = saveSlotSummaries();
    s.slots[slot].stage = image.stage;
    s.slots[slot].room = image.room;
    s.slots[slot].difficulty = image.difficulty;
    s.slots[slot].place = image.place;
    s.slots[slot].time = int32_t(uint32_t(image.playTime));
}

void saveSlotTableFill(uint8_t slot, std::span<const uint8_t> image) {
    if (image.size() < kSaveHeaderSize || saveChecksumBad(image, true)) return;
    SaveSlotSummaries& s = saveSlotSummaries();
    s.slots[slot].stage = image[kImageStage];
    s.slots[slot].room = image[kImageRoom];
    s.slots[slot].difficulty = image[kImageDifficulty];
    s.slots[slot].place = image[kImagePlace];
    s.slots[slot].time = int32_t(imageWord(image, kImagePlayTime / 4));
}

} // namespace od::port
