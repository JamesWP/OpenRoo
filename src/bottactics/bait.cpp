/* Bait(c): with a thrower about that bombs the player, the player standing
 * on c, where the thrower's bomb lands (board.h, baits): the blocks it spends,
 * no bomb of the player's.  Its cost is a guess: when the thrower comes up
 * only the foe model says.
 * Done in three parts: the way to c (an Approach: set off after a wait, or
 * by way of a cell near the thrower to draw it off, or leading it there,
 * throwers.h); a wait on c for the bomb the foe model has the thrower drop;
 * and a walk clear of it the moment it is dropped.  Taken only where the foe
 * model has a bomb spend a block and the player stays safe through its
 * blast (danger.h).  With such a bomb dropped already, the walk clear of it.
 * With none to be had yet, a wait on c, the thrower following. */
#include "action.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdio.h>
#include "danger.h"
#include "rules.h"
#include "throwers.h"
#include "walk.h"

namespace bot {

namespace {

/* What the planner takes a bait to cost, past the walk: the thrower's coming
 * and the blast. */
const double BAIT_MS = 10000;
/* How long the player waits on the cell for the thrower's bomb, at most. */
const double WAIT_MS = 15000;
/* The cells to draw a thrower off from: this many cells from it, the nearest
 * LURES of them, with these waits there (or led on from there). */
const int    LURE_NEAR = 3;
const size_t LURES = 6;
const double LURE_WAITS[] = {0, 2000};
/* The waits before setting off for the bait, not drawing a thrower first. */
const double SET_OFF_WAITS[] = {0, 1500, 3000, 5000};
/* A thrower come up may be baited from a cell this near the bait instead. */
const int    BESIDE = 2;
/* The cells to walk away to: the nearest RETREATS (walk.h). */
const size_t RETREATS = 24;
/* With no bait to be had yet, the player waits on the bait cell this long,
 * drawing a thrower on, where the foe model has one come within DRAW_NEAR
 * of it in DRAW_COME_MS. */
const double DRAW_MS = 800;
const double DRAW_COME_MS = 40000;
const int    DRAW_NEAR = 2;
/* A teleporter's tile kind (TILE_TELEPORTER, tile.h). */
const int TELEPORTER = 0x0f;

/* before, with a walk from at, startMs from now, to the nearest cell where
 * the player is safe through blast, and a wait until it has blown. */
bool clearOf(Situation &s, const Option &before, Step at, int facing, double startMs,
             const Blast &blast, Option *out)
{
    const BotView &view = s.view;
    const RoutesOn away(s.nav, before.schedule, at, facing, startMs, Bound{RETREAT_MS});
    for (const Step &to : retreatCells(view, away.paths(), RETREATS)) {
        Option o = before;
        Option walk;
        if (!walkTo(s, away.paths(), to, 0, walk))
            continue;
        append(o, walk);
        const double done = blast.until + CLEAR_MARGIN_MS;
        o.script.push_back({ScriptStep::Wait, 0, {}, done});
        o.lingerMs = std::max(1000.0, done - view.now - o.schedule.back().t);
        if (checkEscape(s.foes, view, o.schedule, o.lingerMs).hit)
            continue;
        o.checked = true;
        o.stepMs  = done - view.now;
        o.cost    = o.stepMs;
        char buf[48];
        snprintf(buf, sizeof(buf), ", away to (%d,%d)", to.u, to.v);
        o.what += buf;
        *out = std::move(o);
        return true;
    }
    return false;
}

/* A way to come to the bait cell: from where the player stands, or from a
 * cell near a thrower (from), after a wait there, or leading it on. */
struct Approach {
    Step   from{-1, -1};
    double wait = 0;
    bool   led = false;

    bool drawn() const { return from.u >= 0; }
    /* A thrower come up by the end may stand where its bomb at a cell beside
     * the bait spends the block instead. */
    bool beside() const { return led || (!drawn() && wait == 0); }
};

/* The approaches, in the order tried: setting off, then by each cell near a
 * thrower, nearest first. */
std::vector<Approach> approaches(const Situation &s)
{
    const BotView &view = s.view;
    std::vector<Approach> out;
    for (const double wait : SET_OFF_WAITS)
        out.push_back({{-1, -1}, wait, false});
    for (const auto &[ms, c] : nearCells(view, s.from, INFINITY, LURES, [&](Step c) {
             return std::any_of(view.foes.begin(), view.foes.end(), [&](const FoeView &f) {
                 return f.kind == 3 && !f.held && chebyshev({f.u, f.v}, c) == LURE_NEAR;
             });
         })) {
        for (const double wait : LURE_WAITS)
            out.push_back({c, wait, false});
        out.push_back({c, 0, true});
    }
    return out;
}

/* The walk to the bait cell by the approach a. */
bool comeTo(Situation &s, const Approach &a, Step bait, Option &o)
{
    const BotView &view = s.view;
    const Step from = a.drawn() ? a.from : Step{view.player.u, view.player.v};
    if (!walkTo(s, s.from, from, 0, o))
        return false;
    waitTill(view, o, o.cost + a.wait);
    if (a.led)
        return leadThrower(s, o, from, bait);
    return (from.u == bait.u && from.v == bait.v) || walkOn(s, s.nav, o, from, bait);
}

/* A walk on from going, which ends on the bait cell, to a cell near it where
 * the foe model has the thrower's bomb spend a block, taken. */
template <typename Take>
bool besideBait(Situation &s, const Option &going, Step bait, Take &take)
{
    const BotView &view = s.view;
    const RoutesOn routes(s.nav, going.schedule, bait, facingAfter(view.player.facing, going.script),
                          going.cost);
    for (const auto &[ms, c] : nearCells(view, routes.paths(), INFINITY, 24, [&](Step c) {
             return chebyshev(c, bait) <= BESIDE && view.cell(c.u, c.v).kind != TELEPORTER;
         })) {
        Option o = going;
        Blast blast;
        if (walkOn(s, s.nav, o, bait, c) && baitedBomb(s, o, WAIT_MS, &blast) && take(o, c, blast))
            return true;
    }
    return false;
}

class BaitAction : public Action {
public:
    const char *name() const override { return "bait"; }

    /* The cheapest bait cell for each set of blocks. */
    void expand(const Facts &f, double, const Board &board, const PlanOptions &options,
                std::vector<Successor> &out) const override
    {
        if ((f.changes & board.allBlocks()) == board.allBlocks())
            return;
        std::map<Changes, std::pair<double, Place>> best;
        for (const Site &site : board.baits(f.changes, f.throwerAt)) {
            const double t = board.time(f.changes, f.at, {site.cell.u, site.cell.v});
            if (t < 0)
                continue;
            // The thrower comes up as well.
            const double ms = t + BAIT_MS + site.far * THROWER_STEP_MS;
            auto it = best.find(site.hits);
            if (it == best.end() || ms < it->second.first)
                best[site.hits] = {ms, site.cell};
        }
        for (const auto &[hits, c] : best) {
            Successor n{f, {}};
            n.facts.changes |= hits;
            if (!board.worlds().has(n.facts.changes) && board.worlds().size() > options.maxWorlds)
                continue;
            n.facts.at = c.second;
            n.step.end   = c.second;
            n.step.heads = {c.second.u, c.second.v};
            n.step.ms    = c.first;
            out.push_back(n);
        }
    }

    void offer(Situation &s, const Board &, const Plan &plan,
               std::vector<Option> &out) const override
    {
        const BotView &view = s.view;
        const PlayerView &p = view.player;
        if (p.falling)
            return;
        const Step here{p.u, p.v};
        const Step bait{plan.steps.front().end.u, plan.steps.front().end.v};
        // Dropped already: clear of it until it blows.
        for (const BombView &b : view.bombs) {
            const Blast blast = predictBomb(view, b);
            if (!spendsBlock(view, blast))
                continue;
            Option before;
            before.what = "clear of a thrower's bomb";
            before.schedule.push_back({0, here});
            Option o;
            if (clearOf(s, before, here, p.facing, s.from.startMs(), blast, &o))
                out.push_back(std::move(o));
            return;
        }
        // On the cell till the bomb is dropped, then clear of it.
        auto take = [&](const Option &on, Step stand, const Blast &blast) {
            Option o = on;
            waitTill(view, o, blast.from - FUSE_MS - view.now + view.tickMs);
            Option cleared;
            if (!clearOf(s, o, stand, facingAfter(p.facing, o.script), o.cost, blast, &cleared))
                return false;
            out.push_back(std::move(cleared));
            return true;
        };
        for (const Approach &a : approaches(s)) {
            Option going;
            if (!comeTo(s, a, bait, going))
                continue;
            char buf[96];
            if (a.drawn())
                snprintf(buf, sizeof(buf), "bait at (%d,%d), %s from (%d,%d) after %.0f ms", bait.u,
                         bait.v, a.led ? "led" : "drawn", a.from.u, a.from.v, a.wait);
            else
                snprintf(buf, sizeof(buf), "bait at (%d,%d) after %.0f ms", bait.u, bait.v, a.wait);
            going.what = buf;
            Blast blast;
            if (baitedBomb(s, going, WAIT_MS, &blast) && take(going, bait, blast))
                return;
            if (a.beside() && besideBait(s, going, bait, take))
                return;
        }
        drawTo(s, bait, out);
    }

private:
    /* None yet: on to the bait cell and a wait there, the thrower following
     * (as send.cpp), where the foe model has it come up. */
    static void drawTo(Situation &s, Step bait, std::vector<Option> &out)
    {
        const BotView &view = s.view;
        Option o;
        if (!walkTo(s, s.from, bait, 0, o))
            return;
        Option coming = o;
        if (!drawThrower(s, coming, bait, DRAW_NEAR, DRAW_COME_MS))
            return;
        waitTill(view, o, o.cost + DRAW_MS);
        o.lingerMs = DRAW_MS;
        if (checkPlan(s.foes, view, o.schedule, o.lingerMs).hit)
            return;
        // Only a start on the bait: its time is still the planner's guess.
        o.checked = true;
        char buf[80];
        snprintf(buf, sizeof(buf), "draw a thrower to bait at (%d,%d)", bait.u, bait.v);
        o.what = buf;
        out.push_back(std::move(o));
    }
};

}  // namespace

const Action &baitAction()
{
    static const BaitAction action;
    return action;
}

}  // namespace bot
