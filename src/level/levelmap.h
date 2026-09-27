/* LevelMap: the level's map, a sub-object of Game, read from the level's .jjm
 * file (levelmap.cpp).  A header, then a fixed 100 x 100 grid of tile records
 * (tile.h), then a second grid of the same shape: the snapshot, the level as
 * the file gave it.  The reader writes each cell's four file bytes into both.
 * The builder spawns from the snapshot's contents, a restart copies it back
 * over the live grid, and a despawning foe clears its home cell in it.
 * Objects keep the map's address as their "tile base". */
#pragma once

#include "layout.h"
#include "tile.h"

class __attribute__((packed)) LevelMap {
public:
    static const int ORIGIN = 0;

    // The grid is 100 x 100 whatever the level's real size.
    static const int DIM        = 100;
    static const int GRID_BYTES = DIM * DIM * 0x7f;

    // Objects keep the map's address as a raw tile base; this is where that
    // pointer becomes a LevelMap again.
    static LevelMap *fromTileBase(unsigned char *base) { return (LevelMap *)base; }
    unsigned char   *tileBase()      { return (unsigned char *)this; }

    Tile       *tile(int u, int v)       { return Tile::at(tileBase(), u, v); }
    const Tile *tile(int u, int v) const
    {
        return Tile::at((unsigned char *)this, u, v);
    }
    // The same cell in the snapshot grid.
    static Tile *snapshotOf(Tile *t)
    {
        return (Tile *)((unsigned char *)t + GRID_BYTES);
    }
    Tile       *snapshot(int u, int v)       { return snapshotOf(tile(u, v)); }
    const Tile *snapshot(int u, int v) const
    {
        return snapshotOf(const_cast<Tile *>(tile(u, v)));
    }

    // Reads <path>.jjm; path has no extension.
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

    // Called only by the Game's construction and destruction.
    void construct();
    void destruct();

private:
    LevelMap() = delete;  // embedded in the Game
    KAROO_LAYOUT_REGISTER(LevelMap);

    const void   *vtable_;         // our one-slot table
    int           timeLimit_;      // the live time limit, s; a time bonus adds 5
    unsigned int  timeElapsed_;    // ms of play
    unsigned int  bonus_;          // the level's bonus flag
    char          text010_[0x80];  // from the file; unread
    char          title_[0x80];    // the display title
    char          mapName_[0x80];  // the map name: the theme and CD-track key
    unsigned char gap_190[0x192 - 0x190];
    int           fileTimeLimit_;  // the time limit as the file gives it, s
    int           gemsRequired_;   // crystals needed to open the exit
    unsigned char extentV_;        // the v extent (file byte 1)
    unsigned char extentU_;        // the u extent (file byte 0)
    // Indexed [u][v]: one step of u is 100 records.  tile() and snapshot() go
    // through Tile::at rather than subscripting because the bridge and slide
    // spawn scans are unbounded and can step outside the grid on malformed
    // data; pointer arithmetic keeps that the game's behaviour rather than an
    // out-of-bounds subscript.
    Tile          grid_[DIM][DIM];
    Tile          snapshot_[DIM][DIM];
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
    // [u][v] is Tile::at(u, v): one step of u is 100 records.
    KAROO_LAYOUT_AT(grid_[1][0],    0x19c + 100 * 0x7f);
    KAROO_LAYOUT_AT(grid_[0][1],    0x19c + 0x7f);
    KAROO_LAYOUT_AT(snapshot_[2][3], 0x13628c + (3 + 2 * 100) * 0x7f);
    KAROO_LAYOUT_SIZE(0x26c37c);
}

/* Reads <path>.jjm into the map. */
int LevelMap_ReadFile(LevelMap *self, const char *path);

/* The one slot of LevelMap's vtable. */
LevelMap *LevelMap_ScalarDestructor(LevelMap *self, unsigned char flags);
