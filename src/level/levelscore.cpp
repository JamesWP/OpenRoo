/* Six components, each stored as a count (the tally's left column) and a score
 * (its right column); the tally animation counts each up afterwards.
 *   crystals   min(collected, needed) * 5
 *   surplus    (collected - needed) * 10, or 0
 *   foes       foes killed * 50
 *   time       (limit - elapsed s) * 2, only for a completed level
 *   all items  item total * 5, if every item was taken on a first attempt
 *   vitality   the clamped percentage, one point each
 * Water01 is a worked example: 75 + 0 + 0 + 92 + 75 + 78 = 320, and 1077 + 320
 * = 1397.
 *
 * PRESERVED:
 *   1. The crystal comparison is signed: surplus pays only when collected
 *      > needed.
 *   2. The time bonus carries a term that always vanishes:
 *      ((-q) << 31) - q + limit, times 2 in 32 bits, where q is the whole
 *      seconds elapsed.  It equals (limit - q) * 2 for every input.
 *   3. The all-items test is an unsigned 16-bit compare of two different
 *      counters, and is also denied on a restart.
 *
 * KAROO_SIM_FX=score is a negative control: every score doubles while its
 * count does not, so on the tally screen each row's count and score disagree
 * by exactly two times. */

#include "portable.h"
#include <stdint.h>
#include "sysdev.h"
#include "logger.h"
#include "game.h"
#include "tilequery.h"
#include "levelscore.h"
#include "player.h"

/* The counts, scores, totals and animation state are the Game's tally
 * (ScoreTally, game.h). */

static int s_fxDouble = -1;

static int fx_double(void)
{
    if (s_fxDouble < 0) {
        char buf[32];
        uint32_t n = sysdev::getEnv("KAROO_SIM_FX", buf, sizeof(buf));
        s_fxDouble = (n > 0 && n < sizeof(buf) && lstrcmpiA(buf, "score") == 0);
        if (s_fxDouble)
            g_logger.write("levelscore: KAROO_SIM_FX=score -- scores doubled, counts left alone\n");
    }
    return s_fxDouble;
}

/* The level report's type census (tilequery.cpp). */

  void  
Score_CalculateLevelScore(Game *self, char endReason)
{
    // KAROO_TILEQ_DIAG=1 only, read-only: the census of foe types this level
    // carries.
    tilequery_census_object_types(self);

    // Crystals and surplus.
    const int collected = self->player()->gemsCollected();
    const int quota     = self->gemsRequired();

    ScoreTally *t = self->tally();

    if (collected > quota) {
        t->score[TALLY_GEMS]    = quota * 5;
        t->count[TALLY_GEMS]    = quota;
        t->count[TALLY_SURPLUS] = collected - quota;
        t->score[TALLY_SURPLUS] = collected * 10 - quota * 10;  // collected * 10 - needed * 10, as written
    } else {
        t->score[TALLY_GEMS]    = collected * 5;
        t->count[TALLY_GEMS]    = collected;
        t->score[TALLY_SURPLUS] = 0;
        t->count[TALLY_SURPLUS] = 0;
    }

    // Foes.
    const unsigned foes = self->foesKilled();
    t->score[TALLY_FOES] = (int)(foes * 50u);
    t->count[TALLY_FOES] = (int)foes;

    // Time, only when the level ended at the exit.
    if (endReason == 3) {
        const unsigned q = self->timeElapsed() / 1000u;
        const unsigned limit = (unsigned)self->timeLimit();
        // PRESERVED: the ((-q) << 31) term is shifted away by the * 2.
        const unsigned acc = ((0u - q) << 31) - q + limit;
        t->score[TALLY_TIME] = (int)(acc * 2u);
        t->count[TALLY_TIME] = (int)(limit - q);
    } else {
        t->score[TALLY_TIME] = 0;
        t->count[TALLY_TIME] = 0;
    }

    // All items.
    const unsigned short itemTotal = self->itemTotal();
    if (itemTotal > self->player()->itemsCollected() || self->restartCount() != 0) {
        t->score[TALLY_ALLITEMS] = 0;
        t->count[TALLY_ALLITEMS] = 0;
    } else {
        t->count[TALLY_ALLITEMS] = (int)itemTotal;
        t->score[TALLY_ALLITEMS] = (int)(itemTotal * 5u);
    }

    // Vitality: the clamped percentage, a point each.
    const unsigned vitality = self->vitalityPercent();
    t->shownScore[TALLY_GEMS] = 0;  // zeroed here, before the rest
    t->score[TALLY_VITALITY]  = (int)vitality;
    t->count[TALLY_VITALITY]  = (int)vitality;

    if (fx_double())
        for (int r = 0; r < TALLY_ROWS; r++)
            t->score[r] *= 2;

    // The totals, summed in this order.  The game adds the vitality value
    // still in a register rather than re-reading the score; they are the same
    // number.
    const int total = t->score[TALLY_ALLITEMS] + t->score[TALLY_FOES]
                    + t->score[TALLY_SURPLUS]  + t->score[TALLY_VITALITY]
                    + t->score[TALLY_TIME]     + t->score[TALLY_GEMS];

    const int running = self->player()->score();
    t->levelTotal = total;
    t->grandTotal = total + running;

    // The other eleven shown cells, in this order.
    for (int r = TALLY_SURPLUS; r < TALLY_ROWS; r++)
        t->shownScore[r] = 0;
    for (int r = 0; r < TALLY_ROWS; r++)
        t->shownCount[r] = 0;
    t->shownLevelTotal = 0;

    t->shownBase       = running;
    t->shownGrandTotal = running;
    self->player()->setScore(total + running);

    // Hand the tally animation stage 0 and its start time.
    t->stage      = 0;
    t->stageStart = (int)*self->clock();
    self->setTallyDone(1);
}
