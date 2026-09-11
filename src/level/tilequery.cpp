/* GAMETICK_PLAN.md Band B — the tile-query cluster.
 *
 *   MarkListedTilesBlockedByObject  0x0041a200   2 E8 sites
 *   FindNearestListedObjectTile     0x0041b680   1 E8 site
 *   FindNearestFlaggedTileInRadius  0x0041b810   2 E8 sites
 *   FindFarthestOccupiedTile        0x00412530   1 E8 site
 *
 * ─── Why these four, and why together ────────────────────────────────────
 *
 * Band A closed with the spawn/despawn quartet.  Band B's first cycle was
 * to be a naming pass over "the fourteen unnamed FUN_0041xxxx helpers in
 * GameTick's callee list" -- but re-reading that callee list this session,
 * there are none left: Band 0 and the intervening cycles named every one.
 * The naming cycle is therefore already done, and Band B needs a target
 * instead.
 *
 * Per this document's twice-recorded standing rule -- "the next target" is
 * a hypothesis until its callee list is read -- all four lists were read
 * before any code was written:
 *
 *   MarkListedTilesBlockedByObject   (no callees at all)
 *   FindNearestListedObjectTile      __ftol  0x451134
 *   FindNearestFlaggedTileInRadius   __ftol  0x451134
 *   FindFarthestOccupiedTile         (no callees at all)
 *
 * All four are LEAVES.  None calls anything in the game binary, so this
 * cycle -- like the sliding hazard, and unlike the spawns -- leaves no
 * callback behind.  They are taken as one cycle because they are one
 * family: each answers "which tile?" against the same tilemap, and each is
 * small enough that splitting them would cost four compile-patch-launch
 * cycles to exercise one code path.  Each gets its OWN KAROO_SIM_FX mode so
 * they stay independently falsifiable.
 *
 * `this` is Game for all four, and the offsets below are absolute from the
 * Game base -- the huge `+0x170543` / `+0x2ab7a4` style offsets that appear
 * throughout GameTick, not pData-relative ones.
 *
 *   +0x048b12   object-list count (see the dead entry guard, point 1)
 *   +0x140543   list tile pairs, u byte;  stride 3, list stride 0x300
 *   +0x140544   list tile pairs, v byte
 *   +0x170543   per-list object count, one byte per list
 *   +0x170643   per-list object pointer, one dword per list
 *   +0x2ab727   grid extent, V
 *   +0x2ab728   grid extent, U
 *   +0x2ab72c   tile flag byte      } same tilemap, pitch 0x7f,
 *   +0x2ab7a4   tile blocked dword  } row stride 0x319c = 100 * 0x7f
 *
 * The V/U assignment of the two extent bytes is NOT guessed here: it is
 * what worldstate.cpp already established and has been shipping on --
 * "the axis that is multiplied is called U (extent Game+0x2ab728), the one
 * that is added is V (extent Game+0x2ab727)".
 *
 * FindFarthestOccupiedTile does NOT use the Game-relative tilemap: it takes
 * its base from Game+0x34 and indexes tile fields +0x19a/+0x19b (the grid
 * extents), +0x19d (occupied) and +0x1a5 (marked).  That is the same
 * tilemap record the entity ticks use, reached the other way round, and
 * levelmap.cpp already fixes those two bytes: +0x19b is WIDTH (the u
 * extent), +0x19a is HEIGHT (the v extent).
 *
 * ─── A Band 0 finding that does not survive the listing ──────────────────
 *
 * The plate comments Band 0 left on FindNearestFlaggedTileInRadius and
 * FindFarthestOccupiedTile each assert a "crossed axes" defect -- that the
 * distance is measured against a transposed origin, and (for the radius
 * search) that the value written back through pv is really a u coordinate.
 * Both claims are WRONG, and this cycle is where that gets corrected in
 * Ghidra as well as here.  CLAUDE.md's rule applies to this project's own
 * notes exactly as it does to subagent reports: reliable about addresses
 * and sizes, frequently wrong about what the code MEANS.
 *
 * The claims come from mislabelling which nested loop variable is which.
 * The tile-index arithmetic settles it, because in the `(v + u*100) * 0x7f`
 * addressing the term multiplied by 100 is u BY DEFINITION:
 *
 *   FindNearestFlaggedTileInRadius
 *     tile ptr = (outer + inner*100)*0x7f + 0x2ab72c, stepping +0x319c
 *     (= 100*0x7f) per INNER increment.  So INNER is u and OUTER is v.
 *     Outer runs over [*pv - r, *pv + r) and is bounds-checked against
 *     +0x2ab727 (V);  inner runs over [*pu - r, *pu + r) and is checked
 *     against +0x2ab728 (U).  Both correct.
 *     The "local_25 = local_24" the decompile shows is not a mis-assignment
 *     of u into v -- local_24 was itself just set from the OUTER (v) index.
 *     It is one value living in two stack slots, which is what a compiler
 *     emits, and *pv duly receives v.
 *
 *   FindFarthestOccupiedTile
 *     tile index = (outer + inner*100)*0x7f, so again inner is u, outer v.
 *     The listing at 004125c6..004125de differences the outer index against
 *     the slot seeded from *pv and the inner against the slot seeded from
 *     *pu;  the outer loop is bounded by +0x19a (height/v) and the inner by
 *     +0x19b (width/u);  and the write-back is *pu = inner, *pv = outer.
 *     Every one of those pairs agrees.  Nothing is transposed.
 *
 * So the origin is NOT transposed in either, and neither function needs a
 * deliberate bug reproduced on this account.  The genuine defects are the
 * ones listed below, which are fewer but real.
 *
 * ─── The decompiler drops both `__ftol` arguments ────────────────────────
 *
 * As in foechase.cpp, tileeffects.cpp and slidinghazard.cpp before it,
 * `decompile_function` renders each call as a bare `lVar = __ftol();`.  The
 * argument arrives on the x87 stack and is invisible to the decompiler, so
 * both were read from the LISTING this cycle:
 *
 *   0041b8d6  FILD dword ptr [ESP + 0x24]   sum of squares, INTEGER
 *   0041b8da  FSQRT
 *   0041b8dc  CALL __ftol                   <- 80-bit ST0, no store between
 *
 *   0041b760  FILD dword ptr [ESP + 0x2c]   sum of squares, INTEGER
 *   0041b764  FSQRT
 *   0041b766  CALL __ftol                   <- 80-bit ST0, no store between
 *
 * Both distances are therefore
 * `(byte)(long long)sqrtl((long double)(int)sumsq)`.  The squares are formed
 * with IMUL in integer registers and only then FILD'd -- there is no
 * floating-point multiply anywhere in either -- and nothing rounds the
 * FSQRT result to `float` or `double` before __ftol truncates it.  Written
 * with `sqrtl` and `long double` for that reason: a `sqrt()` in `double`
 * would round first, and the truncation immediately afterwards turns a
 * 1-ulp difference into a whole distance unit at any perfect square.
 *
 * The radius search maintains its two deltas INCREMENTALLY -- each is
 * seeded to the radius at the window edge and DEC'd as the matching loop
 * index is INC'd, so the register holds (origin - current) throughout.
 * Both are exact integers with no accumulation, so writing the difference
 * directly is the same value at every iteration, and it is written directly
 * below for legibility.
 *
 * ─── Exactness points preserved ──────────────────────────────────────────
 *
 * 1. THE LISTED-OBJECT SEARCH'S ENTRY GUARD IS DEAD CODE.  The listing
 *    loads the count byte ZERO-extended, increments it as a 32-bit value
 *    and tests the result:
 *
 *      0041b69a  MOV  CL,byte ptr [ESI + 0x48b12]
 *      0041b6a8  INC  ECX
 *      0041b6ad  TEST ECX,ECX
 *      0041b6b3  JLE  <fail>
 *
 *    A zero-extended byte plus one lies in [1, 0x100], so the branch can
 *    never be taken.  (This is what the decompiler renders as the
 *    impossible `!= 0xffffffff`.)  It is transcribed as an always-true
 *    condition rather than deleted, so the shape still matches the original.
 *
 * 2. THAT SEARCH'S OUTER LOOP RUNS ONE LIST PAST THE COUNT.  The bound is
 *    reloaded and incremented every iteration (`0041b7a6` / `0041b7bb`) and
 *    compared SIGNED (`CMP EAX,ECX` / `JL`), so the body executes
 *    count + 1 times.  The extra list's own count byte is read from the
 *    same +0x170543 table and is normally zero, which is why this is
 *    invisible rather than harmless-by-design.  The +1 is preserved.
 *
 *    Its counter is an 8-BIT register (`INC AL`) widened for that signed
 *    compare, so a count of 0xff gives a bound of 0x100 the counter can
 *    never reach and the loop never terminates.  Unreachable in stock
 *    content;  written as the original has it rather than widened.
 *
 * 3. THE BLOCKED FLAG IS INVERTED.  MarkListedTilesBlockedByObject writes
 *    `(object->field_0x58 == 0)` -- tiles are marked blocked when the
 *    object's field is ZERO and cleared when it is set.  Reading it the
 *    natural way round is wrong everywhere.
 *
 * 4. ITS PAIR READ TAKES v FIRST, THEN u AT pointer-1.  The pointer walks
 *    from +0x140544 in steps of 3 and u is read one byte BELOW it.  The -1
 *    is not an off-by-one to clean up.
 *
 * 5. THE RADIUS SEARCH CANNOT SELECT ROW OR COLUMN 0.  Its bounds test is
 *    strictly greater than zero on both axes, so the outermost ring of the
 *    grid is unreachable.  Preserved.
 *
 * 6. ITS WINDOW IS HALF-OPEN: `< centre + radius`, so it reaches radius-1
 *    tiles in the positive direction and radius in the negative.  The
 *    window is not symmetric about the centre.  Preserved.
 *
 * 7. THE LISTED-OBJECT SEARCH SCRIBBLES ON ITS OUTPUTS.  It writes *pu and
 *    *pv on EVERY inner iteration as scratch while scanning (`0041b6ff`,
 *    `0041b70c`) and only restores them on the failure path at the end
 *    (`0041b7ff`, `0041b802`).  A caller that aliases them into live state
 *    sees intermediate values.  The scratch writes are kept.
 *
 * 8. FindFarthestOccupiedTile's INNER INDEX IS A SIGNED CHAR.  `INC DL`
 *    then a sign-extending read, so a grid wider than 127 would wrap
 *    negative and end the loop.  Stock grids are 100 wide so it never
 *    engages;  the cast is kept as the original has it.
 *
 * 9. ITS BEST DISTANCE IS COMPARED AT 80 BITS AND STORED AT 64.  The
 *    listing compares the FSQRT result on the x87 stack against the running
 *    best and only then rounds it down to a `double`:
 *
 *      004125f4  FSQRT
 *      ...       compare against [ESP + 0x28]
 *      00412609  FSTP double ptr [ESP + 0x28]
 *
 *    So the comparison sees the full extended-precision value but the
 *    stored best is a `double`.  Written that way below.  Collapsing both
 *    to `double`, or both to `long double`, changes which of two nearly
 *    equidistant tiles wins.  Note this one does NOT go through __ftol at
 *    all -- it is the only one of the four that keeps a real distance
 *    rather than a truncated byte.
 *
 * 10. ITS SUCCESS TEST IS STRICT AND UNORDERED-FALSE.  DAT_0045d390 was
 *    read this session and is `00 00 00 00 00 00 00 00` -- the double 0.0,
 *    loaded as `FCOMP double ptr`, not as a float:
 *
 *      0041263f  FLD   double ptr [ESP + 0x28]
 *      00412643  FCOMP double ptr [0x0045d390]
 *      00412649  FNSTSW AX
 *      0041264b  TEST  AH,0x41
 *      0041264e  JNZ   return 0
 *
 *    TEST AH,0x41 is C0|C3 -- less-than OR EQUAL -- and both bits are also
 *    set when the compare is unordered.  So the function returns 1 only
 *    when the best distance is STRICTLY greater than zero, and a NaN
 *    returns 0.  Plain `best > 0.0` is unordered-false in C too, so that is
 *    what is written -- and the equality half matters: a best of exactly
 *    0.0, which is the initial value, returns 0 and leaves *pu/*pv
 *    untouched.  That is the no-candidate path.
 *
 * 11. THE TWO SEARCHES' ACCEPT TESTS ARE UNSIGNED AND STRICT.  `CMP AL,..`
 *    with JNC in both, on bytes that have already been truncated from the
 *    __ftol result.  A distance of 0 is additionally rejected outright, so
 *    a candidate sitting exactly on the origin never wins.
 *
 * ─── Measurable proof, one mode per function ─────────────────────────────
 *
 * Per the plan's acceptance point 5, each function gets its own
 * KAROO_SIM_FX mode, and each is a DIRECTION change rather than a magnitude
 * one -- "a colour tint proves the code runs; a direction change proves the
 * maths":
 *
 *   blockinvert   MarkListedTilesBlockedByObject inverts the blocked flag,
 *                 so the tiles an object blocks become exactly the tiles it
 *                 does not.  A change to what the world is made of, not to
 *                 where anything is.
 *   listfar       FindNearestListedObjectTile picks the FARTHEST qualifying
 *                 listed object instead of the nearest.
 *   radiusfar     FindNearestFlaggedTileInRadius picks the FARTHEST
 *                 qualifying tile in its window instead of the nearest.
 *   farnear       FindFarthestOccupiedTile picks the NEAREST occupied tile
 *                 instead of the farthest -- the mirror of the above.
 *
 * The three near/far modes deliberately do NOT flip the accept test or the
 * success test.  They keep the same candidate set and the same "did we
 * beat the threshold" answer, and change only WHICH member of that set
 * wins.  Flipping the comparison itself would empty the set and degrade the
 * control into "always fail", which proves far less -- the same reasoning
 * that made `keyclash` break injectivity rather than perturb a value.
 *
 * KAROO_TILEQ_DIAG=1 logs the first call to each of the four and the first
 * SUCCESSFUL result from each, plus a call count every 5000.  It exists for
 * the reason the hazard, lift and block diags do: to answer honestly
 * whether a recording that reaches these queries ever gets a HIT from one,
 * which is the difference between a control that fails and a control that
 * cannot fail.
 */

#include <windows.h>
#include <string.h>
#include <math.h>

#include "log.h"
#include "tilequery.h"

/* ─── KAROO_SIM_FX / KAROO_TILEQ_DIAG, read by VALUE ──────────────────────
 *
 * By value, never by presence: GetEnvironmentVariableA returns 0 for empty
 * and unset alike, and a bare presence test is what silently turned the TGA
 * acceptance test into a no-op (RENDER_PLAN.md, 2026-09-02). */
enum { FX_NONE = 0, FX_BLOCKINVERT, FX_LISTFAR, FX_RADIUSFAR, FX_FARNEAR };

static int s_fx   = FX_NONE;
static int s_diag = 0;
static int s_init = 0;

static void fx_init(void)
{
    char buf[64];
    DWORD n;

    if (s_init)
        return;
    s_init = 1;

    n = GetEnvironmentVariableA("KAROO_SIM_FX", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        if (strcmp(buf, "blockinvert") == 0) {
            s_fx = FX_BLOCKINVERT;
            log_write("tilequery: KAROO_SIM_FX=blockinvert -- blocked flag "
                      "inverted\n");
        } else if (strcmp(buf, "listfar") == 0) {
            s_fx = FX_LISTFAR;
            log_write("tilequery: KAROO_SIM_FX=listfar -- listed-object "
                      "search picks the farthest\n");
        } else if (strcmp(buf, "radiusfar") == 0) {
            s_fx = FX_RADIUSFAR;
            log_write("tilequery: KAROO_SIM_FX=radiusfar -- radius search "
                      "picks the farthest\n");
        } else if (strcmp(buf, "farnear") == 0) {
            s_fx = FX_FARNEAR;
            log_write("tilequery: KAROO_SIM_FX=farnear -- farthest-tile "
                      "search picks the nearest\n");
        }
    }

    n = GetEnvironmentVariableA("KAROO_TILEQ_DIAG", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0)
        s_diag = 1;
}

/* ─── Game-base access, unaligned as in the other sim files ───────────── */
#include "game.h"
#include "bridgeobject.h"

#define GU8(o)    (*(unsigned char *)(B + (o)))
#define GI32(o)   (*(int  *)(B + (o)))
#define GPP(o)   (*(unsigned char **)(B + (o)))

/* Tile addressing: base + (v + u*100) * 0x7f, identical to entitymove.cpp's
 * and bomb.cpp's TILE(). */
#define TILEOFF(u, v)   ((((int)(v)) + ((int)(u)) * 100) * 0x7f)

#define T8(t, o)    (*(unsigned char *)((t) + (o)))

/* The CRT's __ftol 0x00451134: truncate toward zero into an __int64.  Only
 * the low byte is ever kept by either search, exactly as the originals'
 * `MOV <byte>,AL` keeps it. */
static inline unsigned char ftol_b(long double v)
{
    return (unsigned char)(long long)v;
}

/* The distance both searches compute: squares formed in INTEGER registers,
 * summed, FILD'd, FSQRT'd, and truncated straight off the 80-bit stack. */
static inline unsigned char tile_distance(int da, int db)
{
    return ftol_b(sqrtl((long double)(int)(da * da + db * db)));
}

/* ─── Diagnostics ─────────────────────────────────────────────────────── */
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

/* Unconditional once-each, the same choice bomb.cpp and slidinghazard.cpp
 * make and for the same reason: a flag-gated line cannot distinguish "this
 * query is never called here" from "the flag never arrived". */
static void diag_enter(int which, const void *self)
{
    if (!s_first[which]) {
        s_first[which] = 1;
        log_write("tilequery: first %s -- this=%p\n", k_names[which], self);
    }
    if (s_diag) {
        ++s_calls;
        if ((s_calls % 5000) == 0)
            log_write("tilequery: %lu calls\n", s_calls);
    }
}

static void diag_hit(int which, unsigned u, unsigned v)
{
    if (!s_success[which]) {
        s_success[which] = 1;
        log_write("tilequery: first %s HIT -- u=%u v=%u\n",
                  k_names[which], u, v);
    }
}

/* ──────────────────────────────────────────────────────────────────────────
 * MarkListedTilesBlockedByObject 0x0041a200 -- __thiscall, RET 4.
 *
 * Recomputes the per-tile "blocked" dword for one object list from that
 * list's object.  Point 3 (the inversion) and point 4 (the v-then-u-at-1
 * read order) both live here.
 * ────────────────────────────────────────────────────────────────────── */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_MarkListedTilesBlockedByObject(void *self, unsigned int listIndex)
{
    unsigned char *B = (unsigned char *)self;
    unsigned int   li = listIndex & 0xff;
    unsigned char *pair;
    int            flagged;
    unsigned int   value;
    int            i;

    fx_init();
    diag_enter(Q_MARK, self);

    /* Point 3: the flag is (object->+0x58 == 0) -- set when the field is
     * ZERO.  Read once, before the loop, exactly as the original does. */
    flagged = ((Game *)B)->bridgeSlot(li)->phase();
    value   = (unsigned int)(flagged == 0);

    if (s_fx == FX_BLOCKINVERT)
        value = !value;

    if (GU8(0x170543 + li) == 0)
        return;

    pair = B + li * 0x300 + 0x140544;
    i    = 0;
    do {
        /* Point 4: v is read AT the pointer and u one byte BELOW it, and
         * the v read happens first.  The pointer then advances by 3. */
        unsigned char v = pair[0];
        unsigned char u = pair[-1];

        pair += 3;
        ++i;

        *(int *)(B + 0x2ab7a4 + TILEOFF(u, v)) = (int)value;

        /* The bound is re-read from memory every iteration, as the
         * original's `CMP` against [+0x170543 + li] is. */
    } while (i < (int)(unsigned)GU8(0x170543 + li));
}

/* ──────────────────────────────────────────────────────────────────────────
 * FindNearestListedObjectTile 0x0041b680 -- __thiscall, RET 0xc.
 *
 * Walks every object list and picks the listed object whose tile is nearest
 * the tile passed in, among those whose blocked dword is non-zero.  Points
 * 1, 2, 7 and 11 all live here.
 * ────────────────────────────────────────────────────────────────────── */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_FindNearestListedObjectTile(void *self, unsigned char *pu,
                                unsigned char *pv, unsigned char maxDist)
{
    unsigned char *B = (unsigned char *)self;
    unsigned char  u0 = *pu;          /* saved inputs, restored on failure */
    unsigned char  v0 = *pv;
    unsigned char  best     = maxDist;
    unsigned char  bestU    = 0, bestV = 0;
    unsigned char  farDist  = 0;      /* KAROO_SIM_FX=listfar only */
    unsigned char  farU     = 0, farV = 0;
    int            haveFar  = 0;
    unsigned int   list;

    fx_init();
    diag_enter(Q_LIST, self);

    /* Point 1: the entry guard is `(unsigned)count + 1 > 0`, which cannot
     * fail.  Written as the original's shape rather than deleted. */
    if ((int)((unsigned int)GU8(0x48b12) + 1) > 0) {

        for (list = 0; ; ) {
            unsigned char inner = 0;

            if (GU8(0x170543 + list) != 0) {
                do {
                    int            idx = (int)(list * 0x100 + inner);
                    unsigned char  u, v;

                    u = GU8(0x140543 + idx * 3);
                    /* Point 7: both outputs are scribbled on every
                     * iteration, not only on a win. */
                    *pu = u;
                    v = GU8(0x140544 + idx * 3);
                    *pv = v;

                    if (*(int *)(B + 0x2ab7a4 + TILEOFF(u, v)) != 0) {
                        unsigned char d =
                            tile_distance((int)u0 - (int)u,
                                          (int)v0 - (int)v);

                        /* Point 11: unsigned and strict, and a zero
                         * distance is rejected outright. */
                        if (d != 0 && d < best) {
                            best  = d;
                            bestU = u;
                            bestV = v;
                        }
                        /* The same candidate set, kept the other way round,
                         * for the control below.  Never consulted unless
                         * KAROO_SIM_FX=listfar. */
                        if (d != 0 && d < maxDist &&
                            (!haveFar || d > farDist)) {
                            haveFar = 1;
                            farDist = d;
                            farU    = u;
                            farV    = v;
                        }
                    }

                    /* An 8-bit counter compared UNSIGNED against the count. */
                    ++inner;
                } while (inner < GU8(0x170543 + list));
            }

            /* Point 2: an 8-bit counter, widened for a SIGNED compare
             * against a bound that is the count PLUS ONE and is re-read
             * every iteration.  So the body runs count + 1 times. */
            list = (unsigned int)(unsigned char)(list + 1);
            if (!((int)list < (int)((unsigned int)GU8(0x48b12) + 1)))
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

/* ──────────────────────────────────────────────────────────────────────────
 * FindNearestFlaggedTileInRadius 0x0041b810 -- __thiscall, RET 0xc.
 *
 * Scans a square window about the tile passed in for the nearest tile whose
 * flag byte equals 1.  Points 5, 6 and 11 live here.  The outer loop is v
 * and the inner is u -- see the Band 0 correction above.
 * ────────────────────────────────────────────────────────────────────── */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_FindNearestFlaggedTileInRadius(void *self, unsigned char *pu,
                                   unsigned char *pv, unsigned char radius)
{
    unsigned char *B  = (unsigned char *)self;
    unsigned char  u0 = *pu;
    unsigned char  v0 = *pv;
    unsigned char  best    = radius;
    unsigned char  bestU   = 0, bestV = 0;
    unsigned char  farDist = 0;       /* KAROO_SIM_FX=radiusfar only */
    unsigned char  farU    = 0, farV = 0;
    int            haveFar = 0;
    int            v, vEnd, uBeg, uEnd;

    fx_init();
    diag_enter(Q_RADIUS, self);

    /* Point 6: the window is half-open on both axes -- radius tiles in the
     * negative direction, radius-1 in the positive. */
    v    = (int)v0 - (int)radius;
    vEnd = (int)v0 + (int)radius;

    if (v < vEnd) {
        uBeg = (int)u0 - (int)radius;
        uEnd = (int)u0 + (int)radius;

        do {
            if (uBeg < uEnd) {
                unsigned char  vExtent = GU8(0x2ab727);
                unsigned char *flag    = B + 0x2ab72c + TILEOFF(uBeg, v);
                int            u       = uBeg;

                do {
                    /* Point 5: strictly greater than zero on both axes, so
                     * row 0 and column 0 can never be selected. */
                    if (v < (int)(unsigned)vExtent && v > 0 &&
                        u < (int)(unsigned)GU8(0x2ab728) && u > 0 &&
                        *flag == 1) {
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

                    ++u;
                    /* One u step is one row of 100 tiles: 100 * 0x7f. */
                    flag += 0x319c;
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

/* ──────────────────────────────────────────────────────────────────────────
 * FindFarthestOccupiedTile 0x00412530 -- __thiscall, RET 8.
 *
 * Scans the whole grid for the tile FARTHEST from the tile passed in, among
 * tiles that are occupied (+0x19d != 0) and not marked (+0x1a5 == 0).
 * Points 8, 9 and 10 live here.  It is the only one of the four that keeps
 * a real distance rather than a truncated byte, and the only one that never
 * calls __ftol.
 * ────────────────────────────────────────────────────────────────────── */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_FindFarthestOccupiedTile(void *self, unsigned char *pu,
                             unsigned char *pv)
{
    unsigned char *B     = (unsigned char *)self;
    unsigned char *tiles = GPP(0x34);
    unsigned char  u0    = *pu;
    unsigned char  v0    = *pv;
    unsigned char  bestU = *pu;       /* seeded from the inputs, not zeroed */
    unsigned char  bestV = *pv;
    unsigned char  nearU = 0, nearV = 0;   /* KAROO_SIM_FX=farnear only */
    double         best  = 0.0;
    double         near_ = 0.0;
    int            haveNear = 0;
    unsigned char  v;

    fx_init();
    diag_enter(Q_FAR, self);

    if (T8(tiles, 0x19a) != 0) {
        v = 0;
        do {
            unsigned char u = 0;

            if (T8(tiles, 0x19b) != 0) {
                int ui = 0;

                do {
                    /* Outer index sits in the v slot, inner in the u slot. */
                    int off = TILEOFF(ui, (int)(signed char)v);

                    if (T8(tiles, off + 0x19d) != 0 &&
                        T8(tiles, off + 0x1a5) == 0) {
                        /* Point 9: the candidate is the full 80-bit FSQRT
                         * result and is COMPARED at that width... */
                        long double d = sqrtl((long double)(int)(
                            ((int)(signed char)v - (int)v0) *
                            ((int)(signed char)v - (int)v0) +
                            (ui - (int)u0) * (ui - (int)u0)));

                        if ((long double)best < d) {
                            bestV = v;
                            bestU = u;
                            /* ...and only then ROUNDED to double on store. */
                            best  = (double)d;
                        }
                        if (!haveNear || d < (long double)near_) {
                            haveNear = 1;
                            near_    = (double)d;
                            nearV    = v;
                            nearU    = u;
                        }
                    }

                    /* Point 8: an 8-bit index read back SIGN-extended. */
                    ++u;
                    ui = (int)(signed char)u;
                } while (ui < (int)(unsigned)T8(tiles, 0x19b));
            }
            ++v;
        } while ((int)(signed char)v < (int)(unsigned)T8(tiles, 0x19a));

        /* Point 10: strict, and unordered-false -- a NaN best returns 0. */
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

/* ──────────────────────────────────────────────────────────────────────────
 * The coverage census -- KAROO_TILEQ_DIAG=1 only.
 *
 * All four of the above turned out to be reached by NOTHING the project can
 * currently run: every one of the fourteen recordings is blind to them, and
 * so is the 80-level report.  All four negative controls returned 14/14,
 * which is not weak coverage but ZERO coverage.
 *
 * Reading the six call sites says why.  Three of them dispatch on the
 * object's +0x62 byte -- == 2 for the listed-object search, == 3 for the
 * radius search, == 5 for the farthest search -- so a level with no object
 * of that type cannot exercise the corresponding query however long it is
 * played.
 *
 * This census answers the only question that gets us out of that: WHICH
 * LEVELS carry those types.  It walks the same object-pointer array the
 * queries themselves use (Game+0x170643, count at Game+0x48b12) and logs
 * each distinct +0x62 value once, naming every level that carries a 2, 3 or
 * 5.  It is called from Score_CalculateLevelScore, which is already ours
 * and which `tools/levelreport.py` drives once per level for all 80 levels
 * in a single 17-second pass -- the same instrument, and the same trick,
 * that chose `freeze03` and `enemyfactory` when the `type > 0x64` branch had
 * this same problem.
 *
 * It reads only; it writes nothing and changes no game state.
 * ────────────────────────────────────────────────────────────────────── */
extern "C" void tilequery_census_object_types(void *self)
{
    unsigned char *B = (unsigned char *)self;
    static int seen[256];
    static int announced = 0;
    static unsigned int level = 0;
    unsigned int count, i;
    int has2 = 0, has3 = 0, has5 = 0;

    fx_init();
    if (!s_diag)
        return;

    if (!announced) {
        announced = 1;
        log_write("tilequery: census of FOE +0x62 types begins "
                  "(2 = listed-object search, 3 = radius search, "
                  "5 = farthest search)\n");
    }

    ++level;

    /* The FOE table, exactly as worldstate.cpp enumerates it: pointer array
     * at Game+0x174804 indexed by the live-id list at Game+0x174fd5, with
     * the count byte at Game+0x174fd4.  This is the same table, and the
     * same +0x62 field, that GameTick's type dispatch reads.
     *
     * An earlier version of this census walked Game+0x170643 -- the object
     * LISTS -- instead.  That is a different array whose entries have no
     * +0x62 type field, so the values it reported (221, 131, 69, 183, 240
     * and so on) were garbage, and the six levels it named were wrong.
     * Recorded here because the mistake is an easy one to repeat: the
     * object-list array is what MarkListedTilesBlockedByObject uses, and it
     * sits only a few hundred bytes away from the foe table in Game. */
    count = (unsigned int)GU8(0x174fd4);
    for (i = 0; i < count; ++i) {
        unsigned char  id  = GU8(0x174fd5 + i);
        unsigned char *foe = GPP(0x174804 + (unsigned int)id * 4);
        unsigned char  t;

        if (foe == 0)
            continue;
        t = foe[0x62];

        if (!seen[t]) {
            seen[t] = 1;
            log_write("tilequery: CENSUS new foe type +0x62 = %u\n",
                      (unsigned)t);
        }
        if (t == 2) has2 = 1;
        if (t == 3) has3 = 1;
        if (t == 5) has5 = 1;
    }

    if (has2 || has3 || has5) {
        log_write("tilequery: CENSUS level #%u carries GATED foe type%s%s%s "
                  "-- capture a recording here\n", level,
                  has2 ? " 2" : "", has3 ? " 3" : "", has5 ? " 5" : "");
    }
}
