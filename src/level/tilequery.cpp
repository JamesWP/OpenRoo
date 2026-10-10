/* The tile queries.  Each object list is a switch's cells (switchcells.h)
 * paired with the bridge that switch arms.  Distances are
 * (byte)trunc(sqrtl(sum of integer squares)): the squares are integers and the
 * square root is truncated without rounding through double first, which would
 * turn a one-ulp difference into a whole unit at a perfect square.
 *
 * PRESERVED:
 *   1. The listed-object search's entry guard, (unsigned)count + 1 > 0,
 *      cannot fail.
 *   2. Its outer loop runs count + 1 times: an 8-bit counter compared
 *      signed against a bound of count + 1 re-read every pass.  A count of
 *      0xff would never end.
 *   3. The blocked flag is inverted: a list's tiles are marked blocked when
 *      its bridge's phase is zero.
 *   4. The pair read takes v first, then u one byte below it.
 *   5. The radius search can never select row or column 0.
 *   6. Its window is half-open: radius tiles one way, radius - 1 the other.
 *   7. The listed-object search writes *pu and *pv on every inner pass as
 *      scratch, and restores them only on failure.
 *   8. The farthest-tile search's inner index is a signed char.
 *   9. Its best distance is compared at extended precision and stored as a
 *      double; collapsing both to one precision can change which of two
 *      nearly equidistant tiles wins.
 * 10. It succeeds only for a best distance strictly above 0 (a NaN fails).
 * 11. The two nearest searches accept unsigned, strictly, and reject a
 *      distance of 0.
 *
 * Negative controls (KAROO_SIM_FX), each changing which candidate wins rather
 * than whether one does: "blockinvert" inverts the blocked flag, "listfar" and
 * "radiusfar" pick the farthest candidate, "farnear" the nearest.
 * KAROO_TILEQ_DIAG=1 logs the first call and first hit of each query and a
 * count every 5000 calls. */

#include <stdint.h>
#include "sysdev.h"
#include <string.h>
#include <math.h>

#include "logger.h"
#include "tilequery.h"

/* By value, not by presence. */
enum { FX_NONE = 0, FX_BLOCKINVERT, FX_LISTFAR, FX_RADIUSFAR, FX_FARNEAR };

static int s_fx   = FX_NONE;
static int s_diag = 0;
static int s_init = 0;

static void fx_init(void)
{
    char buf[64];
    uint32_t n;

    if (s_init)
        return;
    s_init = 1;

    n = sysdev::getEnv("KAROO_SIM_FX", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        if (strcmp(buf, "blockinvert") == 0) {
            s_fx = FX_BLOCKINVERT;
            g_logger.write("tilequery: KAROO_SIM_FX=blockinvert -- blocked flag "
                      "inverted\n");
        } else if (strcmp(buf, "listfar") == 0) {
            s_fx = FX_LISTFAR;
            g_logger.write("tilequery: KAROO_SIM_FX=listfar -- listed-object "
                      "search picks the farthest\n");
        } else if (strcmp(buf, "radiusfar") == 0) {
            s_fx = FX_RADIUSFAR;
            g_logger.write("tilequery: KAROO_SIM_FX=radiusfar -- radius search "
                      "picks the farthest\n");
        } else if (strcmp(buf, "farnear") == 0) {
            s_fx = FX_FARNEAR;
            g_logger.write("tilequery: KAROO_SIM_FX=farnear -- farthest-tile "
                      "search picks the nearest\n");
        }
    }

    n = sysdev::getEnv("KAROO_TILEQ_DIAG", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0)
        s_diag = 1;
}

#include "levelmap.h"
#include "switchcells.h"
#include "tile.h"

/* The tile tables are the map's; reached through Game::map(). */

/* Truncation toward zero; only the low byte is kept. */
static inline unsigned char ftol_b(long double v)
{
    return (unsigned char)(long long)v;
}

/* The distance both nearest searches compute, at extended precision. */
static inline unsigned char tile_distance(int da, int db)
{
    return ftol_b(sqrtl((long double)(int)(da * da + db * db)));
}

/* Diagnostics. */
enum { Q_MARK = 0, Q_LIST, Q_RADIUS, Q_FAR };

static unsigned long s_calls = 0;
static int s_first[4]   = { 0, 0, 0, 0 };
static int s_success[4] = { 0, 0, 0, 0 };

static const char *const k_names[4] = {
    "MarkListedTilesBlockedByObject",
    "FindNearestListedObjectTile",
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

/* Recomputes the blocked word of one list's tiles from its bridge. */
  void  
Sim_MarkListedTilesBlockedByObject(SwitchCells *sw, LevelMap *map,
                                   int bridgePhase, unsigned int listIndex)
{
    unsigned int   li = listIndex & 0xff;
    int            flagged;
    unsigned int   value;
    int            i;

    fx_init();
    diag_enter(Q_MARK, sw);

    // PRESERVED: blocked when the phase is zero.  Read once, before the loop.
    flagged = bridgePhase;
    value   = (unsigned int)(flagged == 0);

    if (s_fx == FX_BLOCKINVERT)
        value = !value;

    if (sw->count(li) == 0)
        return;

    i = 0;
    do {
        // PRESERVED: v at the pointer, u one byte below it.
        unsigned char v = sw->cellV(li, (unsigned)i);
        unsigned char u = sw->cellU(li, (unsigned)i);

        ++i;

        map->tile(u, v)->setBusy((int)value);

    // The bound is re-read every pass.
    } while (i < (int)(unsigned)sw->count(li));
}

/* The listed tile nearest the one passed in, among tiles whose blocked word is
 * non-zero, within maxDist. */
  unsigned int  
Sim_FindNearestListedObjectTile(SwitchCells *sw, unsigned char switchMax,
                                LevelMap *map, unsigned char *pu,
                                unsigned char *pv, unsigned char maxDist)
{
    unsigned char  u0 = *pu;  // saved inputs, restored on failure
    unsigned char  v0 = *pv;
    unsigned char  best     = maxDist;
    unsigned char  bestU    = 0, bestV = 0;
    unsigned char  farDist  = 0;  // KAROO_SIM_FX=listfar only
    unsigned char  farU     = 0, farV = 0;
    int            haveFar  = 0;
    unsigned int   list;

    fx_init();
    diag_enter(Q_LIST, sw);

    // PRESERVED: this guard cannot fail.
    if ((int)((unsigned int)switchMax + 1) > 0) {

        for (list = 0; ; ) {
            unsigned char inner = 0;

            if (sw->count(list) != 0) {
                do {
                    unsigned char  u, v;

                    u = sw->cellU(list, inner);
                    // PRESERVED: both outputs are scribbled on every pass.
                    *pu = u;
                    v = sw->cellV(list, inner);
                    *pv = v;

                    if (map->tile(u, v)->busy() != 0) {
                        unsigned char d =
                            tile_distance((int)u0 - (int)u,
                                          (int)v0 - (int)v);

                        // Unsigned and strict; a zero distance is rejected.
                        if (d != 0 && d < best) {
                            best  = d;
                            bestU = u;
                            bestV = v;
                        }
                        // The same candidates the other way round, for the
                        // listfar control only.
                        if (d != 0 && d < maxDist &&
                            (!haveFar || d > farDist)) {
                            haveFar = 1;
                            farDist = d;
                            farU    = u;
                            farV    = v;
                        }
                    }

                    // An 8-bit counter compared unsigned against the count.
                    ++inner;
                } while (inner < sw->count(list));
            }

            // PRESERVED: an 8-bit counter widened for a signed compare against
            // count + 1, re-read every pass: the body runs count + 1 times.
            list = (unsigned int)(unsigned char)(list + 1);
            if (!((int)list < (int)((unsigned int)switchMax + 1)))
                break;
        }

        if (best < maxDist) {
            if (s_fx == FX_LISTFAR && haveFar) {
                bestU = farU;
                bestV = farV;
            }
            *pu = bestU;
            *pv = bestV;
            diag_hit(Q_LIST, bestU, bestV);
            return 1;
        }
    }

    *pu = u0;
    *pv = v0;
    return 0;
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
    unsigned char  farDist = 0;  // KAROO_SIM_FX=radiusfar only
    unsigned char  farU    = 0, farV = 0;
    int            haveFar = 0;
    int            v, vEnd, uBeg, uEnd;

    fx_init();
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
                        if (d != 0 && d < radius &&
                            (!haveFar || d > farDist)) {
                            haveFar = 1;
                            farDist = d;
                            farU    = (unsigned char)u;
                            farV    = (unsigned char)v;
                        }
                    }

                    // One step of u.
                    ++u;
                } while (u < uEnd);
            }
            ++v;
        } while (v < vEnd);

        if (best < radius) {
            if (s_fx == FX_RADIUSFAR && haveFar) {
                bestU = farU;
                bestV = farV;
            }
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
    unsigned char  nearU = 0, nearV = 0;  // KAROO_SIM_FX=farnear only
    double         best  = 0.0;
    double         near_ = 0.0;
    int            haveNear = 0;
    unsigned char  v;

    fx_init();
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
                        long double d = sqrtl((long double)(int)(
                            ((int)(signed char)v - (int)v0) *
                            ((int)(signed char)v - (int)v0) +
                            (ui - (int)u0) * (ui - (int)u0)));

                        if ((long double)best < d) {
                            bestV = v;
                            bestU = u;
                            // ...and only then rounded to double on store.
                            best  = (double)d;
                        }
                        if (!haveNear || d < (long double)near_) {
                            haveNear = 1;
                            near_    = (double)d;
                            nearV    = v;
                            nearU    = u;
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
            if (s_fx == FX_FARNEAR && haveNear) {
                bestU = nearU;
                bestV = nearV;
            }
            *pu = bestU;
            *pv = bestV;
            diag_hit(Q_FAR, bestU, bestV);
            return 1;
        }
    }

    return 0;
}
