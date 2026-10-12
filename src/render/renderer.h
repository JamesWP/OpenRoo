/* renderer.h -- the render state kept between frames and levels: the rendering
 * device and the fixed assets (textures, models, fonts), the level's
 * fixed picture and scene, the theme's assets, the camera focus of the frame,
 * and the level-entry images.  app owns one (app/main.cpp) and
 * passes it down to the frame renderer, the level entry and the render setup. */
#pragma once

#include "image.h"
#include "camera.h"
#include "levelplacements.h"
#include "scene.h"
#include "theme.h"
#include "texture.h"
#include "model.h"
#include "animatedmesh.h"
#include "textrenderer.h"

class RenderDevice;

struct Renderer {
    /* The rendering device.  Not owned: app creates it and deletes it, and
     * stores it here for the render passes. */
    RenderDevice *device{};

    /* The theme's texture and model caches; the Scene has its own (scene.h). */
    TextureManager textures;
    ModelManager models;

    /* The two character meshes, loaded once at startup (renderstate.cpp):
     * models\John.mdl and models\Enemy.mdl. */
    AnimatedMesh meshEnemy;
    AnimatedMesh meshPlayer;

    /* textures\shadow.tga and textures\karoo128.tga, loaded once at startup
     * (renderstate.cpp). */
    Texture texKaroo128;
    Texture texShadow;

    /* The two fonts: fonts\font1.fon and fonts\numbers.fon. */
    TextRenderer fontMain;
    TextRenderer fontNumbers;

    /* The camera focus: FramePose_Player fills it every frame; the level entry
     * zeroes it. */
    CameraFocus cameraFocus{};


    /* The level's placement lists, built at level entry. */
    LevelPlacements levelPlacements;

    /* The level's scene-object list, built at level entry (scene.h). */
    Scene scene;

    /* The theme's assets, loaded at level entry when the theme changes. */
    ThemeAssetBlock themeBlock;

    /* The F3/F4 debug views of the scene objects' paths; core polls the keys each frame. */
    bool showSplinePaths{};
    bool showSplineControlPolygons{};

    /* The clock (ms) at the last GameTick.  Level entry resets it, so the level's
     * first frame ticks from there. */
    double lastTickMs{};

    /* World::deaths() at the last frame drawn; the deaths after it fire
     * their effects in the next. */
    unsigned deathsDrawn{};

    /* A copy of the LevelMap's title, made at level entry. */
    char levelTitle[128]{};

    /* The loading screen: bitmaps\<map>.bmp is loaded into the first; the second
     * is shown when that fails. */
    Image loadingImage;
    Image fallbackImage;
};
