/* The tile queries.  A switch's cells are the tiles of kind TILE_SWITCH that
 * carry its number (Tile::field1f3); switch k arms bridge k.  Distances are
 * (byte)trunc(sqrt(sum of integer squares)): the squares are integers and the
 * square root is truncated without rounding through double first, which would
 * turn a one-ulp difference into a whole unit at a perfect square.
 *
 * PRESERVED:
 *   1. The radius search can never select row or column 0.
 *   2. Its window is half-open: radius tiles one way, radius - 1 the other.
 *   3. The farthest-tile search's inner index is a signed char.
 *   4. Its best distance is compared at extended precision and stored as a
 *      double; collapsing both to one precision can change which of two
 *      nearly equidistant tiles wins.
 *   5. It succeeds only for a best distance strictly above 0 (a NaN fails).
 *   6. The two nearest searches accept unsigned, strictly, and reject a
 *      distance of 0.
 *
 * KAROO_TILEQ_DIAG=1 logs the first call and first hit of each query and a
 * count every 5000 calls. */

#include <stdint.h>
#include "sysdev.h"
#include <string.h>
#include <math.h>
#include <algorithm>

#include "logger.h"
#include "tilequery.h"

static int s_diag = 0;
static int s_init = 0;

static void diag_init(void)
{

    if (s_init)
        return;
    s_init = 1;

    s_diag = sysdev::envFlag("KAROO_TILEQ_DIAG");
}

#include "levelmap.h"
#include "tile.h"

/* The tile tables are the map's; reached through World::map(). */

/* Truncation toward zero; only the low byte is kept. */
static inline unsigned char ftol_b(double v)
{
    return (unsigned char)(long long)v;
}

/* The distance both nearest searches compute, at extended precision. */
static inline unsigned char tile_distance(int da, int db)
{
    return ftol_b(sqrt((double)(int)(da * da + db * db)));
}

/* Diagnostics. */
enum { Q_MARK = 0, Q_LIST, Q_RADIUS, Q_FAR };

static unsigned long s_calls = 0;
static int s_first[4]   = { 0, 0, 0, 0 };
static int s_success[4] = { 0, 0, 0, 0 };

static const char *const k_names[4] = {
    "MarkSwitchTiles",
    "FindNearestBusySwitch",
    "FindNearestFlaggedTileInRadius",
    "FindFarthestOccupiedTile",
};

/* Once each, unconditionally: a line gated on the flag could not tell "never
 * called" from "the flag never arrived". */
static void diag_enter(int which, const void *self)
{
    if (!s_first[which]) {
        s_first[which] = 1;
        g_logger.write("tilequery: first %s -- this=%p\n", k_names[which], self);
    }
    if (s_diag) {
        ++s_calls;
        if ((s_calls % 5000) == 0)
            g_logger.write("tilequery: %lu calls\n", s_calls);
    }
}

static void diag_hit(int which, unsigned u, unsigned v)
{
    if (!s_success[which]) {
        s_success[which] = 1;
        g_logger.write("tilequery: first %s HIT -- u=%u v=%u\n",
                  k_names[which], u, v);
    }
}

/* Sets the busy flag of switch sw's tiles from the phase of the bridge it
 * arms: busy while the phase is zero. */
void Sim_MarkSwitchTiles(LevelMap *map, int bridgePhase, unsigned int sw)
{
    diag_enter(Q_MARK, map);
    Sim_ForEachSwitchCell(map, sw, [&](unsigned char u, unsigned char v) {
        map->tile(u, v)->setBusy(bridgePhase == 0);
    });
}

/* The busy switch tile nearest (*pu, *pv), within maxDist and not on it.  Of
 * two as near, the lower switch number wins, then the lower v, then the
 * lower u: the order the switches' cells were listed in. */
unsigned int Sim_FindNearestBusySwitch(const LevelMap *map, unsigned char *pu, unsigned char *pv,
                                       unsigned char maxDist)
{
    const int u0 = *pu, v0 = *pv;
    bool found = false;
    int best = 0, bestU = 0, bestV = 0, bestSw = 0;

    diag_enter(Q_LIST, map);
    for (int v = std::max(0, v0 - maxDist); v <= std::min(map->extentV() - 1, v0 + maxDist); v++) {
        for (int u = std::max(0, u0 - maxDist); u <= std::min(map->extentU() - 1, u0 + maxDist); u++) {
            const Tile *t = map->tile(u, v);
            if (t->objectMarker() != TILE_SWITCH || t->busy() == 0)
                continue;
            const unsigned char d = tile_distance(u0 - u, v0 - v);
            if (d == 0 || d >= maxDist)
                continue;
            if (found && (d > best || (d == best && t->field1f3() >= bestSw)))
                continue;
            found = true;
            best = d;
            bestU = u;
            bestV = v;
            bestSw = t->field1f3();
        }
    }
    if (!found)
        return 0;
    *pu = (unsigned char)bestU;
    *pv = (unsigned char)bestV;
    diag_hit(Q_LIST, *pu, *pv);
    return 1;
}

/* The nearest tile with flag 1 in a square window around the one passed in.
 * Outer loop v, inner u. */
  unsigned int  
Sim_FindNearestFlaggedTileInRadius(LevelMap *map, unsigned char *pu,
                                   unsigned char *pv, unsigned char radius)
{
    unsigned char  u0 = *pu;
    unsigned char  v0 = *pv;
    unsigned char  best    = radius;
    unsigned char  bestU   = 0, bestV = 0;
    int            v, vEnd, uBeg, uEnd;

    diag_init();
    diag_enter(Q_RADIUS, map);

    // PRESERVED: half-open, radius tiles one way and radius - 1 the other.
    v    = (int)v0 - (int)radius;
    vEnd = (int)v0 + (int)radius;

    if (v < vEnd) {
        uBeg = (int)u0 - (int)radius;
        uEnd = (int)u0 + (int)radius;

        do {
            if (uBeg < uEnd) {
                unsigned char  vExtent = map->extentV();
                int            u       = uBeg;

                do {
                    // PRESERVED: strictly above zero, so row and column 0 are
                    // never chosen.
                    if (v < (int)(unsigned)vExtent && v > 0 &&
                        u < (int)(unsigned)map->extentU() && u > 0 &&
                        map->tile(u, v)->contents() == CONTENTS_CRYSTAL) {
                        unsigned char d =
                            tile_distance((int)u0 - u, (int)v0 - v);

                        if (d != 0 && d < best) {
                            best  = d;
                            bestU = (unsigned char)u;
                            bestV = (unsigned char)v;
                        }
                    }

                    // One step of u.
                    ++u;
                } while (u < uEnd);
            }
            ++v;
        } while (v < vEnd);

        if (best < radius) {
            *pu = bestU;
            *pv = bestV;
            diag_hit(Q_RADIUS, bestU, bestV);
            return 1;
        }
    }

    *pu = u0;
    *pv = v0;
    return 0;
}

/* The occupied, unmarked tile farthest from the one passed in, over the whole
 * grid.  The only query that keeps a real distance. */
  unsigned int  
Sim_FindFarthestOccupiedTile(LevelMap *map, unsigned char *pu,
                             unsigned char *pv)
{
    LevelMap      *tiles = map;
    LevelMap      *hdr   = tiles;
    unsigned char  u0    = *pu;
    unsigned char  v0    = *pv;
    unsigned char  bestU = *pu;  // seeded from the inputs, not zeroed
    unsigned char  bestV = *pv;
    double         best  = 0.0;
    unsigned char  v;

    diag_init();
    diag_enter(Q_FAR, map);

    if (hdr->extentV() != 0) {
        v = 0;
        do {
            unsigned char u = 0;

            if (hdr->extentU() != 0) {
                int ui = 0;

                do {
                    // Outer index v, inner u.
                    Tile *t = tiles->tile( ui, (int)(signed char)v);

                    if (t->objectMarker() != TILE_EMPTY && t->occupant() == 0) {
                        // PRESERVED: compared at full extended precision...
                        double d = sqrt((double)(int)(
                            ((int)(signed char)v - (int)v0) *
                            ((int)(signed char)v - (int)v0) +
                            (ui - (int)u0) * (ui - (int)u0)));

                        if ((double)best < d) {
                            bestV = v;
                            bestU = u;
                            // ...and only then rounded to double on store.
                            best  = (double)d;
                        }
                    }

                    // PRESERVED: an 8-bit index read back sign-extended.
                    ++u;
                    ui = (int)(signed char)u;
                } while (ui < (int)(unsigned)hdr->extentU());
            }
            ++v;
        } while ((int)(signed char)v < (int)(unsigned)hdr->extentV());

        // PRESERVED: strict; a NaN best returns 0.
        if (best > 0.0) {
            *pu = bestU;
            *pv = bestV;
            diag_hit(Q_FAR, bestU, bestV);
            return 1;
        }
    }

    return 0;
}
