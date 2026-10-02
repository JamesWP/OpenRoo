/* Danger has two tiers.  A cell an awake foe occupies or could step into next
 * tick is lethal and is never entered, even when no safe route exists.
 * Falling tiles, and the cells around a frozen foe, are merely dangerous:
 * avoided when there is an alternative, crossed when there is not.  Foes' real
 * choices come from their own pathfinder; predicting every cell they could
 * reach is cheaper and robust.
 *
 * The collection order is a tour: nearest-neighbour for a start, improved by
 * 2-opt, over breadth-first walking distances so walls and ledges count.  It
 * is rebuilt only when the set of pickups changes, so the cost never lands on
 * every frame. */

#include <stdint.h>
#include "plan.h"
#include "logger.h"
#include <string.h>

#define CELLS      (WS_GRID_PITCH * WS_GRID_PITCH)
#define IDX(u, v)  ((v) + (u) * WS_GRID_PITCH)
#define UNREACHED  0x7fffffff
#define MAX_STOPS  64

static const int DU[4] = { 0, +1, 0, -1 };
static const int DV[4] = { -1, 0, +1, 0 };

static bool  g_danger[CELLS];  // dangerous: prefer not to enter
static bool  g_lethal[CELLS];  // lethal: never enter

/* While pickups remain, the exit is avoided: once enough crystals are held,
 * stepping on it ends the level and abandons what is left. */
static int   g_avoid_cell = -1;
static uint32_t g_danger_frame = 0xffffffff;

static void mark_danger(const Observation *o)
{
    if (g_danger_frame == o->frame) return;
    g_danger_frame = o->frame;
    memset(g_danger, 0, sizeof(g_danger));
    memset(g_lethal, 0, sizeof(g_lethal));

    // A falling tile kills only if stood on when it falls, and on the levels
    // that use them they are often the only way across.
    for (int u = 0; u < o->cols; u++)
        for (int v = 0; v < o->rows; v++)
            if (o->grid[IDX(u, v)].kind == WS_TILE_FALLING)
                g_danger[IDX(u, v)] = true;

    for (unsigned pass = 0; pass < 2; pass++) {
        const WsEntity *v = pass ? o->enemies : o->foes;
        unsigned n        = pass ? o->n_enemies : o->n_foes;
        for (unsigned i = 0; i < n; i++) {
            int fu = v[i].gu, fv = v[i].gv;
            if (fu < 0 || fv < 0 || fu >= o->cols || fv >= o->rows) continue;
            g_danger[IDX(fu, fv)] = true;
            g_lethal[IDX(fu, fv)] = true;
            // A frozen foe cannot move this tick, so only its own cell is
            // lethal; that is what makes a guarded pickup reachable while the
            // freeze bonus runs.
            if (v[i].frozen) continue;
            for (int d = 0; d < 4; d++) {
                int au = fu + DU[d], av = fv + DV[d];
                if (au < 0 || av < 0 || au >= o->cols || av >= o->rows) continue;
                g_danger[IDX(au, av)] = true;
                g_lethal[IDX(au, av)] = true;
            }
        }
    }
}

bool plan_is_dangerous(const Observation *o, int u, int v)
{
    if (u < 0 || v < 0 || u >= o->cols || v >= o->rows) return false;
    mark_danger(o);
    return g_danger[IDX(u, v)];
}

static int   g_dist[CELLS];
static int   g_prevClock[CELLS];
static int   g_queue[CELLS];

/* Fills g_dist and g_prevClock (the parent cell) from (su, sv).  avoid skips
 * dangerous cells except the start, so standing in danger does not make the
 * whole grid unreachable.  ignore_foes treats foe-occupied cells as passable.
 */
static void bfs(const Observation *o, int su, int sv, bool avoid,
                bool ignore_foes = false)
{
    for (int i = 0; i < CELLS; i++) { g_dist[i] = UNREACHED; g_prevClock[i] = -1; }
    if (su < 0 || sv < 0 || su >= o->cols || sv >= o->rows) return;
    mark_danger(o);

    int head = 0, tail = 0;
    int start = IDX(su, sv);
    g_dist[start] = 0;
    g_queue[tail++] = start;

    while (head < tail) {
        int cur = g_queue[head++];
        int cu = cur / WS_GRID_PITCH, cv = cur % WS_GRID_PITCH;
        for (int d = 0; d < 4; d++) {
            int au = cu + DU[d], av = cv + DV[d];
            if (au < 0 || av < 0 || au >= o->cols || av >= o->rows) continue;
            int adj = IDX(au, av);
            if (g_dist[adj] != UNREACHED) continue;
            if (ignore_foes) {
                if (!o->passableIgnoringFoes(cu, cv, au, av)) continue;
            } else if (!o->passable(cu, cv, au, av)) continue;
            if (g_lethal[adj]) continue;  // not even as a fallback
            if (avoid && g_danger[adj]) continue;
            if (adj == g_avoid_cell) continue;
            g_dist[adj] = g_dist[cur] + 1;
            g_prevClock[adj] = cur;
            g_queue[tail++] = adj;
        }
    }
}

static int  g_tour[MAX_STOPS];  // cell indices, in visiting order
static int  g_tour_n;
static int  g_tour_at;   // index of the next stop
static int  g_tour_sig;  // the pickup set the tour was built for

/* The stop being walked to, kept until it is collected or has been unreachable
 * for TARGET_PATIENCE frames.  Without it the choice flips frame to frame as
 * foes move.  90 frames is long enough for a falling tile to come back. */
#define TARGET_PATIENCE 90

static int g_target = -1;
static int g_target_fail;

void plan_reset(void)
{
    g_tour_n = 0; g_tour_at = 0; g_tour_sig = -1;
    g_target = -1; g_target_fail = 0;
}

static int pickup_signature(const Observation *o)
{
    int sig = 0;
    for (int u = 0; u < o->cols; u++)
        for (int v = 0; v < o->rows; v++)
            if (ws_is_pickup(o->grid[IDX(u, v)].contents))
                sig = sig * 31 + IDX(u, v) + 1;
    return sig;
}

/* Cost matrix over [player, stop 0, stop 1, ...]. */
static int  g_cost[MAX_STOPS + 1][MAX_STOPS + 1];
static int  g_stop[MAX_STOPS];

static void build_tour(const Observation *o, int pu, int pv)
{
    int n = 0;
    for (int u = 0; u < o->cols && n < MAX_STOPS; u++)
        for (int v = 0; v < o->rows && n < MAX_STOPS; v++)
            if (ws_is_pickup(o->grid[IDX(u, v)].contents))
                g_stop[n++] = IDX(u, v);

    g_tour_n = 0;
    g_tour_at = 0;
    if (n == 0) return;

    // Row 0 is the player, rows 1..n the pickups.  Foes are ignored when
    // costing: a pickup beside a foe is guarded, not unreachable, and must
    // stay on the tour so the wait-for-the-foe logic gets a chance.
    for (int a = 0; a <= n; a++) {
        int su = (a == 0) ? pu : g_stop[a - 1] / WS_GRID_PITCH;
        int sv = (a == 0) ? pv : g_stop[a - 1] % WS_GRID_PITCH;
        bfs(o, su, sv, false, true);
        for (int b = 0; b <= n; b++) {
            int t = (b == 0) ? IDX(pu, pv) : g_stop[b - 1];
            g_cost[a][b] = g_dist[t];
        }
    }

    // Rank 2 is a special pickup (anything but a crystal: an extra life, the
    // freeze bonus, a timer top-up), 1 a crystal with a foe on or beside it, 0
    // an ordinary crystal.  Higher ranks are taken first.  Foes start a level
    // dormant, so a guarded pickup is cheapest early.  Contents do not change
    // as foes move, so special pickups keep their rank across rebuilds.
    int rank[MAX_STOPS];
    for (int i = 0; i < n; i++) {
        int gu = g_stop[i] / WS_GRID_PITCH, gv = g_stop[i] % WS_GRID_PITCH;
        bool guarded = o->foeOnCell(gu, gv);
        for (int d = 0; d < 4 && !guarded; d++)
            if (o->foeOnCell(gu + DU[d], gv + DV[d])) guarded = true;
        rank[i] = (o->grid[g_stop[i]].contents != CONTENTS_CRYSTAL) ? 2 : (guarded ? 1 : 0);
    }

    // Nearest neighbour, highest rank first.
    bool used[MAX_STOPS];
    memset(used, 0, sizeof(used));
    int order[MAX_STOPS], m = 0, cur = 0;
    for (int k = 0; k < n; k++) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (used[i]) continue;
            if (g_cost[cur][i + 1] == UNREACHED) continue;
            if (best < 0) { best = i; continue; }
            if (rank[i] != rank[best]) {  // a higher rank wins outright
                if (rank[i] > rank[best]) best = i;
                continue;
            }
            if (g_cost[cur][i + 1] < g_cost[cur][best + 1]) best = i;
        }
        if (best < 0) break;  // the rest are unreachable
        used[best] = true;
        order[m++] = best;
        cur = best + 1;
    }

    // 2-opt: reverse any segment that shortens the open tour, in at most eight
    // passes.  Segments holding a ranked stop are left alone, or distance
    // would undo the ranking.
    for (int pass = 0; pass < 8; pass++) {
        bool improved = false;
        for (int i = 0; i < m - 1; i++) {
            for (int j = i + 1; j < m; j++) {
                bool has_ranked = false;
                for (int x = i; x <= j && !has_ranked; x++)
                    if (rank[order[x]] > 0) has_ranked = true;
                if (has_ranked) continue;
                int a = (i == 0) ? 0 : order[i - 1] + 1;
                int b = order[i] + 1, c = order[j] + 1;
                int dNext = (j + 1 < m) ? order[j + 1] + 1 : -1;
                int before = g_cost[a][b];
                int after  = g_cost[a][c];
                if (dNext >= 0) { before += g_cost[c][dNext]; after += g_cost[b][dNext]; }
                if (after < before) {
                    for (int x = i, y = j; x < y; x++, y--) {
                        int t = order[x]; order[x] = order[y]; order[y] = t;
                    }
                    improved = true;
                }
            }
        }
        if (!improved) break;
    }

    for (int i = 0; i < m; i++) g_tour[i] = g_stop[order[i]];
    g_tour_n = m;

    g_logger.write("plan: tour rebuilt from (%d,%d): %d of %d stops reachable\n",
              pu, pv, m, n);
    for (int i = 0; i < m && i < 6; i++)
        g_logger.write("plan:   [%d] (%d,%d) contents=%u%s cost=%d\n", i,
                  g_tour[i] / WS_GRID_PITCH, g_tour[i] % WS_GRID_PITCH,
                  o->grid[g_tour[i]].contents,
                  rank[order[i]] == 2 ? " SPECIAL" : rank[order[i]] ? " GUARDED" : "",
                  g_cost[i == 0 ? 0 : order[i-1] + 1][order[i] + 1]);
    for (int i = 0; i < n; i++)
        if (o->grid[g_stop[i]].contents != CONTENTS_CRYSTAL)
            g_logger.write("plan:   special contents=%u at (%d,%d) reachable=%s%s\n",
                      o->grid[g_stop[i]].contents,
                      g_stop[i] / WS_GRID_PITCH, g_stop[i] % WS_GRID_PITCH,
                      g_cost[0][i + 1] == UNREACHED ? "NO" : "yes",
                      rank[i] == 2 ? " SPECIAL" : rank[i] ? " GUARDED" : "");
}

/* Walks the parent chain back from goal to the cell next to the start: the one
 * cell that can be stepped to this frame. */
static bool first_hop(int start, int goal, int *nu, int *nv)
{
    if (goal < 0 || goal == start || g_dist[goal] == UNREACHED) return false;
    int cur = goal;
    while (g_prevClock[cur] != start) {
        if (g_prevClock[cur] < 0) return false;
        cur = g_prevClock[cur];
    }
    *nu = cur / WS_GRID_PITCH;
    *nv = cur % WS_GRID_PITCH;
    return true;
}

static bool step_towards(const Observation *o, int pu, int pv, int goal,
                         int *nu, int *nv)
{
    int start = IDX(pu, pv);
    bfs(o, pu, pv, true);  // prefer a safe route
    if (first_hop(start, goal, nu, nv)) return true;
    bfs(o, pu, pv, false);  // but move regardless
    return first_hop(start, goal, nu, nv);
}

bool plan_next_step(const Observation *o, int pu, int pv, int *nu, int *nv,
                    bool seek_exit)
{
    if (pu < 0 || pv < 0 || pu >= o->cols || pv >= o->rows) return false;

    if (seek_exit) {
        int goal = IDX(o->exit_cell[0], o->exit_cell[1]);
        if (o->exit_cell[0] >= o->cols || o->exit_cell[1] >= o->rows) return false;
        g_avoid_cell = -1;
        return step_towards(o, pu, pv, goal, nu, nv);
    }

    // Collecting: keep off the exit so the level is not ended early.
    g_avoid_cell = (o->exit_cell[0] < o->cols && o->exit_cell[1] < o->rows)
                 ? IDX(o->exit_cell[0], o->exit_cell[1]) : -1;

    // Stay with the current target while it is still worth having.
    if (g_target >= 0 && ws_is_pickup(o->grid[g_target].contents) &&
        g_target != IDX(pu, pv)) {
        if (step_towards(o, pu, pv, g_target, nu, nv)) {
            g_target_fail = 0;
            return true;
        }
        // Blocked.  If only a foe is in the way, hold the target and wait for
        // it to move rather than wander off.
        bfs(o, pu, pv, false, true);
        if (g_dist[g_target] != UNREACHED) return false;

        if (++g_target_fail < TARGET_PATIENCE) return false;  // wait it out
    }
    g_target = -1;
    g_target_fail = 0;

    int sig = pickup_signature(o);
    if (sig != g_tour_sig) {  // something was collected
        g_tour_sig = sig;
        build_tour(o, pu, pv);
    }

    // Skip stops already taken, and try each in order, so one that has become
    // unreachable does not stall the tour.
    for (int i = g_tour_at; i < g_tour_n; i++) {
        int goal = g_tour[i];
        if (!ws_is_pickup(o->grid[goal].contents)) { g_tour_at = i + 1; continue; }
        if (goal == IDX(pu, pv)) { g_tour_at = i + 1; continue; }
        if (step_towards(o, pu, pv, goal, nu, nv)) {
            g_target = goal;
            g_target_fail = 0;
            return true;
        }
    }

    // Nothing on the tour is reachable: force a rebuild next frame.  Falling
    // tiles vanish for a moment and come back, so a tour built during the gap
    // sees a severed map and would otherwise never be rebuilt.
    g_tour_sig = -1;
    return false;
}
