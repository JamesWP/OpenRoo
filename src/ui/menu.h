#pragma once
#include <windows.h>

/* The menu reader and driver, for the autoplay policy.  The menu is a tree of
 * byte node ids, readable from the Game, so the policy navigates it as a
 * person does: see where the cursor is, work out a route, press keys.
 * Descending into some nodes is an action (6 quits, 0x29 continues after a
 * completed level, 200 + k loads save slot k), so "navigate to node N" is the
 * only primitive needed. */

/* The live menu state, read from the Game. */
struct MenuState {
    bool  valid;
    BYTE  node;      // current node id
    BYTE  cursor;    // highlighted child
    BYTE  count;     // children of the current node
    BYTE  depth;     // node-stack depth
    BYTE  last_key;  // debounce: key already handled
    DWORD lock;      // non-zero: input ignored (the 200 ms lockout)
    BYTE  children[256];
};

bool menu_read(MenuState *m);

/* Asks the driver to navigate to goal.  Idempotent: call it every frame while
 * the goal stands.  MENU_NO_GOAL stands down. */
#define MENU_NO_GOAL 0xFFFF
void menu_request(unsigned goal);

/* Presses one key once, independent of any goal: for prompts that are not menu
 * nodes, such as the level intro.  Does nothing while another press is in
 * flight. */
void menu_pulse(int vkey);

/* The current goal, or MENU_NO_GOAL. */
unsigned menu_goal(void);

/* Once per frame, before the game reads any keys. */
void menu_tick(void);

/* The key-poll override: true, with the answer in out, when the driver is
 * pressing this key this frame; otherwise the keyboard answers. */
bool menu_async_override(int vkey, SHORT *out);

/* Nodes the keypress handler acts on. */
#define MENU_NODE_NEW_GAME   0x01
#define MENU_NODE_QUIT       0x06
#define MENU_NODE_CONTINUE   0x29  // after a completed level
#define MENU_NODE_COMPLETED  0x28  // the level-completed screen
#define MENU_NODE_LOAD_SLOT  200   // plus the slot index

/* The game state (Game::state()):
 *   0 -> 4   a level loaded (intro flythrough and "press Enter" screen)
 *   4 -> 1   the level starts running
 *   1 -> 2   game over;  3 level completed;  7 quitting
 * So 4 means loaded, not playing. */
#define GAME_ST_MENU      0
#define GAME_ST_PLAYING   1
#define GAME_ST_GAMEOVER  2
#define GAME_ST_COMPLETED 3
#define GAME_ST_LOADED    4
#define GAME_ST_QUITTING  7
