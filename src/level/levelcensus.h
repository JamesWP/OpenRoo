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

/* The per-level object counts.  Names are the builder's (its C_* defines);
 * "L2" counts are of the map's SNAPSHOT contents (levelmap.h), the rest of
 * the live tiles.  reportwriter.cpp prints most of them, by offset. */
struct __attribute__((packed)) LevelCensus {
    unsigned short teleports;      /* 0x421df */
    unsigned short type17;         /* 0x421e1 */
    unsigned short type1;          /* 0x421e3 */
    unsigned short field_e5;       /* 0x421e5  never reset, never counted */
    unsigned short total;          /* 0x421e7 */
    unsigned short bridges;        /* 0x421e9 */
    unsigned short unusedEb;       /* 0x421eb  reset only */
    unsigned short type2;          /* 0x421ed */
    unsigned short type10;         /* 0x421ef */
    unsigned short type15;         /* 0x421f1 */
    unsigned short l2_8;           /* 0x421f3 */
    unsigned short l2_6;           /* 0x421f5 */
    unsigned short item7;          /* 0x421f7 */
    unsigned short l2_d;           /* 0x421f9 */
    unsigned short shadow1;        /* 0x421fb */
    unsigned short shadow7;        /* 0x421fd */
    unsigned short l2_5;           /* 0x421ff */
    unsigned short l2_a;           /* 0x42201 */
    unsigned short l2_9;           /* 0x42203 */
    unsigned short freeBombs;      /* 0x42205  entries in the FreeBomb table */
    unsigned short timed;          /* 0x42207  entries in the TimedSpawner table */
    unsigned short type0e;         /* 0x42209 */

    /* ResetLevelObjectCounters 0x41ceb0: in the original's store ORDER,
     * which is not ascending, and with field_e5 left out as it is there. */
    void reset()
    {
        teleports = 0; type17 = 0; type1 = 0; total = 0; bridges = 0;
        unusedEb = 0; type2 = 0; type10 = 0; type15 = 0; l2_8 = 0;
        l2_6 = 0; l2_d = 0; l2_a = 0; l2_5 = 0; shadow1 = 0;
        shadow7 = 0; type0e = 0; item7 = 0; l2_9 = 0; freeBombs = 0;
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
static_assert(offsetof(LevelCensus, type0e)     == 0x42209 - 0x421df, "");
