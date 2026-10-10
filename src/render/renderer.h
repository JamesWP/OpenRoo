/* renderer.h -- the render state kept between frames and levels: the level's
 * placements and scene, the theme's assets, the camera focus and foe poses of
 * the frame, and the level-entry images.  app owns one (app/main.cpp) and
 * passes it down to the frame renderer, the level entry and the render setup. */
#pragma once

#include "image.h"
#include "camera.h"
#include "framepose.h"
#include "levelplacements.h"
#include "scene.h"
#include "theme.h"

struct Renderer {
    /* The camera focus: FramePose_Player fills it every frame; the level entry
     * zeroes it. */
    CameraFocus cameraFocus{};

    /* One record per live foe, in foe-list order; RenderGameFrame draws from it. */
    FoePose foePoses[500]{};

    /* The level's placement lists, built at level entry. */
    LevelPlacements levelPlacements;

    /* The level's scene-object list, built at level entry (scene.h). */
    Scene scene;

    /* The theme's assets, loaded at level entry when the theme changes. */
    ThemeAssetBlock themeBlock;

    /* The clock (ms) at the last GameTick.  Level entry resets it, so the level's
     * first frame ticks from there. */
    double lastTickMs{};

    /* A copy of the LevelMap's title, made at level entry. */
    char levelTitle[128]{};

    /* The loading screen: bitmaps\<map>.bmp is loaded into the first; the second
     * is shown when that fails. */
    Image loadingImage;
    Image fallbackImage;

    /* bitmaps\demo.bmp, loaded once at startup. */
    Image demoImage;
};
