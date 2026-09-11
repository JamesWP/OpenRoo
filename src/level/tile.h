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

    /* +0x19d  the object marker/kind byte.  SetupLevelObjects reads it back
     * (a slide scan stops at a nonzero one); a slide stamps 0x0c here. */
    unsigned char objectMarker() const         { return objectMarker_; }
    void setObjectMarker(unsigned char k)      { objectMarker_ = k; }
    /* +0x1a5  cleared with the marker when a slide vacates a cell. */
    void setField1a5(unsigned char b)          { field_1a5 = b; }

    /* ── published by a lift standing on this cell (liftobject.cpp) ── */
    void setLiftLiveHeight(float h)            { liftLiveHeight_ = h; }
    void setLiftSlot(unsigned char n)          { liftSlot_ = n; }
    signed char liftBottom() const             { return liftBottom_; }
    signed char liftTop() const                { return liftTop_; }
    void setLiftBottom(unsigned char h)        { liftBottom_ = (signed char)h; }
    void setLiftTop(unsigned char h)           { liftTop_ = (signed char)h; }
    void setLiftMovingSince(double t)          { liftMovingSince_ = t; }
    /* +0.0 is all-zero bits, so this is the original's two zero dwords. */
    void clearLiftMovingSince()                { liftMovingSince_ = 0.0; }
    void setLiftParkedSince(double t)          { liftParkedSince_ = t; }
    void setLiftDwell(double ms)               { liftDwell_ = ms; }

    /* ── a slide's track and live position (slideobject.cpp) ────────── */
    /* Stamped on every cell of a track by the spawn's scan. */
    void setSlideSlot(unsigned char n)         { slideSlot_ = n; }
    void setSlideHeight(unsigned char h)       { slideHeight_ = h; }
    void setSlideTrack(int on)                 { slideTrack_ = on; }
    void setSlideOrigin(unsigned char u, unsigned char v)
    {
        slideOriginU_ = u;
        slideOriginV_ = v;
    }
    /* Published by a parked slide on the cell it is parked on. */
    void setSlideParkedSince(double t)         { slideParkedSince_ = t; }
    void setSlideDwell(double ms)              { slideDwell_ = ms; }
    /* Published every tick on the slide's ORIGIN cell -- how the renderer
     * finds a slide that has moved off its home tile. */
    void setSlideCell(unsigned char u, unsigned char v)
    {
        slideCellU_ = u;
        slideCellV_ = v;
    }
    void setSlidePos(float u, float y, float v)
    {
        slidePosU_ = u;
        slidePosY_ = y;
        slidePosV_ = v;
    }
    /* Cleared by the spawn on the spawn cell only; meaning unknown. */
    void setField1f2(unsigned char b)          { field_1f2 = b; }
    void setField202(unsigned char b)          { field_202 = b; }

private:
    Tile() = delete;   /* game-owned; only ever reached through at() */
    KAROO_LAYOUT_REGISTER(Tile);

    unsigned char gap_000[0x19c - 0x000];
    unsigned char height_;            /* 0x19c                              */
    unsigned char objectMarker_;      /* 0x19d  object kind / scan stop     */
    unsigned char gap_19e[0x1a5 - 0x19e];
    unsigned char field_1a5;          /* 0x1a5                              */
    float         liftLiveHeight_;    /* 0x1a6  the lift's live height      */
    unsigned char slideSlot_;         /* 0x1aa  which slide's track this is */
    unsigned char slideHeight_;       /* 0x1ab                              */
    double        slideParkedSince_;  /* 0x1ac  phase start while PARKED    */
    /* One double, though the original writes it as two dwords (0 at
     * +0x1b4, 0x40977000 at +0x1b8): 0x4097700000000000 = 1500.0. */
    double        slideDwell_;        /* 0x1b4  park dwell in ms, 1500.0    */
    int           slideTrack_;        /* 0x1bc  1 = on a slide's track      */
    unsigned char slideOriginU_;      /* 0x1c0  } the track's spawn cell    */
    unsigned char slideOriginV_;      /* 0x1c1  }                           */
    unsigned char gap_1c2[0x1c3 - 0x1c2];
    unsigned char slideCellU_;        /* 0x1c3  } live cell, on the ORIGIN  */
    unsigned char slideCellV_;        /* 0x1c4  } tile only                 */
    unsigned char gap_1c5[0x1c6 - 0x1c5];
    float         slidePosU_;         /* 0x1c6  } live position, on the     */
    float         slidePosY_;         /* 0x1ca  } ORIGIN tile only          */
    float         slidePosV_;         /* 0x1ce  }                           */
    unsigned char liftSlot_;          /* 0x1d2  which lift slot stands here */
    signed char   liftBottom_;        /* 0x1d3  lift parks here at bottom   */
    signed char   liftTop_;           /* 0x1d4  ... and here at top         */
    double        liftMovingSince_;   /* 0x1d5  phase start while MOVING    */
    double        liftParkedSince_;   /* 0x1dd  phase start while PARKED    */
    /* One double, though the original writes it as two dwords (0 at
     * +0x1e5, 0x40977000 at +0x1e9): 0x4097700000000000 = 1500.0. */
    double        liftDwell_;         /* 0x1e5  park dwell in ms, 1500.0    */
    unsigned char gap_1ed[0x1f2 - 0x1ed];
    unsigned char field_1f2;          /* 0x1f2                              */
    unsigned char gap_1f3[0x202 - 0x1f3];
    unsigned char field_202;          /* 0x202                              */
};

KAROO_LAYOUT_CHECKS(Tile)
{
    KAROO_LAYOUT_AT(height_,           0x19c);
    KAROO_LAYOUT_AT(objectMarker_,     0x19d);
    KAROO_LAYOUT_AT(field_1a5,         0x1a5);
    KAROO_LAYOUT_AT(liftLiveHeight_,   0x1a6);
    KAROO_LAYOUT_AT(slideSlot_,        0x1aa);
    KAROO_LAYOUT_AT(slideHeight_,      0x1ab);
    KAROO_LAYOUT_AT(slideParkedSince_, 0x1ac);
    KAROO_LAYOUT_AT(slideDwell_,       0x1b4);
    KAROO_LAYOUT_AT(slideTrack_,       0x1bc);
    KAROO_LAYOUT_AT(slideOriginU_,     0x1c0);
    KAROO_LAYOUT_AT(slideOriginV_,     0x1c1);
    KAROO_LAYOUT_AT(slideCellU_,       0x1c3);
    KAROO_LAYOUT_AT(slideCellV_,       0x1c4);
    KAROO_LAYOUT_AT(slidePosU_,        0x1c6);
    KAROO_LAYOUT_AT(slidePosY_,        0x1ca);
    KAROO_LAYOUT_AT(slidePosV_,        0x1ce);
    KAROO_LAYOUT_AT(liftSlot_,         0x1d2);
    KAROO_LAYOUT_AT(liftBottom_,       0x1d3);
    KAROO_LAYOUT_AT(liftTop_,          0x1d4);
    KAROO_LAYOUT_AT(liftMovingSince_,  0x1d5);
    KAROO_LAYOUT_AT(liftParkedSince_,  0x1dd);
    KAROO_LAYOUT_AT(liftDwell_,        0x1e5);
    KAROO_LAYOUT_AT(field_1f2,         0x1f2);
    KAROO_LAYOUT_AT(field_202,         0x202);
}
