/* camera.h -- the camera globals, one static block (not part of Game).
 *
 * UpdateViewTransform (camera.cpp) eases `target` towards the player every
 * frame and places `eye` at target + RotX(-pitch).RotY(yaw) applied to
 * (0, 0, -distance).  yaw and pitch are in radians; pitch is pi/3 after the
 * level entry and clamped at 1.569051.
 *
 * SetupLevelObjects (levelsetup.cpp) copies Game::cameraEye into `target` and
 * sets `eye` to (0, 1000, 0), the other way round from these names; the
 * level entry overwrites both one frame later.
 */
#pragma once

#include <windows.h>
#include "layout.h"

struct __attribute__((packed)) CameraGlobals {
    static const int ORIGIN = 0;

    float  eye[3];      // +0x00
    float  target[3];   // +0x0c
    float  yaw;         // +0x18
    float  pitch;       // +0x1c

    KAROO_LAYOUT_REGISTER(CameraGlobals);
};

KAROO_LAYOUT_CHECKS(CameraGlobals)
{
    KAROO_LAYOUT_AT(target,   0x0c);
    KAROO_LAYOUT_AT(yaw,      0x18);
    KAROO_LAYOUT_AT(pitch,    0x1c);
    KAROO_LAYOUT_SIZE(0x20);
}

extern CameraGlobals g_camera;

struct Mat4;
class RenderDevice;
class Game;

/* The 9-float block FramePose_Player fills and RenderGameFrame passes BY
 * VALUE to UpdateViewTransform: [5] is the yaw the camera turns
 * towards, [6..8] the point `target` follows.  [0..4] are not read here. */
struct CameraFocus { float f[9]; };
static_assert(sizeof(CameraFocus) == 0x24, "CameraFocus size");
/* The level entry zeroes it; FramePose_Player fills it every frame. */
extern CameraFocus g_cameraFocus;

extern "C" {
/* cdecl(out, eye, at, up by value, roll) -> out: a left-handed
 * LookAt (x = up x fwd, y = fwd x x, z = fwd, each normalised), then
 * * RotZ(-roll) when roll != 0. */
__declspec(dllexport) Mat4 *__cdecl
Camera_BuildLookAt(Mat4 *out, float ex, float ey, float ez,
                   float ax, float ay, float az,
                   float ux, float uy, float uz, float roll);
/* cdecl(cam, d3d, game, focus BY VALUE, double dt): eases the orbit camera
 * and sets the VIEW transform.  One caller, RenderGameFrame. */
__declspec(dllexport) void __cdecl
Camera_UpdateViewTransform(CameraGlobals *cam, RenderDevice *d3d, Game *g,
                           CameraFocus focus, double dt);
}
