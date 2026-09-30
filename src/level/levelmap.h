/* LevelMap: the level's map, a sub-object of Game, read from the level's .jjm
 * file (levelmap.cpp).  A header, then a fixed 100 x 100 grid of tile records
 * (tile.h), then a second grid of the same shape: the snapshot, the level as
 * the file gave it.  The reader writes each cell's four file bytes into both.
 * The builder spawns from the snapshot's contents, a restart copies it back
 * over the live grid, and a despawning foe clears its home cell in it.
 * Objects keep the grid's first cell as their "tile base". */
#pragma once

#include <stddef.h>
#include "tile.h"

class LevelMap {
public:
     

    // The grid is 100 x 100 whatever the level's real size.
    static const int DIM = 100;

    // Objects keep the grid's first cell as their tile base; this is where
    // that pointer becomes a LevelMap again.
    static LevelMap *fromTileBase(Tile *base)
    {
        // The vptr makes LevelMap non-standard-layout; grid_'s offset is fixed.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
        return (LevelMap *)((unsigned char *)base - offsetof(LevelMap, grid_));
#pragma GCC diagnostic pop
    }
    Tile *tileBase()                     { return &grid_[0][0]; }

    Tile       *tile(int u, int v)       { return Tile::at(tileBase(), u, v); }
    const Tile *tile(int u, int v) const
    {
        return Tile::at(const_cast<LevelMap *>(this)->tileBase(), u, v);
    }
    // The same cell in the snapshot grid, which follows the live grid.
    static Tile *snapshotOf(Tile *t)
    {
        return t + DIM * DIM;
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

    LevelMap();
    virtual ~LevelMap();
    LevelMap(const LevelMap &) = delete;
    LevelMap &operator=(const LevelMap &) = delete;


private:
     

    int           timeLimit_;      // the live time limit, s; a time bonus adds 5
    unsigned int  timeElapsed_;    // ms of play
    unsigned int  bonus_;          // the level's bonus flag
    char          text010_[0x80];  // from the file; unread
    char          title_[0x80];    // the display title
    char          mapName_[0x80];  // the map name: the theme and CD-track key
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
