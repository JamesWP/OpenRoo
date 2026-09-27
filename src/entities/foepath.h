/* FoePath: the foe pathfinder, a best-first (A*-shaped) search over the tile
 * grid, run backward from the target to the foe.  One per foe, at foe+0x13b;
 * 0x35 bytes.  The object, its worklist block, cells and nodes are all
 * allocated and freed here (new and delete, calloc and free).  foepath.cpp has
 * each method's PRESERVED notes. */

#pragma once

#include "layout.h"

/* A search node, calloc(1, 0x44).  A plain public record: FoePath builds and
 * links them, and the chase (foe.cpp) reads the result's parent and cell. */
struct __attribute__((packed)) PathNode {
    static const int ORIGIN = 0;

    int       f;            // +0x00  g + h, the open list's order
    int       h;            // +0x04  the squared distance to the goal
    int       g;            // +0x08  steps from the seed
    int       field_0c;     // +0x0c  never touched
    int       u;            // +0x10
    int       v;            // +0x14
    int       key;          // +0x18  FoePath::cellKey(u, v)
    PathNode *parent;       // +0x1c
    PathNode *children[8];  // +0x20  the nodes relaxed through this one
    PathNode *next;         // +0x40  the open or closed chain

    // PRESERVED: the child scan has no bound, so a ninth child is stored one
    // past the array, onto next.  Written as that store, so the defect is kept
    // without an out-of-bounds access.
    void recordChild(PathNode *node)
    {
        int i = 0;
        while (children[i] != 0 && ++i < 8)
            ;
        if (i == 8)
            next = node;  // children[8] is next
        else
            children[i] = node;
    }

private:
    KAROO_LAYOUT_REGISTER(PathNode);
};

KAROO_LAYOUT_CHECKS(PathNode)
{
    KAROO_LAYOUT_AT(f,        0x00);
    KAROO_LAYOUT_AT(h,        0x04);
    KAROO_LAYOUT_AT(g,        0x08);
    KAROO_LAYOUT_AT(u,        0x10);
    KAROO_LAYOUT_AT(v,        0x14);
    KAROO_LAYOUT_AT(key,      0x18);
    KAROO_LAYOUT_AT(parent,   0x1c);
    KAROO_LAYOUT_AT(children, 0x20);
    KAROO_LAYOUT_AT(next,     0x40);
    // calloc(1, 0x44).
    KAROO_LAYOUT_SIZE(0x44);
}

/* One cell of the cost-propagation worklist: calloc(1, 9), nine bytes for two
 * pointers; the ninth is never touched. */
struct __attribute__((packed)) PendingCell {
    static const int ORIGIN = 0;

    PathNode    *node;      // +0x00
    PendingCell *next;      // +0x04
    unsigned char field_8;  // +0x08  never touched

private:
    KAROO_LAYOUT_REGISTER(PendingCell);
};

KAROO_LAYOUT_CHECKS(PendingCell)
{
    KAROO_LAYOUT_AT(node, 0x00);
    KAROO_LAYOUT_AT(next, 0x04);
    KAROO_LAYOUT_SIZE(9);
}

/* The worklist's owner block, calloc(1, 9).  Only the head is ever touched:
 * the worklist is a stack. */
struct __attribute__((packed)) PendingStack {
    static const int ORIGIN = 0;

    int           field_0;  // +0x00  never touched
    PendingCell  *head;     // +0x04
    unsigned char field_8;  // +0x08  never touched

private:
    KAROO_LAYOUT_REGISTER(PendingStack);
};

KAROO_LAYOUT_CHECKS(PendingStack)
{
    KAROO_LAYOUT_AT(head, 0x04);
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
