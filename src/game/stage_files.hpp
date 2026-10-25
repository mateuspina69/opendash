// The stage's files: the per-stage selectors exe 0x5d5be0 runs before a stage's DAT load ([0x8d3af0 + stage * 4],
// exe 0x5d5cd0) that rewrite the stage's geometry / texture file ids in the exe's tables (written in the game state,
// as the exe does) by the story and its flags, and the DAT names of the ids. Checked by test_stage_files.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "game/state_array.hpp"

namespace od::port {

// The stages' file ids by stage (game state the selectors rewrite; they start as the data source's tables
// TableId::StageGeometryFiles / StageTextureFiles; their DAT names: game/dat_names)
constexpr size_t kStageFileIds = 0x5e;
struct StageFileTables {
    StateArray<uint16_t, kStageFileIds> geometry{};
    StateArray<uint16_t, kStageFileIds> texture{};
};
// the tables (loaded from the bound data source on first use)
StageFileTables& stageFileTables();

// The DAT loader's state the game logic waits on (the loads themselves are the host's)
struct FileLoadState {
    uint32_t loading = 0; // kFileLoading | kStreamBusy | kLoaderBit7

    // the flags of the word's low half cleared (the exe's word store: the tutorial's return to the title)
    void clearLowFlags(uint16_t mask) { loading &= ~uint32_t(mask); }
};
constexpr uint8_t kFileLoading = 0x01; // a file request is still loading
constexpr uint8_t kStreamBusy = 0x02;  // (hypothesis) the disc stream is busy: the voice clips wait for it to clear
constexpr uint8_t kLoaderBit7 = 0x80;  // a loader flag whose setter is not traced (cleared with kStreamBusy)
inline FileLoadState& fileLoads() {
    static FileLoadState s;
    return s;
}

// The DAT load queue (exe 0xa79fe0: four requests {kind, file id}, drained by the host after the frame)
struct DatLoadRequest {
    uint16_t kind = 0; // 0: free; kDatLoadFile
    uint16_t fileId = 0;
};
constexpr uint16_t kDatLoadFile = 1;
struct DatLoadQueue {
    StateArray<DatLoadRequest, 4> requests{};

    // the requests cleared (the host's drain)
    void clear() { requests.fill(DatLoadRequest{}); }
};

inline DatLoadQueue& datLoadQueue() {
    static DatLoadQueue s;
    return s;
}
// exe 0x40efe0(id): file `id` queued in the first free request (none free: not queued), the loader busy
void datLoadRequest(uint16_t id);

// The two buffers of the stage's type-1 bank (StageState::bankBuffer picks the one in use)
constexpr uint32_t kBankBufferA = 0x754800, kBankBufferB = 0x76c800;
// exe 0x5f3200: the bank of the buffer in use walked: its leading animation records are the player's stage animations
// (the last one with header bit 31; the last with bit 30: the second, + 4), the first other record is the model bank;
// the stage animation marker reset, the hands' shapes cleared
void stageBankWalk();
// the other buffer of the type-1 bank switched to and walked (exe 0x5f3200: a cutscene's DAT loaded into it)
void switchStageBank();
// exe 0x5f3200 as the status sub-screen runs it (its pages' banks): stageBankWalk's walk without the model bank's store
// (OpenDash keeps the field's model bank -- the host's room copy -- and the equipment page swaps it itself), stopping at
// the end of the mapped image or at a zero-length record (OpenDash maps a page's bank on its first load: before that
// there is nothing to walk). Returns the model bank the walk reached (0: nothing walked)
uint32_t statusBankWalk();

// exe 0x5d5be0's per-stage selector (exe 0x5d5cd0(stage)) and the names of the stage's geometry / texture files. Every
// selector of the table is ported (the other stages' entry is a plain ret: the tables' own ids). false = a name is
// empty.
bool stageDatFiles(int stage, std::string& geometry, std::string& texture);

} // namespace od::port
