// How open to the sky a room's geometry is: from its floor, what the sun's rays and the vertical meet, by the same rule as
// the sun's shadow (scene.hlsl: opaque polygons, the transparent texels let light through). The base of the sun's decision
// on the rooms without a roof (--roof-study writes it for every room).
#pragma once

#include "app/room_mesh.hpp"

namespace od::app {

struct RoomOpenness {
    int samples = 0;      // the floor points measured (the lowest level surface of each sampled cell)
    float covered = 0.0f; // of them, the part with something straight above
    float sunReach = 0.0f; // the part of the rays toward a sun 50 degrees high, 8 directions, that get out
    float walled = 0.0f;  // the part of the level rays at a character's height, 8 directions, that meet a wall
    float floorY = 0.0f;  // the floor points' median height (world units)
    float gap = 0.0f;     // from it to the room's highest vertex, in the character's heights
    // the room's opaque faces, at each triangle's centre, by area: the PS1's colour (texel * vertex colour / 0x80)
    float luminance = 0.0f; // its mean brightness after the clamp at 1
    float clipped = 0.0f;   // the part with a channel over 1 (the clamp cuts it)
    float vertexShade = 0.0f; // the vertex colour's mean (1 = 0x80)
};

// from loadRoom's mesh, before the upload clears its triangles and pages
RoomOpenness measureRoomOpenness(const RoomMesh& mesh);

// A room with no sky at all: the game clears the frame to black behind it (Maybe_SetSkyGradient 0x5f0370(0)) and it draws
// no sky layer. The ruins and the interiors need no roof over the camera, so most have none, and the sun would come in:
// the sun's shadow covers such a room whole, as a roof would (SunLight::sheltered). Not ST57: a picture, no place.
// A room with a sky gradient and walls all around (ST29, 2B, 30, 31, 3B, 3C) is open: checked by eye, the sun stays
bool roomHasNoSky(const RoomMesh& mesh);

} // namespace od::app
