// The player's model and files (see player_model.hpp).
//
// The file tables (the DAT names 0x8886d4: game/dat_names, the body files 0x8d37b4, the weapon files 0x8d37e2) come
// from the data source (engine/data_source).
#include "game/player/player_model.hpp"
#include "game/player/player_model_state.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/dat.hpp"
#include "engine/data_source.hpp"
#include "game/dat_names.hpp"
#include "game/player/player_state.hpp"

using namespace od::port::player_model_state;

namespace od {

namespace {
constexpr uint32_t kSeg0Psx = 0x110800, kSeg0Pc = 0x740800, kWeaponPsx = 0x113340;
constexpr uint32_t kPartHeaderBytes = 0x18; // a part's submesh header; a block's parts follow one another
// exe 0x5f3440: part blocks [0x91bce8 + (costume byte seg0+0x71 * 7 + block) * 4] as PC addresses of the PS1 RAM
// image (PC 0x740800 = PS1 0x110800 = segment 0); block 3 = the left arm with the special-weapon arm, block 5 = the
// right arm with the buster
constexpr size_t kCostumes = 3, kBlocks = 7, kGroups = 5, kRows = 4, kGroupBones = 17;
const DataTable<ScalarTable<uint32_t, kCostumes * kBlocks>> partBlocks{TableId::PlayerPartBlocks};
// the block each of the 5 part groups draws, a row by (special weapon > 1) + 2 * (buster > 1)
const DataTable<ScalarTable<uint8_t, kRows * kGroups>> partRows{TableId::PlayerPartRows};
// the groups' bones (0x91bd50), the count of each group (0x91bd64)
const DataTable<ScalarTable<uint8_t, kGroupBones>> groupBones{TableId::PlayerPartGroupBones};
const DataTable<ScalarTable<uint8_t, kGroups>> groupCounts{TableId::PlayerPartGroupCounts};

// the group a block is drawn in (its column in the rows)
uint32_t blockGroup(int block) {
    for (uint32_t row = 0; row < kRows; ++row)
        for (uint32_t g = 0; g < kGroups; ++g)
            if (partRows[row * kGroups + g] == block) return g;
    throw std::runtime_error("player model: block " + std::to_string(block) + " is in no part group");
}
// bone i of a group
int groupBone(uint32_t group, uint32_t i) {
    uint32_t first = 0;
    for (uint32_t g = 0; g < group; ++g) first += groupCounts[g];
    return groupBones[first + i];
}
} // namespace

// the player's model from its body file (and the weapon file's arm): see player_model.hpp
PlayerModel decodePlayerModel(const GameFile& pl00p, const GameFile* pl00r) {
    std::span<const uint8_t> seg0;
    for (const FilePart& p : pl00p.parts)
        if (p.kind == FilePart::Kind::Code && p.arg == kSeg0Psx) seg0 = p.bytes;
    if (seg0.size() < 0x70) throw std::runtime_error("PL00P: no model segment at PS1 0x110800");
    PlayerModel pm;
    for (int i = 0; i < 4; ++i) pm.model.textures.push_back(rd32(seg0.data() + 0x60 + size_t(i) * 4));
    const uint8_t costume = seg0.size() > 0x71 ? seg0[0x71] : 0;
    const uint32_t blockRow = (costume < kCostumes ? costume : 0) * uint32_t(kBlocks);
    // The part headers hold offsets from PS1 0x110800 (segment 0's base). The special-weapon arm (block 3, past
    // segment 0) lies in the weapon file's segment loaded at PS1 0x113340 (PL00R##, exe 0x5d5c40): it is decoded in
    // an image of PS1 0x110800.. with that segment placed as in RAM.
    std::vector<uint8_t> image(seg0.begin(), seg0.end());
    if (pl00r)
        for (const FilePart& p : pl00r->parts)
            if (p.kind == FilePart::Kind::Code && p.arg == kWeaponPsx) {
                image.resize(kWeaponPsx - kSeg0Psx, 0);
                image.insert(image.end(), p.bytes.begin(), p.bytes.end());
            }
    // every block a part group draws, in the rows' order: the first row's (no special weapon, no buster), then the
    // arms the weapon slots swap in (see playerPartShown)
    std::vector<int> blocks;
    for (uint32_t k = 0; k < kRows * kGroups; ++k)
        if (std::find(blocks.begin(), blocks.end(), partRows[k]) == blocks.end()) blocks.push_back(partRows[k]);
    for (int b : blocks) {
        const uint32_t base = partBlocks[blockRow + uint32_t(b)] - kSeg0Pc;
        const uint32_t group = blockGroup(b);
        const uint32_t parts = groupCounts[group];
        // past the image: the special-weapon arm without a weapon file
        if (base + parts * kPartHeaderBytes > image.size()) continue;
        for (uint32_t i = 0; i < parts; ++i) {
            pm.model.parts.push_back(decodeSubmesh(image, base + i * kPartHeaderBytes));
            pm.partBone.push_back(groupBone(group, i));
            pm.partBlock.push_back(b);
        }
    }
    return pm;
}

bool playerPartShown(int block, int8_t subWeapon, int8_t buster) {
    // exe 0x5f3440: the blocks of row (sub weapon +0x18c > 1) + 2 * (buster +0x18d > 1) of 0x91bd3c
    const uint32_t row = uint32_t(subWeapon > 1) + 2 * uint32_t(buster > 1);
    for (uint32_t g = 0; g < kGroups; ++g)
        if (partRows[row * kGroups + g] == block) return true;
    return false;
}

namespace {
// The player's file tables (exe data): the body file's load index per costume * 6 + variant (0x8d37b4, 17 entries: the
// other player kinds' table 0x8d37d6 follows), the weapon file's load index per special weapon (0x8d37e2)
constexpr size_t kBodyFileCount = 17, kWeaponCount = 18;
const DataTable<ScalarTable<uint16_t, kBodyFileCount>> bodyFiles{TableId::PlayerBodyFiles};
const DataTable<ScalarTable<uint16_t, kWeaponCount>> weaponFiles{TableId::PlayerWeaponFiles};
} // namespace

namespace {
// the player files' block (one for the program)
constexpr uint8_t kMegaman = 0; // the player kind (+4) whose files these are
} // namespace

void playerFilesForget() { playerFiles().forgetAll(); }

std::vector<uint16_t> playerDatLoaded() {
    std::vector<uint16_t> v;
    const PlayerFiles& files = playerFiles();
    if (files.body != 0) v.push_back(files.body);
    if (files.weapon != 0) v.push_back(files.weapon);
    return v;
}

namespace {
// the load indices exe 0x5d5c40 picks for a look; the one table lookup behind playerLookFiles
// and playerDatRequest. hasWeapon = special weapon > 2 (the game then records / loads the table entry as it is)
constexpr int8_t kLastBuiltInWeapon = 2; // the special weapons above this have a file of their own
struct LookIndices {
    uint16_t body = 0, weapon = 0;
    bool hasWeapon = false;
};
LookIndices lookIndices(const port::PlayerLook& look) {
    LookIndices r;
    const uint32_t k = uint32_t(int32_t(int8_t(look.costume)) * 6 + int32_t(int8_t(look.costumeVariant)));
    r.body = bodyFiles[k];
    r.hasWeapon = look.specialWeapon > kLastBuiltInWeapon;
    if (r.hasWeapon) r.weapon = weaponFiles[uint32_t(look.specialWeapon)];
    return r;
}
} // namespace

std::optional<PlayerLookFiles> playerLookFiles(const port::PlayerLook& look) {
    if (look.kind != kMegaman) return std::nullopt; // the other player kinds (0x8d37d6) are not Megaman's files
    // outside the tables: the body table ends where the kind table begins (17 entries; exe 0x5d5c40 itself does not
    // bound the index), the weapon table has the 18 weapons 0..17
    const int32_t bodyIndex = int32_t(int8_t(look.costume)) * 6 + int32_t(int8_t(look.costumeVariant));
    if (bodyIndex < 0 || bodyIndex >= int32_t(kBodyFileCount) || look.specialWeapon >= int(kWeaponCount))
        return std::nullopt;
    const LookIndices i = lookIndices(look);
    PlayerLookFiles f;
    f.body = datFileName(i.body);
    if (i.hasWeapon) f.weapon = datFileName(i.weapon);
    return f;
}

std::vector<uint16_t> playerDatRequest() {
    std::vector<uint16_t> v;
    const port::Player& p = port::player();
    if (p.type != kMegaman) return v; // the other player kinds (0x8d37d6) are not Megaman's files
    port::PlayerLook look;
    look.kind = kMegaman;
    look.costume = uint8_t(p.helmetPart);
    look.costumeVariant = uint8_t(p.legPart);
    look.specialWeapon = p.specialWeapon;
    const LookIndices i = lookIndices(look);
    PlayerFiles& files = playerFiles();
    if (files.body != i.body) {
        files.body = i.body;
        v.push_back(i.body);
    }
    if (i.hasWeapon && files.weapon != i.weapon) {
        files.weapon = i.weapon;
        v.push_back(i.weapon);
    }
    return v;
}

} // namespace od
