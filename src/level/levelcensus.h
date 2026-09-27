/* The level builder's census and its two spawn tables, sub-objects of Game,
 * filled by the builder (levelsetup.cpp) as it walks the map.  Each table
 * holds up to 256 entries, the most that fit its space; the builders index
 * them by the census count with no bound check.  The free-bomb table is
 * written and never read. */
#pragma once

/* A timed foe spawner: a snapshot cell whose contents are 0x64. */
struct __attribute__((packed)) TimedSpawner {
    unsigned char u;
    unsigned char v;
    unsigned char height;     // the snapshot cell's height
    double        lastSpawn;  // the clock at the last spawn attempt
    // Read by the tick (plus 100, it is the spawned foe's drop contents);
    // never written by the builder.
    unsigned char field_0b;
    double        interval;  // ms between spawns
    unsigned char maxFoes;   // spawns only below this foe count
};

/* A free bomb: a snapshot cell whose contents are 0x4d. */
struct __attribute__((packed)) FreeBomb {
    unsigned char u;
    unsigned char v;
    unsigned char param;     // the snapshot cell's param
    double        placedAt;  // the clock when the level was built
};

/* The per-level object counts.  Each counts one tile kind or contents value
 * (tile.h) over either the live grid or the map's snapshot grid (levelmap.h),
 * as marked.  The level report prints them. */
struct __attribute__((packed)) LevelCensus {
    // Kind counts, over the live grid.
    unsigned short teleports;      // TILE_TELEPORTER, paired only
    unsigned short destructibles;  // TILE_DESTRUCTIBLE
    unsigned short kind01;         // TILE_KIND_01, itself unnamed
    unsigned short field_e5;       // never reset, never counted
    unsigned short total;          // collectable items: see below
    unsigned short bridges;        // TILE_BRIDGE_U and _V
    unsigned short unusedEb;       // reset only
    unsigned short gluePads;       // TILE_GLUE
    unsigned short climbTiles;     // TILE_CLIMB
    unsigned short conveyors;      // TILE_CONVEYOR

    // Contents counts over the snapshot grid.
    unsigned short effect8Items;  // CONTENTS_EFFECT_8
    unsigned short timeBonuses;   // CONTENTS_TIME_BONUS

    // Contents count over the live grid.
    unsigned short extraLives;  // CONTENTS_EXTRA_LIFE

    unsigned short transforms;  // CONTENTS_TRANSFORM, snapshot

    // The two concealed-item counters keep numeric names: each is counted from
    // two places that do not agree.  shadow1 counts a destructible hiding a
    // crystal, and a kind 2 foe dropping 0x0b or 0x07; shadow7 a destructible
    // hiding an extra life, and a kind 2 foe dropping 0x4d.  The destructible
    // half reads as crystals and lives; the foe half does not (0x07 is an
    // extra life, 0x4d a free bomb), so a meaningful name would be a guess.
    unsigned short shadow1;
    unsigned short shadow7;

    unsigned short paragliders;   // CONTENTS_PARAGLIDER, snapshot
    unsigned short speedUps;      // CONTENTS_SPEED_UP, snapshot
    unsigned short grant09Items;  // CONTENTS_GRANT_09, snapshot
    unsigned short freeBombs;     // entries in the FreeBomb table
    unsigned short timed;         // entries in the TimedSpawner table
    unsigned short jumpPads;      // TILE_JUMP_PAD

    // total is the level's collectable-item count: grant09Items + speedUps +
    // shadow1 + transforms + paragliders + shadow7 + effect8Items + extraLives
    // + timeBonuses + the Game's crystal count.  It counts CONTENTS_TRANSFORM,
    // which the autoplay's pickup test (worldstate.h) does not.  It is not
    // Game::itemTotal().

    // Zeroes the counts, in the game's store order, leaving field_e5 as the
    // game does.
    void reset()
    {
        teleports = 0; destructibles = 0; kind01 = 0; total = 0; bridges = 0;
        unusedEb = 0; gluePads = 0; climbTiles = 0; conveyors = 0; effect8Items = 0;
        timeBonuses = 0; transforms = 0; speedUps = 0; paragliders = 0; shadow1 = 0;
        shadow7 = 0; jumpPads = 0; extraLives = 0; grant09Items = 0; freeBombs = 0;
        timed = 0;
    }
};

static_assert(sizeof(TimedSpawner) == 0x15, "TimedSpawner is 0x15 bytes");
static_assert(sizeof(FreeBomb)     == 0x0b, "FreeBomb is 0xb bytes");
static_assert(sizeof(LevelCensus)  == 0x2c, "LevelCensus is 22 WORDs");
static_assert(offsetof(TimedSpawner, lastSpawn) == 0x03, "");
static_assert(offsetof(TimedSpawner, field_0b)  == 0x0b, "");
static_assert(offsetof(TimedSpawner, interval)  == 0x0c, "");
static_assert(offsetof(TimedSpawner, maxFoes)   == 0x14, "");
static_assert(offsetof(FreeBomb, placedAt)      == 0x03, "");
static_assert(offsetof(LevelCensus, freeBombs)  == 0x42205 - 0x421df, "");
static_assert(offsetof(LevelCensus, jumpPads)     == 0x42209 - 0x421df, "");
