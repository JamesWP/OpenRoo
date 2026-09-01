/* Autoplay policy — AI_PLAN.md Stage 4.
 *
 * ── How it presses keys ─────────────────────────────────────────────────
 *
 * Not by hardcoding scancodes.  `ProgableControl::action_tables[mode]` is a
 * list of named actions, each carrying the KeyBind chain the *user's* config
 * bound to it, so the policy looks up "John_Move_Forward" and presses whatever key
 * that action is actually bound to.  A player with remapped controls, or a
 * different Karoo.cfg, still gets a working policy — and, more to the point,
 * the policy cannot silently stop working because a default changed.
 *
 * The names below were read from the live table, not guessed from strings in
 * the binary.  That distinction cost a run: the binary does contain
 * "walk_forward", "turn_left" and friends, but the gameplay table registers
 * `John_Move_Forward`, `John_Turn_Left`, `John_Turn_Right`, `John_Move_Back`,
 * `John_Release_Bomb` and `John_Harakiri`, so a policy built on the string
 * dump pressed nothing at all.  dump_actions() below logs the real table once
 * per mode, which is how these were obtained and how a future change to them
 * will be noticed.
 *
 * ── How it knows which way it is facing ─────────────────────────────────
 *
 * It does not read a facing field, because no such field has been located and
 * guessing one would be exactly the "correlation is not confirmation" mistake
 * CLAUDE.md warns about.  Instead the heading is *measured*: the policy
 * remembers where the player was HEADING_LAG frames ago and treats the
 * difference as the direction it is travelling in.  Walking forward for a few
 * frames therefore produces a heading estimate with no reverse-engineering at
 * all, and the estimate self-corrects every time the player moves.
 *
 * The cost is that the estimate is stale while turning on the spot, which is
 * why the controller alternates: turn until roughly aligned, walk to refresh
 * the estimate, repeat.  It is a deliberately dumb controller — the point of
 * Stage 4 is to prove that perception, decision and injection line up, not to
 * play well.
 *
 * ── Determinism ─────────────────────────────────────────────────────────
 *
 * Reads only the Observation and its own state; no rand(), no wall clock, no
 * uninitialised memory.  A policy run is therefore as reproducible as a replay
 * is, which is what lets the run be recorded to a .rec and replayed back as a
 * normal test.
 */
#include "policy.h"
#include "progctrl.h"
#include "worldstate.h"
#include "log.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* Frames of position history used to estimate the heading.  Long enough that
 * a single frame's sub-tile motion is not swamped by float noise, short enough
 * that the estimate is not badly stale after a turn. */
#define ACT_FORWARD  "John_Move_Forward"
#define ACT_TURN_L   "John_Turn_Left"
#define ACT_TURN_R   "John_Turn_Right"

#define HEADING_LAG   8

/* Radians.  Wider than it looks: the controller walks whenever it is roughly
 * pointed at the target and lets the next measurement correct the error. */
#define ALIGN_TOL     0.45f

/* Treat the target as reached inside this many tiles, and ignore heading
 * estimates built from less motion than this (the player was standing still). */
#define REACH_TILES   0.75f
#define MIN_MOTION    0.02f

/* Below this much movement in a frame the player counts as standing still. */
#define STILL_EPS     0.004f
/* Frames of no motion before the controller assumes it is facing a wall. */
#define STUCK_FRAMES  10
/* Frames a turn is committed for once started. */
#define TURN_FRAMES   6
/* Frames of walking forced after every turn, so the next turn decision is
 * taken from a heading measured *after* the turn.  Must exceed HEADING_LAG. */
#define WALK_FRAMES   12

static int  g_mode = -1;          /* -1 unknown, 0 off, 1 nearest-crystal */
static char g_name[32];

/* Which way "John_Turn_Left" actually turns depends on the handedness of (U, V)
 * pair, and that is not known from the decompile — the axes are only known to
 * be consistent, not oriented.  So the sign is a setting rather than a guess:
 * KAROO_POLICY_FLIP=1 inverts it.  The default below is the one that was
 * measured to work; see the commit message.  If a level has the policy circling
 * a crystal instead of walking to it, this is the first thing to flip. */
static int g_flip = -1;

/* Frame at which the policy takes over, default 0 (immediately).
 *
 * This exists because a policy run has to start somewhere, and "somewhere" is
 * a loaded level — but reaching one means navigating the launcher, the menu
 * and a save slot, which is menu-driving work the policy has no business
 * doing.  KAROO_POLICY_AFTER=<frame> lets an existing recording replay the
 * menu prefix and then hand control over mid-level: the recording gets the
 * player into the level, the policy plays it.
 *
 * It is also the only way to exercise the policy without a human at the
 * keyboard, which is what makes Stage 4 checkable in the replay harness at
 * all. */
static DWORD g_after;
static bool  g_after_read;

/* Per-frame decision trace.  A frozen policy is a symptom you cannot debug by
 * watching the screen — this turns it into numbers, the same way
 * KAROO_SIM_STATS does for the particle rings. */
static int g_trace = -1;

static bool policy_trace(void)
{
    if (g_trace < 0) {
        char buf[8];
        g_trace = (GetEnvironmentVariableA("KAROO_POLICY_TRACE", buf, sizeof(buf))
                   && buf[0] && buf[0] != '0');
    }
    return g_trace > 0;
}

static DWORD policy_after(void)
{
    if (!g_after_read) {
        char buf[16];
        g_after_read = true;
        if (GetEnvironmentVariableA("KAROO_POLICY_AFTER", buf, sizeof(buf)) && buf[0])
            g_after = (DWORD)strtoul(buf, NULL, 10);
    }
    return g_after;
}

static bool turn_flipped(void)
{
    if (g_flip < 0) {
        char buf[8];
        g_flip = (GetEnvironmentVariableA("KAROO_POLICY_FLIP", buf, sizeof(buf))
                  && buf[0] && buf[0] != '0');
    }
    return g_flip > 0;
}

bool policy_active(void)
{
    if (g_mode < 0) {
        char buf[32];
        g_mode = 0;
        if (GetEnvironmentVariableA("KAROO_POLICY", buf, sizeof(buf)) && buf[0]) {
            if (strcmp(buf, "nearest-crystal") == 0) g_mode = 1;
            else if (strcmp(buf, "none") != 0)
                log_write("policy: unknown KAROO_POLICY=%s — disabled "
                          "(known: nearest-crystal)\n", buf);
            strncpy(g_name, buf, sizeof(g_name) - 1);
        }
        log_write("policy: %s\n", g_mode ? g_name : "disabled");
    }
    return g_mode > 0;
}

bool policy_in_control(DWORD frame)
{
    return policy_active() && frame >= policy_after();
}

/* ── pressing a named action ────────────────────────────────────────────── */

static void press(ProgableControl *s, unsigned short mode, const char *action, BYTE *keys)
{
    if (mode >= 5) return;
    for (ActionEntry *e = s->action_tables[mode].head; e; e = e->chain) {
        if (strcmp(e->name, action) != 0) continue;
        /* Press every key bound to the action.  The dispatcher stops at the
         * first bound key it finds held, so pressing them all is equivalent to
         * pressing whichever one it would have looked at. */
        for (KeyBind *kb = e->kbd; kb; kb = kb->next)
            if (kb->scancode >= 0 && kb->scancode < 256)
                keys[kb->scancode] = 0x80;
        return;
    }
    /* Only worth saying once per action — otherwise it is one line per frame. */
    static const char *warned[8];
    static unsigned    n_warned;
    for (unsigned i = 0; i < n_warned; i++) if (warned[i] == action) return;
    if (n_warned < 8) warned[n_warned++] = action;
    log_write("policy: action \"%s\" not bound in mode %u — cannot press it\n",
              action, (unsigned)mode);
}

/* One-shot dump of what is actually registered for this mode.
 *
 * Guessing action names from strings in the binary is not the same as reading
 * the table the dispatcher walks — the first attempt at this policy pressed
 * nothing at all because it looked for "walk_forward" in a table that does not
 * use that name.  So the names are logged once, and the mapping below is built
 * from that log rather than from the string dump. */
static void dump_actions(ProgableControl *s, unsigned short mode)
{
    static bool done[5];
    if (mode >= 5 || done[mode]) return;
    done[mode] = true;
    for (ActionEntry *e = s->action_tables[mode].head; e; e = e->chain) {
        char keys[128]; keys[0] = 0;
        for (KeyBind *kb = e->kbd; kb; kb = kb->next) {
            char one[16];
            wsprintfA(one, "%s0x%02X", keys[0] ? "," : "", kb->scancode);
            if (strlen(keys) + strlen(one) + 1 < sizeof(keys)) strcat(keys, one);
        }
        log_write("policy: mode %u action \"%s\" keys=[%s]\n",
                  (unsigned)mode, e->name, keys);
    }
}

/* ── the controller ─────────────────────────────────────────────────────── */

static float g_hist_u[HEADING_LAG], g_hist_v[HEADING_LAG];
static unsigned g_hist_n;
static DWORD    g_last_frame;
static float    g_last_u, g_last_v;
static int      g_still;
static int      g_burst;
static int      g_walk;
static bool     g_walked_last;
static bool     g_turn_left;

static void heading_reset(void)
{
    g_hist_n = 0; g_still = 0; g_burst = 0; g_walk = 0; g_walked_last = false;
}

/* Breadth-first search from the player's cell to the nearest reachable
 * crystal, returning the next cell to step to.
 *
 * Terrain awareness is here because the first working version did not have it
 * and the result was unambiguous: from (7,6) on Forest\BombStart the nearest
 * crystal is (1,6), due west, and the policy walked west off the edge at (5,6)
 * and fell — the vertical position dropped from 30 to 4 in freefall and the
 * run ended with death_cause 2.  That is what "goal-seeking with no map" looks
 * like, and it is also how tile kind 0 was confirmed to mean *no floor*: the
 * player fell at exactly the first kind-0 cell on its path.
 *
 * So: 4-neighbour BFS over tiles whose kind is nonzero.  Deliberately minimal —
 * it ignores height differences (so it will happily route up a cliff it cannot
 * climb), foes, bridges, lifts and switches.  Those are AI_PLAN.md § "Not in
 * scope yet" and they need the tile-kind semantics confirmed first.  This is
 * enough to make Stage 4's check observable, and no more.
 */
#define WALKABLE(o, u, v) ((o)->grid[(v) + (u) * WS_GRID_PITCH].kind != 0)

static short g_prev_cell[WS_GRID_PITCH * WS_GRID_PITCH];
static short g_queue[WS_GRID_PITCH * WS_GRID_PITCH];

static bool next_step(const Observation *o, int pu, int pv, int *nu, int *nv)
{
    if (pu < 0 || pv < 0 || pu >= o->cols || pv >= o->rows) return false;

    for (int i = 0; i < WS_GRID_PITCH * WS_GRID_PITCH; i++) g_prev_cell[i] = -1;

    int head = 0, tail = 0;
    int start = pv + pu * WS_GRID_PITCH;
    g_queue[tail++] = (short)start;
    g_prev_cell[start] = (short)start;      /* its own parent: marks visited */

    int goal = -1;
    static const int du[4] = { 1, -1, 0, 0 };
    static const int dv[4] = { 0, 0, 1, -1 };

    while (head < tail && goal < 0) {
        int cur = g_queue[head++];
        int cu = cur / WS_GRID_PITCH, cv = cur % WS_GRID_PITCH;

        /* The start cell holds a crystal only if the player is standing on one,
         * in which case there is nothing to walk towards. */
        if (cur != start && o->grid[cur].contents == 1) { goal = cur; break; }

        for (int d = 0; d < 4; d++) {
            int au = cu + du[d], av = cv + dv[d];
            if (au < 0 || av < 0 || au >= o->cols || av >= o->rows) continue;
            int adj = av + au * WS_GRID_PITCH;
            if (g_prev_cell[adj] >= 0) continue;
            if (!WALKABLE(o, au, av)) continue;
            g_prev_cell[adj] = (short)cur;
            g_queue[tail++] = (short)adj;
        }
    }
    if (goal < 0) return false;                     /* none reachable */

    /* Walk the parent chain back to the cell adjacent to the start. */
    int cur = goal;
    while (g_prev_cell[cur] != start && g_prev_cell[cur] != cur)
        cur = g_prev_cell[cur];
    if (cur == start) return false;

    *nu = cur / WS_GRID_PITCH;
    *nv = cur % WS_GRID_PITCH;
    return true;
}

bool policy_keys(ProgableControl *s, unsigned short game_state, BYTE *keys)
{
    if (!policy_active() || !s) return false;

    const Observation *o = worldstate_latest();
    if (!o || !o->valid || o->mode == 0) { heading_reset(); return false; }
    if (o->frame < policy_after()) { heading_reset(); return false; }

    /* worldstate_tick() runs once per frame from the clock; the dispatcher can
     * be called without an intervening frame boundary.  Only advance the
     * history on a new frame, or the heading estimate collapses. */
    bool new_frame = (o->frame != g_last_frame);
    g_last_frame = o->frame;

    float pu = o->player_grid[0], pv = o->player_grid[2];

    float hu = 0, hv = 0;
    bool have_heading = false;
    if (g_hist_n >= HEADING_LAG) {
        unsigned oldest = g_hist_n % HEADING_LAG;   /* slot about to be reused */
        hu = pu - g_hist_u[oldest];
        hv = pv - g_hist_v[oldest];
        have_heading = (hu * hu + hv * hv) >= (MIN_MOTION * MIN_MOTION);
    }
    if (new_frame) {
        g_hist_u[g_hist_n % HEADING_LAG] = pu;
        g_hist_v[g_hist_n % HEADING_LAG] = pv;
        g_hist_n++;
    }

    dump_actions(s, game_state);
    memset(keys, 0, 256);

    int nu, nv;
    if (!next_step(o, (int)(pu + 0.5f), (int)(pv + 0.5f), &nu, &nv)) {
        if (policy_trace())
            log_write("policy: f=%lu pos=(%.3f,%.3f) NO REACHABLE CRYSTAL\n",
                      (unsigned long)o->frame, pu, pv);
        return true;
    }

    float du = (float)nu - pu, dv = (float)nv - pv;
    if (du * du + dv * dv <= REACH_TILES * REACH_TILES) {
        /* Already on the next waypoint — keep walking so the heading estimate
         * stays fresh rather than stalling on the tile boundary. */
        press(s, game_state, ACT_FORWARD, keys);
        return true;
    }

    /* ── the controller ──────────────────────────────────────────────────
     *
     * A state machine, not a per-frame reflex, because the per-frame version
     * deadlocked and the trace showed exactly how: turning on the spot
     * produces no motion, so the measured heading goes stale, so the
     * controller falls back to "walk forward", so it walks into whatever it
     * happens to be facing — for 170 straight frames at (9,9) on
     * Forest\Start, without moving.
     *
     * Two rules fix it, and both are about committing:
     *   - a turn runs for a fixed burst before the heading is consulted again,
     *     so a turn actually completes;
     *   - if walking has produced no motion for STUCK_FRAMES, turn regardless
     *     of what the heading says, because "facing a wall" and "no heading"
     *     look identical from here.
     *
     * The result cannot deadlock: every state has a timeout into the other.
     */
    /* Motion bookkeeping.  g_still counts only frames the policy spent trying
     * to WALK.  Counting turn frames too was the second deadlock: turning
     * never moves the player, so "still" kept climbing during a turn, so the
     * stuck rule fired, so it turned again — the trace showed it spinning on
     * the spot from f=474 onwards, alternating turn bursts and never once
     * walking. */
    if (new_frame) {
        float mu = pu - g_last_u, mv = pv - g_last_v;
        bool moved = (mu * mu + mv * mv) >= (STILL_EPS * STILL_EPS);
        g_last_u = pu; g_last_v = pv;
        if (moved)              g_still = 0;
        else if (g_walked_last) g_still++;
    }

    /* One-tile lookahead.
     *
     * The BFS never routes through a kind-0 cell, but the controller only
     * steers *roughly* toward the next waypoint and can drift diagonally past
     * it.  That is how the first long run ended: two crystals collected, then
     * a walk into (9,13) — a kind-0 cell the path never contained — and a fall
     * to -100.  So refuse to press forward when the cell one tile ahead along
     * the current heading is not walkable, and turn instead. */
    bool ahead_ok = true;
    if (have_heading) {
        float len = sqrtf(hu * hu + hv * hv);
        if (len > 0.0f) {
            int au = (int)(pu + hu / len + 0.5f);
            int av = (int)(pv + hv / len + 0.5f);
            ahead_ok = (au >= 0 && av >= 0 && au < o->cols && av < o->rows &&
                        WALKABLE(o, au, av));
        }
    }

    const char *act;
    if (g_burst > 0) {                        /* committed to a turn */
        if (new_frame) g_burst--;
        act = g_turn_left ? ACT_TURN_L : ACT_TURN_R;
    } else if (!ahead_ok) {
        /* About to walk off the map — turn, whatever the plan said. */
        g_turn_left = true;
        g_burst     = TURN_FRAMES;
        g_walk      = WALK_FRAMES;
        g_still     = 0;
        act = ACT_TURN_L;
    } else if (g_walk > 0) {
        /* Always walk for a stretch after a turn, so there is fresh motion to
         * measure a heading from before the next turn decision is taken.
         * Without this the controller decides on a heading it measured before
         * the turn, which is exactly the one the turn was meant to change. */
        if (new_frame) g_walk--;
        act = ACT_FORWARD;
    } else if (g_still >= STUCK_FRAMES) {     /* walking is getting nowhere */
        g_turn_left = true;
        g_burst     = TURN_FRAMES;
        g_walk      = WALK_FRAMES;
        g_still     = 0;
        act = ACT_TURN_L;
    } else if (have_heading) {
        float cross = hu * dv - hv * du;
        float dot   = hu * du + hv * dv;
        float err   = atan2f(cross, dot);
        if (turn_flipped()) err = -err;
        if (err > ALIGN_TOL || err < -ALIGN_TOL) {
            g_turn_left = (err > 0);
            g_burst     = TURN_FRAMES;
            g_walk      = WALK_FRAMES;
            act = g_turn_left ? ACT_TURN_L : ACT_TURN_R;
        } else {
            act = ACT_FORWARD;
        }
    } else {
        act = ACT_FORWARD;                    /* walking is what builds a heading */
    }
    press(s, game_state, act, keys);
    if (new_frame) g_walked_last = (act == ACT_FORWARD);

    if (policy_trace())
        log_write("policy: f=%lu pos=(%.3f,%.3f) way=(%d,%d) head=(%.3f,%.3f)%s "
                  "still=%d burst=%d walk=%d%s -> %s\n",
                  (unsigned long)o->frame, pu, pv, nu, nv, hu, hv,
                  have_heading ? "" : "[stale]", g_still, g_burst, g_walk,
                  ahead_ok ? "" : " EDGE", act);
    return true;
}
