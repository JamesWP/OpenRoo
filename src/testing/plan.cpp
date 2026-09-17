/* Route planning for the policy — AI_PLAN.md Stage 6.
 *
 * ── Foe avoidance ───────────────────────────────────────────────────────
 *
 * Danger covers two things: cells a foe could reach next tick, and falling
 * tiles.  Both are places to prefer not to be rather than places you cannot
 * go.
 *
 * Foes are already *blocked* — SpawnFoeObject writes the foe's kind into the
 * destination tile's occupant byte, and ws_passable() refuses a cell whose
 * occupant is set.  That stops the policy walking into a foe standing still;
 * it does nothing about one that is about to move.
 *
 * Modelling their choice exactly is not worth it.  FUN_0043a9d0 does not use a
 * simple rule: it runs a real pathfinder (FUN_00401c20) from the foe's cell to
 * the player's, reads the first node off the result and converts it to a
 * direction.  Reproducing that faithfully means reproducing their pathfinder
 * and its tie-breaking.  What is cheap and robust instead is to treat every
 * cell a foe could occupy next tick — its own, plus its four neighbours — as
 * dangerous, and prefer routes that avoid them.
 *
 * "Prefer", not "forbid": a foe closing in can make every route dangerous, and
 * refusing to move then is strictly worse than moving.  So the search runs
 * twice, once avoiding danger and once ignoring it.
 *
 * ── Collection order ────────────────────────────────────────────────────
 *
 * Always walking to the *nearest* pickup is a greedy tour, and it shows: the
 * policy crosses back over ground it has already cleared.  This builds a real
 * tour instead — nearest-neighbour for a starting order, then 2-opt until it
 * stops improving — over exact BFS path lengths, not straight-line distance,
 * so walls and ledges count.
 *
 * The tour is recomputed only when the set of remaining pickups changes, which
 * is at most once per pickup, so the cost never lands on a per-frame path.
 */
#include "plan.h"
#include "log.h"
#include <string.h>

#define CELLS      (WS_GRID_PITCH * WS_GRID_PITCH)
#define IDX(u, v)  ((v) + (u) * WS_GRID_PITCH)
#define UNREACHED  0x7fffffff
#define MAX_STOPS  64

static const int DU[4] = { 0, +1, 0, -1 };
static const int DV[4] = { -1, 0, +1, 0 };

/* ── danger ─────────────────────────────────────────────────────────────── */

static bool  g_danger[CELLS];
/* Danger has two tiers.
 *
 * LETHAL is a cell an awake foe occupies or could step into next tick.  Going
 * there is not a risk, it is a death, so it is refused outright — including by
 * the "ignore danger and move anyway" fallback.  That fallback previously
 * treated a foe like any other hazard, and the result was visible on screen:
 * the policy would route around a foe when it could and walk straight into it
 * when it could not, losing two lives in a single run.
 *
 * DANGER is ground to prefer not to stand on — falling tiles, and the cells
 * around a frozen foe that cannot act this tick.  Those are avoided when there
 * is an alternative and crossed when there is not.
 */
static bool  g_lethal[CELLS];
/* While pickups remain, the exit is a hazard rather than a destination: once
 * gems_collected >= gems_required, merely stepping on it ends the level, so a
 * route that crosses it would finish early and abandon whatever is left.
 *
 * This is a guard, not a fix for anything observed -- on Forest\DestrStart the
 * exit is a dead end and is not on the way to anything, so it was NOT the
 * reason the extra life there kept being missed. */
static int   g_avoid_cell = -1;
static DWORD g_danger_frame = 0xffffffff;

static void mark_danger(const Observation *o)
{
    if (g_danger_frame == o->frame) return;
    g_danger_frame = o->frame;
    memset(g_danger, 0, sizeof(g_danger));
    memset(g_lethal, 0, sizeof(g_lethal));

    /* Falling tiles are dangerous ground rather than blocked ground: standing
     * on one kills, crossing one does not, and on the levels that use them they
     * are laid out as the only way across. */
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
            /* A frozen foe cannot step anywhere this tick, so only the cell it
             * occupies is off limits — its neighbours are ordinary ground.
             * That is what makes a pickup guarded by a foe reachable while the
             * freeze bonus is running. */
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

/* ── breadth-first distance field ───────────────────────────────────────── */

static int   g_dist[CELLS];
static int   g_prev[CELLS];
static int   g_queue[CELLS];

/* Fill g_dist/g_prev from (su,sv).  `avoid` skips dangerous cells except the
 * start it(self) — standing in danger must not make the whole grid unreachable. */
static void bfs(const Observation *o, int su, int sv, bool avoid,
                bool ignore_foes = false)
{
    for (int i = 0; i < CELLS; i++) { g_dist[i] = UNREACHED; g_prev[i] = -1; }
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
                if (!ws_passable_ignoring_foes(o, cu, cv, au, av)) continue;
            } else if (!ws_passable(o, cu, cv, au, av)) continue;
            if (g_lethal[adj]) continue;          /* never, even as a fallback */
            if (avoid && g_danger[adj]) continue;
            if (adj == g_avoid_cell) continue;
            g_dist[adj] = g_dist[cur] + 1;
            g_prev[adj] = cur;
            g_queue[tail++] = adj;
        }
    }
}

/* ── the tour ───────────────────────────────────────────────────────────── */

static int  g_tour[MAX_STOPS];   /* cell indices, in visiting order */
static int  g_tour_n;
static int  g_tour_at;           /* how far along we are */
static int  g_tour_sig;          /* pickup-set signature the tour was built for */

/* The stop we are currently walking to, and how long it has been unreachable.
 *
 * Without this the policy visibly dithered — it would set off towards a gem,
 * turn around, then head back.  Two things made the choice flip frame to
 * frame: the danger-avoiding search depends on where the foes are, which
 * changes every frame, so which stops look reachable changes with it; and a
 * forced tour rebuild re-runs nearest-neighbour from wherever the player now
 * stands, which can reorder the head of the tour.
 *
 * So pick a target and keep it.  A target is only abandoned once it has been
 * collected, or has been unreachable for TARGET_PATIENCE consecutive frames —
 * long enough for a falling tile to respawn, so a momentary gap in the floor
 * does not cause a change of mind. */
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

/* Cost matrix over [player, stop0, stop1, ...]. */
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

    /* Row 0 is the player; rows 1..n are the pickups.  Distances come from a
     * BFS per node, so they are real walking distances over passable ground
     * rather than straight lines. */
    for (int a = 0; a <= n; a++) {
        int su = (a == 0) ? pu : g_stop[a - 1] / WS_GRID_PITCH;
        int sv = (a == 0) ? pv : g_stop[a - 1] % WS_GRID_PITCH;
        /* Ignore foes when costing the tour.  A pickup with a foe standing on
         * or beside it is guarded, not unreachable — some are placed that way
         * deliberately — and excluding it here means it never becomes a target
         * at all, so the wait-for-the-foe-to-move logic never gets a chance.
         * That is how the extra life on Forest\DestrStart was walked past. */
        bfs(o, su, sv, false, true);
        for (int b = 0; b <= n; b++) {
            int t = (b == 0) ? IDX(pu, pv) : g_stop[b - 1];
            g_cost[a][b] = g_dist[t];
        }
    }

    /* Which stops are guarded -- a foe on or beside them right now.
     *
     * These are taken FIRST, ahead of anything nearer.  Foes start a level
     * dormant: on Forest\DestrStart the one guarding the extra life sits
     * still for 650 frames before it moves at all, and the life is the cell
     * next to it.  A plain nearest-first tour reaches that corner late, by
     * which time the foe is awake and the pickup costs a life to reach --
     * a hand-played run had to lure the foe onto a falling tile to kill it
     * before it could be taken.  Going early is much cheaper than fighting.
     *
     * A guarded stop that has become unreachable still falls out of the tour
     * below, and the danger-avoidance and wait-for-the-foe logic still apply
     * on the way there, so this changes the order rather than the safety. */
    /* Rank 2 = a special pickup, 1 = one a foe is standing on or beside,
     * 0 = an ordinary crystal.  Higher ranks are taken first.
     *
     * Guardedness alone was not a durable key.  It is recomputed from where
     * the foes are at the moment the tour is built, and the tour is rebuilt
     * every time anything is collected — so the extra life on
     * Forest\DestrStart started out ranked first (the foe was dormant beside
     * it), and then LOST that rank the moment the foe woke and wandered off.
     * Watching it, the player heads for the life, collects a gem on the way,
     * the tour rebuilds without the life ranked, and it turns round and goes
     * back to the crystals.  Which is exactly what happened on screen.
     *
     * A pickup's contents value does not decay, so rank on that first: a
     * crystal is contents 1 and there are dozens; anything else is rare and
     * worth a detour (7 is the extra life, and the freeze bonus and the timer
     * top-up are likewise one-offs). */
    int rank[MAX_STOPS];
    for (int i = 0; i < n; i++) {
        int gu = g_stop[i] / WS_GRID_PITCH, gv = g_stop[i] % WS_GRID_PITCH;
        bool guarded = ws_foe_on_cell(o, gu, gv);
        for (int d = 0; d < 4 && !guarded; d++)
            if (ws_foe_on_cell(o, gu + DU[d], gv + DV[d])) guarded = true;
        rank[i] = (o->grid[g_stop[i]].contents != CONTENTS_CRYSTAL) ? 2 : (guarded ? 1 : 0);
    }

    /* Nearest neighbour, guarded stops first. */
    bool used[MAX_STOPS];
    memset(used, 0, sizeof(used));
    int order[MAX_STOPS], m = 0, cur = 0;
    for (int k = 0; k < n; k++) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (used[i]) continue;
            if (g_cost[cur][i + 1] == UNREACHED) continue;
            if (best < 0) { best = i; continue; }
            if (rank[i] != rank[best]) {               /* higher rank wins outright */
                if (rank[i] > rank[best]) best = i;
                continue;
            }
            if (g_cost[cur][i + 1] < g_cost[cur][best + 1]) best = i;
        }
        if (best < 0) break;            /* the rest are unreachable */
        used[best] = true;
        order[m++] = best;
        cur = best + 1;
    }

    /* 2-opt: reverse any segment that shortens the open tour.  Bounded so a
     * pathological level cannot spend the frame here.  Segments containing a
     * guarded stop are left alone, or the reordering above would be undone by
     * the very distance argument it exists to override. */
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

    log_write("plan: tour rebuilt from (%d,%d): %d of %d stops reachable\n",
              pu, pv, m, n);
    for (int i = 0; i < m && i < 6; i++)
        log_write("plan:   [%d] (%d,%d) contents=%u%s cost=%d\n", i,
                  g_tour[i] / WS_GRID_PITCH, g_tour[i] % WS_GRID_PITCH,
                  o->grid[g_tour[i]].contents,
                  rank[order[i]] == 2 ? " SPECIAL" : rank[order[i]] ? " GUARDED" : "",
                  g_cost[i == 0 ? 0 : order[i-1] + 1][order[i] + 1]);
    for (int i = 0; i < n; i++)
        if (o->grid[g_stop[i]].contents != CONTENTS_CRYSTAL)
            log_write("plan:   special contents=%u at (%d,%d) reachable=%s%s\n",
                      o->grid[g_stop[i]].contents,
                      g_stop[i] / WS_GRID_PITCH, g_stop[i] % WS_GRID_PITCH,
                      g_cost[0][i + 1] == UNREACHED ? "NO" : "yes",
                      rank[i] == 2 ? " SPECIAL" : rank[i] ? " GUARDED" : "");
}

/* ── the step ───────────────────────────────────────────────────────────── */

/* Walk the BFS parent chain back from `goal` to the cell adjacent to the
 * start, which is the one cell we can actually step to this frame. */
static bool first_hop(int start, int goal, int *nu, int *nv)
{
    if (goal < 0 || goal == start || g_dist[goal] == UNREACHED) return false;
    int cur = goal;
    while (g_prev[cur] != start) {
        if (g_prev[cur] < 0) return false;
        cur = g_prev[cur];
    }
    *nu = cur / WS_GRID_PITCH;
    *nv = cur % WS_GRID_PITCH;
    return true;
}

static bool step_towards(const Observation *o, int pu, int pv, int goal,
                         int *nu, int *nv)
{
    int start = IDX(pu, pv);
    bfs(o, pu, pv, true);                          /* prefer a safe route */
    if (first_hop(start, goal, nu, nv)) return true;
    bfs(o, pu, pv, false);                         /* ...but move regardless */
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

    /* Collecting: keep off the exit so the level is not ended early. */
    g_avoid_cell = (o->exit_cell[0] < o->cols && o->exit_cell[1] < o->rows)
                 ? IDX(o->exit_cell[0], o->exit_cell[1]) : -1;

    /* Stick with the current target while it is still worth having. */
    if (g_target >= 0 && ws_is_pickup(o->grid[g_target].contents) &&
        g_target != IDX(pu, pv)) {
        if (step_towards(o, pu, pv, g_target, nu, nv)) {
            g_target_fail = 0;
            return true;
        }
        /* Blocked.  If the only thing in the way is a foe, this is a guarded
         * pickup rather than an unreachable one — some are deliberately placed
         * behind a foe — so hold the target and wait for it to move instead of
         * giving up and wandering off to something else. */
        bfs(o, pu, pv, false, true);
        if (g_dist[g_target] != UNREACHED) return false;

        if (++g_target_fail < TARGET_PATIENCE) return false;   /* wait it out */
    }
    g_target = -1;
    g_target_fail = 0;

    int sig = pickup_signature(o);
    if (sig != g_tour_sig) {                       /* something was collected */
        g_tour_sig = sig;
        build_tour(o, pu, pv);
    }

    /* Skip stops already taken (the tour is rebuilt on collection, so this is
     * only about the head of the list) and try each in order — a stop that has
     * become unreachable must not stall the whole tour. */
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

    /* Nothing on the tour is reachable.  Force a rebuild next frame.
     *
     * The tour is normally rebuilt only when a pickup is taken, and that is a
     * trap on levels with falling tiles: those tiles go void for a moment and
     * come back (UpdateBreakableTile respawns them unless the tile's param
     * byte is nonzero), so a tour built during the gap sees a severed map,
     * comes out short or empty, and is then never rebuilt because no pickup
     * was collected.  The policy stalls for good on a level that is still
     * perfectly winnable — which is exactly what Forest\DestrStart did at
     * 16/30, and why "the corridors are permanently severed" was the wrong
     * diagnosis. */
    g_tour_sig = -1;
    return false;
}
