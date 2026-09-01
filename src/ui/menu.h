#pragma once
#include <windows.h>

/* Menu reader and driver — AI_PLAN.md Stage 5.
 *
 * The menu is a tree of byte node ids, and every part of it is readable out of
 * the Game object, so a policy can navigate it the same way a person does:
 * look at where the cursor is, work out a route, and press keys.
 *
 * Node ids double as *actions*: descending into node 6 quits, node 0x29
 * continues after a completed level, and nodes 200+k load save slot k.  So
 * "navigate to node N" is the only primitive needed.
 */

/* Live menu state, read from the Game object. */
struct MenuState {
    bool  valid;
    BYTE  node;        /* current node id            */
    BYTE  cursor;      /* highlighted child index    */
    BYTE  count;       /* children of the current node */
    BYTE  depth;       /* node-stack depth           */
    BYTE  last_key;    /* debounce: key already handled */
    DWORD lock;        /* nonzero = input ignored (200 ms lockout) */
    BYTE  children[256];
};

bool menu_read(MenuState *m);

/* Ask the driver to navigate to `node`.  Idempotent — call it every frame
 * while the goal stands.  Pass MENU_NO_GOAL to stand down. */
#define MENU_NO_GOAL 0xFFFF
void menu_request(unsigned goal);

/* Pulse a single key once, independently of any goal.  Used for prompts that
 * are not menu nodes at all — the level intro and its "press enter" screen.
 * A no-op while another pulse or a goal-driven press is in flight. */
void menu_pulse(int vkey);

/* Current goal, or MENU_NO_GOAL. */
unsigned menu_goal(void);

/* Called once per frame, before the game reads any keys. */
void menu_tick(void);

/* GetAsyncKeyState interception.  Returns true and fills `out` when the driver
 * is synthesising this key this frame; otherwise the real keyboard answers. */
bool menu_async_override(int vkey, SHORT *out);

/* Well-known nodes, all confirmed from Game::HandleKeypress's switch. */
#define MENU_NODE_NEW_GAME   0x01
#define MENU_NODE_QUIT       0x06
#define MENU_NODE_CONTINUE   0x29   /* after a completed level */
#define MENU_NODE_COMPLETED  0x28   /* the level-completed screen itself */
#define MENU_NODE_LOAD_SLOT  200    /* + slot index */

/* Game+0x2ab58c.  The writers give 0/2/3/4/7; the 4 -> 1 transition is not
 * written by any of them and was found by logging every change at runtime:
 *
 *   0 -> 4   save slot loaded (script flythrough + "press enter" screen)
 *   4 -> 1   the level actually starts running
 *   1 -> 2   game over        3 = level completed        7 = quitting
 *
 * 4 is therefore "loaded", NOT "playing" — an early gate that assumed
 * otherwise silently never fired. */
#define OFF_GAME_STATE   0x2ab58c
#define GAME_ST_MENU      0
#define GAME_ST_PLAYING   1
#define GAME_ST_GAMEOVER  2
#define GAME_ST_COMPLETED 3
#define GAME_ST_LOADED    4
#define GAME_ST_QUITTING  7
