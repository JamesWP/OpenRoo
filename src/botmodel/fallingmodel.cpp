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
    for (const FallingView &f : view.fallings)
        if (f.u == c.u && f.v == c.v && f.armed)
            at = std::min(at, f.armedAt + FALL_MS - view.now);
    return at;
}

double backAt(const BotView &view, Step c)
{
    for (const FallingView &f : view.fallings)
        if (f.u == c.u && f.v == c.v && f.fallen)
            return f.respawns ? f.armedAt + BACK_MS - view.now : 1e12;
    return -1;
}

bool goneAt(const BotView &view, Step c, double atMs)
{
    const double back = backAt(view, c);
    return back >= 0 && atMs < back;
}

}  // namespace bot
