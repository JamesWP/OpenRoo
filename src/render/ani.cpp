/* ASSET_PLAN.md Phase 4 (third cycle) — the .ani reader.
 *
 *   0x401070  LoadAnimationFile(AnimTable *dest, const char *path, Logger *log)
 *             __cdecl (plain RET, no argument cleanup).  2 E8 call sites
 *             (0x40d0ae in ThemeFileLoader, 0x420d38 in BuildSceneObjectList);
 *             no E9, no PUSH, no vtable slot.  UD2-stubbed.
 *
 * ─── This closes the .ani question the plan carried since Phase 3 ─────────
 *
 * Phase 3 moved .ani here on the grounds that "the .leo parser opens it
 * inline", and Phase 4's second cycle repeated that.  Both were half right and
 * the conclusion was wrong: the .ani open does sit at 0x4010aa inside this
 * function, but this function is NOT part of ParseExtraObjectEntry's scene
 * construction -- it is a self-contained animation-table loader called from
 * ThemeFileLoader and BuildSceneObjectList, and it can be replaced on its own.
 *
 * What misled the earlier reading: Phase 0's log attributes an open to the
 * function containing the fopen, and this function's fopen site is reached
 * from the .leo path, so ".ani is inside the .leo work" looked settled.  It
 * was not re-checked until the disassembly was read for this cycle.
 *
 * ─── The format ───────────────────────────────────────────────────────────
 *
 * A .ani is a plain-text table, opened "r" (TEXT mode, 0x464200), read with
 * fgets in 0x100-byte lines.  Per line:
 *
 *   - "//" at the start is a comment; a bare "\n" is blank; both are skipped.
 *   - The line is split with strtok on " \t\n" (0x4641fc) into 0x100-byte
 *     token slots.
 *   - Fewer than 4 tokens: the line is ignored.
 *   - token[0] is lowercased (strlwr, 0x4505b7) and matched against 24
 *     keywords; a match selects a 0x10-byte slot in the destination table.
 *   - The slot is filled: [0] = atoi(token[1]), [4] = atoi(token[2]),
 *     [8] = atoi(token[3]).
 *   - If there are more than 4 tokens and token[4] lowercases to "r", the
 *     slot's [0xc] is set to 1.
 *
 * The destination is cleared to 0x180 bytes up front, and 24 slots x 0x10
 * bytes IS 0x180 -- the table tiles exactly, which is what confirms the slot
 * list below is complete.
 *
 * ─── The slot map, and its one oddity ─────────────────────────────────────
 *
 * Keyword order in the compare chain, with the offset each selects:
 *
 *   walk_forward      +0x00     ice               +0xa0
 *   walk_backward     +0x10     fall              +0xb0
 *   speed_forward     +0x20     paraglide         +0xc0
 *   speed_backward    +0x30     slide             +0xd0
 *   slow_forward      +0x40     idle1             +0xe0
 *   slow_backward     +0x50     idle2             +0xf0
 *   celebration       +0x60     field_stair_up    +0x100
 *   jump              +0x70     field_stair_down  +0x110
 *   glue              +0x80     stair_stair_up    +0x120
 *   ghost             +0x90     stair_stair_down  +0x130
 *                               stair_field_up    +0x140
 *                               stair_field_down  +0x150
 *                               turn_left         +0x160
 *                               turn_right        +0x170
 *
 * NOTE the compare order is NOT the offset order: after "celebration" (+0x60)
 * the chain tests "ice" (+0xa0), then "jump" (+0x70), "fall" (+0xb0),
 * "glue" (+0x80), "ghost" (+0x90).  Four slots are visited out of sequence.
 * That is how the original is written and the table above follows the
 * ADDRESSES, not the compare order; the code below keeps the compare order,
 * because a duplicate keyword would resolve to the first match.
 *
 * ─── Calls into the game binary ───────────────────────────────────────────
 *
 * ONE: Log_Message (0x441b10), for the "ANI: %s loaded" line on success.
 * That is the "logging" kind ASSET_PLAN.md's no-callback rule keeps while the
 * game's log is observable behaviour.  Everything else -- open, fgets,
 * tokenise, lowercase, atoi, close -- is our CRT.
 *
 * ─── Defects and oddities preserved deliberately ──────────────────────────
 *
 * 1. NO BOUND ON THE TOKEN COUNT.  The original writes token N into a
 *    0x100-byte slot at tokens + N*0x100 inside a 0x1000-byte (16-slot) stack
 *    buffer, with no check, so a line with more than 16 tokens walks off it.
 *    The replacement gives the array 64 slots, so it is identical for every
 *    line the original survives.  Measured over the shipped content: 24 .ani
 *    files, 956 lines, most tokens on any line 6 (models/KOPF.ANI).
 * 2. NO BOUND ON TOKEN LENGTH either -- each token is copied with sprintf
 *    "%s" into its 0x100-byte slot.
 * 3. A NULL from fgets does NOT end the loop.  The original jumps to the
 *    bottom-of-loop EOF test and goes round again if EOF is not yet set, so a
 *    read error that never sets EOF spins forever.  Reproduced exactly.
 * 4. An empty path returns 0 having already cleared the table.
 * 5. The line-start tests are asymmetric: '/' is only a comment when the
 *    SECOND character is also '/', but a line starting '/' followed by
 *    anything else falls through to be tokenised.
 * 6. Every keyword comparison lowercases token[0] again, in place -- 24 times
 *    for an unmatched line.  Harmless, and kept.
 *
 * ─── Visual proof ─────────────────────────────────────────────────────────
 *
 * KAROO_ANI_FX=freeze forces every slot's end frame to equal its start frame,
 * so animated models stop animating while everything else about them stays.
 * A motion change, not a colour.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "log.h"

/* The game's logger -- the one callback here.  __cdecl(logger, level, fmt, ...) */
typedef void (__cdecl *logmsg_fn)(void *logger, int level, const char *fmt, ...);
#define ORIG_LOG_MESSAGE ((logmsg_fn)0x00441b10)
#define STR_ANI_LOADED   ((const char *)0x004640b0)   /* "ANI: %s loaded" */

#define ANI_TABLE_SIZE   0x180      /* 24 slots x 0x10, and it tiles exactly */
#define ANI_LINE_MAX     0x100
#define ANI_TOKEN_SLOTS  64         /* the original has 16; see defect 1 */
#define ANI_TOKEN_SIZE   0x100
#define ANI_LOG_FIRST    6

/* Keyword -> slot offset, in the original's COMPARE order (see the header). */
static const struct { const char *key; unsigned off; } ANI_SLOTS[] = {
    { "walk_forward",     0x000 }, { "walk_backward",    0x010 },
    { "speed_forward",    0x020 }, { "speed_backward",   0x030 },
    { "slow_forward",     0x040 }, { "slow_backward",    0x050 },
    { "celebration",      0x060 }, { "ice",              0x0a0 },
    { "jump",             0x070 }, { "fall",             0x0b0 },
    { "glue",             0x080 }, { "ghost",            0x090 },
    { "paraglide",        0x0c0 }, { "slide",            0x0d0 },
    { "idle1",            0x0e0 }, { "idle2",            0x0f0 },
    { "field_stair_up",   0x100 }, { "field_stair_down", 0x110 },
    { "stair_stair_up",   0x120 }, { "stair_stair_down", 0x130 },
    { "stair_field_up",   0x140 }, { "stair_field_down", 0x150 },
    { "turn_left",        0x160 }, { "turn_right",       0x170 },
};
#define ANI_SLOT_COUNT (sizeof(ANI_SLOTS) / sizeof(ANI_SLOTS[0]))

static char s_tokens[ANI_TOKEN_SLOTS][ANI_TOKEN_SIZE];

static bool fx_freeze(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (GetEnvironmentVariableA("KAROO_ANI_FX", buf, sizeof(buf)))
            cached = (lstrcmpiA(buf, "freeze") == 0);
        log_write("ani: FX mode = %s\n", cached ? "freeze" : "off");
    }
    return cached != 0;
}

/* strlwr (0x4505b7) in its ASCII fast path: 'A'..'Z' += 0x20, in place. */
static char *ani_strlwr(char *s)
{
    for (char *p = s; *p; p++)
        if (*p > '@' && *p < '[')
            *p = (char)(*p + ' ');
    return s;
}

extern "C" __declspec(dllexport) int __cdecl
Ani_LoadAnimationFile(void *dest, const char *path, void *logger)
{
    unsigned char *table = (unsigned char *)dest;
    char line[ANI_LINE_MAX];
    FILE *fp;
    static int logged = 0;

    memset(table, 0, ANI_TABLE_SIZE);

    if (path == NULL || path[0] == '\0')
        return 0;                                  /* defect 4 */

    fp = fopen(path, "r");
    if (fp == NULL)
        return 0;

    while (!feof(fp)) {
        char *tok;
        unsigned count = 0;
        unsigned char *slot = NULL;

        if (fgets(line, ANI_LINE_MAX, fp) == NULL)
            continue;                              /* defect 3: not a break */

        memset(s_tokens, 0, sizeof(s_tokens));

        if (line[0] == '/') {                      /* defect 5 */
            if (line[1] == '/')
                continue;
        } else if (line[0] == '\n') {
            continue;
        }

        tok = strtok(line, " \t\n");
        if (tok == NULL)
            continue;
        while (tok != NULL) {
            if (count < ANI_TOKEN_SLOTS)           /* defect 1 */
                sprintf(s_tokens[count], "%s", tok);
            count++;
            tok = strtok(NULL, " \t\n");
        }

        if (count <= 3)
            continue;

        for (unsigned i = 0; i < ANI_SLOT_COUNT; i++) {
            if (strcmp(ani_strlwr(s_tokens[0]), ANI_SLOTS[i].key) == 0) {
                slot = table + ANI_SLOTS[i].off;   /* defect 6 */
                break;
            }
        }
        if (slot == NULL)
            continue;

        ((int *)slot)[0] = atoi(s_tokens[1]);
        ((int *)slot)[1] = atoi(s_tokens[2]);
        ((int *)slot)[2] = atoi(s_tokens[3]);

        if (fx_freeze())
            ((int *)slot)[1] = ((int *)slot)[0];   /* end frame = start frame */

        if (count > 4 && strcmp(ani_strlwr(s_tokens[4]), "r") == 0)
            ((int *)slot)[3] = 1;
    }

    fclose(fp);

    /* KAROO_ANI_DUMP=<path> -- the whole 0x180-byte table, hashed, for
     * comparison against an independent Python parse.  ASSET_PLAN.md Phase 4. */
    {
        char dump[MAX_PATH];
        if (GetEnvironmentVariableA("KAROO_ANI_DUMP", dump, sizeof(dump))) {
            HANDLE h = CreateFileA(dump, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                                   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                unsigned long hash = 2166136261UL;
                for (unsigned i = 0; i < ANI_TABLE_SIZE; i++) {
                    hash ^= table[i];
                    hash *= 16777619UL;
                }
                char l[768];
                int n = wsprintfA(l, "%s table=%08lx\r\n", path, hash);
                DWORD w = 0;
                WriteFile(h, l, (DWORD)n, &w, NULL);
                CloseHandle(h);
            }
        }
    }

    if (logger != NULL)
        ORIG_LOG_MESSAGE(logger, 1, STR_ANI_LOADED, path);

    if (logged < ANI_LOG_FIRST) {
        logged++;
        log_write("ani: '%s' loaded\n", path);
    }
    return 1;
}
