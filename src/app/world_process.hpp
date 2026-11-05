// A world process: one room's simulation with no window, no renderer and no sound device, the same game code the
// windowed game runs.
//   opendash --world <stage, hex> <sub> [--story-save <file>] [--world-ticks N] [--world-unpaced]
//     [--world-pad "<ticks> <hex PS1 pad bits>; ..."] [--world-guest <id> <dx> <dz>]... [--world-host-away]
//     [--world-link <the coordinator's port>] [--stage-record <hex>] [--world-mark-defeated <enemy id>]
//     [--world-keep-player] [--world-net-ticks N] [--world-pose-log <file>] [--world-net-stats <file>]
//   opendash --world <stage, hex> <sub> --world-client [--world-client-id N] --world-link <port> [--story-save <file>] [--world-ticks N]
//     [--world-pose-log <file>] [--world-effect-log <file>]
// --world-client: the receiver of a world's network state (game/world_net_state): the room's files, no world update, no
// window. --world-net-ticks: the ticks between two updates (2: 15 a second). The test instruments: --world-keep-player, the
// world's own player on a link, driven by the pad; --world-pose-log, each actor's pose per tick in a local file;
// --world-net-stats, the bytes the network state would take for one client, in a local file.
#pragma once

namespace od::app {

bool worldProcessRequested(int argc, char** argv);
int runWorldProcess(int argc, char** argv);

} // namespace od::app
