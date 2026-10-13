#include "fallingmodel.h"
#include <algorithm>
#include "tile.h"

namespace bot {

namespace {

const double FALL_MS = 1500.0;  // fallingtile.cpp: armed to falling

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

}  // namespace bot
