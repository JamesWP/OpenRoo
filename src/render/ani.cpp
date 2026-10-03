/* The .ani reader (LoadAnimationFile), the table lookup (LookupAnimDescriptor)
 * and the two frame evaluators (ani.h).
 *
 * FORMAT: a .ani is a plain-text table, opened in text mode and read a line
 * at a time.  Per line:
 *   - "//" at the start is a comment and a blank line is empty; both are skipped.
 *   - The line is split into whitespace-separated tokens.
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

#include <strings.h>
#include "portable.h"
#include "sysdev.h"
#include <fstream>
#include <sstream>
#include <string>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include "logger.h"

#include "gamestr.h"
#include "ani.h"
#include <algorithm>
#include <iterator>

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

static bool fx_freeze(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (sysdev::getEnv("KAROO_ANI_FX", buf, sizeof(buf)))
            cached = (strcasecmp(buf, "freeze") == 0);
        g_logger.write("ani: FX mode = %s\n", cached ? "freeze" : "off");
    }
    return cached != 0;
}

/* 'A'..'Z' += 0x20. */
static char ani_lower(char c)
{
    return (c > '@' && c < '[') ? (char)(c + ' ') : c;
}

int AnimTable::load(const char *path)
{
    unsigned char *table = (unsigned char *)this;
    static int logged = 0;

    *this = AnimTable();

    if (path == NULL || path[0] == '\0')
        return 0;  // PRESERVED: after the table is cleared

    std::ifstream in(path);  // text mode: the CRT folds CRLF
    if (!in)
        return 0;

    std::string line;
    while (std::getline(in, line)) {
        if (line.compare(0, 2, "//") == 0)
            continue;

        std::istringstream words(line);
        std::string tok[5];
        unsigned count = 0;
        for (std::string w; words >> w; count++)
            if (count < 5)
                tok[count] = w;

        if (count <= 3)
            continue;

        AnimSlot *slot = NULL;
        std::transform(tok[0].begin(), tok[0].end(), tok[0].begin(), ani_lower);
        for (unsigned i = 0; i < ANI_SLOT_COUNT; i++) {
            if (tok[0] == ANI_SLOTS[i].key) {
                slot = (AnimSlot *)(table + ANI_SLOTS[i].off);
                break;
            }
        }
        if (slot == NULL)
            continue;

        slot->firstFrame_ = atoi(tok[1].c_str());
        slot->numFrames_  = atoi(tok[2].c_str());
        slot->fps_        = atoi(tok[3].c_str());

        if (fx_freeze())
            slot->numFrames_ = 1;  // hold the first frame

        if (count > 4) {
            std::transform(tok[4].begin(), tok[4].end(), tok[4].begin(), ani_lower);
            if (tok[4] == "r")
                slot->reverse_ = 1;
        }
    }

    // KAROO_ANI_DUMP=<path>: the whole table, hashed, for comparison against
    // an independent parse.
    {
        char dump[kMaxPath];
        if (sysdev::getEnv("KAROO_ANI_DUMP", dump, sizeof(dump))) {
            std::ofstream h(dump, std::ios::binary | std::ios::app);
            if (h) {
                unsigned long hash = 2166136261UL;
                for (unsigned i = 0; i < sizeof(AnimTable); i++) {
                    hash ^= table[i];
                    hash *= 16777619UL;
                }
                char l[768];
                int n = snprintf(l, sizeof(l), "%s table=%08lx\r\n", path, hash);
                h.write(l, n);
            }
        }
    }

    g_logger.logMessage(1, "ANI: %s loaded", path);

    if (logged < ANI_LOG_FIRST) {
        logged++;
        g_logger.write("ani: '%s' loaded\n", path);
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
AnimSlot *AnimTable::lookup(unsigned int code)
{
    switch (code & 0xff) {
    case ANIM_WALK_FORWARD:     return &walkForward_;
    case ANIM_WALK_BACKWARD:    return &walkBackward_;
    case ANIM_JUMP:             return &jump_;
    case ANIM_GLUE:             return &glue_;
    case ANIM_GHOST:            return &ghost_;
    case ANIM_ICE:              return &ice_;
    case ANIM_FALL:             return &fall_;
    case ANIM_PARAGLIDE:        return &paraglide_;
    case ANIM_SLIDE:            return &slide_;
    case ANIM_IDLE1:            return &idle1_;
    case ANIM_IDLE2:            return &idle2_;
    case ANIM_FIELD_STAIR_UP:   return &fieldStairUp_;
    case ANIM_FIELD_STAIR_DOWN: return &fieldStairDown_;
    case ANIM_STAIR_STAIR_UP:   return &stairStairUp_;
    case ANIM_STAIR_STAIR_DOWN: return &stairStairDown_;
    case ANIM_STAIR_FIELD_UP:   return &stairFieldUp_;
    case ANIM_STAIR_FIELD_DOWN: return &stairFieldDown_;
    case ANIM_TURN_LEFT:        return &turnLeft_;
    case ANIM_TURN_RIGHT:       return &turnRight_;
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

int AnimSlot::frameOnClock(const AnimSlot *slot, double timeMs)
{
    if (slot == NULL || slot->numFrames_ == 0)
        return 0;

    double count = (double)(unsigned)slot->numFrames_;
    double v = (double)(unsigned)slot->fps_ * timeMs * 0.001 / count;
    return (int)(fmod(v, 1.0) * count);
}

int AnimSlot::frameAtPhase(const AnimSlot *slot, float phase)
{
    if (slot == NULL || slot->numFrames_ == 0)
        return 0;

    double count = (double)(unsigned)slot->numFrames_;
    double first = (double)(unsigned)slot->firstFrame_;

    if (slot->reverse_)
        return (int)(first - count * (double)phase);
    return (int)(count * (double)phase + first);
}
