// The save image: the 0xc00 bytes the game builds at kSaveBuf (PS1 RAM 0x90000) and writes raw to DASH2-%02d.DAT (exe
// 0x5493b0 / read back by exe 0x548de0): a PS1 memory-card header ("SC", the title, the icon), the game state (+0x128
// / +0x200), the player (+0x280), the event flags (+0x400 / +0x520) and two more blocks (+0xb20 / +0xb34), guarded by
// four u32 word-sum checksums. Bit-exact against tests/data/save_golden.bin.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <array>

#include "game/vectors.hpp"

namespace od::port {

constexpr uint32_t kSaveBuf = 0x6c0000;   // the game's save image buffer (PS1 RAM 0x90000)
constexpr uint32_t kSaveSize = 0xc00;     // DASH2-%02d.DAT
constexpr uint32_t kSaveHeaderSize = 0x180; // the part the slot list reads (the first checksum's block)
constexpr uint32_t kClearDataImage = 0x73fc00; // the in-memory image the game-clear save writes into (exe 0x58b3a0
                                               // builds it)
// the offsets of the summary's bytes in an image (the slot list reads them from the host's bytes too)
constexpr uint32_t kImageStage = 0x128, kImageRoom = 0x129, kImageDifficulty = 0x12e, kImagePlace = 0x12f,
                   kImagePlayTime = 0x138;

constexpr size_t kSaveWeaponLevels = 20; // the weapons' level slots the image keeps
constexpr size_t kSaveLevelBytes = 5;    // ... the first bytes of each

// The image's layout (the destination of exe 0x542ac0, the source of the game mode's init); the bytes between its
// fields keep what the image held (the checksums among them: word / setWord)
struct SaveImage {
    uint16_t magic = 0; // +0x000: kSaveMagic
    uint8_t iconFlag = 0; // +0x002
    uint8_t blockCount = 0; // +0x003
    std::array<uint32_t, 0x10> title{}; // +0x004: the Shift-JIS title (exe 0x549600)
    std::array<uint8_t, 0x1c> headerZeros{}; // +0x044
    std::array<uint32_t, 0x8> iconClut{}; // +0x060
    std::array<uint32_t, 0x20> iconBitmap{}; // +0x080: 16 x 16, 4 bits
    std::array<uint8_t, 0x28> gap100{}; // +0x100
    uint8_t stage = 0; // +0x128
    uint8_t room = 0; // +0x129
    uint8_t flutterDock = 0; // +0x12a
    uint8_t playerKind = 0; // +0x12b
    uint8_t story = 0; // +0x12c
    uint8_t nextStory = 0; // +0x12d
    uint8_t difficulty = 0; // +0x12e
    uint8_t place = 0; // +0x12f: the place name's index + 1 (exe 0x549600; left as is when not listed)
    uint32_t bestTime = 0; // +0x130
    uint16_t bestDifficulty = 0; // +0x134
    uint16_t clears = 0; // +0x136
    uint32_t playTime = 0; // +0x138
    uint32_t playTimeCopy = 0; // +0x13c
    std::array<uint8_t, 0xc0> gap140{}; // +0x140
    std::array<uint32_t, 0x6> counters{}; // +0x200: zenny, the dword after it, the damages, the defeats (GameState order)
    uint16_t balance = 0; // +0x218
    uint16_t toolKitBalance = 0; // +0x21a
    uint8_t balanceTier = 0; // +0x21c
    uint8_t toolKitTier = 0; // +0x21d
    std::array<uint8_t, 0x2> gap21e{}; // +0x21e
    uint8_t mono = 0; // +0x220
    uint8_t minimapShown = 0; // +0x221
    uint8_t ruby = 0; // +0x222
    std::array<uint8_t, 0x1> gap223{}; // +0x223
    uint8_t controlOption = 0; // +0x224
    uint8_t controllerLayout = 0; // +0x225
    uint8_t lookDirection = 0; // +0x226
    uint8_t vibrationOff = 0; // +0x227
    uint8_t autoLockSpecial = 0; // +0x228
    uint8_t autoLockBuster = 0; // +0x229
    std::array<uint8_t, 0x2> gap22a{}; // +0x22a
    std::array<uint8_t, 0x8> charges{}; // +0x22c: the canister / cure charges (game/shop ShopUpgrades) and two more
    std::array<uint16_t, 0x2> scriptCounters{}; // +0x234
    std::array<uint32_t, 0x2> dockCounters{}; // +0x238
    uint8_t countedDock = 0; // +0x240
    std::array<uint8_t, 0x3> gap241{}; // +0x241
    uint8_t sfxMaster = 0; // +0x244
    uint8_t bgmMaster = 0; // +0x245
    std::array<uint8_t, 0x6> gap246{}; // +0x246
    std::array<uint32_t, 0x8> unreadRecords{}; // +0x24c
    std::array<uint8_t, 0x14> gap26c{}; // +0x26c
    Fixed16Vec3 playerPos; // +0x280
    uint16_t playerYaw = 0; // +0x28c
    std::array<uint8_t, 0x2> gap28e{}; // +0x28e
    uint16_t hitPoints = 0; // +0x290
    uint16_t maxHitPoints = 0; // +0x292
    uint16_t status = 0; // +0x294
    uint8_t pulled = 0; // +0x296
    uint8_t wadeDepth = 0; // +0x297
    uint8_t drag = 0; // +0x298
    uint8_t slippery = 0; // +0x299
    uint8_t inWater = 0; // +0x29a
    uint8_t burden = 0; // +0x29b
    std::array<uint16_t, 0x4> statusTicks{}; // +0x29c
    uint8_t specialWeapon = 0; // +0x2a4
    uint8_t busterWeapon = 0; // +0x2a5
    uint8_t specialAllowed = 0; // +0x2a6
    std::array<uint8_t, 0x1> gap2a7{}; // +0x2a7
    std::array<uint16_t, 0x6> gauge{}; // +0x2a8: the special weapon's energy and gauge (Player +0x190 .. +0x19a)
    uint8_t helmetPart = 0; // +0x2b4
    uint8_t legPart = 0; // +0x2b5
    uint8_t armourPart = 0; // +0x2b6
    std::array<uint8_t, 0x1> gap2b7{}; // +0x2b7
    uint32_t busterParts = 0; // +0x2b8: the part slots and the hit height level (Player::busterPartsWord)
    std::array<uint8_t, 0x24> ownedParts{}; // +0x2bc: the parts owned and the four bytes after them
    std::array<int8_t, 0xa0> weaponLevels{}; // +0x2e0: the first kSaveLevelBytes of each slot
    std::array<uint8_t, 0x80> gap380{}; // +0x380
    std::array<uint32_t, 0x48> storyFlags{}; // +0x400
    std::array<uint32_t, 0x180> extraFlags{}; // +0x520
    std::array<uint32_t, 0x5> visitedRooms{}; // +0xb20
    std::array<uint32_t, 0x1b> bestTimes{}; // +0xb34
    std::array<uint8_t, 0x60> gapba0{}; // +0xba0

    // the image's bytes (the file DASH2-%02d.DAT) / the image from them
    std::array<uint8_t, kSaveSize> bytes() const;
    void setBytes(std::span<const uint8_t> b);
    // the image's dword i (the checksums' word sums)
    uint32_t word(size_t i) const;
    void setWord(size_t i, uint32_t v);
};
constexpr uint16_t kSaveMagic = 0x4353; // "SC"
// the image the game names by its address (kSaveBuf or kClearDataImage; another address: std::invalid_argument)
SaveImage& saveImageAt(uint32_t address);

// exe 0x542f40: the four checksums (u32 word sums) of a save image: words [0, 0x5f) -> 0x5f, [0x80, 0x9f) -> 0x9f,
// [0xa0, 0xff) -> 0xff, [0x100, 0x2ff) -> 0x2ff
void saveChecksumWrite(std::span<uint8_t> image);
// exe 0x549550(image, headerOnly): true = a checksum is wrong (headerOnly: only the first block)
bool saveChecksumBad(std::span<const uint8_t> image, bool headerOnly = false);
// ... of the image at `image` in the game state
void saveChecksumWrite(uint32_t image);
bool saveChecksumBad(uint32_t image, bool headerOnly = false);

// exe 0x542ac0(image): the game state, the player and the flags into the image at `image` (only these fields; the rest
// keeps what it held), then the checksums
void saveSerialise(uint32_t image);
// exe 0x549600(slot, image, time, fromImage): the memory-card title at TableId::SaveImageTitle (the slot digit, the play time h:m:s
// from the frame count, the place name of the stage / room; the place's index + 1 into the image when listed).
// fromImage = 0: the time / the stage from the game state, else `time` and the image's stage / room
void saveTitle(uint8_t slot, uint32_t image, uint32_t time, uint32_t fromImage);
// exe 0x549350(image, TableId::SaveImageTitle): the "SC" header, the title, the icon's clut and bitmap
void saveHeader(uint32_t image);
// exe 0x549430's step 1 (the save menu, a slot chosen): the title, the header and the state into kSaveBuf
void saveBuildImage(uint8_t slot);
// exe 0x548bc0's step 0 for one slot: its summary (game/save_slots) from a save image whose header checksum is good
// (the host scans its saves; the slot bits: game/save_menus SaveSlotBits)
void saveSlotTableFill(uint8_t slot, std::span<const uint8_t> image);
// the summary of `slot` from an image in the game state (the menus' write and scan)
void saveSlotSummaryFromImage(uint32_t slot, const SaveImage& image);

} // namespace od::port
