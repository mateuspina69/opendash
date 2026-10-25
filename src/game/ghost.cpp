// Ghost players: their own loaded files and player, an isolated pose for each slot (see ghost.hpp).
#include "game/ghost.hpp"

#include <cstdio>
#include <exception>

#include "engine/game_file.hpp"
#include "game/geometry_scratch.hpp"
#include "game/player/player_model.hpp"
#include "game/player/player_pose.hpp"
#include "game/player/player_state.hpp"
#include "game/stage_state.hpp"

namespace od::ghost {

const char* hiddenReason(Hidden h) {
    switch (h) {
    case Hidden::No: return "shown";
    case Hidden::NotVisible: return "not drawn (draw flags +0 & +1 & 2 clear)";
    case Hidden::UnsupportedCharacter: return "character not supported (kind != 0 or look outside the tables)";
    case Hidden::MissingFile: return "player file missing";
    }
    return "?";
}

Hidden GhostSet::pose(int slot, uint32_t localGeneration, const port::PlayerSnapshot& s, const ReadFile& read) {
    Slot& g = slots_.at(size_t(slot));
    if (!(s.drawFlags[0] & s.drawFlags[1] & port::kActorDrawn)) return Hidden::NotVisible; // the files stay valid
    const auto files = playerLookFiles(s.look); // reads only the data source's tables
    if (!files) {
        g.files.reset();
        return Hidden::UnsupportedCharacter;
    }
    if (g.failed && g.failedGeneration == localGeneration && g.failedLook == s.look) return Hidden::MissingFile;
    if (!g.files || g.generation != localGeneration || !(g.look == s.look)) {
        // body + weapon files of the look are the ghost's own; the rest is the local files as they are now. The local
        // player's own files (0xa6d1d8 / 0xa6d1da) are cleared first: a look without a PL00R (special weapon <= 2)
        // would otherwise pose from the local's weapon data -- its arm animations 0x60..0x6f (table 0x751e20 =
        // PS1 0x121e20, exe 0x5722c0), arm model (0x743340) and 0x7546b0 come with the PL00R. Zero = no PL00R
        // loaded. A local file that is also one of the look's is skipped (the look maps the same segments).
        LoadedFileData own = loadedFiles(); // the local blocks, shared until written
        std::string localFile;
        const std::string* current = &localFile;
        try {
            for (const uint16_t i : playerDatLoaded()) {
                localFile = datFileName(i);
                if (localFile.empty() || localFile == files->body || localFile == files->weapon) continue;
                for (const FilePart& p : read(localFile).parts)
                    if (p.kind == FilePart::Kind::Code && p.arg != 0)
                        own.zero(LoadedFileData::kPsxBase + p.arg, uint32_t(p.bytes.size()));
            }
            current = &files->body;
            auto loadLook = [&](const std::string& name) {
                const GameFile& file = read(name);
                for (const FilePart& p : file.parts)
                    if (p.kind == FilePart::Kind::Code && p.arg != 0)
                        own.zero(LoadedFileData::kPsxBase + p.arg, uint32_t(p.bytes.size()));
                own.mapCode(file);
            };
            loadLook(files->body);
            if (!files->weapon.empty()) {
                current = &files->weapon;
                loadLook(files->weapon);
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "ghost %d hidden: cannot read %s (%s)\n", slot, current->c_str(), e.what());
            g.files.reset();
            g.failed = true;
            g.failedGeneration = localGeneration;
            g.failedLook = s.look;
            return Hidden::MissingFile;
        }
        g.failed = false;
        g.files = std::move(own);
        g.generation = localGeneration;
        g.look = s.look;
    }
    const auto poseData = port::playerPoseData(*g.files, s, port::stage().coordinateShift);
    const uint32_t actor = actorAddr(slot);
    // the pose runs on the ghost's files; the local pose scratch and matrix stack are put back after it
    const ScopedLoadedFiles ghostFiles(*g.files);
    const port::GeometryScratch scratch = port::geometryScratch();
    const port::PoseStack stack = port::poseStack();
    if (!g.player) {
        g.player = std::make_unique<port::Player>(); // the ghost's player (not the local one)
        g.player->at = actor;
    }
    port::applyPlayerSnapshot(*g.player, actor + kFrameOffset, s);
    port::playerPose(*g.player, poseData);
    port::geometryScratch() = scratch;
    port::poseStack() = stack;
    return Hidden::No;
}

const port::Player* GhostSet::player(int slot) const { return slots_.at(size_t(slot)).player.get(); }

const LoadedFileData* GhostSet::files(int slot) const {
    const Slot& g = slots_.at(size_t(slot));
    return g.files ? &*g.files : nullptr;
}

LoadedFileData* GhostSet::files(int slot) {
    Slot& g = slots_.at(size_t(slot));
    return g.files ? &*g.files : nullptr;
}

void GhostSet::clear(int slot) { slots_.at(size_t(slot)) = Slot{}; }

} // namespace od::ghost
