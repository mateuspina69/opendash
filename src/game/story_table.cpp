// The table of what advances the story (see story_table.hpp).
#include "game/story_table.hpp"

#include <algorithm>
#include <array>

#include "game/story_flags.hpp"

namespace od::port {

namespace {

constexpr uint32_t kNoFlag = 0xffffffffu;

struct StoryPlace {
    uint8_t stage;
    int sub;
    uint8_t story;
    StoryPlaceKind kind;
    uint32_t beaten; // the flag its class sets at the end
};
using K = StoryPlaceKind;

// From the classes that raise a boss gauge and the room scripts that spawn them
constexpr std::array kPlaces = {
    StoryPlace{0x25, 6, 3, K::Boss, 0x589},           // Tron's tank (content/calbania/tron_boss)
    StoryPlace{0x1d, 2, kAnyStory, K::Boss, 0x682},   // the elephant's arena (content/forbidden_island/elephant_boss)
    StoryPlace{0x11, 0, 1, K::Boss, kNoFlag},         // content/forbidden_island/st11_boss: the room's story is the record
    StoryPlace{0x45, 1, kAnyStory, K::Boss, 0x176},   // content/late_game/st45_boss
    StoryPlace{0x14, 4, 4, K::Boss, 0x5b5},           // the Manda ruin's guardian (content/manda/manda_boss)
    StoryPlace{0x13, 1, kAnyStory, K::Boss, 0x5b1},   // content/manda/manda_guardian, after script 9
    StoryPlace{0x12, 5, kAnyStory, K::Boss, 0x5b1},   // ... its arena after script 0xb
    StoryPlace{0x37, 6, kAnyStory, K::Boss, kNoFlag}, // the gauge reaverbots (content/nino/gauge_reaverbots)
    StoryPlace{0x44, 2, kAnyStory, K::Boss, 0x172},
    StoryPlace{0x35, 5, kAnyStory, K::Boss, 0x94},    // the guardian under Nino (content/nino/ruin_guardian)
    StoryPlace{0x35, 0, kAnyStory, K::Boss, 0x95},
    StoryPlace{0x28, 0, kAnyStory, K::Boss, 0x4e},    // the fire ruin's gunner and lava bosses (content/saul_kada)
    StoryPlace{0x3c, 2, 12, K::Boss, 0x6f},           // Teisel's gold statue (content/saul_kada/st3c_gold_boss)
    StoryPlace{0x2b, 1, 12, K::Boss, 0x5cc},          // the pirates' mine (content/saul_kada/st2b_cannon_boss)
    StoryPlace{0x0e, 2, kAnyStory, K::Boss, 0x587},   // the boss vehicle (content/yosyonke/st0e_boss_vehicle)
    StoryPlace{0x0e, 5, kAnyStory, K::Boss, 0x587},
    StoryPlace{0x0f, 12, 0, K::Boss, kNoFlag},        // the first ruin's guardian, script 0x52 (game/guardian_reaverbot)
    StoryPlace{0x4b, 5, kAnyStory, K::Boss, 0x681},   // content/late_game/st4b
    StoryPlace{0x4c, 3, kAnyStory, K::Boss, 0x177},   // content/late_game/st4c_boss
    StoryPlace{0x5c, 0, kAnyStory, K::Boss, 0x680},   // content/late_game/st5c
    // the missions
    StoryPlace{0x55, kAnySub, kAnyStory, K::Mission, kNoFlag}, // the licence exam's clock (content/manda/exam_spawner)
    StoryPlace{0x1e, kAnySub, kAnyStory, K::Mission, kNoFlag}, // the fires and room timers (content/intro/fire_mission_rooms)
    StoryPlace{0x53, kAnySub, kAnyStory, K::Mission, kNoFlag}, // ST0E -> ST53 -> ST0E, the defended vehicle's life
    StoryPlace{0x30, 2, kAnyStory, K::Mission, kNoFlag},       // the escape's timer (content/calbania/ch3_story)
    StoryPlace{0x31, kAnySub, kAnyStory, K::Mission, kNoFlag}, // the alarm (content/calbania/ch3_story)
    StoryPlace{0x27, 8, 13, K::Mission, kNoFlag},              // the lava valve (content/saul_kada/st27_mission)
    StoryPlace{0x15, kAnySub, kAnyStory, K::Mission, kNoFlag}, // Nino's missions (content/nino/ch2_rooms)
    StoryPlace{0x16, kAnySub, kAnyStory, K::Mission, kNoFlag},
    StoryPlace{0x17, kAnySub, kAnyStory, K::Mission, kNoFlag},
    StoryPlace{0x18, kAnySub, kAnyStory, K::Mission, kNoFlag},
    StoryPlace{0x59, kAnySub, kAnyStory, K::Mission, kNoFlag}, // the race (content/late_game/st59)
};

// The scripts that advance the story's byte (exe 0x512800 in the script or at its start in the room script)
// and those a story boss's fight follows
constexpr std::array<uint8_t, 34> kGatedScripts = {
    0x13, 0x1c, 0x24, 0x31, 0x38, 0x3b, 0x3f, 0x54, 0x5e, 0x62, 0x65, 0x66, 0x67, 0x69, // the story's byte
    0x09, 0x0b, 0x1a, 0x1b, 0x28, 0x2a, 0x42, 0x52, 0x59, 0x5a, 0x5b, 0x5d, 0x6b, 0x6c, 0x6d, 0x75, // a boss's fight
    0x37, 0x39, 0x3a, 0x3e, // the story's flights and attack (content/nino/ch2_rooms, content/calbania/nino_return_rooms)
};

} // namespace

StoryPlaceKind storyPlaceOf(uint8_t stage, int sub, uint8_t story) {
    for (const StoryPlace& p : kPlaces) {
        if (p.stage != stage || (p.sub != kAnySub && p.sub != sub) || (p.story != kAnyStory && p.story != story)) continue;
        if (p.beaten != kNoFlag && storyFlag(p.beaten)) return K::Free;
        return p.kind;
    }
    return K::Free;
}

bool storyDoorGated(uint8_t fromStage, uint8_t toStage, int toSub, uint8_t story) {
    if (storyPlaceOf(toStage, toSub, story) == K::Free) return false;
    for (const StoryPlace& p : kPlaces)
        if (p.stage == toStage && p.sub == kAnySub) return fromStage != toStage;
    return true;
}

bool storyScriptGated(uint8_t script) { return std::find(kGatedScripts.begin(), kGatedScripts.end(), script) != kGatedScripts.end(); }

} // namespace od::port
