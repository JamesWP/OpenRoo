/* GAMETICK_PLAN.md Band B reopened — the checkpoint restore.
 *
 *   RestoreCheckpointStateBlocks  0x00418c70   2 E8 sites (0x00414EEF,
 *                                              0x00414F12, both GameTick)
 *
 * __fastcall, Game base in ECX, bare RET.  Transcribed from the LISTING.
 *
 * Its one callee, the script-player tick 0x0041d920, is the entry into the
 * whole script interpreter (PlayScript and the camera spline).  It is KEPT
 * as a named callback, ScriptPlayer::tick (scriptplayer.h): replacing it
 * means replacing the interpreter, which is ASSET_PLAN/jjscript territory,
 * not a GameTick band.
 *
 *   if script running && script loaded:          (ScriptPlayer +0xdae, +0x9b1)
 *     script->tick(clock, dt)
 *     cameraMode      <- script cameraMode        (+0xdad)
 *     cameraDistance  <- script cameraDistance    (+0x9b5)
 *     cameraEye[0]    <- script eye[0]            (+0xda1)
 *     cameraEye[1]    <- script eye[2]    } SWAPPED source order against the
 *     cameraEye[2]    <- script eye[1]    } destinations -- preserved
 *     +0x13cc94[0..2] <- script splinePoint[0..2] (+0x931)
 *
 * The float copies are plain `=` (COHESION_PLAN.md template point 3: the
 * original's integer MOVs differ from x87 only for a signalling NaN).
 *
 * Control: KAROO_SIM_FX=camswap -- the eye[1]/eye[2] pair is un-swapped
 * (straight copy).  A camera-position change; whether it reaches an
 * asserted field is measured, not assumed.
 */
#include <windows.h>
#include <string.h>
#include "log.h"
#include "game.h"
#include "checkpoint.h"

static int s_fx = -1;

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_RestoreCheckpointStateBlocks(Game *self)
{
    Game         *g  = self;
    ScriptPlayer *sp = g->scriptPlayer();

    if (s_fx < 0) {
        char e[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "camswap") == 0);
        if (s_fx)
            log_write("checkpoint: KAROO_SIM_FX=camswap -- straight copy\n");
    }

    if (sp->running() == 0 || sp->loaded() == 0)
        return;

    sp->tick(*g->clock(), g->tickStep()->value);
    g->setCameraMode(sp->cameraMode());
    g->setCameraDistance(sp->cameraDistance());
    g->setCameraEye(0, sp->eye(0));
    g->setCameraEye(1, sp->eye(s_fx ? 1 : 2));
    g->setCameraEye(2, sp->eye(s_fx ? 2 : 1));
    g->setField13cc94(0, sp->splinePoint(0));
    g->setField13cc94(1, sp->splinePoint(1));
    g->setField13cc94(2, sp->splinePoint(2));
}
