/* Tile -- one cell of the level grid (COHESION_PLAN.md Band 4b).
 *
 * SKETCH.  Only the fields a converted class reads or writes are declared;
 * unconverted files still reach the rest by raw offset, and join this class
 * as they are converted.
 *
 * ORIGIN.  The game addresses a cell as `tileBase + (v + u*100) * 0x7f` --
 * "the tile pointer" in every existing file and FIELD_CENSUS_REPORT.txt, and
 * the offsets asserted below are relative to it.  It is NOT where the cell's
 * record starts: the fields in use span +0x19a..+0x21b, wider than the 0x7f
 * stride, so the true record boundary is unsettled RE.
 *
 * For now the struct starts AT the tile pointer (ORIGIN 0) behind a leading
 * gap.  When Band 4b settles the record start, set ORIGIN to it and drop the
 * gap: at() and every KAROO_LAYOUT_AT below keep working unchanged, because
 * they are written against the game's tile pointer, not against the struct.
 * Because the fields overrun the stride, a Tile is an overlay reached only
 * through at() -- never index an array of them.
 */
#pragma once

#include <string.h>

#include "layout.h"

class __attribute__((packed)) Tile {
public:
    static const int ORIGIN = 0;

    /* Both axes are signed: the lift tick reads its cell as s8.  The
     * arithmetic is the original's, stated once. */
    static Tile *at(unsigned char *base, int u, int v)
    {
        return (Tile *)(base + (v + u * 100) * 0x7f + ORIGIN);
    }

    /* +0x19c  the cell's height byte. */
    void setHeight(unsigned char h)            { height_ = h; }

    /* ── published by a lift standing on this cell (liftobject.cpp) ── */
    /* The two floats are written as raw bits, as the original's integer
     * moves do, so no value passes through the FPU. */
    void setLiftLiveHeightBits(unsigned int b) { memcpy(&liftLiveHeight_, &b, 4); }
    void setLiftSlot(unsigned char n)          { liftSlot_ = n; }
    signed char liftBottom() const             { return liftBottom_; }
    signed char liftTop() const                { return liftTop_; }
    void setLiftBottom(unsigned char h)        { liftBottom_ = (signed char)h; }
    void setLiftTop(unsigned char h)           { liftTop_ = (signed char)h; }
    void setLiftMovingSince(const void *d)     { memcpy(&liftMovingSince_, d, 8); }
    void clearLiftMovingSince()                { memset(&liftMovingSince_, 0, 8); }
    void setLiftParkedSince(const void *d)     { memcpy(&liftParkedSince_, d, 8); }
    void setField_1e5(int x)                   { field_1e5_ = x; }
    void setLiftDwellBits(unsigned int b)      { memcpy(&liftDwell_, &b, 4); }

private:
    Tile() = delete;   /* game-owned; only ever reached through at() */
    static void assertLayout();

    unsigned char gap_000[0x19c - 0x000];
    unsigned char height_;            /* 0x19c                              */
    unsigned char gap_19d[0x1a6 - 0x19d];
    float         liftLiveHeight_;    /* 0x1a6  the lift's live height      */
    unsigned char gap_1aa[0x1d2 - 0x1aa];
    unsigned char liftSlot_;          /* 0x1d2  which lift slot stands here */
    signed char   liftBottom_;        /* 0x1d3  lift parks here at bottom   */
    signed char   liftTop_;           /* 0x1d4  ... and here at top         */
    double        liftMovingSince_;   /* 0x1d5  phase start while MOVING    */
    double        liftParkedSince_;   /* 0x1dd  phase start while PARKED    */
    int           field_1e5_;         /* 0x1e5  zeroed while parked         */
    float         liftDwell_;         /* 0x1e9  park dwell, 1500.0f         */
};

inline void Tile::assertLayout()
{
    KAROO_LAYOUT_AT(Tile, height_,          0x19c);
    KAROO_LAYOUT_AT(Tile, liftLiveHeight_,  0x1a6);
    KAROO_LAYOUT_AT(Tile, liftSlot_,        0x1d2);
    KAROO_LAYOUT_AT(Tile, liftBottom_,      0x1d3);
    KAROO_LAYOUT_AT(Tile, liftTop_,         0x1d4);
    KAROO_LAYOUT_AT(Tile, liftMovingSince_, 0x1d5);
    KAROO_LAYOUT_AT(Tile, liftParkedSince_, 0x1dd);
    KAROO_LAYOUT_AT(Tile, field_1e5_,       0x1e5);
    KAROO_LAYOUT_AT(Tile, liftDwell_,       0x1e9);
}
