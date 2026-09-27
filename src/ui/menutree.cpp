/* The node stack and the navigator.  The rewind leaves node 0 on the stack at
 * depth 1, which is why the navigator's back-out test is depth < 2.
 *
 * Negative controls (KAROO_SIM_FX), both unable to fail the suite, as
 * docs/CONTROLS.md records: "stacktop" makes the pop read one past the top,
 * but every pop in the recordings is overwritten before anything reads it;
 * "menuroot" opens every menu on its second item, but the autoplay driver
 * routes from wherever the cursor is, so only frames_run moves (+1, the
 * music-on signature).  "menuwrap" stops Down wrapping at the last entry.
 * KAROO_MENUSTACK_DIAG=1 logs every push, pop and rewind with running counts
 * and the deepest depth reached. */

#include <windows.h>
#include <string.h>

#include "log.h"
#include "menutree.h"
#include <stdlib.h>
#include "record.h"

static int s_fx_menuroot = 0;
static int s_fx_stacktop = 0;
static int s_diag        = 0;
static int s_init        = 0;

static unsigned s_pushes   = 0;
static unsigned s_pops     = 0;
static unsigned s_rewinds  = 0;
static unsigned s_maxdepth = 0;
static int s_logged_push   = 0;
static int s_logged_pop    = 0;
static int s_logged_rewind = 0;

static void fx_init(void)
{
    char buf[64];
    DWORD n;

    if (s_init)
        return;
    s_init = 1;

    n = GetEnvironmentVariableA("KAROO_SIM_FX", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        if (strcmp(buf, "menuroot") == 0) {
            s_fx_menuroot = 1;
            log_write("menustack: KAROO_SIM_FX=menuroot -- the rewind seeds "
                      "the cursor at 1, so every menu opens on the second "
                      "item\n");
        } else if (strcmp(buf, "stacktop") == 0) {
            s_fx_stacktop = 1;
            log_write("menustack: KAROO_SIM_FX=stacktop -- the pop reads one "
                      "entry PAST the top of the stack\n");
        }
    }

    n = GetEnvironmentVariableA("KAROO_MENUSTACK_DIAG", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0)
        s_diag = 1;
}

static void diag_census(void)
{
    if (!s_diag)
        return;
    // Every call, not a sample: the stack is used a few dozen times a run.
    log_write("menustack: DIAG push=%u pop=%u rewind=%u maxdepth=%u\n",
              s_pushes, s_pops, s_rewinds, s_maxdepth);
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PushMenuNodeOnStack(MenuTree *self, unsigned int nodeArg)
{
    self->push((unsigned char)nodeArg);
}

void MenuTree::push(unsigned char node)
{
    unsigned char depth;

    fx_init();

    depth = depth_;
    stack_[depth] = node;
    // PRESERVED: the depth is read again, not reused.
    depth_ = (unsigned char)(depth_ + 1);

    s_pushes++;
    if (depth_ > s_maxdepth)
        s_maxdepth = depth_;
    if (s_diag && !s_logged_push) {
        s_logged_push = 1;
        log_write("menustack: first push node=%u at depth=%u -> depth=%u\n",
                  node, depth, depth_);
    }
    diag_census();
}

void MenuTree::pop()
{
    unsigned char depth;
    unsigned char node;

    fx_init();

    depth = depth_;
    // PRESERVED: indexes from the depth byte itself, one below the array, with
    // the depth not yet decremented.  The two errors cancel and read
    // stack[depth - 1]; fixing either alone would change behaviour.  A pop at
    // depth 0 reads the depth byte as a node and wraps the depth to 0xff.
    node = s_fx_stacktop ? stack_[depth] : (&depth_)[depth];
    depth_  = (unsigned char)(depth - 1);
    node_   = node;
    cursor_ = savedCursor_[node];

    s_pops++;
    if (s_diag && !s_logged_pop) {
        s_logged_pop = 1;
        log_write("menustack: first pop at depth=%u -> node=%u cursor=%u "
                  "depth=%u\n", depth, node, cursor_, depth_);
    }
    diag_census();
}

void MenuTree::rewind()
{
    fx_init();

    depth_  = 0;
    cursor_ = s_fx_menuroot ? 1 : 0;
    node_   = 0;
    leave_  = 0;

    s_rewinds++;
    if (s_diag && !s_logged_rewind) {
        s_logged_rewind = 1;
        log_write("menustack: first rewind -- depth/cursor/node cleared, "
                  "pushing root\n");
    }

    push(0);
}

/* DETERMINISM: every key poll goes through hooks_GetAsyncKeyState, in this
 * order, and the discarded polls are part of the recording: while locked, Up,
 * Down, Escape and Enter are polled for nothing; after Up or Down, Left and
 * Right are.  The lock clears once now is more than 200 ms past its start
 * (unsigned).  Down's wrap test is a signed compare. */

#define KEY(k)  hooks_GetAsyncKeyState(k)

static int s_fx_menuwrap = -1;

void MenuTree::navigate(int now)
{
#define CHANGED changed_
#define LOCK    lock_
#define LEAVE   leave_
#define DEB     lastKey_
#define CUR     cursor_
#define DEPTH   depth_
#define NODE    node_
#define COUNT(n) childCount_[n]

    if (s_fx_menuwrap < 0) {
        char e[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", e, sizeof(e));
        s_fx_menuwrap = (n > 0 && n < sizeof(e) && strcmp(e, "menuwrap") == 0);
        if (s_fx_menuwrap)
            log_write("menunav: KAROO_SIM_FX=menuwrap -- DOWN does not wrap\n");
    }

    CHANGED = 0;
    if (LOCK != 0) {
        unsigned int t = (unsigned int)(long long)lockStart_;
        if ((unsigned int)now - t > 200)
            LOCK = 0;
        KEY(0x26);
        KEY(0x28);
        KEY(0x1b);
        KEY(0x0d);
    } else {
        if (DEB != 0x0d && KEY(0x0d) != 0) {
            unsigned char child = children_[NODE * CHILD_STRIDE + CUR];
            savedCursor_[NODE] = CUR;
            CUR = 0;
            CHANGED = 1;
            push(NODE);
            NODE = child;
            DEB = 0x0d;
        }
        if (DEB != 0x1b && KEY(0x1b) != 0) {
            if (DEPTH > 1) {
                pop();
                CHANGED = 1;
            } else {
                LEAVE = 1;
            }
            DEB = 0x1b;
        }
        if (DEB != 0x26 && KEY(0x26) != 0) {
            if (CUR > 0)
                CUR = (unsigned char)(CUR - 1);
            else
                CUR = (unsigned char)(COUNT(NODE) - 1);
            DEB = 0x26;
            KEY(0x25);
            KEY(0x27);
        }
        if (DEB != 0x28 && KEY(0x28) != 0) {
            if ((int)CUR < (int)COUNT(NODE) - 1)
                CUR = (unsigned char)(CUR + 1);
            else if (!s_fx_menuwrap)
                CUR = 0;
            DEB = 0x28;
            KEY(0x25);
            KEY(0x27);
        }
    }
    if (KEY(DEB) == 0)
        DEB = 0;
#undef CHANGED
#undef LOCK
#undef LEAVE
#undef DEB
#undef CUR
#undef DEPTH
#undef NODE
#undef COUNT
}

static void *const g_MenuTreeVtable[1] = { (void *)&MenuTree_ScalarDestructor };

/* The vtable and these six fields; nothing else is touched. */
void MenuTree::construct()
{
    vtable_       = g_MenuTreeVtable;
    depth_        = 0;
    cursor_       = 0;
    lastKey_      = 0;
    leave_        = 0;
    lock_         = 0;
    lastNodeSeen_ = 0;
}

void MenuTree::destruct()
{
    vtable_ = g_MenuTreeVtable;
}

extern "C" __declspec(dllexport) MenuTree *__attribute__((thiscall))
MenuTree_ScalarDestructor(MenuTree *self, unsigned char flags)
{
    self->destruct();
    if (flags & 1)
        free(self);
    return self;
}

/* Children are node ids; 0xc8 + i is save slot i's entry.  Node 2 is
 * Load Game, 42 Save Game, 40 the level-complete choice. */
void MenuTree::buildDefaultGraph(unsigned char saveSlots)
{
    static const unsigned char root[]     = { 1, 2, 3, 4, 5, 6 };
    static const unsigned char n10[]      = { 0x14, 0x15, 0x17, 0x16, 0x18, 0x19, 0x1c, 0x1a,
                                              0x1b, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22 };
    static const unsigned char n11[]      = { 0x47, 0x48, 0x49, 0x4a };
    static const unsigned char n12[]      = { 0x3c, 0x3f, 0x3d, 0x3e };

    for (unsigned i = 0; i < sizeof root; i++) setChild(0, i, root[i]);
    childCount_[0] = 6;
    for (unsigned i = 0; i < saveSlots; i++) setChild(2, i, (unsigned char)(i + 0xc8));
    childCount_[2] = saveSlots;
    setChild(40, 0, 0x29);
    setChild(40, 1, 0x2a);
    childCount_[40] = 2;
    for (unsigned i = 0; i < saveSlots; i++) setChild(42, i, (unsigned char)(saveSlots + i + 0xc8));
    setChild(4, 0, 10);
    childCount_[42] = saveSlots;
    setChild(3, 0, 0x32);
    childCount_[3] = 1;
    setChild(5, 0, 0x50);
    childCount_[5] = 1;
    setChild(4, 1, 0x0b);
    setChild(4, 2, 0x0c);
    childCount_[4] = 3;
    for (unsigned i = 0; i < sizeof n10; i++) setChild(10, i, n10[i]);
    childCount_[10] = sizeof n10;
    for (unsigned i = 0; i < sizeof n11; i++) setChild(11, i, n11[i]);
    childCount_[11] = sizeof n11;
    for (unsigned i = 0; i < sizeof n12; i++) setChild(12, i, n12[i]);
    childCount_[12] = sizeof n12;
    node_ = 0;
    push(0);
}
