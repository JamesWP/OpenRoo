#include "glide.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include "tile.h"

namespace bot {

namespace {

/* The game's (movableentity.cpp), in its own float constants: the free fall
 * h = H + (-3 - 5.405 t) t (t the whole ms since it began, in s), the glider
 * opening once the player's cell is more than 2 below where it fell from;
 * then down 0.004 a ms of each tick. */
const float FALL_SPEED  = -3.0f;
const float FALL_ACCEL  = 5.405f;
const float MS_TO_S     = 0.001f;
const float GLIDE_RATE  = 0.004f;
const int   OPEN_DROP   = 3;
/* The glide ends (the player falls) once its cell is lower than this. */
const int   LOWEST      = 2;
/* High enough above a cell to be out of reach of what is on it. */
const double ALOFT      = 2.0;
/* A step off a lift is refused while the player stands further than this
 * from its cell's height (K_STAIRS_EPS). */
const double LIFT_EPS   = 0.2;
/* A refused move is held this many ticks at most (the motor gives up on a
 * key held longer, motor.cpp). */
const int    HOLD_TICKS = 24;

bool wall(const Cell &c)
{
    return c.kind == TILE_IMPASSABLE || (c.kind == TILE_BOMBABLE && !c.busy);
}

bool stairs(const Cell &c)
{
    return c.kind >= TILE_STAIRS_1 && c.kind <= TILE_STAIRS_4;
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

/* The player in the air: its height and its cell's (heightCell_). */
struct Air {
    float y;
    int   h;
};

/* Whether the player at rest over c has come down on it. */
bool lands(const Air &a, const Cell &c)
{
    return c.height != 0 && a.h == c.height;
}

/* One pass of the gliding branch over c, for one tick of tickMs: it sinks,
 * or, within half a height below c's top, stands on it.  False once the
 * glide is over (the player then falls). */
bool sink(Air &a, const Cell &c, double tickMs)
{
    const unsigned char th = c.height;
    if ((float)th < a.y || a.y < (float)th - 0.5f) {
        const double h = (double)a.y - tickMs * (double)GLIDE_RATE;
        a.y = (float)h;
        a.h = (int)(long long)h + 1;
    } else {
        a.h = th;
        a.y = (float)th;
    }
    // Stairs let it settle half a height lower.
    if (stairs(c) && (float)th < a.y && a.y <= (float)th + 0.5)
        a.h -= 1;
    return a.h >= LOWEST;
}

}  // namespace

bool glideLaunch(const BotView &view, int u, int v, int f)
{
    const Cell &here = view.cell(u, v);
    const int bu = u + DU[f], bv = v + DV[f];
    if (!view.inside(bu, bv))
        return false;
    const Cell &b = view.cell(bu, bv);
    if (wall(b) || NavModel::moving(b))
        return false;
    // Off an edge, or under a tile two or more higher (not stairs): a tile
    // is a thin slab with open space below, and the player walks under it
    // and falls.
    return here.height - b.height >= OPEN_DROP ||
           (b.height >= here.height + 2 && !stairs(b) && !stairs(here));
}

/* The game's tick by tick: each tick the integrator runs once, and once more
 * on a tick a move is pressed for (the key's handler runs it to start the
 * move), which sinks the player a tick's worth more.  So the times are in
 * ticks from the launch's: its step ends, the player falls from the edge's
 * height, the glider opens, and from then each move, held from before, starts
 * on the tick the last ended. */
std::vector<Landing> glideLandings(const BotView &view, int u, int v, int f)
{
    std::vector<Landing> out;
    if (!glideLaunch(view, u, v, f))
        return out;
    const double tick = view.tickMs > 0 ? view.tickMs : 1000.0 / 60.0;
    // A move's ticks: it ends on the first whose clock is its time on.
    int moveTicks = 1;
    while (moveTicks * tick < view.player.stepMs - 1e-6)
        moveTicks++;
    const int H = view.cell(u, v).height;
    const Step b{u + DU[f], v + DV[f]};

    // The free fall, from the tick the launch step ends, to the glider opening.
    Air air{(float)H, H + 1};
    int open = moveTicks;
    for (int j = 1;; j++) {
        const double ts = std::floor(j * tick + 1e-6) * (double)MS_TO_S;
        const double h  = ((double)FALL_SPEED - ts * (double)FALL_ACCEL) * ts + H;
        air = {(float)h, (int)(long long)h + 1};
        if (H - air.h >= OPEN_DROP) {
            open += j;
            break;
        }
    }

    // Dijkstra over (cell, facing) in the air, by tick: a state is the player
    // at the end of a move (the first, the opening), the tick's integrator
    // run and it still in the air.
    const int eu = view.extentU, cells = eu * view.extentV, n = cells * 4;
    auto index = [&](int cu, int cv, int cf) { return (cv * eu + cu) * 4 + cf - 1; };
    std::vector<int> at(n, -1), parent(n, -1), started(n, -1);  // started: the move's first tick
    std::vector<Air> airAt(n);
    // The earliest landing on each cell: the tick, the state it is from, and
    // if a move from that state came down (not a stop on it), the tick the
    // move started; the facing it leaves.
    struct Down {
        int tick = -1, from = -1, moveStart = -1, facing = 0;
    };
    std::vector<Down> down(cells);
    auto landOn = [&](int cu, int cv, int tickAt, int from, int moveStart, int facing) {
        Down &d = down[cv * eu + cu];
        if (!landable(view.cell(cu, cv)) || (d.tick >= 0 && d.tick <= tickAt))
            return;
        d = {tickAt, from, moveStart, facing};
    };
    using Item = std::pair<int, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
    if (lands(air, view.cell(b.u, b.v))) {
        landOn(b.u, b.v, open + 1, -1, -1, f);  // the tick after the glider opens
    } else {
        const int s0 = index(b.u, b.v, f);
        at[s0] = open;
        airAt[s0] = air;
        queue.push({open, s0});
    }
    while (!queue.empty()) {
        const auto [t, i] = queue.top();
        queue.pop();
        if (t > at[i])
            continue;
        const int cf = i % 4 + 1, cu = (i / 4) % eu, cv = (i / 4) / eu;
        const Cell &here = view.cell(cu, cv);

        // Still from here on: it sinks until it comes down, unless it is
        // over nothing or inside a cell that stands higher.
        Air still = airAt[i];
        for (int k = t + 1; here.height != 0 && still.h >= here.height; k++) {
            if (lands(still, here)) {
                landOn(cu, cv, k, i, -1, cf);
                break;
            }
            if (!sink(still, here, tick))
                break;
        }

        // A move from here, its key held from this tick on: each tick the
        // key's handler runs the integrator again, then starts the move or
        // is refused (a refusal over a lift or below a cell's top passes as
        // the player sinks).
        auto move = [&](int nu, int nv, int d, int nf) {
            const Cell &to = view.cell(nu, nv);
            const bool step = nu != cu || nv != cv;
            if (step && wall(to))
                return;
            // A lift's height or a platform's is where it has got to: the
            // glide passes over one only out of reach of its highest.
            const int highest = std::max({(int)to.height, (int)to.liftTop, (int)to.liftBottom});
            if (step && NavModel::moving(to) && (to.track || airAt[i].h <= highest + 1))
                return;
            Air a = airAt[i];
            for (int k = t; k <= t + HOLD_TICKS; k++) {
                if (k > t && (lands(a, here) || !sink(a, here, tick)))
                    return;
                if (lands(a, here) || !sink(a, here, tick))
                    return;   // come down, or fallen
                if (step) {
                    if (to.kind != TILE_EMPTY && a.h == to.height - 1)
                        continue;
                    if (here.kind == TILE_LIFT && std::fabs(a.y - (float)a.h) > (float)LIFT_EPS)
                        continue;
                    if (stairs(here)) {
                        const int along = here.kind - 4;
                        if (d != along && d != NavModel::opposite(along) && to.height == here.height + 1)
                            return;
                    }
                }
                Air m = a;
                if (step && stairs(to) && to.height < here.height)
                    m.h -= 1;
                for (int j = 1; j < moveTicks; j++)
                    if (!sink(m, to, tick))
                        return;
                const int end = k + moveTicks;
                if (lands(m, to)) {
                    landOn(nu, nv, end, i, k, nf);
                    return;
                }
                if (!sink(m, to, tick))
                    return;
                const int j = index(nu, nv, nf);
                if (at[j] < 0 || end < at[j]) {
                    at[j] = end;
                    airAt[j] = m;
                    parent[j] = i;
                    started[j] = k;
                    queue.push({end, j});
                }
                return;
            }
        };
        move(cu, cv, 0, NavModel::turned(cf, 1));
        move(cu, cv, 0, NavModel::turned(cf, -1));
        for (const int d : {cf, NavModel::opposite(cf)}) {
            const int nu = cu + DU[d], nv = cv + DV[d];
            if (view.inside(nu, nv))
                move(nu, nv, d, cf);
        }
    }

    // A landing for each cell come down on: the way there, back from it.
    for (int c = 0; c < cells; c++) {
        const Down &dn = down[c];
        if (dn.tick < 0)
            continue;
        const Step cell{c % eu, c / eu};
        Landing l;
        l.cell = cell;
        l.facing = dn.facing;
        l.ms = dn.tick * tick;
        l.path.push_back({u, v});
        l.schedule.push_back({0, {u, v}});
        l.path.push_back(b);
        l.schedule.push_back({0, b, view.cell(b.u, b.v).height + ALOFT <= H - OPEN_DROP});
        Step prev = b;
        // Into a cell on a move started on tick k, from y high.
        auto enter = [&](Step to, int k, float y) {
            if (to.u == prev.u && to.v == prev.v)
                return;   // a turn
            l.path.push_back(to);
            // Over the cell through the step and the next: well above it?
            const double low = y - 2 * moveTicks * tick * GLIDE_RATE;
            l.schedule.push_back({k * tick, to, low - view.cell(to.u, to.v).height >= ALOFT});
            prev = to;
        };
        std::vector<int> states;
        for (int i = dn.from; i >= 0; i = parent[i])
            states.push_back(i);
        for (auto it = states.rbegin(); it != states.rend(); ++it)
            if (parent[*it] >= 0)
                enter({(*it / 4) % eu, (*it / 4) / eu}, started[*it], airAt[parent[*it]].y);
        if (dn.moveStart >= 0)
            enter(cell, dn.moveStart, airAt[dn.from].y);
        out.push_back(std::move(l));
    }
    return out;
}

}  // namespace bot
