/* MenuTree -- the game's menu, embedded in Game at +0x175518 (COHESION_PLAN.md
 * Band 3).  Not to be confused with menu.h, which is our AI driver that
 * READS this object; this header is the object itself.
 *
 * The layout is menu.cpp's, derived from NavigateMenuTree 0x41ec40 and
 * cross-checked against HandleKeypress, plus what the ctor settles:
 * PopulateMenuTreeDefaults 0x41eb70 stores the vtable 0x45d41c and zeroes
 * depth, cursor, lastKey, leave, lock and lastNodeSeen -- nothing else.
 * The dtor 0x41ebc0 only restores the vtable.  Both are still the game's,
 * run by Game's Load/Destruct.  The vtable has one slot, the deleting dtor
 * 0x41eba0; the next dword, 0x41ee00, is the high-score table's.
 *
 * 0x2021d bytes: the current-node byte is the last, and Game+0x195735 --
 * the script/checkpoint block -- follows immediately.
 */
#pragma once

#include "layout.h"

class __attribute__((packed)) MenuTree {
public:
    static const int ORIGIN = 0;
    static const int CHILD_STRIDE = 0xff;

    /* The four routines that take the menu as `this` (menutree.cpp,
     * menunav.cpp); the Sim_* exports below are one-line shims onto them. */
    void push(unsigned char node);          /* PushMenuNodeOnStack 0x41ebd0 */
    void pop();                             /* PopMenuNodeFromStack 0x41ec00 */
    void rewind();                          /* RewindMenuStackToRootNode 0x41edd0 */
    void navigate(int now);                 /* NavigateMenuTree 0x41ec40 */

    /* Game-embedded lifecycle, called only by Game_Construct / Game_Destruct
     * (gamelife.cpp).  The vtable installed is ours (one slot, the scalar
     * dtor below); the game's is left as a tripwire. */
    void construct();                       /* 0x41eb70 */
    void destruct();                        /* 0x41ebc0 */
    /* 0x418ab0 -- the fixed menu graph, then push(0).  Nodes 2 (Load
     * Game) and 42 get one child per save slot. */
    void buildDefaultGraph(unsigned char saveSlots);

    /* Set by a navigation that moved the cursor into a new node. */
    unsigned int   changed() const                  { return changed_; }
    /* HandleKeypress's copy of the node as of its last pass, so ENTER on
     * a node it has not seen yet does not click. */
    unsigned short lastNodeSeen() const             { return lastNodeSeen_; }
    void           setLastNodeSeen(unsigned short n) { lastNodeSeen_ = n; }

    /* The 200 ms input lock: while lock() is nonzero every key is
     * discarded, until `now` passes lockStart by 200.  The original copies
     * the time in as two dword MOVs; it is one double. */
    void           setLockStart(double t)           { lockStart_ = t; }
    unsigned int   lock() const                     { return lock_; }
    void           setLock(unsigned int l)          { lock_ = l; }
    /* ESC at the root: leave the menu. */
    unsigned int   leave() const                    { return leave_; }

    unsigned char  lastKey() const                  { return lastKey_; }
    void           setLastKey(unsigned char k)      { lastKey_ = k; }
    unsigned char  cursor() const                   { return cursor_; }
    void           setCursor(unsigned char c)       { cursor_ = c; }
    unsigned char  savedCursor(unsigned char node) const { return savedCursor_[node]; }
    void           setSavedCursor(unsigned char node, unsigned char c) { savedCursor_[node] = c; }
    unsigned char  childCount(unsigned char node) const { return childCount_[node]; }
    void           setChildCount(unsigned char node, unsigned char n) { childCount_[node] = n; }
    unsigned char  child(unsigned char node, unsigned char i) const
    {
        return children_[(unsigned)node * CHILD_STRIDE + i];
    }
    /* Our addition, for the level select's nodes (levelselect.h), which
     * the game's builder never fills. */
    void           setChild(unsigned char node, unsigned char i, unsigned char c)
    {
        children_[(unsigned)node * CHILD_STRIDE + i] = c;
    }
    /* A node's row of children, for callers that copy it whole (menu.cpp
     * copies 256 bytes -- one past the 0xff-byte row, as it always has). */
    const unsigned char *childRow(unsigned char node) const
    {
        return &children_[(unsigned)node * CHILD_STRIDE];
    }
    unsigned char  depth() const                    { return depth_; }
    unsigned char  node() const                     { return node_; }
    void           setNode(unsigned char n)         { node_ = n; }
    /* For the files that alias the node as a NODE lvalue. */
    unsigned char &nodeRef()                        { return node_; }

private:
    MenuTree() = delete;    /* game-owned; only ever reached by pointer */
    KAROO_LAYOUT_REGISTER(MenuTree);

    const void    *vtable_;                 /* +0x00     0x45d41c */
    unsigned int   changed_;                /* +0x04 */
    unsigned short lastNodeSeen_;           /* +0x08 */
    unsigned char  gap_0a[0x0c - 0x0a];
    double         lockStart_;              /* +0x0c */
    unsigned int   lock_;                   /* +0x14 */
    unsigned int   leave_;                  /* +0x18 */
    unsigned char  lastKey_;                /* +0x1c */
    unsigned char  cursor_;                 /* +0x1d */
    unsigned char  savedCursor_[0xff];      /* +0x1e     by node id */
    unsigned char  childCount_[0xff];       /* +0x11d    by node id */
    /* node * 0xff + i.  Its row count is not settled, so it runs, undivided,
     * to the depth byte. */
    unsigned char  children_[0x2001d - 0x21c]; /* +0x21c */
    unsigned char  depth_;                  /* +0x2001d */
    /* No bounds check anywhere: a runaway push walks off the end. */
    unsigned char  stack_[0x2021c - 0x2001e]; /* +0x2001e */
    unsigned char  node_;                   /* +0x2021c */
};

KAROO_LAYOUT_CHECKS(MenuTree)
{
    KAROO_LAYOUT_AT(changed_,      0x04);
    KAROO_LAYOUT_AT(lastNodeSeen_, 0x08);
    KAROO_LAYOUT_AT(lockStart_,    0x0c);
    KAROO_LAYOUT_AT(lock_,         0x14);
    KAROO_LAYOUT_AT(leave_,        0x18);
    KAROO_LAYOUT_AT(lastKey_,      0x1c);
    KAROO_LAYOUT_AT(cursor_,       0x1d);
    KAROO_LAYOUT_AT(savedCursor_,  0x1e);
    KAROO_LAYOUT_AT(childCount_,   0x11d);
    KAROO_LAYOUT_AT(children_,     0x21c);
    KAROO_LAYOUT_AT(depth_,        0x2001d);
    KAROO_LAYOUT_AT(stack_,        0x2001e);
    KAROO_LAYOUT_AT(node_,         0x2021c);
    KAROO_LAYOUT_SIZE(0x2021d);
}

/* The exports patch.py binds by name. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PushMenuNodeOnStack(MenuTree *self, unsigned int nodeArg);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PopMenuNodeFromStack(MenuTree *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_RewindMenuStackToRootNode(MenuTree *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_NavigateMenuTree(MenuTree *self, int now);

/* 0x41eba0, slot 0 of our MenuTree table. */
extern "C" __declspec(dllexport) MenuTree *__attribute__((thiscall))
MenuTree_ScalarDestructor(MenuTree *self, unsigned char flags);
