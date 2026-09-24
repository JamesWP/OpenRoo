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
 * The last two dwords are named neutrally: 0x4b8 is only ever zeroed, and
 * 0x4bc is 0 after SetupLevelObjects and pi/3 (0x3f860a92) after the level
 * entry -- a FOV is plausible but unconfirmed.
 */
#pragma once

#include <windows.h>
#include "layout.h"

struct __attribute__((packed)) CameraGlobals {
    static const int ORIGIN = 0;

    float  eye[3];      /* 0x46c4a0 */
    float  target[3];   /* 0x46c4ac */
    DWORD  field_18;    /* 0x46c4b8 */
    DWORD  field_1c;    /* 0x46c4bc */

    KAROO_LAYOUT_REGISTER(CameraGlobals);
};

KAROO_LAYOUT_CHECKS(CameraGlobals)
{
    KAROO_LAYOUT_AT(target,   0x0c);
    KAROO_LAYOUT_AT(field_18, 0x18);
    KAROO_LAYOUT_AT(field_1c, 0x1c);
    KAROO_LAYOUT_SIZE(0x20);
}

static CameraGlobals *const GG_CAMERA = (CameraGlobals *)0x0046c4a0;
