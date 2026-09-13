/* GAMETICK_PLAN.md Band A — Game::CalculateLevelScore (0x0041a760).
 *
 * The end-of-level tally.  One of only two genuine leaves in Band A: its sole
 * callee is the CRT's __ftol, so this replacement calls nothing in the game
 * binary at all (see below).  `RET 4` __thiscall, one stack argument.
 *
 * EIGHT call sites, all E8: four in GameTick's end-of-level branches, two in
 * HandleTypedCheatCode, and two in WriteLevelReport -- which is what makes
 * tools/levelreport.py an 80-level acceptance test for this function rather
 * than a smoke test.
 *
 * ─── Why this one first ──────────────────────────────────────────────────
 *
 * GAMETICK_PLAN.md lists seven Band A targets as "leaf simulators (no game
 * callees of their own)".  Checked against Ghidra's callee lists, only two of
 * the seven actually are:
 *
 *   CalculateLevelScore  0x41a760   __ftol                          LEAF
 *   UpdateBreakableTile  0x403d40   Set3DPosition, TriggerPlayback  LEAF (ours)
 *   UpdateBombFuseAndBlast 0x402870 UpdateEntityMovement 0x438770   NOT a leaf
 *   UpdatePlayerTileEffects 0x41fcb0 UpdateEntityMovement + 9 more  NOT a leaf
 *   SetFoeChaseTarget    0x43a9d0   GetTurnedDirection, FUN_00401c20 NOT a leaf
 *   SpawnFoeObject / RemoveFoeObject / Spawn+RemoveBombObject       (untested)
 *
 * UpdateEntityMovement 0x00438770 is the real gate: it is the movement
 * integrator every entity tick ends in, it is not ours, and the no-callback
 * rule forbids calling it from a replacement.  It has to be replaced before
 * the three entity ticks above can be.  See GAMETICK_PLAN.md, Band A.
 *
 * ─── No calls into the game binary ───────────────────────────────────────
 *
 * The original's one call is `CALL 0x00451134` (__ftol) at 0x0041a951,
 * converting the double at Game+0x170a54 that was loaded by the FLD at
 * 0x0041a8cd.  That is the CRT, not game logic, and a C cast reproduces it:
 * __ftol truncates toward zero, which is what (int)(double) does.  Nothing
 * else is called.  Every other access is a plain read or write of the Game
 * object the caller hands us.
 *
 * ─── The score, as the disassembly has it ────────────────────────────────
 *
 * Six components, each stored twice: a COUNT (what the tally screen shows in
 * its left column) and a SCORE (the right column).  AnimateScoreTallyStages
 * 0x0041a970 counts each one up from zero afterwards.
 *
 *   component   count at   score at   value
 *   gems        0x14051e   0x140502   min(collected, quota) * 5
 *   surplus     0x140522   0x140506   (collected - quota) * 10, or 0
 *   foes        0x14052a   0x14050e   foesKilled * 50
 *   time        0x140526   0x14050a   (limit - elapsed_s) * 2, endReason 3 only
 *   allItems    0x14052e   0x140512   itemTotal * 5, if earned
 *   vitality    0x140532   0x140516   the clamped percentage, 1 point each
 *
 * Water01 is the worked example the manifest already records:
 * 75 + 0 + 0 + 92 + 75 + 78 = 320, and 1077 + 320 = 1397.
 *
 * ─── Three things that are easy to get wrong ─────────────────────────────
 *
 * 1. THE GEM COMPARISON IS SIGNED AND ITS OPERANDS ARE THE OTHER WAY ROUND
 *    from what the field names suggest.  `CMP EDX,EAX / JLE` with EDX =
 *    [0x175406] and EAX = [0x2ab723] takes the surplus branch only when
 *    0x175406 > 0x2ab723, so 0x175406 is what the player COLLECTED and
 *    0x2ab723 is the level's QUOTA.  Collecting past the quota pays 10 a gem
 *    instead of 5.  Both are read as signed int.
 *
 * 2. THE TIME BONUS CARRIES A DEAD OVERFLOW TERM.  The original computes
 *
 *        q   = elapsed_ms / 1000            (unsigned magic-number divide)
 *        acc = ((-q) << 31) - q + limit
 *        bonus = acc * 2
 *
 *    `(-q) << 31` keeps only bit 0 of q, in bit 31 — and the final `* 2`
 *    shifts that bit out of a 32-bit word entirely, so it always contributes
 *    nothing.  It is reproduced verbatim in uint32 arithmetic rather than
 *    simplified to (limit - q) * 2: the two agree for every input, but the
 *    literal form is what the binary does and it costs nothing to keep.
 *
 * 3. THE ALL-ITEMS TEST IS AN UNSIGNED WORD COMPARE (`JA`), and it is a
 *    comparison of two DIFFERENT counters, 0x42250 against 0x1753e3.  The
 *    bonus is denied when 0x42250 is above 0x1753e3, or when the byte at
 *    0x4220b is set.  Read as u16; a signed compare differs above 0x7fff.
 *
 * ─── Visual proof ────────────────────────────────────────────────────────
 *
 * KAROO_SIM_FX=score doubles every component's SCORE while leaving its COUNT
 * alone, so the tally screen shows a count and a score that disagree by
 * exactly 2x on all six rows — a measurement, not a colour, and one only this
 * code path can produce.  Reach it by finishing a level (water01 does).
 */
#include <windows.h>
#include "log.h"
#include "game.h"
#include "player.h"

/* Inputs */
#define OFF_FOES_KILLED 0x4224d    /* byte                                  */
#define OFF_TIME_LIMIT  0x2ab591   /* int   seconds                         */
#define OFF_TIME_ELAPSED 0x2ab595  /* uint  milliseconds                    */
#define OFF_ITEM_TOTAL  0x42250    /* u16                                   */
#define OFF_NO_BONUS    0x4220b    /* byte  set = all-items bonus denied     */
#define OFF_CLOCK_MS    0x170a54   /* double                                 */

/* Counts */
#define OFF_C_GEMS      0x14051e
#define OFF_C_SURPLUS   0x140522
#define OFF_C_TIME      0x140526
#define OFF_C_FOES      0x14052a
#define OFF_C_ALLITEMS  0x14052e
#define OFF_C_VITALITY  0x140532

/* Scores */
#define OFF_S_SURPLUS   0x140506
#define OFF_S_TIME      0x14050a
#define OFF_S_FOES      0x14050e
#define OFF_S_ALLITEMS  0x140512

/* Tally output and animation state */
#define OFF_LEVEL_TOTAL 0x140536
#define OFF_GRAND_TOTAL 0x14053a
#define OFF_STAGE       0x14053e   /* byte, AnimateScoreTallyStages' state  */
#define OFF_STAGE_TIME  0x14053f   /* int   ms, stage start timestamp       */
#define OFF_TALLY_BASE  0x1404d9
#define OFF_TALLY_TOTAL 0x1404f9
#define OFF_DONE_FLAG   0x517909

/* The eleven running-tally accumulators the animation counts up into. */
/* The twelve zeroed after the totals.  0x1404c1 is NOT here: the original
 * zeroes it earlier, alongside the vitality stores, and the order is kept. */
static const unsigned s_tallyZero[] = {
    0x1404c5, 0x1404c9, 0x1404cd, 0x1404d1, 0x1404d5,
    0x1404dd, 0x1404e1, 0x1404e5, 0x1404e9, 0x1404ed, 0x1404f1, 0x1404f5,
};

#define I32(base, off)  (*(int *)((char *)(base) + (off)))
#define U32(base, off)  (*(unsigned *)((char *)(base) + (off)))
#define U16(base, off)  (*(unsigned short *)((char *)(base) + (off)))
#define U8(base, off)   (*(unsigned char *)((char *)(base) + (off)))
#define GAME            ((Game *)(self))

static int s_fxDouble = -1;

static int fx_double(void)
{
    if (s_fxDouble < 0) {
        char buf[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", buf, sizeof(buf));
        s_fxDouble = (n > 0 && n < sizeof(buf) && lstrcmpiA(buf, "score") == 0);
        if (s_fxDouble)
            log_write("levelscore: KAROO_SIM_FX=score -- scores doubled, counts left alone\n");
    }
    return s_fxDouble;
}

/* karoo-hooks/tilequery.cpp -- read-only coverage census, KAROO_TILEQ_DIAG=1. */
extern "C" void tilequery_census_object_types(void *self);

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Score_CalculateLevelScore(void *self, char endReason)
{
    /* KAROO_TILEQ_DIAG=1 only, and read-only: censuses the object +0x62
     * types this level carries, so `tools/levelreport.py` can name the
     * levels that reach tilequery.cpp's type-gated call sites.  See the
     * census note at the bottom of karoo-hooks/tilequery.cpp. */
    tilequery_census_object_types(self);

    /* ── gems and surplus ────────────────────────────────────────────── */
    const int collected = ((Game *)self)->player()->gemsCollected();
    const int quota     = ((Game *)self)->gemsRequired();

    if (collected > quota) {
        GAME->setGemsScore(quota * 5);
        I32(self, OFF_C_GEMS)    = quota;
        I32(self, OFF_C_SURPLUS) = collected - quota;
        /* the original computes collected*10 - quota*10, not (c-q)*10 */
        I32(self, OFF_S_SURPLUS) = collected * 10 - quota * 10;
    } else {
        GAME->setGemsScore(collected * 5);
        I32(self, OFF_C_GEMS)    = collected;
        I32(self, OFF_S_SURPLUS) = 0;
        I32(self, OFF_C_SURPLUS) = 0;
    }

    /* ── foes ────────────────────────────────────────────────────────── */
    const unsigned foes = U8(self, OFF_FOES_KILLED);
    I32(self, OFF_S_FOES) = (int)(foes * 50u);
    I32(self, OFF_C_FOES) = (int)foes;

    /* ── time, only when the level ended by reaching the exit ────────── */
    if (endReason == 3) {
        const unsigned q = U32(self, OFF_TIME_ELAPSED) / 1000u;   /* magic divide */
        const unsigned limit = U32(self, OFF_TIME_LIMIT);
        /* defect 2: the ((-q) << 31) term is shifted away by the * 2 */
        const unsigned acc = ((0u - q) << 31) - q + limit;
        I32(self, OFF_S_TIME) = (int)(acc * 2u);
        I32(self, OFF_C_TIME) = (int)(limit - q);
    } else {
        I32(self, OFF_S_TIME) = 0;
        I32(self, OFF_C_TIME) = 0;
    }

    /* ── all-items bonus ─────────────────────────────────────────────── */
    const unsigned short itemTotal = U16(self, OFF_ITEM_TOTAL);
    if (itemTotal > ((Game *)self)->player()->field21a() || U8(self, OFF_NO_BONUS) != 0) {
        I32(self, OFF_S_ALLITEMS) = 0;
        I32(self, OFF_C_ALLITEMS) = 0;
    } else {
        I32(self, OFF_C_ALLITEMS) = (int)itemTotal;
        I32(self, OFF_S_ALLITEMS) = (int)(itemTotal * 5u);
    }

    /* ── vitality: the clamped percentage scores one point each ──────── */
    const unsigned vitality = GAME->vitalityPercent();
    U32(self, 0x1404c1)      = 0;          /* zeroed here, before the rest */
    GAME->setVitalityScore((int)vitality);
    I32(self, OFF_C_VITALITY) = (int)vitality;

    if (fx_double()) {
        GAME->setGemsScore(GAME->gemsScore() * 2);
        I32(self, OFF_S_SURPLUS)  *= 2;
        I32(self, OFF_S_FOES)     *= 2;
        I32(self, OFF_S_TIME)     *= 2;
        I32(self, OFF_S_ALLITEMS) *= 2;
        GAME->setVitalityScore(GAME->vitalityScore() * 2);
    }

    /* ── totals.  Summed in the original's order.
     *
     * The original adds the vitality byte it still has in EAX rather than
     * re-reading the vitality score; the two are the same value, so summing the
     * score fields is bit-identical and keeps the FX mode consistent. */
    const int total = I32(self, OFF_S_ALLITEMS) + I32(self, OFF_S_FOES)
                    + I32(self, OFF_S_SURPLUS)  + GAME->vitalityScore()
                    + I32(self, OFF_S_TIME)     + GAME->gemsScore();

    const int running = ((Game *)self)->player()->field22c();
    I32(self, OFF_LEVEL_TOTAL) = total;
    I32(self, OFF_GRAND_TOTAL) = total + running;

    for (unsigned i = 0; i < sizeof(s_tallyZero) / sizeof(s_tallyZero[0]); i++)
        U32(self, s_tallyZero[i]) = 0;

    I32(self, OFF_TALLY_BASE)  = running;
    I32(self, OFF_TALLY_TOTAL) = running;
    ((Game *)self)->player()->setField22c(total + running);

    /* ── hand the tally animation its stage 0 and start timestamp ────── */
    U8(self, OFF_STAGE) = 0;
    I32(self, OFF_STAGE_TIME) = (int)(*(double *)((char *)self + OFF_CLOCK_MS));
    U32(self, OFF_DONE_FLAG) = 1;
}
