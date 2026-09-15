/* Tile -- one cell of the level grid (COHESION_PLAN.md Band 4b).
 *
 * SKETCH.  Only the fields a converted class reads or writes are declared;
 * unconverted files still reach the rest by raw offset, and join this class
 * as they are converted.
 *
 * ORIGIN.  The game addresses a cell as `tileBase + (v + u*100) * 0x7f` --
 * "the tile pointer" in every existing file and FIELD_CENSUS_REPORT.txt, and
 * the offsets asserted below are relative to it.  The tile base is the
 * LevelMap (levelmap.h), whose 0x19c-byte header precedes the grid, so the
 * cell's record starts at tile pointer +0x19c and is exactly the 0x7f stride:
 * +0x19c..+0x21b, which is every field below.  Settled 2026-09-15 by the
 * LevelMap's extent (header + two grids tile Game's bytes up to tallyDone
 * exactly).  The fields that used to sit below +0x19c (+0x004, +0x19a,
 * +0x19b) were LevelMap header fields read through cell (0, 0); they are
 * LevelMap's now.
 *
 * The first four bytes (height, kind, param, contents) are the .jjm file's
 * four bytes per cell; the rest is runtime state.
 */
#pragma once

#include "layout.h"

class __attribute__((packed)) Tile {
public:
    static const int ORIGIN = 0x19c;

    /* Both axes are signed: the lift tick reads its cell as s8.  The
     * arithmetic is the original's, stated once. */
    static Tile *at(unsigned char *base, int u, int v)
    {
        return (Tile *)(base + (v + u * 100) * 0x7f + ORIGIN);
    }

    /* +0x19c  the cell's height byte. */
    unsigned char height() const               { return height_; }
    void setHeight(unsigned char h)            { height_ = h; }

    /* +0x19d  the object marker/kind byte.  SetupLevelObjects reads it back
     * (a slide scan stops at a nonzero one); a slide stamps 0x0c here. */
    unsigned char objectMarker() const         { return objectMarker_; }
    void setObjectMarker(unsigned char k)      { objectMarker_ = k; }
    /* +0x1a5  cleared with the marker when a slide vacates a cell.  A
     * breakable arms when it is nonzero (someone is standing on the cell). */
    unsigned char field1a5() const             { return field_1a5; }
    void setField1a5(unsigned char b)          { field_1a5 = b; }
    /* +0x19e  the file's parameter byte: a switch's number, a bridge's
     * switch, a lift's, a teleporter's pair id, a foe's drop.  The level
     * builder consumes it (clears it) as it spawns what it describes. */
    unsigned char param() const                { return param_; }
    void setParam(unsigned char p)             { param_ = p; }

    /* ── published by a lift standing on this cell (liftobject.cpp) ── */
    /* The level builder seeds it on EVERY cell with the cell's own height
     * as a float; a lift then overwrites it on its cells. */
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

    /* ── a bridge deck cell (bridgeobject.cpp) ──────────────────────── */
    /* The spawn stamps its own cell; the tick stamps each deck cell as it
     * extends.  Retract clears +0x1f6 and +0x217 but NOT these two. */
    void setBridgeSlot(unsigned char n)        { bridgeSlot_ = n; }
    void setBridgeAxis(unsigned char a)        { bridgeAxis_ = a; }
    /* Meaning unknown: 1 on an extended deck cell, 0 otherwise. */
    void setField1f6(int b)                    { field_1f6 = b; }
    void setField217(int b)                    { field_217 = b; }
    /* A breakable sets it when it falls and clears it when it respawns
     * (breakabletile.cpp), and will not arm while it is set. */
    int  field217() const                      { return field_217; }

    /* ── a bomb's blast (bomb.cpp) ──────────────────────────────────── */
    /* +0x1a0: the blasting bomb's height while its 3x3 is live, else 0. */
    void setBlastHeight(unsigned char h)       { blastHeight_ = h; }
    /* +0x19f: a spent destructible block's promoted hidden contents. */
    void setContents(unsigned char c)          { contents_ = c; }
    /* +0x202: the destructible block's hidden contents. */
    unsigned char field202() const             { return field_202; }
    /* Meanings unknown: raised by a blast on a destructible block; +0x203
     * drops again when the blast clears. */
    void setField203(int b)                    { field_203 = b; }
    void setField20f(int b)                    { field_20f = b; }
    void setBlastTime(double t)                { blastTime_ = t; }

    /* ── read by a foe (foe.cpp) ─────────────────────────────────────── */
    unsigned char contents() const             { return contents_; }
    int    slideTrack() const                  { return slideTrack_; }
    double slideParkedSince() const            { return slideParkedSince_; }
    double slideDwell() const                  { return slideDwell_; }
    double liftParkedSince() const             { return liftParkedSince_; }
    double liftDwell() const                   { return liftDwell_; }
    /* Cleared (one dword) by a foe's destructor on the cell it stood on;
     * meaning unknown. */
    void setField1a1(int b)                    { field_1a1 = b; }

    /* ── set by the level builder (levelsetup.cpp) ───────────────────── */
    /* +0x1ed: a teleporter's pair id, moved here from param. */
    void setTeleportId(unsigned char id)       { teleportId_ = id; }
    void setField1ee(unsigned char u)          { field_1ee = u; }
    void setField1ef(unsigned char v)          { field_1ef = v; }
    void setField1f1(unsigned char b)          { field_1f1 = b; }
    void setField1f3(unsigned char b)          { field_1f3 = b; }
    /* +0x213: a random phase, rand() * 2pi / 32768, given to every cell
     * holding an item. */
    void setItemPhase(float p)                 { itemPhase_ = p; }

    /* ── read by the foe pathfinder (foepath.cpp) ────────────────────── */
    /* +0x1f1: an elevator (kind 0x0e) cell's level byte. */
    unsigned char field1f1() const             { return field_1f1; }
    /* +0x1f2: on a bridge (kind 0x10) cell, its direction byte. */
    unsigned char field1f2() const             { return field_1f2; }

    /* ── read by the movement tick (movableentity.cpp) ──────────────────── */
    /* The tick reads +0x19c, +0x19d, +0x1a5 and +0x1f1 both MOVSX and
     * MOVZX; the accessors are unsigned and the signed reads cast at the
     * read site. */
    unsigned char blastHeight() const          { return blastHeight_; }
    float  liftLiveHeight() const              { return liftLiveHeight_; }
    unsigned char slideSlot() const            { return slideSlot_; }
    unsigned char slideOriginU() const         { return slideOriginU_; }
    unsigned char slideOriginV() const         { return slideOriginV_; }
    unsigned char slideCellU() const           { return slideCellU_; }
    unsigned char slideCellV() const           { return slideCellV_; }
    float  slidePosU() const                   { return slidePosU_; }
    float  slidePosY() const                   { return slidePosY_; }
    float  slidePosV() const                   { return slidePosV_; }
    /* +0x1ee / +0x1ef: on a teleporter (kind 0x0f) cell, read as the u and
     * v of the cell it sends the entity to. */
    unsigned char field1ee() const             { return field_1ee; }
    unsigned char field1ef() const             { return field_1ef; }
    /* +0x1f3: on a kind-0x11 cell, copied into the entity's +0xd7. */
    unsigned char field1f3() const             { return field_1f3; }

private:
    Tile() = delete;   /* game-owned; only ever reached through at() */
    KAROO_LAYOUT_REGISTER(Tile);

    unsigned char height_;            /* 0x19c  file byte 0                 */
    unsigned char objectMarker_;      /* 0x19d  file byte 1: kind / scan stop */
    unsigned char param_;             /* 0x19e  file byte 2                 */
    unsigned char contents_;          /* 0x19f  file byte 3: pickup here    */
    unsigned char blastHeight_;       /* 0x1a0  live blast, 0 = none        */
    int           field_1a1;          /* 0x1a1                              */
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
    unsigned char teleportId_;        /* 0x1ed  a teleporter's pair id      */
    unsigned char field_1ee;          /* 0x1ee                              */
    unsigned char field_1ef;          /* 0x1ef                              */
    unsigned char gap_1f0[0x1f1 - 0x1f0];
    unsigned char field_1f1;          /* 0x1f1  an elevator's level byte    */
    unsigned char field_1f2;          /* 0x1f2                              */
    unsigned char field_1f3;          /* 0x1f3                              */
    unsigned char bridgeSlot_;        /* 0x1f4  the bridge's switch slot    */
    unsigned char bridgeAxis_;        /* 0x1f5  1 = along U, 2 = along V    */
    int           field_1f6;          /* 0x1f6                              */
    unsigned char gap_1fa[0x202 - 0x1fa];
    unsigned char field_202;          /* 0x202                              */
    int           field_203;          /* 0x203                              */
    double        blastTime_;         /* 0x207  when a blast spent this cell */
    int           field_20f;          /* 0x20f                              */
    float         itemPhase_;         /* 0x213  an item's random phase      */
    int           field_217;          /* 0x217                              */
};

KAROO_LAYOUT_CHECKS(Tile)
{
    KAROO_LAYOUT_AT(height_,           0x19c);
    KAROO_LAYOUT_AT(objectMarker_,     0x19d);
    KAROO_LAYOUT_AT(param_,            0x19e);
    KAROO_LAYOUT_AT(contents_,         0x19f);
    KAROO_LAYOUT_AT(blastHeight_,      0x1a0);
    KAROO_LAYOUT_AT(field_1a1,         0x1a1);
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
    KAROO_LAYOUT_AT(field_1f1,         0x1f1);
    KAROO_LAYOUT_AT(field_1ee,         0x1ee);
    KAROO_LAYOUT_AT(field_1ef,         0x1ef);
    KAROO_LAYOUT_AT(field_1f2,         0x1f2);
    KAROO_LAYOUT_AT(field_1f3,         0x1f3);
    KAROO_LAYOUT_AT(bridgeSlot_,       0x1f4);
    KAROO_LAYOUT_AT(bridgeAxis_,       0x1f5);
    KAROO_LAYOUT_AT(field_1f6,         0x1f6);
    KAROO_LAYOUT_AT(field_202,         0x202);
    KAROO_LAYOUT_AT(field_203,         0x203);
    KAROO_LAYOUT_AT(blastTime_,        0x207);
    KAROO_LAYOUT_AT(field_20f,         0x20f);
    KAROO_LAYOUT_AT(itemPhase_,        0x213);
    KAROO_LAYOUT_AT(teleportId_,       0x1ed);
    KAROO_LAYOUT_AT(field_217,         0x217);
    KAROO_LAYOUT_SIZE(0x7f);
}
