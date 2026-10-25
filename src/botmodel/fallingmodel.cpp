#include "fallingmodel.h"
#include <algorithm>
#include "rules.h"
#include "tile.h"

namespace bot {

namespace {


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

std::vector<int> oneShotRuns(const BotView &view)
{
    const size_t n = view.cells.size();
    std::vector<int> run(n, -1);
    std::vector<char> tile(n, 0), crossing(n, 0);
    for (const FallingView &f : view.fallings) {
        const size_t at = (size_t)f.v * view.extentU + f.u;
        if (f.respawns || f.fallen)
            continue;
        tile[at] = 1;
        crossing[at] = f.armed || (f.u == view.player.u && f.v == view.player.v);
    }
    int runs = 0;
    for (size_t first = 0; first < n; first++) {
        if (!tile[first] || run[first] >= 0)
            continue;
        std::vector<size_t> cells{first};
        run[first] = runs;
        bool crossed = false;
        for (size_t k = 0; k < cells.size(); k++) {
            const int u = (int)(cells[k] % view.extentU), v = (int)(cells[k] / view.extentU);
            crossed = crossed || crossing[cells[k]];
            const int du[] = {1, -1, 0, 0}, dv[] = {0, 0, 1, -1};
            for (int d = 0; d < 4; d++) {
                if (!view.inside(u + du[d], v + dv[d]))
                    continue;
                const size_t next = (size_t)(v + dv[d]) * view.extentU + u + du[d];
                if (tile[next] && run[next] < 0) {
                    run[next] = runs;
                    cells.push_back(next);
                }
            }
        }
        if (crossed)
            for (const size_t c : cells)
                run[c] = -2;
        else
            runs++;
    }
    for (int &r : run)
        if (r == -2)
            r = -1;
    return run;
}

}  // namespace bot
