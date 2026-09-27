/* The .ani reader (LoadAnimationFile), the table lookup (LookupAnimDescriptor)
 * and the two frame evaluators (ani.h).
 *
 * FORMAT: a .ani is a plain-text table, opened in text mode and read with
 * fgets in 0x100-byte lines.  Per line:
 *   - "//" at the start is a comment; a bare "\n" is blank; both are skipped.
 *   - The line is split with strtok on " \t\n" into 0x100-byte token slots.
 *   - Fewer than 4 tokens: the line is ignored.
 *   - token[0] is lowercased and matched against 24 keywords; a match selects
 *     a slot in the destination table.
 *   - The slot gets firstFrame = atoi(token[1]), numFrames = atoi(token[2]),
 *     fps = atoi(token[3]).
 *   - If there are more than 4 tokens and token[4] lowercases to "r", the
 *     slot's reverse flag is set.
 * The destination table is cleared up front.
 *
 * KAROO_ANI_FX=freeze forces every loaded slot to one frame (numFrames = 1),
 * so animated models hold their first frame while everything else about them
 * stays. */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include "log.h"

#include "gamelog.h"
#include "gamestr.h"
#include "ani.h"

#define ANI_LINE_MAX     0x100
#define ANI_TOKEN_SLOTS  64  // see the token loop
#define ANI_TOKEN_SIZE   0x100
#define ANI_LOG_FIRST    6

/* Keyword -> slot offset, in compare order, not offset order: after
 * "celebration" come "ice", "jump", "fall", "glue", "ghost".  A duplicate
 * keyword resolves to the first match. */
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

/* 'A'..'Z' += 0x20, in place. */
static char *ani_strlwr(char *s)
{
    for (char *p = s; *p; p++)
        if (*p > '@' && *p < '[')
            *p = (char)(*p + ' ');
    return s;
}

extern "C" __declspec(dllexport) int __cdecl
Ani_LoadAnimationFile(AnimTable *dest, const char *path, GameLogger *logger)
{
    unsigned char *table = (unsigned char *)dest;
    char line[ANI_LINE_MAX];
    FILE *fp;
    static int logged = 0;

    memset(table, 0, sizeof(AnimTable));

    if (path == NULL || path[0] == '\0')
        return 0;  // PRESERVED: after the table is cleared

    fp = fopen(path, "r");
    if (fp == NULL)
        return 0;

    while (!feof(fp)) {
        char *tok;
        unsigned count = 0;
        AnimSlot *slot = NULL;

        if (fgets(line, ANI_LINE_MAX, fp) == NULL)
            continue;  // PRESERVED: a read error that never sets EOF spins forever

        memset(s_tokens, 0, sizeof(s_tokens));

        if (line[0] == '/') {  // '/' then anything else is tokenised
            if (line[1] == '/')
                continue;
        } else if (line[0] == '\n') {
            continue;
        }

        tok = strtok(line, " \t\n");
        if (tok == NULL)
            continue;
        // PRESERVED: no bound on the token count or length.  More than 16
        // tokens overran the original's buffer; ANI_TOKEN_SLOTS is sized so
        // that every line that did not overrun reads the same.  No shipped
        // .ani line has more than 6.
        while (tok != NULL) {
            if (count < ANI_TOKEN_SLOTS)
                sprintf(s_tokens[count], "%s", tok);
            count++;
            tok = strtok(NULL, " \t\n");
        }

        if (count <= 3)
            continue;

        for (unsigned i = 0; i < ANI_SLOT_COUNT; i++) {
            if (strcmp(ani_strlwr(s_tokens[0]), ANI_SLOTS[i].key) == 0) {
                slot = (AnimSlot *)(table + ANI_SLOTS[i].off);  // token[0] is lowercased again for every keyword
                break;
            }
        }
        if (slot == NULL)
            continue;

        slot->firstFrame = atoi(s_tokens[1]);
        slot->numFrames  = atoi(s_tokens[2]);
        slot->fps        = atoi(s_tokens[3]);

        if (fx_freeze())
            slot->numFrames = 1;  // hold the first frame

        if (count > 4 && strcmp(ani_strlwr(s_tokens[4]), "r") == 0)
            slot->reverse = 1;
    }

    fclose(fp);

    // KAROO_ANI_DUMP=<path>: the whole table, hashed, for comparison against
    // an independent parse.
    {
        char dump[MAX_PATH];
        if (GetEnvironmentVariableA("KAROO_ANI_DUMP", dump, sizeof(dump))) {
            HANDLE h = CreateFileA(dump, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                                   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                unsigned long hash = 2166136261UL;
                for (unsigned i = 0; i < sizeof(AnimTable); i++) {
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
        GameLog_LogMessage(logger, 1, GS_ANI_LOADED, path);

    if (logged < ANI_LOG_FIRST) {
        logged++;
        log_write("ani: '%s' loaded\n", path);
    }
    return 1;
}

/* ─── LookupAnimDescriptor ────────────────────────────────────────────────
 *
 * An animation code in, the address of that animation's slot out, or NULL for
 * "this code has no animation".  The argument is masked to a byte first, so
 * 0x114 is code 0x14, and every code outside the table -- including 0 --
 * returns NULL.  There is no NULL check on the table: the arms only compute an
 * address.
 *
 * The four speed_/slow_ slots and celebration have no code, so nothing can
 * reach them through this function. */
extern "C" __declspec(dllexport) AnimSlot * __cdecl
Ani_LookupAnimDescriptor(AnimTable *table, unsigned int code)
{
    switch (code & 0xff) {
    case ANIM_WALK_FORWARD:     return &table->walkForward;
    case ANIM_WALK_BACKWARD:    return &table->walkBackward;
    case ANIM_JUMP:             return &table->jump;
    case ANIM_GLUE:             return &table->glue;
    case ANIM_GHOST:            return &table->ghost;
    case ANIM_ICE:              return &table->ice;
    case ANIM_FALL:             return &table->fall;
    case ANIM_PARAGLIDE:        return &table->paraglide;
    case ANIM_SLIDE:            return &table->slide;
    case ANIM_IDLE1:            return &table->idle1;
    case ANIM_IDLE2:            return &table->idle2;
    case ANIM_FIELD_STAIR_UP:   return &table->fieldStairUp;
    case ANIM_FIELD_STAIR_DOWN: return &table->fieldStairDown;
    case ANIM_STAIR_STAIR_UP:   return &table->stairStairUp;
    case ANIM_STAIR_STAIR_DOWN: return &table->stairStairDown;
    case ANIM_STAIR_FIELD_UP:   return &table->stairFieldUp;
    case ANIM_STAIR_FIELD_DOWN: return &table->stairFieldDown;
    case ANIM_TURN_LEFT:        return &table->turnLeft;
    case ANIM_TURN_RIGHT:       return &table->turnRight;
    default:                    return NULL;  // code 0 included
    }
}

/* ─── The two evaluators ────────────────────────────────────────────────────
 *
 * The arithmetic RenderSceneObjects and DrawObjectShadows share.  timeMs is in
 * milliseconds, so `fps` really is frames per second.
 *
 * The slot's fields are read unsigned: a negative firstFrame, numFrames or fps
 * in a .ani reads as a huge positive number.  That changes the result, so the
 * casts are deliberate.  The float-to-int step truncates toward zero. */

int Anim_FrameOnClock(const AnimSlot *slot, double timeMs)
{
    if (slot == NULL || slot->numFrames == 0)
        return 0;

    double count = (double)(unsigned)slot->numFrames;
    double v = (double)(unsigned)slot->fps * timeMs * 0.001 / count;
    return (int)(fmod(v, 1.0) * count);
}

int Anim_FrameAtPhase(const AnimSlot *slot, float phase)
{
    if (slot == NULL || slot->numFrames == 0)
        return 0;

    double count = (double)(unsigned)slot->numFrames;
    double first = (double)(unsigned)slot->firstFrame;

    if (slot->reverse)
        return (int)(first - count * (double)phase);
    return (int)(count * (double)phase + first);
}
