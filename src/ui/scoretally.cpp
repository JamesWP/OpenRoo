/* Six stages, one per tally line (crystals, surplus crystals, foes, time, all
 * items, vitality).  Each counts its line up with the count sound over a time
 * proportional to its count, then moves on; Enter skips to the end.  Elapsed
 * time is whole milliseconds of the game clock since the stage began, compared
 * unsigned.
 *
 * PRESERVED:
 *   - Stages 0 and 1 go straight to the totals; stage 2 falls through to
 *     the stage 3 test, so finishing stage 2 runs stage 3 in the same call
 *     with the same elapsed time.
 *   - Stages 3..5 read the clock again for the next stage's start; 0..2 use
 *     the reading they began with.
 *   - Enter's skip is debounced on the Game's shared debounce byte.
 *
 * KAROO_SIM_FX=tallyfast is a negative control: every stage takes a tenth of
 * the time, so the frame at which the tally finishes moves. */

#include "inputdev.h"
#include <stdint.h>
#include "sysdev.h"
#include <string.h>
#include "audiodev.h"
#include "logger.h"
#include "game.h"
#include "scoretally.h"
#include "menutree.h"
#include "record.h"

static int s_fx = -1;

/* The clock truncated to whole milliseconds, low word. */
static unsigned int ftol_low(Game *game)
{
    double d = *game->clock();
    return (unsigned int)(long long)d;
}

static void tick_sound(Game *game)
{
    audiodev::Buffer *s = game->fixedSounds()->count;
    if (s != NULL)
        s->play(false);
}

  unsigned int  
Sim_AnimateScoreTallyStages(Game *self)
{
    unsigned int now, el, lim, n;
    unsigned char st;

    if (s_fx < 0) {
        char b[32];
        uint32_t k = sysdev::getEnv("KAROO_SIM_FX", b, sizeof(b));
        s_fx = (k > 0 && k < sizeof(b) && strcmp(b, "tallyfast") == 0);
        if (s_fx)
            g_logger.write("scoretally: KAROO_SIM_FX=tallyfast -- stages x0.1\n");
    }
#define LIM(x)  (s_fx ? (x) / 10 : (x))

    ScoreTally *T = self->tally();

    now = ftol_low(self);
    el  = now - (unsigned int)T->stageStart;
    st  = T->stage;

    if (st == 0) {
        n = (unsigned int)T->count[TALLY_GEMS];
        lim = LIM((n * 5 + 5) * 10);
        if (el >= lim) {
            T->stageStart = (int)now;
            T->stage = 1;
            T->shownCount[TALLY_GEMS] = (int)n;
            T->shownScore[TALLY_GEMS] = T->score[TALLY_GEMS];
        } else {
            tick_sound(self);
            T->shownCount[TALLY_GEMS] = (int)(el / 50);
            T->shownScore[TALLY_GEMS] = (int)((el / 50) * 5);
        }
        goto totals;
    }
    if (st == 1) {
        n = (unsigned int)T->count[TALLY_SURPLUS];
        lim = LIM((n * 5 + 5) * 10);
        if (el >= lim) {
            T->stageStart = (int)now;
            T->stage = 2;
            T->shownCount[TALLY_SURPLUS] = (int)n;
            T->shownScore[TALLY_SURPLUS] = T->score[TALLY_SURPLUS];
        } else {
            tick_sound(self);
            T->shownCount[TALLY_SURPLUS] = (int)(el / 50);
            T->shownScore[TALLY_SURPLUS] = (int)((el / 50) * 10);
        }
        goto totals;
    }
    if (st == 2) {
        n = (unsigned int)T->count[TALLY_FOES];
        lim = LIM((n * 5 + 5) * 20);
        if (el >= lim) {
            T->stageStart = (int)now;
            T->stage = 3;
            T->shownScore[TALLY_FOES] = T->score[TALLY_FOES];
            T->shownCount[TALLY_FOES] = (int)n;
        } else {
            tick_sound(self);
            T->shownCount[TALLY_FOES] = (int)(el / 100);
            T->shownScore[TALLY_FOES] = (int)((el / 100) * 50);
        }
    // PRESERVED: no jump; falls into the stage 3 test with the same elapsed
    // time.
    }

    st = T->stage;
    if (st == 3) {
        n = (unsigned int)T->count[TALLY_TIME];
        lim = LIM((n * 5 + 5) * 2);
        if (el >= lim) {
            T->stage = 4;
            T->stageStart = (int)ftol_low(self);
            T->shownCount[TALLY_TIME] = (int)n;
            T->shownScore[TALLY_TIME] = T->score[TALLY_TIME];
        } else {
            tick_sound(self);
            T->shownCount[TALLY_TIME] = (int)(el / 10);
            T->shownScore[TALLY_TIME] = (int)((el / 10) * 2);
        }
    } else if (st == 4) {
        n = (unsigned int)T->count[TALLY_ALLITEMS];
        lim = LIM((n * 5 + 5) * 20);
        if (el >= lim) {
            T->stage = 5;
            T->stageStart = (int)ftol_low(self);
            T->shownCount[TALLY_ALLITEMS] = (int)n;
            T->shownScore[TALLY_ALLITEMS] = T->score[TALLY_ALLITEMS];
        } else {
            tick_sound(self);
            T->shownCount[TALLY_ALLITEMS] = (int)(el / 100);
            T->shownScore[TALLY_ALLITEMS] = (int)((el / 100) * 5);
        }
    } else if (st == 5) {
        n = (unsigned int)T->count[TALLY_VITALITY];
        lim = LIM((n * 5 + 5) * 10);
        if (el >= lim) {
            T->stage = 6;
            T->stageStart = (int)ftol_low(self);
            T->shownCount[TALLY_VITALITY] = (int)n;
            T->shownScore[TALLY_VITALITY] = T->score[TALLY_VITALITY];
            self->setTallyDone(1);
        } else {
            tick_sound(self);
            T->shownCount[TALLY_VITALITY] = (int)(el / 50);
            T->shownScore[TALLY_VITALITY] = (int)(el / 50);  // PRESERVED: the same quotient into both
        }
    }

totals:
    {
        // Summed unsigned, in this order.
        unsigned int t = (unsigned int)T->shownScore[TALLY_TIME]
                       + (unsigned int)T->shownScore[TALLY_FOES]
                       + (unsigned int)T->shownScore[TALLY_GEMS]
                       + (unsigned int)T->shownScore[TALLY_SURPLUS]
                       + (unsigned int)T->shownScore[TALLY_ALLITEMS]
                       + (unsigned int)T->shownScore[TALLY_VITALITY];
        T->shownLevelTotal = (int)t;
        T->shownGrandTotal = (int)((unsigned int)T->shownBase + t);
        if (self->debounce() != inputdev::KEY_RETURN && input_key_down(inputdev::KEY_RETURN) != 0)
            self->setTallyDone(1);
        return (t & 0xffffff00u) | 0xffu;
    }
#undef LIM
}
