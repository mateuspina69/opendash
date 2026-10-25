// Megaman (Volnutt) mesh from PL00P000.DAT segment 0 (PS1 0x110800). Textures: the table at +0x60 holds
// (clut << 16) | tpage entries like the banks (0x3C000005 ...), sampled from VRAM after PL00T.DAT is
// uploaded (od::Vram). Placement: bone matrices of the pose (od::port::playerPose): world = t + M * v /
// 4096.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "engine/bank.hpp"
#include "game/dat_names.hpp"
#include "game/loaded_data.hpp"
#include "game/player/player_model_state.hpp"
#include "game/player/player_snapshot.hpp"

namespace od {

struct PlayerModel {
    BankModel model;           // parts in PART_DIRECTORY order + the 4-entry texture table
    std::vector<int> partBone; // bone of each part (0 body, 1 head ... 14 R foot; see player_pose.hpp)
    std::vector<int> partBlock; // the draw block of each part (exe 0x5f3440): 0 body, 1 head,
                                // 2 / 3 left arm (hand / special-weapon arm), 4 / 5 right arm (hand / buster), 6 feet
};

// pl00p = the whole PL00P0xx.DAT body file, pl00r = the special weapon's PL00R##.DAT (or empty)
// parts 0..16 = PART_DIRECTORY (the hands), then the buster right arm (block 5) of the model's costume (seg0
// +0x71); the special-weapon arm (block 3, PC 0x743340.. = PS1 0x113340..) is not in segment 0: it comes with the
// weapon file (its type-0 segment loaded at PS1 0x113340), decoded when pl00r is given
PlayerModel decodePlayerModel(const GameFile& pl00p, const GameFile* pl00r = nullptr);
// exe 0x5f3440: whether a part block is drawn for the weapon slots (player +0x18c special weapon,
// +0x18d buster): the buster arm replaces the right hand when +0x18d > 1 (the field buster; the town's 1 = the kick),
// the special-weapon arm the left hand when +0x18c > 1. (Per-bone hide mask +0x294 and the carried-object hands
// +0x1a5 / +0x1a6 are not modelled.)
bool playerPartShown(int block, int8_t subWeapon, int8_t buster);

// The game's player files: exe 0x5d5c40 queues, for Megaman (kind byte 0xa71d24 == 0), the body file [0x8d37b4 + (+0x1a2 * 6 + +0x1a3) * 2] (PL00P010..015 with +0x1a2
// == 0, PL00P000..005 with 1, and more entries for +0x1a2 >= 2: the table has 17 entries) unless it is the loaded one 0xa6d1d8, and with the saved special weapon +0x18e
// > 2 the weapon file [0x8d37e2 + w * 2] (PL00R02..10 = weapon 3..17; weapon 15 = PL00R0E: the arm model at PS1 0x113340, the arm animations 0x60..0x6f at 0x1246b0, its
// SFX bank) unless it is 0xa6d1da. A load index i names the file [0x8886d4 + i * 4] (exe 0x40efe0 -> 0x40ef60: game/dat_names datFileName). the load indices of the
// player files the game holds now (0xa6d1d8, and 0xa6d1da when != 0)
std::vector<uint16_t> playerDatLoaded();
struct PlayerLookFiles {
    std::string body, weapon; // weapon is empty when the look has no PL00R
};
// the files exe 0x5d5c40 would load for this look (pure: reads only the data source's tables
// 0x8d37b4 / 0x8d37e2 / 0x8886d4); nullopt for kind != 0 (no other character is supported yet) and for a look
// outside the tables (costume * 6 + variant not in the 17-entry body table 0x8d37b4..0x8d37d6, special weapon > 17;
// the game's code does not bound them, the table ends are the limits). With special weapon > 2 the weapon is the
// name of the table entry as it is, exactly what the game loads (all of 3..17 are nonzero in the exe)
std::optional<PlayerLookFiles> playerLookFiles(const port::PlayerLook& look);
// exe 0x5d5c40 for Megaman: the indices to load (and records them in 0xa6d1d8 / 0xa6d1da)
std::vector<uint16_t> playerDatRequest();
// the loaded body file's costume byte (its segment 0 at 0x740800, +0x71): the player's variant
inline uint8_t loadedBodyVariant() { return port::loadedScalar<uint8_t>(0x740871); }
// exe 0x543010 / 0x5433f0 (the boot, the hand-over to the game loop): the load indices of the player files (0xa6d1d8 /
// 0xa6d1da and the two words before them, whose reader is not traced) set to kNoPlayerFile, first to last
void playerFilesForget();

} // namespace od
