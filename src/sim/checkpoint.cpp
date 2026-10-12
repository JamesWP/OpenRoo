/* The scripted-camera step.  While the instruction-script player is running a
 * loaded script, GameTick calls this: it ticks the script, then copies the
 * script's camera mode, distance and eye, and its spline point, into the Game.
 *
 * PRESERVED: eye[1] and eye[2] are copied crossed, script eye[2] into the
 * Game's eye[1] and eye[1] into eye[2]. */

#include <stdint.h>
#include "sysdev.h"
#include <string.h>
#include "logger.h"
#include "game.h"
#include "checkpoint.h"

  void  
Sim_RestoreCheckpointStateBlocks(Game *self)
{
    Game         *g  = self;
    ScriptPlayer *sp = g->scriptPlayer();

    if (sp->running() == 0 || sp->loaded() == 0)
        return;

    sp->tick(g->world()->now(), g->world()->tickStep()->value, g->camera()->pose()->eye());
    g->camera()->setMode(sp->cameraMode());
    g->camera()->setDistance(sp->cameraDistance());
    g->camera()->setFocus(0, sp->eye(0));
    g->camera()->setFocus(1, sp->eye(2));
    g->camera()->setFocus(2, sp->eye(1));
    g->camera()->setScriptedEye(0, sp->splinePoint(0));
    g->camera()->setScriptedEye(1, sp->splinePoint(1));
    g->camera()->setScriptedEye(2, sp->splinePoint(2));
}
