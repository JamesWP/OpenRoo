/* SwitchCells -- the cells each switch controls, embedded in Game at
 * +0x140543 (COHESION_PLAN.md Band 3).
 *
 * EXTENT, settled arithmetically: 256 switches x 256 cells x 3 bytes, then
 * one count byte per switch at +0x30000 (Game+0x170543).  0x30000 + 0x100 =
 * 0x30100, which tiles Game's span 0x140543..0x170643 exactly -- it ends
 * where the bridge slots begin.
 *
 * WRITERS AND READERS, all ours:
 *   - SetupLevelObjects (levelsetup.cpp) clears the counts, then appends
 *     (u, v) for every kind-0x11 switch cell under switch param-1;
 *   - ClearGameState (gamereset.cpp) clears the counts (one REP STOSD of
 *     0x40 dwords -- the 256 count bytes, nothing else);
 *   - MarkListedTilesBlockedByObject and FindNearestListedObjectTile
 *     (tilequery.cpp) walk switch k's cells.
 * The switch number is the bridge slot the switch arms, so list k and
 * bridge k go together (tilequery's blocking pass reads bridge k's phase).
 *
 * The third byte of each cell is never written or read by any of them; it
 * is declared, unnamed, so the stride is right.
 */
#pragma once

#include "layout.h"

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

    /* Both clearers zero exactly the 256 counts; the cells are left as the
     * previous level wrote them, and only the counts bound a walk. */
    void clearCounts()
    {
        for (int i = 0; i < SWITCHES; i++)
            counts_[i] = 0;
    }

private:
    SwitchCells() = delete;   /* game-owned; embedded in Game */
    KAROO_LAYOUT_REGISTER(SwitchCells);

    SwitchCell    cells_[SWITCHES][CELLS];            /* 0x00000 */
    unsigned char counts_[SWITCHES];                  /* 0x30000 */
};

KAROO_LAYOUT_CHECKS(SwitchCells)
{
    KAROO_LAYOUT_AT(cells_,  0x00000);
    KAROO_LAYOUT_AT(counts_, 0x30000);
    KAROO_LAYOUT_SIZE(0x30100);
}
