/* The menu navigator (menutree.cpp) acts on key edges, not levels: it ignores
 * a key equal to the last one it handled, and clears that only when the key
 * reads up.  So a held key acts once, and the driver presses in pulses:
 * HOLD_FRAMES down, then RELEASE_FRAMES up.  It also waits out the 200 ms
 * lockout, during which every key is discarded.
 *
 * It presses keys rather than setting the node, because node ids are also
 * actions: the keypress handler loads a slot, quits or advances a level when a
 * node is entered.  The navigator's rules:
 *   Enter:  child = children[node][cursor]; saved[node] = cursor;
 *           cursor = 0; push(node); node = child
 *   Escape: depth < 2 ? leave the menu : pop (cursor = saved[node])
 *   Up, Down: move the cursor, wrapping at both ends */

#include "inputdev.h"
#include <stdint.h>
#include "menu.h"
#include "sysdev.h"
#include <stdio.h>
#include "logger.h"
#include "game.h"
#include "menutree.h"
#include <string.h>
#include <stdlib.h>
#include <algorithm>

/* Two frames down is an edge at any frame rate the game runs at; three up lets
 * the navigator clear its debounce on the next poll. */
#define HOLD_FRAMES    2
#define RELEASE_FRAMES 3

#define KEY_UP_    inputdev::KEY_UP
#define KEY_DOWN_  inputdev::KEY_DOWN
#define KEY_ENTER_ inputdev::KEY_RETURN
#define KEY_ESC_   inputdev::KEY_ESCAPE

static unsigned g_goal = MENU_NO_GOAL;
static int      g_key;    // key being pulsed, 0 for none
static int      g_phase;  // > 0 holding, < 0 releasing

/* KAROO_MENU_TRACE=1 logs each node or cursor change while a goal stands. */
static int      g_trace = -1;
static uint8_t     g_last_node = 0xff;
static uint8_t     g_last_cursor = 0xff;

static bool trace_on(void)
{
    if (g_trace < 0) {
        char buf[8];
        g_trace = (sysdev::getEnv("KAROO_MENU_TRACE", buf, sizeof(buf))
                   && buf[0] && buf[0] != '0');
    }
    return g_trace > 0;
}

bool MenuState::read()
{
    const uint8_t *g = (const uint8_t *)Game::instance();
    *this = MenuState();
    if (!g) return false;

    const MenuTree *mt = ((const Game *)g)->menu();
    node_     = mt->node();
    cursor_   = mt->cursor();
    count_    = mt->childCount(node_);
    depth_    = mt->depth();
    last_key_ = mt->lastKey();
    lock_     = mt->lock();
    std::copy_n(mt->childRow(node_), 256, children_);
    valid_    = true;
    return true;
}

void menu_request(unsigned goal)
{
    if (goal != g_goal) {
        g_logger.write("menu: goal %s%u\n",
                  goal == MENU_NO_GOAL ? "cleared, was " : "-> node ",
                  goal == MENU_NO_GOAL ? g_goal : goal);
        g_goal = goal;
        g_key = 0; g_phase = 0;
    }
}

unsigned menu_goal(void) { return g_goal; }

void menu_pulse(int key)
{
    if (!g_key) { g_key = key; g_phase = HOLD_FRAMES; }
}

/* Breadth-first over the child tables from the current node.  Returns the
 * cursor index on the current node that starts the shortest route to the goal,
 * or -1 if the goal cannot be reached going forwards, in which case the caller
 * backs out with Escape.  Node ids are bytes, so a visited array of 256 is the
 * whole cycle protection. */
static int route_first_hop(const uint8_t *g, uint8_t from, uint8_t goal)
{
    if (from == goal) return -1;

    bool  seen[256] = {};
    short first[256];  // first hop from `from`, per node
    short queue[256];

    int head = 0, tail = 0;
    seen[from] = true;

    uint8_t n = ((const Game *)g)->menu()->childCount(from);
    for (uint8_t i = 0; i < n; i++) {
        uint8_t c = ((const Game *)g)->menu()->child(from, (unsigned char)i);
        if (c == goal) return i;
        if (seen[c]) continue;
        seen[c]  = true;
        first[c] = i;
        queue[tail++] = c;
    }

    while (head < tail) {
        uint8_t cur = (uint8_t)queue[head++];
        uint8_t cn  = ((const Game *)g)->menu()->childCount(cur);
        for (uint8_t i = 0; i < cn; i++) {
            uint8_t c = ((const Game *)g)->menu()->child(cur, (unsigned char)i);
            if (c == goal) return first[cur];
            if (seen[c]) continue;
            seen[c]  = true;
            first[c] = first[cur];
            if (tail < 256) queue[tail++] = c;
        }
    }
    return -1;
}

static void want(int key)
{
    if (g_key != key) { g_key = key; g_phase = HOLD_FRAMES; }
}

void menu_tick(void)
{
    // Advance the pulse whatever is decided below, so a release always
    // completes and the navigator's debounce clears.
    if (g_phase > 0) {
        if (--g_phase == 0) g_phase = -RELEASE_FRAMES;
    } else if (g_phase < 0) {
        if (++g_phase == 0) g_key = 0;
    }

    // KAROO_MENU_NODE=<n>: drive to one menu node once and stop there, so the
    // menu-only screens (the high-score table is node 3) can be reached
    // without a person.  One-shot, so reaching it does not re-request it.
    static int node_req = -1;
    if (node_req < 0) {
        char buf[16];
        node_req = 0;
        if (sysdev::getEnv("KAROO_MENU_NODE", buf, sizeof(buf)) && buf[0]) {
            node_req = 1;
            menu_request((unsigned)strtol(buf, NULL, 10));
        }
    }

    if (g_goal == MENU_NO_GOAL) return;

    const uint8_t *g = (const uint8_t *)Game::instance();
    if (!g) return;

    MenuState m;
    if (!m.read()) return;

    if (trace_on() && (m.node() != g_last_node || m.cursor() != g_last_cursor)) {
        char kids[128]; kids[0] = 0;
        for (uint8_t i = 0; i < m.count() && i < 16; i++) {
            char one[12];
            snprintf(one, sizeof(one), "%s%u", i ? "," : "", m.children()[i]);
            if (strlen(kids) + strlen(one) + 1 < sizeof(kids)) strcat(kids, one);
        }
        g_logger.write("menu: node=%u cursor=%u/%u depth=%u lock=%lu screen=%u "
                  "children=[%s] goal=%u\n",
                  m.node(), m.cursor(), m.count(), m.depth(), (unsigned long)m.lock(),
                  (unsigned)((const Game *)g)->state(), kids, g_goal);
        g_last_node = m.node(); g_last_cursor = m.cursor();
    }

    if ((uint8_t)g_goal == m.node()) {  // arrived
        g_logger.write("menu: reached node %u\n", m.node());
        menu_request(MENU_NO_GOAL);
        return;
    }

    if (m.lock()) return;        // the 200 ms lockout: keys are discarded
    if (g_key) return;         // a pulse is still in flight
    if (m.count() == 0) return;  // a leaf: nothing to press

    int hop = route_first_hop(g, m.node(), (uint8_t)g_goal);
    if (hop < 0) {
        // Not reachable forwards: back out one level and try from the parent.
        // At depth < 2 Escape would leave the menu, so don't.
        if (m.depth() >= 2) want(KEY_ESC_);
        return;
    }

    if (m.cursor() == (uint8_t)hop) {
        want(KEY_ENTER_);
        // The goal is met when Enter is pressed on the item leading to it:
        // action nodes such as "load slot 4" are passed through within the
        // same frame, so arriving is never seen, and a goal left standing
        // would repeat the action every time the menu came back.
        if (m.children()[hop] == (uint8_t)g_goal) {
            g_logger.write("menu: selected node %u (from node %u cursor %u)\n",
                      (unsigned)g_goal, m.node(), m.cursor());
            g_goal = MENU_NO_GOAL;
        }
        return;
    }

    // The navigator wraps both ways, so go the short way round.
    int down = ((int)hop - (int)m.cursor() + m.count()) % m.count();
    int up   = ((int)m.cursor() - (int)hop + m.count()) % m.count();
    want(down <= up ? KEY_DOWN_ : KEY_UP_);
}

bool menu_async_override(int key, bool *down)
{
    if (g_key && key == g_key && g_phase > 0) { *down = true; return true; }
    return false;
}
