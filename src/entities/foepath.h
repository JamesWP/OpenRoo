/* FoePath: the foe pathfinder, a best-first (A*-shaped) search over the tile
 * grid, run backward from the target to the foe.  One per foe.  The object,
 * its worklist block, cells and nodes are all allocated and freed here (new
 * and delete).  foepath.cpp has each method's PRESERVED
 * notes. */

#pragma once

class Tile;

/* A search node, 0x44 bytes, zeroed.  FoePath builds and links them; the chase
 * (foe.cpp) reads the result's parent and cell. */
class PathNode {
public:
     

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
    int       u_;            // +0x10
    int       v_;            // +0x14
    int       key_;          // +0x18  FoePath::cellKey(u, v)
    PathNode *parent_;       // +0x1c
    PathNode *children_[8];  // +0x20  the nodes relaxed through this one
    PathNode *next_;         // +0x40  the open or closed chain

     
};

 

/* One cell of the cost-propagation worklist: nine bytes for two
 * pointers; the ninth is never touched. */
class PendingCell {
public:
     

private:
    friend class FoePath;  // the worklist is FoePath's

    PathNode    *node_;      // +0x00
    PendingCell *next_;      // +0x04
    unsigned char field_8_;  // +0x08  never touched

     
};

 
/* The worklist's owner block, one pointer.  Only the head is ever touched:
 * the worklist is a stack. */
class PendingStack {
public:
     

private:
    friend class FoePath;
    PendingCell  *head_;     // +0x04
};

class FoePath {
public:
     

    // Allocates (nothrow) and populates one; NULL if the allocation fails, and
    // the caller stores that.
    static FoePath *create(Tile *tileBase, unsigned short field04);
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

    Tile* tileBase() const { return tileBase_; }

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
    void populate(Tile *tileBase, unsigned short field04);
    static PathNode *findByKey(PathNode *hdr, int key);

    Tile *tileBase_;  // the ctor's argument
    unsigned short field_04;   // the ctor's second argument (0)
    PathNode      *open_;      // a header node, fresh each search
    PathNode      *closed_;    // likewise
    PathNode      *result_;
    PendingStack  *pending_;
    int            found_;     // mirrors find()'s return
    int            extentV_;   // the map's v extent
    // The map's u extent.  Not the tile stride (100).
    int            keyStride_;
    unsigned char  mode_;
    unsigned short cap_;
    unsigned char  targetU_;
    unsigned char  targetV_;
    unsigned char  foeU_;
    unsigned char  foeV_;
};
