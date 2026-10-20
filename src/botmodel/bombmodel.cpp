#include "bombmodel.h"
#include <cmath>
#include <stdlib.h>
#include "liftmodel.h"
#include "tile.h"

namespace bot {

namespace {

const double CLEAR_MS = 2400.0;
const double STEP_MS = 200.0;    // its one step
/* The drops dropSpells tries: over a carrier's whole cycle (a lift's or a
 * platform's runs and parks), a step apart. */
const double CYCLE_MS      = 8000.0;
const double SPELL_STEP_MS = 50.0;

bool stairs(int k) { return k > 4 && k < 9; }

/* A bomb on (u, v) of a platform's track from atMs (from the view's clock):
 * it rides the platform, and blows where the platform has carried it by the
 * tick before (the tick it blows on does not move it, bomb.cpp).  False if
 * the platform is not there to ride. */
bool ride(const BotView &view, int u, int v, double atMs, double fuseAt, Blast *b)
{
    const PlatformView *p = platformThrough(view, u, v);
    Step on, blows;
    if (!p || !platformCellAt(view, *p, atMs, &on) || on.u != u || on.v != v ||
        !platformCellAt(view, *p, fuseAt - view.now - view.tickMs, &blows))
        return false;
    b->cell = blows;
    b->h    = p->height;
    return true;
}

/* Whether a rolling bomb at height h may step from a to b. */
bool may_roll(const Cell &a, int h, const Cell &b)
{
    if (b.kind == TILE_IMPASSABLE || (b.kind == TILE_BOMBABLE && !b.busy))
        return false;
    if (!stairs(a.kind) && !stairs(b.kind) && b.height == h + 1)
        return false;
    return b.height <= h + 1;
}

/* Where and when it blows; *timed whether that hangs on when it was dropped
 * (it went onto a platform or a lift). */
Blast roll(const BotView &view, int u, int v, int h, int facing, double droppedAt,
           bool rolled, bool *timed)
{
    Blast b;
    *timed = false;
    // One step along its facing, if it may: once moving it is held, and a
    // held bomb takes no further move of its own.  Ice carries it on, the
    // way it went, until it enters a cell that is not ice or may not go on.
    bool step = !rolled;
    for (int guard = 0; guard < 200; guard++) {
        if (!step && view.cell(u, v).kind != TILE_ICE)
            break;
        const int nu = u + DIR_DU[facing], nv = v + DIR_DV[facing];
        if (!view.inside(nu, nv))
            break;
        if (const LiftView *lift = liftAt(view, nu, nv)) {
            // Onto a lift: as onto ground at the height it stands through the
            // step, and then it rides the lift up or down, and blows at the
            // height the lift has taken it to.
            const double in = droppedAt - view.now + view.tickMs;
            const int at = liftHeightAt(view, *lift, in), after = liftHeightAt(view, *lift, in + STEP_MS);
            *timed = true;
            if (at < 0 || at != after) {
                b.lost = true;   // it moves under the step: not modelled
                break;
            }
            if (at > h)
                break;          // a face: it stays
            b.cell  = {nu, nv};
            // Where it had got to on the tick before it blows (bomb.cpp).
            b.h     = liftHeightAt(view, *lift, droppedAt + FUSE_MS - view.now - view.tickMs);
            b.lost  = b.h < 0;
            b.from  = droppedAt + FUSE_MS;
            b.until = droppedAt + CLEAR_MS;
            return b;
        }
        if (!may_roll(view.cell(u, v), h, view.cell(nu, nv)))
            break;
        u = nu;
        v = nv;
        const Cell &next = view.cell(nu, nv);
        if (next.track) {
            // Onto a platform's track: it must stand there through the step.
            const double in = droppedAt - view.now + view.tickMs;
            Blast r;
            r.from  = droppedAt + FUSE_MS;
            r.until = droppedAt + CLEAR_MS;
            *timed = true;
            if (ride(view, nu, nv, in, r.from, &r) && ride(view, nu, nv, in + STEP_MS, r.from, &r))
                return r;
            b.lost = true;
            break;
        }
        if (next.kind == TILE_EMPTY || next.height == 0) {
            b.lost = true;
            break;
        }
        h = next.height;
        step = false;
    }
    b.cell  = {u, v};
    b.h     = h;
    b.from  = droppedAt + FUSE_MS;
    b.until = droppedAt + CLEAR_MS;
    return b;
}

}  // namespace

bool Blast::kills(int u, int v, int eh, double t) const
{
    return !lost && t >= from && t < until && abs(u - cell.u) <= 1 && abs(v - cell.v) <= 1 &&
           abs(eh - h) <= 1;
}

Blast predictBomb(const BotView &view, int u, int v, int h, int facing, double droppedAt)
{
    bool timed;
    return roll(view, u, v, h, facing, droppedAt, false, &timed);
}

std::vector<DropSpell> dropSpells(const BotView &view, int u, int v, int h, int facing,
                                  double fromMs)
{
    std::vector<DropSpell> out;
    bool timed;
    const Blast first = roll(view, u, v, h, facing, fromMs, false, &timed);
    if (!timed) {
        if (!first.lost)
            out.push_back({fromMs, INFINITY, first});
        return out;
    }
    // Drops a step apart over a carrier's cycle; a spell is a run of them
    // that blow on one cell at one height.
    DropSpell run;
    bool open = false;
    auto close = [&](double at) {
        if (open && at - run.from >= DROP_SLACK_MS) {
            run.to = at;
            out.push_back(run);
        }
        open = false;
    };
    for (double at = fromMs; at < fromMs + CYCLE_MS; at += SPELL_STEP_MS) {
        const Blast b = at == fromMs ? first : roll(view, u, v, h, facing, at, false, &timed);
        if (open && (b.lost || b.cell.u != run.blast.cell.u || b.cell.v != run.blast.cell.v ||
                     b.h != run.blast.h))
            close(at);
        if (!open && !b.lost) {
            run  = {at, at, b};
            open = true;
        }
    }
    close(fromMs + CYCLE_MS);
    return out;
}

std::vector<Step> carriedFrom(const BotView &view)
{
    std::vector<Step> out;
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++) {
            const Cell &c = view.cell(u, v);
            if (!c.track && c.kind != TILE_LIFT)
                continue;
            for (int d = DIR_MIN; d <= DIR_MAX; d++)
                if (view.inside(u + DIR_DU[d], v + DIR_DV[d]))
                    out.push_back({u + DIR_DU[d], v + DIR_DV[d]});
        }
    return out;
}

bool spends(const BotView &view, const Blast &b, Step c)
{
    return !b.lost && abs(c.u - b.cell.u) <= 1 && abs(c.v - b.cell.v) <= 1 &&
           view.cell(c.u, c.v).height == b.h;
}

Blast predictBomb(const BotView &view, const BombView &bomb)
{
    // Moving, its cell is already the step's end; seen after its first tick
    // at rest, it has taken (or been refused) its one step.
    const bool rolled = bomb.moving || view.now - bomb.droppedAt > view.tickMs * 1.5;
    if (rolled && !bomb.moving)
        if (const LiftView *lift = liftAt(view, bomb.u, bomb.v)) {
            Blast r;
            r.cell  = {bomb.u, bomb.v};
            r.h     = liftHeightAt(view, *lift, bomb.droppedAt + FUSE_MS - view.now);
            r.lost  = r.h < 0;
            r.from  = bomb.droppedAt + FUSE_MS;
            r.until = bomb.droppedAt + CLEAR_MS;
            return r;
        }
    if (rolled && !bomb.moving && view.inside(bomb.u, bomb.v) && view.cell(bomb.u, bomb.v).track) {
        Blast r;
        r.from  = bomb.droppedAt + FUSE_MS;
        r.until = bomb.droppedAt + CLEAR_MS;
        if (ride(view, bomb.u, bomb.v, 0, r.from, &r))
            return r;
    }
    bool timed;
    return roll(view, bomb.u, bomb.v, bomb.h, bomb.facing, bomb.droppedAt, rolled, &timed);
}

/* Whether the blast spends a bombable block. */
bool spendsBlock(const BotView &view, const Blast &b)
{
    for (int dv = -1; dv <= 1; dv++)
        for (int du = -1; du <= 1; du++) {
            const int u = b.cell.u + du, v = b.cell.v + dv;
            if (view.inside(u, v) && view.cell(u, v).kind == TILE_BOMBABLE &&
                !view.cell(u, v).busy && spends(view, b, {u, v}))
                return true;
        }
    return false;
}

}  // namespace bot
