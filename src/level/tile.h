/* Tile -- one cell of the level grid (COHESION_PLAN.md Band 4).
 *
 * SKETCH.  Only the fields a converted class reads or writes are here; the
 * rest are still reached by raw offset in unconverted files, and join this
 * class as those files are converted.
 *
 * ORIGIN.  A Tile pointer is `tileBase + (v + u*100) * 0x7f` -- the address
 * every existing file and FIELD_CENSUS_REPORT.txt calls "the tile", so the
 * offsets below match them exactly.  It is NOT where the cell's record
 * starts: the fields in use span +0x19a..+0x21b, wider than the 0x7f
 * stride, so the true record boundary is unsettled RE.  Until it is, the
 * class keeps the code's convention rather than guessing one.
 *
 * No data members, for the same reason as Game: the layout is the game's.
 */
#pragma once

#include <string.h>

class Tile {
public:
    /* Both axes are signed: the lift tick reads its cell as s8.  The
     * arithmetic is the original's, stated once. */
    static Tile *at(unsigned char *base, int u, int v)
    {
        return (Tile *)(base + (v + u * 100) * 0x7f);
    }

    /* +0x19c  the cell's height byte. */
    void setHeight(unsigned char h)          { at<unsigned char>(0x19c) = h; }

    /* ── published by a lift standing on this cell (liftobject.cpp) ── */
    /* +0x1a6  the lift's live height, as raw float bits. */
    void setLiftLiveHeightBits(unsigned int b) { at<unsigned int>(0x1a6) = b; }
    /* +0x1d2  which lift slot stands here. */
    void setLiftSlot(unsigned char n)        { at<unsigned char>(0x1d2) = n; }
    /* +0x1d3 / +0x1d4  where the lift parks at the bottom / top. */
    signed char liftBottom() const           { return at<signed char>(0x1d3); }
    signed char liftTop() const              { return at<signed char>(0x1d4); }
    void setLiftBottom(unsigned char h)      { at<unsigned char>(0x1d3) = h; }
    void setLiftTop(unsigned char h)         { at<unsigned char>(0x1d4) = h; }
    /* +0x1d5  phase start while MOVING, 8 bytes; zero while parked. */
    void setLiftMovingSince(const void *d)   { memcpy(raw() + 0x1d5, d, 8); }
    void clearLiftMovingSince()
    {
        at<unsigned int>(0x1d5) = 0;          /* two dword stores, as the */
        at<unsigned int>(0x1d9) = 0;          /* original emits them      */
    }
    /* +0x1dd  phase start while PARKED, 8 bytes. */
    void setLiftParkedSince(const void *d)   { memcpy(raw() + 0x1dd, d, 8); }
    /* +0x1e5  zeroed while parked; meaning unknown. */
    void setField_1e5(int x)                 { at<int>(0x1e5) = x; }
    /* +0x1e9  the park dwell, raw float bits (1500.0f). */
    void setLiftDwellBits(unsigned int b)    { at<unsigned int>(0x1e9) = b; }

private:
    Tile() = delete;   /* game-owned; only ever reached by pointer */

    unsigned char       *raw()       { return (unsigned char *)this; }
    const unsigned char *raw() const { return (const unsigned char *)this; }

    template <class T> T &at(unsigned int off)
    {
        typedef T __attribute__((aligned(1))) ua;
        return *(ua *)(raw() + off);
    }
    template <class T> const T &at(unsigned int off) const
    {
        typedef T __attribute__((aligned(1))) ua;
        return *(const ua *)(raw() + off);
    }
};
