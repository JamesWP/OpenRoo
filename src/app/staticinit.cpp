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
    g_levelPlacements.initTileQuad();
}

void StaticInit_Destruct()
{
}
