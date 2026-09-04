/* ASSET_PLAN.md Phase 1 — ReadLevelMapFile (0x41f190), the .jjm reader.
 *
 *   int __thiscall ReadLevelMapFile(LevelMap *this, const char *path)
 *   `ret 4`, one stack argument.  3 E8 call sites (0x418772, 0x4187E4 in
 *   OpenLevelFile; 0x418994 in ParseLevelFiles), no E9, no PUSH, no vtable
 *   slot -- so CALL_PATCHES catches every caller and the original is
 *   UD2-stubbed.
 *
 * `this` is an interior pointer: OpenLevelFile passes Game + 0x2ab58d, so
 * cell byte 0 of tile (0,0) lands at Game + 0x2ab729.
 *
 * ─── No calls into the game binary ────────────────────────────────────────
 *
 * Per ASSET_PLAN.md's no-callback rule: this file opens the file with our own
 * CRT, reads it with our own CRT, and writes the game's memory directly.  It
 * calls nothing in Karoo.exe.  The Phase 0 exemption in assetio.cpp (which
 * must hand back an MSVC FILE*) does not apply here -- no FILE* we create ever
 * reaches the game.
 *
 * A consequence worth knowing when reading logs: the asset log
 * (KAROO_ASSET_LOG) will no longer show .jjm opens at all after this change,
 * because our fopen is not the game's fopen.  That is expected, and it is the
 * shape every later phase will take.
 *
 * ─── The file, as the original reads it ───────────────────────────────────
 *
 *   +0x00  byte    width   -> this+0x19b
 *   +0x01  byte    height  -> this+0x19a
 *   +0x02  width*height cells of 4 bytes, y outer / x inner
 *   ...    396-byte trailer, six freads (see below)
 *
 *   len(file) == 398 + width*height*4, over every shipped level (jjm.py).
 *
 * The path argument arrives WITHOUT an extension: the reader appends ".jjm"
 * (0x4663f4) itself and opens with mode "rb" (0x465188).
 *
 * The grid is a fixed 100 x 100 array of 0x7f-byte tiles regardless of the
 * level's real size, based at this+0x19c:
 *
 *   cell(x, y) = this + 0x19c + y*0x7f + x*0x319c        (0x319c == 100*0x7f)
 *
 * ─── Defects and oddities preserved deliberately ──────────────────────────
 *
 * 1. THE PATH BUFFER IS 128 BYTES AND UNCHECKED.  The original does strcpy
 *    then strcat into a 128-byte stack buffer with no bounds test; a path
 *    longer than 123 characters smashes its own frame.  Reproduced, including
 *    the buffer size, rather than "fixed" into a truncation that would change
 *    which paths work.
 *
 * 2. EVERY BORDER CELL IS ZEROED AFTER BEING READ.  x == 0, y == 0,
 *    x == width-1 or y == height-1 clears all four bytes.  The bytes are still
 *    consumed from the file first -- this is a post-read clear, not a skip, so
 *    the file position advances identically.
 *
 * 3. THE CELL IS WRITTEN TWICE: at the tile, and again at tile + 0x1360f0.
 *    The copy happens AFTER the border zeroing, so both copies hold zeros on
 *    the border.  Which copy is authoritative is not established; both are
 *    written because the original writes both.
 *
 * 4. ONLY width*height CELLS ARE FILLED, but all 10000 are zeroed first, so
 *    the remainder of the 100x100 grid reads as zero tiles.  The zeroing pass
 *    clears 4 bytes per tile (tile+0 .. tile+3), not the whole 0x7f stride.
 *
 * 5. EOF IS NOT DETECTED.  The cells are read with getc and the byte stored is
 *    the low byte of the return value, so a truncated file yields 0xFF cells
 *    rather than an error.  The function returns 1 for everything except a
 *    failed fopen.
 *
 * 6. THE HEADER BYTES GO TO DESCENDING OFFSETS: file byte 0 (width) to
 *    this+0x19b, byte 1 (height) to this+0x19a.  Not a typo here.
 *
 * ─── What is deliberately NOT claimed ─────────────────────────────────────
 *
 * What the four cell bytes MEAN is not settled, and this file does not need it
 * to be.  tools/jjm.py calls byte[2] the height (established from the engine's
 * own skirt vertices via KAROO_QUAD_DUMP) and byte[3] the type; AI_PLAN.md's
 * tile layout, read from SetupLevelObjects, would instead put the height at
 * cell byte 0.  Those two readings disagree, and resolving it is not this
 * phase's job: a byte-for-byte copy into the same addresses is correct under
 * either interpretation.  Recorded as an open question in ASSET_PLAN.md.
 *
 * ─── Visual proof ─────────────────────────────────────────────────────────
 *
 * KAROO_JJM_FX=flipx mirrors the map about its vertical axis as it is read --
 * the level comes out left-right reversed, which only this code path can do.
 * A geometry change, not a colour, in the sense CLAUDE.md prefers.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"

/* Offsets from `this` (Game + 0x2ab58d at every call site). */
#define LM_TRAILER_C     0x00c   /* 4 bytes, read last            */
#define LM_TRAILER_10    0x010   /* 0x80 bytes                    */
#define LM_TRAILER_90    0x090   /* 0x80 bytes                    */
#define LM_TRAILER_110   0x110   /* 0x80 bytes                    */
#define LM_TRAILER_192   0x192   /* 4 bytes                       */
#define LM_TRAILER_196   0x196   /* 4 bytes, read first           */
#define LM_HEIGHT        0x19a   /* byte: file byte 1             */
#define LM_WIDTH         0x19b   /* byte: file byte 0             */
#define LM_GRID          0x19c   /* cell(0,0)                     */

#define LM_ROW_STRIDE    0x7f    /* per y */
#define LM_COL_STRIDE    0x319c  /* per x == 100 * 0x7f */
#define LM_GRID_DIM      100
#define LM_SHADOW_DELTA  0x1360f0 /* the second copy of every cell */

#define LM_LOG_FIRST     8

static bool fx_flipx(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (GetEnvironmentVariableA("KAROO_JJM_FX", buf, sizeof(buf)))
            cached = (lstrcmpiA(buf, "flipx") == 0);
        log_write("levelmap: FX mode = %s\n", cached ? "flipx" : "off");
    }
    return cached != 0;
}

extern "C" __declspec(dllexport) int __attribute__((thiscall))
LevelMap_ReadFile(void *self, const char *path)
{
    unsigned char *base = (unsigned char *)self;
    char name[128];              /* 128 and unchecked, exactly as the original */
    FILE *fp;
    unsigned char hdr[2];
    unsigned x, y, width, height;
    static int logged = 0;

    strcpy(name, path);
    strcat(name, ".jjm");

    fp = fopen(name, "rb");
    if (fp == NULL)
        return 0;

    /* Header: one 2-byte fread, then the two bytes split to descending
     * offsets -- width to +0x19b, height to +0x19a. */
    fread(hdr, 2, 1, fp);
    base[LM_WIDTH]  = hdr[0];
    base[LM_HEIGHT] = hdr[1];

    /* Zero all 100*100 tiles' first four bytes.  The original walks a single
     * pointer in 0x7f steps for 10000 iterations, which is the same set of
     * addresses as the (x, y) form below. */
    {
        unsigned char *p = base + LM_GRID;
        for (int i = 0; i < LM_GRID_DIM * LM_GRID_DIM; i++) {
            p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 0;
            p += LM_ROW_STRIDE;
        }
    }

    width  = base[LM_WIDTH];
    height = base[LM_HEIGHT];

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            unsigned dx = fx_flipx() ? (width - 1 - x) : x;
            unsigned char *cell = base + LM_GRID + y * LM_ROW_STRIDE
                                                 + dx * LM_COL_STRIDE;
            /* getc, four times.  The stored byte is the low byte of the
             * return value, so EOF lands as 0xFF -- see defect 5. */
            cell[0] = (unsigned char)fgetc(fp);
            cell[1] = (unsigned char)fgetc(fp);
            cell[2] = (unsigned char)fgetc(fp);
            cell[3] = (unsigned char)fgetc(fp);

            /* Border clear, AFTER the read (defect 2).  Tested against the
             * true x, not the flipped one: the original's test is on the loop
             * counter, and under flipx the border is the border either way. */
            if (y == 0 || x == 0 || y == height - 1 || x == width - 1) {
                cell[0] = 0; cell[1] = 0; cell[2] = 0; cell[3] = 0;
            }

            /* The second copy, after the clear (defect 3). */
            cell[LM_SHADOW_DELTA + 0] = cell[0];
            cell[LM_SHADOW_DELTA + 1] = cell[1];
            cell[LM_SHADOW_DELTA + 2] = cell[2];
            cell[LM_SHADOW_DELTA + 3] = cell[3];
        }
    }

    /* Trailer, in the original's order. */
    fread(base + LM_TRAILER_196, 4,    1, fp);
    fread(base + LM_TRAILER_192, 4,    1, fp);
    fread(base + LM_TRAILER_110, 0x80, 1, fp);
    fread(base + LM_TRAILER_90,  0x80, 1, fp);
    fread(base + LM_TRAILER_10,  0x80, 1, fp);
    fread(base + LM_TRAILER_C,   4,    1, fp);

    fclose(fp);

    if (logged < LM_LOG_FIRST) {
        logged++;
        log_write("levelmap: '%s' %ux%u\n", name, width, height);
    }
    return 1;
}
