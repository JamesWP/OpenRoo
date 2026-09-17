/* ASSET_PLAN.md Phase 1 — ReadLevelMapFile (0x41f190), the .jjm reader.
 *
 *   int __thiscall ReadLevelMapFile(LevelMap *this, const char *path)
 *   `ret 4`, one stack argument.  3 E8 call sites (0x418772, 0x4187E4 in
 *   OpenLevelFile; 0x418994 in ParseLevelFiles), no E9, no PUSH, no vtable
 *   slot -- so CALL_PATCHES catches every caller and the original is
 *   UD2-stubbed.
 *
 * `this` is the LevelMap embedded in Game at +0x2ab58d (levelmap.h), so cell
 * byte 0 of tile (0,0) lands at Game + 0x2ab729.  It is LevelMap::readFile;
 * the export is a one-line shim (COHESION_PLAN.md Band 4b).
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
 *   +0x00  byte    width   -> extentU (this+0x19b)
 *   +0x01  byte    height  -> extentV (this+0x19a)
 *   +0x02  width*height cells of 4 bytes, y outer / x inner
 *   ...    396-byte trailer, six freads (see below)
 *
 *   len(file) == 398 + width*height*4, over every shipped level (jjm.py).
 *
 * The path argument arrives WITHOUT an extension: the reader appends ".jjm"
 * (0x4663f4) itself and opens with mode "rb" (0x465188).
 *
 * The grid is a fixed 100 x 100 array of 0x7f-byte tiles regardless of the
 * level's real size.  File x is the tile's u, file y its v:
 *
 *   cell(x, y) = this + 0x19c + y*0x7f + x*0x319c  ==  tile(x, y)
 *
 * The four file bytes land in the tile's first four fields: height, kind
 * (objectMarker), param, contents.
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
 * 3. THE CELL IS WRITTEN TWICE: at the tile, and again in the snapshot grid
 *    (tile + 0x1360f0, LevelMap::snapshotOf).  The copy happens AFTER the
 *    border zeroing, so both copies hold zeros on the border.  The snapshot
 *    is the level as loaded: the restart restore copies it back.
 *
 * 4. ONLY width*height CELLS ARE FILLED, but all 10000 are zeroed first, so
 *    the remainder of the 100x100 grid reads as zero tiles.  The zeroing pass
 *    clears the four file bytes of each tile, not the whole 0x7f record, and
 *    not the snapshot.
 *
 * 5. EOF IS NOT DETECTED.  The cells are read with getc and the byte stored is
 *    the low byte of the return value, so a truncated file yields 0xFF cells
 *    rather than an error.  The function returns 1 for everything except a
 *    failed fopen.
 *
 * 6. THE HEADER BYTES GO TO DESCENDING OFFSETS: file byte 0 (width, the u
 *    extent) to this+0x19b, byte 1 (height, the v extent) to this+0x19a.
 *
 * ─── What is deliberately NOT claimed ─────────────────────────────────────
 *
 * tools/jjm.py calls cell byte[2] the height and byte[3] the type, from the
 * engine's own skirt vertices (KAROO_QUAD_DUMP); the level builder reads byte
 * 0 as the height and byte 1 as the kind.  The field names here follow the
 * builder, which is the code that consumes them; the reader is a byte copy
 * and correct under either reading.  Recorded as an open question in
 * ASSET_PLAN.md.
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
#include <stddef.h>
#include "log.h"
#include "levelmap.h"

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

/* The four file bytes of a tile, as the zeroing pass and the border clear
 * write them. */
static void clear_file_bytes(Tile *t)
{
    t->setHeight(0);
    t->setObjectMarker(0);
    t->setParam(0);
    t->setContents(0);
}

int LevelMap::readFile(const char *path)
{
    unsigned char *base = (unsigned char *)this;
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
    extentU_ = hdr[0];
    extentV_ = hdr[1];

    /* Zero all 100*100 tiles' four file bytes.  The original walks a single
     * pointer in 0x7f steps for 10000 iterations -- index v + u*100 rising,
     * so u outer and v inner, the same addresses in the same order. */
    for (int u = 0; u < DIM; u++)
        for (int v = 0; v < DIM; v++)
            clear_file_bytes(tile(u, v));

    width  = extentU_;
    height = extentV_;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            unsigned dx = fx_flipx() ? (width - 1 - x) : x;
            Tile *cell = tile((int)dx, (int)y);
            /* getc, four times.  The stored byte is the low byte of the
             * return value, so EOF lands as 0xFF -- see defect 5. */
            cell->setHeight((unsigned char)fgetc(fp));
            cell->setObjectMarker((unsigned char)fgetc(fp));
            cell->setParam((unsigned char)fgetc(fp));
            cell->setContents((unsigned char)fgetc(fp));

            /* Border clear, AFTER the read (defect 2).  Tested against the
             * true x, not the flipped one: the original's test is on the loop
             * counter, and under flipx the border is the border either way. */
            if (y == 0 || x == 0 || y == height - 1 || x == width - 1)
                clear_file_bytes(cell);

            /* The snapshot copy, after the clear (defect 3). */
            Tile *snap = snapshotOf(cell);
            snap->setHeight(cell->height());
            snap->setObjectMarker(cell->objectMarker());
            snap->setParam(cell->param());
            snap->setContents(cell->contents());
        }
    }

    /* Trailer, in the original's order. */
    fread(base + offsetof(LevelMap, gemsRequired_),  4,    1, fp);
    fread(base + offsetof(LevelMap, fileTimeLimit_), 4,    1, fp);
    fread(base + offsetof(LevelMap, mapName_),       0x80, 1, fp);
    fread(base + offsetof(LevelMap, title_),         0x80, 1, fp);
    fread(base + offsetof(LevelMap, text010_),       0x80, 1, fp);
    fread(base + offsetof(LevelMap, bonus_),         4,    1, fp);

    fclose(fp);

    if (logged < LM_LOG_FIRST) {
        logged++;
        log_write("levelmap: '%s' %ux%u\n", name, width, height);
    }
    return 1;
}

extern "C" __declspec(dllexport) int __attribute__((thiscall))
LevelMap_ReadFile(LevelMap *self, const char *path)
{
    return self->readFile(path);
}
