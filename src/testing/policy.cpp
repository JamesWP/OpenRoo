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
#include "menu.h"
#include "plan.h"
#include "gamestate.h"
#include "log.h"
#include "game.h"
#include "player.h"
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

bool policy_active(void)
{
    if (g_mode < 0) {
        char buf[32];
        g_mode = 0;
        if (GetEnvironmentVariableA("KAROO_POLICY", buf, sizeof(buf)) && buf[0]) {
            if (strcmp(buf, "nearest-crystal") == 0) g_mode = 1;
            else if (strcmp(buf, "probe") == 0) g_mode = 2;
            else if (strcmp(buf, "none") != 0)
                log_write("policy: unknown KAROO_POLICY=%s — disabled "
                          "(known: nearest-crystal, probe)\n", buf);
            strncpy(g_name, buf, sizeof(g_name) - 1);
        }
        log_write("policy: %s\n", g_mode ? g_name : "disabled");
    }
    return g_mode > 0;
}

/* ── menu goals: getting into a level, and out of one ────────────────────
 *
 * This replaces the recording prefix.  A policy run used to borrow
 * bombstart-crash's menu keystrokes to reach a level, which meant the run only
 * started if SavedGames happened to hold what that recording expected — the
 * cause of every "wedged at the main menu" failure.  Now the policy navigates
 * the menu itself: KAROO_MENU_SLOT=<k> asks for node 200+k, which is the
 * "load save slot k" leaf.
 *
 * Game+0x2ab58c is the screen enum: 2 = game over, 3 = level completed,
 * 4 = playing.  It is set alongside the completed/gameover handling in
 * FUN_0041aca0, which also sets the menu node to 0x28 (the level-completed
 * screen) — so the values are read off the same code that drives the menu,
 * not guessed.
 */
#define GAME_PTR   ((void **)0x0046c498)

static int  g_slot = -2;      /* -2 unread, -1 none */
static bool g_slot_done;
static bool g_played;         /* have we been in a running level this run? */

static bool loop_runs(void)
{
    static int v = -1;
    if (v < 0) {
        char buf[8];
        v = (GetEnvironmentVariableA("KAROO_POLICY_LOOP", buf, sizeof(buf))
             && buf[0] && buf[0] != '0');
    }
    return v > 0;
}

static int menu_slot(void)
{
    if (g_slot == -2) {
        char buf[16];
        g_slot = -1;
        if (GetEnvironmentVariableA("KAROO_MENU_SLOT", buf, sizeof(buf)) && buf[0])
            g_slot = (int)strtol(buf, NULL, 10);
    }
    return g_slot;
}

/* Called every frame from policy_keys, and also while no level is loaded. */
void policy_menu_tick(void)
{
    if (!policy_active()) return;
    const BYTE *g = (const BYTE *)*GAME_PTR;
    if (!g) return;
    BYTE screen = g[OFF_GAME_STATE];

    /* bGame_state turned out not to hold the value the level-load path writes
     * for the whole time a level is up, so log every transition rather than
     * reasoning about it: a gate built on the wrong value fails silently. */
    static int last_screen = -1;
    if ((int)screen != last_screen) {
        log_write("policy: bGame_state %d -> %u (mode=%u death=%u)\n",
                  last_screen, (unsigned)screen, (unsigned)gamestate_mode(),
                  (unsigned)((const Game *)g)->player()->moveState());
        last_screen = screen;
        /* A new level means a new tour; a stale one would send the policy to
         * cells that no longer hold anything. */
        if (screen == GAME_ST_MENU || screen == GAME_ST_LOADED) plan_reset();
    }
    if (screen == GAME_ST_PLAYING) g_played = true;

    if (menu_goal() != MENU_NO_GOAL) return;   /* a route is already running */

    /* Loading a level does not start it.  The script system flies the camera
     * through the level with the intro audio, and then a "press enter" screen
     * waits before the timer and the simulation actually run — both advanced
     * with ENTER.  Throughout, bGame_state is already 4, so "a level is
     * loaded" is not the same as "a level is running"; the discriminator is
     * the dispatcher's game_state, which stays 0 until play begins.
     *
     * Without this the policy simply never started: a run sat on the intro for
     * 4400 frames with the level loaded (10 gems, 140 s limit) and mode 0. */
    /* A death also waits for ENTER, and this is why the very first policy runs
     * looked like they "lost control" after a few hundred frames: they had
     * died, and nothing was acknowledging it.  The borrowed recording used to
     * hide this — its leftover ENTER presses advanced the prompt, which is why
     * the recording-driven runs appeared to respawn on their own and the
     * menu-driven one froze at the same point.
     *
     * Game+0x1752e8 is the death cause byte gamestate.cpp already logs; it is
     * nonzero for the whole death/respawn window and clears on respawn. */
    if (((const Game *)g)->player()->moveState() != 0 && screen != GAME_ST_GAMEOVER && screen != GAME_ST_MENU) {
        static bool said;
        if (!said) { said = true; log_write("policy: death — pressing enter to respawn\n"); }
        menu_pulse(0x0d);
        return;
    }

    if (screen == GAME_ST_LOADED && gamestate_mode() == 0) {
        static bool said;
        if (!said) { said = true; log_write("policy: level intro — pressing enter to start\n"); }
        menu_pulse(0x0d);
        return;
    }

    /* The game-over and level-completed score screens are not menu nodes: they
     * wait for ENTER and then drop back to the main menu.  Only once we are at
     * the main menu (state 0) is there a node tree to navigate. */
    if (screen == GAME_ST_GAMEOVER || screen == GAME_ST_COMPLETED) {
        static int said;
        if (said != screen) { said = screen;
            log_write("policy: %s screen — pressing enter to clear\n",
                      screen == GAME_ST_GAMEOVER ? "game over" : "level completed"); }
        menu_pulse(0x0d);
        return;
    }


    if (screen != GAME_ST_MENU) return;

    if (!g_slot_done && menu_slot() >= 0) {
        g_slot_done = true;                     /* one shot: get into the level */
        log_write("policy: loading save slot %d via the menu (node %d)\n",
                  menu_slot(), MENU_NODE_LOAD_SLOT + menu_slot());
        menu_request(MENU_NODE_LOAD_SLOT + menu_slot());
        return;
    }

    /* Back at the main menu having already played: the run is over.  Quit
     * through the menu rather than reloading — reloading here is what produced
     * the endless load/die/load loop the first time round.  KAROO_POLICY_LOOP=1
     * asks for that loop deliberately, for unattended repeated runs. */
    if (g_slot_done && g_played) {
        static bool said;
        if (loop_runs()) {
            g_played = false;
            log_write("policy: run over — reloading slot %d (KAROO_POLICY_LOOP)\n", menu_slot());
            menu_request(MENU_NODE_LOAD_SLOT + menu_slot());
        } else {
            if (!said) { said = true; log_write("policy: run over — quitting via the menu\n"); }
            menu_request(MENU_NODE_QUIT);
        }
    }
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

bool policy_keys(ProgableControl *s, unsigned short game_state, BYTE *keys)
{
    if (!policy_active() || !s) return false;

    const Observation *o = worldstate_latest();
    if (!o || !o->valid || o->mode == 0) return false;
    if (o->frame < policy_after()) return false;

    dump_actions(s, game_state);
    memset(keys, 0, 256);

    /* ── the controller ──────────────────────────────────────────────────
     *
     * Facing is a real field, so this is now arithmetic rather than
     * guesswork.  The previous version estimated a heading from how the
     * player had moved over the last few frames, which is what made it
     * deadlock twice: turning produces no motion, so the estimate went stale
     * exactly when it was needed, and the fallback walked into walls.  All of
     * that machinery — the history ring, the stuck detector, the turn bursts,
     * the KAROO_POLICY_FLIP guess about handedness — is gone.
     *
     * The keys here are level-triggered, not edge-triggered like the menu's:
     * PlayerMoveForward and PlayerTurnRight both no-op while a move is in
     * progress (entity+0x14e != 0), so holding a key steps one tile or one
     * quarter-turn at a time.  Holding is therefore correct and needs no
     * pulsing; the controller simply stops asking once it is aligned.
     */
    int pu = (int)(o->player_grid[0] + 0.5f);
    int pv = (int)(o->player_grid[2] + 0.5f);

    /* Direction calibration.
     *
     * The direction->axis table was first derived from FUN_00438770's movement
     * interpolation and came out wrong in play: with facing 2 the player moved
     * along +V, not +U.  Two encodings are mixed up in that function —
     * entity+0x14e holds a plain 1..4 while walking but direction+10 while
     * turning (PlayerTurnRight writes +0x145 = dir + 10) — so the switch cases
     * do not map to facing values as directly as they appear to.
     *
     * So measure it instead of arguing with the decompile: log the facing and
     * the cell delta every time the player's cell actually changes, and read
     * the table off a run.  KAROO_POLICY_TRACE shows these as STEP lines.
     */
    {
        static int last_u = -999, last_v = -999;
        static BYTE last_face;
        if (last_u != -999 && (pu != last_u || pv != last_v) && policy_trace())
            log_write("policy: STEP face=%u(before %u) delta=(%+d,%+d)\n",
                      o->player_facing, last_face, pu - last_u, pv - last_v);
        if (pu != last_u || pv != last_v) { last_u = pu; last_v = pv; }
        last_face = o->player_facing;
    }

    /* KAROO_POLICY=probe — calibration, not play.
     *
     * Walk until the cell changes, then turn right until the facing changes,
     * and repeat.  Over one run that visits all four facings and prints a STEP
     * line for each, which is how the direction table below was established
     * after deriving it from the decompile gave the wrong answer.  It also
     * shows the turn *order*, so "right is +1" is measured rather than assumed.
     */
    if (g_mode == 2) {
        static int  phase;          /* 0 = walking, 1 = turning */
        static int  mark_u, mark_v;
        static BYTE mark_face;
        static bool primed;
        if (!primed) { primed = true; mark_u = pu; mark_v = pv; mark_face = o->player_facing; }

        if (phase == 0) {
            if (pu != mark_u || pv != mark_v) {
                phase = 1; mark_face = o->player_facing;
            } else {
                press(s, game_state, ACT_FORWARD, keys);
                return true;
            }
        }
        if (o->player_facing != mark_face) {
            log_write("policy: PROBE turn_right %u -> %u\n",
                      mark_face, o->player_facing);
            phase = 0; mark_u = pu; mark_v = pv;
            press(s, game_state, ACT_FORWARD, keys);
            return true;
        }
        press(s, game_state, ACT_TURN_R, keys);
        return true;
    }

    /* Crystals first, then the exit.
     *
     * With every crystal collected the policy had no target left and simply
     * stood still — the level was won on points and never finished.  GameTick
     * only sets the completion flag when the player's cell matches the exit
     * AND gems_collected >= gems_required, so switching targets in that order
     * matches the game's own condition. */
    /* Collect everything worth having first, then leave.  gems_required is the
     * exit's condition, but other pickups are still worth a detour while the
     * level is open, so only switch to the exit when nothing else is left. */
    bool seek_exit = false;
    int nu, nv;
    if (!plan_next_step(o, pu, pv, &nu, &nv, false)) {
        if (o->gems_collected >= o->gems_required &&
            plan_next_step(o, pu, pv, &nu, &nv, true))
            seek_exit = true;               /* nothing left to collect — leave */
        else {
            if (policy_trace())
                log_write("policy: f=%lu cell=(%d,%d) NOTHING REACHABLE (%s)\n",
                          (unsigned long)o->frame, pu, pv,
                          o->gems_collected >= o->gems_required
                              ? "exit" : "pickup");
            return true;
        }
    }

    /* Which of the four directions steps from here to the next cell. */
    int want = 0;
    for (int d = WS_DIR_MIN; d <= WS_DIR_MAX; d++)
        if (pu + WS_DIR_DU[d] == nu && pv + WS_DIR_DV[d] == nv) { want = d; break; }

    BYTE face = o->player_facing;
    const char *act;
    if (want == 0 || face < WS_DIR_MIN || face > WS_DIR_MAX) {
        /* The BFS only ever returns a 4-neighbour, so this should not happen;
         * walking forward is a harmless default that keeps things moving. */
        act = ACT_FORWARD;
    } else if (face == want) {
        act = ACT_FORWARD;
    } else if (o->player_moving != 0) {
        /* Mid-action: press nothing.
         *
         * Turning is the one place a held key is wrong.  PlayerTurnRight
         * no-ops while a move is in progress *except* that it buffers the
         * press into entity+0x60/+0x61, so holding the key queues a second
         * turn that fires the moment the first completes — the player
         * overshoots by 90 degrees every time.  That is exactly what the probe
         * run showed: turn_right stepped 1 -> 2, but by the time the next walk
         * happened the facing had already run on to 3, so facings 2 and 4
         * never got to move at all and the policy oscillated along one axis.
         *
         * Gating on the busy field means one turn is issued per completed
         * action, which is what a person pressing the key does. */
        act = NULL;
    } else {
        /* Turn the short way: right is +1 mod 4 (measured: 1->2, 3->4), left
         * is -1.  A half-turn goes right arbitrarily and re-decides next
         * frame. */
        int cw = (want - (int)face + 4) % 4;
        act = (cw == 3) ? ACT_TURN_L : ACT_TURN_R;
    }
    if (act) press(s, game_state, act, keys);

    if (policy_trace())
        log_write("policy: f=%lu cell=(%d,%d) face=%u want=%d step=(%d,%d) "
                  "moving=%u %s -> %s\n",
                  (unsigned long)o->frame, pu, pv, o->player_facing, want,
                  nu, nv, o->player_moving, seek_exit ? "exit" : "gem",
                  act ? act : "(wait)");
    return true;
}
