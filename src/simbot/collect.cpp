/* collect: walk to a gem not yet taken, while the quota is short.
 *   pre:  the gem is not taken; gems held < quota; the MoveGraph reaches it;
 *         it is one of the NEAREST nearest in its region
 *   eff:  the gem taken, one more held, the player on it
 *   do:   a RouteAttempt to the gem. */
#include <stdio.h>
#include <algorithm>
#include <map>
#include <random>
#include "actions.h"
#include "subgoals.h"
#include "routeattempt.h"
#include "survey.h"

namespace simbot {

namespace {

/* The gems offered in each region: the nearest so many. */
const int NEAREST = 4;

/* Random edits to a plan's gem order (polishGems) at most, at most so many
 * in a row none quicker, and the seed. */
const int      POLISH_EDITS = 20000;
const int      POLISH_STALE = 3000;
const unsigned POLISH_SEED  = 1;
/* What a polished order must save to be taken. */
const double POLISH_MIN_MS = 1;

/* The time of a leg by MoveGraph::timeTo, for a given number of charges:
 * walking, and the quickest way (gliding as it may) with the charges it
 * spends. */
struct Leg {
    float   walk = -1, any = -1;
    uint8_t glides = 0;
    double ms(int &charges) const
    {
        if (charges > 0 && any >= 0 && glides <= charges) {
            charges -= glides;
            return any;
        }
        return walk;
    }
};

Leg legOf(const MoveGraph &g, int from, Cell to)
{
    Leg l;
    l.walk = (float)g.timeTo(from, to, 0);
    int used;
    l.any = (float)g.timeTo(from, to, 255, &used);
    l.glides = (uint8_t)used;
    return l;
}

class Collect : public Action {
public:
    const char *name() const override { return "collect"; }

    void offer(const Facts &f, const Survey &survey, std::vector<Step> &out,
               int only) const override
    {
        if (f.done || f.at < 0 || f.gems >= survey.quota())
            return;
        // The nearest few in each region the player comes to (regions are
        // left for good, so each is offered its own).
        struct Near {
            double ms;
            size_t gem;
            int    to, used;
        };
        std::vector<Near> near;
        const MoveGraph &g = survey.graph();
        for (size_t i = 0; i < survey.gems().size(); i++) {
            if (f.taken.has(i) || (only >= 0 && (int)i != only))
                continue;
            Near n{0, i, -1, 0};
            n.to = g.arrive(f.at, survey.gems()[i], &n.ms, f.glides, &n.used);
            if (n.to >= 0)
                near.push_back(n);
        }
        std::sort(near.begin(), near.end(), [](const Near &a, const Near &b) {
            return a.ms < b.ms || (a.ms == b.ms && a.gem < b.gem);
        });
        std::map<int, int> offered;   // by region
        for (const Near &n : near) {
            if (offered[g.region(n.to)]++ >= NEAREST && only < 0)
                continue;
            const Cell c = survey.gems()[n.gem];
            Step s;
            s.action = this;
            s.arg    = (int)n.gem;
            s.after  = f;
            s.after.at = n.to;
            s.after.taken.add(n.gem);
            s.after.gems++;
            s.after.glides -= n.used;
            s.ms = n.ms;
            char buf[48];
            snprintf(buf, sizeof(buf), "collect g%zu (%d,%d)", n.gem, c.u, c.v);
            s.what = buf;
            out.push_back(std::move(s));
        }
    }

    std::unique_ptr<Attempt> attempt(const Step &step, const Survey &survey) const override
    {
        return std::make_unique<RouteAttempt>(std::make_shared<Goal>(survey.gems()[step.arg]), survey);
    }
};

}  // namespace

const Action &collectAction()
{
    static const Collect a;
    return a;
}

const Action &exitAction();

void polishGems(const Survey &survey, const Facts &start, std::vector<Step> &steps, Work &work)
{
    // The gems the plan collects first, one after another, and what comes
    // after them: the exit (its leg counted), nothing, or another step (the
    // last gem kept last, so that step goes as planned).
    size_t n = 0;
    while (n < steps.size() && steps[n].action == &collectAction())
        n++;
    const bool exit = n < steps.size() && steps[n].action == &exitAction();
    const bool keepLast = n < steps.size() && !exit;
    if (n < 3)
        return;
    const MoveGraph &g = survey.graph();
    const std::vector<Cell> &gems = survey.gems();
    // Legs between them (from the place a gem is come to: MoveGraph::arrive),
    // from the start, and to the exit; index n is the start.
    std::vector<int> gem(n);
    for (size_t i = 0; i < n; i++)
        gem[i] = steps[i].arg;
    std::vector<std::vector<Leg>> leg(n + 1, std::vector<Leg>(n + 1));
    std::vector<Leg> toExit(n);
    for (size_t i = 0; i <= n; i++) {
        const int from = i == n ? start.at : g.on(gems[gem[i]]).front();
        for (size_t j = 0; j < n; j++)
            if (j != i)
                leg[i][j] = legOf(g, from, gems[gem[j]]);
        if (exit && i < n)
            toExit[i] = legOf(g, from, survey.exit());
    }
    work.spend(PLAN_NODE_WORK * (long)n);
    const double NONE = 1e18;
    auto tour = [&](const std::vector<int> &order) {
        int charges = start.glides;
        int at = (int)n;
        double ms = 0;
        for (int i : order) {
            const double t = leg[at][i].ms(charges);
            if (t < 0)
                return NONE;
            ms += t;
            at = i;
        }
        if (exit) {
            const double t = toExit[at].ms(charges);
            if (t < 0)
                return NONE;
            ms += t;
        }
        return ms;
    };
    std::vector<int> order(n);
    for (size_t i = 0; i < n; i++)
        order[i] = (int)i;
    const double planned = tour(order);
    double best = planned;
    // Random edits, each kept if quicker: a gem moved elsewhere, a stretch
    // turned about, two gems swapped; till POLISH_EDITS, or POLISH_STALE in
    // a row none quicker.  A fixed seed: the same plan polishes the same.
    const int m = (int)n - (keepLast ? 1 : 0);
    std::mt19937 rng(POLISH_SEED);
    std::vector<int> o;
    int stale = 0;
    for (int e = 0; e < POLISH_EDITS && stale < POLISH_STALE && m >= 2; e++) {
        int i = (int)(rng() % (unsigned)m), j = (int)(rng() % (unsigned)(m - 1));
        if (j >= i)
            j++;
        o = order;
        switch (rng() % 3) {
        case 0: {
            const int x = o[i];
            o.erase(o.begin() + i);
            o.insert(o.begin() + j, x);
            break;
        }
        case 1:
            std::reverse(o.begin() + std::min(i, j), o.begin() + std::max(i, j) + 1);
            break;
        default:
            std::swap(o[i], o[j]);
        }
        const double t = tour(o);
        if (t < best - POLISH_MIN_MS) {
            best = t;
            order.swap(o);
            stale = 0;
        } else {
            stale++;
        }
        if (e % m == m - 1)
            work.spend(PLAN_NODE_WORK);
    }
    if (best >= planned)
        return;
    // The new order, then the rest as planned.
    std::vector<Step> in;
    for (int i : order)
        in.push_back(steps[i]);
    in.insert(in.end(), steps.begin() + n, steps.end());
    steps = std::move(in);
}

}  // namespace simbot
