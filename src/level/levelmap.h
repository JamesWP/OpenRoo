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

    // The cell at (u, v), either axis signed.  A cell outside the grid is a
    // scratch solid wall, fresh on every call, so a scan that steps off the
    // grid stops there and a write to it is lost.
    Tile       *tile(int u, int v)       { return cellAt(grid_, u, v); }
    const Tile *tile(int u, int v) const
    {
        return cellAt(const_cast<LevelMap *>(this)->grid_, u, v);
    }
    // The same cell in the snapshot grid.
    Tile       *snapshot(int u, int v)       { return cellAt(snapshot_, u, v); }
    const Tile *snapshot(int u, int v) const
    {
        return cellAt(const_cast<LevelMap *>(this)->snapshot_, u, v);
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
     

    int           timeLimit_{};      // the live time limit, s; a time bonus adds 5
    unsigned int  timeElapsed_{};    // ms of play
    unsigned int  bonus_{};          // the level's bonus flag
    char          text010_[0x80]{};  // from the file; unread
    char          title_[0x80]{};    // the display title
    char          mapName_[0x80]{};  // the map name: the theme and CD-track key
    int           fileTimeLimit_{};  // the time limit as the file gives it, s
    int           gemsRequired_{};   // crystals needed to open the exit
    unsigned char extentV_{};        // the v extent (file byte 1)
    unsigned char extentU_{};        // the u extent (file byte 0)
    // Indexed [u][v].
    Tile          grid_[DIM][DIM];
    Tile          snapshot_[DIM][DIM];
    mutable Tile  outside_;        // what tile() hands out beyond the grid

    Tile *cellAt(Tile (&grid)[DIM][DIM], int u, int v) const
    {
        if (u >= 0 && u < DIM && v >= 0 && v < DIM)
            return &grid[u][v];
        outside_ = Tile::outsideGrid();
        return &outside_;
    }
};
