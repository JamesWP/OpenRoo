/* camera.h -- the camera globals at 0x0046c4a0..0x0046c4bf, one static
 * block in the game's data (not part of Game).  Named once here; it had
 * three names across levelsetup.cpp, scriptplayer.cpp and levelentry.cpp.
 *
 * eye vs target -- the evidence, and one conflict:
 *   - PrepareLevelAssetsOnEntry (levelentry.cpp) puts `target` over the
 *     grid centre and `eye` at target + (0, 6, -4): up and behind.
 *   - The script player's splinexyz starts a camera glide from `eye`.
 *   - BUT SetupLevelObjects (levelsetup.cpp) copies Game::cameraEye into
 *     `target` and sets `eye` to (0, 1000, 0).  Either that Game field or
 *     these names are wrong; nothing read so far settles which.  Those
 *     values are overwritten one frame later by the level entry.
 *
 * The last two dwords are the orbit angles, in radians, that
 * UpdateViewTransform 0x404470 (camera.cpp) eases every frame: yaw (0 after
 * setup and level entry) and pitch (0 after SetupLevelObjects, pi/3 after the
 * level entry, clamped at 1.569051).  That function also settles the naming
 * above: it eases `target` towards the player and places `eye` at
 * target + RotX(-pitch).RotY(yaw) applied to (0, 0, -distance).
 */
#pragma once

#include <windows.h>
#include "layout.h"

struct __attribute__((packed)) CameraGlobals {
    static const int ORIGIN = 0;

    float  eye[3];      /* 0x46c4a0 */
    float  target[3];   /* 0x46c4ac */
    float  yaw;         /* 0x46c4b8 */
    float  pitch;       /* 0x46c4bc */

    KAROO_LAYOUT_REGISTER(CameraGlobals);
};

KAROO_LAYOUT_CHECKS(CameraGlobals)
{
    KAROO_LAYOUT_AT(target,   0x0c);
    KAROO_LAYOUT_AT(yaw,      0x18);
    KAROO_LAYOUT_AT(pitch,    0x1c);
    KAROO_LAYOUT_SIZE(0x20);
}

static CameraGlobals *const GG_CAMERA = (CameraGlobals *)0x0046c4a0;

struct Mat4;
struct Direct3D;
class Game;

/* The 9-float block at 0x4e01a0 that RenderGameFrame's 0x404120 fills and
 * passes BY VALUE to UpdateViewTransform: [5] is the yaw the camera turns
 * towards, [6..8] the point `target` follows.  [0..4] are not read here. */
struct CameraFocus { float f[9]; };

extern "C" {
/* 0x407b20, cdecl(out, eye, at, up by value, roll) -> out: a left-handed
 * LookAt (x = up x fwd, y = fwd x x, z = fwd, each normalised), then
 * * RotZ(-roll) when roll != 0. */
__declspec(dllexport) Mat4 *__cdecl
Camera_BuildLookAt(Mat4 *out, float ex, float ey, float ez,
                   float ax, float ay, float az,
                   float ux, float uy, float uz, float roll);
/* UpdateViewTransform 0x404470, cdecl(cam, d3d, game, focus BY VALUE,
 * double dt): eases the orbit camera and sets the VIEW transform.  One
 * caller, RenderGameFrame 0x42747d (add esp,0x38). */
__declspec(dllexport) void __cdecl
Camera_UpdateViewTransform(CameraGlobals *cam, Direct3D *d3d, Game *g,
                           CameraFocus focus, double dt);
}
