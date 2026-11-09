/* FoePath: the foe pathfinder, a best-first (A*-shaped) search over the tile
 * grid, run backward from the target to the foe.  foepath.cpp has each
 * method's PRESERVED notes. */

#pragma once

#include <vector>

class LevelMap;
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

    int       f_;            // g + h, the open list's order
    int       h_;            // the squared distance to the goal
    int       g_;            // steps from the seed
    int       u_;
    int       v_;
    int       key_;          // FoePath::cellKey(u, v)
    PathNode *parent_;
    PathNode *children_[8];  // the nodes relaxed through this one
    PathNode *next_;         // the open or closed chain
};

/* One search, made for the call that needs it and gone with it: a foe keeps
 * nothing of its searches but the mode it searches in.  Its nodes are freed
 * when it goes out of scope. */
class FoePath {
public:
    // A search of map, in the foe's mode (the foe type, or 0 or 2 for the
    // switch runner; passable() branches on it), giving up after cap nodes.
    FoePath(const LevelMap &map, unsigned char mode, unsigned short cap)
        : map_(&map), mode_(mode), cap_(cap) {}
    ~FoePath() { releaseLists(); }
    FoePath(const FoePath &) = delete;
    FoePath &operator=(const FoePath &) = delete;

    // Searches from the target back to the foe; true if it got there.  Once.
    bool find(int uFoe, int vFoe, int uTarget, int vTarget);
    // After a find that got there: the foe's own cell, whose parents lead on
    // to the target -- the first parent is the foe's next step.
    const PathNode *result() const { return result_; }

private:
    const LevelMap *map() const { return map_; }
    int       cellKey(int u, int v) const;
    int       passable(int u, int v) const;
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
    static PathNode *findByKey(PathNode *hdr, int key);

    const LevelMap *map_;
    unsigned char   mode_;
    unsigned short  cap_;       // the iteration budget, from the chase's speed
    PathNode       *open_{};    // a header node
    PathNode       *closed_{};  // likewise
    PathNode       *result_{};
    // The cost-propagation worklist, used as a stack.
    std::vector<PathNode *> pending_;
};
