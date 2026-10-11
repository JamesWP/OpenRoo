/* In order: blank the screen; show bitmaps\<map>.bmp as the loading screen;
 * load themes\<map>.thm unless it is the theme already loaded; build the
 * placement lists and the scene objects; copy the level title; stamp the
 * level's start time; place the camera.  The level builder also places the
 * camera, with other values; this runs a frame later, so these are the ones
 * that count. */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "levelentry.h"
#include "renderdevice.h"
#include "image.h"
#include "game.h"
#include "levelmap.h"
#include "theme.h"
#include "levelplacements.h"
#include "scene.h"
#include "logger.h"
#include "renderstate.h"
#include "renderer.h"
#include "clock.h"
#include "camera.h"

  void  
LevelEntry_PrepareAssets(Renderer *renderer)
{
    RenderDevice *d3d = renderer->device;
    Game *g = Game::instance();
    const LevelMap *map = g->world()->map();

    // 1. Blank the back buffer and flip.
    d3d->Clear(ClearFlag::Color);
    d3d->Present();

    // 2. The loading screen.
    char thm[0x100], bmp[0x124];
    sprintf(thm, "themes\\%s.thm", map->mapName());
    sprintf(bmp, "bitmaps\\%s.bmp", map->mapName());
    bool ok = Image_LoadBMP(bmp, renderer->loadingImage);
    d3d->PresentImage(ok ? renderer->loadingImage : renderer->fallbackImage);

    // 3. The theme, only when it has changed: the block keeps the path it was
    // loaded from.
    if (strcmp(renderer->themeBlock.themeName(), thm) != 0) {
        g_logger.logMessage(1, "THM: *** Theme: %s ***", map->mapName());
        if (!renderer->themeBlock.load(g->audio()->themeSounds(), g->gameDir(), d3d,
                                       &renderer->textures, &renderer->models, thm))
            g_logger.logMessage(4, "Couldn't load theme %s.", map->mapName());
    }

    // 4. What the frame renderer draws.
    renderer->levelPlacements.build(g->world(), &renderer->themeBlock);
    renderer->scene.buildObjectList(d3d, g->world()->extraObjects());

    strcpy(renderer->levelTitle, map->title());
    renderer->lastTickMs = clock_seconds() * 1000.0;

    // 5. The camera: a fixed offset (0, 6, -4) scaled to 255/sqrt(52), placed
    // over the middle of the grid.  Double precision; the game's
    // extended-precision chain differs below a float's last bit here.
    const double s = sqrt(52.0);
    const float offY = (float)(6.0f / s) * 255.0f;
    const float offX = (float)(0.0f / s) * 255.0f;
    const double offZ = (-4.0f / s) * 255.0f;
    const float x = (float)((float)map->extentU() * 0.5f + offX);
    const float z = (float)((float)(-(int)map->extentV()) * 0.5f + offZ);

    CameraPose *cam = g->camera()->pose();
    cam->target()[0] = x;
    cam->target()[1] = offY;
    cam->target()[2] = z;
    cam->eye()[0] = x;
    cam->eye()[1] = offY + 6.0f;
    cam->eye()[2] = z - 4.0f;
    renderer->cameraFocus = CameraFocus();
    cam->setYaw(0.0f);
    cam->setPitch(1.0471976f);  // pi/3
}
