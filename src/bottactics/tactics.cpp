#include "tactics.h"
#include <algorithm>
#include <memory>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include "danger.h"
#include "action.h"
#include "liftmodel.h"
#include "rules.h"
#include "walk.h"

namespace bot {

namespace {

/* Cells within this many ms are offered to evade to, the nearest EVADES. */
const double EVADE_MS = 1500;
const size_t EVADES = 12;
/* The routes go round a thrower the player could come to within this many
 * ms: one further off has moved on by then (nav.h). */
const double THROWER_NEAR_MS = 2000;
/* The first steps choose checks at most. */
const int MAX_CHECKS = 8;
/* How many times realise offers a step again round the throwers in the way. */
const int ROUNDS = 3;

}  // namespace

bool pick(Situation &s, std::vector<Option> &options, Option *out, Tally *tally,
          std::vector<Step> *blocked)
{
    std::stable_sort(options.begin(), options.end(),
                     [](const Option &a, const Option &b) { return a.cost < b.cost; });
    bool found = false;
    for (Option &o : options) {
        tally->offered++;
        if (!o.checked) {
            const Danger d = checkPlan(s.foes, s.view, o.schedule, o.lingerMs, o.blasts);
            if (d.hit) {
                if (blocked && std::string(d.why) == "blocked" &&
                    std::none_of(blocked->begin(), blocked->end(),
                                 [&](Step c) { return c.u == d.at.u && c.v == d.at.v; }))
                    blocked->push_back(d.at);
                continue;
            }
        }
        tally->safe++;
        if (!found) {
            *out  = std::move(o);
            found = true;
        }
        if (!tally->all)
            break;
    }
    return found;
}

namespace {

/* How long a wait on the cell, come to rest on it arriveMs from the view's
 * clock, must stay safe: on a lift, which carries the player off to whatever
 * waits at its other end, a whole cycle of it (liftmodel.h); on a cell every
 * way off which is onto a lift or a platform, as long again as the longest
 * wait for one: the way on may be the one the player waits for. */
double lingerOn(const Situation &s, Step c, double arriveMs, double linger)
{
    const LiftView *lift = liftAt(s.view, c.u, c.v);
    if (lift)
        linger = std::max(linger, liftCycleMs(*lift));
    double longest = 0;
    for (int d = 1; d <= 4; d++) {
        double start;
        const Outcome o = s.nav.stepWhenOpen(c.u, c.v, d, d, arriveMs, &start);
        if (o.kind != Outcome::Rest || o.cells.empty())
            continue;
        const Step to = o.cells.front();
        if (!NavModel::moving(s.view.cell(to.u, to.v)))
            return linger;
        longest = std::max(longest, start - arriveMs);
    }
    return linger + longest;
}

/* A teleporter's hold before it sends the player, and after. */
const double TELEPORT_HOLD_MS = 500.0;

/* How long the player is busy yet: the move under way, and the fall into
 * the cell it is stepping to, if that is lower than the player is. */
double busy(const BotView &v)
{
    const PlayerView &p = v.player;
    if (p.launched) {
        // Up the arc to the pad's top, from where it steps on.
        const Cell &pad = v.cell(p.u, p.v);
        return std::max(0.0, p.launchedAt + NavModel::jumpMs(pad.jumpTop - pad.height, v.tickMs) - v.now);
    }
    if (p.teleport) {
        // Each hold is half a second (movableentity.cpp); then it steps on.
        const double holds = p.teleport == 1 ? 2 * TELEPORT_HOLD_MS : TELEPORT_HOLD_MS;
        return std::max(0.0, p.teleportAt + holds - v.now);
    }
    if (!p.moving)
        return 0;
    double ms = std::max(0.0, p.animStart + p.animMs - v.now);
    // Onto stairs (kinds 5 to 8) the step has taken the player down a
    // height already (nav.h).
    const Cell &to = v.cell(p.u, p.v);
    ms += NavModel::fallMs(p.h - to.height, v.tickMs, stairs(to.kind));
    return ms;
}

/* When a walk set off now starts its first move, ms from now: once the move
 * under way is done, a key pressed in it being kept for then; from rest, on
 * the next tick, a key pressed on this one being taken on the next, or
 * where the last move ended if that is within the grace of it
 * (movableentity.cpp). */
double firstMove(const BotView &v, double busyMs)
{
    const PlayerView &p = v.player;
    if (p.moving || p.falling)
        return busyMs;
    if (p.stuckSince > 0) {
        // Held by a sticky pad: free on the first tick past its hold.
        double at = v.tickMs;
        while (v.now + at - p.stuckSince <= NavModel::stickyMs(0) && v.tickMs > 0)
            at += v.tickMs;
        return at;
    }
    const double taken = v.tickMs, ended = p.stepEnd - v.now;
    const double since = taken - ended;
    // Not before now, which the routes take as no start (Paths).
    return since > 0 && since < STEP_GRACE_MS ? std::max(0.0, ended) : taken;
}

}  // namespace

Situation::Situation(const BotView &v, FoeModel &m, bool around)
    : view(v), nav(around ? aroundThrowers(v, busy(v), THROWER_NEAR_MS) : NavModel(v)),
      busyMs(busy(v)),
      from(nav, v.player.u, v.player.v, v.player.facing, firstMove(v, busyMs)), foes(m)
{
}

namespace {

NavModel shutting(NavModel nav, const std::vector<Step> &cells)
{
    for (const Step &c : cells)
        nav.shut(c);
    return nav;
}

}  // namespace

Situation::Situation(const Situation &base, const std::vector<Step> &shut)
    : view(base.view), nav(shutting(base.nav, shut)), busyMs(base.busyMs),
      from(nav, view.player.u, view.player.v, view.player.facing, base.from.startMs()),
      foes(base.foes)
{
}

bool realise(Situation &s, const Board &board, const Plan &plan, Option *out, Tally *tally)
{
    if (plan.steps.empty())
        return false;
    const Action &action = *actions()[plan.steps.front().action];
    std::vector<Option> options;
    action.offer(s, board, plan, options);
    std::vector<Step> blocked;
    if (pick(s, options, out, tally, &blocked))
        return true;
    // Round where the throwers stood in the way.
    std::unique_ptr<Situation> round;
    for (int i = 0; i < ROUNDS && !blocked.empty(); i++) {
        const Step here{s.view.player.u, s.view.player.v};
        blocked.erase(std::remove_if(blocked.begin(), blocked.end(),
                                     [&](Step c) { return c.u == here.u && c.v == here.v; }),
                      blocked.end());
        round = std::make_unique<Situation>(round ? *round : s, blocked);
        options.clear();
        action.offer(*round, board, plan, options);
        blocked.clear();
        if (pick(*round, options, out, tally, &blocked))
            return true;
    }
    return false;
}

bool choose(Situation &s, const Board &board, PlanOptions options, Choice *out,
            std::vector<Tally> &tally)
{
    std::vector<Option> checked;   // beside options.root
    for (int checks = 0;; checks++) {
        const Plan plan = planGoal(board, planActions(), options);
        if (checks == 0)
            out->first = plan;
        if (!plan.found || plan.steps.empty())
            return false;
        const PlanStep &step = plan.steps.front();
        for (size_t i = 0; i < options.root.size(); i++) {
            const PlanOptions::Known &k = options.root[i];
            if (k.action == step.action && !(k.end < step.end) && !(step.end < k.end)) {
                out->taken  = plan;
                out->option = checked[i];
                return true;   // the cheapest, on its real cost
            }
        }
        if (checks == MAX_CHECKS)
            return false;
        Option o;
        if (!realise(s, board, plan, &o, &tally[step.action])) {
            options.root.push_back({step.action, step.end, -1});
            checked.push_back(Option());
            continue;
        }
        if (o.stepMs < 0) {
            // The estimate stands: nothing more is known to plan by.
            out->taken  = plan;
            out->option = std::move(o);
            return true;
        }
        options.root.push_back({step.action, step.end, o.stepMs});
        checked.push_back(std::move(o));
    }
}

void offerApproach(Situation &s, const Board &board, Step goal, std::vector<Option> &options)
{
    const double whole = s.from.cost(goal.u, goal.v);
    for (const Step &c : s.from.cellsTo(goal.u, goal.v)) {
        const double soFar = s.from.cost(c.u, c.v);
        if (soFar <= 0 || (c.u == goal.u && c.v == goal.v) || !board.canFinishFrom(c) ||
            switchedBy(s.view, c.u, c.v))
            continue;  // the start, a cell passed only by a forced move, the goal itself, a trap, a switch
        Option o;
        if (!walkTo(s, s.from, c, 0, o))
            continue;
        o.lingerMs = lingerOn(s, c, s.from.startMs() + soFar, o.lingerMs);
        o.cost = whole - soFar;
        char buf[96];
        snprintf(buf, sizeof(buf), "approach (%d,%d) towards (%d,%d)", c.u, c.v, goal.u, goal.v);
        o.what = buf;
        options.push_back(std::move(o));
    }
}

bool approach(Situation &s, const Board &board, Step goal, Option *out, Tally *tally)
{
    std::vector<Option> options;
    offerApproach(s, board, goal, options);
    return pick(s, options, out, tally);
}

int room(const Situation &s, Step c, int most)
{
    const BotView &view = s.view;
    const size_t n = (size_t)view.extentU * view.extentV;
    // Steps from the foes, and from c: the cells c is nearer.
    const int du[] = {0, 1, 0, -1}, dv[] = {-1, 0, 1, 0};
    auto spread = [&](std::vector<Step> queue) {
        std::vector<int> d(n, -1);
        for (const Step &q : queue)
            d[(size_t)q.v * view.extentU + q.u] = 0;
        for (size_t i = 0; i < queue.size(); i++) {
            const Step a = queue[i];
            const int at = d[(size_t)a.v * view.extentU + a.u];
            for (int k = 0; k < 4; k++) {
                const int u = a.u + du[k], v = a.v + dv[k];
                if (!view.inside(u, v) || s.from.cost(u, v) < 0 || d[(size_t)v * view.extentU + u] >= 0 ||
                    abs(view.cell(u, v).height - view.cell(a.u, a.v).height) > 1)
                    continue;
                d[(size_t)v * view.extentU + u] = at + 1;
                queue.push_back({u, v});
            }
        }
        return d;
    };
    std::vector<Step> foes;
    for (const FoeView &f : view.foes)
        if (!f.dying && view.inside(f.u, f.v))
            foes.push_back({f.u, f.v});
    if (foes.empty() || !view.inside(c.u, c.v))
        return most;
    const std::vector<int> mine = spread({c}), theirs = spread(foes);
    int cells = 0;
    for (size_t i = 0; i < n && cells < most; i++)
        cells += mine[i] >= 0 && (theirs[i] < 0 || mine[i] < theirs[i]);
    return cells;
}

double cramped(const Situation &s, Step c)
{
    if (!gemsFromSpawners(s.view))
        return 0;
    return (ROOM_CELLS - room(s, c, ROOM_CELLS)) * ROOM_MS;
}

void offerFetchGem(Situation &s, const Board &board, std::vector<Option> &out)
{
    Step gem{-1, -1};
    double best = -1;
    for (const Step &g : board.gems(0)) {
        const double c = s.from.cost(g.u, g.v);
        if (c > 0 && (best < 0 || c < best)) {
            gem  = g;
            best = c;
        }
    }
    Option o;
    if (gem.u < 0 || !walkTo(s, s.from, gem, 0, o))
        return;
    char buf[64];
    snprintf(buf, sizeof(buf), "fetch the gem at (%d,%d)", gem.u, gem.v);
    o.what = buf;
    out.push_back(std::move(o));
}

void offerEvade(Situation &s, const Board &board, std::vector<Option> &options)
{
    const BotView &view = s.view;
    std::vector<std::pair<double, Step>> cells;
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++) {
            const double c = s.from.cost(u, v);
            // Not onto a switch, which works a bridge no plan asked for.
            if (c > 0 && c <= EVADE_MS && !switchedBy(view, u, v))
                cells.push_back({c, {u, v}});
        }
    std::sort(cells.begin(), cells.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    // Not into a trap, but where the player is cannot finish either (a foe
    // may yet open the way, say), the test tells none apart.
    const bool any = !board.canFinishFrom({view.player.u, view.player.v});
    std::vector<std::pair<Step, double>> to;
    for (size_t i = 0; i < cells.size() && to.size() < EVADES; i++)
        if (any || board.canFinishFrom(cells[i].second))
            to.push_back({cells[i].second, 0});
    const size_t first = options.size();
    offerWalks(s, to, "evade to", false, options);
    for (size_t i = first; i < options.size(); i++) {
        Option &o = options[i];
        const Step c = o.schedule.back().cell;
        o.lingerMs = lingerOn(s, c, s.from.startMs() + s.from.cost(c.u, c.v), o.lingerMs);
        o.cost += cramped(s, c);
    }
}

}  // namespace bot
