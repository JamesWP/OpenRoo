#include "schedule.h"
#include <algorithm>
#include <stdlib.h>
#include "world.h"

namespace simbot {

namespace {

// A tick or so past the reckoned time, so it has been ticked.
const double MARGIN_MS = 20;

/* Where in a cell or a height a step off is let through, the middle of it
 * (movableentity.cpp): a platform no more than 0.15 of a cell past a whole
 * one, a lift within 0.2 of a whole height. */
const double PLATFORM_MID = 0.075;
const double LIFT_MID = 0.1;

/* The most the player drops unharmed (movableentity.cpp). */
const int MAX_DROP = 2;

/* A press is played the tick after a wait ends, and the move it makes is
 * checked the tick after that, against where the lift or platform has gone
 * by then (world: they tick before the player): a wait ends this many ticks
 * ahead of the time it is aimed at, so that check falls on it, give or take
 * half a tick. */
const double PRESS_TICKS = 2.5;

bool beside(int u, int v, Cell c)
{
    return abs(u - c.u) + abs(v - c.v) <= 1;
}

/* A thing that runs to and fro on the clock alone: span units (heights or
 * cells) from its start to its end at perMs, parking parkMs at each end. */
struct Run {
    double forth;    // a time it set off, or sets off, from its start
    double travel;   // ms for the run one way
    double cycle;    // ms for the run there and back, the parks counted

    Run(int state, bool atEnd, double phaseStart, int span, float perMs, double parkMs)
    {
        travel = span / (double)perMs;
        cycle = 2 * (travel + parkMs);
        if (state == 1)
            forth = phaseStart;
        else if (state == 2)
            forth = phaseStart - travel - parkMs;
        else if (atEnd)
            forth = phaseStart - travel;
        else
            forth = phaseStart + parkMs;
    }
    /* The first of t, t + cycle, ... after now. */
    double next(double t, double now) const
    {
        while (t > now + cycle)
            t -= cycle;
        while (t <= now)
            t += cycle;
        return t;
    }
    /* When it next parks at each end. */
    void parks(std::vector<double> &out, double now) const
    {
        out.push_back(next(forth + travel + MARGIN_MS, now));
        out.push_back(next(forth + travel + cycle / 2 + travel + MARGIN_MS, now));
    }
    /* When a wait should end for a press to be checked as it passes x units
     * from its start, each way. */
    void passes(std::vector<double> &out, double x, float perMs, double lead, double now) const
    {
        out.push_back(next(forth + x / perMs - lead, now));
        out.push_back(next(forth + cycle / 2 + travel - x / perMs - lead, now));
    }
};

}  // namespace

std::vector<double> waitTimes(const World &w, Cell c)
{
    std::vector<double> out;
    const double now = w.now();
    const double tick = w.tickStep()->value > 0 ? w.tickStep()->value : 1000.0 / 60;
    const double lead = PRESS_TICKS * tick;
    for (const LiftObject &l : w.lifts()) {
        if (!beside(l.cellU(), l.cellV(), c))
            continue;
        const int span = l.topHeight() - l.baseHeight();
        const Run run(l.state(), l.atTop(), l.phaseStart(), span, LiftObject::HEIGHT_PER_MS,
                      LiftObject::PARK_MS);
        run.parks(out, now);
        // Ridden, a step off at any height; from beside, a step on at the
        // player's height or a drop onto it.
        const bool on = l.cellU() == c.u && l.cellV() == c.v;
        const int h = w.player()->heightCell() - l.baseHeight();
        for (int n = 0; n < span; n++)
            if (on || (n <= h && n >= h - MAX_DROP))
                run.passes(out, n + LIFT_MID, LiftObject::HEIGHT_PER_MS, lead, now);
    }
    for (const PlatformObject &p : w.platforms()) {
        const bool alongU = p.kind() == 0x0a;
        const int from = alongU ? p.originU() : p.originV();
        const int span = p.limit() - from;
        const Run run(p.state(), p.atLimit(), p.phaseStart(), span, PlatformObject::CELLS_PER_MS,
                      PlatformObject::PARK_MS);
        bool near = false;
        for (int k = 0; k <= span; k++) {
            const int i = from + k;
            if (!(alongU ? beside(i, p.originV(), c) : beside(p.originU(), i, c)))
                continue;
            near = true;
            if (k < span)
                run.passes(out, k + PLATFORM_MID, PlatformObject::CELLS_PER_MS, lead, now);
        }
        if (near)
            run.parks(out, now);
    }
    for (const FallingTile &f : w.fallings())
        if (f.respawnPending())
            out.push_back(f.armedAt() + FallingTile::RESPAWN_MS + MARGIN_MS);
    const double stuck = w.player()->stuckSince();
    if (stuck != 0)
        out.push_back(stuck + MovableEntity::STUCK_MS + MARGIN_MS);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}  // namespace simbot
