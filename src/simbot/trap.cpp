/* trap: lead a foe onto a falling tile that drops under it.
 *   pre:  the foe alive; a falling tile the foe comes to (on the player's
 *         MoveGraph), with a cell beside it the foe and the player come to,
 *         the foe only over falling tiles (so it comes over them, and if
 *         the trap fails the player is not shut in with it)
 *   eff:  the foe dead, the player at the stand: a cell beside the falling
 *         tile nearest where the foe was seen
 *   do:   a walk to the stand, then a RouteAttempt with the trap as its goal
 *         (routing.h): walking and waiting, played on copies, foe and tiles
 *         and all, till one has the foe dead.  A foe on the move crosses a falling tile before it
 *         drops; one stopped on it, its way on dropped first, falls.
 * Its cost is a guess past the walk: how the foe comes on only playing
 * says. */
#include <stdio.h>
#include "actions.h"
#include "routeattempt.h"
#include "survey.h"

namespace simbot {

namespace {

/* What a trap is reckoned to take past the walk to the tile. */
const double TRAP_MS = 6000;
/* A foe's step against the player's (foe.cpp: the quicker foes', 500 ms),
 * for its time on the player's MoveGraph; and how much sooner than the foe
 * the player must come to the stand. */
const double FOE_SLOWER = 500.0 / 200.0;
const double AHEAD_MS = 2000;
/* How far a foe is led: its time to the tile (on the player's MoveGraph) at
 * most this, so the trap is played out in a few seconds (TRAP_MS). */
const double LURE_MS = 6000;

/* First a walk to the stand, then the trap searched from there. */
class TrapAttempt : public Attempt {
public:
    TrapAttempt(Cell stand, Goal trap, const Survey &survey)
        : walk_(Goal{stand}, survey), trap_(trap, survey) {}
    Status act(const World &live, Work &work, unsigned *presses) override
    {
        if (!there_) {
            const Status s = walk_.act(live, work, presses);
            if (s != Done) {
                why_ = walk_.why();
                return s;
            }
            there_ = true;
        }
        const Status s = trap_.act(live, work, presses);
        why_ = trap_.why();
        return s;
    }
    std::string why() const override { return why_; }

private:
    RouteAttempt walk_, trap_;
    bool there_ = false;
    std::string why_;
};

/* The place the player stands to lead foe (a Survey index) onto a falling
 * tile, from f.at, coming there well before the foe; its ms and the glides
 * it uses.  -1 if none. */
int trapStand(const Facts &f, const Survey &survey, size_t foe, double *ms, int *used)
{
    if (f.at < 0 || !(f.alive >> foe & 1))
        return -1;
    // Of the stands by the tiles the foe comes to soonest, the one the
    // player comes to soonest, well before the foe.
    int to = -1;
    double tile = -1;
    for (const Survey::Stand &s : survey.stands(survey.seen((int)foe))) {
        if ((to >= 0 && s.tileMs > tile) || s.tileMs > LURE_MS)
            break;
        double m;
        int u;
        const int p = survey.graph().arrive(f.at, s.stand, &m, f.glides, &u);
        if (p < 0 || m + AHEAD_MS > FOE_SLOWER * s.standMs || (to >= 0 && m >= *ms))
            continue;
        to = p;
        *ms = m;
        *used = u;
        tile = s.tileMs;
    }
    return to;
}

class Trap : public Action {
public:
    const char *name() const override { return "trap"; }

    void offer(const Facts &f, const Survey &survey, std::vector<Step> &out,
               int /*only*/) const override
    {
        if (f.done || f.at < 0 || survey.fallings().empty())
            return;
        const MoveGraph &g = survey.graph();
        for (size_t i = 0; i < survey.foes().size(); i++) {
            if (!(f.alive >> i & 1))
                continue;
            double ms;
            int used;
            const int to = trapStand(f, survey, i, &ms, &used);
            if (to < 0)
                continue;
            Step s;
            s.action = this;
            s.arg    = (int)i;
            s.after  = f;
            s.after.at = to;
            s.after.alive &= ~((uint64_t)1 << i);
            s.after.glides -= used;
            s.ms = ms + TRAP_MS;
            const Cell c = survey.seen((int)i), at = g.place(to).cell();
            char buf[64];
            snprintf(buf, sizeof(buf), "trap f%zu (%d,%d) from (%d,%d)", i, c.u, c.v, at.u, at.v);
            s.what = buf;
            out.push_back(std::move(s));
        }
    }

    std::unique_ptr<Attempt> attempt(const Step &step, const Survey &survey) const override
    {
        const Cell stand = survey.graph().place(step.after.at).cell();
        Goal g;
        g.cell = stand;
        g.trap = true;
        g.foe  = survey.foes()[step.arg];
        return std::make_unique<TrapAttempt>(stand, g, survey);
    }
};

}  // namespace

const Action &trapAction()
{
    static const Trap a;
    return a;
}

}  // namespace simbot
