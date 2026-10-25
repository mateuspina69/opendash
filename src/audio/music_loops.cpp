// Do not edit.
#include "audio/music_loops.hpp"

#include <algorithm>
#include <array>

namespace od::audio {
namespace {

// {FNV-1a 64 of the MP3, decoded frames, loop start, loop end} // first DAT, rule
constexpr std::array<MusicLoop, 72> kLoops{{
    {0x0440fda382267f05ull, 2717568u, 1105u, 2717542u}, // ST1001T whole
    {0x06718393c99f21c9ull, 1128960u, 1105u, 1128238u}, // ST20T body
    {0x080b35a33e1590e2ull, 945792u, 1105u, 945194u}, // ST0AT whole
    {0x0c53322335c77aa5ull, 1889280u, 1105u, 1889258u}, // ST0E01T whole
    {0x0dd9a17da275b435ull, 1575936u, 1105u, 1575130u}, // ST25T whole
    {0x0e771c94a4098ed6ull, 2655360u, 1105u, 2653559u}, // ST2DT body
    {0x0fb9aa96969e72faull, 1491840u, 1105u, 1490744u}, // ST26T whole
    {0x0fffeb9dc9f00065ull, 2691072u, 1105u, 2690250u}, // ST2801 whole
    {0x12f96ed44e2fc922ull, 1700352u, 839536u, 1688708u}, // ST2502T repeat
    {0x18a059a4e304f8e8ull, 1534464u, 1105u, 1534464u}, // ST15T body
    {0x219c2d08d5e73a3dull, 3538944u, 674549u, 3505124u}, // ST3501T repeat
    {0x25fb12b00834f59dull, 2024064u, 262829u, 1880090u}, // ST0F01 repeat
    {0x2a514b27cc6cd150ull, 930816u, 1105u, 930816u}, // ST1201 body
    {0x32d15e94f72c1417ull, 1558656u, 698441u, 1547614u}, // ST0DT repeat
    {0x33384739fe57d5e3ull, 1817856u, 1105u, 1817475u}, // ST1D02 whole
    {0x3d6462575cc027f8ull, 3284352u, 184919u, 3272753u}, // ST0DT found
    {0x4464eb054ce4ca6dull, 2130048u, 1105u, 2130048u}, // ST4ET whole
    {0x49c60b0668b6f28dull, 1891584u, 1105u, 1891103u}, // ST1705 whole
    {0x52a8c0a2566bcd75ull, 4397184u, 1105u, 4396741u}, // ST3FT whole
    {0x5be9a6645e076120ull, 1593216u, 1105u, 1593199u}, // ST1F01T body
    {0x6031e69ba1ce3cdcull, 632448u, 1105u, 632085u}, // ST1F01T body
    {0x61603b838e9f3563ull, 1918080u, 1105u, 1917584u}, // ST30T whole
    {0x64f3fa5e73b9aa51ull, 1584000u, 1105u, 1584000u}, // ST1FT whole
    {0x663b609646372f45ull, 4399488u, 1105u, 4398193u}, // ST3DT whole
    {0x6d2c8d247b2ed454ull, 2579328u, 1105u, 2578818u}, // ST40T whole
    {0x6d5d0a398a1dcaa1ull, 1744128u, 1105u, 1744128u}, // ST50T whole
    {0x6fbcf1f6179958a2ull, 1343232u, 1105u, 1342753u}, // ST18T whole
    {0x72811f9612266803ull, 1589760u, 174364u, 1589656u}, // ST1401 intro+body
    {0x7884866ef6eea7b0ull, 2517120u, 1105u, 2517120u}, // ST35T body
    {0x7c3b4f37b7adaeabull, 1512576u, 1105u, 1512043u}, // ST1703T whole
    {0x7fbeb13db63f3de7ull, 708480u, 1105u, 708480u}, // ST30T body
    {0x867550df0f569e2cull, 873216u, 426207u, 861849u}, // ST32T found
    {0x8679c8dd27eef518ull, 2684160u, 1105u, 2683426u}, // ST3C02 whole
    {0x880a70d720193d3aull, 1240704u, 1105u, 1239822u}, // ST2F01 body
    {0x896b1008ccc7769eull, 2125440u, 1105u, 2124604u}, // ST54T body
    {0x9550cb239897f368ull, 1888128u, 1105u, 1887260u}, // ST32T whole
    {0x96089db9792809fcull, 1032192u, 1105u, 1032192u}, // ST01T whole
    {0x98e4623c70e80d00ull, 2087424u, 1105u, 2087299u}, // ST41T whole
    {0x9b093f50b97dc9ffull, 2481408u, 1105u, 2481408u}, // ST4FT whole
    {0x9b0f47d415bf211eull, 4254336u, 1105u, 4252844u}, // ST4BT whole
    {0x9c3431a2abfea217ull, 1725696u, 1105u, 1724814u}, // ST0E01T whole
    {0x9e630ca2f221c220ull, 2166912u, 434137u, 2099309u}, // ST4CT found
    {0xa0608e85a6034554ull, 1997568u, 995955u, 1831859u}, // ST31T repeat
    {0xa08032d198efd91cull, 3715200u, 1105u, 3715145u}, // ST3501T whole
    {0xa41be695d6dfcc13ull, 1152000u, 1105u, 1151998u}, // ST26T whole
    {0xa4541e90182e0194ull, 2477952u, 1900812u, 2466927u}, // ST1ET repeat
    {0xa4d96f52608669a2ull, 764928u, 34729u, 742373u}, // ST4DT repeat
    {0xa67b14d2cbb0b14dull, 2536704u, 1105u, 2536704u}, // ST48T whole
    {0xb658ff207008796aull, 2093184u, 1105u, 2092328u}, // ST10T whole
    {0xbb5a77206dbe3feeull, 2047104u, 1105u, 2046798u}, // ST26T body
    {0xbd82f85a6c5ac33eull, 2877696u, 1105u, 2876789u}, // ST4BT whole
    {0xbd84a7b568d944c4ull, 2407680u, 1263914u, 2396144u}, // TITLE repeat
    {0xc0f4367c77085b18ull, 2916864u, 1105u, 2916233u}, // ST5CT whole
    {0xcb01d4857cfc74bdull, 1756800u, 179172u, 1751718u}, // ST21T intro+body
    {0xcc3333de26ad32d8ull, 3764736u, 1866499u, 3753549u}, // ST43T repeat
    {0xd3ee1d80c3ecebd8ull, 2550528u, 1105u, 2549763u}, // ST1701 body
    {0xd43948dfe0a38011ull, 949248u, 1105u, 948647u}, // ST04T whole
    {0xda29a4c23c9ff4feull, 2613888u, 1105u, 2613881u}, // ST24T whole
    {0xdc6ca70ca2835317ull, 3113856u, 1818048u, 3091806u}, // ST1102 repeat
    {0xdf4e3be3fb48447eull, 3717504u, 355668u, 3716988u}, // ST1200 intro+body
    {0xe4a2f3d4414eee18ull, 2189952u, 354829u, 2168166u}, // ST33T repeat
    {0xe8447415de6adcb1ull, 1562112u, 59182u, 1538598u}, // ST0BT found
    {0xeaf3b1ab5fee705full, 2026368u, 1105u, 2025910u}, // ST29T whole
    {0xeceadbf31c4bec90ull, 584064u, 219218u, 573039u}, // ST4AT found
    {0xeebbaf01b16c0406ull, 2336256u, 1193001u, 2325231u}, // ST1ET repeat
    {0xef8df3adea6f518eull, 2833920u, 935013u, 2822062u}, // ST0F00 repeat
    {0xf3f8f373fb865de4ull, 3397248u, 178239u, 3386222u}, // ST2901T found
    {0xf4ba96b3458f4190ull, 1983744u, 284856u, 1983207u}, // ST08T intro+body
    {0xf7304da58008c375ull, 3090816u, 1105u, 3090523u}, // ST4CT whole
    {0xf77a954e352c9220ull, 1418112u, 1105u, 1417300u}, // ST3001T body
    {0xfb289cb88c5d7311ull, 2403072u, 1212610u, 2392017u}, // ST48T found
    {0xfc69839e994343dfull, 1700352u, 1105u, 1699789u}, // ST1201 whole
}};

} // namespace

const MusicLoop* findMusicLoop(std::span<const uint8_t> mp3) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint8_t b : mp3) h = (h ^ b) * 0x100000001b3ull;
    auto it = std::lower_bound(kLoops.begin(), kLoops.end(), h,
                               [](const MusicLoop& m, uint64_t v) { return m.hash < v; });
    return it != kLoops.end() && it->hash == h ? &*it : nullptr;
}

} // namespace od::audio
