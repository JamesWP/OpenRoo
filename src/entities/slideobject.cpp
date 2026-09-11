/* SlideObject -- a block that slides along its track and back: spawn, tick
 * and purge.
 *
 *     Game::SpawnSlideObject        0x00417e20   (was objectplace.cpp)
 *     Game::PurgeSlideObjects       0x004181b0   (was gamereset.cpp)
 *     SlideObject::UpdateSlideObject 0x0043ae00  (was pushedblock.cpp)
 *
 *     SlideObject ctor / dtor       0x0043adb0 / 0x0043add0 + 0x0043adf0
 *
 * ─── One class, two old names ────────────────────────────────────────────
 *
 * GAMETICK_PLAN.md took the tick as "UpdatePushedBlockObject", "the pushable
 * block / moving crate".  The game has no pushable blocks.  The tick is
 * called on every entry of the slot array Game+0x173588 (count +0x173718),
 * which only SpawnSlideObject fills, and it reads exactly the fields that
 * spawn writes: sound +0x39, origin +0x3d/+0x3e, limit +0x40, tile base
 * +0x43, kind +0x47 -- and 0x0a in +0x47 means "along U" to both.  So the
 * "pushed block" is the slide, and Ghidra now names the tick
 * UpdateSlideObject.  The control and diag names `blockstay` /
 * KAROO_BLOCK_DIAG are kept, so that the commands recorded in CLAUDE.md,
 * GAMETICK_PLAN.md and old commit messages still do what they say.
 *
 * Every function that reads or writes a SlideObject field lives here;
 * slideobject.h keeps the fields private.  Outside readers, both original
 * code: the renderer FUN_00408920 (+0x25/+0x29/+0x2d and +0x47) and the
 * sound attach in levelsounds.cpp (writes +0x39 by raw offset).
 *
 * ─── The tick ────────────────────────────────────────────────────────────
 *
 * A three-state machine like the lift (0 parked, 1 advancing, 2 retreating,
 * at +0x4c) travelling along one grid axis (+0x47: 0x0a means u, anything
 * else v).  It shares the lift's rate and dwell constants exactly --
 * `_DAT_0045d384` (0.005) and `_DAT_0045d2e0` (1500.0).
 *
 *   1 ADVANCING   coord = origin(+0x3d u / +0x3e v) + elapsed * 0.005
 *                 Each time the integer coordinate passes the old one it
 *                 CLEARS THE VACATED TILE: marker (+0x19d) and +0x1a5 both
 *                 zeroed at the cell the slide just left.
 *                 Ends when the coordinate reaches the limit (+0x40): snap,
 *                 park, latch +0x48 = 1, halt the sound.
 *   2 RETREATING  coord = limit(+0x40) - elapsed * 0.005, mirrored, clearing
 *                 the vacated tile the same way, ending back at the origin
 *                 with +0x48 = 0.
 *   0 PARKED      publishes the phase start to tile+0x1ac and the dwell to
 *                 tile+0x1b4; after 1500 ms it clears the current tile's
 *                 marker and departs in the direction of the latch:
 *                 state = (+0x48 != 0) + 1.
 *
 * Then, UNCONDITIONALLY, every tick:
 *
 *   tile(cur)+0x19d = 0x0c            the slide's marker, re-stamped
 *   tile(cur)+0x19c = +0x42           its height
 *   +0x29 (float y) = (float)+0x33
 *   tile(ORIGIN)+0x1c3/+0x1c4         current u, v
 *   tile(ORIGIN)+0x1c6/+0x1ca/+0x1ce  current u, y, v as floats
 *
 * That last group is indexed by the ORIGIN cell (+0x3d, +0x3e), not the
 * current one: the home tile carries the live coordinates.  A slide whose
 * origin is outside the grid writes out of bounds; that is the original's
 * behaviour and is reproduced rather than guarded.
 *
 * THE DWELL IS ONE DOUBLE AT tile+0x1b4.  The parked branch stores 0 to
 * tile+0x1b4 and 0x40977000 to +0x1b8 (0043B1FE / 0043B205).  The old notes
 * read +0x1b8 alone as "1500.0f"; together they are the double
 * 0x4097700000000000 = 1500.0 -- the lift's tile+0x1e5 misreading exactly.
 *
 * ─── Point 1: EIGHT `__ftol` calls, and they are NOT interchangeable ─────
 *
 * The arguments arrive on the x87 stack and `decompile_function` renders all
 * eight as a bare `__ftol()`.  Read from the listing, they pair up two per
 * branch, and THE TWO IN A PAIR TAKE DIFFERENT VALUES:
 *
 * | Site | Branch | Argument |
 * |---|---|---|
 * | `0043AE52` | advance u | the 80-bit `ST0` (`FST`, no pop) |
 * | `0043AEA4` | advance u | the ROUNDED float32 at `+0x25` (`FLD`) |
 * | `0043AF13` | advance v | the 80-bit `ST0` |
 * | `0043AF65` | advance v | the ROUNDED float32 at `+0x2d` |
 * | `0043B02B` | retreat u | the 80-bit `ST0` |
 * | `0043B07D` | retreat u | the ROUNDED float32 at `+0x25` |
 * | `0043B0DD` | retreat v | the 80-bit `ST0` |
 * | `0043B12F` | retreat v | the ROUNDED float32 at `+0x2d` |
 *
 * The FIRST of each pair decides whether the slide VACATED a tile; the
 * SECOND becomes its new cell index.  Where float32 rounding crosses an
 * integer the two disagree, so both forms are transcribed exactly.
 *
 * ─── Point 2: `+0x40` is read UNSIGNED to compare and SIGNED to snap ─────
 *
 *   0043AEB1  AND   EDX, 0xff            <- ZERO-extend: the COMPARE
 *   0043AECD  MOVSX EAX, CL              <- SIGN-extend: the SNAP
 *
 * The retreat branch starts from the same byte ZERO-extended while its
 * origin `+0x3d`/`+0x3e` is sign-extended.  Transcribed as written.
 *
 * ─── Other exactness points preserved ────────────────────────────────────
 *
 * 3. THE STATE TESTS ARE SEQUENTIAL: `+0x4c` is re-read before each test, so
 *    an advance that COMPLETES this tick runs the PARKED block in the same
 *    tick.  Three separate `if`s.
 *
 * 4. THE COMPLETION TESTS HAVE OPPOSITE NaN POLARITIES.  Advance is
 *    `FCOMP; TEST AH,0x41; JZ notdone` -- unordered COMPLETES, written
 *    `!(limit > pos)`.  Retreat is `FCOM; TEST AH,1; JNZ notdone` --
 *    unordered does NOT complete, written `pos <= origin`.
 *
 * 5. THE VACATED-TILE TESTS ARE SIGNED BYTE COMPARES (`CMP AL,CL` with
 *    `JLE`/`JGE`), against the cell index BEFORE it is updated.
 *
 * 6. THE PARKED DEPART CLEARS THE TILE MARKER FIRST, then updates the
 *    timestamp and the state.  It calls TriggerPlayback but -- unlike the
 *    lift -- NOT Set3DPosition.  That asymmetry is the original's.
 *
 * 7. `+0x4d` / `+0x51` ARE ONE DOUBLE (two dword moves): the phase start.
 *
 * ─── Floating-point copies ───────────────────────────────────────────────
 *
 * Doubles and floats are copied by plain assignment; the original copies
 * several with integer MOVs (phaseStart_ <- now_, every tile publish).  The
 * two differ only for a signalling NaN, which no clock, timestamp or
 * position holds.  Accepted deliberately (COHESION_PLAN.md, template 3).
 *
 * ─── Controls and diags ──────────────────────────────────────────────────
 *
 * KAROO_SIM_FX=blockstay suppresses the vacated-tile clear -- the slide
 * advances but never releases the tile behind it, leaving a trail of solid
 * cells.  KAROO_SIM_FX=slideaxis exchanges the spawn's two track kinds (see
 * spawn).  KAROO_SIM_FX=keepobjects makes the purge do nothing, shared with
 * the other purges.
 *
 * KAROO_BLOCK_DIAG=1 logs the first tick, vacate, completed advance,
 * completed retreat and depart, plus a tick count every 5000.
 * KAROO_PLACE_DIAG=1 logs every spawn with its scan kind; KAROO_RESET_DIAG=1
 * the first purge and every live one.
 */

#include <windows.h>
#include <stddef.h>
#include <new>               /* std::nothrow */
#include <string.h>          /* strcmp */

#include "slideobject.h"
#include "game.h"
#include "tile.h"
#include "soundmanager.h"
#include "static.h"
#include "log.h"

/* Read from .rdata: 0x3ba3d70a and 0x4097700000000000. */
static const float  K_MS_TO_TILE = 0.005f;    /* 0x0045d384 */
static const double K_PARK_DWELL = 1500.0;    /* 0x0045d2e0 */


/* ─── Controls and diags, read by VALUE, never by presence ──────────────── */
static int s_fx_blockstay   = 0;
static int s_fx_slideaxis   = 0;
static int s_fx_keepobjects = 0;
static int s_diag_block     = 0;   /* KAROO_BLOCK_DIAG */
static int s_diag_place     = 0;   /* KAROO_PLACE_DIAG */
static int s_diag_reset     = 0;   /* KAROO_RESET_DIAG */
static int s_init           = 0;

static int env_set(const char *name, char *buf, DWORD cb)
{
    DWORD n = GetEnvironmentVariableA(name, buf, cb);
    return n > 0 && n < cb;
}

static void fx_init(void)
{
    char buf[64];

    if (s_init)
        return;
    s_init = 1;

    if (env_set("KAROO_SIM_FX", buf, sizeof(buf))) {
        if (strcmp(buf, "blockstay") == 0) {
            s_fx_blockstay = 1;
            log_write("slideobject: KAROO_SIM_FX=blockstay -- vacated tiles "
                      "are never released\n");
        } else if (strcmp(buf, "slideaxis") == 0) {
            s_fx_slideaxis = 1;
            log_write("slideobject: KAROO_SIM_FX=slideaxis -- the two "
                      "slide-track kind codes are exchanged, so a track meant "
                      "to run along U is scanned along V and vice versa; scan, "
                      "stamping and recorded span all move together\n");
        } else if (strcmp(buf, "keepobjects") == 0) {
            s_fx_keepobjects = 1;
            log_write("slideobject: KAROO_SIM_FX=keepobjects -- slide purge "
                      "does nothing\n");
        }
    }

    if (env_set("KAROO_BLOCK_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_block = 1;
    if (env_set("KAROO_PLACE_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_place = 1;
    if (env_set("KAROO_RESET_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_reset = 1;
}

/* ═══ Construction and destruction ═════════════════════════════════════
 *
 * 0x43adb0 constructs in two layers: the shared level-object base
 * constructor 0x401000 (installs the base vtable 0x45d290, zeroes +0x25,
 * +0x29, +0x2d), then the slide's own (installs 0x45d6f0, zeroes the sound
 * handle +0x39).  Every other byte is left as operator new returned it.
 *
 * 0x43add0 (vtable slot 0; the vtable 0x45d6f0 has ONE slot -- 0x45d6f4
 * belongs to another class, referenced from 0x43b394) calls 0x43adf0, which
 * re-installs 0x45d6f0 and runs the base destructor 0x401060 (installs
 * 0x45d290); then Free2 if flags & 1.  Both vtable stores are dead -- the
 * only caller passes flags 1 -- and are not reproduced.
 *
 * A byte scan of .text for 0x45d6f0 finds only the ctor (0043ADBA) and the
 * dtor (0043ADF2); the ctor's one caller is the spawn, now ours; the dtor is
 * reached only through the vtable.  So we both create and destroy every
 * slide, and use our own new/delete.
 */
const SlideObject::Vtbl SlideObject::VTABLE = { &SlideObject::scalarDeletingDtor };

SlideObject *SlideObject::create()
{
    return new (std::nothrow) SlideObject;
}

SlideObject::SlideObject()
{
    /* base constructor 0x401000 */
    posU_ = 0.0f;
    posY_ = 0.0f;
    posV_ = 0.0f;
    /* slide constructor 0x43adb0 */
    vtable_ = &VTABLE;
    sound_  = 0;
}

void *SlideObject::scalarDeletingDtor(SlideObject *self, unsigned int flags)
{
    if (flags & 1)
        delete self;
    return self;
}

void SlideObject::destroy()
{
    vtable_->scalarDeletingDtor(this, 1);
}

/* ═══ 0x00417e20 -- Game::SpawnSlideObject ═════════════════════════════════
 *
 * __thiscall on Game, four dword stack arguments (RET 0x10): u, v, height,
 * kind.  ONE E8 call site, 0x00416AE4, inside SetupLevelObjects -- which
 * calls it only for tile types 0x0a and 0x0b.
 *
 * `kind` selects one of two SCANS that walk the grid from the spawn cell
 * until they meet a tile whose marker byte (tile+0x19d) is non-zero,
 * stamping every cell on the way.  The object's +0x38 is the SPAN between
 * where the scan started (+0x41) and the last cell it scanned (+0x40).
 *
 *   kind == 0x0a   walks U (the row index), idx = v + var*100
 *   kind == 0x0b   walks V (the column),    idx = var + u*100
 *   anything else  no scan at all
 *
 * ─── The asymmetry between the two scans, from the LISTING ───────────────
 *
 *   The marker clear at 0x00417f3f happens BEFORE the kind dispatch, so it
 *   fires for every kind.  It clears the SPAWN cell's marker only.
 *
 *   The kind == 0x0b loop clears the marker AGAIN, at 0x00417ff1, from
 *   INSIDE the loop body, so it wipes the marker of every cell it steps onto.
 *
 *   The kind == 0x0a loop does NOT.  Its body (0x00417f52) has no such store.
 *
 * So the V scan erases the markers along its track and the U scan leaves
 * them standing.  Both are transcribed as written.
 *
 * ─── Other details from the LISTING ──────────────────────────────────────
 *
 * 1. FOUR tile fields are cleared BEFORE the allocation (0x00417e71 ..
 *    0x00417e89): +0x19c (height) byte, +0x1bc dword, +0x1f2 byte, +0x202
 *    byte, all on the spawn cell.
 *
 * 2. `+0x2d` is NOT negated here, where SpawnBreakableObject's is.
 *
 * 3. WHEN `kind` IS NEITHER 0x0a NOR 0x0b, `+0x40` AND `+0x41` ARE NEVER
 *    WRITTEN, and `+0x38 = +0x40 - +0x41` subtracts two uninitialised bytes.
 *    Preserved (left uninitialised).  Unreachable from the one caller, which
 *    only passes 0x0a / 0x0b -- which is also why allocating with our own
 *    `new` rather than the game's heap cannot change what it reads.
 *
 * 4. The scan loops are `do { ... } while (marker == 0)`, so the body always
 *    runs at least once.  `+0x40` is the last cell SCANNED, `+0x41` the cell
 *    the scan STARTED from.  The scans are UNBOUNDED -- see CLAUDE.md, "A
 *    control's blast radius".
 *
 * 5. Every store through the new object re-reads the slot it was just
 *    written to, so a failed allocation faults on the first field store,
 *    here as there.
 *
 * ─── Negative control: `slideaxis` ───────────────────────────────────────
 *
 * Exchanges the two kind codes, so a track meant to run along U is scanned
 * along V and vice versa -- a DIRECTION change.  It gives **15/16** -- only
 * `thrower02` fails -- and `levelreport.py` PASSES.
 *
 * That 1-of-16 is NOT a ceiling.  `KAROO_PLACE_DIAG=1` logs every slide spawn
 * with its kind, and the census across six recordings was:
 *
 *     thrower02          3 slides    3 SCAN-U              FAILS
 *     castle-something   3 slides    2 SCAN-U, 1 SCAN-V    passes
 *     sandra02           1 slide     1 SCAN-U              passes
 *     water01           13 slides    7 SCAN-U, 6 SCAN-V    passes
 *     bridge01           0 slides                          passes
 *     freeze03           0 slides                          passes
 *
 * A track's axis only reaches an asserted field if the PLAYER RIDES that
 * slide; `thrower02` is the one recording that does.  EXERCISED, and
 * CONTROLLED ONLY WEAKLY -- closing it needs a recording that rides a slide
 * on `water01`'s level.  `levelreport.py` is blind to it: the scan clears
 * markers rather than setting them, which does not change how many objects a
 * level builds.
 */
static int s_logged_spawn = 0;
static int s_logged_oom   = 0;

void SlideObject::spawn(Game *game, unsigned int uArg, unsigned int vArg,
                        unsigned int heightArg, unsigned int kindArg)
{
    unsigned int u, v, height, kind;
    unsigned char n, var, last;
    unsigned char *base;
    SlideObject *obj;
    Tile *tile;

    fx_init();

    u      = uArg & 0xff;
    v      = vArg & 0xff;
    height = heightArg & 0xff;
    kind   = kindArg & 0xff;

    /* The single point the two track axes are distinguished. */
    if (s_fx_slideaxis) {
        if (kind == 0x0a)
            kind = 0x0b;
        else if (kind == 0x0b)
            kind = 0x0a;
    }

    base = game->tileBase();
    tile = Tile::at(base, (int)u, (int)v);

    /* Detail 1: four tile fields cleared BEFORE the allocation. */
    tile->setHeight(0);
    tile->setSlideTrack(0);
    tile->setField1f2(0);
    tile->setField202(0);

    obj = create();
    if (obj == 0 && s_diag_place && !s_logged_oom) {
        s_logged_oom = 1;
        log_write("slideobject: ALLOCATION FAILED in spawn -- the original "
                  "would store through the slot, which now holds NULL\n");
    }

    n = game->slideCount();
    game->setSlideSlot(n, obj);

    /* Every spawn is logged, not just the first -- see `slideaxis` above. */
    if (s_diag_place) {
        s_logged_spawn++;
        log_write("slideobject: slide spawn #%d -- slot=%u u=%u v=%u "
                  "height=%u kind=0x%x %s\n",
                  s_logged_spawn, (unsigned)n, u, v, height, kind,
                  (kind == 0x0a) ? "SCAN-U" :
                  (kind == 0x0b) ? "SCAN-V" : "no-scan");
    }

    obj->clock_    = game->clock();
    obj->record_   = game->field_170a5c();
    obj->kind_     = (signed char)kind;
    obj->state_    = 1;
    obj->tileBase_ = base;

    /* Fires for EVERY kind, including the ones that never scan. */
    tile->setObjectMarker(0);

    if (kind == 0x0a) {
        /* Walk U.  Does NOT clear the marker as it goes. */
        var = (unsigned char)u;
        for (;;) {
            tile->setSlideTrack(1);
            tile->setSlideHeight((unsigned char)height);
            tile->setSlideSlot(n);
            tile->setSlideOrigin((unsigned char)u, (unsigned char)v);

            last = var;
            var  = (unsigned char)(var + 1);
            tile = Tile::at(base, (int)var, (int)v);
            if (tile->objectMarker() != 0)
                break;
        }
        obj->trackStart_ = (unsigned char)u;
        obj->limit_      = last;
    } else if (kind == 0x0b) {
        /* Walk V.  DOES clear the marker of every cell it steps onto. */
        var = (unsigned char)v;
        for (;;) {
            tile->setObjectMarker(0);
            tile->setSlideTrack(1);
            tile->setSlideHeight((unsigned char)height);
            tile->setSlideSlot(n);
            tile->setSlideOrigin((unsigned char)u, (unsigned char)v);

            last = var;
            var  = (unsigned char)(var + 1);
            tile = Tile::at(base, (int)u, (int)var);
            if (tile->objectMarker() != 0)
                break;
        }
        obj->trackStart_ = (unsigned char)v;
        obj->limit_      = last;
    }

    obj->cellU_      = (signed char)u;
    obj->cellV_      = (signed char)v;
    obj->heightCell_ = (signed char)height;

    obj->posU_ = (float)(int)u;
    obj->posY_ = (float)(int)height;
    obj->posV_ = (float)(int)v;               /* NOT negated -- cf. breakable */

    obj->originU_      = (signed char)u;
    obj->originV_      = (signed char)v;
    obj->originHeight_ = (signed char)height;
    obj->tileHeight_   = (unsigned char)height;

    /* Two zero dwords: +0.0. */
    obj->phaseStart_ = 0.0;

    /* Detail 3: on the no-scan path both operands are uninitialised. */
    obj->span_  = (unsigned char)(obj->limit_ - obj->trackStart_);
    obj->sound_ = 0;

    game->setSlideCount((unsigned char)(n + 1));
}

/* ═══ 0x004181b0 -- Game::PurgeSlideObjects ════════════════════════════════
 *
 * The count is RE-READ every iteration and the index is a byte (AL, `JC`).
 * The sound handle is read off the raw slot with no null test on the object;
 * the destructor call does test it.  The trailing count store is redundant
 * when the count was already 0, and is preserved.
 */
static int s_logged_purge = 0;
static unsigned s_live_purges = 0;

void SlideObject::purgeAll(Game *game)
{
    unsigned char i;

    fx_init();

    if (s_fx_keepobjects)
        return;   /* objects AND count survive -- see gamereset.cpp */

    if (s_diag_reset && !s_logged_purge) {
        s_logged_purge = 1;
        log_write("slideobject: first purge -- count=%u\n",
                  (unsigned)game->slideCount());
    }

    i = 0;
    if (game->slideCount() != 0) {
        if (s_diag_reset)
            log_write("slideobject: LIVE purge #%u -- count=%u\n",
                      ++s_live_purges, (unsigned)game->slideCount());
        do {
            if (game->soundCreated() != 0) {
                CStaticSoundbuffer *h = game->slideSlot(i)->sound_;
                if (h != 0)
                    game->soundManager()->releaseStaticForOwner(h, 1);
            }
            SlideObject *obj = game->slideSlot(i);
            if (obj != 0)
                obj->destroy();
            i++;
        } while (i < game->slideCount());
    }
    game->setSlideCount(0);
}

/* ═══ 0x0043ae00 -- UpdateSlideObject ═════════════════════════════════════ */

/* The CRT's __ftol 0x00451134: truncate toward zero; only the low byte is
 * kept, as the original's `MOV byte ptr [..],AL`. */
static inline signed char ftol_c(long double v)
{
    return (signed char)(long long)v;
}

static unsigned long s_ticks = 0;
static int s_logged_first   = 0;
static int s_logged_vacate  = 0;
static int s_logged_advance = 0;
static int s_logged_retreat = 0;
static int s_logged_depart  = 0;

/* Release the cell the slide is leaving -- the two stores the listing makes
 * at each of the four vacate sites, both at (+0x31, +0x32) BEFORE it is
 * updated.  KAROO_SIM_FX=blockstay skips them. */
void SlideObject::vacate()
{
    if (s_fx_blockstay)
        return;
    if (s_diag_block && !s_logged_vacate) {
        s_logged_vacate = 1;
        log_write("slideobject: first vacate -- cell=(%d,%d)\n",
                  (int)cellU_, (int)cellV_);
    }
    Tile::at(tileBase_, cellU_, cellV_)->setObjectMarker(0);
    Tile::at(tileBase_, cellU_, cellV_)->setField1a5(0);
}

void SlideObject::tick()
{
    Tile *t;

    fx_init();

    /* Unconditional once-per-run line: silence from a flag-gated line is
     * ambiguous between "no slides" and "the flag never arrived". */
    if (!s_logged_first) {
        s_logged_first = 1;
        log_write("slideobject: first slide tick -- this=%p\n", (void *)this);
    }
    if (s_diag_block) {
        ++s_ticks;
        if ((s_ticks % 5000) == 0)
            log_write("slideobject: %lu ticks\n", s_ticks);
    }

    recordCopy_ = *record_;
    now_ = *clock_;

    /* ─── State 1: ADVANCING ────────────────────────────────────────── */
    if (state_ == 1) {
        /* The travel term is computed BEFORE the axis branch, as in the
         * original (0043AE30..0043AE37). */
        long double travel = ((long double)now_ - (long double)phaseStart_)
                             * (long double)K_MS_TO_TILE;
        int done = 0;

        if (kind_ == 0x0a) {
            long double pos = travel + (long double)(int)originU_;
            posU_ = (float)pos;                   /* FST: keeps 80-bit */

            /* Point 1: the FIRST ftol takes the 80-bit value. */
            if (ftol_c(pos) > cellU_)
                vacate();

            /* Point 1: the SECOND ftol takes the ROUNDED float32. */
            cellU_ = ftol_c((long double)posU_);

            /* Point 2: the compare is UNSIGNED.  Point 4: unordered-true. */
            if (!((long double)(int)limit_ > (long double)posU_)) {
                /* Point 2: the snap is SIGNED. */
                cellU_ = (signed char)limit_;
                phaseStart_ = now_;
                posU_ = (float)(int)(signed char)limit_;
                state_ = 0;
                atLimit_ = 1;
                done = 1;
            }
        } else {
            long double pos = travel + (long double)(int)originV_;
            posV_ = (float)pos;

            if (ftol_c(pos) > cellV_)
                vacate();

            cellV_ = ftol_c((long double)posV_);

            if (!((long double)(int)limit_ > (long double)posV_)) {
                cellV_ = (signed char)limit_;
                phaseStart_ = now_;
                posV_ = (float)(int)(signed char)limit_;
                state_ = 0;
                atLimit_ = 1;
                done = 1;
            }
        }

        if (done) {
            if (sound_ != 0)
                CStatic_HaltPlayback(sound_);

            if (s_diag_block && !s_logged_advance) {
                s_logged_advance = 1;
                log_write("slideobject: first completed advance -- "
                          "cell=(%d,%d) limit=%u\n",
                          (int)cellU_, (int)cellV_, (unsigned)limit_);
            }
        }

        if (sound_ != 0)
            CStatic_Set3DPosition(sound_, (float)(int)cellU_, posY_,
                                  -(float)(int)cellV_, 1);
    }

    /* ─── State 2: RETREATING.  Point 3: a separate `if`. ───────────── */
    if (state_ == 2) {
        int done = 0;

        if (kind_ == 0x0a) {
            /* Point 2: the start point is the limit ZERO-extended. */
            long double pos = (long double)(int)limit_
                              - ((long double)now_ - (long double)phaseStart_)
                                * (long double)K_MS_TO_TILE;
            posU_ = (float)pos;

            if (ftol_c(pos) < cellU_)
                vacate();

            cellU_ = ftol_c((long double)posU_);

            /* Point 4: FCOM + TEST AH,1 / JNZ -- unordered does NOT
             * complete, so a plain `<=`. */
            if ((long double)posU_ <= (long double)(int)originU_) {
                cellU_ = originU_;
                phaseStart_ = now_;
                posU_ = (float)(int)originU_;
                state_ = 0;
                done = 1;
            }
        } else {
            long double pos = (long double)(int)limit_
                              - ((long double)now_ - (long double)phaseStart_)
                                * (long double)K_MS_TO_TILE;
            posV_ = (float)pos;

            if (ftol_c(pos) < cellV_)
                vacate();

            cellV_ = ftol_c((long double)posV_);

            if ((long double)posV_ <= (long double)(int)originV_) {
                cellV_ = originV_;
                state_ = 0;
                posV_ = (float)(int)originV_;
                phaseStart_ = now_;
                done = 1;
            }
        }

        if (done) {
            /* LAB_0043b164: the latch clear is shared by both axes. */
            atLimit_ = 0;
            if (sound_ != 0)
                CStatic_HaltPlayback(sound_);

            if (s_diag_block && !s_logged_retreat) {
                s_logged_retreat = 1;
                log_write("slideobject: first completed retreat -- "
                          "cell=(%d,%d)\n", (int)cellU_, (int)cellV_);
            }
        }

        if (sound_ != 0)
            CStatic_Set3DPosition(sound_, (float)(int)cellU_, posY_,
                                  -(float)(int)cellV_, 1);
    }

    /* ─── State 0: PARKED ───────────────────────────────────────────── */
    if (state_ == 0) {
        t = Tile::at(tileBase_, cellU_, cellV_);
        t->setSlideParkedSince(phaseStart_);
        t->setSlideDwell(K_PARK_DWELL);

        /* Point 4: unordered-false, so a NaN does not depart. */
        if (now_ - phaseStart_ >= K_PARK_DWELL) {
            /* Point 6: the tile marker is cleared FIRST. */
            Tile::at(tileBase_, cellU_, cellV_)->setObjectMarker(0);

            phaseStart_ = now_;
            state_ = (signed char)((atLimit_ != 0) + 1);

            if (s_diag_block && !s_logged_depart) {
                s_logged_depart = 1;
                log_write("slideobject: first depart -- cell=(%d,%d) "
                          "latch=%d state=%d\n",
                          (int)cellU_, (int)cellV_, atLimit_, (int)state_);
            }

            /* Point 6: TriggerPlayback, but NO Set3DPosition. */
            if (sound_ != 0)
                CStatic_TriggerPlayback(sound_, 1);
        }
    }

    /* ─── The unconditional tail ────────────────────────────────────── */
    t = Tile::at(tileBase_, cellU_, cellV_);
    t->setObjectMarker(0x0c);
    t->setHeight(tileHeight_);

    /* Written BEFORE the origin-tile copy below reads it. */
    posY_ = (float)(int)heightCell_;

    /* Indexed by the ORIGIN cell, not the current one. */
    t = Tile::at(tileBase_, originU_, originV_);
    t->setSlideCell((unsigned char)cellU_, (unsigned char)cellV_);
    t->setSlidePos(posU_, posY_, posV_);
}

/* ═══ Exports -- thin ABI shims; patch.py routes the three originals here ═
 * The tick keeps its old export name so patch.py does not change. */

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_UpdatePushedBlockObject(SlideObject *self)
{
    self->tick();
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_SpawnSlideObject(Game *self, unsigned int uArg, unsigned int vArg,
                     unsigned int heightArg, unsigned int kindArg)
{
    SlideObject::spawn(self, uArg, vArg, heightArg, kindArg);
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PurgeSlideObjects(Game *self)
{
    SlideObject::purgeAll(self);
}
