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
    g_scene.construct();
    g_logger.construct();
    g_movie.init();
    g_levelPlacements.initTileQuad();
    Theme_BlockConstruct(&g_themeBlock);
    g_fallbackImage.construct();
    g_loadingImage.construct();
    g_texShadow.construct();
    g_meshPlayer.init();
    g_meshEnemy.init();
    g_fontMain.construct();
    g_fontNumbers.construct();
    g_textureManager.construct();
    g_modelManager.construct();
    g_cdAudio.construct();
    g_demoImage.construct();
    g_texKaroo128.construct();
    for (SceneTexture *t : MENU_TEX_A)
        t->construct();
    for (SceneTexture *t : MENU_TEX_B)
        t->construct();
    // The sound logger is initialised but never constructed: zero-initialised
    // static storage is its starting state.
    g_soundLogger.initialize("StreamSoundBuffer.log", 0);
}

void StaticInit_Destruct()
{
    g_soundLogger.closeAndRebindVtable();
    for (int i = (int)(sizeof(MENU_TEX_B) / sizeof(*MENU_TEX_B)) - 1; i >= 0; --i)
        MENU_TEX_B[i]->dtorBody();
    for (int i = (int)(sizeof(MENU_TEX_A) / sizeof(*MENU_TEX_A)) - 1; i >= 0; --i)
        MENU_TEX_A[i]->dtorBody();
    g_texKaroo128.dtorBody();
    g_demoImage.dtorBody();
    g_cdAudio.stopAndClose();
    g_modelManager.destruct();
    g_textureManager.destruct();
    g_fontNumbers.destruct();
    g_fontMain.destruct();
    g_meshEnemy.dtorBody();
    g_meshPlayer.dtorBody();
    g_texShadow.dtorBody();
    g_loadingImage.dtorBody();
    g_fallbackImage.dtorBody();
    Theme_BlockDestruct(&g_themeBlock);
    g_movie.destruct();
    g_logger.closeAndRebindVtable();
    g_scene.destruct();
    g_progCtrl.teardown();
}
