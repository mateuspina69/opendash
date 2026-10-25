// The ghost players as the host draws them: per slot the state to show (od::port::RemotePlayerState: room,
// loaded-data fingerprint, snapshot), posed after the tick loop on its own files and player (od::ghost::GhostSet,
// never the local ones) and drawn with its look's mesh (app/ghost_looks: built alone when a slot first shows it,
// dropped when no slot does). app/netplay sets the slots from their smoothing buffers (a network peer or the dev
// mirror). A ghost that cannot be shown is hidden with a logged reason (no stand-in model).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "app/ghost_looks.hpp"
#include "engine/vram.hpp"
#include "game/ghost.hpp"
#include "game/player/player_snapshot.hpp"
#include "render/renderer.hpp"

namespace od::app {

// The room upload the ghosts are drawn into: its generation (g_roomVramGen at its load), its slice count, its VRAM
// (the looks decode their slices from it) and its coordinate shift (RoomMesh::shift)
struct GhostRoomUpload {
    uint32_t generation = 0;
    int slices = 0;
    const od::Vram* vram = nullptr;
    int shift = 0;
};

// Slot contract: 0 <= slot < od::ghost::kMaxGhosts; any other slot throws std::out_of_range (setRemote, clearRemote,
// status). Validate slot indices from the network before calling.
class GhostView {
public:
    void setRemote(int slot, const od::port::RemotePlayerState& s); // the state a slot shows
    void clearRemote(int slot);
    // after the tick loop: poses every slot in the local room in its own files; a slot whose loaded-data fingerprint
    // differs from localFingerprint (game/loaded_data_fingerprint) is hidden. The looks of the posed slots are
    // made (app/ghost_looks: a new one built and its slices sent to `sink` now, no room rebuild), the others dropped.
    void update(const GhostRoomUpload& room, uint64_t localFingerprint, const od::ghost::ReadFile& read,
                const SliceSink& sink);
    // the shown ghosts, objectId(kKindPlayer, 1 + slot); none while the room upload is newer than the last update
    // (the looks' slices belong to the upload they were made for)
    void appendDynamic(std::vector<od::DynamicObject>& dyn, uint32_t roomGeneration) const;
    std::string status(int slot) const; // "shown" or the hide reason
    // the state a slot shows when it was shown at the last update (its room and loaded data the local ones), else null
    const od::port::RemotePlayerState* shownState(int slot) const;
    const GhostLooks& looks() const { return looks_; }

private:
    // Why a slot is (not) drawn; logged once per change. The log text is derived from it (stateText).
    enum class State : uint8_t {
        NoRemote, Shown, NotFieldPlay, OtherRoom, LoadedDataDiffers, HiddenByPose, LookNotBuilt
    };
    static const char* stateText(State st, od::ghost::Hidden h);
    struct Slot {
        bool remote = false;
        od::port::RemotePlayerState remoteState;
        bool shown = false;
        State state = State::NoRemote;                    // the last logged one
        od::ghost::Hidden hidden = od::ghost::Hidden::No; // valid when state == HiddenByPose
        std::string lookFailure;                           // valid when state == LookNotBuilt
    };
    std::array<Slot, od::ghost::kMaxGhosts> slots_;
    od::ghost::GhostSet set_;
    GhostLooks looks_;
    uint32_t generation_ = 0; // the room upload the looks belong to
    int shift_ = 0;
};

} // namespace od::app
