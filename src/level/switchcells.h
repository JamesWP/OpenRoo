/* SwitchCells: the cells each switch controls, a sub-object of Game.  The
 * level builder lists every switch cell under its switch number; the tile
 * queries walk switch k's cells.  Switch k arms bridge k, so list k and bridge
 * k go together. */
#pragma once

 

/* One cell: its grid position.  The third byte is never used. */
struct SwitchCell {
    unsigned char u{};
    unsigned char v{};
    unsigned char field_2{};
};

class SwitchCells {
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

    SwitchCells() = default;  // left uninitialised until clearCounts()

private:

    SwitchCell    cells_[SWITCHES][CELLS];
    unsigned char counts_[SWITCHES]{};
};