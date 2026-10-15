#include "glide.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include "tile.h"

namespace bot {

namespace {

/* The game's (movableentity.cpp): free fall h = H - (3 + 5.405 t) t (t in s)
 * until it has dropped more than 2; then down 0.004 a ms. */
const double FALL_V      = 3.0;
const double FALL_A      = 5.405;
const double OPEN_DROP   = 3.0;
const double SINK_PER_MS = 0.004;
/* Margins: a step off dropping less than this only comes down on the cell
 * below; the player stays this far above the top of a cell it passes over or
 * comes down on, and comes down no lower than LOWEST.  Against the model's measured error: the glider opened 16 ms
 * late (0.06 of a height) in Egypt\ParaglideStart, the sinking on time. */
const int    LAUNCH_DROP = 5;
const double OVER        = 0.3;
/* A glide is refused only a cell whose top is one above the player's cell
 * (its height, rounded down, plus one); it enters any other, passing through
 * one that stands higher still.  The model enters one only this far above
 * the player as the step starts, and never turns or stops in it. */
const double THROUGH     = 2.5;
const int    LOWEST      = 3;
/* High enough above a cell to be out of reach of what is on it. */
const double ALOFT       = 2.0;

/* The time from the start of the free fall to the glider opening, ms. */
double openMs()
{
    // (FALL_V + FALL_A t) t = OPEN_DROP
    const double t = (-FALL_V + std::sqrt(FALL_V * FALL_V + 4 * FALL_A * OPEN_DROP)) / (2 * FALL_A);
    return t * 1000.0;
}

bool wall(const Cell &c)
{
    return c.kind == TILE_IMPASSABLE || (c.kind == TILE_BOMBABLE && !c.busy);
}

/* Plain ground to come down on: nothing that moves, carries or works on the
 * player when it lands. */
bool landable(const Cell &c)
{
    if (c.height < LOWEST)
        return false;
    return c.kind == TILE_KIND_01 || c.kind == TILE_START || c.kind == TILE_EXIT ||
           c.kind == TILE_BRIDGE_DECK || (c.kind == TILE_BOMBABLE && c.busy);
}

const int DU[] = {0, 0, 1, 0, -1};
const int DV[] = {0, -1, 0, 1, 0};

}  // namespace

bool glideLaunch(const BotView &view, int u, int v, int f)
{
    const Cell &here = view.cell(u, v);
    const int bu = u + DU[f], bv = v + DV[f];
    if (!view.inside(bu, bv))
        return false;
    const Cell &b = view.cell(bu, bv);
    return !wall(b) && here.height - b.height >= OPEN_DROP;
}

std::vector<Landing> glideLandings(const BotView &view, int u, int v, int f)
{
    std::vector<Landing> out;
    if (!glideLaunch(view, u, v, f))
        return out;
    const double stepMs = view.player.stepMs;
    const double top = view.cell(u, v).height - OPEN_DROP;  // as it opens
    const double open = stepMs + openMs();                  // when, from the launch
    auto heightAt = [&](double t) { return top - (t - open) * SINK_PER_MS; };

    // A short drop: the glider opens as the player comes down on the cell
    // below, or just above it.
    const Step b{u + DU[f], v + DV[f]};
    const Cell &below = view.cell(b.u, b.v);
    if (view.cell(u, v).height - below.height < LAUNCH_DROP) {
        if (landable(below)) {
            Landing l;
            l.cell = b;
            l.facing = f;
            l.ms = open + (top - below.height) / SINK_PER_MS;
            l.path = {{u, v}, b};
            l.schedule = {{0, {u, v}}, {0, b}};
            out.push_back(std::move(l));
        }
        return out;
    }

    // Dijkstra over (cell, facing) in the air, from over the cell stepped
    // onto, at the opening: the earliest is the highest.
    const int eu = view.extentU, n = eu * view.extentV * 4;
    auto index = [&](int cu, int cv, int cf) { return (cv * eu + cu) * 4 + cf - 1; };
    std::vector<double> at(n, -1);
    std::vector<int> parent(n, -1);
    using Item = std::pair<double, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open_;
    const int s = index(u + DU[f], v + DV[f], f);
    at[s] = open;
    open_.push({open, s});
    while (!open_.empty()) {
        const auto [t, i] = open_.top();
        open_.pop();
        if (t > at[i])
            continue;
        const int cf = i % 4 + 1, cu = (i / 4) % eu, cv = (i / 4) / eu;
        auto relax = [&](int j, double tj) {
            if (at[j] < 0 || tj < at[j]) {
                at[j] = tj;
                parent[j] = i;
                open_.push({tj, j});
            }
        };
        const double te = t + stepMs;
        if (heightAt(te) < LOWEST + OVER)
            continue;
        // Passing through a cell that stands higher, it keeps on through it.
        if (view.cell(cu, cv).height <= heightAt(t)) {
            relax(index(cu, cv, NavModel::turned(cf, 1)), te);
            relax(index(cu, cv, NavModel::turned(cf, -1)), te);
        }
        for (const int d : {cf, NavModel::opposite(cf)}) {
            const int nu = cu + DU[d], nv = cv + DV[d];
            if (!view.inside(nu, nv))
                continue;
            const Cell &c = view.cell(nu, nv);
            const bool over = c.height == 0 || c.height <= heightAt(te) - OVER;
            const bool through = c.height >= heightAt(t) + THROUGH;
            if (wall(c) || !(over || through))
                continue;
            relax(index(nu, nv, cf), te);
        }
    }

    // Each cell's earliest state, if the player is over it before it sinks
    // to its top.
    for (int cv = 0; cv < view.extentV; cv++)
        for (int cu = 0; cu < eu; cu++) {
            const Cell &c = view.cell(cu, cv);
            if (!landable(c))
                continue;
            int best = -1;
            for (int cf = 1; cf <= 4; cf++) {
                const int i = index(cu, cv, cf);
                if (at[i] >= 0 && (best < 0 || at[i] < at[best]))
                    best = i;
            }
            const double lands = open + (top - c.height) / SINK_PER_MS;
            if (best < 0 || heightAt(at[best]) < c.height + OVER)
                continue;
            Landing l;
            l.cell = {cu, cv};
            l.facing = best % 4 + 1;
            l.ms = lands;
            // The cells entered, back from the landing; a turn enters none.
            std::vector<std::pair<double, Step>> entered;
            for (int i = best; i >= 0; i = parent[i]) {
                const int p = parent[i];
                const Step here{(i / 4) % eu, (i / 4) / eu};
                const Step from = p < 0 ? Step{u, v} : Step{(p / 4) % eu, (p / 4) / eu};
                if (here.u != from.u || here.v != from.v)
                    entered.push_back({p < 0 ? 0 : at[p], here});
            }
            l.path.push_back({u, v});
            l.schedule.push_back({0, {u, v}});
            for (auto it = entered.rbegin(); it != entered.rend(); ++it) {
                l.path.push_back(it->second);
                // Over the cell through the step and the next: well above it?
                const double low = heightAt(std::max(it->first, open) + 2 * stepMs);
                l.schedule.push_back({it->first, it->second,
                                      low - view.cell(it->second.u, it->second.v).height >= ALOFT});
            }
            out.push_back(std::move(l));
        }
    return out;
}

}  // namespace bot
