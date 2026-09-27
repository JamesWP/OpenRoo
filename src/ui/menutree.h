/* MenuTree: the game's menu, a sub-object of Game (menu.h is the autoplay
 * driver that reads it).  The menu is a graph of byte node ids: each node has
 * a row of child node ids, the cursor picks one, Enter pushes the node and
 * moves to the child, Escape pops.  Entering some nodes is an action, taken by
 * the keypress handler (keypress.cpp).  Construction zeroes only the depth,
 * cursor, last key, leave, lock and last-seen fields. */
#pragma once

#include "layout.h"

class __attribute__((packed)) MenuTree {
public:
    static const int ORIGIN = 0;
    static const int CHILD_STRIDE = 0xff;

    // The routines that take the menu as this.
    void push(unsigned char node);
    void pop();
    void rewind();
    void navigate(int now);

    // Called only by the Game's construction and destruction.
    void construct();
    void destruct();
    // Builds the fixed menu graph, then push(0).  Nodes 2 (Load Game) and 42
    // (Save Game) get one child per save slot.
    void buildDefaultGraph(unsigned char saveSlots);

    // Set by a navigation that moved the cursor into a new node.
    unsigned int   changed() const                  { return changed_; }
    // The keypress handler's copy of the node as of its last pass, so Enter on
    // a node it has not seen yet does not click.
    unsigned short lastNodeSeen() const             { return lastNodeSeen_; }
    void           setLastNodeSeen(unsigned short n) { lastNodeSeen_ = n; }

    // The 200 ms input lock: while lock() is non-zero every key is discarded,
    // until now passes lockStart by 200.
    void           setLockStart(double t)           { lockStart_ = t; }
    unsigned int   lock() const                     { return lock_; }
    void           setLock(unsigned int l)          { lock_ = l; }
    // Escape at the root: leave the menu.
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
    // For the level select's nodes (levelselect.h), which the game's builder
    // never fills.
    void           setChild(unsigned char node, unsigned char i, unsigned char c)
    {
        children_[(unsigned)node * CHILD_STRIDE + i] = c;
    }
    // A node's row of children, for callers that copy it whole.  The autoplay
    // driver copies 256 bytes, one past the row.
    const unsigned char *childRow(unsigned char node) const
    {
        return &children_[(unsigned)node * CHILD_STRIDE];
    }
    unsigned char  depth() const                    { return depth_; }
    unsigned char  node() const                     { return node_; }
    void           setNode(unsigned char n)         { node_ = n; }
    // The node as an lvalue.
    unsigned char &nodeRef()                        { return node_; }

private:
    MenuTree() = delete;  // only ever reached through the Game
    KAROO_LAYOUT_REGISTER(MenuTree);

    const void    *vtable_;  // our one-slot table
    unsigned int   changed_;
    unsigned short lastNodeSeen_;
    unsigned char  gap_0a[0x0c - 0x0a];
    double         lockStart_;
    unsigned int   lock_;
    unsigned int   leave_;
    unsigned char  lastKey_;
    unsigned char  cursor_;
    unsigned char  savedCursor_[0xff];  // by node id
    unsigned char  childCount_[0xff];   // by node id
    // children_[node * 0xff + i].  How many rows the game uses is not settled,
    // so it runs, undivided, to the depth byte.
    unsigned char  children_[0x2001d - 0x21c];
    unsigned char  depth_;
    // PRESERVED: no bounds check anywhere; a runaway push walks off the end.
    unsigned char  stack_[0x2021c - 0x2001e];
    unsigned char  node_;
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

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PushMenuNodeOnStack(MenuTree *self, unsigned int nodeArg);

/* The one slot of MenuTree's vtable. */
extern "C" __declspec(dllexport) MenuTree *__attribute__((thiscall))
MenuTree_ScalarDestructor(MenuTree *self, unsigned char flags);
