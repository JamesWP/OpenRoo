/* Objects are constructed in the game's order and destroyed in exactly the
 * reverse.  The control is set up before the logger it is handed exists, as
 * the game does it; it only keeps the pointer. */
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
#include "textrenderer.h"
#include "scenetexture.h"
#include "model.h"
#include "cdm.h"
#include "menuscreens.h"

/* The menu's textures, constructed in these two groups in this order. */
static SceneTexture *const MENU_TEX_A[] = {
    &g_menuTex1, &g_menuTex2, &g_menuTex3, &g_menuTex4, &g_menuTexSelector,
};
static SceneTexture *const MENU_TEX_B[] = {
    &g_menuTexOn, &g_menuTexOff, &g_menuTexKnob, &g_menuTexScale,
};

void StaticInit_Construct()
{
    g_progCtrl.setup((int)&g_logger);
    Scene_Construct(&g_scene);
    GameLog_Construct(&g_logger);
    g_movie.init();
    LevelPlacements_StaticInit();
    Theme_BlockConstruct(&g_themeBlock);
    Texture_ImageCtor(&g_fallbackImage);
    Texture_ImageCtor(&g_loadingImage);
    Texture_SceneCtor(&g_texShadow);
    FaktMesh_Init(&g_meshPlayer);
    FaktMesh_Init(&g_meshEnemy);
    Text_Construct(&g_fontMain);
    Text_Construct(&g_fontNumbers);
    TextureManager_Construct(&g_textureManager);
    ModelManager_Construct(&g_modelManager);
    g_cdAudio.construct();
    Texture_ImageCtor(&g_demoImage);
    Texture_SceneCtor(&g_texKaroo128);
    for (SceneTexture *t : MENU_TEX_A)
        Texture_SceneCtor(t);
    for (SceneTexture *t : MENU_TEX_B)
        Texture_SceneCtor(t);
    // The sound logger is initialised but never constructed: zero-initialised
    // static storage is its starting state.
    GameLog_Initialize(&g_soundLogger, "StreamSoundBuffer.log", 0);
}

void StaticInit_Destruct()
{
    GameLog_CloseAndRebindVtable(&g_soundLogger);
    for (int i = (int)(sizeof(MENU_TEX_B) / sizeof(*MENU_TEX_B)) - 1; i >= 0; --i)
        Texture_SceneDtorBody(MENU_TEX_B[i]);
    for (int i = (int)(sizeof(MENU_TEX_A) / sizeof(*MENU_TEX_A)) - 1; i >= 0; --i)
        Texture_SceneDtorBody(MENU_TEX_A[i]);
    Texture_SceneDtorBody(&g_texKaroo128);
    Texture_ImageDtorBody(&g_demoImage);
    g_cdAudio.stopAndClose();
    ModelManager_Destruct(&g_modelManager);
    TextureManager_Destruct(&g_textureManager);
    Text_DtorBody(&g_fontNumbers);
    Text_DtorBody(&g_fontMain);
    FaktMesh_DtorBody(&g_meshEnemy);
    FaktMesh_DtorBody(&g_meshPlayer);
    Texture_SceneDtorBody(&g_texShadow);
    Texture_ImageDtorBody(&g_loadingImage);
    Texture_ImageDtorBody(&g_fallbackImage);
    Theme_BlockDestruct(&g_themeBlock);
    g_movie.destruct();
    GameLog_CloseAndRebindVtable(&g_logger);
    Scene_Destruct(&g_scene);
    g_progCtrl.teardown();
}
