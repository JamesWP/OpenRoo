/* SwitchCells: the cells each switch controls, a sub-object of Game.  The
 * level builder lists every switch cell under its switch number; the tile
 * queries walk switch k's cells.  Switch k arms bridge k, so list k and bridge
 * k go together. */
#pragma once

#include "layout.h"

/* One cell: its grid position.  The third byte is never used. */
struct __attribute__((packed)) SwitchCell {
    unsigned char u;
    unsigned char v;
    unsigned char field_2;
};

class __attribute__((packed)) SwitchCells {
public:
    static const int ORIGIN   = 0;
    static const int SWITCHES = 256;
    static const int CELLS    = 256;

    unsigned char count(unsigned sw) const       { return counts_[sw & 0xff]; }
    void setCount(unsigned sw, unsigned char n)  { counts_[sw & 0xff] = n; }

    unsigned char cellU(unsigned sw, unsigned i) const { return cells_[sw & 0xff][i & 0xff].u; }
    unsigned char cellV(unsigned sw, unsigned i) const { return cells_[sw & 0xff][i & 0xff].v; }
    void setCellU(unsigned sw, unsigned i, unsigned char u) { cells_[sw & 0xff][i & 0xff].u = u; }
    void setCellV(unsigned sw, unsigned i, unsigned char v) { cells_[sw & 0xff][i & 0xff].v = v; }

    // Only the counts are cleared; the cells keep the previous level's values,
    // and only the counts bound a walk.
    void clearCounts()
    {
        for (int i = 0; i < SWITCHES; i++)
            counts_[i] = 0;
    }

private:
    SwitchCells() = delete;  // embedded in the Game
    KAROO_LAYOUT_REGISTER(SwitchCells);

    SwitchCell    cells_[SWITCHES][CELLS];
    unsigned char counts_[SWITCHES];
};

KAROO_LAYOUT_CHECKS(SwitchCells)
{
    KAROO_LAYOUT_AT(cells_,  0x00000);
    KAROO_LAYOUT_AT(counts_, 0x30000);
    KAROO_LAYOUT_SIZE(0x30100);
}
