/* Glide(c): with a glide held, a glide down to cell c (glide.h): on, or
 * beside, a gem, a pickup or the exit, from the launch that gets there
 * soonest.  Done as a walk to that launch, a turn to face off the edge, and
 * the glide's own path through the air. */
#include <stdio.h>
#include <map>
#include "action.h"
#include "motor.h"
#include "walk.h"

namespace bot {

namespace {

const int DU[] = {0, 0, 1, 0, -1};
const int DV[] = {0, -1, 0, 1, 0};

/* A glide to a landing: where it is launched from, and the landing. */
struct Way {
    double         ms = -1;   // from the place, walk and turn included
    Place          launch;
    const Landing *landing = nullptr;
};

/* The quickest glide from the place to each landing, in the world s. */
std::map<Place, Way> ways(const Board &board, Changes s, Place from)
{
    const Terrain &w = board.worlds().get(s);
    const BotView &view = w.view();
    // Where it is worth coming down: on or beside what the plans go for.
    std::vector<char> wanted(view.cells.size(), 0);
    auto want = [&](Step c) {
        for (int d = 0; d <= 4; d++) {
            const int u = c.u + DU[d], v = c.v + DV[d];
            if (view.inside(u, v))
                wanted[(size_t)v * view.extentU + u] = 1;
        }
    };
    for (const Step &g : board.gems(s))
        want(g);
    for (const Pickup &p : board.pickups())
        if (Board::available(p, s))
            want(p.cell);
    want(board.exit());

    std::map<Place, Way> best;
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++)
            for (int f = 1; f <= 4; f++) {
                if (!glideLaunch(view, u, v, f))
                    continue;
                const double walk = board.time(s, from, {u, v});
                if (walk < 0)
                    continue;
                const double before = walk + board.turnMs({u, v, 0}, f);
                for (const Landing &l : w.glides(u, v, f)) {
                    if (!wanted[(size_t)l.cell.v * view.extentU + l.cell.u])
                        continue;
                    const Place at{l.cell.u, l.cell.v, 0};
                    Way &b = best[at];
                    if (b.ms < 0 || before + l.ms < b.ms)
                        b = {before + l.ms, {u, v, f}, &l};
                }
            }
    return best;
}

class GlideAction : public Action {
public:
    const char *name() const override { return "glide"; }

    void expand(const Facts &f, double, const Board &board, const PlanOptions &,
                std::vector<Successor> &out) const override
    {
        if (f.glides <= 0)
            return;
        const auto all = ways(board, f.changes, f.at);
        for (const auto &[at, way] : all) {
            Successor n{f, {}};
            n.facts.glides--;
            n.facts.at   = at;
            n.step.end   = at;
            n.step.heads = {way.launch.u, way.launch.v};
            n.step.ms    = way.ms;
            out.push_back(n);
        }
    }

    void offer(Situation &s, const Board &board, const Plan &plan,
               std::vector<Option> &out) const override
    {
        const Place &end = plan.steps.front().end;
        const std::map<Place, Way> all = ways(board, 0, board.start());
        const auto it = all.find({end.u, end.v, 0});
        if (it == all.end())
            return;
        const Way &way = it->second;
        const Step launch{way.launch.u, way.launch.v};
        Option o;
        if (!walkTo(s, s.from, launch, 0, o))
            return;
        const int arrives = s.from.facingAt(launch.u, launch.v);
        const int turns = (way.launch.facing - arrives + 4) % 4;
        const double off = o.cost + (turns == 3 ? 1 : turns) * s.view.player.stepMs;
        o.script.push_back({ScriptStep::Turn, way.launch.facing, {}, 0});
        ScriptStep glide;
        glide.kind = ScriptStep::Walk;
        glide.path = way.landing->path;
        o.script.push_back(glide);
        for (size_t i = 1; i < way.landing->schedule.size(); i++) {
            Timed m = way.landing->schedule[i];
            m.t += off;
            o.schedule.push_back(m);
        }
        o.schedule.push_back({off + way.landing->ms, way.landing->cell});
        o.stepMs = off + way.landing->ms;
        o.cost = plan.cost - plan.steps.front().ms + o.stepMs;
        char buf[96];
        snprintf(buf, sizeof(buf), "glide from (%d,%d) to (%d,%d)", launch.u, launch.v, end.u, end.v);
        o.what = buf;
        out.push_back(std::move(o));
    }
};

}  // namespace

const Action &glideAction()
{
    static const GlideAction action;
    return action;
}

}  // namespace bot
