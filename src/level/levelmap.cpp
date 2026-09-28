/* FORMAT: a .jjm level map, opened "rb":
 *   +0x00  byte    width  (the u extent)
 *   +0x01  byte    height (the v extent)
 *   +0x02  width * height cells of 4 bytes, y outer, x inner:
 *          height, kind, param, contents
 *   then a trailer: crystals needed (4), time limit (4), map name (0x80),
 *   title (0x80), an unread string (0x80), bonus flag (4)
 * File x is the tile's u, y its v.  tools/jjm.py reads the same format.
 *
 * PRESERVED:
 *   1. The path is built unchecked in a 128-byte buffer.
 *   2. Every border cell is zeroed after it is read, so the file position
 *      advances as for any cell.
 *   3. Each cell is copied into the snapshot after the border clear.
 *   4. Only width * height cells are filled; the four file bytes of all
 *      10000 are zeroed first (not the rest of the record, not the
 *      snapshot).
 *   5. End of file is not detected: a truncated file gives 0xff cells, and
 *      only a failed open returns 0.
 *
 * KAROO_JJM_FX=flipx is a negative control: the map is mirrored left to right
 * as it is read. */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "log.h"
#include "levelmap.h"
#include <stdlib.h>

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

/* The four file bytes of a tile. */
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
    char name[128];  // PRESERVED: 128 bytes, unchecked
    FILE *fp;
    unsigned char hdr[2];
    unsigned x, y, width, height;
    static int logged = 0;

    strcpy(name, path);
    strcat(name, ".jjm");

    fp = fopen(name, "rb");
    if (fp == NULL)
        return 0;

    // Width to extentU_, height to extentV_.
    fread(hdr, 2, 1, fp);
    extentU_ = hdr[0];
    extentV_ = hdr[1];

    // Zero every tile's four file bytes, u outer and v inner.
    for (int u = 0; u < DIM; u++)
        for (int v = 0; v < DIM; v++)
            clear_file_bytes(tile(u, v));

    width  = extentU_;
    height = extentV_;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            unsigned dx = fx_flipx() ? (width - 1 - x) : x;
            Tile *cell = tile((int)dx, (int)y);
            // The stored byte is the low byte of getc's result: end of file
            // lands as 0xff.
            cell->setHeight((unsigned char)fgetc(fp));
            cell->setObjectMarker((unsigned char)fgetc(fp));
            cell->setParam((unsigned char)fgetc(fp));
            cell->setContents((unsigned char)fgetc(fp));

            // The border clear comes after the read, and tests the loop's x,
            // not the flipped one; under flipx the border is the border either
            // way.
            if (y == 0 || x == 0 || y == height - 1 || x == width - 1)
                clear_file_bytes(cell);

            // The snapshot copy, after the clear.
            Tile *snap = snapshotOf(cell);
            snap->setHeight(cell->height());
            snap->setObjectMarker(cell->objectMarker());
            snap->setParam(cell->param());
            snap->setContents(cell->contents());
        }
    }

    // The trailer, in file order.
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

static void *const g_LevelMapVtable[1] = { (void *)&LevelMap::scalarDeletingDtor };

/* The vtable and the two extent bytes; the rest is left as it was. */
void LevelMap::construct()
{
    vtable_  = g_LevelMapVtable;
    extentV_ = 0;
    extentU_ = 0;
}

void LevelMap::destruct()
{
    vtable_ = g_LevelMapVtable;
}

LevelMap *__attribute__((thiscall))
LevelMap::scalarDeletingDtor(LevelMap *self, unsigned char flags)
{
    self->destruct();
    if (flags & 1)
        free(self);
    return self;
}
