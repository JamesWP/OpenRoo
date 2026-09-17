/* The level builder's census and its two spawn tables, embedded in Game
 * (COHESION_PLAN.md Band 3).  All three are filled by SetupLevelObjects
 * (levelsetup.cpp) as it walks the map.
 *
 *   Game+0x2023d  TimedSpawner[256]  0x15 each  } 0x1500 + 0xb00 bytes, the
 *   Game+0x2173d  FreeBomb[256]      0x0b each  } second ending 2 bytes short
 *                                                 of the CD themes (0x2223f)
 *   Game+0x421df  LevelCensus        22 WORDs, ending exactly at restartCount
 *
 * The 256 is FITTED, not proven: it is the largest count that fits each
 * span, and the writers have no bound check (the census WORD indexes the
 * table directly).  The 2 bytes at 0x2223d are not accounted for.
 *
 * READERS.  An operand search of the original binary for the tables' bases
 * finds only SetupLevelObjects' stores and GameTick's spawner loop -- both
 * ours now.  The free-bomb table has no reader found that way: it is
 * written and, as far as the search can see, never read.  Recorded as a
 * finding of the search, not a proof.
 */
#pragma once

#include "layout.h"

/* A timed foe spawner: a snapshot cell whose contents are 0x64. */
struct __attribute__((packed)) TimedSpawner {
    unsigned char u;               /* +0x00 */
    unsigned char v;               /* +0x01 */
    unsigned char height;          /* +0x02  the snapshot cell's height */
    double        lastSpawn;       /* +0x03  clock at the last spawn attempt */
    /* Read by GameTick (+100 is the spawned foe's drop contents); the
     * builder never writes it. */
    unsigned char field_0b;        /* +0x0b */
    double        interval;        /* +0x0c  ms between spawns */
    unsigned char maxFoes;         /* +0x14  spawns only below this foe count */
};

/* A free bomb: a snapshot cell whose contents are 0x4d. */
struct __attribute__((packed)) FreeBomb {
    unsigned char u;               /* +0x00 */
    unsigned char v;               /* +0x01 */
    unsigned char param;           /* +0x02  the snapshot cell's param */
    double        placedAt;        /* +0x03  the clock when the level was built */
};

/* The per-level object counts.
 *
 * The fields were named after the VALUE each counted -- type1, type2,
 * type0e, l2_5, l2_a, item7 -- from the builder's own C_* defines.  Now that
 * the kind and contents bytes have enums (tile.h), they are named after the
 * thing instead (COHESION_PLAN.md Band 7a): `type0e` is `jumpPads`, `l2_5`
 * is `paragliders`.  Each field's comment names the enumerator it counts and
 * says which grid it walks -- the LIVE one or the map's SNAPSHOT (levelmap.h),
 * which is the distinction the old "L2" prefix carried.
 *
 * Three fields keep a by-the-number name on purpose: `kind01`, because
 * TILE_KIND_01 is itself still unnamed, and `shadow1`/`shadow7`, for the
 * reason set out at their declaration.
 *
 * Renaming is safe against the level report: reportwriter.cpp prints these
 * by OFFSET and its column headings are the game's own strings, so no
 * report byte moves. */
struct __attribute__((packed)) LevelCensus {
    /* Kind counts, over the LIVE grid. */
    unsigned short teleports;      /* 0x421df  TILE_TELEPORTER, paired only */
    unsigned short destructibles;  /* 0x421e1  TILE_DESTRUCTIBLE            */
    unsigned short kind01;         /* 0x421e3  TILE_KIND_01 -- itself still
                                    *          unnamed, so this is too      */
    unsigned short field_e5;       /* 0x421e5  never reset, never counted   */
    unsigned short total;          /* 0x421e7  see below                    */
    unsigned short bridges;        /* 0x421e9  TILE_BRIDGE_U + _V           */
    unsigned short unusedEb;       /* 0x421eb  reset only                   */
    unsigned short gluePads;       /* 0x421ed  TILE_GLUE                    */
    unsigned short climbTiles;     /* 0x421ef  TILE_CLIMB                   */
    unsigned short conveyors;      /* 0x421f1  TILE_CONVEYOR                */

    /* Contents counts over the map's SNAPSHOT grid (levelmap.h) -- these
     * were the builder's C_L2_* names, "L2" being the second tile layer. */
    unsigned short effect8Items;   /* 0x421f3  CONTENTS_EFFECT_8   snapshot */
    unsigned short timeBonuses;    /* 0x421f5  CONTENTS_TIME_BONUS snapshot */

    /* Contents count over the LIVE grid. */
    unsigned short extraLives;     /* 0x421f7  CONTENTS_EXTRA_LIFE          */

    unsigned short transforms;     /* 0x421f9  CONTENTS_TRANSFORM  snapshot */

    /* The two concealed-item counters, and the ONLY two fields here that
     * keep a by-the-number name.  They are deliberately not renamed: each is
     * incremented from two unrelated places, and the second does not agree
     * with the first.
     *
     *   shadow1: a TILE_DESTRUCTIBLE hiding CONTENTS_CRYSTAL (+0x202 == 1),
     *            AND a type-2 foe whose drop param is 0x0b or 0x07
     *   shadow7: a TILE_DESTRUCTIBLE hiding CONTENTS_EXTRA_LIFE (== 7),
     *            AND a type-2 foe whose drop param is 0x4d
     *
     * The destructible branch reads as "concealed crystals / extra lives",
     * which is where the 1 and the 7 come from.  The foe branch does not
     * fit it: drop 0x07 is CONTENTS_EXTRA_LIFE yet counts toward shadow1,
     * and 0x4d is CONTENTS_FREE_BOMB yet counts toward shadow7.  Until a
     * reader settles what the pair really counts, a semantic name here
     * would be a guess -- which is the TILE_SHADOW mistake of Band 7a,
     * caught once already. */
    unsigned short shadow1;        /* 0x421fb */
    unsigned short shadow7;        /* 0x421fd */

    unsigned short paragliders;    /* 0x421ff  CONTENTS_PARAGLIDER snapshot */
    unsigned short speedUps;       /* 0x42201  CONTENTS_SPEED_UP   snapshot */
    unsigned short grant09Items;   /* 0x42203  CONTENTS_GRANT_09   snapshot */
    unsigned short freeBombs;      /* 0x42205  entries in the FreeBomb table */
    unsigned short timed;          /* 0x42207  entries in the TimedSpawner table */
    unsigned short jumpPads;       /* 0x42209  TILE_JUMP_PAD                */

    /* `total` is the level's COLLECTABLE-ITEM count: the builder sets it to
     * grant09Items + speedUps + shadow1 + transforms + paragliders +
     * shadow7 + effect8Items + extraLives + timeBonuses + Game's crystal
     * count (+0x42252).  Note CONTENTS_TRANSFORM is counted as an item here
     * even though worldstate.h's ws_is_pickup() excludes it -- an open
     * disagreement, recorded rather than settled.
     *
     * It is NOT Game::itemTotal(), which is a different word at 0x42250. */

    /* ResetLevelObjectCounters 0x41ceb0: in the original's store ORDER,
     * which is not ascending, and with field_e5 left out as it is there. */
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
