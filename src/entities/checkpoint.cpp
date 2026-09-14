/* GAMETICK_PLAN.md Band B reopened — the checkpoint restore.
 *
 *   RestoreCheckpointStateBlocks  0x00418c70   2 E8 sites (0x00414EEF,
 *                                              0x00414F12, both GameTick)
 *
 * __fastcall, Game base in ECX, bare RET.  Transcribed from the LISTING.
 *
 * Its one callee, the script-player tick 0x0041d920, is the entry into the
 * whole script interpreter (PlayScript and the camera spline).  It is KEPT
 * as a named callback: replacing it means replacing the interpreter, which
 * is ASSET_PLAN/jjscript territory, not a GameTick band.
 *
 *   if dword +0x1964e3 && dword +0x1960e6:
 *     tick(+0x195735, +0x170a54, +0x170a58, +0x170a5c, +0x170a60)
 *     +0x28ab2d (byte) <- +0x1964e2
 *     +0x28ab29        <- +0x1960ea
 *     +0x2ab580        <- +0x1964d6
 *     +0x2ab584        <- +0x1964de    } SWAPPED source order against the
 *     +0x2ab588        <- +0x1964da    } destinations -- preserved
 *     +0x13cc94/98/9c  <- +0x196066/6a/6e
 *
 * Control: KAROO_SIM_FX=camswap -- the +0x2ab584/+0x2ab588 pair is
 * un-swapped (straight copy).  A camera-position change; whether it reaches
 * an asserted field is measured, not assumed.
 */
#include <windows.h>
#include <string.h>
#include "log.h"
#include "game.h"

typedef void (__attribute__((thiscall)) *script_tick_fn)(void *player,
                                                        unsigned int a, unsigned int b,
                                                        unsigned int c, unsigned int d);
#define ORIG_SCRIPT_TICK ((script_tick_fn)0x0041d920)   /* named callback */

#define G32(o) (*(unsigned int *)(B + (o)))
#define G8(o)  (*(unsigned char *)(B + (o)))

static int s_fx = -1;

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_RestoreCheckpointStateBlocks(void *self)
{
    unsigned char *B = (unsigned char *)self;

    if (s_fx < 0) {
        char e[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "camswap") == 0);
        if (s_fx)
            log_write("checkpoint: KAROO_SIM_FX=camswap -- straight copy\n");
    }

    if (G32(0x1964e3) == 0 || G32(0x1960e6) == 0)
        return;

    ORIG_SCRIPT_TICK(B + 0x195735, G32(0x170a54), G32(0x170a58),
                     G32(0x170a5c), G32(0x170a60));
    ((Game *)B)->setCameraMode(G8(0x1964e2));
    G32(0x28ab29) = G32(0x1960ea);
    G32(0x2ab580) = G32(0x1964d6);
    G32(0x2ab584) = G32(s_fx ? 0x1964da : 0x1964de);
    G32(0x2ab588) = G32(s_fx ? 0x1964de : 0x1964da);
    G32(0x13cc94) = G32(0x196066);
    G32(0x13cc98) = G32(0x19606a);
    G32(0x13cc9c) = G32(0x19606e);
}
