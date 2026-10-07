/* Tile: one cell of the level grid (levelmap.h).  The
 * first four bytes (height, kind, param, contents) are the .jjm file's four
 * bytes per cell; the rest is state the level's objects keep on the cell.  Not
 * every field is declared yet: only those the code reads by name.
 *
 * The kind byte and the contents byte have separate value spaces: the same
 * number means unrelated things in each (0x0d is a falling tile kind but the
 * transform contents), hence the two enums with unlike prefixes. */

#pragma once

 

/* The kind byte (objectMarker()), file byte 1.  Named from the code that acts
 * on each value (the level builder spawns one object per kind); values nothing
 * settles keep a neutral TILE_KIND_<hex> name.  Call sites compare it as
 * signed char, as the game does. */
enum TileKind {
    // A void cell.  The foe pathfinder treats it as blocked unless a platform
    // track bridges it.
    TILE_EMPTY       = 0x00,
    // Counted as LevelCensus::kind01.  The builder rewrites TILE_START to
    // this; nothing else reads it.
    TILE_KIND_01     = 0x01,
    // The pad that holds whoever stands on it until it is spent.
    TILE_STICKY      = 0x02,
    // The player's start.  The builder rewrites it to TILE_KIND_01, so nothing
    // after the level build reads this value.
    TILE_START       = 0x03,
    // The level exit.  The placement lists keep its single position and count
    // it as solid.
    TILE_EXIT        = 0x04,

    // Stairs, one per facing: kind - 4 is the direction, 1..4.  A step off a
    // stair tile is exempt from the fall rule only along the stairs' own axis.
    TILE_STAIRS_1      = 0x05,
    TILE_STAIRS_2      = 0x06,
    TILE_STAIRS_3      = 0x07,
    TILE_STAIRS_4      = 0x08,

    // A lift; the param is the lift's.
    TILE_LIFT        = 0x09,
    // The two platform spawns.  The kind is the track axis: 0x0a runs along u,
    // 0x0b along v.  PRESERVED: the v scan clears the markers along its track
    // and the u scan leaves them standing.
    TILE_PLATFORM_U  = 0x0a,
    TILE_PLATFORM_V  = 0x0b,
    // Stamped by a platform on every cell of its track; an entity standing on
    // one rides the moving platform.
    TILE_PLATFORM_TRACK = 0x0c,
    // The falling tile.
    TILE_FALLING     = 0x0d,
    // The jump pad: landing on it survives any drop, and it cancels a queued
    // move.
    TILE_JUMP_PAD    = 0x0e,
    // Paired by the builder through the param byte; the pair's cell lands in
    // teleportU/teleportV.
    TILE_TELEPORTER  = 0x0f,
    // A slide (the game's own name for it): standing on one moves you on in
    // its direction, quickly and facing that way, and paths leave it only
    // that way.  The builder moves the param byte into slideDir().
    TILE_SLIDE       = 0x10,
    // A numbered switch; the builder files the cell into SwitchCells and
    // leaves the number in field1f3().
    TILE_SWITCH      = 0x11,
    // Bridges along u and v.
    TILE_BRIDGE_U    = 0x12,
    TILE_BRIDGE_V    = 0x13,
    // Ice: whoever steps on keeps moving the way they entered until off it.
    TILE_ICE         = 0x15,
    // Blocked unconditionally.  On screen it is whatever the level's .leo
    // object at that cell is (a crate, a beehive, a bush); nothing links the
    // two files, so under KAROO_LEO_FX=nomodels the cells stay blocked and
    // bare.
    TILE_IMPASSABLE  = 0x16,
    // A bombable block (the game's "obstacle"): a bomb's blast spends it
    // and promotes its hidden contents (field202()) into contents() to be
    // picked up.  Blocked until spent.
    TILE_BOMBABLE    = 0x17,
};

/* The contents byte, file byte 3.  Named from the code that consumes each
 * value: the player's tile effects (player.cpp) for everything that can be
 * picked up, and the builder for the build markers.  Values only a census
 * reads keep a neutral name.  Call sites compare it as signed char, which is
 * why CONTENTS_RANDOM is -1 (0xff in the file). */
enum TileContents {
    CONTENTS_NONE        = 0x00,
    // The crystal, the level's goal: the only thing that raises the crystal
    // count.
    CONTENTS_CRYSTAL     = 0x01,
    // Build markers, not pickups: the builder spawns a foe of that type here
    // and clears the byte; the param byte is the foe's own parameter.
    CONTENTS_FOE_TYPE2   = 0x02,
    CONTENTS_FOE_TYPE3   = 0x03,
    // One paraglider charge.  Refused while falling.
    CONTENTS_PARAGLIDER  = 0x05,
    // Five more seconds on the time limit.
    CONTENTS_TIME_BONUS  = 0x06,
    // An extra life.
    CONTENTS_EXTRA_LIFE  = 0x07,
    // Freezes every foe for five seconds (the player's freeze effect).
    CONTENTS_FREEZE      = 0x08,
    // Grants 3 bombs.
    CONTENTS_GRANT_09    = 0x09,
    // Timed speed changes: the step time goes to 100 (0x0a, faster) or 400
    // (0x0c, slower) against the player's default 200.
    CONTENTS_SPEED_UP    = 0x0a,
    // Timed: reverses forward movement.
    CONTENTS_REVERSED    = 0x0b,
    CONTENTS_SPEED_DOWN  = 0x0c,
    // Timed: transforms the player and the cell's occupant.  The one contents
    // value that is not an item.
    CONTENTS_TRANSFORM   = 0x0d,
    // A build marker: the builder files the cell into the free-bomb list and
    // stamps the clock.
    CONTENTS_FREE_BOMB   = 0x4d,
    // A build marker: the cell becomes a timed spawner.
    CONTENTS_TIMED_SPAWN = 0x64,

    // Rolls its own contents when the player arrives: rand() * 8 / 0x7fff + 5,
    // an 8-bit add, a uniform pick over 5..12 (paraglider to slower).  It
    // cannot roll the transform or itself.
    CONTENTS_RANDOM      = -1,
};

class Tile {
public:
    // The cell beyond the grid's edge: a solid wall at a height no level uses.
    static Tile outsideGrid()
    {
        Tile t{};
        t.height_ = 0xff;
        t.objectMarker_ = TILE_IMPASSABLE;
        return t;
    }

    unsigned char height() const               { return height_; }
    void setHeight(unsigned char h)            { height_ = h; }

    // The kind byte; a platform scan stops at a non-zero one, and a platform stamps
    // 0x0c here.
    unsigned char objectMarker() const         { return objectMarker_; }
    void setObjectMarker(unsigned char k)      { objectMarker_ = k; }
    // Cleared with the marker when a platform leaves a cell.  A falling tile arms
    // when it is non-zero (someone is standing on the cell).
    unsigned char occupant() const             { return occupant_; }
    void setOccupant(unsigned char b)          { occupant_ = b; }
    // The file's parameter byte: a switch's number, a bridge's switch, a
    // lift's, a teleporter's pair id, a foe's drop.  The builder clears it as
    // it spawns what it describes.
    unsigned char param() const                { return param_; }
    void setParam(unsigned char p)             { param_ = p; }

    // Published by a lift standing on this cell (liftobject.cpp).  The builder
    // seeds it on every cell with the cell's height as a float; a lift
    // overwrites it on its cells.
    void setLiftLiveHeight(float h)            { liftLiveHeight_ = h; }
    void setLiftSlot(unsigned char n)          { liftSlot_ = n; }
    signed char liftBottom() const             { return liftBottom_; }
    signed char liftTop() const                { return liftTop_; }
    void setLiftBottom(unsigned char h)        { liftBottom_ = (signed char)h; }
    void setLiftTop(unsigned char h)           { liftTop_ = (signed char)h; }
    void setLiftMovingSince(double t)          { liftMovingSince_ = t; }
    // +0.0 is all-zero bits.
    void clearLiftMovingSince()                { liftMovingSince_ = 0.0; }
    void setLiftParkedSince(double t)          { liftParkedSince_ = t; }
    void setLiftDwell(double ms)               { liftDwell_ = ms; }

    // A platform's track and live position (platformobject.cpp).  The track number
    // is stamped on every cell of a track by the spawn.
    void setPlatformSlot(unsigned char n)         { platformSlot_ = n; }
    void setPlatformHeight(unsigned char h)       { platformHeight_ = h; }
    void setPlatformTrack(int on)                 { platformTrack_ = on; }
    void setPlatformOrigin(unsigned char u, unsigned char v)
    {
        platformOriginU_ = u;
        platformOriginV_ = v;
    }
    // Published by a parked platform on the cell it is parked on.
    void setPlatformParkedSince(double t)         { platformParkedSince_ = t; }
    void setPlatformDwell(double ms)              { platformDwell_ = ms; }
    // Published every tick on the platform's origin cell: how the renderer finds
    // a platform that has moved off its home tile.
    void setPlatformCell(unsigned char u, unsigned char v)
    {
        platformCellU_ = u;
        platformCellV_ = v;
    }
    void setPlatformPos(float u, float y, float v)
    {
        platformPosU_ = u;
        platformPosY_ = y;
        platformPosV_ = v;
    }
    // Cleared by the spawn on the spawn cell only; meaning unknown.
    void setSlideDir(unsigned char b)          { slideDir_ = b; }
    void setField202(unsigned char b)          { field_202 = b; }

    // A bridge deck cell (bridgeobject.cpp).  The spawn stamps its own cell,
    // the tick each deck cell as it extends.  Retracting clears field_1f6 and
    // busy_ but not these two.
    void setBridgeSlot(unsigned char n)        { bridgeSlot_ = n; }
    void setBridgeAxis(unsigned char a)        { bridgeAxis_ = a; }
    // Meaning unknown: 1 on an extended deck cell, else 0.
    void setField1f6(int b)                    { field_1f6 = b; }
    void setBusy(int b)                    { busy_ = b; }
    // A falling tile sets it when it falls and clears it when it comes back, and
    // will not arm while it is set.
    int  busy() const                      { return busy_; }

    // A bomb's blast (bomb.cpp).  blastHeight is the bomb's height while its
    // 3x3 is live, else 0.
    void setBlastHeight(unsigned char h)       { blastHeight_ = h; }
    // A spent bombable block's promoted hidden contents.
    void setContents(unsigned char c)          { contents_ = c; }
    // The bombable block's hidden contents.
    unsigned char field202() const             { return field_202; }
    // Meaning unknown: raised by a blast on a bombable block; field_203
    // drops again when the blast clears.
    void setField203(int b)                    { field_203 = b; }
    void setField20f(int b)                    { field_20f = b; }
    // Read by the frame renderer: field_203 while a bombable block is
    // blasted draws the effect model; field_20f starts the debris burst once.
    int  field203() const                      { return field_203; }
    int  field20f() const                      { return field_20f; }
    void setBlastTime(double t)                { blastTime_ = t; }

    // Read by a foe (foe.cpp).
    unsigned char contents() const             { return contents_; }
    int    platformTrack() const                  { return platformTrack_; }
    double platformParkedSince() const            { return platformParkedSince_; }
    double platformDwell() const                  { return platformDwell_; }
    double liftParkedSince() const             { return liftParkedSince_; }
    double liftDwell() const                   { return liftDwell_; }
    // Cleared by a foe's destructor on the cell it stood on; meaning unknown.
    void setField1a1(int b)                    { field_1a1 = b; }

    // Set by the level builder.  teleportId is a teleporter's pair id, moved
    // here from the param byte.
    void setTeleportId(unsigned char id)       { teleportId_ = id; }
    void setTeleportU(unsigned char u)          { teleportU_ = u; }
    void setTeleportV(unsigned char v)          { teleportV_ = v; }
    void setField1f1(unsigned char b)          { field_1f1 = b; }
    void setField1f3(unsigned char b)          { field_1f3 = b; }
    // A random phase, rand() * 2pi / 32768, for every cell holding an item.
    void setItemPhase(float p)                 { itemPhase_ = p; }
    float itemPhase() const                    { return itemPhase_; }

    // Read by the foe pathfinder.  field_1f1 is set from the param byte on a
    // jump pad; what it is for is not settled.
    unsigned char field1f1() const             { return field_1f1; }
    // On a slide cell, its direction byte, moved here from the param byte.
    unsigned char slideDir() const             { return slideDir_; }

    // Read by the movement tick, sometimes signed and sometimes unsigned; the
    // accessors are unsigned and the signed reads cast at the call site.
    unsigned char blastHeight() const          { return blastHeight_; }
    float  liftLiveHeight() const              { return liftLiveHeight_; }
    unsigned char platformSlot() const            { return platformSlot_; }
    unsigned char platformOriginU() const         { return platformOriginU_; }
    unsigned char platformOriginV() const         { return platformOriginV_; }
    unsigned char platformCellU() const           { return platformCellU_; }
    unsigned char platformCellV() const           { return platformCellV_; }
    float  platformPosU() const                   { return platformPosU_; }
    float  platformPosY() const                   { return platformPosY_; }
    float  platformPosV() const                   { return platformPosV_; }
    // On a teleporter cell: the destination cell.
    unsigned char teleportU() const             { return teleportU_; }
    unsigned char teleportV() const             { return teleportV_; }
    // On a switch cell: the switch number, copied into the entity.
    unsigned char field1f3() const             { return field_1f3; }

    Tile() = default;

private:

    unsigned char height_{};        // file byte 0
    unsigned char objectMarker_{};  // file byte 1: the kind
    unsigned char param_{};         // file byte 2
    unsigned char contents_{};      // file byte 3: what lies here
    unsigned char blastHeight_{};   // a live blast's height, 0 for none
    int           field_1a1{};
    unsigned char occupant_{};        // the kind of entity standing here
    float         liftLiveHeight_{};  // the lift's live height
    unsigned char platformSlot_{};       // which platform's track this is
    unsigned char platformHeight_{};
    double        platformParkedSince_{};  // phase start while parked
    double        platformDwell_{};        // park dwell, ms: 1500
    int           platformTrack_{};        // 1 on a platform's track
    unsigned char platformOriginU_{};      // the track's spawn cell
    unsigned char platformOriginV_{};
    unsigned char platformCellU_{};  // live cell, on the origin tile only
    unsigned char platformCellV_{};
    float         platformPosU_{};  // live position, on the origin tile only
    float         platformPosY_{};
    float         platformPosV_{};
    unsigned char liftSlot_{};         // which lift stands here
    signed char   liftBottom_{};       // where the lift parks at the bottom
    signed char   liftTop_{};          // and at the top
    double        liftMovingSince_{};  // phase start while moving
    double        liftParkedSince_{};  // phase start while parked
    double        liftDwell_{};        // park dwell, ms: 1500
    unsigned char teleportId_{};       // a teleporter's pair id
    unsigned char teleportU_{};        // teleporter destination
    unsigned char teleportV_{};
    unsigned char field_1f1{};  // set from param on a jump pad
    unsigned char slideDir_{};  // slide cell: its direction
    unsigned char field_1f3{};
    unsigned char bridgeSlot_{};  // the bridge's switch slot
    unsigned char bridgeAxis_{};  // 1 along u, 2 along v
    int           field_1f6{};
    unsigned char field_202{};
    int           field_203{};
    double        blastTime_{};  // when a blast spent this cell
    int           field_20f{};
    float         itemPhase_{};  // an item's random phase
    int           busy_{};       // a pad, teleporter or falling tile is in use
};