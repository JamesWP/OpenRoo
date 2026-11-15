/* FoePath: the foe pathfinder, a best-first (A*-shaped) search over the tile
 * grid, run backward from the target to the foe.  foepath.cpp has each
 * method's PRESERVED notes. */

#pragma once

#include <deque>
#include <vector>

class LevelMap;
class Tile;

/* A search node, zeroed.  FoePath builds and links them; the chase (foe.cpp)
 * reads the result's parent and cell. */
class PathNode {
public:

    PathNode *parent() const { return parent_; }
    int       u() const      { return u_; }
    int       v() const      { return v_; }

private:
    friend class FoePath;  // builds, links and frees them

    // A node is expanded once, relaxing its four neighbours, so it has at
    // most four children.  (The game's scan had no bound, a ninth child
    // landing on next; it never had the ninth.)
    void recordChild(PathNode *node)
    {
        for (PathNode *&c : children_)
            if (c == 0) {
                c = node;
                return;
            }
    }

    int       f_;            // g + h, the open list's order
    int       h_;            // the squared distance to the goal
    int       g_;            // steps from the seed
    int       u_;
    int       v_;
    int       key_;          // FoePath::cellKey(u, v)
    PathNode *parent_;
    PathNode *children_[4];  // the nodes relaxed through this one
    PathNode *next_;         // the open or closed chain
    char      list_;         // which chain it is on: 0 none, OPEN or CLOSED
};

/* One search, made for the call that needs it and gone with it: a foe keeps
 * nothing of its searches but the mode it searches in.  Its nodes live in
 * a pool of its own, freed when it goes out of scope, and are found by
 * their cell through an index rather than by walking the chains. */
class FoePath {
public:
    // A search of map, in the foe's mode (the foe type, or 0 or 2 for the
    // switch runner; passable() branches on it), giving up after cap nodes.
    FoePath(const LevelMap &map, unsigned char mode, unsigned short cap)
        : map_(&map), mode_(mode), cap_(cap) {}
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
    PathNode *newNode();
    int       search(int uFoe, int vFoe, int uTarget, int vTarget);
    PathNode *popBestOpen();
    void      expand(PathNode *node, int goalU, int goalV);
    void      relax(PathNode *parent, int u, int v,
                    int goalU, int goalV);
    PathNode *findOpen(int key);
    PathNode *findClosed(int key);
    PathNode *find(int key, char list);
    void      insertOpenByCost(PathNode *node);
    void      propagate(PathNode *node);
    static PathNode *findByKey(PathNode *hdr, int key);
    enum : char { OPEN = 1, CLOSED = 2 };

    const LevelMap *map_;
    unsigned char   mode_;
    unsigned short  cap_;       // the iteration budget, from the chase's speed
    PathNode       *open_{};    // a header node
    PathNode       *closed_{};  // likewise
    PathNode       *result_{};
    // The cost-propagation worklist, used as a stack.
    std::vector<PathNode *> pending_;
    std::deque<PathNode>    nodes_;   // every node, the headers too
    // The node made for each cell, by its key, while the key is one of the
    // map's cells; null for none.
    std::vector<PathNode *> byKey_;
};
