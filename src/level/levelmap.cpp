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

#include <strings.h>
#include "portable.h"
#include "sysdev.h"
#include <fstream>
#include "binio.h"
#include <string.h>
#include <stddef.h>
#include "logger.h"
#include "levelmap.h"
#include <stdlib.h>

#define LM_LOG_FIRST     8

static bool fx_flipx(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (sysdev::getEnv("KAROO_JJM_FX", buf, sizeof(buf)))
            cached = (strcasecmp(buf, "flipx") == 0);
        g_logger.write("levelmap: FX mode = %s\n", cached ? "flipx" : "off");
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
    char name[128];  // PRESERVED: 128 bytes, unchecked
    unsigned char hdr[2];
    unsigned x, y, width, height;
    static int logged = 0;

    strcpy(name, path);
    strcat(name, ".jjm");

    std::ifstream in(name, std::ios::binary);
    if (!in)
        return 0;

    // Width to extentU_, height to extentV_.
    readBytes(in, hdr, 2);
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
            // The stored byte is the low byte of get's result: end of file
            // lands as 0xff.
            cell->setHeight((unsigned char)in.get());
            cell->setObjectMarker((unsigned char)in.get());
            cell->setParam((unsigned char)in.get());
            cell->setContents((unsigned char)in.get());

            // The border clear comes after the read, and tests the loop's x,
            // not the flipped one; under flipx the border is the border either
            // way.
            if (y == 0 || x == 0 || y == height - 1 || x == width - 1)
                clear_file_bytes(cell);

            // The snapshot copy, after the clear.
            Tile *snap = snapshot((int)dx, (int)y);
            snap->setHeight(cell->height());
            snap->setObjectMarker(cell->objectMarker());
            snap->setParam(cell->param());
            snap->setContents(cell->contents());
        }
    }

    // The trailer, in file order.
    readBytes(in, &gemsRequired_, 4);
    readBytes(in, &fileTimeLimit_, 4);
    readBytes(in, &mapName_, 0x80);
    readBytes(in, &title_, 0x80);
    readBytes(in, &text010_, 0x80);
    readBytes(in, &bonus_, 4);

    if (logged < LM_LOG_FIRST) {
        logged++;
        g_logger.write("levelmap: '%s' %ux%u\n", name, width, height);
    }
    return 1;
}

/* The two extent bytes; the rest is left as it was. */
LevelMap::LevelMap()
{
    extentV_ = 0;
    extentU_ = 0;
}

LevelMap::~LevelMap()
{
}

