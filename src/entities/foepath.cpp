/* FoePath: the foe pathfinder's search over the tile grid.  The search runs
 * backward -- seeded at the target cell, it stops on reaching the foe's own
 * cell -- so the caller takes the result node's parent as the foe's next step
 * toward the target.  h is a squared distance, not a true one, so the
 * heuristic is inadmissible; PropagateImprovedPathCosts exists to repair the
 * closed-list reopenings that produces.  Each function below is a FoePath
 * method (or a free helper it calls) followed by its export shim. */

#include <windows.h>
#include <stdlib.h>
#include <new>
#include "log.h"
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
    DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", buf, sizeof(buf));
    return (n > 0 && n < sizeof(buf) && lstrcmpiA(buf, mode) == 0) ? 1 : 0;
}

static int fx_blindfoe(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("blindfoe");
        if (cached)
            log_write("foepath: KAROO_SIM_FX=blindfoe -- every cell reports impassable\n");
    }
    return cached;
}

static int fx_keyclash(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("keyclash");
        if (cached)
            log_write("foepath: KAROO_SIM_FX=keyclash -- node keys drop the column\n");
    }
    return cached;
}

static int fx_popsecond(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("popsecond");
        if (cached)
            log_write("foepath: KAROO_SIM_FX=popsecond -- expanding the second-best node\n");
    }
    return cached;
}

static int fx_nolookup(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("nolookup");
        if (cached)
            log_write("foepath: KAROO_SIM_FX=nolookup -- both list lookups report absent\n");
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
        DWORD n = GetEnvironmentVariableA("KAROO_FOEPATH_DIAG", buf, sizeof(buf));
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

    log_write("foepath diag: cascades=%u reparent=%u pushes=%u pops=%u deepest=%u\n",
              g_diag.cascades, g_diag.reparent, g_diag.pushes,
              g_diag.pops, g_diag.deepest);
}

static int fx_shortsearch(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("shortsearch");
        if (cached)
            log_write("foepath: KAROO_SIM_FX=shortsearch -- one expansion per search\n");
    }
    return cached;
}

static int fx_fwdsearch(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("fwdsearch");
        if (cached)
            log_write("foepath: KAROO_SIM_FX=fwdsearch -- endpoints swapped, search runs forward\n");
    }
    return cached;
}

static int fx_revexpand(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("revexpand");
        if (cached)
            log_write("foepath: KAROO_SIM_FX=revexpand -- neighbours expanded in reverse\n");
    }
    return cached;
}

static int fx_freestep(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("freestep");
        if (cached)
            log_write("foepath: KAROO_SIM_FX=freestep -- every cell step reports legal\n");
    }
    return cached;
}

static int fx_truedist(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("truedist");
        if (cached)
            log_write("foepath: KAROO_SIM_FX=truedist -- admissible heuristic, not squared\n");
    }
    return cached;
}

static int fx_facingramp(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("facingramp");
        if (cached)
            log_write("foepath: KAROO_SIM_FX=facingramp -- ramp codes use the facing pairing\n");
    }
    return cached;
}

static int fx_nocostfix(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("nocostfix");
        if (cached)
            log_write("foepath: KAROO_SIM_FX=nocostfix -- no cost re-propagation at all\n");
    }
    return cached;
}

static int fx_nopropagate(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("nopropagate");
        if (cached)
            log_write("foepath: KAROO_SIM_FX=nopropagate -- cost improvements do not cascade\n");
    }
    return cached;
}

static int fx_nosort(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = fx_is("nosort");
        if (cached)
            log_write("foepath: KAROO_SIM_FX=nosort -- open list pushed at the front\n");
    }
    return cached;
}

int FoePath::passable(int u, int v)
{
    if (fx_blindfoe())
        return 0;

    const Tile *t = Tile::at(tileBase_, u, v);
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

extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_CheckPathCellPassable(FoePath *self, int u, int v)
{
    return self->passable(u, v);
}

int FoePath::cellKey(int u, int v)
{
    const int stride = keyStride_;

    if (fx_keyclash())
        return stride * v;

    return stride * v + u;
}

extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_ComputeCellLinearIndex(FoePath *self, int u, int v)
{
    return self->cellKey(u, v);
}

/* The open list stays in f order because insertOpenByCost inserts by cost, so
 * taking the front here is taking the cheapest node; no scan is needed. */
PathNode *FoePath::popBestOpen()
{
    PathNode *open = open_;
    PathNode *head = open->next;
    if (head == 0)
        return 0;

    if (fx_popsecond()) {
        PathNode *second = head->next;
        if (second != 0) {
            open = head;
            head = second;
        }
    }

    open->next = head->next;

    head->next    = closed_->next;
    closed_->next = head;

    return head;
}

extern "C" __declspec(dllexport) PathNode * __attribute__((thiscall))
Sim_PopBestOpenPathNode(FoePath *self)
{
    return self->popBestOpen();
}

static inline void *path_calloc(int count, int size)
{
    return calloc((size_t)count, (size_t)size);
}

void FoePath::releaseLists()
{
    static int reported = 0;
    int freed = 0;

    for (int i = 0; i < 2; ++i) {
        PathNode *hdr = (i == 0) ? open_ : closed_;
        if (hdr == 0)
            continue;

        PathNode *n = hdr->next;
        if (n == 0)
            continue;

        do {
            PathNode *p = n;
            n = n->next;  // next must be read before p is freed
            free(p);
            ++freed;
        // PRESERVED: hdr->next is left pointing at the freed chain here.
        // Harmless only because SearchPathNodeGraph callocs a fresh header for
        // every search before anything reads it; do not add the "obvious"
        // hdr->next = 0 without checking every caller still relies on that.
        } while (n != 0);

    }

    // Logged in two stages because the first call is always empty: it runs
    // before any search has allocated a node, so a single log line would only
    // prove the function is reached, not that it ever frees anything.
    if (reported == 0) {
        reported = 1;
        log_write("foepath: first ReleasePathSearchNodeLists -- %d node(s) freed\n", freed);
    }
    if (reported == 1 && freed > 0) {
        reported = 2;
        log_write("foepath: first non-empty release -- %d node(s) freed\n", freed);
    }
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_ReleasePathSearchNodeLists(FoePath *self)
{
    self->releaseLists();
}

FoePath *FoePath::create(unsigned char *tileBase, unsigned short field04)
{
    FoePath *p = (FoePath *)::operator new(sizeof(FoePath), std::nothrow);
    if (p == 0)
        return 0;
    p->populate(tileBase, field04);
    return p;
}

void FoePath::destroy(FoePath *p)
{
    p->dispose();
    ::operator delete(p);
}

void FoePath::populate(unsigned char *tileBase, unsigned short field04)
{
    const LevelMap *map = LevelMap::fromTileBase(tileBase);

    tileBase_  = tileBase;
    field_04   = field04;
    keyStride_ = (int)map->extentU();
    extentV_   = (int)map->extentV();
    pending_   = (PendingStack *)path_calloc(1, 9);
    found_     = 0;
    open_      = 0;
    closed_    = 0;
    result_    = 0;
}

void FoePath::dispose()
{
    releaseLists();
    free(pending_);
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_DisposeFoePathSearchState(FoePath *self)
{
    self->dispose();
}

PathNode *FoePath::findByKey(PathNode *hdr, int key)
{
    PathNode *n = hdr->next;

    while (n != 0) {
        if (n->key == key)
            return n;
        n = n->next;
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

extern "C" __declspec(dllexport) PathNode * __attribute__((thiscall))
Sim_FindOpenPathNodeByKey(FoePath *self, int key)
{
    return self->findOpen(key);
}

extern "C" __declspec(dllexport) PathNode * __attribute__((thiscall))
Sim_FindClosedPathNodeByKey(FoePath *self, int key)
{
    return self->findClosed(key);
}

/* Inserts by ascending f, which is what lets popBestOpen just take the front.
 * A tie is inserted BEFORE the equal-cost nodes already queued, so of several
 * equally short paths this decides which one a foe actually walks. */
void FoePath::insertOpenByCost(PathNode *n)
{
    PathNode *hdr = open_;

    PathNode *cur = hdr->next;
    if (cur == 0) {
        hdr->next = n;
        return;
    }

    const int f = n->f;
    PathNode *prev = hdr;

    while (cur != 0 && cur->f < f) {
        prev = cur;
        cur = cur->next;
    }

    if (fx_nosort()) {
        n->next   = hdr->next;
        hdr->next = n;
        return;
    }

    n->next    = cur;
    prev->next = n;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_InsertOpenPathNodeByCost(FoePath *self, PathNode *node)
{
    self->insertOpenByCost(node);
}

void FoePath::pushPending(PathNode *node)
{
    if (fx_nopropagate())
        return;

    ++g_diag.pushes;

    PendingCell *cell = (PendingCell *)path_calloc(1, 9);

    cell->node = node;

    cell->next         = pending_->head;
    pending_->head     = cell;
}

PathNode *FoePath::popPending()
{
    PendingStack *owner = pending_;
    PendingCell  *cell  = owner->head;

    ++g_diag.pops;

    PathNode *node = cell->node;
    owner->head = cell->next;

    free(cell);
    return node;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PushPendingPathNode(FoePath *self, PathNode *node)
{
    self->pushPending(node);
}

extern "C" __declspec(dllexport) PathNode * __attribute__((thiscall))
Sim_PopPendingPathNode(FoePath *self)
{
    return self->popPending();
}

void FoePath::propagate(PathNode *p)
{
    if (fx_nocostfix())
        return;

    ++g_diag.cascades;
    g_diag.cur_drain = 0;

    const int gp = p->g;
    for (int i = 0; i < 8; ++i) {
        PathNode *c = p->children[i];
        if (c == 0)
            break;
        const int gnew = gp + 1;
        if (gnew < c->g) {
            c->g      = gnew;
            c->f      = c->h + gnew;
            c->parent = p;
            ++g_diag.reparent;
            pushPending(c);
        }
    }

    PendingStack *owner = pending_;
    if (owner->head == 0) {
        diag_report();
        return;
    }

    do {
        PathNode *q = popPending();
        if (++g_diag.cur_drain > g_diag.deepest)
            g_diag.deepest = g_diag.cur_drain;

        for (int i = 0; i < 8; ++i) {
            PathNode *c = q->children[i];
            if (c == 0)
                break;
            const int gnew = q->g + 1;
            if (gnew < c->g) {
                c->g      = gnew;
                c->f      = c->h + gnew;
                c->parent = q;
                ++g_diag.reparent;
                pushPending(c);
            }
        }

        owner = pending_;
    } while (owner->head != 0);

    diag_report();
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PropagateImprovedPathCosts(FoePath *self, PathNode *node)
{
    self->propagate(node);
}

void FoePath::relax(PathNode *p, int u, int v, int goalU, int goalV)
{
    const int gnew = p->g + 1;
    const int key = cellKey(u, v);

    // PRESERVED: recordChild's open-coded scan has no bound, so a parent that
    // accumulates a ninth distinct child across repeated relaxes overwrites
    // its own list-chain pointer instead.  Exercised only by relax(), never
    // guarded.
    PathNode *n = findOpen(key);
    if (n != 0) {
        p->recordChild(n);
        if (gnew < n->g) {
            n->g      = gnew;
            n->f      = n->h + gnew;
            n->parent = p;
        }
        return;
    }

    n = findClosed(key);
    if (n != 0) {
        p->recordChild(n);
        if (gnew < n->g) {
            n->parent = p;
            n->f      = n->h + gnew;
            n->g      = gnew;
            propagate(n);
        }
        return;
    }

    n = (PathNode *)path_calloc(1, 0x44);

    const int du = u - goalU;
    const int dv = v - goalV;
    int h = dv * dv + du * du;

    if (fx_truedist())
        h = (du < 0 ? -du : du) + (dv < 0 ? -dv : dv);

    n->parent = p;
    n->g      = gnew;
    n->h      = h;
    n->key    = key;
    n->f      = h + gnew;
    n->u      = u;
    n->v      = v;

    insertOpenByCost(n);

    p->recordChild(n);  // recorded last here, unlike the two branches above
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_RelaxPathNeighbourCell(FoePath *self, PathNode *parent, int u, int v,
                           int goalU, int goalV)
{
    self->relax(parent, u, v, goalU, goalV);
}

/* GetCellStepDirectionCode's axis pairing does not match the movement facing
 * table in worldstate.h (+v and +u are swapped between them).  That is not a
 * bug in either: this result is compared against a ramp-orientation code, a
 * different enumeration from entity facing, and reconciling the two would
 * misclassify every ramp. */
extern "C" __declspec(dllexport) int __attribute__((stdcall))
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
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_CheckCellStepIsLegal(unsigned char *base, unsigned char u_from, unsigned char v_from,
                         unsigned char u_to, unsigned char v_to)
{

    if (fx_freestep())
        return 1;

    const Tile *to   = Tile::at(base, u_to,   v_to);
    const Tile *from = Tile::at(base, u_from, v_from);

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
        const Tile *nu_pos = Tile::at(base, u_from + 1, v_from);
        const Tile *nu_neg = Tile::at(base, u_from - 1, v_from);
        const Tile *nv_pos = Tile::at(base, u_from, v_from + 1);
        const Tile *nv_neg = Tile::at(base, u_from, v_from - 1);

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
        return Tile::at(base, up, vp)->occupant() == 4;
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
        const int u = n->u;
        const int v = n->v;
        const int nu = u + step[i].du;
        const int nv = v + step[i].dv;

        if (!passable(nu, nv))
            continue;
        if (!Sim_CheckCellStepIsLegal(tileBase_, (unsigned char)u, (unsigned char)v,
                                      (unsigned char)nu, (unsigned char)nv))
            continue;

        relax(n, nu, nv, goalU, goalV);
    }
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_ExpandPathNodeNeighbours(FoePath *self, PathNode *node, int goalU, int goalV)
{
    self->expand(node, goalU, goalV);
}

int FoePath::search(int uFoe, int vFoe, int uTarget, int vTarget)
{
    targetV_ = (unsigned char)vTarget;
    foeV_    = (unsigned char)vFoe;
    targetU_ = (unsigned char)uTarget;
    foeU_    = (unsigned char)uFoe;

    const int goalKey = cellKey(uFoe, vFoe);

    // PRESERVED: these two headers, and the seed node below, are calloc'd
    // fresh on every search and never reused; releaseLists frees only the
    // nodes chained off a header, not the header itself, so each search leaks
    // two small blocks.
    open_   = (PathNode *)path_calloc(1, 0x44);
    closed_ = (PathNode *)path_calloc(1, 0x44);

    PathNode *seed = (PathNode *)path_calloc(1, 0x44);

    const int du = uTarget - uFoe;
    const int dv = vTarget - vFoe;
    const int h = du * du + dv * dv;

    seed->g   = 0;
    seed->h   = h;
    seed->f   = h;
    seed->key = cellKey(uTarget, vTarget);
    seed->u   = uTarget;
    seed->v   = vTarget;

    open_->next = seed;

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
            if (node->key == goalKey)
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

extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_SearchPathNodeGraph(FoePath *self, int uFoe, int vFoe, int uTarget, int vTarget)
{
    return self->search(uFoe, vFoe, uTarget, vTarget);
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

extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_FindFoePathBetweenCells(FoePath *self, int uFoe, int vFoe,
                            int uTarget, int vTarget)
{
    return self->find(uFoe, vFoe, uTarget, vTarget);
}
