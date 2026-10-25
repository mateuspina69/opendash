#include "engine/file_data.hpp"

#include "engine/game_file.hpp"

namespace od {

namespace {
LoadedFileData* gBound = nullptr;
} // namespace

void LoadedFileData::mapCode(const GameFile& file) {
    for (const FilePart& p : file.parts)
        if (p.kind == FilePart::Kind::Code && p.arg != 0) map(kPsxBase + p.arg, p.bytes);
}

LoadedFileData& loadedFiles() {
    if (!gBound) throw std::logic_error("loaded files: no store bound (the host binds one before the game runs)");
    return *gBound;
}
bool loadedFilesBound() { return gBound != nullptr; }
void bindLoadedFiles(LoadedFileData* files) { gBound = files; }

ScopedLoadedFiles::ScopedLoadedFiles(LoadedFileData& files) : previous_(gBound) { gBound = &files; }
ScopedLoadedFiles::~ScopedLoadedFiles() { gBound = previous_; }

} // namespace od
