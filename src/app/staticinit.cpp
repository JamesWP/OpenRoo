/* staticinit.cpp -- see staticinit.h.
 *
 * One block per slot of the original's initialiser table, in table order;
 * the destructors run in exactly the reverse of their atexit registration.
 * Slots 0x464014/18, 0x464024/28 and 0x46405c..68 were empty thunks (a bare
 * RET), and 0x46401c registered no destructor.  Every constructor and
 * destructor is already ours; each comment names the thunk it replaces.
 */
#include <windows.h>
#include "staticinit.h"
#include "gameglobals.h"
#include "progctrl.h"
#include "scene.h"
#include "gamelog.h"
#include "movie.h"
#include "levelplacements.h"
#include "theme.h"
#include "texture.h"
#include "faktmesh.h"
#include "scenematerial.h"
#include "scenelight.h"
#include "textrenderer.h"
#include "scenetexture.h"
#include "model.h"
#include "cdm.h"
#include "menuscreens.h"

/* The menu's two thunks, in their construction order. */
static SceneTexture *const MENU_TEX_A[] = {
    &g_menuTex1, &g_menuTex2, &g_menuTex3, &g_menuTex4, &g_menuTexSelector,   /* 0x42d850 */
};
static SceneTexture *const MENU_TEX_B[] = {
    &g_menuTexOn, &g_menuTexOff, &g_menuTexKnob, &g_menuTexScale,             /* 0x42d8f0 */
};

void StaticInit_Construct()
{
    ProgCtrl_Setup(&g_progCtrl, (int)&g_logger);        /* 0x403900: before the logger is built, as in the table */
    Scene_Construct(&g_scene);                          /* 0x420b50 */
    GameLog_Construct(&g_logger);                       /* 0x4255b0 */
    Movie_Construct(&g_movie);                          /* 0x4255f0 */
    LevelPlacements_StaticInit();                       /* 0x425670 */
    Theme_BlockConstruct(&g_themeBlock);                /* 0x4256b0 */
    Texture_ImageCtor(&g_fallbackImage);                /* 0x425c80 */
    Texture_ImageCtor(&g_loadingImage);                 /* 0x425cc0 */
    Texture_SceneCtor(&g_texShadow);                    /* 0x425d00 */
    FaktMesh_Init(&g_meshPlayer);                       /* 0x425d40 */
    FaktMesh_Init(&g_meshEnemy);
    SceneMaterial_Construct(&g_material);               /* 0x425da0 */
    SceneLight_Construct(&g_light);                     /* 0x425de0 */
    Text_Construct(&g_fontMain);                        /* 0x425e20 */
    Text_Construct(&g_fontNumbers);
    TextureManager_Construct(&g_textureManager);        /* 0x425e80 */
    ModelManager_Construct(&g_modelManager);            /* 0x425ec0 */
    CDM_Constructor(&g_cdAudio);                        /* 0x425f00 */
    Texture_ImageCtor(&g_demoImage);                    /* 0x425f40 */
    Texture_SceneCtor(&g_texKaroo128);                  /* 0x425f80 */
    for (SceneTexture *t : MENU_TEX_A)                  /* 0x42d840 */
        Texture_SceneCtor(t);
    for (SceneTexture *t : MENU_TEX_B)                  /* 0x42d8e0 */
        Texture_SceneCtor(t);
    /* 0x443c60: Initialize only -- the original never ran a constructor on
     * this one; its BSS zeroes were the starting state. */
    GameLog_Initialize(&g_soundLogger, "StreamSoundBuffer.log", 0);
}

void StaticInit_Destruct()
{
    /* 0x443ca0 guarded this with a run-once flag (0x4e08f8); it only ever
     * runs once here. */
    GameLog_CloseAndRebindVtable(&g_soundLogger);
    for (int i = (int)(sizeof(MENU_TEX_B) / sizeof(*MENU_TEX_B)) - 1; i >= 0; --i)
        Texture_SceneDtorBody(MENU_TEX_B[i]);           /* 0x42d930 */
    for (int i = (int)(sizeof(MENU_TEX_A) / sizeof(*MENU_TEX_A)) - 1; i >= 0; --i)
        Texture_SceneDtorBody(MENU_TEX_A[i]);           /* 0x42d8a0 */
    Texture_SceneDtorBody(&g_texKaroo128);              /* 0x425fb0 */
    Texture_ImageDtorBody(&g_demoImage);                /* 0x425f70 */
    CDM_Destructor(&g_cdAudio);                         /* 0x425f30 */
    ModelManager_Destruct(&g_modelManager);             /* 0x425ef0 */
    TextureManager_Destruct(&g_textureManager);         /* 0x425eb0 */
    Text_DtorBody(&g_fontNumbers);                      /* 0x425e60 */
    Text_DtorBody(&g_fontMain);
    SceneLight_DtorBody(&g_light);                      /* 0x425e10 */
    SceneMaterial_DtorBody(&g_material);                /* 0x425dd0 */
    FaktMesh_DtorBody(&g_meshEnemy);                    /* 0x425d80 */
    FaktMesh_DtorBody(&g_meshPlayer);
    Texture_SceneDtorBody(&g_texShadow);                /* 0x425d30 */
    Texture_ImageDtorBody(&g_loadingImage);             /* 0x425cf0 */
    Texture_ImageDtorBody(&g_fallbackImage);            /* 0x425cb0 */
    Theme_BlockDestruct(&g_themeBlock);                 /* 0x4256e0 */
    Movie_Destruct(&g_movie);                           /* 0x425620 */
    GameLog_CloseAndRebindVtable(&g_logger);            /* 0x4255e0 */
    Scene_Destruct(&g_scene);                           /* 0x420b80 */
    ProgCtrl_Teardown(&g_progCtrl);                     /* 0x403930 */
}
