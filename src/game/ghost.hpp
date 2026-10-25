// View-only remote players. Each ghost has its own loaded files (the local player's, with the look's body and weapon
// files mapped over them; unchanged blocks shared until written) and its own player; its pose runs on them with the
// local player's pose scratch and matrix stack kept as they were (the ghost's writes there are undone).
// PlayerPoseData captures the native file readers, frames and coordinate shift before the pose runs.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/file_data.hpp"
#include "game/player/player_snapshot.hpp"

namespace od::ghost {

constexpr int kMaxGhosts = 7;               // 8 players in total
constexpr uint32_t kActorBase = 0x7c000000; // + slot * kSlotStride: the ghost player's identity; frame bytes at +0x800
constexpr uint32_t kSlotStride = 0x1000;
constexpr uint32_t kFrameOffset = 0x800;
constexpr uint32_t actorAddr(int slot) { return kActorBase + uint32_t(slot) * kSlotStride; }

// a .DAT by file name (throws when it cannot); the bytes stay valid while the caller's cache lives (the host reads
// each file once, app/dat_cache)
using ReadFile = std::function<const GameFile&(const std::string& datName)>;

enum class Hidden { No, NotVisible, UnsupportedCharacter, MissingFile };
const char* hiddenReason(Hidden h);

// Slot contract: 0 <= slot < kMaxGhosts; any other slot throws std::out_of_range (pose, files, clear).
class GhostSet {
public:
    // Poses `slot` from `s`; its files are built from the bound loaded files (the local ones) when localGeneration or
    // the look changed since the last build. Never writes the local files. A ghost that cannot be shown returns the
    // reason and is not drawn: callers draw only on Hidden::No (files() can be non-null while the slot is NotVisible:
    // the files stay valid, only the pose is skipped) and log NotVisible / UnsupportedCharacter themselves via
    // hiddenReason. MissingFile is logged once here and not retried until localGeneration or the look changes. There
    // is no stand-in model.
    Hidden pose(int slot, uint32_t localGeneration, const port::PlayerSnapshot& s, const ReadFile& read);
    const LoadedFileData* files(int slot) const; // null when the slot has no files built
    LoadedFileData* files(int slot);             // ... to bind them (the host's draw reads the ghost's model data)
    const port::Player* player(int slot) const;  // the ghost's posed player (null before its first pose)

    void clear(int slot);

private:
    struct Slot {
        std::optional<LoadedFileData> files;
        std::unique_ptr<port::Player> player;
        uint32_t generation = 0;
        port::PlayerLook look;
        bool failed = false;        // the last build for (failedGeneration, failedLook) could not read a file
        uint32_t failedGeneration = 0;
        port::PlayerLook failedLook;
    };
    std::array<Slot, kMaxGhosts> slots_;
};

} // namespace od::ghost
