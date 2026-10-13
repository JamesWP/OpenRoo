#include "bombmodel.h"
#include <stdlib.h>
#include "tile.h"

namespace bot {

namespace {

const double FUSE_MS = 2000.0;   // bomb.cpp
const double CLEAR_MS = 2400.0;

bool stairs(int k) { return k > 4 && k < 9; }

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
    return roll(view, bomb.u, bomb.v, bomb.h, bomb.facing, bomb.droppedAt, rolled);
}

}  // namespace bot
