/* Menu reader and driver — AI_PLAN.md Stage 5.
 *
 * ── Where these offsets come from ────────────────────────────────────────
 *
 * `FUN_0041ec40` (0x0041EC40) is the menu navigator.  It is reached from
 * `Game::HandleKeypress` as `FUN_0041ec40(&this->field_0x175518, ...)`, so its
 * `this` is Game+0x175518 and every offset below is that base plus the offset
 * the decompile shows.  It is also the function REPLAY_PLAN's Stage D tripped
 * over: two of its GetAsyncKeyState references are *hoisted* `mov reg,[IAT]`
 * loads rather than `FF 15` call sites, which is why patch.py has
 * MOV_IAT_REDIRECT_PATCHES at 0x1EC5A and 0x1EDA0.  Because those are already
 * patched, this driver needs **no new binary patches at all** — it answers the
 * navigator's key queries through the existing hooks_GetAsyncKeyState.
 *
 *   MENU base                 Game + 0x175518
 *   +0x04  changed flag       Game + 0x17551c
 *   +0x14  input lock timer   Game + 0x17552c   (nonzero = keys ignored)
 *   +0x18  leave-menu flag    Game + 0x175530
 *   +0x1c  last key handled   Game + 0x175534   (debounce)
 *   +0x1d  cursor             Game + 0x175535
 *   +0x1e  saved cursor[node] Game + 0x175536 + node
 *   +0x11d children count[]   Game + 0x175635 + node
 *   +0x21c child[node][i]     Game + 0x175734 + node*0xff + i
 *   +0x2001d stack depth      Game + 0x195535
 *   +0x2001e node stack[]     Game + 0x195536 + i
 *   +0x2021c current node     Game + 0x195734
 *
 * Every one of those cross-checks against a *different* function:
 * Game::HandleKeypress independently reads the cursor at 0x175535, the node at
 * 0x195734, the count table at 0x175635 and the child table at 0x175734.  Two
 * unrelated decompiles agreeing on all four is why this is stated plainly
 * rather than marked as inferred.
 *
 * ── The navigator's rules, as implemented there ──────────────────────────
 *
 *   ENTER : child = children[node][cursor]; saved[node] = cursor;
 *           cursor = 0; push(node); node = child
 *   ESC   : depth < 2 ? leave the menu : pop() (node = stack top,
 *           cursor = saved[node])
 *   UP    : cursor = cursor ? cursor - 1 : count[node] - 1
 *   DOWN  : cursor = cursor < count[node] - 1 ? cursor + 1 : 0
 *
 * Two things make key *edges* rather than levels the unit of input:
 *   - the navigator ignores a key whose code equals `last key handled`, and
 *   - it clears that field only when GetAsyncKeyState for it reads 0.
 * So a held key acts exactly once.  The driver therefore presses in pulses:
 * HOLD frames asserted, then RELEASE frames not asserted.  It also waits out
 * the 200 ms lockout at +0x14, during which every key is discarded.
 *
 * ── Why key synthesis and not writing the fields ─────────────────────────
 *
 * Setting `node` directly would skip HandleKeypress's switch, which is where
 * the *actions* live — loading a slot, quitting, advancing a level.  Node ids
 * are both "where the cursor is" and "what happens when you pick it", so the
 * only correct way to pick one is to press the keys.  That also keeps the
 * DLL's no-writes-to-game-state property intact.
 */
#include "menu.h"
#include "log.h"
#include "game.h"
#include <string.h>
#include <stdlib.h>


#define OFF_LOCK      0x17552c
#define OFF_LASTKEY   0x175534
#define OFF_CURSOR    0x175535
#define OFF_COUNTS    0x175635
#define OFF_CHILDREN  0x175734
#define OFF_DEPTH     0x195535
#define OFF_NODE      0x195734
#define CHILD_STRIDE  0xff

/* Pulse shape.  Two frames asserted is comfortably an edge at any frame rate
 * the game runs at; three released is enough for the navigator to clear its
 * debounce field on the following query. */
#define HOLD_FRAMES    2
#define RELEASE_FRAMES 3

#define VK_UP_    0x26
#define VK_DOWN_  0x28
#define VK_ENTER_ 0x0d
#define VK_ESC_   0x1b

static unsigned g_goal = MENU_NO_GOAL;
static int      g_key;          /* vkey being pulsed, 0 = none */
static int      g_phase;        /* >0 holding, <0 releasing    */
static int      g_trace = -1;
static BYTE     g_last_node = 0xff;
static BYTE     g_last_cursor = 0xff;

static bool trace_on(void)
{
    if (g_trace < 0) {
        char buf[8];
        g_trace = (GetEnvironmentVariableA("KAROO_MENU_TRACE", buf, sizeof(buf))
                   && buf[0] && buf[0] != '0');
    }
    return g_trace > 0;
}

bool menu_read(MenuState *m)
{
    const BYTE *g = (const BYTE *)Game::instance();
    memset(m, 0, sizeof(*m));
    if (!g) return false;

    m->node     = g[OFF_NODE];
    m->cursor   = g[OFF_CURSOR];
    m->count    = g[OFF_COUNTS + m->node];
    m->depth    = g[OFF_DEPTH];
    m->last_key = g[OFF_LASTKEY];
    m->lock     = *(const DWORD *)(g + OFF_LOCK);
    memcpy(m->children, g + OFF_CHILDREN + (unsigned)m->node * CHILD_STRIDE, 256);
    m->valid    = true;
    return true;
}

void menu_request(unsigned goal)
{
    if (goal != g_goal) {
        log_write("menu: goal %s%u\n",
                  goal == MENU_NO_GOAL ? "cleared, was " : "-> node ",
                  goal == MENU_NO_GOAL ? g_goal : goal);
        g_goal = goal;
        g_key = 0; g_phase = 0;
    }
}

unsigned menu_goal(void) { return g_goal; }

void menu_pulse(int vkey)
{
    if (!g_key) { g_key = vkey; g_phase = HOLD_FRAMES; }
}

/* ── routing ────────────────────────────────────────────────────────────
 *
 * Breadth-first over the child tables, from the current node.  Returns the
 * cursor index on the *current* node that starts the shortest route to the
 * goal, or -1 if the goal is not reachable going forwards (in which case the
 * caller backs out with ESC and tries again from the parent).
 *
 * Node ids are bytes, so the graph has at most 256 nodes and a plain visited
 * array is the whole cycle protection needed.
 */
static int route_first_hop(const BYTE *g, BYTE from, BYTE goal)
{
    if (from == goal) return -1;

    bool  seen[256];
    short first[256];      /* first hop (cursor index on `from`) per node */
    short queue[256];
    memset(seen, 0, sizeof(seen));

    int head = 0, tail = 0;
    seen[from] = true;

    BYTE n = g[OFF_COUNTS + from];
    for (BYTE i = 0; i < n; i++) {
        BYTE c = g[OFF_CHILDREN + (unsigned)from * CHILD_STRIDE + i];
        if (c == goal) return i;
        if (seen[c]) continue;
        seen[c]  = true;
        first[c] = i;
        queue[tail++] = c;
    }

    while (head < tail) {
        BYTE cur = (BYTE)queue[head++];
        BYTE cn  = g[OFF_COUNTS + cur];
        for (BYTE i = 0; i < cn; i++) {
            BYTE c = g[OFF_CHILDREN + (unsigned)cur * CHILD_STRIDE + i];
            if (c == goal) return first[cur];
            if (seen[c]) continue;
            seen[c]  = true;
            first[c] = first[cur];
            if (tail < 256) queue[tail++] = c;
        }
    }
    return -1;
}

/* ── driving ────────────────────────────────────────────────────────────── */

static void want(int vkey)
{
    if (g_key != vkey) { g_key = vkey; g_phase = HOLD_FRAMES; }
}

void menu_tick(void)
{
    /* Advance the pulse regardless of whether a decision is taken below, so a
     * release always completes and the navigator's debounce clears. */
    if (g_phase > 0) {
        if (--g_phase == 0) g_phase = -RELEASE_FRAMES;
    } else if (g_phase < 0) {
        if (++g_phase == 0) g_key = 0;
    }

    /* KAROO_MENU_NODE=<n> — drive to one menu node, once, and stop there.
     * A test hook for the menu-gated screens (the high-score table is node 3,
     * per RenderGameFrame's `field_0x195734 == 3` gate), so they can be
     * reached without a human at the keyboard.  Read by value; one-shot, so
     * the arrival that clears g_goal does not re-request it. */
    static int node_req = -1;
    if (node_req < 0) {
        char buf[16];
        node_req = 0;
        if (GetEnvironmentVariableA("KAROO_MENU_NODE", buf, sizeof(buf)) && buf[0]) {
            node_req = 1;
            menu_request((unsigned)strtol(buf, NULL, 10));
        }
    }

    if (g_goal == MENU_NO_GOAL) return;

    const BYTE *g = (const BYTE *)Game::instance();
    if (!g) return;

    MenuState m;
    if (!menu_read(&m)) return;

    if (trace_on() && (m.node != g_last_node || m.cursor != g_last_cursor)) {
        char kids[128]; kids[0] = 0;
        for (BYTE i = 0; i < m.count && i < 16; i++) {
            char one[12];
            wsprintfA(one, "%s%u", i ? "," : "", m.children[i]);
            if (strlen(kids) + strlen(one) + 1 < sizeof(kids)) strcat(kids, one);
        }
        log_write("menu: node=%u cursor=%u/%u depth=%u lock=%lu screen=%u "
                  "children=[%s] goal=%u\n",
                  m.node, m.cursor, m.count, m.depth, (unsigned long)m.lock,
                  (unsigned)g[0x2ab58c], kids, g_goal);
        g_last_node = m.node; g_last_cursor = m.cursor;
    }

    if ((BYTE)g_goal == m.node) {          /* arrived */
        log_write("menu: reached node %u\n", m.node);
        menu_request(MENU_NO_GOAL);
        return;
    }

    if (m.lock) return;                    /* 200 ms lockout — keys discarded */
    if (g_key) return;                     /* a pulse is still in flight */
    if (m.count == 0) return;              /* leaf with no children; nothing to press */

    int hop = route_first_hop(g, m.node, (BYTE)g_goal);
    if (hop < 0) {
        /* Not reachable forwards.  Back out one level and try again from the
         * parent; at depth < 2 ESC leaves the menu entirely, so don't. */
        if (m.depth >= 2) want(VK_ESC_);
        return;
    }

    if (m.cursor == (BYTE)hop) {
        want(VK_ENTER_);
        /* Clearing the goal on `node == goal` is not enough, and getting that
         * wrong caused a real runaway: node 204 ("load save slot 4") is a leaf
         * *action*, not a place the cursor comes to rest — HandleKeypress sees
         * the id, loads the level and the node moves on within the same frame,
         * so the arrival is never observed.  The goal stayed set for the whole
         * run, and every time the menu came back (after each game over) the
         * driver dutifully loaded the save again, forever.
         *
         * The request is fulfilled the moment the selection is committed, so
         * clear it when pressing ENTER on the item whose child IS the goal. */
        if (m.children[hop] == (BYTE)g_goal) {
            log_write("menu: selected node %u (from node %u cursor %u)\n",
                      (unsigned)g_goal, m.node, m.cursor);
            g_goal = MENU_NO_GOAL;
        }
        return;
    }

    /* Move the cursor the short way round; the navigator wraps in both
     * directions, so either key gets there. */
    int down = ((int)hop - (int)m.cursor + m.count) % m.count;
    int up   = ((int)m.cursor - (int)hop + m.count) % m.count;
    want(down <= up ? VK_DOWN_ : VK_UP_);
}

bool menu_async_override(int vkey, SHORT *out)
{
    if (g_key && vkey == g_key && g_phase > 0) { *out = (SHORT)0x8000; return true; }
    return false;
}
