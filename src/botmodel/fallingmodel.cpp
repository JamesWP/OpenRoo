#include "fallingmodel.h"
#include <algorithm>
#include "tile.h"

namespace bot {

namespace {

const double FALL_MS = 1500.0;  // fallingtile.cpp: armed to falling
const double BACK_MS = 5000.0;  // armed to respawned

}  // namespace

double fallsAt(const BotView &view, Step c, double enterMs)
{
    if (!view.inside(c.u, c.v) || view.cell(c.u, c.v).kind != TILE_FALLING)
        return -1;
    double at = enterMs + FALL_MS;
    for (const FallingView &f : view.fallings) {
        if (f.u != c.u || f.v != c.v || !f.armed)
            continue;
        // Armed already: it falls when that arming says, unless it has
        // fallen and is back by the time the player steps on, which arms it
        // afresh.
        const double back = f.respawns ? f.armedAt + BACK_MS - view.now : 1e12;
        if (enterMs < back)
            at = std::min(at, f.armedAt + FALL_MS - view.now);
    }
    // Never < 0, which would say there is no falling tile.
    return std::max(at, 0.0);
}

double backAt(const BotView &view, Step c)
{
    for (const FallingView &f : view.fallings)
        if (f.u == c.u && f.v == c.v && f.fallen)
            return f.respawns ? f.armedAt + BACK_MS - view.now : 1e12;
    return -1;
}

bool goneSpell(const BotView &view, Step c, double armedMs, double *from, double *back)
{
    if (!view.inside(c.u, c.v) || view.cell(c.u, c.v).kind != TILE_FALLING)
        return false;
    bool respawns = true;
    for (const FallingView &f : view.fallings)
        if (f.u == c.u && f.v == c.v)
            respawns = f.respawns;
    *from = armedMs + FALL_MS;
    *back = respawns ? armedMs + BACK_MS : 1e12;
    return true;
}

bool goneAt(const BotView &view, Step c, double atMs)
{
    const double back = backAt(view, c);
    return back >= 0 && atMs < back;
}

}  // namespace bot
