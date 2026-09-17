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
 * four bytes per cell; the rest is runtime state.  The kind byte's values
 * are `enum TileKind` below.  The contents byte has its OWN value space --
 * the same number means different things in the two bytes -- and is not yet
 * an enum (COHESION_PLAN.md Band 7a).
 */
#pragma once

#include "layout.h"

/* The cell's kind byte (+0x19d, `objectMarker()`) -- .jjm file byte 1, and
 * the value every file used to compare as bare hex (COHESION_PLAN.md Band
 * 7a).
 *
 * Named from the code that ACTS on each value, never from the value:
 * SetupLevelObjects (levelsetup.cpp) is the authority, since it spawns one
 * object per kind, with MovableEntity::updateMovement, FoePath and
 * worldstate.cpp settling the rest.  Where nothing settles a value it keeps
 * a neutral TILE_KIND_<hex> name -- a wrong name is worse than a number
 * (Band 4b pass 2).
 *
 * `objectMarker()` returns `unsigned char`, and several call sites compare
 * it as `(signed char)`, exactly as the original does.  These constants are
 * therefore plain ints in the enum and the casts at the call sites are left
 * alone: this band renames, it does not change an expression.
 */
enum TileKind {
    /* A void cell.  FoePath treats it as blocked unless something bridges
     * it (the +0x1bc slide track is nonzero). */
    TILE_EMPTY       = 0x00,
    /* Censused as type1.  SetupLevelObjects rewrites TILE_KIND_03 to this
     * before any later test sees it; nothing else reads it. */
    TILE_KIND_01     = 0x01,
    /* The pad that freezes whoever stands on it until it is spent.  Derived
     * in worldstate.h, which has named it WS_TILE_GLUE since before this
     * enum; that macro is now defined from this. */
    TILE_GLUE        = 0x02,
    /* Only ever seen being rewritten to TILE_KIND_01 at load. */
    TILE_KIND_03     = 0x03,

    /* Ramps, one per facing: `kind - 4` is the direction, 1..4, and
     * worldstate.cpp's ws_is_ramp() is `kind > 4 && kind < 9`.  A step off a
     * ramp is exempt from the fall rule only along the ramp's own axis. */
    TILE_RAMP_1      = 0x05,
    TILE_RAMP_2      = 0x06,
    TILE_RAMP_3      = 0x07,
    TILE_RAMP_4      = 0x08,

    /* LiftObject::spawn takes the cell; the param is the lift's. */
    TILE_LIFT        = 0x09,
    /* The two slide spawns.  The kind IS the track axis: 0x0a walks U (the
     * row), 0x0b walks V (the column), and the V scan clears the markers
     * along its track while the U scan leaves them standing (slideobject.cpp
     * -- a preserved asymmetry, not a tidy-up). */
    TILE_SLIDE_U     = 0x0a,
    TILE_SLIDE_V     = 0x0b,
    /* Stamped by a slide onto every cell of its track.  An entity standing
     * on one attaches to the moving platform. */
    TILE_SLIDE_TRACK = 0x0c,
    /* BreakableTile::spawn -- the falling tile.  worldstate.h has called it
     * WS_TILE_FALLING since before this enum; that macro is now defined from
     * this.  NOTE: contents 0x0d is a different thing entirely (worldstate's
     * WS_TILE_TRANSFORM) -- the same number in the other byte, which is one
     * of the reasons for splitting the two enums. */
    TILE_BREAKABLE   = 0x0d,
    /* The jump pad: landing on it survives any drop, and it cancels a queued
     * move.  updateMovement names it; worldstate.h's WS_TILE_SOFT_LAND is
     * the same kind seen from the fall rule, and is now defined from this. */
    TILE_JUMP_PAD    = 0x0e,
    /* Paired by the builder through the param byte; the pair's cell lands in
     * teleportU/teleportV. */
    TILE_TELEPORTER  = 0x0f,
    /* Climbable.  The builder moves the param byte into climbDir(). */
    TILE_CLIMB       = 0x10,
    /* A numbered switch; the builder files its cell into SwitchCells and
     * leaves the index in +0x1f3. */
    TILE_SWITCH      = 0x11,
    /* BridgeObject::spawn, axis argument 1 and 2 respectively. */
    TILE_BRIDGE_U    = 0x12,
    TILE_BRIDGE_V    = 0x13,
    /* Carries whoever stands on it along conveyorDir. */
    TILE_CONVEYOR    = 0x15,
    /* Blocked unconditionally, by both FoePath and worldstate.  Named for
     * what the code does with it; what it IS on screen is not settled. */
    TILE_IMPASSABLE  = 0x16,
    /* A destructible block, and the game's own word for it is "obstacle":
     * Bomb's blast logs "GAME: obstacle is exploding at:%d,%d,%d" as it
     * marks one spent and promotes its hidden contents (+0x202) into
     * contents() so they can be picked up.  The builder is what hid them
     * there at load.  Blocked until spent, in both FoePath and worldstate.
     *
     * Named from bomb.cpp, not from the builder: the builder only hides the
     * contents, which is why an earlier draft of this enum called it
     * TILE_SHADOW after the level census's shadow1/shadow7 counters.  Those
     * count the hidden CONTENTS, not the tile -- the code that destroys the
     * thing is what names it. */
    TILE_DESTRUCTIBLE = 0x17,
};

/* The cell's contents byte (+0x19f, `contents()`) -- .jjm file byte 3.
 *
 * A DIFFERENT VALUE SPACE from TileKind above: the same number means
 * unrelated things in the two bytes, and 0x0d is the standing example --
 * kind 0x0d is TILE_BREAKABLE, contents 0x0d is CONTENTS_TRANSFORM.  The
 * prefixes are deliberately unalike so no site can mix them up
 * (COHESION_PLAN.md Band 7a).
 *
 * Named from the code that CONSUMES each value.  Player::updateTileEffects
 * (player.cpp) is the authority for everything a player can pick up -- it
 * is one `if` per value, each granting its own thing -- and
 * SetupLevelObjects consumes the four that are level-build markers rather
 * than pickups.  Where only a census reads a value it keeps a neutral name.
 *
 * `contents()` returns `unsigned char`; the call sites compare it as
 * `(signed char)`, exactly as the original does, which is why
 * CONTENTS_RANDOM is -1 and not 0xff.  Its byte in the file is 0xff; the
 * only site that reads it does so through that cast.
 */
enum TileContents {
    CONTENTS_NONE        = 0x00,
    /* The crystal, and the level's own goal: it is the only thing that
     * raises gemsCollected_.  A VoicePool, not a static buffer. */
    CONTENTS_CRYSTAL     = 0x01,
    /* Not pickups -- the builder spawns a foe of that type here and clears
     * the byte.  The param byte carries the foe's own parameter. */
    CONTENTS_FOE_TYPE2   = 0x02,
    CONTENTS_FOE_TYPE3   = 0x03,
    /* One paraglider charge (glides_).  Refused while falling. */
    CONTENTS_PARAGLIDER  = 0x05,
    /* Five more seconds on the map's time limit. */
    CONTENTS_TIME_BONUS  = 0x06,
    /* An extra life.  worldstate.h's WS_TILE_EXTRA_LIFE is this, and is now
     * defined from it. */
    CONTENTS_EXTRA_LIFE  = 0x07,
    /* Timed, and what it does is NOT settled: the tick only stamps its
     * start and raises effect8Active_.  worldstate.cpp guesses "freeze
     * timer", which nothing here confirms -- so it keeps a neutral name.
     * (Band 4b pass 2 stopped at exactly this point for the same reason.) */
    CONTENTS_EFFECT_8    = 0x08,
    /* Grants 3 of whatever MovableEntity's +0x0e8 counts, which no reader
     * we own settles.  Neutral until one does. */
    CONTENTS_GRANT_09    = 0x09,
    /* Timed speed changes: stepDuration_ to 100.0 and 400.0 against the
     * player's default 200.0, so 0x0a is faster and 0x0c slower. */
    CONTENTS_SPEED_UP    = 0x0a,
    /* Timed; reverses PlayerMoveForward. */
    CONTENTS_REVERSED    = 0x0b,
    CONTENTS_SPEED_DOWN  = 0x0c,
    /* Timed; transforms the player (kind_ = 3) and the cell's occupant.
     * worldstate.h's WS_TILE_TRANSFORM is this, and ws_is_pickup() excludes
     * it -- it is the one contents value that is not an item. */
    CONTENTS_TRANSFORM   = 0x0d,
    /* A build marker: the builder files the cell into Game's free-bomb
     * list and stamps the clock. */
    CONTENTS_FREE_BOMB   = 0x4d,
    /* A build marker: the cell becomes a timed item spawner. */
    CONTENTS_TIMED_SPAWN = 0x64,

    /* A tile that rolls its own contents when the player arrives:
     *   rand() * 8 / 0x7FFF + 5, an 8-bit add
     * which is a uniform pick over 5..12 -- exactly CONTENTS_PARAGLIDER
     * through CONTENTS_SPEED_DOWN.  It cannot roll CONTENTS_TRANSFORM, and
     * it cannot roll itself. */
    CONTENTS_RANDOM      = -1,
};

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
    unsigned char occupant() const             { return occupant_; }
    void setOccupant(unsigned char b)          { occupant_ = b; }
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
    void setClimbDir(unsigned char b)          { climbDir_ = b; }
    void setField202(unsigned char b)          { field_202 = b; }

    /* ── a bridge deck cell (bridgeobject.cpp) ──────────────────────── */
    /* The spawn stamps its own cell; the tick stamps each deck cell as it
     * extends.  Retract clears +0x1f6 and +0x217 but NOT these two. */
    void setBridgeSlot(unsigned char n)        { bridgeSlot_ = n; }
    void setBridgeAxis(unsigned char a)        { bridgeAxis_ = a; }
    /* Meaning unknown: 1 on an extended deck cell, 0 otherwise. */
    void setField1f6(int b)                    { field_1f6 = b; }
    void setBusy(int b)                    { busy_ = b; }
    /* A breakable sets it when it falls and clears it when it respawns
     * (breakabletile.cpp), and will not arm while it is set. */
    int  busy() const                      { return busy_; }

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
    void setTeleportU(unsigned char u)          { teleportU_ = u; }
    void setTeleportV(unsigned char v)          { teleportV_ = v; }
    void setField1f1(unsigned char b)          { field_1f1 = b; }
    void setField1f3(unsigned char b)          { field_1f3 = b; }
    /* +0x213: a random phase, rand() * 2pi / 32768, given to every cell
     * holding an item. */
    void setItemPhase(float p)                 { itemPhase_ = p; }

    /* ── read by the foe pathfinder (foepath.cpp) ────────────────────── */
    /* +0x1f1: set from the param byte on a TILE_JUMP_PAD cell.  An earlier
     * comment here called kind 0x0e an "elevator"; nothing supports that --
     * updateMovement names it the jump pad and worldstate.cpp reads it as
     * the fall exemption.  What this byte is FOR is still unsettled, so it
     * keeps its offset name. */
    unsigned char field1f1() const             { return field_1f1; }
    /* +0x1f2: on a TILE_CLIMB cell, its direction byte.  (This said "on a
     * bridge (kind 0x10) cell" -- wrong: the bridges are TILE_BRIDGE_U and
     * TILE_BRIDGE_V, 0x12/0x13.  The builder moves the param byte here and
     * updateMovement's climb block is the only reader.) */
    unsigned char climbDir() const             { return climbDir_; }

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
    /* +0x1ee / +0x1ef: on a TILE_TELEPORTER cell, read as the u and
     * v of the cell it sends the entity to. */
    unsigned char teleportU() const             { return teleportU_; }
    unsigned char teleportV() const             { return teleportV_; }
    /* +0x1f3: on a TILE_SWITCH cell, copied into the entity's +0xd7. */
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
    unsigned char occupant_;          /* 0x1a5  the entity kind standing here*/
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
    unsigned char teleportU_;         /* 0x1ee  } teleporter destination     */
    unsigned char teleportV_;         /* 0x1ef  }                            */
    unsigned char gap_1f0[0x1f1 - 0x1f0];
    unsigned char field_1f1;          /* 0x1f1  set from param on a jump pad */
    unsigned char climbDir_;          /* 0x1f2  climb tile: which way up     */
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
    int           busy_;              /* 0x217  a pad/teleporter is in use    */
};

KAROO_LAYOUT_CHECKS(Tile)
{
    KAROO_LAYOUT_AT(height_,           0x19c);
    KAROO_LAYOUT_AT(objectMarker_,     0x19d);
    KAROO_LAYOUT_AT(param_,            0x19e);
    KAROO_LAYOUT_AT(contents_,         0x19f);
    KAROO_LAYOUT_AT(blastHeight_,      0x1a0);
    KAROO_LAYOUT_AT(field_1a1,         0x1a1);
    KAROO_LAYOUT_AT(occupant_,         0x1a5);
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
    KAROO_LAYOUT_AT(teleportU_,        0x1ee);
    KAROO_LAYOUT_AT(teleportV_,        0x1ef);
    KAROO_LAYOUT_AT(climbDir_,         0x1f2);
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
    KAROO_LAYOUT_AT(busy_,             0x217);
    KAROO_LAYOUT_SIZE(0x7f);
}
