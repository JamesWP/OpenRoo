/* kill: drop a bomb that kills a foe, and live through the blast.
 *   pre:  the foe alive; a bomb held; the MoveGraph reaches the foe's cell,
 *         or one beside it, as last seen
 *   eff:  the foe dead, a bomb fewer, the player near where it was seen
 *   do:   a RouteAttempt with the foe as its goal (routing.h): walking,
 *         waiting and one drop, played on copies till one has the foe dead.
 * The foe moves, so where it is reckoned to be is only where it was last
 * seen (Survey::see). */
#include <stdio.h>
#include "actions.h"
#include "kill.h"
#include <stdlib.h>
#include "routeattempt.h"
#include "survey.h"
#include "world.h"

namespace simbot {

namespace {

/* nearFoe: the ms a foe may walk, on the MoveGraph, to come beside where
 * the bomb blows: its fuse. */
const double REACH_MS = Bomb::FUSE_MS;
/* foeMoving: a step ended this recently. */
const double MOVING_MS = 500;
/* blockedBy: a foe holds a place it comes to no later than this after the
 * player. */
const double HELD_MS = 500;
/* A kill's Routes are searched no further: it is near the foe or nowhere. */
const size_t KILL_PLACES = 3000;

}  // namespace

bool foeMoving(const World &w, const Foe &f)
{
    return f.moveDir() != 0 || w.now() - f.stepEnd() < MOVING_MS;
}

bool nearFoe(const MoveGraph &g, const Place &place, Cell foe, int foeH, bool moving)
{
    const MoveGraph::Blast *b = g.blasts(g.find(place));
    if (!b)
        return false;
    const int foeAt = g.find({foe.u, foe.v, foeH, 0});
    for (int k = 0; k < 2; k++) {
        if (!b[k].ok)
            continue;
        if (abs(b[k].cell.u - foe.u) <= 1 && abs(b[k].cell.v - foe.v) <= 1 &&
            abs(b[k].h - foeH) <= 1)
            return true;
        if (!moving)
            continue;
        const double t = g.timeBeside(foeAt, b[k].cell);
        if (t >= 0 && t <= REACH_MS)
            return true;
    }
    return false;
}

bool blockedBy(const MoveGraph &g, const World &w, unsigned foe, Cell cell, int foeH,
               bool moving, unsigned *blocker)
{
    const int from = g.find(placeOf(w));
    if (from < 0)
        return false;
    // Each other foe's time to every place, on the player's MoveGraph: it
    // holds a place it comes to no later than the player.
    std::vector<std::pair<unsigned, MoveGraph::Table>> others;
    for (const Foe &o : w.foes()) {
        if (o.id() == foe || o.moveState() != 0)
            continue;
        const int at = g.find({o.cellU(), o.cellV(), o.heightCell(), 0});
        if (at >= 0)
            others.push_back({o.id(), g.times(at)});
    }
    const MoveGraph::Table mineTable = g.times(from);
    const std::vector<double> &mine = *mineTable;
    auto heldBy = [&](int p, unsigned *id) {
        for (const auto &[oid, t] : others)
            if ((*t)[p] >= 0 && (mine[p] < 0 || (*t)[p] <= mine[p] + HELD_MS)) {
                *id = oid;
                return true;
            }
        return false;
    };
    unsigned id;
    auto near = [&](int p) { return nearFoe(g, g.place(p), cell, foeH, moving); };
    auto held = [&](int p) { return heldBy(p, &id); };
    if (!g.path(from, near, held).empty())
        return false;
    for (int p : g.path(from, near, [](int) { return false; }))
        if (p != from && heldBy(p, blocker))
            return true;
    return false;   // no way even without them: the search will find so
}

bool KillGoal::reached(const Route::Node &n) const
{
    // Every bomb gone (one dropped before may still be on its way): what
    // its blast takes is known.
    return n.dropAt >= 0 && n.now >= n.dropAt + BLAST_OVER_MS && foeDead(*n.world, foe_) &&
           n.world->bombs().empty() && safe(*n.world);
}

double KillGoal::estimate(const Route::Node &n, double toCell) const
{
    if (n.dropAt < 0)
        return toCell + BLAST_OVER_MS;
    const double h = n.dropAt + BLAST_OVER_MS - n.now;
    return h < 0 ? 0 : h;
}

Route::Key KillGoal::key(const Route::Node &n, const Route::Key &plain) const
{
    if (n.dropAt < 0)
        return plain;
    // After the drop, where the foe is.
    Route::Key k = plain;
    if (const Foe *f = findFoe(*n.world, foe_))
        std::get<4>(k) = {f->cellU(), f->cellV()};
    return k;
}

bool KillGoal::mayDrop(const Route::Node &n) const
{
    const Foe *f = findFoe(*n.world, foe_);
    return f && nearFoe(*graph_, n.place, {f->cellU(), f->cellV()}, f->heightCell(),
                        foeMoving(*n.world, *f));
}

bool KillGoal::keeps(const Route::Node &n)
{
    // A drop whose blast is over with the foe alive has failed.
    if (n.dropAt >= 0 && n.now >= n.dropAt + BLAST_OVER_MS && !foeDead(*n.world, foe_)) {
        lived_++;
        return false;
    }
    return true;
}

std::string KillGoal::summary() const
{
    return ", foe lived " + std::to_string(lived_);
}

bool KillGoal::done(const World &live) const
{
    return foeDead(live, foe_);
}

bool TrackGoal::reached(const Route::Node &n) const
{
    const Foe *f = findFoe(*n.world, foe_);
    if (!f)
        return true;   // gone: nothing left to come near
    return nearFoe(*graph_, n.place, {f->cellU(), f->cellV()}, f->heightCell(),
                   foeMoving(*n.world, *f)) &&
           safe(*n.world);
}

namespace {

class Kill : public Action {
public:
    const char *name() const override { return "kill"; }

    void offer(const Facts &f, const Survey &survey, std::vector<Step> &out,
               int /*only*/) const override
    {
        if (f.done || f.at < 0 || f.bombs == 0)
            return;
        for (size_t i = 0; i < survey.foes().size(); i++) {
            if (!(f.alive >> i & 1))
                continue;
            const Cell c = survey.seen((int)i);
            // The quickest arrival on its cell or one beside it.
            const Cell near[] = {c, {c.u + 1, c.v}, {c.u - 1, c.v}, {c.u, c.v + 1}, {c.u, c.v - 1}};
            int to = -1, used = 0;
            double best = 0;
            for (const Cell &n : near) {
                double ms;
                int u;
                const int p = survey.graph().arrive(f.at, n, &ms, f.glides, &u);
                if (p >= 0 && (to < 0 || ms < best)) {
                    to = p;
                    best = ms;
                    used = u;
                }
            }
            if (to < 0)
                continue;
            Step s;
            s.action = this;
            s.arg    = (int)i;
            s.after  = f;
            s.after.at = to;
            s.after.alive &= ~((uint64_t)1 << i);
            s.after.bombs--;
            s.after.glides -= used;
            s.ms = best + Bomb::CLEAR_MS;
            char buf[48];
            snprintf(buf, sizeof(buf), "kill f%zu (%d,%d)", i, c.u, c.v);
            s.what = buf;
            out.push_back(std::move(s));
        }
    }

    std::unique_ptr<Attempt> attempt(const Step &step, const Survey &survey) const override
    {
        RouteOptions o;
        o.places     = KILL_PLACES;
        o.waitFirst  = true;   // for the foe to come on
        return std::make_unique<RouteAttempt>(
            std::make_shared<KillGoal>(survey.seen(step.arg), survey.foes()[step.arg],
                                       survey.graph()),
            survey, o);
    }
};

}  // namespace

const Action &killAction()
{
    static const Kill a;
    return a;
}

}  // namespace simbot
