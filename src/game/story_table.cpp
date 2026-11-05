// The table of what advances the story (see story_table.hpp).
#include "game/story_table.hpp"

#include <array>

namespace od::port {

namespace {

struct StoryPlace {
    uint8_t stage;
    int sub;
    uint8_t story;
    StoryPlaceKind kind;
};
using K = StoryPlaceKind;

// From the classes that raise a boss gauge and the room scripts that spawn them; the story where the class says so
constexpr std::array kPlaces = {
    StoryPlace{0x25, 6, 3, K::Boss},          // Tron's tank, Pokte (content/calbania/tron_boss)
    StoryPlace{0x1d, 2, kAnyStory, K::Boss},  // the elephant's arena (content/forbidden_island/elephant_boss)
    StoryPlace{0x11, 0, 1, K::Boss},          // content/forbidden_island/st11_boss
    StoryPlace{0x45, 1, kAnyStory, K::Boss},  // content/late_game/st45_boss
    StoryPlace{0x14, 4, 4, K::Boss},          // the Manda ruin's guardian (content/manda/manda_boss)
    StoryPlace{0x13, 1, kAnyStory, K::Boss},  // content/manda/manda_guardian, after script 9
    StoryPlace{0x12, 5, kAnyStory, K::Boss},  // ... its arena after script 0xb
    StoryPlace{0x37, 6, kAnyStory, K::Boss},  // the gauge reaverbots (content/nino/gauge_reaverbots)
    StoryPlace{0x35, 5, kAnyStory, K::Boss},  // the guardian under Nino (content/nino/ruin_guardian)
    StoryPlace{0x35, 0, kAnyStory, K::Boss},
    StoryPlace{0x28, 0, kAnyStory, K::Boss},  // the fire ruin's gunner and lava bosses (content/saul_kada)
    StoryPlace{0x3c, 2, 12, K::Boss},         // Teisel's gold statue (content/saul_kada/st3c_gold_boss)
    StoryPlace{0x0e, 2, kAnyStory, K::Boss},  // the boss vehicle (content/yosyonke/st0e_boss_vehicle)
    StoryPlace{0x0e, 5, kAnyStory, K::Boss},
    // their gauge's class gives the stage, not the room
    StoryPlace{0x2b, kAnySub, 12, K::Boss},   // the pirates' mine (content/saul_kada/st2b_cannon_boss)
    StoryPlace{0x44, kAnySub, kAnyStory, K::Boss}, // its last gauge reaverbot sets flag 0x172
    StoryPlace{0x4b, kAnySub, kAnyStory, K::Boss}, // content/late_game/st4b
    StoryPlace{0x4c, kAnySub, kAnyStory, K::Boss}, // content/late_game/st4c_boss
    StoryPlace{0x5c, kAnySub, kAnyStory, K::Boss}, // content/late_game/st5c
    // the missions
    StoryPlace{0x55, kAnySub, kAnyStory, K::Mission}, // the licence exam's clock (content/manda/exam_spawner)
    StoryPlace{0x1e, kAnySub, kAnyStory, K::Mission}, // the fires and room timers (content/intro/fire_mission_rooms)
    StoryPlace{0x53, kAnySub, kAnyStory, K::Mission}, // ST0E -> ST53 -> ST0E, the defended vehicle's life
    StoryPlace{0x30, 2, kAnyStory, K::Mission},       // the escape's timer (content/calbania/ch3_story)
    StoryPlace{0x31, kAnySub, kAnyStory, K::Mission}, // the alarm (content/calbania/ch3_story)
    StoryPlace{0x27, 8, 13, K::Mission},              // the lava valve (content/saul_kada/st27_mission)
    StoryPlace{0x15, kAnySub, kAnyStory, K::Mission}, // Nino's missions (content/nino/ch2_rooms)
    StoryPlace{0x16, kAnySub, kAnyStory, K::Mission},
    StoryPlace{0x17, kAnySub, kAnyStory, K::Mission},
    StoryPlace{0x18, kAnySub, kAnyStory, K::Mission},
    StoryPlace{0x59, kAnySub, kAnyStory, K::Mission}, // the race (content/late_game/st59)
};

} // namespace

StoryPlaceKind storyPlaceOf(uint8_t stage, int sub, uint8_t story) {
    for (const StoryPlace& p : kPlaces) {
        if (p.stage != stage || (p.sub != kAnySub && p.sub != sub) || (p.story != kAnyStory && p.story != story)) continue;
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

} // namespace od::port
