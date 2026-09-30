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

void StaticInit_Construct()
{
    g_progCtrl.setup((int)&g_logger);
    g_scene.construct();
    g_logger.construct();
    g_movie.init();
    g_levelPlacements.initTileQuad();
    g_textureManager.construct();
    g_modelManager.construct();
    g_cdAudio.construct();
    // The sound logger is initialised but never constructed: zero-initialised
    // static storage is its starting state.
    g_soundLogger.initialize("StreamSoundBuffer.log", 0);
}

void StaticInit_Destruct()
{
    g_soundLogger.closeAndRebindVtable();
    g_cdAudio.stopAndClose();
    g_modelManager.destruct();
    g_textureManager.destruct();
    g_movie.destruct();
    g_logger.closeAndRebindVtable();
    g_scene.destruct();
    g_progCtrl.teardown();
}
