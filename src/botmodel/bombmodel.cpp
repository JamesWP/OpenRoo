#include "bombmodel.h"
#include <stdlib.h>
#include "liftmodel.h"
#include "tile.h"

namespace bot {

namespace {

const double FUSE_MS = 2000.0;   // bomb.cpp
const double CLEAR_MS = 2400.0;
const double STEP_MS = 200.0;    // its one step

bool stairs(int k) { return k > 4 && k < 9; }

/* A bomb on (u, v) of a platform's track from atMs (from the view's clock):
 * it rides the platform, and blows where the platform has carried it.  False
 * if the platform is not there to ride. */
bool ride(const BotView &view, int u, int v, double atMs, double fuseAt, Blast *b)
{
    const PlatformView *p = platformThrough(view, u, v);
    Step on, blows;
    if (!p || !platformCellAt(view, *p, atMs, &on) || on.u != u || on.v != v ||
        !platformCellAt(view, *p, fuseAt - view.now, &blows))
        return false;
    b->cell = blows;
    b->h    = p->height;
    b->timed = true;
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

Blast roll(const BotView &view, int u, int v, int h, int facing, double droppedAt,
           bool rolled)
{
    Blast b;
    // One step along its facing, if it may: once moving it is held, and a
    // held bomb takes no further move of its own.  Ice carries it on, the
    // way it went, until it enters a cell that is not ice or may not go on.
    bool step = !rolled;
    for (int guard = 0; guard < 200; guard++) {
        if (!step && view.cell(u, v).kind != TILE_ICE)
            break;
        const int nu = u + DIR_DU[facing], nv = v + DIR_DV[facing];
        if (!view.inside(nu, nv) || !may_roll(view.cell(u, v), h, view.cell(nu, nv)))
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
            if (ride(view, nu, nv, in, r.from, &r) && ride(view, nu, nv, in + STEP_MS, r.from, &r))
                return r;
            b.lost  = true;
            b.timed = true;
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
    return roll(view, u, v, h, facing, droppedAt, false);
}

Blast predictBomb(const BotView &view, const BombView &bomb)
{
    // Moving, its cell is already the step's end; seen after its first tick
    // at rest, it has taken (or been refused) its one step.
    const bool rolled = bomb.moving || view.now - bomb.droppedAt > view.tickMs * 1.5;
    if (rolled && !bomb.moving && view.inside(bomb.u, bomb.v) && view.cell(bomb.u, bomb.v).track) {
        Blast r;
        r.from  = bomb.droppedAt + FUSE_MS;
        r.until = bomb.droppedAt + CLEAR_MS;
        if (ride(view, bomb.u, bomb.v, 0, r.from, &r))
            return r;
    }
    return roll(view, bomb.u, bomb.v, bomb.h, bomb.facing, bomb.droppedAt, rolled);
}

/* Whether the blast spends a bombable block. */
bool spendsBlock(const BotView &view, const Blast &b)
{
    if (b.lost)
        return false;
    for (int dv = -1; dv <= 1; dv++)
        for (int du = -1; du <= 1; du++) {
            const int u = b.cell.u + du, v = b.cell.v + dv;
            if (view.inside(u, v) && view.cell(u, v).kind == TILE_BOMBABLE &&
                !view.cell(u, v).busy && view.cell(u, v).height == b.h)
                return true;
        }
    return false;
}

}  // namespace bot
