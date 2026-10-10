/* The scripted-camera step.  While the instruction-script player is running a
 * loaded script, GameTick calls this: it ticks the script, then copies the
 * script's camera mode, distance and eye, and its spline point, into the Game.
 *
 * PRESERVED: eye[1] and eye[2] are copied crossed, script eye[2] into the
 * Game's eye[1] and eye[1] into eye[2].
 *
 * KAROO_SIM_FX=camswap, a negative control, copies them straight. */

#include <stdint.h>
#include "sysdev.h"
#include <string.h>
#include "logger.h"
#include "game.h"
#include "camera.h"
#include "checkpoint.h"

static int s_fx = -1;

  void  
Sim_RestoreCheckpointStateBlocks(Game *self)
{
    Game         *g  = self;
    ScriptPlayer *sp = g->scriptPlayer();

    if (s_fx < 0) {
        char e[32];
        uint32_t n = sysdev::getEnv("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "camswap") == 0);
        if (s_fx)
            g_logger.write("checkpoint: KAROO_SIM_FX=camswap -- straight copy\n");
    }

    if (sp->running() == 0 || sp->loaded() == 0)
        return;

    sp->tick(*g->clock(), g->tickStep()->value, g_camera.eye());
    g->setCameraMode(sp->cameraMode());
    g->setCameraDistance(sp->cameraDistance());
    g->setCameraEye(0, sp->eye(0));
    g->setCameraEye(1, sp->eye(s_fx ? 1 : 2));
    g->setCameraEye(2, sp->eye(s_fx ? 2 : 1));
    g->setField13cc94(0, sp->splinePoint(0));
    g->setField13cc94(1, sp->splinePoint(1));
    g->setField13cc94(2, sp->splinePoint(2));
}
