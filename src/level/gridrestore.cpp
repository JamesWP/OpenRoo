/* GAMETICK_PLAN.md Band B (reopened) — the restart tile-grid restore.
 *
 *   RestoreTileGridFromSnapshot  0x004184a0   1 E8 site (0x004160DF, GameTick)
 *
 * A LEAF: no callees at all.  __fastcall in the original, Game base in ECX,
 * no stack arguments, bare RET -- identical at the ABI level to a thiscall
 * with no arguments, which is how it is declared here.
 *
 * Transcribed from the LISTING, not the decompile.  Per tile (pitch 0x319c
 * along the inner loop, 0x7f along the outer), the snapshot lives 0x1360f0
 * bytes above the live tile:
 *
 *   +0..+2 copied verbatim from the snapshot
 *   snap+3 == 1 && live+3 != 1          -> live+3 = 0
 *   snap+1 == 0x17 && dword live+0x7b   -> snap+1 = live+1 = 1   (WRITES THE
 *                                          SNAPSHOT -- preserved)
 *   live+0x66 != 0                      -> live+3 = live+0x66
 *   snap+3 == 7 && live+3 != 7          -> live+3 = 0
 *   snap+3 not 7 and not 1              -> live+3 = snap+3
 *
 * The order is load-bearing and kept exactly.  Both extents are re-read
 * every iteration, as the listing does.
 *
 * Control: KAROO_SIM_FX=gridkeep -- the state byte (+3) is left untouched,
 * so consumed tiles stay consumed across a restart while +0..+2 and the 0x17
 * snapshot write still happen.  A change to what the world contains, not a
 * skipped call.
 */
#include <windows.h>
#include <string.h>
#include "log.h"
#include "game.h"
#include "gridrestore.h"

static int s_fx = 0, s_diag = 0, s_init = 0;
static unsigned s_calls = 0;

static void fx_init(void)
{
    char buf[64];
    DWORD n;

    if (s_init)
        return;
    s_init = 1;

    n = GetEnvironmentVariableA("KAROO_SIM_FX", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "gridkeep") == 0) {
        s_fx = 1;
        log_write("gridrestore: KAROO_SIM_FX=gridkeep -- tile state bytes "
                  "not restored\n");
    }
    n = GetEnvironmentVariableA("KAROO_GRIDRESTORE_DIAG", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0)
        s_diag = 1;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_RestoreTileGridFromSnapshot(Game *self)
{
    LevelMap *map = self->map();

    fx_init();
    ++s_calls;
    if (s_diag)
        log_write("gridrestore: call %u extents 727=%u 728=%u\n", s_calls,
                  map->extentV(), map->extentU());

    /* The outer loop runs over v (pitch 0x7f), the inner over u (0x319c). */
    for (unsigned r = 0; r < map->extentV(); ++r) {
        for (unsigned c = 0; c < map->extentU(); ++c) {
            Tile *t = map->tile((int)c, (int)r);
            Tile *s = LevelMap::snapshotOf(t);

            t->setHeight(s->height());
            t->setObjectMarker(s->objectMarker());
            t->setParam(s->param());
            if (s->contents() == 1 && t->contents() != 1 && !s_fx)
                t->setContents(0);
            if (s->objectMarker() == 0x17 && t->busy() != 0) {
                s->setObjectMarker(1);
                t->setObjectMarker(1);
            }
            if (s_fx)
                continue;               /* gridkeep: no +3 store at all */
            if (t->field202() != 0)
                t->setContents(t->field202());
            if (s->contents() == 7 && t->contents() != 7)
                t->setContents(0);
            if (s->contents() != 7 && s->contents() != 1)
                t->setContents(s->contents());
        }
    }
}
