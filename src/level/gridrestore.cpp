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
Sim_RestoreTileGridFromSnapshot(void *self)
{
    unsigned char *B = (unsigned char *)self;

    fx_init();
    ++s_calls;
    if (s_diag)
        log_write("gridrestore: call %u extents 727=%u 728=%u\n", s_calls,
                  B[0x2ab727], B[0x2ab728]);

    for (unsigned r = 0; r < B[0x2ab727]; ++r) {
        unsigned char *t = B + 0x2ab729 + r * 0x7f;
        for (unsigned c = 0; c < B[0x2ab728]; ++c, t += 0x319c) {
            unsigned char *s = t + 0x1360f0;

            t[0] = s[0];
            t[1] = s[1];
            t[2] = s[2];
            if (s[3] == 1 && t[3] != 1 && !s_fx)
                t[3] = 0;
            if (s[1] == 0x17 && *(int *)(t + 0x7b) != 0) {
                s[1] = 1;
                t[1] = 1;
            }
            if (s_fx)
                continue;               /* gridkeep: no +3 store at all */
            if (t[0x66] != 0)
                t[3] = t[0x66];
            if (s[3] == 7 && t[3] != 7)
                t[3] = 0;
            if (s[3] != 7 && s[3] != 1)
                t[3] = s[3];
        }
    }
}
