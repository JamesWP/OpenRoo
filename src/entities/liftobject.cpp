/* LiftObject -- the rising/falling platform: spawn, tick and purge.
 *
 *     Game::SpawnLiftObject          0x00417b90   (was objectplace.cpp)
 *     Game::PurgeLiftObjects         0x00417d90   (was gamereset.cpp)
 *     LiftObject::UpdateVerticalLiftObject 0x00411cb0  GAMETICK_PLAN.md Band A
 *
 *     LiftObject ctor / dtor         0x00411c50 / 0x00411c80 + 0x00411ca0
 *
 * Every function that reads or writes a LiftObject field lives in this
 * file; the class (liftobject.h) keeps them private so nothing else can.
 * The one exception is the original renderer, FUN_00408870, which still
 * reads the three position floats -- see liftobject.h.
 *
 * ─── The tick ────────────────────────────────────────────────────────────
 *
 * A three-state machine on the byte at +0x43:
 *
 *   1 RISING    height = base(+0x38) + elapsed * 0.005
 *               completes when height >= top (+0x39); snaps to the TILE's
 *               stored top (tile+0x1d4), latches direction +0x3f = 1, parks.
 *   2 FALLING   height = top(+0x39) - elapsed * 0.005
 *               completes when the truncated height <= base-1; snaps to the
 *               tile's stored bottom (tile+0x1d3), clears +0x3f, parks.
 *   0 PARKED    publishes the dwell timestamps into the tile, and once 1500 ms
 *               have passed departs in the latched direction:
 *               state = (+0x3f != 0) + 1, i.e. 1 when parked at the bottom
 *               and 2 when parked at the top.
 *
 * `elapsed` is `now (+0x04) - phase start (+0x44)` in ms.  The constants,
 * read out of .rdata this session:
 *
 *   _DAT_0045d384 = 0x3ba3d70a = the float nearest 0.005 -- the ms-to-tile
 *                   rate, HALF the sliding hazard's 0.01, so a lift covers
 *                   one height unit per 200 ms.
 *   _DAT_0045d2e0 = 0x4097700000000000 = 1500.0 (double) -- the park dwell.
 *
 * The parked branch also stores 0 to tile+0x1e5 and 0x40977000 to +0x1e9.
 * Those are the low and high dwords of ONE little-endian double,
 * 0x4097700000000000 = 1500.0 -- the park dwell again, published to the tile
 * as a double at +0x1e5 (foechase.cpp reads it back as one).  An earlier
 * reading took +0x1e9 alone as the float "1500.0f"; 0x40977000 as a float is
 * about 4.73, and the zeroed +0x1e5 is its low half.
 *
 * The tile it publishes to, at (u,v) = (+0x31, +0x32):
 *
 *   tile+0x19c  u8      height byte, from +0x33 -- every tick, both states
 *   tile+0x1a6  float   live height, the raw bits of +0x29 -- every tick
 *   tile+0x1d3  s8      stored BOTTOM, read on a completed fall
 *   tile+0x1d4  s8      stored TOP, read on a completed rise
 *   tile+0x1d5  double  phase start, while MOVING (zeroed while parked)
 *   tile+0x1dd  double  phase start, while PARKED
 *   tile+0x1e5  double  1500.0, the park dwell, while parked
 *
 * ─── Two things the decompile gets wrong, and one it hides ───────────────
 *
 * 1. THE `__ftol` ARGUMENT IS THE 80-BIT VALUE, NOT THE STORED FLOAT.  Both
 *    calls are preceded by `FST float ptr [ESI+0x29]` -- FST, *not* FSTP.
 *    The store rounds to float32, but ST0 keeps the full 80-bit value, and
 *    that is what __ftol then consumes:
 *
 *      00411CF5  FST   float ptr [ESI + 0x29]     <- rounds to float32
 *      00411CF8  CALL  0x00451134                 <- truncates the 80-BIT ST0
 *
 *    This differs from slidinghazard.cpp, where the equivalent sites are
 *    `FSTP float [..]` followed later by `FLD float [..]` and so DO truncate
 *    the rounded float32.  Getting it wrong here would be a one-cell error
 *    only for heights where the float32 rounding crosses an integer -- rare,
 *    deterministic, and exactly the kind of thing a replay diverges on much
 *    later.  Both calls below take the pre-rounding `long double`.
 *
 *    (As everywhere else in this plan, the argument it(self) is invisible to
 *    the decompiler -- it arrives on the x87 stack, so `decompile_function`
 *    renders both as a bare `__ftol()`.  Read from the listing.)
 *
 * 2. `+0x44` AND `+0x48` ARE ONE DOUBLE, NOT TWO STRAY DWORDS.  The
 *    decompile shows
 *
 *      *(undefined4 *)(param_1 + 0x44) = *(undefined4 *)(param_1 + 4);
 *      *(undefined4 *)(param_1 + 0x48) = *(undefined4 *)(param_1 + 8);
 *
 *    which reads like a truncated copy of the 8-byte timestamp -- a juicy
 *    "preserved bug".  It is not one.  +0x04 is the `now` double, so +0x08 is
 *    its HIGH dword; +0x44 is the phase-start double, so +0x48 is ITS high
 *    dword.  The pair is a full 8-byte copy that MSVC emitted as two dword
 *    moves and Ghidra did not re-pair.  Written as a double assignment
 *    here -- see "Floating-point copies" below.
 *    Checked because the plan's standing rule says to: the decompile's
 *    framing was misleading, and `FSUB double ptr [ESI + 0x44]` at three
 *    sites settles that +0x44 really is read as eight bytes.
 *
 * 3. THE STATE TESTS ARE SEQUENTIAL, NOT MUTUALLY EXCLUSIVE.  The listing
 *    re-reads +0x43 from memory before each test (00411DA3, 00411E67).  So a
 *    rise that COMPLETES this tick sets state 0 and then runs the PARKED
 *    block in the same tick -- publishing the dwell timestamps immediately
 *    rather than a frame later.  Transcribed as three separate `if`s, which
 *    is what the original is; an `else if` chain would be a behaviour change.
 *
 * ─── Other exactness points preserved ────────────────────────────────────
 *
 * 4. THE TWO COMPLETION TESTS ARE NOT THE SAME KIND OF TEST.  The rise
 *    compares FLOATS -- `FILD top; FCOMP float [+0x29]` -- and against the
 *    ROUNDED float at +0x29, even though the ftol above it used the
 *    unrounded value.  The fall compares INTEGERS -- the sign-extended BYTE
 *    of the ftol result against `base - 1`.  Both are transcribed as written.
 *
 * 5. THE RISE TEST IS NaN-COMPLETING, THE DWELL TEST IS NOT.  The rise is
 *    `TEST AH,0x41 / JZ skip`, so an unordered compare (which sets both C0
 *    and C3) COMPLETES the rise; written `!(top > height)`, unordered-true.
 *    The dwell is `TEST AH,1 / JNZ skip`, so an unordered compare does NOT
 *    depart; written `elapsed >= 1500.0`, unordered-false.  Opposite
 *    polarities, and both are the original's.
 *
 * 6. THE PARKED BLOCK PUBLISHES THE OLD TIMESTAMP, THEN DEPARTS.  tile+0x1dd
 *    is written from +0x44 BEFORE the dwell test can overwrite +0x44, and
 *    tile+0x1d5/+0x1d9 are zeroed AFTER it.  So on the tick a lift departs,
 *    the tile still shows the park's start time.  Order preserved literally.
 *
 * 7. THE DEPART DIRECTION IS `(latch != 0) + 1`, via SETNZ/INC.  AL is 0 or 1
 *    after SETNZ so the INC cannot carry, and only AL is stored.  State
 *    becomes 1 (rise) when the latch is clear and 2 (fall) when it is set.
 *
 * ─── Floating-point copies ───────────────────────────────────────────────
 *
 * Doubles and floats are copied by plain assignment.  The original does
 * some of those copies with integer MOVs (phaseStart_ <- now_, every tile
 * publish) and one through x87 (now_ <- *clock_, FLD/FST); GCC at -O0
 * copies through x87 throughout.  The two differ only for a signalling NaN,
 * which x87 quietens -- and no clock or timestamp ever holds one.  Accepted
 * deliberately, as the project-wide rule (COHESION_PLAN.md, template 3).
 *
 * ─── Visual / measurable proof ───────────────────────────────────────────
 *
 * KAROO_SIM_FX=liftflip inverts the departure direction latch at the one
 * point it is consumed, so a lift parked at the bottom tries to FALL and one
 * parked at the top tries to RISE.  Platforms invert their cycle.  That is a
 * direction change rather than a magnitude one -- per CLAUDE.md, "a colour
 * tint proves the code runs; a direction change proves the maths" -- and only
 * this code path can produce it.
 *
 * KAROO_LIFT_DIAG=1 logs the first tick, the first completed rise, the first
 * completed fall and the first depart (each once), plus a tick count every
 * 5000.  As with slidinghazard.cpp it exists to distinguish "no lifts in this
 * recording" from "lifts that never move" -- the difference between a control
 * that fails and a control that cannot fail.
 */

#include <windows.h>
#include <stddef.h>
#include <new>               /* std::nothrow */
#include <string.h>          /* strcmp */

#include "liftobject.h"
#include "game.h"
#include "tile.h"
#include "soundmanager.h"
#include "static.h"
#include "log.h"

/* Read from .rdata: 0x3ba3d70a and 0x4097700000000000. */
static const float  K_MS_TO_HEIGHT = 0.005f;    /* 0x0045d384 */
static const double K_PARK_DWELL   = 1500.0;    /* 0x0045d2e0 */


/* ─── Controls and diags, read by VALUE, never by presence ────────────────
 *
 * GetEnvironmentVariableA returns 0 for empty and unset alike
 * (RENDER_PLAN.md, 2026-09-02).  placeaxis and keepobjects are shared with
 * the sibling spawns/purges in objectplace.cpp and gamereset.cpp; each file
 * reads the flag it(self). */
static int s_fx_liftflip    = 0;
static int s_fx_placeaxis   = 0;
static int s_fx_keepobjects = 0;
static int s_diag_lift      = 0;   /* KAROO_LIFT_DIAG  */
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
        if (strcmp(buf, "liftflip") == 0) {
            s_fx_liftflip = 1;
            log_write("liftobject: KAROO_SIM_FX=liftflip -- departure "
                      "direction inverted\n");
        } else if (strcmp(buf, "placeaxis") == 0) {
            s_fx_placeaxis = 1;
            log_write("liftobject: KAROO_SIM_FX=placeaxis -- lift spawn "
                      "exchanges u and v\n");
        } else if (strcmp(buf, "keepobjects") == 0) {
            s_fx_keepobjects = 1;
            log_write("liftobject: KAROO_SIM_FX=keepobjects -- lift purge "
                      "does nothing\n");
        }
    }

    if (env_set("KAROO_LIFT_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_lift = 1;
    if (env_set("KAROO_PLACE_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_place = 1;
    if (env_set("KAROO_RESET_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_reset = 1;
}

/* ═══ Construction and destruction ═════════════════════════════════════
 *
 * 0x411c50 constructs in two layers: the shared level-object base
 * constructor 0x401000 (installs the base vtable 0x45d290, zeroes +0x25,
 * +0x29, +0x2d), then the lift's own (installs 0x45d380, state 1, atTop 1,
 * sound 0).  The base vtable store is overwritten at once, so only its
 * three zeroes survive -- those are kept; every other byte is left as
 * operator new returned it, as the original leaves it.
 *
 * 0x411c80 destroys in two layers too: 0x411ca0 re-installs 0x45d380, then
 * the base destructor 0x401060 installs 0x45d290; then Free2 if flags & 1
 * (here, our own `delete` -- we allocated it with our own `new`).
 * Both vtable stores are dead -- the only caller passes flags 1, so the
 * memory is freed in the same call -- and are not reproduced.
 */
const LiftObject::Vtbl LiftObject::VTABLE = { &LiftObject::scalarDeletingDtor };

LiftObject *LiftObject::create()
{
    return new (std::nothrow) LiftObject;
}

/* Only the fields the original constructors write; every other byte is left
 * as `new` returned it, as the original leaves operator new's. */
LiftObject::LiftObject()
{
    /* base constructor 0x401000 */
    posU_   = 0.0f;
    height_ = 0.0f;
    posV_   = 0.0f;
    /* lift constructor 0x411c50 */
    vtable_ = &VTABLE;
    state_  = 1;
    atTop_  = 1;          /* spawn overwrites this with 0 */
    sound_  = 0;
}

void *LiftObject::scalarDeletingDtor(LiftObject *self, unsigned int flags)
{
    if (flags & 1)
        delete self;
    return self;
}

void LiftObject::destroy()
{
    vtable_->scalarDeletingDtor(this, 1);
}

/* ═══ 0x00417b90 -- Game::SpawnLiftObject ════════════════════════════════
 *
 * __thiscall on Game, four dword stack arguments (RET 0x10), masked to
 * bytes at each use.  The count is re-read from Game before every store in
 * the original; it cannot change here, so one local is equivalent.
 *
 * Unlike objectspawn.cpp's pair, a failed allocation is not dereferenced
 * through the raw pointer -- but the stores still go through the (NULL) slot,
 * so it faults all the same.  The allocator returns NULL rather than
 * throwing, so the original's EH frame is unobservable and not reproduced.
 */
static int s_logged_spawn = 0;
static int s_logged_oom   = 0;

void LiftObject::spawn(Game *game, unsigned int uArg, unsigned int vArg,
                       unsigned int baseArg, unsigned int topArg)
{
    unsigned int u, v, base, top;
    unsigned char n;
    LiftObject *obj;
    Tile *tile;
    void *raw;

    fx_init();

    /* The single point u and v are read -- KAROO_SIM_FX=placeaxis. */
    u = uArg & 0xff;
    v = vArg & 0xff;
    if (s_fx_placeaxis) {
        unsigned int t = u;
        u = v;
        v = t;
    }
    base = baseArg & 0xff;
    top  = topArg & 0xff;

    raw = create();
    if (raw == 0) {
        if (s_diag_place && !s_logged_oom) {
            s_logged_oom = 1;
            log_write("liftobject: ALLOCATION FAILED in spawn -- the original "
                      "would store through the slot, which now holds NULL\n");
        }
    }

    n = game->liftCount();
    game->setLiftSlot(n, (LiftObject *)raw);
    obj  = (LiftObject *)raw;
    tile = Tile::at(game->tileBase(), (int)u, (int)v);

    if (s_diag_place && !s_logged_spawn) {
        s_logged_spawn = 1;
        log_write("liftobject: first lift spawn -- slot=%u u=%u v=%u "
                  "base=%u top=%u obj=%p\n",
                  (unsigned)n, u, v, base, top, (void *)obj);
    }

    obj->clock_    = game->clock();
    obj->tickStep_   = game->tickStep();
    obj->tileBase_ = game->tileBase();

    obj->cellU_      = (signed char)u;
    obj->cellV_      = (signed char)v;
    obj->slot_       = n;
    obj->heightCell_ = (signed char)base;
    obj->height_     = (float)(int)base;      /* FILD dword, FSTP float */

    tile->setLiftBottom((unsigned char)base);
    obj->baseHeight_ = (signed char)base;
    tile->setLiftTop((unsigned char)top);
    obj->topHeight_  = (signed char)top;
    tile->setLiftSlot(n);

    obj->sound_ = 0;
    tile->clearLiftMovingSince();

    /* The 8-byte clock (two dword moves at 00417d34 in the original). */
    obj->phaseStart_ = *game->clock();

    obj->atTop_ = 0;       /* overwrites the constructor's 1 */
    obj->state_ = 1;

    game->setLiftCount((unsigned char)(n + 1));
}

/* ═══ 0x00417d90 -- Game::PurgeLiftObjects ═══════════════════════════════
 *
 * The count is RE-READ every iteration and the index is a byte (AL, `JC`).
 * The slot is re-read between the sound release and the destructor.  The
 * sound handle is read off the raw slot with no null test on the object;
 * the destructor call does test it.  The trailing count store is
 * redundant when the count was already 0, and is preserved.
 */
static int s_logged_purge = 0;
static unsigned s_live_purges = 0;

void LiftObject::purgeAll(Game *game)
{
    unsigned char i;

    fx_init();

    if (s_fx_keepobjects)
        return;   /* objects AND count survive -- see gamereset.cpp */

    if (s_diag_reset && !s_logged_purge) {
        s_logged_purge = 1;
        log_write("liftobject: first purge -- count=%u\n",
                  (unsigned)game->liftCount());
    }

    i = 0;
    if (game->liftCount() != 0) {
        if (s_diag_reset)
            log_write("liftobject: LIVE purge #%u -- count=%u\n",
                      ++s_live_purges, (unsigned)game->liftCount());
        do {
            if (game->soundCreated() != 0) {
                CStaticSoundbuffer *h = game->liftSlot(i)->sound_;
                if (h != 0)
                    game->soundManager()->releaseStaticForOwner(h, 1);
            }
            LiftObject *obj = game->liftSlot(i);
            if (obj != 0)
                obj->destroy();
            i++;
        } while (i < game->liftCount());
    }
    game->setLiftCount(0);
}

/* ═══ 0x00411cb0 -- UpdateVerticalLiftObject ═════════════════════════════ */

/* The CRT's __ftol 0x00451134: truncate toward zero.  Only the low byte is
 * ever kept, exactly as the original's `MOV byte ptr [ESI+0x33],AL`. */
static inline signed char ftol_c(long double v)
{
    return (signed char)(long long)v;
}

static unsigned long s_ticks = 0;
static int s_logged_first  = 0;
static int s_logged_rise   = 0;
static int s_logged_fall   = 0;
static int s_logged_depart = 0;

void LiftObject::tick()
{
    Tile *t;

    fx_init();

    /* Unconditional once-per-run line: silence from a flag-gated line is
     * ambiguous between "no lifts" and "the flag never arrived". */
    if (!s_logged_first) {
        s_logged_first = 1;
        log_write("liftobject: first lift tick -- this=%p\n", (void *)this);
    }
    if (s_diag_lift) {
        ++s_ticks;
        if ((s_ticks % 5000) == 0)
            log_write("liftobject: %lu ticks\n", s_ticks);
    }

    tickStepCopy_ = *tickStep_;
    now_ = *clock_;

    /* ─── State 1: RISING ───────────────────────────────────────────── */
    if (state_ == 1) {
        /* Point 1: h is the 80-bit value.  height_ gets the rounded float
         * (FST), but the ftol below consumes h it(self). */
        long double h = ((long double)now_ - (long double)phaseStart_)
                        * (long double)K_MS_TO_HEIGHT
                        + (long double)(int)baseHeight_;
        height_     = (float)h;
        heightCell_ = ftol_c(h);

        /* Point 4/5: a FLOAT compare against the ROUNDED height, and
         * unordered-true so a NaN completes the rise. */
        if (!((long double)(int)topHeight_ > (long double)height_)) {
            signed char top = Tile::at(tileBase_, cellU_, cellV_)->liftTop();

            if (s_diag_lift && !s_logged_rise) {
                s_logged_rise = 1;
                log_write("liftobject: first completed rise -- cell=(%d,%d) "
                          "top=%d\n", (int)cellU_, (int)cellV_, (int)top);
            }

            heightCell_ = top;
            /* Point 2: one 8-byte double, not two stray dwords. */
            phaseStart_ = now_;
            height_ = (float)(int)top;
            atTop_  = 1;
            state_  = 0;

            if (sound_ != 0)
                CStatic_HaltPlayback(sound_);
        }

        if (sound_ != 0)
            CStatic_Set3DPosition(sound_, (float)(int)cellU_, height_,
                                  -(float)(int)cellV_, 1);
    }

    /* ─── State 2: FALLING.  Point 3: a separate `if`, re-reading state,
     * so a rise that just completed does NOT fall through into this. ─── */
    if (state_ == 2) {
        long double h = (long double)(int)topHeight_
                        - ((long double)now_ - (long double)phaseStart_)
                          * (long double)K_MS_TO_HEIGHT;
        height_ = (float)h;
        {
            signed char c = ftol_c(h);
            heightCell_ = c;

            /* Point 4: an INTEGER compare on the sign-extended byte. */
            if ((int)c <= (int)baseHeight_ - 1) {
                signed char bot =
                    Tile::at(tileBase_, cellU_, cellV_)->liftBottom();

                if (s_diag_lift && !s_logged_fall) {
                    s_logged_fall = 1;
                    log_write("liftobject: first completed fall -- "
                              "cell=(%d,%d) bottom=%d\n",
                              (int)cellU_, (int)cellV_, (int)bot);
                }

                heightCell_ = bot;
                phaseStart_ = now_;
                height_ = (float)(int)bot;
                atTop_  = 0;
                state_  = 0;

                if (sound_ != 0)
                    CStatic_HaltPlayback(sound_);
            }
        }

        if (sound_ != 0)
            CStatic_Set3DPosition(sound_, (float)(int)cellU_, height_,
                                  -(float)(int)cellV_, 1);
    }

    /* ─── The tile publish, and the parked dwell ────────────────────── */
    t = Tile::at(tileBase_, cellU_, cellV_);

    if (state_ == 0) {
        /* Point 6: the OLD phase start is published first. */
        t->setLiftParkedSince(phaseStart_);
        t->setLiftDwell(K_PARK_DWELL);

        /* Point 5: unordered-false, so a NaN does NOT depart. */
        if (now_ - phaseStart_ >= K_PARK_DWELL) {
            int latch = atTop_;

            /* KAROO_SIM_FX=liftflip: invert the latch at the single point it
             * is consumed, so the lift departs the wrong way. */
            if (s_fx_liftflip)
                latch = (latch != 0) ? 0 : 1;

            phaseStart_ = now_;
            /* Point 7: SETNZ + INC, so 1 (rise) or 2 (fall). */
            state_ = (signed char)((latch != 0) + 1);

            if (s_diag_lift && !s_logged_depart) {
                s_logged_depart = 1;
                log_write("liftobject: first depart -- cell=(%d,%d) "
                          "latch=%d state=%d\n",
                          (int)cellU_, (int)cellV_, latch, (int)state_);
            }

            if (sound_ != 0) {
                CStatic_Set3DPosition(sound_, (float)(int)cellU_, height_,
                                      -(float)(int)cellV_, 1);
                CStatic_TriggerPlayback(sound_, 1);
            }
        }

        /* Point 6: zeroed AFTER the depart test, so a departing lift still
         * leaves the park timestamp visible at tile+0x1dd this tick. */
        t->clearLiftMovingSince();
    } else {
        t->setLiftMovingSince(phaseStart_);
    }

    /* Published every tick, in both states. */
    t->setHeight((unsigned char)heightCell_);
    t->setLiftLiveHeight(height_);

    posU_ = (float)(int)cellU_;
    posV_ = (float)(int)cellV_;
}

/* ═══ Exports -- thin ABI shims; patch.py routes the three originals here ═ */

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_UpdateVerticalLiftObject(LiftObject *self)
{
    self->tick();
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_SpawnLiftObject(Game *self, unsigned int uArg, unsigned int vArg,
                    unsigned int baseArg, unsigned int topArg)
{
    LiftObject::spawn(self, uArg, vArg, baseArg, topArg);
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PurgeLiftObjects(Game *self)
{
    LiftObject::purgeAll(self);
}
