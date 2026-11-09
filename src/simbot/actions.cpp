#include "actions.h"
#include <stdio.h>
#include <stdlib.h>
#include "tile.h"
#include "world.h"

namespace simbot {

namespace {

/* What an estimate counts a leg there is no way for. */
const double NO_WAY_MS = 600000.0;
/* How long the player may stand on the exit before the level completes. */
const int EXIT_TICKS = 240;

std::string at(const char *what, Cell c)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "%s (%d,%d)", what, c.u, c.v);
    return buf;
}

bool quotaIn(const World &w)
{
    return w.player()->gemsCollected() >= w.gemsRequired();
}

class CollectGem : public Action {
public:
    const char *name() const override { return "collect gem"; }
    void expand(const World &from, Planning &planning, std::vector<Successor> &out) const override
    {
        if (quotaIn(from))
            return;
        const Reach &reach = planning.reach();
        for (int a : reach.arrivals()) {
            const Place p = reach.place(a);
            if (from.map()->tile(p.u, p.v)->contents() != CONTENTS_CRYSTAL)
                continue;
            Successor s;
            s.world   = std::make_unique<World>(reach.world(a));
            s.what    = at("gem", {p.u, p.v});
            s.presses = reach.presses(a);
            out.push_back(std::move(s));
        }
    }
};

class Exit : public Action {
public:
    const char *name() const override { return "exit"; }
    void expand(const World &from, Planning &planning, std::vector<Successor> &out) const override
    {
        if (!quotaIn(from))
            return;
        const Cell exit = planning.exit();
        const Reach &reach = planning.reach();
        for (int a : reach.arrivals()) {
            const Place p = reach.place(a);
            if (p.u != exit.u || p.v != exit.v)
                continue;
            Successor s;
            s.world   = std::make_unique<World>(reach.world(a));
            s.presses = reach.presses(a);
            for (int t = 0; t < EXIT_TICKS && !s.world->completed(); t++) {
                tick(*s.world, 0);
                s.presses.push_back(0);
            }
            if (!s.world->completed())
                continue;
            s.what = at("exit", exit);
            out.push_back(std::move(s));
            return;   // the quickest way on is enough
        }
    }
};

class CompleteLevel : public Goal {
public:
    const char *name() const override { return "complete"; }
    bool reached(const World &w) const override { return w.completed(); }
    /* The rest of the quota and then the exit, nearest gem first, by the
     * legs' times on the level as it started (Planning::legMs): a guide,
     * not a bound, so the plans are good rather than the quickest. */
    double estimate(const World &w, Planning &planning) const override
    {
        if (w.completed())
            return 0;
        auto leg = [&](Cell a, Cell b) {
            const double ms = planning.legMs(a, b);
            return ms < 0 ? NO_WAY_MS : ms;
        };
        const Place p = placeOf(w);
        Cell at{p.u, p.v};
        std::vector<Cell> left;
        for (const Cell &g : planning.gems())
            if (w.map()->tile(g.u, g.v)->contents() == CONTENTS_CRYSTAL)
                left.push_back(g);
        double ms = 0;
        for (int need = w.gemsRequired() - w.player()->gemsCollected(); need > 0 && !left.empty(); need--) {
            size_t best = 0;
            for (size_t i = 1; i < left.size(); i++)
                if (leg(at, left[i]) < leg(at, left[best]))
                    best = i;
            ms += leg(at, left[best]);
            at = left[best];
            left.erase(left.begin() + best);
        }
        return ms + leg(at, planning.exit());
    }
};

}  // namespace

const std::vector<const Action *> &actions()
{
    static const CollectGem collect;
    static const Exit exit;
    static const std::vector<const Action *> all = {&collect, &exit};
    return all;
}

const Goal &completeLevel()
{
    static const CompleteLevel goal;
    return goal;
}

}  // namespace simbot
