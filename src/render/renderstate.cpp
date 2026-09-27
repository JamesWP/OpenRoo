/* ConfigureRenderState: the one-time setup WinMain runs once the Direct3D
 * device exists.  It loads the loading-screen and demo bitmaps, wires the
 * texture and model caches' loggers, places the initial camera and builds the
 * view/projection matrices, sets the fixed render states, and imports the
 * shared player/enemy models, textures, material, light and fonts. */

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "renderstate.h"
#include "renderdevice.h"
#include "texturedib.h"
#include "scenetexture.h"
#include "texture.h"
#include "model.h"
#include "faktmesh.h"
#include "menuscreens.h"
#include "textrenderer.h"
#include "gamelog.h"
#include "gameglobals.h"
#include "gamestr.h"
#include "game.h"
#include "levelmap.h"
#include "levelplacements.h"
#include "camera.h"
#include "d3dmath_common.h"

extern "C" __declspec(dllexport) void __cdecl
Render_ConfigureRenderState(void)
{
    RenderDevice *d3d = g_renderDevice;

    // Loading-screen and demo bitmaps.
    if (!(char)TextureDIB_CreateSurface(&g_fallbackImage, d3d, "bitmaps\\loading.bmp", 1))
        g_logger.logMessage(3, "SUR: *ERROR* couldn't load loading.bmp");
    g_renderDevice->PresentImage(&g_fallbackImage);
    TextureDIB_CreateSurface(&g_demoImage, d3d, "bitmaps\\demo.bmp", 1);

    // Texture and model caches' loggers, and the level placement scratch
    // block.
    TextureManager_SetLogger(&g_textureManager, &g_logger);
    TextureManager_SetLogger((TextureManager *)&g_modelManager, &g_logger);
    memset(&g_levelPlacements, 0, 0x12c);
    Menu_BuildMenuGeometry(d3d, g_gameDir);

    // The initial camera, placed exactly as level entry places it.
    Mat4 world;
    m4_identity(&world);
    g_worldIdentity = *(Mat4 *)&world;

    const LevelMap *map = Game::instance()->map();
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
    cam->yaw   = 0.0f;
    cam->pitch = 1.0471976f;  // pi/3

    // World, view and projection transforms.
    Mat4 view;
    Camera_BuildLookAt(&view, cam->eye[0], cam->eye[1], cam->eye[2],
                       cam->target[0], cam->target[1], cam->target[2],
                       0.0f, 1.0f, 0.0f, 0.0f);

    const double half = 0.7853981852531433;  // (float)pi/4, widened to double
    const float c = (float)cos(half), sn = (float)sin(half);
    const float q = (float)(sn * 1.001001000404358);
    // Projection with an unnormalised depth term (m22, m23 are not the usual
    // 1/(f-n) form).
    Mat4 proj;
    memset(&proj, 0, sizeof proj);
    proj.m[0]  = c;
    proj.m[5]  = c;
    proj.m[10] = q;
    proj.m[11] = sn;
    proj.m[14] = (float)(q * -0.10000000149011612);

    CameraFocus *f = &g_cameraFocus;
    memset(f, 0, sizeof *f);
    f->f[3] = 5000.0f;
    f->f[5] = cam->yaw;
    f->f[6] = cam->eye[0];
    f->f[7] = cam->eye[1];
    f->f[8] = cam->eye[2];

    RenderDevice *dev = d3d;
    dev->SetTransform(Transform::World, &g_worldIdentity);
    dev->SetTransform(Transform::View, &view);
    dev->SetTransform(Transform::Projection, &proj);

    // Ambient light and the fixed render states.
    dev->SetAmbientLight(0x404040);
    static const DWORD states[][2] = {
        { 0x09, 2 }, { 0x1a, 0 }, { 0x11, 2 }, { 0x12, 2 }, { 0x1d, 0 },
        { 0x07, 1 }, { 0x38, 8 }, { 0x39, 1 }, { 0x3a, 0xffffffff },
        { 0x3b, 0xffffffff }, { 0x36, 1 }, { 0x35, 1 }, { 0x37, 3 },
    };
    for (unsigned i = 0; i < sizeof states / sizeof states[0]; ++i)
        dev->SetRenderState((RS)states[i][0], states[i][1]);

    // Shared player/enemy models, textures, material, light and fonts.
    g_meshPlayer.importSceneModels("models\\John.mdl");
    g_meshEnemy.importSceneModels("models\\Enemy.mdl");

    char path[0x100];
    sprintf(path, "%s\\textures\\shadow.tga", g_gameDir);
    Texture_ImportSceneTextures(&g_texShadow, d3d, path, 1, 0, 0);
    sprintf(path, "%s\\textures\\karoo128.tga", g_gameDir);
    Texture_ImportSceneTextures(&g_texKaroo128, d3d, path, 1, 0, 0);

    Material mat = {
        { 0.9f, 1.0f, 0.9f, 1.0f },  // diffuse
        { 1.0f, 1.0f, 1.0f, 1.0f },  // ambient
        { 1.0f, 1.0f, 1.0f, 1.0f },  // specular
        { 0.0f, 0.0f, 0.0f, 0.0f },  // emissive
        20.0f,
    };
    d3d->SetMaterial(mat);

    // REVIEW: the light used to be set twice (the second a no-op).
    DirectionalLight light = { { 0.8f, 0.8f, 0.8f, 1.0f }, { 1.0f, -1.1f, 1.2f } };
    d3d->SetDirectionalLight(light);

    if (!(char)g_fontMain.load("fonts\\font1.fon", d3d)) {
        g_logger.logMessage(4, "Couldn't create Font font1.fon");
        PostQuitMessage(1);
    }
    if (!(char)g_fontNumbers.load("fonts\\numbers.fon", d3d)) {
        g_logger.logMessage(4, "Couldn't create Font numbers.fon");
        PostQuitMessage(1);
    }
}
