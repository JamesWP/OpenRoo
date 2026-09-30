/* What is left of the global set-up: the classes here still have
 * construct()/destruct(); the rest build themselves as C++ globals. */
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
    g_scene.construct();
    g_levelPlacements.initTileQuad();
    g_textureManager.construct();
    g_modelManager.construct();
}

void StaticInit_Destruct()
{
    g_modelManager.destruct();
    g_textureManager.destruct();
    g_scene.destruct();
}
