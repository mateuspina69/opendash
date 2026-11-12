// Subtitles for the voiced lines the game shows no text for (the cutscenes' voices: a script's voice clip, no message
// window; OpenDash's own). A language pack folder's legendas.tsv gives the lines of its own voices, timed from the
// clip's start: one line per text, `file<TAB>track<TAB>start s<TAB>end s<TAB>text` ("xa02_37.dat 3 0.00 2.40 ..."; #
// comments; legendas_<voice>.tsv: the same text timed to another language's voices; "{漢字|かんじ}": furigana over the
// kanji, drawn with the full M+ font when assets/fonts has it). While the clip plays
// (game/audio_logic voiceClipInfo / voiceClipPlaying) the line of its time is drawn at the bottom of the screen; never
// for a clip a message window speaks (it has its text).
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "app/ui/ui_text.hpp"

namespace od::app {

struct Subtitles {
    struct Line {
        float start = 0, end = 0;
        std::string text;
    };
    bool enabled = true;  // subtitles= 0|1 (opendash.ini)
    UiText font;

    // the lines of a subtitles file (none: no subtitles)
    void load(const std::filesystem::path& file);
    // the file for a text language and a voice language: lang/<text>/legendas.tsv when they are the same (the lines
    // timed to their own voices), else lang/<text>/legendas_<voice>.tsv (the text timed to the other voices)
    // direct: with other voices, legendas_<voice>_direto.tsv when there is one -- the text translated straight from
    // those voices (by AI), not the dub's own lines
    static std::filesystem::path fileFor(const std::filesystem::path& langDir, const char* text, const char* voice,
                                         bool direct);
    bool direct = false;  // subtitles_direct= 0|1
    // one frame: the line of the clip playing (if any) at the bottom of a width x height screen
    void draw(od::Renderer& r, ui::Canvas& cv, float width, float height);
    void flush(od::Renderer& r) { font.flush(r); }

private:
    std::filesystem::path loaded_;
    std::filesystem::path fontFile_; // the font `font` holds (a pack's font.txt names its own: Korean, Chinese)
    bool loadedOnce_ = false;
    std::map<std::string, std::vector<Line>> lines_; // "xa02_37.dat#3"
    uint32_t serial_ = 0;
    uint64_t startedNs_ = 0;
    const std::vector<Line>* current_ = nullptr;
    // the SFX banks' voice barks (legendas_sfx*.tsv): by sample key, end = its length; the one heard last
    std::map<std::string, Line> sfx_;
    uint32_t sfxSerial_ = 0;
    uint64_t sfxNs_ = 0;
    const Line* sfxLine_ = nullptr;
};

} // namespace od::app
