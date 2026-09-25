/* framepose.{h,cpp} -- the two per-frame pose builders RenderGameFrame calls
 * before it places the camera and draws the actors (0x427029, 0x42703e):
 * the player's into the CameraFocus block at 0x4e01a0 (camera.h), and one
 * FoePose record per live foe into the array at 0x4dc7c8.
 *
 *   0x404120  FramePose_Player  cdecl(Game*, double t, double dt, CameraFocus*)
 *   0x404300  FramePose_Foes    cdecl(Game*, double t, double dt, FoePose*)
 *
 * `t` is never read by either.  Written from the listings (the decompiler
 * would not complete on either).
 */
#pragma once

#include <windows.h>
#include "layout.h"

class Game;
struct CameraFocus;

/* One foe's pose, 0x1d bytes, packed. */
struct __attribute__((packed)) FoePose {
    static const int ORIGIN = 0;

    unsigned char kind;       /* +0x00  the foe's +0x152 */
    float  pos[3];            /* +0x01  (U, H, -V) */
    float  rotX;              /* +0x0d  always 0 */
    float  rotY;              /* +0x11  facing, plus the turn in progress */
    float  rotZ;              /* +0x15  always 0 */
    float  stepFrac;          /* +0x19  written only while stepping; else stale */

    KAROO_LAYOUT_REGISTER(FoePose);
};

KAROO_LAYOUT_CHECKS(FoePose)
{
    KAROO_LAYOUT_AT(pos,      0x01);
    KAROO_LAYOUT_AT(rotY,     0x11);
    KAROO_LAYOUT_AT(stepFrac, 0x19);
    KAROO_LAYOUT_SIZE(0x1d);
}

extern "C" {
__declspec(dllexport) void __cdecl
FramePose_Player(Game *g, double t, double dt, CameraFocus *out);
__declspec(dllexport) void __cdecl
FramePose_Foes(Game *g, double t, double dt, FoePose *out);
}
