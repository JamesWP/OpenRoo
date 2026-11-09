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
 *   4. Only width * height cells are filled; the four file bytes of each
 *      are zeroed first (not the rest of the record, not the snapshot):
 *      a cell keeps the rest of its record from the level read before, if
 *      that had the cell too.
 *   5. End of file is not detected: a truncated file gives 0xff cells, and
 *      only a failed open returns 0. */

#include <strings.h>
#include "sysdev.h"
#include <fstream>
#include "binio.h"
#include <string.h>
#include <stddef.h>
#include "logger.h"
#include "levelmap.h"
#include <stdlib.h>

#define LM_LOG_FIRST     8

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

    std::ifstream in(sysdev::nativePath(name), std::ios::binary);
    if (!in)
        return 0;

    // Width to extentU_, height to extentV_; the grids to the new size, each
    // cell the last level had kept where it was.
    readBytes(in, hdr, 2);
    resize(hdr[0], hdr[1]);

    // Zero every tile's four file bytes.
    for (Tile &t : grid_)
        clear_file_bytes(&t);

    width  = extentU_;
    height = extentV_;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            Tile *cell = tile((int)x, (int)y);
            // The stored byte is the low byte of get's result: end of file
            // lands as 0xff.
            cell->setHeight((unsigned char)in.get());
            cell->setObjectMarker((unsigned char)in.get());
            cell->setParam((unsigned char)in.get());
            cell->setContents((unsigned char)in.get());

            // The border clear comes after the read.
            if (y == 0 || x == 0 || y == height - 1 || x == width - 1)
                clear_file_bytes(cell);

            // The snapshot copy, after the clear.
            Tile *snap = snapshot((int)x, (int)y);
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

LevelMap::LevelMap()
{
    extentV_ = 0;
    extentU_ = 0;
}

void LevelMap::resize(unsigned char eu, unsigned char ev)
{
    auto remap = [&](std::vector<Tile> &grid) {
        std::vector<Tile> next((size_t)eu * ev);
        for (int v = 0; v < ev && v < extentV_; v++)
            for (int u = 0; u < eu && u < extentU_; u++)
                next[(size_t)v * eu + u] = grid[(size_t)v * extentU_ + u];
        grid.swap(next);
    };
    remap(grid_);
    remap(snapshot_);
    extentU_ = eu;
    extentV_ = ev;
}

