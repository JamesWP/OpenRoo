/* In order: blank the screen; show bitmaps\<map>.bmp as the loading screen;
 * load themes\<map>.thm unless it is the theme already loaded; build the
 * placement lists and the scene objects; copy the level title; stamp the
 * level's start time; place the camera.  The level builder also places the
 * camera, with other values; this runs a frame later, so these are the ones
 * that count. */

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "levelentry.h"
#include "renderdevice.h"
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

void LevelEntry_PrepareAssets(void)
{
    RenderDevice *d3d = g_renderDevice;
    Game *g = Game::instance();
    const LevelMap *map = g->map();

    // 1. Blank the back buffer and flip.
    d3d->ClearBackBuffer();
    d3d->Flip();

    // 2. The loading screen.
    char thm[0x100], bmp[0x124];
    sprintf(thm, "themes\\%s.thm", map->mapName());
    sprintf(bmp, "bitmaps\\%s.bmp", map->mapName());
    unsigned ok = TextureDIB_CreateSurface(&g_loadingImage, d3d, bmp, 1);
    g_renderDevice->PresentImage((char)ok ? &g_loadingImage : &g_fallbackImage);

    // 3. The theme, only when it has changed: the block keeps the path it was
    // loaded from.
    if (strcmp(g_themeBlock.themeName, thm) != 0) {
        GameLog_LogMessage(&g_logger, 1, "THM: *** Theme: %s ***", map->mapName());
        if (!Theme_Load(g, d3d, &g_themeBlock, thm, &g_logger))
            GameLog_LogMessage(&g_logger, 4, "Couldn't load theme %s.", map->mapName());
    }

    // 4. What the frame renderer draws.
    LevelPlacements_Build(&g_levelPlacements, g, &g_themeBlock);
    Scene_BuildObjectList(d3d, g->extraObjects(), &g_logger);

    strcpy(g_levelTitle, map->title());
    g_lastTickMs = clock_seconds() * 1000.0;

    // 5. The camera: a fixed offset (0, 6, -4) scaled to 255/sqrt(52), placed
    // over the middle of the grid.  Double precision; the game's
    // extended-precision chain differs below a float's last bit here.
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
    cam->pitch = 1.0471976f;  // pi/3
}
