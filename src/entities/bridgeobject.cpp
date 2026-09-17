/* BridgeObject -- a switch-operated bridge: spawn, tick, purge and its deck
 * quad.
 *
 *     Game::SpawnBridgeObject        0x00419ed0   (was objectplace.cpp)
 *     Game::PurgeBridgeObjects       0x0041a190   (was gamereset.cpp)
 *     BridgeObject::UpdateBridgeObject 0x0043ec50 (was slidinghazard.cpp)
 *     DrawBridgeSurfaces, loop C     0x00408a00   (vertex build; the D3D
 *                                                  state stays in bridgesurf.cpp)
 *
 *     BridgeObject ctor / dtor       0x0043ec00 / 0x0043ec20 + 0x0043ec40
 *
 * ─── One class, three old names ──────────────────────────────────────────
 *
 * GAMETICK_PLAN.md took the tick as "UpdateSlidingHazardObject", a sliding
 * spike; bridgesurf.cpp called the same objects "conveyors" and HOOKS.md
 * "switch triggers".  They are all the bridge: the slot array Game+0x170643
 * (count +0x170a43) is filled only by SpawnBridgeObject, the tick reads
 * exactly the fields that spawn writes, a switch arms it (GameTick: the
 * player's or a foe's switch byte selects a slot here, sets +0x53 and the
 * phase start), and DrawBridgeSurfaces draws a deck from its anchor
 * (+0x39..) to its live end (+0x25..).  Ghidra now names the tick
 * UpdateBridgeObject, and the names here follow: the tick's export is
 * Sim_UpdateBridgeObject (was Sim_UpdateSlidingHazardObject), its control
 * `deckaxis` (was `hazardaxis`) and its diag KAROO_DECK_DIAG (was
 * KAROO_HAZARD_DIAG).  Not `bridgeaxis` / KAROO_BRIDGE_DIAG: those name the
 * rejected spawn control and bridgesurf.cpp's render diag.  GAMETICK_PLAN.md
 * and older commit messages use the old names.
 *
 * ─── The tick ────────────────────────────────────────────────────────────
 *
 * A two-phase mover driven by the game clock, active only while ARMED:
 *
 *   phase 0   EXTENDING   coord = rest + elapsed*0.01*step
 *   phase 1   RETRACTING  coord = (span*step + rest) - elapsed*0.01*step
 *
 * `elapsed` is now - phaseStart in ms; 0.01 is _DAT_0045d400 (0x3c23d70a).
 * A phase ends when elapsed >= span*100: the sound halts, armed clears, the
 * phase flips and the coordinate snaps.  While extending or retracting, each
 * cell short of the guard (+0x38) is stamped as bridge deck:
 *
 *   extend    tile+0x19d = 0x14, +0x217 = 1, +0x19c = height, +0x1f5 = axis,
 *             +0x1f6 = 1, +0x1f4 = slot
 *   retract   tile+0x19d = 0, +0x217 = 0, +0x19c = 0, +0x1f6 = 0
 *
 * Exactness points, all from the listing:
 *
 * 1. THE PHASE-END COMPARE IS AN UNSIGNED WIDENING.  span*100 is computed in
 *    32-bit int and loaded as the low dword of a qword with a zero high dword
 *    (FILD qword), so a negative span becomes a huge positive threshold.
 *
 * 2. THE COMPARE IS NaN-ASYMMETRIC: `TEST AH,0x41; JZ mid` ends the phase on
 *    unordered, so it is written `!(span100 > elapsed)`.
 *
 * 3. THE ARITHMETIC IS 80-BIT and the phases associate differently -- extend
 *    `((elapsed*0.01f)*step)+rest`, retract `(span*step+rest) - travel`.
 *    Written in long double in that order; a 1-ulp change is a whole cell.
 *
 * 4. THE SOUND IS POSITIONED BEFORE THE MOVE, from last frame's cell.
 *
 * 5. RETRACT CLEARS FOUR OF THE SIX FIELDS EXTEND SETS; +0x1f5 and +0x1f4
 *    are left behind (0x0043F037..0x0043F09E has no store to either).
 *
 * 6. THE EXTEND-END SNAP WRITES ONE CELL INDEX AND THEN BOTH FLOATS; the six
 *    __ftol arguments (read from the listing -- the decompile drops them) are
 *    `(int)(step*span) + rest` at extend end and plain `rest` at retract end,
 *    never the live coordinate.
 *
 * 7. THE GUARD TEST FALLS THROUGH: axis 1 with u >= guard falls into the
 *    axis-2 test and returns without stamping -- the || form below.
 *
 * ─── Floating-point copies ───────────────────────────────────────────────
 *
 * Doubles are copied by assignment (phaseStart_ <- clock, now_ <- clock);
 * the original uses integer MOVs.  They differ only for a signalling NaN,
 * which no clock holds.  Accepted deliberately (COHESION_PLAN.md, template 3).
 *
 * ─── Controls and diags ──────────────────────────────────────────────────
 *
 * KAROO_SIM_FX=deckaxis flips the travel axis 1<->2 at the single point the
 * tick reads it, so motion, guard and stamp move together -- a DIRECTION
 * change.  KAROO_SIM_FX=bridgespan exchanges the spawn's two far-end arms
 * (see spawn).  KAROO_SIM_FX=keepobjects makes the purge do nothing, shared
 * with the other purges.
 *
 * KAROO_DECK_DIAG=1 logs the first extend stamp and retract unstamp, plus a
 * tick count every 5000; the first tick is logged unconditionally.
 * KAROO_PLACE_DIAG=1 logs every spawn with its scan axis; KAROO_RESET_DIAG=1
 * the first purge and every live one.
 */

#include <windows.h>
#include <stddef.h>
#include <math.h>            /* fmod, sqrt */
#include <new>               /* std::nothrow */
#include <string.h>          /* strcmp */

#include "bridgeobject.h"
#include "game.h"
#include "tile.h"
#include "soundmanager.h"
#include "static.h"
#include "log.h"

/* _DAT_0045d400, read from .rdata: 0x3c23d70a, the float nearest 0.01.
 * A float constant, so the widening to 80-bit happens where the original's
 * `FMUL float ptr` does it. */
static const float K_MS_TO_TILE = 0.01f;      /* 0x0045d400 */

/* ─── Controls and diags, read by VALUE, never by presence ──────────────── */
static int s_fx_deckaxis  = 0;
static int s_fx_bridgespan  = 0;
static int s_fx_keepobjects = 0;
static int s_diag_deck    = 0;   /* KAROO_DECK_DIAG */
static int s_diag_place     = 0;   /* KAROO_PLACE_DIAG  */
static int s_diag_reset     = 0;   /* KAROO_RESET_DIAG  */
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
        if (strcmp(buf, "deckaxis") == 0) {
            s_fx_deckaxis = 1;
            log_write("bridgeobject: KAROO_SIM_FX=deckaxis -- travel axis "
                      "1<->2 flipped\n");
        } else if (strcmp(buf, "bridgespan") == 0) {
            s_fx_bridgespan = 1;
            log_write("bridgeobject: KAROO_SIM_FX=bridgespan -- the two arms "
                      "that compute the bridge's far end (+0x38) are "
                      "exchanged, so a forward run records the backward extent "
                      "and vice versa.  The SCAN is untouched, so this stays "
                      "in bounds\n");
        } else if (strcmp(buf, "keepobjects") == 0) {
            s_fx_keepobjects = 1;
            log_write("bridgeobject: KAROO_SIM_FX=keepobjects -- bridge purge "
                      "does nothing\n");
        }
    }

    if (env_set("KAROO_DECK_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_deck = 1;
    if (env_set("KAROO_PLACE_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_place = 1;
    if (env_set("KAROO_RESET_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_reset = 1;
}

/* ═══ Construction and destruction ═════════════════════════════════════
 *
 * 0x43ec00 constructs in two layers: the shared level-object base
 * constructor 0x401000 (installs the base vtable 0x45d290, zeroes +0x25,
 * +0x29, +0x2d), then the bridge's own (installs 0x45d714, zeroes +0x58 and
 * +0x53).  Every other byte is left as operator new returned it.
 *
 * 0x43ec20 (vtable slot 0; the vtable 0x45d714 has ONE slot -- 0x45d718 is
 * another class's, referenced from 0x43f0c6/0x43f102) calls 0x43ec40, which
 * re-installs 0x45d714 and jumps to the base destructor 0x401060; then Free2
 * if flags & 1.  Both vtable stores are dead -- the only caller passes
 * flags 1 -- and are not reproduced.
 *
 * A byte scan of .text for 0x45d714 finds only the ctor (0043EC0C) and the
 * dtor (0043EC42); the ctor's one caller (0x419F08) is the spawn, now ours;
 * the dtor is reached only through the vtable.  So we both create and
 * destroy every bridge, and use our own new/delete.
 */
const BridgeObject::Vtbl BridgeObject::VTABLE = { &BridgeObject::scalarDeletingDtor };

BridgeObject *BridgeObject::create()
{
    return new (std::nothrow) BridgeObject;
}

BridgeObject::BridgeObject()
{
    /* base constructor 0x401000 */
    posU_ = 0.0f;
    posY_ = 0.0f;
    posV_ = 0.0f;
    /* bridge constructor 0x43ec00 */
    vtable_ = &VTABLE;
    phase_  = 0;
    armed_  = 0;
}

void *BridgeObject::scalarDeletingDtor(BridgeObject *self, unsigned int flags)
{
    if (flags & 1)
        delete self;
    return self;
}

void BridgeObject::destroy()
{
    vtable_->scalarDeletingDtor(this, 1);
}

/* ═══ 0x00419ed0 -- Game::SpawnBridgeObject ════════════════════════════════
 *
 * __thiscall on Game, FIVE dword stack arguments (RET 0x14): u, v, height,
 * slotIndex, axis.  TWO E8 call sites, 0x0041699F and 0x004169EE, both in
 * SetupLevelObjects (tile types 0x12 -> axis 1 and 0x13 -> axis 2).
 *
 * ─── It does NOT index by the count ──────────────────────────────────────
 *
 * `0x00419f2f` is `LEA ECX,[ESI + ECX*4 + 0x170643]` with ECX from the FOURTH
 * ARGUMENT -- the bridge's switch slot.  The count (+0x170a43) is still
 * incremented at the end, so it is a running total, not the write cursor.
 *
 * ─── It carries the null-deref defect ────────────────────────────────────
 *
 *     00419f3c  MOV dword ptr [ECX],EAX        slot := obj
 *     00419f3e  MOV dword ptr [EAX + 0xc],EDX  <- the RAW pointer
 *
 * so a failed allocation faults at address 0xc.  Our first store is also
 * through the new pointer, so it faults the same way.
 *
 * ─── One FST, two fields ─────────────────────────────────────────────────
 *
 * `+0x29` and `+0x3d` both receive (float)height from ONE FILD: FST at
 * 0041a0f2, FSTP at 0041a119 after two other FILD/FSTP pairs.  The same
 * computation, so they cannot drift apart.
 *
 * ─── The two scans ───────────────────────────────────────────────────────
 *
 *   axis == 1   walks U, guard = marker of (u-1, v), end field +0x5d
 *   axis == 2   walks V, guard = marker of (u, v-1), end field +0x5e
 *   otherwise   no scan
 *
 * A zero guard flips the direction: +0x57 becomes 0xff (-1, read MOVSX) and
 * the end field is incremented.  Then a `while` (not do/while) walks by that
 * step while the marker stays zero, counting cells into +0x45; neither scan
 * clears markers.  +0x38 is `end + count` stepping forward, just `end`
 * stepping backward.  BOTH SCANS ARE UNBOUNDED -- see CLAUDE.md, "A
 * control's blast radius".
 *
 * ─── Negative control: `bridgespan`, after `bridgeaxis` was REJECTED ────
 *
 * `bridgespan` exchanges the two arms that compute +0x38; the scan it(self) is
 * untouched, so it stays in bounds.  **15/16** (`bridge01` fails: the player
 * falls, lives 3->2, vitality 87->0), `levelreport.py` PASS.
 *
 * The first attempt, `bridgeaxis`, exchanged the axis codes and gave a
 * healthy-looking 15/16 -- because `bridge01` CRASHED: the eighth bridge on
 * Space\Bridge01 (u=1 v=15, axis 2 forced to 1) takes the flipped path and
 * walks u down from 1, past 0, to 0xFFFFFFFF and on out of the tile array.
 * A crash reports as a FAIL, so the pass column cannot tell the two apart;
 * running the recording by hand caught it.
 *
 * `KAROO_PLACE_DIAG=1` census: bridge01 builds 11 bridges (6 U, 5 V) and
 * fails; sandra02 builds 1 and passes; water01, thrower02 and
 * castle-something build none.  Nearly a ceiling: the player also has to
 * cross the bridge for a moved end to reach an asserted field.
 */
static int s_logged_spawn = 0;
static int s_logged_oom   = 0;

void BridgeObject::spawn(Game *game, unsigned int uArg, unsigned int vArg,
                         unsigned int heightArg, unsigned int slotArg,
                         unsigned int axisArg)
{
    unsigned int u, v, height, axis, slot;
    unsigned int var;
    unsigned char *base;
    BridgeObject *obj;
    Tile *tile;

    fx_init();

    u      = uArg & 0xff;
    v      = vArg & 0xff;
    height = heightArg & 0xff;
    slot   = slotArg & 0xff;
    axis   = axisArg & 0xff;

    obj = create();
    if (obj == 0 && s_diag_place && !s_logged_oom) {
        s_logged_oom = 1;
        log_write("bridgeobject: ALLOCATION FAILED in spawn -- the original "
                  "faults at 0xc, and so does this\n");
    }

    /* Indexed by the ARGUMENT, not the count -- see above. */
    game->setBridgeSlot(slot, obj);

    obj->clock_  = game->clock();
    obj->tickStep_ = game->tickStep();
    obj->axis_   = (unsigned char)axis;

    base = game->tileBase();
    obj->tileBase_ = base;

    tile = Tile::at(base, (int)u, (int)v);
    tile->setObjectMarker(0);

    obj->endU_ = (unsigned char)u;
    obj->endV_ = (unsigned char)v;

    /* Every spawn is logged, for the same reason the slide's are: whether a
     * 1-of-16 control result is a ceiling depends on how many recordings
     * build one, and along which axis. */
    if (s_diag_place) {
        s_logged_spawn++;
        log_write("bridgeobject: bridge spawn #%d -- slot=%u u=%u v=%u "
                  "height=%u axis=%u %s\n",
                  s_logged_spawn, slot, u, v, height, axis,
                  (axis == 1) ? "SCAN-U" :
                  (axis == 2) ? "SCAN-V" : "no-scan");
    }

    if (axis == 1) {
        obj->step_ = 1;
        obj->endU_ = (unsigned char)u;
        if (Tile::at(base, (int)u - 1, (int)v)->objectMarker() == TILE_EMPTY) {
            obj->step_ = (signed char)0xff;
            obj->endU_ = (unsigned char)(u + 1);
        }
        obj->span_ = 0;

        var = u;
        while (Tile::at(base, (int)var, (int)v)->objectMarker() == TILE_EMPTY) {
            var = (unsigned int)(var + (int)obj->step_);
            obj->span_ = (signed char)(obj->span_ + 1);
        }

        /* The single point the far end is computed -- see above. */
        if ((obj->step_ == 1) != (s_fx_bridgespan != 0))
            obj->guard_ = (unsigned char)(obj->endU_ + (unsigned char)obj->span_);
        else
            obj->guard_ = obj->endU_;
    } else if (axis == 2) {
        obj->step_ = 1;
        obj->endV_ = (unsigned char)v;
        if (Tile::at(base, (int)u, (int)v - 1)->objectMarker() == TILE_EMPTY) {
            obj->step_ = (signed char)0xff;
            obj->endV_ = (unsigned char)(v + 1);
        }
        obj->span_ = 0;

        var = v;
        while (Tile::at(base, (int)u, (int)var)->objectMarker() == TILE_EMPTY) {
            var = (unsigned int)(var + (int)obj->step_);
            obj->span_ = (signed char)(obj->span_ + 1);
        }

        if ((obj->step_ == 1) != (s_fx_bridgespan != 0))
            obj->guard_ = (unsigned char)(obj->endV_ + (unsigned char)obj->span_);
        else
            obj->guard_ = obj->endV_;
    }

    obj->height_     = (unsigned char)height;
    obj->cellU_      = (signed char)obj->endU_;
    obj->cellV_      = (signed char)obj->endV_;
    obj->heightCell_ = (signed char)height;

    /* +0x5d and +0x5e are read SIGNED here (MOVSX), where the byte stores
     * above are not.  A flipped-direction bridge can put 0xff in them. */
    obj->posU_  = (float)(int)(signed char)obj->endU_;
    obj->posY_  = (float)(int)height;
    obj->posV_  = (float)(int)(signed char)obj->endV_;
    obj->restU_ = (float)(int)(signed char)obj->endU_;
    obj->restY_ = (float)(int)height;          /* the same FST as posY_ */
    obj->restV_ = (float)(int)(signed char)obj->endV_;

    obj->slot_       = (unsigned char)slot;
    obj->tileHeight_ = (unsigned char)height;
    obj->phase_      = 0;
    obj->armed_      = 0;

    tile->setBridgeSlot((unsigned char)slot);
    tile->setBridgeAxis((unsigned char)axis);
    tile->setField1f6(0);

    obj->sound_ = 0;

    game->setBridgeCount((unsigned char)(game->bridgeCount() + 1));

    if (s_diag_place)
        log_write("bridgeobject:   bridge done -- +0x38=%u +0x45=%u +0x57=%d "
                  "+0x5d=%u +0x5e=%u\n",
                  (unsigned)obj->guard_, (unsigned)(unsigned char)obj->span_,
                  (int)obj->step_, (unsigned)obj->endU_, (unsigned)obj->endV_);
}

/* ═══ 0x0041a190 -- Game::PurgeBridgeObjects ═══════════════════════════════
 *
 * Indexes with a SIGNED int against the zero-extended count byte (0x0041a1d6
 * INC EBP, 0x0041a1e0 CMP EBP,EAX / JL), unlike the byte index the other
 * purges use; the count is re-read every iteration.  The sound handle is read
 * off the raw slot with no null test on the object; the destructor call does
 * test it.  The trailing count store is redundant at count 0, and preserved.
 */
static int s_logged_purge = 0;
static unsigned s_live_purges = 0;

void BridgeObject::purgeAll(Game *game)
{
    int i;

    fx_init();

    if (s_fx_keepobjects)
        return;   /* objects AND count survive -- see gamereset.cpp */

    if (s_diag_reset && !s_logged_purge) {
        s_logged_purge = 1;
        log_write("bridgeobject: first purge -- count=%u\n",
                  (unsigned)game->bridgeCount());
    }

    i = 0;
    if (game->bridgeCount() != 0) {
        if (s_diag_reset)
            log_write("bridgeobject: LIVE purge #%u -- count=%u\n",
                      ++s_live_purges, (unsigned)game->bridgeCount());
        do {
            if (game->soundCreated() != 0) {
                CStaticSoundbuffer *h = game->bridgeSlot(i)->sound_;
                if (h != 0)
                    game->soundManager()->releaseStaticForOwner(h, 1);
            }
            BridgeObject *obj = game->bridgeSlot(i);
            if (obj != 0)
                obj->destroy();
            i++;
        } while (i < (int)(unsigned int)game->bridgeCount());
    }
    game->setBridgeCount(0);
}

/* ═══ The switch: GameTick's player path starts the loop sound ═══════════ */
void BridgeObject::playArmSound()
{
    if (sound_ != 0) {
        CStatic_Set3DPosition(sound_, (float)(int)cellU_, (float)(int)heightCell_,
                              -(float)(int)cellV_, 1);
        CStatic_TriggerPlayback(sound_, 1);
    }
}

/* ═══ 0x0043ec50 -- UpdateBridgeObject ════════════════════════════════════
 *
 * __thiscall, no stack arguments (bare RET).  ONE E8 call site, 0x004150D9
 * in GameTick, which discards EAX -- so `void` is exact. */

/* The CRT's __ftol 0x00451134: truncate toward zero; only the low byte is
 * kept, as the original's `MOV byte ptr [..],AL`. */
static inline signed char ftol_c(long double v)
{
    return (signed char)(long long)v;
}

static unsigned long s_ticks = 0;
static int s_logged_first   = 0;
static int s_logged_stamp   = 0;
static int s_logged_unstamp = 0;

void BridgeObject::tick()
{
    unsigned char axis;
    long double elapsed;
    double span100;
    signed char cu, cv;

    fx_init();

    /* Unconditional once-per-run line: silence from a flag-gated line is
     * ambiguous between "no bridges" and "the flag never arrived". */
    if (!s_logged_first) {
        s_logged_first = 1;
        log_write("bridgeobject: first bridge tick -- this=%p\n", (void *)this);
    }
    if (s_diag_deck) {
        ++s_ticks;
        if ((s_ticks % 5000) == 0)
            log_write("bridgeobject: %lu ticks\n", s_ticks);
    }

    tickStepCopy_ = *tickStep_;
    now_ = *clock_;

    /* The axis, read once.  deckaxis flips it here, at the single point
     * every axis decision below goes through. */
    axis = axis_;
    if (s_fx_deckaxis) {
        if (axis == 1)      axis = 2;
        else if (axis == 2) axis = 1;
    }

    /* A disarmed bridge is inert: the original drops the loaded clock off
     * the x87 stack (FSTP ST0 at 0x0043F0AC) and returns. */
    if (armed_ == 0)
        return;

    elapsed = (long double)now_ - (long double)phaseStart_;

    /* Point 1. */
    span100 = (double)(unsigned int)((int)span_ * 100);

    if (phase_ == 0) {
        /* ─── EXTENDING ─────────────────────────────────────────────── */

        /* Point 2. */
        if (!((long double)span100 > elapsed)) {
            /* Point 6: one index, then both floats. */
            if (sound_ != 0)
                CStatic_HaltPlayback(sound_);
            armed_ = 0;
            phase_ = 1;

            if (axis == 1)
                cellU_ = ftol_c((long double)(int)((int)step_ * (int)span_)
                                + (long double)restU_);
            else
                cellV_ = ftol_c((long double)(int)((int)step_ * (int)span_)
                                + (long double)restV_);

            posU_ = (float)(int)cellU_;
            posV_ = (float)(int)cellV_;
            return;
        }

        /* Point 4: last frame's cell; note the negated v. */
        if (sound_ != 0)
            CStatic_Set3DPosition(sound_, (float)(int)cellU_,
                                  (float)(int)heightCell_,
                                  -(float)(int)cellV_, 1);

        /* Point 3: ((elapsed * 0.01f) * step) + rest. */
        {
            long double t = elapsed * (long double)K_MS_TO_TILE;
            if (axis == 1)
                posU_ = (float)(t * (long double)(int)step_
                                + (long double)restU_);
            else
                posV_ = (float)(t * (long double)(int)step_
                                + (long double)restV_);
        }

        cu = ftol_c((long double)posU_);
        cellU_ = cu;
        cv = ftol_c((long double)posV_);
        cellV_ = cv;

        /* Point 7. */
        if ((axis == 1 && cu < (signed char)guard_)
            || (axis == 2 && cv < (signed char)guard_)) {
            Tile *t = Tile::at(tileBase_, cu, cv);

            if (s_diag_deck && !s_logged_stamp) {
                s_logged_stamp = 1;
                log_write("bridgeobject: first extend stamp -- axis=%u "
                          "cell=(%d,%d) height=%u\n",
                          (unsigned)axis, (int)cu, (int)cv,
                          (unsigned)tileHeight_);
            }

            t->setObjectMarker(0x14);
            t->setBusy(1);
            t->setHeight(tileHeight_);
            /* The STAMPED axis is the flipped one under deckaxis too. */
            t->setBridgeAxis(axis);
            t->setField1f6(1);
            t->setBridgeSlot(slot_);
        }
        return;
    }

    /* ─── RETRACTING ────────────────────────────────────────────────── */

    if (!((long double)span100 > elapsed)) {
        /* Snap back to REST, not the live coordinate. */
        if (sound_ != 0)
            CStatic_HaltPlayback(sound_);
        armed_ = 0;
        phase_ = 0;

        if (axis == 1)
            cellU_ = ftol_c((long double)restU_);
        else
            cellV_ = ftol_c((long double)restV_);

        posU_ = (float)(int)cellU_;
        posV_ = (float)(int)cellV_;
        return;
    }

    if (sound_ != 0)
        CStatic_Set3DPosition(sound_, (float)(int)cellU_,
                              (float)(int)heightCell_,
                              -(float)(int)cellV_, 1);

    /* Point 3: the destination first, then the travel subtracted from it.
     * Do not re-associate into `rest + (span - t)*step`. */
    {
        int step = (int)step_;
        long double travel = elapsed * (long double)K_MS_TO_TILE
                             * (long double)step;
        if (axis == 1)
            posU_ = (float)(((long double)(int)((int)span_ * step)
                             + (long double)restU_) - travel);
        else
            posV_ = (float)(((long double)(int)((int)span_ * step)
                             + (long double)restV_) - travel);
    }

    cu = ftol_c((long double)posU_);
    cellU_ = cu;
    cv = ftol_c((long double)posV_);
    cellV_ = cv;

    if ((axis == 1 && cu < (signed char)guard_)
        || (axis == 2 && cv < (signed char)guard_)) {
        Tile *t = Tile::at(tileBase_, cu, cv);

        if (s_diag_deck && !s_logged_unstamp) {
            s_logged_unstamp = 1;
            log_write("bridgeobject: first retract unstamp -- axis=%u "
                      "cell=(%d,%d)\n", (unsigned)axis, (int)cu, (int)cv);
        }

        /* Point 5: four of the six; +0x1f5 and +0x1f4 are left alone. */
        t->setObjectMarker(0);
        t->setBusy(0);
        t->setHeight(0);
        t->setField1f6(0);
    }
}

/* ═══ 0x00408a00 -- DrawBridgeSurfaces, loop C's vertex build ════════════
 *
 * Moved from bridgesurf.cpp, which keeps the render states and the draw --
 * see its header for the loop structure and the preserved oddities.  The
 * quad runs from the anchor a (+0x39..) to the live end b (+0x25..), z
 * negated on read, one unit wide.  The vertex zero-init is dead and kept. */
bool BridgeObject::buildSurface(BridgeVertex v[4], double t, bool backward,
                                BridgeSurfaceInfo *info) const
{
    if (armed_ == 0 && phase_ == 0)
        return false;

    for (int q = 0; q < 4; q++) {
        v[q].x = v[q].y = v[q].z = 0.0f;
        v[q].diffuse = 0xFFFFFFFF; /* dead: rewritten by the caller */
        v[q].u0 = v[q].v0 = v[q].u1 = v[q].v1 = 0.0f;
    }

    float ax = restU_;
    float ay = restY_;
    float az = -restV_;
    float bx = posU_;
    float by = posY_;
    float bz = -posV_;
    float n  = (float)span_;

    float dx = bx - ax, dy = by - ay, dz = bz - az;
    /* Summation order matches the x87: dy*dy + dz*dz, then + dx*dx. */
    float len = (float)sqrt(dy * dy + dz * dz + dx * dx);
    float vs  = len * 0.25f;

    float f = (float)fmod(t * (double)0.001f / n, 1.0);
    if (backward)
        f = -f;
    float fn = f * n;

    if ((signed char)axis_ == 1) {
        float x0 = ax - 0.5f;
        float zlo = az - 0.5f, zhi = az + 0.5f;
        if (step_ > 0) {
            v[0].x = x0 + len; v[0].y = ay; v[0].z = zlo;
            v[0].u0 = 0.0f; v[0].v0 = vs - fn;
            v[0].u1 = 0.0f; v[0].v1 = 1.0f;
            v[1].x = x0;       v[1].y = ay; v[1].z = zlo;
            v[1].u0 = 0.0f; v[1].v0 = -fn;
            v[1].u1 = 0.0f; v[1].v1 = 0.0f;
            v[2].x = x0 + len; v[2].y = ay; v[2].z = zhi;
            v[2].u0 = 1.0f; v[2].v0 = vs - fn;
            v[2].u1 = 1.0f; v[2].v1 = 1.0f;
            v[3].x = x0;       v[3].y = ay; v[3].z = zhi;
            v[3].u0 = 1.0f; v[3].v0 = -fn;
            v[3].u1 = 1.0f; v[3].v1 = 0.0f;
        } else {
            v[0].x = x0;       v[0].y = ay; v[0].z = zlo;
            v[0].u0 = 0.0f; v[0].v0 = fn + vs;
            v[0].u1 = 0.0f; v[0].v1 = 1.0f;
            v[1].x = x0 - len; v[1].y = ay; v[1].z = zlo;
            v[1].u0 = 0.0f; v[1].v0 = fn;
            v[1].u1 = 0.0f; v[1].v1 = 0.0f;
            v[2].x = x0;       v[2].y = ay; v[2].z = zhi;
            v[2].u0 = 1.0f; v[2].v0 = fn + vs;
            v[2].u1 = 1.0f; v[2].v1 = 1.0f;
            v[3].x = x0 - len; v[3].y = ay; v[3].z = zhi;
            v[3].u0 = 1.0f; v[3].v0 = fn;
            v[3].u1 = 1.0f; v[3].v1 = 0.0f;
        }
    } else {
        float xhi = ax + 0.5f, xlo = ax - 0.5f;
        float z1 = az + 0.5f;
        if (step_ > 0) {
            v[0].x = xhi; v[0].y = ay; v[0].z = z1 - len;
            v[0].u0 = 1.0f; v[0].v0 = vs - fn;
            v[0].u1 = 1.0f; v[0].v1 = 1.0f;
            v[1].x = xlo; v[1].y = ay; v[1].z = z1 - len;
            v[1].u0 = 0.0f; v[1].v0 = vs - fn;
            v[1].u1 = 0.0f; v[1].v1 = 1.0f;
            v[2].x = xhi; v[2].y = ay; v[2].z = z1;
            v[2].u0 = 1.0f; v[2].v0 = -fn;
            v[2].u1 = 1.0f; v[2].v1 = 0.0f;
            v[3].x = xlo; v[3].y = ay; v[3].z = z1;
            v[3].u0 = 0.0f; v[3].v0 = -fn;
            v[3].u1 = 0.0f; v[3].v1 = 0.0f;
        } else {
            v[0].x = xhi; v[0].y = ay; v[0].z = z1;
            v[0].u0 = 1.0f; v[0].v0 = fn + vs;
            v[0].u1 = 1.0f; v[0].v1 = 1.0f;
            v[1].x = xlo; v[1].y = ay; v[1].z = z1;
            v[1].u0 = 0.0f; v[1].v0 = fn + vs;
            v[1].u1 = 0.0f; v[1].v1 = 1.0f;
            v[2].x = xhi; v[2].y = ay; v[2].z = z1 + len;
            v[2].u0 = 1.0f; v[2].v0 = fn;
            v[2].u1 = 1.0f; v[2].v1 = 0.0f;
            v[3].x = xlo; v[3].y = ay; v[3].z = z1 + len;
            v[3].u0 = 0.0f; v[3].v0 = fn;
            v[3].u1 = 0.0f; v[3].v1 = 0.0f;
        }
    }

    info->axis = (signed char)axis_;
    info->dir  = step_;
    info->n    = span_;
    info->len  = len;
    info->f    = f;
    return true;
}

/* ═══ Exports -- thin ABI shims; patch.py routes the three originals here ═
 */

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_UpdateBridgeObject(BridgeObject *self)
{
    self->tick();
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_SpawnBridgeObject(Game *self, unsigned int uArg, unsigned int vArg,
                      unsigned int heightArg, unsigned int slotArg,
                      unsigned int axisArg)
{
    BridgeObject::spawn(self, uArg, vArg, heightArg, slotArg, axisArg);
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PurgeBridgeObjects(Game *self)
{
    BridgeObject::purgeAll(self);
}
