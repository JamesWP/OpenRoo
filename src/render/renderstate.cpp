/* renderstate.cpp -- ConfigureRenderState 0x00426000, the one-shot setup
 * WinMain runs after the device exists (ENDGAME E5).  Written from the
 * listing (the decompile fails).  In order:
 *
 *   1. bitmaps\loading.bmp as the loading screen (logged if missing), then
 *      bitmaps\demo.bmp into 0x4dc7a8 (result ignored);
 *   2. both caches' loggers; zero the placement block 0x4e0070 (300 bytes);
 *      BuildMenuGeometry;
 *   3. the camera, exactly as the level entry places it (levelentry.cpp),
 *      and the CameraFocus block: f[3] = 5000, f[5] = yaw, f[6..8] = eye,
 *      the rest 0;
 *   4. WORLD = identity (0x4e0440, which the batch passes read), VIEW =
 *      LookAt(eye, target, +Y), PROJECTION with fov/2 = pi/4: m00 = m11 =
 *      cos, m22 = sin*Q, m23 = sin, m32 = -0.1*sin*Q, Q = 1.001001 (the
 *      original's unnormalised form; everything else 0);
 *   5. ambient light 0x404040 and thirteen render states;
 *   6. models John/Enemy, textures shadow/karoo128, the material (diffuse
 *      0.9,1,0.9,1; ambient and specular white; power 20; emissive left),
 *      the directional light (0.8 grey, direction (1,-1.1,1.2), range
 *      sqrt(FLT_MAX)), then both fonts -- PostQuitMessage(1) if either fails.
 *
 * The view matrix is built inline in the original; it is the same algorithm
 * as 0x407b20, so Camera_BuildLookAt is called with roll 0.  The original
 * FSIN/FCOS/FSQRT run in extended precision; plain double here -- the
 * difference is below a float's last bit. */
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "renderstate.h"
#include "direct3d.h"
#include "texturedib.h"
#include "scenetexture.h"
#include "texture.h"
#include "model.h"
#include "faktmesh.h"
#include "menuscreens.h"
#include "scenematerial.h"
#include "scenelight.h"
#include "textrenderer.h"
#include "gamelog.h"
#include "gameglobals.h"
#include "gamestr.h"
#include "game.h"
#include "levelmap.h"
#include "levelplacements.h"
#include "camera.h"
#include "d3dmath_common.h"

#define GG_DEMO_IMAGE     ((LoadedImage *)0x004dc7a8)
#define GG_MESH_PLAYER    ((CFaktMesh *)0x0046c7b0)
#define GG_MESH_ENEMY     ((CFaktMesh *)0x004e0310)
#define GG_TEX_SHADOW     ((SceneTexture *)0x004e02c8)
#define GG_TEX_KAROO128   ((SceneTexture *)0x004e0408)
#define GG_MATERIAL       ((SceneMaterial *)0x004e0390)
#define GG_LIGHT          ((SceneSpotLight *)0x0046c830)
#define GG_FONT_MAIN      ((TextRenderer *)0x004e0480)
#define GG_FONT_NUMBERS   ((TextRenderer *)0x004e02e8)
#define GG_WORLD_IDENTITY ((D3DMATRIX *)0x004e0440)

extern "C" __declspec(dllexport) void __cdecl
Render_ConfigureRenderState(void)
{
    Direct3D *d3d = g_pDirect3D;

    /* 1. */
    if (!(char)TextureDIB_CreateSurface(GG_FALLBACK_IMAGE, d3d->pDD4, "bitmaps\\loading.bmp", 1))
        GameLog_LogMessage(GG_LOGGER, 3, "SUR: *ERROR* couldn't load loading.bmp");
    Direct3D_FlipPrimaryFrame(GG_FALLBACK_IMAGE);
    TextureDIB_CreateSurface(GG_DEMO_IMAGE, d3d->pDD4, "bitmaps\\demo.bmp", 1);

    /* 2. 0x4400c0 serves both caches. */
    TextureManager_SetLogger(GG_TEXTURE_MANAGER, GG_LOGGER);
    TextureManager_SetLogger((TextureManager *)GG_MODEL_MANAGER, GG_LOGGER);
    memset(GG_LEVEL_PLACEMENTS, 0, 0x12c);
    Menu_BuildMenuGeometry(d3d, GS_GAME_DIR);

    /* 3. */
    Mat4 world;
    m4_identity(&world);
    *GG_WORLD_IDENTITY = *(D3DMATRIX *)&world;

    const LevelMap *map = Game::instance()->map();
    const double s = sqrt(52.0);
    const float offY = (float)(6.0f / s) * 255.0f;
    const float offX = (float)(0.0f / s) * 255.0f;
    const double offZ = (-4.0f / s) * 255.0f;
    const float x = (float)((float)map->extentU() * 0.5f + offX);
    const float z = (float)((float)(-(int)map->extentV()) * 0.5f + offZ);

    CameraGlobals *cam = GG_CAMERA;
    cam->target[0] = x;
    cam->target[1] = offY;
    cam->target[2] = z;
    cam->eye[0] = x;
    cam->eye[1] = offY + 6.0f;
    cam->eye[2] = z - 4.0f;
    cam->yaw   = 0.0f;
    cam->pitch = 1.0471976f;      /* pi/3, bits 0x3f860a92 */

    /* 4. */
    Mat4 view;
    Camera_BuildLookAt(&view, cam->eye[0], cam->eye[1], cam->eye[2],
                       cam->target[0], cam->target[1], cam->target[2],
                       0.0f, 1.0f, 0.0f, 0.0f);

    const double half = 0.7853981852531433;     /* (float)pi/4, widened */
    const float c = (float)cos(half), sn = (float)sin(half);
    const float q = (float)(sn * 1.001001000404358);
    Mat4 proj;
    memset(&proj, 0, sizeof proj);
    proj.m[0]  = c;
    proj.m[5]  = c;
    proj.m[10] = q;
    proj.m[11] = sn;
    proj.m[14] = (float)(q * -0.10000000149011612);

    CameraFocus *f = GG_CAMERA_FOCUS;
    memset(f, 0, sizeof *f);
    f->f[3] = 5000.0f;
    f->f[5] = cam->yaw;
    f->f[6] = cam->eye[0];
    f->f[7] = cam->eye[1];
    f->f[8] = cam->eye[2];

    IDirect3DDevice3 *dev = d3d->pDevice;
    dev->SetTransform(D3DTRANSFORMSTATE_WORLD, GG_WORLD_IDENTITY);
    dev->SetTransform(D3DTRANSFORMSTATE_VIEW, (D3DMATRIX *)&view);
    dev->SetTransform(D3DTRANSFORMSTATE_PROJECTION, (D3DMATRIX *)&proj);

    /* 5. */
    dev->SetLightState(D3DLIGHTSTATE_AMBIENT, 0x404040);
    static const DWORD states[][2] = {
        { 0x09, 2 }, { 0x1a, 0 }, { 0x11, 2 }, { 0x12, 2 }, { 0x1d, 0 },
        { 0x07, 1 }, { 0x38, 8 }, { 0x39, 1 }, { 0x3a, 0xffffffff },
        { 0x3b, 0xffffffff }, { 0x36, 1 }, { 0x35, 1 }, { 0x37, 3 },
    };
    for (unsigned i = 0; i < sizeof states / sizeof states[0]; ++i)
        dev->SetRenderState((D3DRENDERSTATETYPE)states[i][0], states[i][1]);

    /* 6. */
    Model_ImportSceneModels(GG_MESH_PLAYER, "models\\John.mdl");
    Model_ImportSceneModels(GG_MESH_ENEMY, "models\\Enemy.mdl");

    char path[0x100];
    sprintf(path, "%s\\textures\\shadow.tga", GS_GAME_DIR);
    Texture_ImportSceneTextures(GG_TEX_SHADOW, d3d->pDD4, d3d->pDevice, path, 1, 0, 0);
    sprintf(path, "%s\\textures\\karoo128.tga", GS_GAME_DIR);
    Texture_ImportSceneTextures(GG_TEX_KAROO128, d3d->pDD4, d3d->pDevice, path, 1, 0, 0);

    SceneMaterial *mat = GG_MATERIAL;
    SceneMaterial_Create(mat, d3d->pD3D, d3d->pDevice);
    mat->mat.dwSize = 0x50;
    mat->mat.diffuse.r = 0.9f; mat->mat.diffuse.g = 1.0f;
    mat->mat.diffuse.b = 0.9f; mat->mat.diffuse.a = 1.0f;
    mat->mat.ambient.r = mat->mat.ambient.g = mat->mat.ambient.b = mat->mat.ambient.a = 1.0f;
    mat->mat.specular.r = mat->mat.specular.g = mat->mat.specular.b = mat->mat.specular.a = 1.0f;
    mat->mat.power = 20.0f;
    mat->pMaterial->SetMaterial(&mat->mat);
    dev->SetLightState(D3DLIGHTSTATE_MATERIAL, mat->hMaterial);

    SceneSpotLight *light = GG_LIGHT;
    SceneLight_Create(light, d3d);
    D3DLIGHT2 &l = light->light;
    l.dwSize = 0x50;
    l.dltType = D3DLIGHT_DIRECTIONAL;
    l.dcvColor.r = l.dcvColor.g = l.dcvColor.b = 0.8f;
    l.dcvColor.a = 1.0f;
    l.dvDirection.x = 1.0f;
    l.dvDirection.y = -1.1f;
    l.dvDirection.z = 1.2f;
    l.dvRange = (float)sqrt(3.4028234663852886e+38);
    l.dvAttenuation0 = 1.0f;
    l.dvAttenuation1 = 0.0f;
    l.dvAttenuation2 = 0.0f;
    l.dwFlags = D3DLIGHT_ACTIVE;
    light->pLight->SetLight((D3DLIGHT *)&l);
    light->pLight->SetLight((D3DLIGHT *)&l);       /* twice, as the original */
    d3d->pViewport->AddLight(light->pLight);

    if (!(char)Text_LoadFont(GG_FONT_MAIN, "fonts\\font1.fon", d3d)) {
        GameLog_LogMessage(GG_LOGGER, 4, "Couldn't create Font font1.fon");
        PostQuitMessage(1);
    }
    if (!(char)Text_LoadFont(GG_FONT_NUMBERS, "fonts\\numbers.fon", d3d)) {
        GameLog_LogMessage(GG_LOGGER, 4, "Couldn't create Font numbers.fon");
        PostQuitMessage(1);
    }
}
