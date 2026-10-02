/* Keys are pressed by action name, not scan code: the policy looks up the
 * action (John_Move_Forward, John_Turn_Left, ...) in the control's table for
 * the current mode and presses whatever keys the player bound to it.
 *
 * DETERMINISM: the policy reads only the Observation and its own state: no
 * rand(), no wall clock.  A policy run is as reproducible as a replay. */

#include "policy.h"
#include "progctrl.h"
#include "worldstate.h"
#include "menu.h"
#include "plan.h"
#include "gamestate.h"
#include "logger.h"
#include "game.h"
#include "player.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* The action names the gameplay mode registers (inputsetup.cpp). */
#define ACT_FORWARD  "John_Move_Forward"
#define ACT_TURN_L   "John_Turn_Left"
#define ACT_TURN_R   "John_Turn_Right"

/* Unused tuning constants. */
#define HEADING_LAG   8

#define ALIGN_TOL     0.45f

#define REACH_TILES   0.75f
#define MIN_MOTION    0.02f

#define STILL_EPS     0.004f
#define STUCK_FRAMES  10
#define TURN_FRAMES   6
#define WALK_FRAMES   12

static int  g_mode = -1;  // -1 unknown, 0 off, 1 nearest-crystal, 2 probe
static char g_name[32];

/* KAROO_POLICY_AFTER=<frame>: the frame at which the policy takes over,
 * default 0.  Lets a recording replay the menu prefix and hand over mid-level.
 */
static DWORD g_after;
static bool  g_after_read;

/* KAROO_POLICY_TRACE=1 logs one line per frame of the controller's decision.
 */
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
                g_logger.write("policy: unknown KAROO_POLICY=%s — disabled "
                          "(known: nearest-crystal, probe)\n", buf);
            strncpy(g_name, buf, sizeof(g_name) - 1);
        }
        g_logger.write("policy: %s\n", g_mode ? g_name : "disabled");
    }
    return g_mode > 0;
}

static int  g_slot = -2;  // -2 unread, -1 none
static bool g_slot_done;
static bool g_played;  // has a level run this session?

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

void policy_menu_tick(void)
{
    if (!policy_active()) return;
    const BYTE *g = (const BYTE *)Game::instance();
    if (!g) return;
    BYTE screen = ((const Game *)g)->state();

    // Log every screen transition: the screen value is easy to misread, and a
    // gate built on the wrong one fails silently.
    static int last_screen = -1;
    if ((int)screen != last_screen) {
        g_logger.write("policy: bGame_state %d -> %u (mode=%u death=%u)\n",
                  last_screen, (unsigned)screen, (unsigned)gamestate_mode(),
                  (unsigned)((const Game *)g)->player()->moveState());
        last_screen = screen;
        // A new level means a new route.
        if (screen == GAME_ST_MENU || screen == GAME_ST_LOADED) plan_reset();
    }
    if (screen == GAME_ST_PLAYING) g_played = true;

    if (menu_goal() != MENU_NO_GOAL) return;  // a menu route is already running

    // A player who has died waits for Enter to respawn, unless the game is
    // over.
    if (((const Game *)g)->player()->moveState() != 0 && screen != GAME_ST_GAMEOVER && screen != GAME_ST_MENU) {
        static bool said;
        if (!said) { said = true; g_logger.write("policy: death — pressing enter to respawn\n"); }
        menu_pulse(0x0d);
        return;
    }

    // A loaded level first plays its intro flythrough and a "press Enter"
    // screen, both advanced with Enter.  The screen already says playing
    // throughout; the dispatch mode stays 0 until play begins.
    if (screen == GAME_ST_LOADED && gamestate_mode() == 0) {
        static bool said;
        if (!said) { said = true; g_logger.write("policy: level intro — pressing enter to start\n"); }
        menu_pulse(0x0d);
        return;
    }

    // The game-over and level-completed screens wait for Enter, then drop to
    // the main menu.
    if (screen == GAME_ST_GAMEOVER || screen == GAME_ST_COMPLETED) {
        static int said;
        if (said != screen) { said = screen;
            g_logger.write("policy: %s screen — pressing enter to clear\n",
                      screen == GAME_ST_GAMEOVER ? "game over" : "level completed"); }
        menu_pulse(0x0d);
        return;
    }

    if (screen != GAME_ST_MENU) return;

    if (!g_slot_done && menu_slot() >= 0) {
        g_slot_done = true;  // once: get into the level
        g_logger.write("policy: loading save slot %d via the menu (node %d)\n",
                  menu_slot(), MENU_NODE_LOAD_SLOT + menu_slot());
        menu_request(MENU_NODE_LOAD_SLOT + menu_slot());
        return;
    }

    // Back at the main menu after playing: the run is over.  Quit through the
    // menu, or reload the slot under KAROO_POLICY_LOOP=1.
    if (g_slot_done && g_played) {
        static bool said;
        if (loop_runs()) {
            g_played = false;
            g_logger.write("policy: run over — reloading slot %d (KAROO_POLICY_LOOP)\n", menu_slot());
            menu_request(MENU_NODE_LOAD_SLOT + menu_slot());
        } else {
            if (!said) { said = true; g_logger.write("policy: run over — quitting via the menu\n"); }
            menu_request(MENU_NODE_QUIT);
        }
    }
}

bool policy_in_control(DWORD frame)
{
    return policy_active() && frame >= policy_after();
}

static void press(ProgableControl *s, unsigned short mode, const char *action, BYTE *keys)
{
    if (mode >= 5) return;
    for (ActionEntry *e = s->actionTable(mode).first(); e; e = e->chain) {
        if (strcmp(e->name, action) != 0) continue;
        // The dispatch stops at the first held key bound to an action, so
        // pressing all of them is the same as pressing the one it would look
        // at.
        for (KeyBind *kb = e->kbd; kb; kb = kb->next)
            if (kb->scancode >= 0 && kb->scancode < 256)
                keys[kb->scancode] = 0x80;
        return;
    }
    // Warn once per action, not once per frame.
    static const char *warned[8];
    static unsigned    n_warned;
    for (unsigned i = 0; i < n_warned; i++) if (warned[i] == action) return;
    if (n_warned < 8) warned[n_warned++] = action;
    g_logger.write("policy: action \"%s\" not bound in mode %u — cannot press it\n",
              action, (unsigned)mode);
}

/* Logs, once per mode, every registered action and its bound keys. */
static void dump_actions(ProgableControl *s, unsigned short mode)
{
    static bool done[5];
    if (mode >= 5 || done[mode]) return;
    done[mode] = true;
    for (ActionEntry *e = s->actionTable(mode).first(); e; e = e->chain) {
        char keys[128]; keys[0] = 0;
        for (KeyBind *kb = e->kbd; kb; kb = kb->next) {
            char one[16];
            wsprintfA(one, "%s0x%02X", keys[0] ? "," : "", kb->scancode);
            if (strlen(keys) + strlen(one) + 1 < sizeof(keys)) strcat(keys, one);
        }
        g_logger.write("policy: mode %u action \"%s\" keys=[%s]\n",
                  (unsigned)mode, e->name, keys);
    }
}

bool policy_keys(ProgableControl *s, unsigned short game_state, BYTE *keys)
{
    if (!policy_active() || !s) return false;

    const Observation *o = worldstate_latest();
    if (!o || !o->valid || o->mode == 0) return false;
    if (o->frame < policy_after()) return false;

    dump_actions(s, game_state);
    memset(keys, 0, 256);

    // Keys are held, not pulsed: moving and turning do nothing while a move is
    // in progress, so holding a key steps one tile or one quarter turn at a
    // time.
    int pu = (int)(o->player_grid[0] + 0.5f);
    int pv = (int)(o->player_grid[2] + 0.5f);

    // KAROO_POLICY_TRACE logs a STEP line with the facing and the cell delta
    // each time the player's cell changes; this is how the direction table was
    // measured.
    {
        static int last_u = -999, last_v = -999;
        static BYTE last_face;
        if (last_u != -999 && (pu != last_u || pv != last_v) && policy_trace())
            g_logger.write("policy: STEP face=%u(before %u) delta=(%+d,%+d)\n",
                      o->player_facing, last_face, pu - last_u, pv - last_v);
        if (pu != last_u || pv != last_v) { last_u = pu; last_v = pv; }
        last_face = o->player_facing;
    }

    // KAROO_POLICY=probe calibrates rather than plays: walk until the cell
    // changes, turn right until the facing changes, repeat.  One run visits
    // all four facings.
    if (g_mode == 2) {
        static int  phase;  // 0 walking, 1 turning
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
            g_logger.write("policy: PROBE turn_right %u -> %u\n",
                      mark_face, o->player_facing);
            phase = 0; mark_u = pu; mark_v = pv;
            press(s, game_state, ACT_FORWARD, keys);
            return true;
        }
        press(s, game_state, ACT_TURN_R, keys);
        return true;
    }

    // Collect everything reachable first; head for the exit only when nothing
    // is left and enough crystals are held, which is the game's own condition
    // for completing a level.
    bool seek_exit = false;
    int nu, nv;
    if (!plan_next_step(o, pu, pv, &nu, &nv, false)) {
        if (o->gems_collected >= o->gems_required &&
            plan_next_step(o, pu, pv, &nu, &nv, true))
            seek_exit = true;  // nothing left to collect: leave
        else {
            if (policy_trace())
                g_logger.write("policy: f=%lu cell=(%d,%d) NOTHING REACHABLE (%s)\n",
                          (unsigned long)o->frame, pu, pv,
                          o->gems_collected >= o->gems_required
                              ? "exit" : "pickup");
            return true;
        }
    }

    // Which of the four directions steps from here to the next cell.
    int want = 0;
    for (int d = WS_DIR_MIN; d <= WS_DIR_MAX; d++)
        if (pu + WS_DIR_DU[d] == nu && pv + WS_DIR_DV[d] == nv) { want = d; break; }

    BYTE face = o->player_facing;
    const char *act;
    if (want == 0 || face < WS_DIR_MIN || face > WS_DIR_MAX) {
        // The route only ever returns a neighbouring cell, so this should not
        // happen; walking forward keeps things moving.
        act = ACT_FORWARD;
    } else if (face == want) {
        act = ACT_FORWARD;
    } else if (o->player_moving != 0) {
        // Mid-move: press nothing.  A turn pressed during a move is buffered
        // and fires when the move completes, so holding the key would
        // overshoot by a quarter turn.  One turn per completed move is what a
        // person does.
        act = NULL;
    } else {
        // Turn the short way: right is +1 mod 4, left -1.  A half turn goes
        // right and re-decides next frame.
        int cw = (want - (int)face + 4) % 4;
        act = (cw == 3) ? ACT_TURN_L : ACT_TURN_R;
    }
    if (act) press(s, game_state, act, keys);

    if (policy_trace())
        g_logger.write("policy: f=%lu cell=(%d,%d) face=%u want=%d step=(%d,%d) "
                  "moving=%u %s -> %s\n",
                  (unsigned long)o->frame, pu, pv, o->player_facing, want,
                  nu, nv, o->player_moving, seek_exit ? "exit" : "gem",
                  act ? act : "(wait)");
    return true;
}
