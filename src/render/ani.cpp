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
 * ─── THE TYPES, NOW THAT THERE ARE SOME ────────────────────────────────
 *
 * This file used to address the table as a `void *` plus hex offsets because
 * the animation structures had not been reverse-engineered.  They have been:
 * `AnimSlot`, `AnimTable` and the animation codes live in `ani.h`, which also
 * carries the evidence summary, and every offset in this file is gone.  The
 * loader's `dest` is an `AnimTable *`; `Ani_LookupAnimDescriptor` returns an
 * `AnimSlot *`.  ANIM_PLAN.md holds the rest -- in particular which animation
 * code is still read out of the game binary rather than ours.
 *
 * ─── Visual proof ─────────────────────────────────────────────────────────
 *
 * KAROO_ANI_FX=freeze forces every loaded slot to a single frame
 * (numFrames = 1), so animated models hold their first frame while everything
 * else about them -- position, path, texture, lighting -- stays.  A motion
 * change, not a colour, and it is a change only this code can make: nothing
 * else writes the table.
 *
 * It used to set numFrames = firstFrame, described as "end frame = start
 * frame".  That was a misreading of a slot whose fields were unnamed: +0x4 is
 * a COUNT, not an end frame, so the old control froze nothing -- it gave each
 * animation a new arbitrary length (and, for the common firstFrame = 0, the
 * numFrames = 0 that means "no animation at all").
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include "log.h"

/* The game's logger.  This used to be the one callback in this file, reaching
 * into Karoo.exe at 0x00441b10; since gamelog.cpp replaced the Logger class
 * that original is UD2-stubbed, and this calls our own writer instead.
 *
 * It called it through an ORIG_LOG_MESSAGE macro that cast the export to
 * (void *, int, const char *, ...).  The name said "original" when it was
 * already ours, and the cast would have turned a signature change into a
 * crash rather than a compile error (COHESION_PLAN.md Band 7c), so both are
 * gone and GameLog_LogMessage is called by name. */
#include "gamelog.h"
#include "gamestr.h"
#include "ani.h"

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
Ani_LoadAnimationFile(AnimTable *dest, const char *path, GameLogger *logger)
{
    unsigned char *table = (unsigned char *)dest;
    char line[ANI_LINE_MAX];
    FILE *fp;
    static int logged = 0;

    memset(table, 0, sizeof(AnimTable));

    if (path == NULL || path[0] == '\0')
        return 0;                                  /* defect 4 */

    fp = fopen(path, "r");
    if (fp == NULL)
        return 0;

    while (!feof(fp)) {
        char *tok;
        unsigned count = 0;
        AnimSlot *slot = NULL;

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
                slot = (AnimSlot *)(table + ANI_SLOTS[i].off);  /* defect 6 */
                break;
            }
        }
        if (slot == NULL)
            continue;

        slot->firstFrame = atoi(s_tokens[1]);
        slot->numFrames  = atoi(s_tokens[2]);
        slot->fps        = atoi(s_tokens[3]);

        if (fx_freeze())
            slot->numFrames = 1;                   /* hold the first frame */

        if (count > 4 && strcmp(ani_strlwr(s_tokens[4]), "r") == 0)
            slot->reverse = 1;
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

/* ─── LookupAnimDescriptor 0x00401970 ────────────────────────────────────
 *
 * The reader half of the table above: an animation CODE in, the address of
 * that animation's 0x10-byte slot out, or 0 for "this code has no animation".
 * Five CALL sites (0x40a0e7, 0x40a165, 0x421c8b, 0x43c39a, 0x43c418), no
 * other reference; __cdecl with a plain RET, so the caller cleans up.
 *
 * The original is a 0xfc-entry byte index at 0x401ab0 feeding a 22-entry
 * jump table at 0x401a5c; each arm is `LEA EAX,[arg + slot]; RET`.  A switch
 * is the same program and reads as what it is.  Two details are semantics,
 * not shape, and are preserved: the argument is masked to a BYTE before the
 * range test (`AND ECX,0xff`), so 0x114 is code 0x14, not out of range; and
 * every code outside the table -- including code 0 -- returns 0, because EAX
 * is zeroed before the dispatch and the default arm is the bare RET.
 *
 * The original computes `table + offset` and never dereferences either, so
 * it has no NULL check and neither does this: the arms are `&table->slot`
 * and a NULL table would fault in the caller exactly as before.
 *
 * The slot offsets are the ones the loader above fills, which is what names
 * the codes: 0x14 walk_forward, 0x15 walk_backward, 0xb jump, 9 glue,
 * 10 ghost, 3 ice, 8 fall, 5 paraglide, 4 slide, 0xfa idle1, 0xfb idle2,
 * 0x16..0x1b the six stair transitions, 0x1f turn_left, 0x1e turn_right.
 * Note that the four speed_/slow_ slots (+0x20..+0x50) and celebration
 * (+0x60) have NO code: nothing can reach them through this function.
 */
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
    default:                    return NULL;   /* code 0 included */
    }
}

/* ─── The two evaluators ─────────────────────────────────────────────────
 *
 * Neither is called from the game binary: they are the arithmetic the four
 * remaining consumer call sites open-code, lifted here so the replacements
 * of those functions (ANIM_PLAN.md) share one copy.  They are written from
 * the disassembly of RenderSceneObjects 0x40a0e7 / 0x40a165, which
 * DrawObjectShadows 0x43c39a / 0x43c418 repeats instruction for instruction.
 *
 * `0.001` is the constant at 0x45d368 -- milliseconds to seconds, so `fps`
 * really is frames per second.
 *
 * Both originals FILD a QWORD whose high dword they have just zeroed, which
 * is an UNSIGNED load of the dword below it: a negative FirstFrame, NumFrames
 * or FPS in a .ani reads as a huge positive number, not as a negative.  That
 * is semantics, not rounding, so the casts below are deliberate.  (No shipped
 * .ani has a negative field; `atoi` would accept one.)
 *
 * The original's float-to-int step is __ftol (0x451134), i.e. truncation
 * toward zero, which is what a C cast does. */

int Anim_FrameOnClock(const AnimSlot *slot, double timeMs)
{
    if (slot == NULL || slot->numFrames == 0)
        return 0;                                  /* both guards are the
                                                    * caller's, not ours */

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
