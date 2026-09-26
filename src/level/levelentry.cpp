/* levelentry.cpp -- PrepareLevelAssetsOnEntry 0x00426c50.  See levelentry.h.
 *
 * In order: blank the screen; show bitmaps\<map>.bmp as the loading screen;
 * load themes\<map>.thm unless it is the theme already loaded; build the
 * placement lists (levelplacements.cpp) and the scene objects (scene.cpp);
 * copy the level title; stamp the level's start time; place the camera.
 * Every callee is ours.
 *
 * The camera globals written last are also written by SetupLevelObjects
 * (levelsetup.cpp) with different values; this runs a frame later, so these
 * are the ones that count.
 */
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "levelentry.h"
#include "direct3d.h"
#include "texturedib.h"
#include "game.h"
#include "levelmap.h"
#include "theme.h"
#include "levelplacements.h"
#include "scene.h"
#include "gamelog.h"
#include "gameglobals.h"
#include "clock.h"
#include "camera.h"

extern "C" __declspec(dllexport) void __cdecl
LevelEntry_PrepareAssets(void)
{
    Direct3D *d3d = g_pDirect3D;
    Game *g = Game::instance();
    const LevelMap *map = g->map();

    /* 1. Blank the back buffer (DDBLT_COLORFILL, colour 0) and flip.  The
     *    original fills in only dwSize and dwFillColor of its DDBLTFX. */
    DDBLTFX fx;
    memset(&fx, 0, sizeof fx);
    fx.dwSize = sizeof fx;
    d3d->pZBuffer->Blt(NULL, NULL, NULL, DDBLT_COLORFILL, &fx);
    d3d->pPrimary->Flip(NULL, DDFLIP_WAIT);

    /* 2. The loading screen. */
    char thm[0x100], bmp[0x124];
    sprintf(thm, "themes\\%s.thm", map->mapName());
    sprintf(bmp, "bitmaps\\%s.bmp", map->mapName());
    unsigned ok = TextureDIB_CreateSurface(&g_loadingImage, d3d->pDD4, bmp, 1);
    Direct3D_FlipPrimaryFrame((char)ok ? &g_loadingImage : &g_fallbackImage);

    /* 3. The theme, only when it changed.  The block keeps the path it was
     *    loaded from. */
    if (strcmp(g_themeBlock.themeName, thm) != 0) {
        GameLog_LogMessage(&g_logger, 1, "THM: *** Theme: %s ***", map->mapName());
        if (!Theme_Load(g, d3d, &g_themeBlock, thm, &g_logger))
            GameLog_LogMessage(&g_logger, 4, "Couldn't load theme %s.", map->mapName());
    }

    /* 4. What RenderGameFrame draws. */
    LevelPlacements_Build(&g_levelPlacements, g, &g_themeBlock);
    Scene_BuildObjectList(d3d, g->extraObjects(), &g_logger);

    strcpy(g_levelTitle, map->title());
    g_lastTickMs = clock_seconds() * 1000.0;

    /* 5. The camera (camera.h): a fixed offset (0, 6, -4) scaled to 255/sqrt(52),
     *    placed over the middle of the grid.  The original divides in
     *    extended precision and stores some terms to float on the way; the
     *    difference is below a float's last bit at these magnitudes. */
    const double s = sqrt(52.0);
    const float offY = (float)(6.0f / s) * 255.0f;
    const float offX = (float)(0.0f / s) * 255.0f;
    const double offZ = (-4.0f / s) * 255.0f;
    const float x = (float)((float)map->extentU() * 0.5f + offX);
    const float z = (float)((float)(-(int)map->extentV()) * 0.5f + offZ);

    CameraGlobals *cam = &g_camera;
    cam->target[0] = x;
    cam->target[1] = offY;
    cam->target[2] = z;
    cam->eye[0] = x;
    cam->eye[1] = offY + 6.0f;
    cam->eye[2] = z - 4.0f;
    memset(&g_cameraFocus, 0, sizeof(g_cameraFocus));
    cam->yaw   = 0.0f;
    cam->pitch = 1.0471976f;      /* pi/3, bits 0x3f860a92 */
}
