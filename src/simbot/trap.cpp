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
#include <algorithm>
#include <map>
#include <set>
#include "actions.h"
#include "routeattempt.h"
#include "survey.h"
#include <stdlib.h>
#include "world.h"

namespace simbot {

const Action &trapAction();

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

/* How far from its cell a trap's Route goes (steps), and the clock in its
 * Key (a short wait). */
const int TRAP_REACH = 1;
const double TRAP_CLOCK_MS = WAIT_TICKS * 1000.0 / 60;
/* Waits in a row for a foe to come on to a trap: 10 s. */
const int TRAP_WAITS = 40;
/* A trap's Routes: a search on the clock alone, no guide. */
const size_t TRAP_PLACES = 4000;
/* A trap's search takes longer, the foe coming on meanwhile: its lead
 * (ticks) from the first. */
const int TRAP_LEAD = 60;

/* foe (a Listed id) killed with no bomb, the player alive and at rest and
 * never more than a step or two from cell: the foe led onto a falling tile
 * as it drops. */
class TrapGoal : public Goal {
public:
    TrapGoal(Cell cell, unsigned foe) : Goal(cell), foe_(foe) {}
    bool reached(const Route::Node &n) const override
    {
        return foeDead(*n.world, foe_) && safe(*n.world);
    }
    /* No guide: what leads the foe on is found by playing. */
    double estimate(const Route::Node &, double) const override { return 0; }
    Route::Key key(const Route::Node &n, const Route::Key &) const override
    {
        // The cell, and the clock, to a short wait, in place of facing,
        // waited and the foe (about where it was set, where the foe is goes
        // by the clock), and which falling tiles are armed or down (the
        // first 32, folded into a Cell).
        const Cell clock{(int)(n.now / TRAP_CLOCK_MS), 0};
        uint64_t bits = 0;
        const auto &fl = n.world->fallings();
        for (size_t i = 0; i < fl.size() && i < 32; i++)
            bits |= (uint64_t)(fl[i].armed() ? 1 : 0) << (2 * i) |
                    (uint64_t)(fl[i].respawnPending() ? 1 : 0) << (2 * i + 1);
        Place at = n.place;
        at.facing = 0;   // which way it faces matters little there
        return {at, 0, carried(*n.world), true, clock,
                {(int)(uint32_t)bits, (int)(uint32_t)(bits >> 32)}};
    }
    /* A trap is played out about where it was set. */
    bool keeps(const Route::Node &n) override
    {
        return std::abs(n.place.u - cell.u) + std::abs(n.place.v - cell.v) <= TRAP_REACH;
    }
    bool done(const World &live) const override { return foeDead(live, foe_); }

private:
    unsigned foe_;
};

/* First a walk to the stand, then the trap searched from there. */
class TrapAttempt : public Attempt {
public:
    TrapAttempt(Cell stand, unsigned foe, const Survey &survey)
        : walk_(std::make_shared<Goal>(stand), survey),
          trap_(std::make_shared<TrapGoal>(stand, foe), survey, trapOptions()) {}
    static RouteOptions trapOptions()
    {
        RouteOptions o;
        o.places     = TRAP_PLACES;
        o.shortWaits = TRAP_WAITS;
        o.waitFirst  = true;
        o.lead       = TRAP_LEAD;
        return o;
    }
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

/* Where a foe on a cell may be trapped: a falling tile it comes to, and a
 * stand beside it, not a falling tile, that it comes to only over falling
 * tiles, by way of that one; with the foe's times to the tile and the stand
 * on the player's MoveGraph. */
struct Stand {
    Cell   tile, stand;
    double tileMs, standMs;
};

/* The trap's data for a level (Survey::actionData): the stands for a foe on
 * each cell, worked out the first time it is asked for. */
struct TrapData : Survey::ActionData {
    std::map<Cell, std::vector<Stand>> stands;
};

/* A stand is on the foe's way by its tile if the foe comes to it no more
 * than this after the tile (a step, or a step and a drop). */
const double ON_WAY_MS = 400;

const std::vector<Stand> &stands(const Survey &survey, Cell foe)
{
    auto &known = survey.actionData<TrapData>(trapAction()).stands;
    auto it = known.find(foe);
    if (it != known.end())
        return it->second;
    std::vector<Stand> &out = known[foe];
    const MoveGraph &g = survey.graph();
    const std::vector<Cell> &fallings_ = survey.fallings();
    if (fallings_.empty() || g.on(foe).empty())
        return out;
    auto falling = [&](Cell c) {
        return std::find(fallings_.begin(), fallings_.end(), c) != fallings_.end();
    };
    // The foe walks as the player would: its time to each cell, and the
    // cells it comes to without crossing a falling tile.
    const int from = g.on(foe).front();
    const MoveGraph::Table times = g.times(from);
    auto timeTo = [&](Cell c) {
        double best = -1;
        for (int p : g.on(c))
            if ((*times)[p] >= 0 && (best < 0 || (*times)[p] < best))
                best = (*times)[p];
        return best;
    };
    std::set<Cell> open;
    std::vector<int> stack{from};
    std::set<int> seen{from};
    while (!stack.empty()) {
        const int p = stack.back();
        stack.pop_back();
        open.insert(g.place(p).cell());
        for (const MoveGraph::Edge &e : g.edges(p))
            if (!falling(g.place(e.to).cell()) && seen.insert(e.to).second)
                stack.push_back(e.to);
    }
    std::vector<std::pair<double, Stand>> found;
    for (const Cell &t : fallings_) {
        const double toTile = timeTo(t);
        if (toTile < 0)
            continue;
        for (const Cell &n : {Cell{t.u + 1, t.v}, Cell{t.u - 1, t.v}, Cell{t.u, t.v + 1},
                              Cell{t.u, t.v - 1}}) {
            const double toStand = timeTo(n);
            // Just past the tile from the foe: its way there is by the tile.
            if (falling(n) || open.count(n) || toStand <= toTile || toStand > toTile + ON_WAY_MS)
                continue;
            found.push_back({toTile, {t, n, toTile, toStand}});
        }
    }
    std::stable_sort(found.begin(), found.end(),
                     [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &f : found)
        out.push_back(f.second);
    return out;
}

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
    for (const Stand &s : stands(survey, survey.seen((int)foe))) {
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
        return std::make_unique<TrapAttempt>(stand, survey.foes()[step.arg], survey);
    }
};

}  // namespace

const Action &trapAction()
{
    static const Trap a;
    return a;
}

}  // namespace simbot
