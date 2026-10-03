/* FoePath: the foe pathfinder's search over the tile grid.  The search runs
 * backward -- seeded at the target cell, it stops on reaching the foe's own
 * cell -- so the caller takes the result node's parent as the foe's next step
 * toward the target.  h is a squared distance, not a true one, so the
 * heuristic is inadmissible; PropagateImprovedPathCosts exists to repair the
 * closed-list reopenings that produces.  Each function below is a FoePath
 * method (or a free helper it calls) followed by its export shim. */

#include "portable.h"
#include <stdint.h>
#include "sysdev.h"
#include <stdlib.h>
#include <new>
#include "logger.h"
#include "foepath.h"
#include "tile.h"
#include "levelmap.h"
#include "entitymath.h"

/* KAROO_SIM_FX flags for this search, read by value, never by presence:
 *   blindfoe     every cell reports impassable
 *   keyclash     node keys drop the column, breaking key injectivity
 *   popsecond    the open list pops the second-best node instead of the best
 *   nolookup     the open/closed lookups always report "not present"
 *   shortsearch  the iteration cap is forced to one
 *   fwdsearch    the search runs forward, target to foe, instead of backward
 *   revexpand    neighbours are expanded in reverse order
 *   freestep     every step between cells is declared legal
 *   truedist     the heuristic is a true distance, not squared
 *   facingramp   ramp step codes use the movement facing pairing instead
 *   nocostfix    cost re-propagation is skipped entirely
 *   nopropagate  cost improvements are found but never cascade to descendants
 *   nosort       the open list is pushed at the front, not cost-ordered
 * KAROO_FOEPATH_DIAG=1 logs a running count of the cost-repropagation work
 * (cascades, rewritten children, worklist pushes/pops, deepest drain). */
static int fx_is(const char *mode)
{
    char buf[64];
    uint32_t n = sysdev::getEnv("KAROO_SIM_FX", buf, sizeof(buf));
    return (n > 0 && n < sizeof(buf) && lstrcmpiA(buf, mode) == 0) ? 1 : 0;
}

static int fx_blindfoe(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("blindfoe");
        if (cached)
            g_logger.write("foepath: KAROO_SIM_FX=blindfoe -- every cell reports impassable\n");
    }
    return cached;
}

static int fx_keyclash(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("keyclash");
        if (cached)
            g_logger.write("foepath: KAROO_SIM_FX=keyclash -- node keys drop the column\n");
    }
    return cached;
}

static int fx_popsecond(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("popsecond");
        if (cached)
            g_logger.write("foepath: KAROO_SIM_FX=popsecond -- expanding the second-best node\n");
    }
    return cached;
}

static int fx_nolookup(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("nolookup");
        if (cached)
            g_logger.write("foepath: KAROO_SIM_FX=nolookup -- both list lookups report absent\n");
    }
    return cached;
}

struct FoePathDiag {
    unsigned cascades, reparent, pushes, pops, deepest, cur_drain;
};
static FoePathDiag g_diag;

static int diag_on(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[64];
        uint32_t n = sysdev::getEnv("KAROO_FOEPATH_DIAG", buf, sizeof(buf));
        cached = (n > 0 && n < sizeof(buf) && buf[0] != '0') ? 1 : 0;
    }
    return cached;
}

static void diag_report(void)
{
    if (!diag_on())
        return;

    const unsigned c = g_diag.cascades;
    if (c != 1 && c != 10 && c != 100 && c != 1000 && c != 10000)
        return;

    g_logger.write("foepath diag: cascades=%u reparent=%u pushes=%u pops=%u deepest=%u\n",
              g_diag.cascades, g_diag.reparent, g_diag.pushes,
              g_diag.pops, g_diag.deepest);
}

static int fx_shortsearch(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("shortsearch");
        if (cached)
            g_logger.write("foepath: KAROO_SIM_FX=shortsearch -- one expansion per search\n");
    }
    return cached;
}

static int fx_fwdsearch(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("fwdsearch");
        if (cached)
            g_logger.write("foepath: KAROO_SIM_FX=fwdsearch -- endpoints swapped, search runs forward\n");
    }
    return cached;
}

static int fx_revexpand(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("revexpand");
        if (cached)
            g_logger.write("foepath: KAROO_SIM_FX=revexpand -- neighbours expanded in reverse\n");
    }
    return cached;
}

static int fx_freestep(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("freestep");
        if (cached)
            g_logger.write("foepath: KAROO_SIM_FX=freestep -- every cell step reports legal\n");
    }
    return cached;
}

static int fx_truedist(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("truedist");
        if (cached)
            g_logger.write("foepath: KAROO_SIM_FX=truedist -- admissible heuristic, not squared\n");
    }
    return cached;
}

static int fx_facingramp(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("facingramp");
        if (cached)
            g_logger.write("foepath: KAROO_SIM_FX=facingramp -- ramp codes use the facing pairing\n");
    }
    return cached;
}

static int fx_nocostfix(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("nocostfix");
        if (cached)
            g_logger.write("foepath: KAROO_SIM_FX=nocostfix -- no cost re-propagation at all\n");
    }
    return cached;
}

static int fx_nopropagate(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("nopropagate");
        if (cached)
            g_logger.write("foepath: KAROO_SIM_FX=nopropagate -- cost improvements do not cascade\n");
    }
    return cached;
}

static int fx_nosort(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("nosort");
        if (cached)
            g_logger.write("foepath: KAROO_SIM_FX=nosort -- open list pushed at the front\n");
    }
    return cached;
}

int FoePath::passable(int u, int v)
{
    if (fx_blindfoe())
        return 0;

    const Tile *t = map()->tile( u, v);
    const unsigned char kind = t->objectMarker();

    if (kind == TILE_EMPTY && t->slideTrack() == 0)  // a void cell is passable only when something bridges it
        return 0;
    if (kind == TILE_IMPASSABLE)
        return 0;
    if (kind == TILE_DESTRUCTIBLE && t->busy() == 0)
        return 0;

    const unsigned char mode = mode_;

    if (mode == 7) {
        const unsigned char occupant = t->occupant();
        if (occupant == 4 || occupant == 3)
            return 0;
    }

    if (mode == 2 && kind == TILE_SWITCH &&
        t->busy() == 0 && t->occupant() != 4)
        return 0;

    return 1;
}

int FoePath::cellKey(int u, int v)
{
    const int stride = keyStride_;

    if (fx_keyclash())
        return stride * v;

    return stride * v + u;
}

/* The open list stays in f order because insertOpenByCost inserts by cost, so
 * taking the front here is taking the cheapest node; no scan is needed. */
PathNode *FoePath::popBestOpen()
{
    PathNode *open = open_;
    PathNode *head = open->next_;
    if (head == 0)
        return 0;

    if (fx_popsecond()) {
        PathNode *second = head->next_;
        if (second != 0) {
            open = head;
            head = second;
        }
    }

    open->next_ = head->next_;

    head->next_    = closed_->next_;
    closed_->next_ = head;

    return head;
}

void FoePath::releaseLists()
{
    static int reported = 0;
    int freed = 0;

    for (int i = 0; i < 2; ++i) {
        PathNode *&hdr = (i == 0) ? open_ : closed_;
        if (hdr == 0)
            continue;

        PathNode *n = hdr->next_;
        while (n != 0) {
            PathNode *p = n;
            n = n->next_;  // next must be read before p is freed
            delete p;
            ++freed;
        }
        delete hdr;
        hdr = 0;
    }

    // Logged in two stages because the first call is always empty: it runs
    // before any search has allocated a node, so a single log line would only
    // prove the function is reached, not that it ever frees anything.
    if (reported == 0) {
        reported = 1;
        g_logger.write("foepath: first ReleasePathSearchNodeLists -- %d node(s) freed\n", freed);
    }
    if (reported == 1 && freed > 0) {
        reported = 2;
        g_logger.write("foepath: first non-empty release -- %d node(s) freed\n", freed);
    }
}

void Sim_ReleasePathSearchNodeLists(FoePath *self)
{
    self->releaseLists();
}

FoePath *FoePath::create(LevelMap *map, unsigned short field04)
{
    FoePath *p = (FoePath *)::operator new(sizeof(FoePath), std::nothrow);
    if (p == 0)
        return 0;
    p->populate(map, field04);
    return p;
}

void FoePath::destroy(FoePath *p)
{
    p->dispose();
    ::operator delete(p);
}

void FoePath::populate(LevelMap *map, unsigned short field04)
{
    map_       = map;
    field_04   = field04;
    keyStride_ = (int)map->extentU();
    extentV_   = (int)map->extentV();
    pending_   = new PendingStack();
    found_     = 0;
    open_      = 0;
    closed_    = 0;
    result_    = 0;
}

void FoePath::dispose()
{
    releaseLists();
    delete pending_;
    pending_ = 0;
}

PathNode *FoePath::findByKey(PathNode *hdr, int key)
{
    PathNode *n = hdr->next_;

    while (n != 0) {
        if (n->key_ == key)
            return n;
        n = n->next_;
    }
    return 0;
}

PathNode *FoePath::findOpen(int key)
{
    if (fx_nolookup())
        return 0;
    return findByKey(open_, key);
}

PathNode *FoePath::findClosed(int key)
{
    if (fx_nolookup())
        return 0;
    return findByKey(closed_, key);
}

/* Inserts by ascending f, which is what lets popBestOpen just take the front.
 * A tie is inserted BEFORE the equal-cost nodes already queued, so of several
 * equally short paths this decides which one a foe actually walks. */
void FoePath::insertOpenByCost(PathNode *n)
{
    PathNode *hdr = open_;

    PathNode *cur = hdr->next_;
    if (cur == 0) {
        hdr->next_ = n;
        return;
    }

    const int f = n->f_;
    PathNode *prev = hdr;

    while (cur != 0 && cur->f_ < f) {
        prev = cur;
        cur = cur->next_;
    }

    if (fx_nosort()) {
        n->next_   = hdr->next_;
        hdr->next_ = n;
        return;
    }

    n->next_    = cur;
    prev->next_ = n;
}

void FoePath::pushPending(PathNode *node)
{
    if (fx_nopropagate())
        return;

    ++g_diag.pushes;

    PendingCell *cell = new PendingCell();

    cell->node_ = node;

    cell->next_         = pending_->head_;
    pending_->head_     = cell;
}

PathNode *FoePath::popPending()
{
    PendingStack *owner = pending_;
    PendingCell  *cell  = owner->head_;

    ++g_diag.pops;

    PathNode *node = cell->node_;
    owner->head_ = cell->next_;

    delete cell;
    return node;
}

void FoePath::propagate(PathNode *p)
{
    if (fx_nocostfix())
        return;

    ++g_diag.cascades;
    g_diag.cur_drain = 0;

    const int gp = p->g_;
    for (int i = 0; i < 8; ++i) {
        PathNode *c = p->children_[i];
        if (c == 0)
            break;
        const int gnew = gp + 1;
        if (gnew < c->g_) {
            c->g_      = gnew;
            c->f_      = c->h_ + gnew;
            c->parent_ = p;
            ++g_diag.reparent;
            pushPending(c);
        }
    }

    PendingStack *owner = pending_;
    if (owner->head_ == 0) {
        diag_report();
        return;
    }

    do {
        PathNode *q = popPending();
        if (++g_diag.cur_drain > g_diag.deepest)
            g_diag.deepest = g_diag.cur_drain;

        for (int i = 0; i < 8; ++i) {
            PathNode *c = q->children_[i];
            if (c == 0)
                break;
            const int gnew = q->g_ + 1;
            if (gnew < c->g_) {
                c->g_      = gnew;
                c->f_      = c->h_ + gnew;
                c->parent_ = q;
                ++g_diag.reparent;
                pushPending(c);
            }
        }

        owner = pending_;
    } while (owner->head_ != 0);

    diag_report();
}

void FoePath::relax(PathNode *p, int u, int v, int goalU, int goalV)
{
    const int gnew = p->g_ + 1;
    const int key = cellKey(u, v);

    // PRESERVED: recordChild's open-coded scan has no bound, so a parent that
    // accumulates a ninth distinct child across repeated relaxes overwrites
    // its own list-chain pointer instead.  Exercised only by relax(), never
    // guarded.
    PathNode *n = findOpen(key);
    if (n != 0) {
        p->recordChild(n);
        if (gnew < n->g_) {
            n->g_      = gnew;
            n->f_      = n->h_ + gnew;
            n->parent_ = p;
        }
        return;
    }

    n = findClosed(key);
    if (n != 0) {
        p->recordChild(n);
        if (gnew < n->g_) {
            n->parent_ = p;
            n->f_      = n->h_ + gnew;
            n->g_      = gnew;
            propagate(n);
        }
        return;
    }

    n = new PathNode();

    const int du = u - goalU;
    const int dv = v - goalV;
    int h = dv * dv + du * du;

    if (fx_truedist())
        h = (du < 0 ? -du : du) + (dv < 0 ? -dv : dv);

    n->parent_ = p;
    n->g_      = gnew;
    n->h_      = h;
    n->key_    = key;
    n->f_      = h + gnew;
    n->u_      = u;
    n->v_      = v;

    insertOpenByCost(n);

    p->recordChild(n);  // recorded last here, unlike the two branches above
}

/* GetCellStepDirectionCode's axis pairing does not match the movement facing
 * table in worldstate.h (+v and +u are swapped between them).  That is not a
 * bug in either: this result is compared against a ramp-orientation code, a
 * different enumeration from entity facing, and reconciling the two would
 * misclassify every ramp. */
int
Sim_GetCellStepDirectionCode(unsigned char u_from, unsigned char v_from,
                             unsigned char u_to,   unsigned char v_to)
{
    if (fx_facingramp()) {
        if (v_from < v_to)
            return 3;
        if (u_from < u_to)
            return 2;
        if (v_from > v_to)
            return 1;
        return (u_to < u_from) ? 4 : 0;
    }

    if (v_from < v_to)
        return 1;
    if (u_from < u_to)
        return 4;
    if (v_from > v_to)
        return 3;
    return (u_to < u_from) ? 2 : 0;
}

/* Answers whether an entity may step from one cell to the adjacent one.  The
 * clauses below are a SEQUENCE OF ASSIGNMENTS to one verdict, not independent
 * tests ORed together: a later clause can overwrite an earlier "legal" back to
 * "illegal" (the bridge and jump-pad blocks both do, on their failure path),
 * so reordering them changes the answer. */
int
Sim_CheckCellStepIsLegal(LevelMap *base, unsigned char u_from, unsigned char v_from,
                         unsigned char u_to, unsigned char v_to)
{

    if (fx_freestep())
        return 1;

    const Tile *to   = base->tile( u_to,   v_to);
    const Tile *from = base->tile( u_from, v_from);

    int flag = 0;

    if (Sim_CheckTileIsRamp(to->objectMarker())) {
        const int code = Sim_GetCellStepDirectionCode(u_from, v_from, u_to, v_to);
        const unsigned char k = to->objectMarker();
        if ((int)(k & 0xff) - 4 == code ||
            Sim_GetTurnedDirection((unsigned char)(k - 4), 2) == code)
            flag = 1;
    }

    if (from->objectMarker() != TILE_CLIMB && from->height() == to->height())
        flag = 1;
    else if (to->objectMarker() == TILE_LIFT &&
             ((unsigned char)to->liftBottom() == from->height() ||
              (unsigned char)to->liftTop() == from->height()))
        flag = 1;
    else if (from->objectMarker() == TILE_LIFT &&
             ((unsigned char)from->liftBottom() == to->height() ||
              (unsigned char)from->liftTop() == to->height()))
        flag = 1;

    if (Sim_CheckTileIsRamp(to->objectMarker())) {
        const int code = Sim_GetCellStepDirectionCode(u_from, v_from, u_to, v_to);
        const unsigned char k = to->objectMarker();
        if ((int)(k & 0xff) - 4 == code ||
            Sim_GetTurnedDirection((unsigned char)(k - 4), 2) == code) {
            int ok = 1;
            if (Sim_CheckTileIsRamp(from->objectMarker()) &&
                code != (int)from->objectMarker())
                ok = 0;
            if (ok && (int)to->height() == (int)from->height() - 1)  // Int arithmetic: a height of 0 matches nothing.
                flag = 1;
        }
    } else {
        const int d = (int)to->height() - (int)from->height();
        if (from->objectMarker() != TILE_CLIMB && d < 3 && d > 0)
            flag = (from->slideTrack() == 0);
    }

    if (from->objectMarker() == TILE_CLIMB && from->height() == to->height()) {
        const unsigned char dir = from->climbDir();
        if (v_from < v_to && dir == 1)
            flag = 1;
        else if (u_from < u_to && dir == 4)
            flag = 1;
        else if (v_from > v_to && dir == 3)
            flag = 1;
        else if (u_from > u_to && dir == 2)
            flag = 1;
        else
            flag = 0;  // overwrites whatever the ramp/height clauses above decided
    }

    if (to->objectMarker() == TILE_CLIMB)
        flag = 1;

    if (from->objectMarker() == TILE_JUMP_PAD) {
        const unsigned char lvl = from->field1f1();
        const Tile *nu_pos = base->tile( u_from + 1, v_from);
        const Tile *nu_neg = base->tile( u_from - 1, v_from);
        const Tile *nv_pos = base->tile( u_from, v_from + 1);
        const Tile *nv_neg = base->tile( u_from, v_from - 1);

        if (u_from > u_to && lvl == nu_pos->height())
            flag = 1;
        else if (u_from < u_to && lvl == nu_neg->height())
            flag = 1;
        else if (v_from > v_to && lvl == nv_pos->height())
            flag = 1;
        else if (v_from < v_to && lvl == nv_neg->height())
            flag = 1;
        else
            flag = 0;  // overwrites whatever was decided above
    }

    if (to->objectMarker() == TILE_JUMP_PAD)
        flag = 1;

    if (from->objectMarker() == TILE_GLUE && from->occupant() == 0) {
        const signed char du = (signed char)(u_from - u_to);
        const signed char dv = (signed char)(v_from - v_to);
        const int up = (int)u_to + (int)du * 2;
        const int vp = (int)v_to + (int)dv * 2;
        return base->tile( up, vp)->occupant() == 4;
    }

    return flag;
}

void FoePath::expand(PathNode *n, int goalU, int goalV)
{
    struct { int du, dv; } step[4] = { { 0, -1 }, { +1, 0 }, { 0, +1 }, { -1, 0 } };

    if (fx_revexpand()) {
        for (int i = 0; i < 2; ++i) {
            const int du = step[i].du, dv = step[i].dv;
            step[i].du = step[3 - i].du;  step[i].dv = step[3 - i].dv;
            step[3 - i].du = du;          step[3 - i].dv = dv;
        }
    }

    for (int i = 0; i < 4; ++i) {
        const int u = n->u_;
        const int v = n->v_;
        const int nu = u + step[i].du;
        const int nv = v + step[i].dv;

        if (!passable(nu, nv))
            continue;
        if (!Sim_CheckCellStepIsLegal(map(), (unsigned char)u, (unsigned char)v,
                                      (unsigned char)nu, (unsigned char)nv))
            continue;

        relax(n, nu, nv, goalU, goalV);
    }
}

int FoePath::search(int uFoe, int vFoe, int uTarget, int vTarget)
{
    targetV_ = (unsigned char)vTarget;
    foeV_    = (unsigned char)vFoe;
    targetU_ = (unsigned char)uTarget;
    foeU_    = (unsigned char)uFoe;

    const int goalKey = cellKey(uFoe, vFoe);

    // These two headers, and the seed node below, are new on every search;
    // releaseLists frees the headers and the nodes chained off them.
    open_   = new PathNode();
    closed_ = new PathNode();

    PathNode *seed = new PathNode();

    const int du = uTarget - uFoe;
    const int dv = vTarget - vFoe;
    const int h = du * du + dv * dv;

    seed->g_   = 0;
    seed->h_   = h;
    seed->f_   = h;
    seed->key_ = cellKey(uTarget, vTarget);
    seed->u_   = uTarget;
    seed->v_   = vTarget;

    open_->next_ = seed;

    unsigned short cap = cap_;

    if (fx_shortsearch() && cap > 1)
        cap = 1;

    int iter = 0;
    PathNode *node = 0;

    if (cap > 0) {
        for (;;) {
            node = popBestOpen();
            if (node == 0)
                return 0;
            if (node->key_ == goalKey)
                break;

            expand(node, uFoe, vFoe);

            if (++iter >= (int)cap)
                break;
        }
    }

    if (iter < (int)cap) {
        result_ = node;
        return 1;
    }
    return 0;
}

int FoePath::find(int uFoe, int vFoe, int uTarget, int vTarget)
{
    if (passable(uTarget, vTarget) != 0 &&
        passable(uFoe, vFoe) != 0) {

        // Compares node KEYS, not coordinates, to detect "already there".
        // Since cellKey's stride is a per-search value rather than the tile
        // stride, two genuinely different cells would collide under a wrong
        // stride and this would report "no path" instead of searching --
        // another reason that stride must stay as read from the map.
        const int keyFoe    = cellKey(uFoe, vFoe);
        const int keyTarget = cellKey(uTarget, vTarget);

        if (keyFoe != keyTarget) {
            releaseLists();

            const int ok = fx_fwdsearch()
                ? search(uTarget, vTarget, uFoe, vFoe)
                : search(uFoe, vFoe, uTarget, vTarget);
            if (ok != 0) {
                found_ = 1;
                return 1;
            }
        }
    }

    found_ = 0;
    return 0;
}

