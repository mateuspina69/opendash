// A game file as the game reads it, whichever source holds it (docs/ps1_disc.md): its parts in the file's order,
// each a kind of data. The PC .DAT (engine/dat) and the PS1 disc file (engine/ps1_container) carry the same room,
// model, code and image payloads; text and sound are in each version's own format, so they are kinds of their own
// and a reader takes only the kind it reads.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace od {

struct VramRect {
    int16_t x = 0, y = 0, w = 0, h = 0; // as the loaders read them (s16)
    bool clut = false;           // the CLUT upload of a page (Maybe_UploadClut), else an image
    std::vector<uint8_t> pixels; // rows of w 16-bit words
};

struct FilePart {
    enum class Kind {
        Code,        // data at a PS1 address (arg: the address, PS1 RAM offset); PC 0 / PS1 0x01
        Models,      // the room's actor model bank; PC 1 / PS1 0x0a, 0x0c
        Geometry,    // the room geometry block (or a ST##T's demo pad recording); PC 2 / PS1 0x0d
        Vram,        // VRAM uploads (rects); PC 3 / 4 / 5 / PS1 0x02, 0x03 (arg: the PC CLUT destination)
        PcMessages,  // the message / script bank, PC text (JP); PC 6
        PcFont,      // a font's 1bpp glyphs (arg: the font, extra: the cell size); PC 7
        WavSfxBank,  // an SFX bank of RIFF WAVs (arg: the slot, extra: the bank id); PC 8
        Mp3Music,    // a music track as MP3 (arg: the loop flag); PC 9
        Ps1Messages, // the message / script bank, the PS1 script in the PS1 font's encoding; PS1 0x12
        Ps1Block,    // a PS1 block no reader takes yet (arg: its type; bytes: the whole block, header included)
        Ps1Music,    // ODSM packet: disc SEQ + its sampled instrument bank (arg: loop flag)
    };
    Kind kind = Kind::Code;
    uint32_t arg = 0, extra = 0;
    std::vector<uint8_t> bytes; // the payload (not Vram)
    std::vector<VramRect> rects; // Vram
};

struct GameFile {
    std::string name; // the game's name for it ("ST4B.DAT")
    std::vector<FilePart> parts;

    // the first part of the kind (nullptr: none)
    const FilePart* first(FilePart::Kind kind) const;
    // the first part of the kind; throws std::runtime_error naming the file when it has none
    const FilePart& require(FilePart::Kind kind) const;
    bool has(FilePart::Kind kind) const { return first(kind) != nullptr; }
};

// A PC .DAT's parts (engine/dat: the TOC's segments by type); throws std::runtime_error on a malformed container or
// a segment type the PC loader (Maybe_ParseDatContainer 0x40eb90) does not have.
GameFile pcGameFile(std::string name, std::span<const uint8_t> dat);
// A PS1 disc file's parts (engine/ps1_container); throws std::runtime_error on a malformed block.
GameFile ps1GameFile(std::string name, std::span<const uint8_t> bin);

} // namespace od
