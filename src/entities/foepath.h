/* FoePath -- the foe pathfinder, a best-first (A*-shaped) search over the
 * tile grid.  One per foe, hung off foe+0x13b; 0x35 bytes, allocated by
 * AttachFoePathfinderToEntity 0x43a970 with operator_new and constructed by
 * PopulateFoePathSearchContext 0x401bb0 (both still the game's, so the
 * object's two ends stay on the game's heap).
 *
 * The search's methods are ours (foepath.cpp); every one is a one-line
 * export shim over a method below.  The layout is packed and asserted
 * against the ctor's stores and the listings; see foepath.cpp for each
 * method's exactness notes.
 */
#pragma once

#include "layout.h"

/* A search node: calloc(1, 0x44) from the game's heap, freed with
 * FactAlloc::Free.  A plain record, public: FoePath builds and links them,
 * and the chase (foe.cpp) reads the result's parent and cell. */
struct __attribute__((packed)) PathNode {
    static const int ORIGIN = 0;

    int       f;            /* +0x00  g + h, the open list's order         */
    int       h;            /* +0x04  SQUARED distance to the goal         */
    int       g;            /* +0x08  steps from the seed                  */
    int       field_0c;     /* +0x0c  never touched                        */
    int       u;            /* +0x10                                       */
    int       v;            /* +0x14                                       */
    int       key;          /* +0x18  FoePath::cellKey(u, v)               */
    PathNode *parent;       /* +0x1c                                       */
    PathNode *children[8];  /* +0x20  the nodes relaxed through this one   */
    PathNode *next;         /* +0x40  the open / closed chain              */

    /* The open-coded child scan, with NO bounds check (RelaxPathNeighbour
     * Cell): a ninth child is stored one past the array -- onto `next`, at
     * +0x40.  Written as that store, so the defect is kept without an
     * out-of-bounds access. */
    void recordChild(PathNode *node)
    {
        int i = 0;
        while (children[i] != 0 && ++i < 8)
            ;
        if (i == 8)
            next = node;            /* children[8] IS next */
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
    /* calloc(1, 0x44). */
    KAROO_LAYOUT_SIZE(0x44);
}

/* One cell of the cost-propagation worklist: calloc(1, 9) -- NINE bytes
 * for two pointers; the ninth is never touched. */
struct __attribute__((packed)) PendingCell {
    static const int ORIGIN = 0;

    PathNode    *node;      /* +0x00 */
    PendingCell *next;      /* +0x04 */
    unsigned char field_8;  /* +0x08  never touched */

private:
    KAROO_LAYOUT_REGISTER(PendingCell);
};

KAROO_LAYOUT_CHECKS(PendingCell)
{
    KAROO_LAYOUT_AT(node, 0x00);
    KAROO_LAYOUT_AT(next, 0x04);
    KAROO_LAYOUT_SIZE(9);
}

/* The worklist's owner block, calloc(1, 9) by the FoePath ctor.  Only the
 * head at +4 is ever touched: the worklist is a STACK. */
struct __attribute__((packed)) PendingStack {
    static const int ORIGIN = 0;

    int           field_0;  /* +0x00  never touched */
    PendingCell  *head;     /* +0x04 */
    unsigned char field_8;  /* +0x08  never touched */

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

    /* ── the chase's side (Foe::chase, foe.cpp) ─────────────────────── */
    /* +0x2f: the search's iteration budget, from the chase's `speed`. */
    void setCap(unsigned short n)                  { cap_ = n; }
    /* +0x31/+0x32, written by the chase (and again by the search). */
    void setTarget(unsigned char u, unsigned char v)
    {
        targetU_ = u;
        targetV_ = v;
    }
    /* +0x2a: the mover mode passable() branches on (the spawn stores the
     * foe type, the chase 0 or 2). */
    void setMode(unsigned char m)                  { mode_ = m; }
    /* +0x0e: the node the search stopped on -- the FOE's cell, since it
     * runs backward -- and the chase advances it to its parent. */
    PathNode *result() const                       { return result_; }
    void setResult(PathNode *n)                    { result_ = n; }

    /* ── the search (foepath.cpp) ───────────────────────────────────── */
    int       find(int uFoe, int vFoe, int uTarget, int vTarget); /* 0x401c20 */
    int       cellKey(int u, int v);                              /* 0x401cb0 */
    int       passable(int u, int v);                             /* 0x401cd0 */
    void      releaseLists();                                     /* 0x401d60 */
    int       search(int uFoe, int vFoe, int uTarget, int vTarget); /* 0x401db0 */
    PathNode *popBestOpen();                                      /* 0x401ec0 */
    void      expand(PathNode *node, int goalU, int goalV);       /* 0x401ef0 */
    void      relax(PathNode *parent, int u, int v,
                    int goalU, int goalV);                        /* 0x402000 */
    PathNode *findOpen(int key);                                  /* 0x402130 */
    PathNode *findClosed(int key);                                /* 0x402150 */
    void      insertOpenByCost(PathNode *node);                   /* 0x402170 */
    void      propagate(PathNode *node);                          /* 0x4021b0 */
    void      pushPending(PathNode *node);                        /* 0x402250 */
    PathNode *popPending();                                       /* 0x402280 */

private:
    FoePath() = delete;   /* game-constructed; only ever reached by pointer */
    static PathNode *findByKey(PathNode *hdr, int key);

    KAROO_LAYOUT_REGISTER(FoePath);

    unsigned char *tileBase_;   /* +0x00  the ctor's argument               */
    unsigned short field_04;    /* +0x04  the ctor's second argument (0)    */
    PathNode      *open_;       /* +0x06  header node, a fresh one per search */
    PathNode      *closed_;     /* +0x0a  ditto                             */
    PathNode      *result_;     /* +0x0e                                    */
    PendingStack  *pending_;    /* +0x12                                    */
    int            found_;      /* +0x16  mirrors find()'s return           */
    int            extentV_;    /* +0x1a  tile+0x19a, the map's v extent    */
    /* +0x1e  tile+0x19b, the map's u extent.  NOT the tile stride (100). */
    int            keyStride_;
    unsigned char  gap_022[0x02a - 0x022];
    unsigned char  mode_;       /* +0x2a                                    */
    unsigned char  gap_02b[0x02f - 0x02b];
    unsigned short cap_;        /* +0x2f                                    */
    unsigned char  targetU_;    /* +0x31                                    */
    unsigned char  targetV_;    /* +0x32                                    */
    unsigned char  foeU_;       /* +0x33                                    */
    unsigned char  foeV_;       /* +0x34                                    */
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
    /* AttachFoePathfinderToEntity's operator_new(0x35). */
    KAROO_LAYOUT_SIZE(0x35);
}

/* PLACEHOLDER: the FoePath destructor body 0x00401c00 (Ghidra
 * FoePath::DisposeFoePathSearchState), __fastcall/__thiscall (ECX =
 * the FoePath): ReleasePathSearchNodeLists (ours) then FactAlloc::Free of
 * its +0x12 block.  Called through, not rewritten: the still-original
 * Player dtor (0x0041FA48) calls it too.  The caller frees the FoePath
 * itself afterwards (Free2). */
inline void FoePath_Destroy(FoePath *self)
{
    typedef void (__attribute__((fastcall)) *fn)(FoePath *);
    ((fn)0x00401c00)(self);
}
