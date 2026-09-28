/* FoePath: the foe pathfinder, a best-first (A*-shaped) search over the tile
 * grid, run backward from the target to the foe.  One per foe, at foe+0x13b;
 * 0x35 bytes.  The object, its worklist block, cells and nodes are all
 * allocated and freed here (new and delete, calloc and free).  foepath.cpp has
 * each method's PRESERVED notes. */

#pragma once

#include "layout.h"

/* A search node, calloc(1, 0x44).  FoePath builds and links them; the chase
 * (foe.cpp) reads the result's parent and cell. */
class __attribute__((packed)) PathNode {
public:
    static const int ORIGIN = 0;

    PathNode *parent() const { return parent_; }
    int       u() const      { return u_; }
    int       v() const      { return v_; }

private:
    friend class FoePath;  // builds, links and frees them

    // PRESERVED: the child scan has no bound, so a ninth child is stored one
    // past the array, onto next.  Written as that store, so the defect is kept
    // without an out-of-bounds access.
    void recordChild(PathNode *node)
    {
        int i = 0;
        while (children_[i] != 0 && ++i < 8)
            ;
        if (i == 8)
            next_ = node;  // children[8] is next
        else
            children_[i] = node;
    }

    int       f_;            // +0x00  g + h, the open list's order
    int       h_;            // +0x04  the squared distance to the goal
    int       g_;            // +0x08  steps from the seed
    int       field_0c_;     // +0x0c  never touched
    int       u_;            // +0x10
    int       v_;            // +0x14
    int       key_;          // +0x18  FoePath::cellKey(u, v)
    PathNode *parent_;       // +0x1c
    PathNode *children_[8];  // +0x20  the nodes relaxed through this one
    PathNode *next_;         // +0x40  the open or closed chain

    KAROO_LAYOUT_REGISTER(PathNode);
};

KAROO_LAYOUT_CHECKS(PathNode)
{
    KAROO_LAYOUT_AT(f_,        0x00);
    KAROO_LAYOUT_AT(h_,        0x04);
    KAROO_LAYOUT_AT(g_,        0x08);
    KAROO_LAYOUT_AT(u_,        0x10);
    KAROO_LAYOUT_AT(v_,        0x14);
    KAROO_LAYOUT_AT(key_,      0x18);
    KAROO_LAYOUT_AT(parent_,   0x1c);
    KAROO_LAYOUT_AT(children_, 0x20);
    KAROO_LAYOUT_AT(next_,     0x40);
    // calloc(1, 0x44).
    KAROO_LAYOUT_SIZE(0x44);
}

/* One cell of the cost-propagation worklist: calloc(1, 9), nine bytes for two
 * pointers; the ninth is never touched. */
class __attribute__((packed)) PendingCell {
public:
    static const int ORIGIN = 0;

private:
    friend class FoePath;  // the worklist is FoePath's

    PathNode    *node_;      // +0x00
    PendingCell *next_;      // +0x04
    unsigned char field_8_;  // +0x08  never touched

    KAROO_LAYOUT_REGISTER(PendingCell);
};

KAROO_LAYOUT_CHECKS(PendingCell)
{
    KAROO_LAYOUT_AT(node_, 0x00);
    KAROO_LAYOUT_AT(next_, 0x04);
    KAROO_LAYOUT_SIZE(9);
}

/* The worklist's owner block, calloc(1, 9).  Only the head is ever touched:
 * the worklist is a stack. */
class __attribute__((packed)) PendingStack {
public:
    static const int ORIGIN = 0;

private:
    friend class FoePath;

    int           field_0_;  // +0x00  never touched
    PendingCell  *head_;     // +0x04
    unsigned char field_8_;  // +0x08  never touched

    KAROO_LAYOUT_REGISTER(PendingStack);
};

KAROO_LAYOUT_CHECKS(PendingStack)
{
    KAROO_LAYOUT_AT(head_, 0x04);
    KAROO_LAYOUT_SIZE(9);
}


class __attribute__((packed)) FoePath {
public:
    static const int ORIGIN = 0;

    // Allocates (nothrow) and populates one; NULL if the allocation fails, and
    // the caller stores that.
    static FoePath *create(unsigned char *tileBase, unsigned short field04);
    // dispose(), then delete.
    static void destroy(FoePath *p);

    // The chase's side (Foe::chase, foe.cpp).
    // The search's iteration budget, from the chase's speed.
    void setCap(unsigned short n)                  { cap_ = n; }
    // Written by the chase, and again by the search.
    void setTarget(unsigned char u, unsigned char v)
    {
        targetU_ = u;
        targetV_ = v;
    }
    // The mover mode passable() branches on: the spawn stores the foe type,
    // the chase 0 or 2.
    void setMode(unsigned char m)                  { mode_ = m; }
    // The node the search stopped on: the foe's cell, since it runs backward.
    // The chase advances it to its parent.
    PathNode *result() const                       { return result_; }
    void setResult(PathNode *n)                    { result_ = n; }

    // The search (foepath.cpp).
    int       find(int uFoe, int vFoe, int uTarget, int vTarget);
    int       cellKey(int u, int v);
    int       passable(int u, int v);
    void      releaseLists();
    int       search(int uFoe, int vFoe, int uTarget, int vTarget);
    PathNode *popBestOpen();
    void      expand(PathNode *node, int goalU, int goalV);
    void      relax(PathNode *parent, int u, int v,
                    int goalU, int goalV);
    PathNode *findOpen(int key);
    PathNode *findClosed(int key);
    void      insertOpenByCost(PathNode *node);
    void      propagate(PathNode *node);
    void      pushPending(PathNode *node);
    PathNode *popPending();

    // The destructor body: frees the nodes and the worklist block.  The caller
    // frees the FoePath itself.
    void      dispose();

private:
    FoePath() = delete;  // built by create()
    // The constructor's stores.
    void populate(unsigned char *tileBase, unsigned short field04);
    static PathNode *findByKey(PathNode *hdr, int key);

    KAROO_LAYOUT_REGISTER(FoePath);

    unsigned char *tileBase_;  // +0x00  the ctor's argument
    unsigned short field_04;   // +0x04  the ctor's second argument (0)
    PathNode      *open_;      // +0x06  a header node, fresh each search
    PathNode      *closed_;    // +0x0a  likewise
    PathNode      *result_;    // +0x0e
    PendingStack  *pending_;   // +0x12
    int            found_;     // +0x16  mirrors find()'s return
    int            extentV_;   // +0x1a  the map's v extent
    // +0x1e  the map's u extent.  Not the tile stride (100).
    int            keyStride_;
    unsigned char  gap_022[0x02a - 0x022];
    unsigned char  mode_;  // +0x2a
    unsigned char  gap_02b[0x02f - 0x02b];
    unsigned short cap_;      // +0x2f
    unsigned char  targetU_;  // +0x31
    unsigned char  targetV_;  // +0x32
    unsigned char  foeU_;     // +0x33
    unsigned char  foeV_;     // +0x34
};

KAROO_LAYOUT_CHECKS(FoePath)
{
    KAROO_LAYOUT_AT(tileBase_,  0x00);
    KAROO_LAYOUT_AT(field_04,   0x04);
    KAROO_LAYOUT_AT(open_,      0x06);
    KAROO_LAYOUT_AT(closed_,    0x0a);
    KAROO_LAYOUT_AT(result_,    0x0e);
    KAROO_LAYOUT_AT(pending_,   0x12);
    KAROO_LAYOUT_AT(found_,     0x16);
    KAROO_LAYOUT_AT(extentV_,   0x1a);
    KAROO_LAYOUT_AT(keyStride_, 0x1e);
    KAROO_LAYOUT_AT(mode_,      0x2a);
    KAROO_LAYOUT_AT(cap_,       0x2f);
    KAROO_LAYOUT_AT(targetU_,   0x31);
    KAROO_LAYOUT_AT(targetV_,   0x32);
    KAROO_LAYOUT_AT(foeU_,      0x33);
    KAROO_LAYOUT_AT(foeV_,      0x34);
    // The allocation size.
    KAROO_LAYOUT_SIZE(0x35);
}
