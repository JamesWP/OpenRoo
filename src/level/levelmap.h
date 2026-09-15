/* LevelMap -- the level's map, embedded in Game at +0x2ab58d (COHESION_PLAN.md
 * Band 4b).  ReadLevelMapFile 0x41f190 fills it from the level's .jjm
 * (levelmap.cpp); every Tile::at() in the game indexes from its address,
 * which is the "tile base" objects copy into their +0x34.
 *
 * EXTENT, settled arithmetically: a 0x19c-byte header, then the 100 x 100
 * grid of 0x7f-byte tile records (0x1360f0 bytes), then a second grid of the
 * same shape -- the reader writes every cell's four file bytes into both,
 * 0x1360f0 apart.  0x19c + 2 * 0x1360f0 = 0x26c37c, which ends exactly at
 * Game+0x517909 (tallyDone).  So a tile record is exactly 0x7f bytes starting
 * at tile pointer +0x19c: see tile.h's ORIGIN.
 *
 * The second grid is the SNAPSHOT: the level as the file gave it.
 * SetupLevelObjects reads its contents byte for the spawns (free bombs, timed
 * items, foes), RestoreTileGridFromSnapshot copies it back over the live grid
 * on a restart, and a despawning foe clears its home cell's contents in it.
 * Only the first four bytes of each snapshot record are ever written by the
 * reader.
 *
 * The header fields, from the reader's trailer (read order: +0x196, +0x192,
 * +0x110, +0x90, +0x10, +0xc) and from their readers:
 *   +0x04  the live time limit, seconds (SetupLevelObjects copies +0x192 in;
 *          the time-bonus pickup adds 5 -- player.cpp)
 *   +0x08  milliseconds of play
 *   +0x0c  the level's bonus flag: GameTick's restart test and the "loaded"
 *          log line's %d; OpenLevelFile peeks the NEXT level's
 *   +0x090 the level's display title (WriteLevelReport's "name" line)
 *   +0x110 the map name, the CD theme lookup's key and the "map changed" test
 *   +0x192 the time limit as the file gives it (the level report's par
 *          time is half of it)
 *   +0x196 the gem quota
 *   +0x19a / +0x19b  the v / u extents (file bytes 1 / 0 -- descending)
 * +0x00 and the 0x80-byte file string at +0x10 have no reader in our code;
 * their meaning is not decoded.
 */
#pragma once

#include "layout.h"
#include "tile.h"

class __attribute__((packed)) LevelMap {
public:
    static const int ORIGIN = 0;

    /* The grid is a fixed 100 x 100 whatever the level's real size. */
    static const int DIM        = 100;
    static const int GRID_BYTES = DIM * DIM * 0x7f;          /* 0x1360f0 */

    /* Objects keep the map's address as a raw tile base (their +0x34);
     * this is the one place that pointer becomes a LevelMap again. */
    static LevelMap *fromTileBase(unsigned char *base) { return (LevelMap *)base; }
    unsigned char   *tileBase()      { return (unsigned char *)this; }

    Tile       *tile(int u, int v)       { return Tile::at(tileBase(), u, v); }
    const Tile *tile(int u, int v) const
    {
        return Tile::at((unsigned char *)this, u, v);
    }
    /* The same cell in the snapshot grid. */
    static Tile *snapshotOf(Tile *t)
    {
        return (Tile *)((unsigned char *)t + GRID_BYTES);
    }
    Tile       *snapshot(int u, int v)       { return snapshotOf(tile(u, v)); }
    const Tile *snapshot(int u, int v) const
    {
        return snapshotOf(const_cast<Tile *>(tile(u, v)));
    }

    /* ReadLevelMapFile 0x41f190: `path` has no extension. */
    int readFile(const char *path);

    unsigned char extentU() const            { return extentU_; }
    unsigned char extentV() const            { return extentV_; }

    int          timeLimit() const           { return timeLimit_; }
    void         setTimeLimit(int s)         { timeLimit_ = s; }
    unsigned int timeElapsed() const         { return timeElapsed_; }
    void         setTimeElapsed(unsigned int ms) { timeElapsed_ = ms; }
    unsigned int bonus() const               { return bonus_; }
    const char  *title() const               { return title_; }
    const char  *mapName() const             { return mapName_; }
    int          fileTimeLimit() const       { return fileTimeLimit_; }
    int          gemsRequired() const        { return gemsRequired_; }

private:
    LevelMap() = delete;   /* game-owned; embedded in Game */
    KAROO_LAYOUT_REGISTER(LevelMap);

    unsigned char gap_000[0x004 - 0x000];
    int           timeLimit_;                     /* 0x004 */
    unsigned int  timeElapsed_;                   /* 0x008 */
    unsigned int  bonus_;                         /* 0x00c  file, read last */
    char          text010_[0x80];                 /* 0x010  file */
    char          title_[0x80];                   /* 0x090  file */
    char          mapName_[0x80];                 /* 0x110  file */
    unsigned char gap_190[0x192 - 0x190];
    int           fileTimeLimit_;                 /* 0x192  file */
    int           gemsRequired_;                  /* 0x196  file, read first */
    unsigned char extentV_;                       /* 0x19a  file byte 1 */
    unsigned char extentU_;                       /* 0x19b  file byte 0 */
    /* Indexed [u][v]: Tile::at's (v + u*100) * 0x7f is exactly this
     * array's addressing, which the checks below pin.  tile()/snapshot()
     * still go through Tile::at rather than subscripting, because the
     * bridge and slide spawn scans are unbounded and may step outside
     * [0, 100) on malformed data -- pointer arithmetic keeps that the
     * original's behaviour rather than an out-of-bounds subscript. */
    Tile          grid_[DIM][DIM];                /* 0x19c */
    Tile          snapshot_[DIM][DIM];            /* 0x13628c */
};

static_assert(sizeof(Tile) == 0x7f, "a Tile record is the 0x7f stride");
static_assert(sizeof(Tile[LevelMap::DIM][LevelMap::DIM]) == LevelMap::GRID_BYTES,
              "the grid is 100 x 100 tile records");

KAROO_LAYOUT_CHECKS(LevelMap)
{
    KAROO_LAYOUT_AT(timeLimit_,     0x004);
    KAROO_LAYOUT_AT(timeElapsed_,   0x008);
    KAROO_LAYOUT_AT(bonus_,         0x00c);
    KAROO_LAYOUT_AT(text010_,       0x010);
    KAROO_LAYOUT_AT(title_,         0x090);
    KAROO_LAYOUT_AT(mapName_,       0x110);
    KAROO_LAYOUT_AT(fileTimeLimit_, 0x192);
    KAROO_LAYOUT_AT(gemsRequired_,  0x196);
    KAROO_LAYOUT_AT(extentV_,       0x19a);
    KAROO_LAYOUT_AT(extentU_,       0x19b);
    KAROO_LAYOUT_AT(grid_,          0x19c);
    KAROO_LAYOUT_AT(snapshot_,      0x13628c);
    /* [u][v] is Tile::at(u, v): one step of u is 100 records. */
    KAROO_LAYOUT_AT(grid_[1][0],    0x19c + 100 * 0x7f);
    KAROO_LAYOUT_AT(grid_[0][1],    0x19c + 0x7f);
    KAROO_LAYOUT_AT(snapshot_[2][3], 0x13628c + (3 + 2 * 100) * 0x7f);
    KAROO_LAYOUT_SIZE(0x26c37c);
}

/* The export patch.py binds (3 E8 sites), a shim over readFile(). */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
LevelMap_ReadFile(LevelMap *self, const char *path);
