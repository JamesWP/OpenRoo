/* GAMETICK_PLAN.md Band A — the foe pathfinder's walkability test.
 *
 * ─── Why this file exists ────────────────────────────────────────────────
 *
 * The plan lists SetFoeChaseTarget 0x0043a9d0 as a Band A entity tick with
 * callees "GetTurnedDirection, FUN_00401c20".  GetTurnedDirection is ours
 * (entitymath.cpp).  FUN_00401c20 was not, and reading its callee list —
 * the standing rule this plan already records twice — turned it from a
 * detail into a sub-band: it is the entry point of a real best-first search
 * over the tile grid, with five helpers of its own.
 *
 * Named in Ghidra this cycle, and the shape is unmistakable A*:
 *
 *   FindFoePathBetweenCells    0x401c20  entry: validate ends, then search
 *   ComputeCellLinearIndex     0x401cb0  cell -> node key (this+0x1e stride)
 *   CheckPathCellPassable      0x401cd0  is this cell walkable?        LEAF
 *   ReleasePathSearchNodeLists 0x401d60  free both node lists (CRT Free)
 *   SearchPathNodeGraph        0x401db0  the search loop
 *   PopBestOpenPathNode        0x401ec0  open list -> closed list
 *   ExpandPathNodeNeighbours   0x401ef0  the four-neighbour expansion
 *   RelaxPathNeighbourCell     0x402000  g/f update, parent rewiring
 *
 * Open list at this+0x06, closed list at this+0x0a, 0x44-byte nodes chained
 * through node+0x40, with f at [0], h at [1], g at [2], cell key at [6],
 * u at [4], v at [5] and parent at [7].  `this` is not the foe: it is the
 * pathfinder object hanging off foe+0x13b, whose +0x00 is the tile base and
 * whose +0x2a is the mover mode SetFoeChaseTarget writes before searching.
 *
 * CheckPathCellPassable is the only true leaf of the eight — it calls
 * nothing at all — so it is this cycle, and the rest of the cluster follows
 * one per cycle behind it.  Six E8 call sites, all *inside* the cluster
 * (two in FindFoePathBetweenCells, four in ExpandPathNodeNeighbours); no
 * DATA reference, no `68 imm32`, and no absolute-address call from our own
 * DLL.  That the call sites live in functions still owned by the game is
 * fine, and is exactly what CALL_PATCHES is for.
 *
 * ─── The rule it implements ──────────────────────────────────────────────
 *
 * Read from the listing at 0x00401cd0, not from the decompile.  A cell is
 * blocked when any of these holds, and walkable otherwise:
 *
 *   kind == 0x00 and blocker(+0x1bc) == 0    void with nothing bridging it
 *   kind == 0x16
 *   kind == 0x17 and spent(+0x217) == 0
 *   mode == 7    and occupant(+0x1a5) is 3 or 4
 *   mode == 2    and kind == 0x11 and spent == 0 and occupant != 4
 *
 * Four exactness points, all from the listing:
 *
 * 1. EVERY COMPARE IS 8-BIT EQUALITY (CMP DL,0x16 and friends, TEST DL,DL),
 *    so signedness never enters into it — unlike UpdateBombFuseAndBlast's
 *    height compare, which mixes MOVSX and MOVZX and where it does.  The
 *    fields are read as unsigned char here to make that explicit.
 *
 * 2. THE RETURN IS A FULL 32-BIT 0/1 — `MOV EAX,1` / `XOR EAX,EAX`, not a
 *    partial AL.  So unlike UpdateEntityMovement there is no garbage-byte
 *    deviation to document: `int` is exact.
 *
 * 3. THE INDEX IS THE FAMILIAR ONE.  LEA/LEA/LEA then SHL 7 minus itself is
 *    (v + u*100) * 0x7f, the same addressing entitymove.cpp uses and the
 *    same 0x7f tile stride; both axes are plain ints.  Note that the *node
 *    key* helper ComputeCellLinearIndex uses a different stride (this+0x1e).
 *    The two are not the same number and must not be merged.
 *
 * 4. THE FIELD READS ARE LAZY IN THE ORIGINAL.  blocker(+0x1bc) is loaded
 *    only when kind == 0, and spent(+0x217) only on the two branches that
 *    test it.  This transcription keeps that order rather than hoisting the
 *    loads, so an out-of-range cell touches exactly the bytes the original
 *    touched.
 */
#include <windows.h>
#include "log.h"
#include "alloc.h"

/* Leaves this file stands on, both already ours (entitymath.cpp). */
extern "C" __declspec(dllexport) int __attribute__((stdcall))
Sim_CheckTileIsRamp(unsigned char kind);
extern "C" __declspec(dllexport) unsigned char __attribute__((stdcall))
Sim_GetTurnedDirection(unsigned char dir, unsigned char delta);

/* Tile addressing, matching entitymove.cpp's TILE() exactly. */
#define TILE(base, u, v)  ((const unsigned char *)(base) + (((int)(v) + (int)(u) * 100) * 0x7f))

/* ─── KAROO_SIM_FX, read by value ─────────────────────────────────────────
 *
 * CLAUDE.md's rule: GetEnvironmentVariableA returns 0 for empty and unset
 * alike, so read the value, never the presence.
 *
 * `blindfoe` makes every cell report impassable.  That is a *measurement*
 * change, not a colour: the search then fails for every foe on every tick,
 * SetFoeChaseTarget leaves the pending move at 0, and foes stand still
 * instead of chasing.  Only this code path can produce it.
 */
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

/* See the note at the call site: this control has to break injectivity, not
 * merely change the number. */
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

/* KAROO_SIM_FX=nolookup makes both list lookups report "not present".
 * RelaxPathNeighbourCell then believes it has never seen any cell before, so
 * it allocates a fresh node for every neighbour of every expansion instead
 * of rewiring the existing one — the search stops recognising revisits and
 * the parent chain it builds is no longer the cheapest one.  A structural
 * break rather than a numeric one, which is what a pure lookup needs. */
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

/* ─── KAROO_FOEPATH_DIAG — is the re-open machinery actually doing work? ──
 *
 * Three functions in this file — PushPendingPathNode, PopPendingPathNode and
 * PropagateImprovedPathCosts — are reachable and exercised but invisible to
 * every asserted field in all twelve recordings (GAMETICK_PLAN.md records
 * this: `nocostfix` skips the cascade outright and the suite still passes
 * 12/12).  That raises the obvious question of whether they are live at all
 * or merely an optimisation that never fires, and it is a question to
 * measure rather than argue about.
 *
 * The structure already answers half of it.  RelaxPathNeighbourCell calls
 * the cascade on exactly ONE branch: a cell found on the CLOSED list that
 * has just been reached with a smaller g.  In a textbook A* with a
 * consistent heuristic that branch is unreachable — a closed node's cost can
 * never improve — and the whole subsystem would be dead code.  It is
 * reachable here because h is a SQUARED Euclidean distance (see
 * SearchPathNodeGraph and RelaxPathNeighbourCell), which is neither
 * admissible nor consistent.  So this is not an optimisation: it is the
 * repair mechanism for a deliberately-wrong heuristic, and how often it runs
 * is a property of the map.
 *
 * KAROO_FOEPATH_DIAG=1 counts the work and logs a running summary at
 * 1/10/100/1000/10000 cascade calls:
 *
 *   cascades  calls to PropagateImprovedPathCosts = closed cells re-opened
 *   reparent  children whose g, f and parent were actually rewritten
 *   pushes    worklist pushes (== reparent; they are the same branch)
 *   pops      worklist drains, i.e. re-opening that went more than one level
 *   deepest   the largest single drain, in nodes
 *
 * `pops` is the interesting one.  cascades > 0 with pops == 0 would mean
 * re-opens happen but never propagate past the immediate children; pops
 * climbing with cascades means the correction really does ripple.
 */
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

/* See the call site: skips cost re-propagation entirely.  Strictly stronger
 * than nopropagate, which left the direct children still being improved. */
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

/* See the call site: drops the cost-propagation push. */
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

/* See the call site: this breaks the write end of the f-ordering invariant,
 * as popsecond breaks the read end. */
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

/* ─── FoePath::CheckPathCellPassable (0x00401cd0) ─────────────────────────
 *
 * __thiscall, RET 8.  `this` = the pathfinder object at foe+0x13b.
 */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_CheckPathCellPassable(void *self, int u, int v)
{
    if (fx_blindfoe())
        return 0;

    const unsigned char *pf = (const unsigned char *)self;
    const void *tilebase = *(void *const *)pf;          /* this+0x00 */

    const unsigned char *t = TILE(tilebase, u, v);
    const unsigned char kind = t[0x19d];

    /* Void cell with nothing bridging it. */
    if (kind == 0 && *(const int *)(t + 0x1bc) == 0)
        return 0;
    if (kind == 0x16)
        return 0;
    if (kind == 0x17 && *(const int *)(t + 0x217) == 0)
        return 0;

    const unsigned char mode = pf[0x2a];                /* this+0x2a */

    if (mode == 7) {
        const unsigned char occupant = t[0x1a5];
        if (occupant == 4 || occupant == 3)
            return 0;
    }

    if (mode == 2 && kind == 0x11 &&
        *(const int *)(t + 0x217) == 0 && t[0x1a5] != 4)
        return 0;

    return 1;
}

/* ─── FoePath::ComputeCellLinearIndex (0x00401cb0) ────────────────────────
 *
 * __thiscall, RET 8.  The whole function is five instructions:
 *
 *     MOV EAX,[ECX+0x1e] / MOV ECX,[ESP+4] / IMUL EAX,[ESP+8]
 *     ADD EAX,ECX / RET 8
 *
 * i.e. `stride * v + u`, where the stride is a full 32-bit int at this+0x1e.
 *
 * THIS IS NOT THE TILE ADDRESSING.  CheckPathCellPassable above uses the
 * game-wide `(v + u*100) * 0x7f` scheme that entitymove.cpp also uses; this
 * one uses a per-search stride read from the object.  The two numbers are
 * unrelated, they are not interchangeable, and merging them — which is
 * tempting, since both turn a cell into a scalar — would silently corrupt
 * every node key the search compares.  Kept deliberately separate.
 *
 * The result is only ever used as an identity for a cell: SearchPathNodeGraph
 * compares it against the goal key (node+0x18) and RelaxPathNeighbourCell
 * stores it at node[6] for the open/closed lookups.  Nothing indexes memory
 * with it, so a stride that does not match the real map width would still
 * "work" as long as it is injective, which is presumably why nobody noticed
 * it differs from the tile stride.
 *
 * IMUL IS SIGNED and the add wraps in 32 bits; both are reproduced by using
 * plain `int`.  Five E8 call sites, all inside the cluster (two in
 * FindFoePathBetweenCells, two in SearchPathNodeGraph, one in
 * RelaxPathNeighbourCell); xref.py reports all five as CALL and nothing else.
 */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_ComputeCellLinearIndex(void *self, int u, int v)
{
    const int stride = *(const int *)((const unsigned char *)self + 0x1e);

    /* KAROO_SIM_FX=keyclash drops the column from the key, so every cell in
     * a row shares one identity.  A *value* change here is not necessarily
     * observable — the key is only ever used as a cell identity, so any
     * injective function of (u,v) would behave identically — which is why
     * the control has to break injectivity rather than just perturb the
     * arithmetic.  With rows collapsed, the search treats cells it has
     * never visited as already closed and the paths it returns go wrong. */
    if (fx_keyclash())
        return stride * v;

    return stride * v + u;
}

/* ─── FoePath::PopBestOpenPathNode (0x00401ec0) ───────────────────────────
 *
 * __thiscall, no arguments, RET 0.  Moves the front node of the open list
 * onto the front of the closed list and returns it.
 *
 *     head = open->next;            open   = this+0x06
 *     if (!head) return NULL;       closed = this+0x0a
 *     open->next   = head->next;    ->next = node+0x40
 *     head->next   = closed->next;
 *     closed->next = head;
 *     return head;
 *
 * GHIDRA HAD THIS TYPED `void`, AND IT IS NOT.  The node stays in EAX from
 * the `MOV EAX,[EDX+0x40]` that loads it — on the empty path EAX is the zero
 * that failed the TEST, on the success path it is the popped node — and
 * SearchPathNodeGraph uses that return value as its current node, so a
 * literal reading of the decompile would have produced a function that
 * silently returned garbage.  The Ghidra prototype is corrected this cycle.
 * This is the "a nonsensical decompile usually means a wrong type" rule in
 * its milder form: the decompile was not nonsensical, just quietly wrong.
 *
 * The list is kept in f order by the insertion in RelaxPathNeighbourCell, so
 * taking the front IS taking the best node; there is no scan here.
 *
 * TWO EXACTNESS POINTS:
 *
 * 1. THE ORIGINAL RE-READS this+0x0a TWICE (`MOV EDX,[ECX+0xa]` and then
 *    `MOV ECX,[ECX+0xa]`) rather than keeping it in a register.  Nothing
 *    between the two writes to it, so a single read is equivalent — but the
 *    two loads are kept here anyway, because CLAUDE.md's rule is to
 *    reproduce the original's shape rather than to tidy it, and a future
 *    reader diffing against the listing should not have to re-derive that
 *    the merge was safe.
 * 2. NO NULL CHECK ON THE LISTS THEMSELVES.  `open` and `closed` are
 *    dereferenced unconditionally; only the *node* is tested.  Both are
 *    allocated by SearchPathNodeGraph before this can run, so the original
 *    is right, and adding a guard would change behaviour on a corrupt object
 *    from a fault into a silent wrong answer.
 *
 * ONE E8 call site, at 0x00401e5c inside SearchPathNodeGraph; xref.py
 * reports that reference and no other.
 */
extern "C" __declspec(dllexport) void * __attribute__((thiscall))
Sim_PopBestOpenPathNode(void *self)
{
    unsigned char *pf = (unsigned char *)self;

    unsigned char *open = *(unsigned char **)(pf + 0x06);
    unsigned char *head = *(unsigned char **)(open + 0x40);
    if (head == 0)
        return 0;

    /* KAROO_SIM_FX=popsecond takes the *second* node off the open list when
     * there is one.  The list is f-ordered, so this is precisely "expand the
     * second-best node instead of the best" — the search still terminates
     * (the iteration cap at this+0x2f bounds it) and still returns paths,
     * but they are no longer the ones A* would choose.  A direction change
     * of sorts: it proves the ordering is load-bearing, not just that the
     * function runs. */
    if (fx_popsecond()) {
        unsigned char *second = *(unsigned char **)(head + 0x40);
        if (second != 0) {
            open = head;          /* unlink `second` from behind `head` */
            head = second;
        }
    }

    *(unsigned char **)(open + 0x40) = *(unsigned char **)(head + 0x40);

    /* Both reads of this+0x0a, as the original has them. */
    *(unsigned char **)(head + 0x40) =
        *(unsigned char **)(*(unsigned char **)(pf + 0x0a) + 0x40);
    *(unsigned char **)(*(unsigned char **)(pf + 0x0a) + 0x40) = head;

    return head;
}

/* FactAlloc::Free — __cdecl(void *), 0x0045087c.  THE named callback into
 * the game binary for this file, and the same exception every other
 * replacement here makes (gamelog.cpp, model.cpp, scenematerial.cpp,
 * texture.cpp all call it).  The pathfinder's nodes are allocated by the
 * game's calloc at 0x004507ff (named AllocateZeroedHeapBlock in Ghidra this
 * cycle), which routes through FactAlloc's own sub-allocator before falling
 * back to HeapAlloc.  Memory from that allocator MUST go back to that
 * allocator, so freeing it ourselves is not an option — this is the
 * no-callback rule's standing allocator exemption, named here as the rule
 * requires. */

/* AllocateZeroedHeapBlock — __cdecl(int count, int size), 0x004507ff: the
 * game's calloc, and the allocator half of the pair above.  The SECOND (and
 * last) named callback in this file, for the same unavoidable reason: cells
 * this allocates are freed by FactAlloc::Free, and blocks that cross that
 * boundary have to come from the matching allocator.  Named here as the
 * no-callback rule requires. */
typedef void * (__cdecl *alloc_zeroed_fn)(int, int);
static const alloc_zeroed_fn AllocateZeroedHeapBlock = (alloc_zeroed_fn)0x004507ff;

/* ─── FoePath::ReleasePathSearchNodeLists (0x00401d60) ────────────────────
 *
 * __thiscall, no arguments, RET 0.  Frees every node on both lists.  The two
 * halves are byte-identical but for the list offset: open at this+0x06,
 * closed at this+0x0a.
 *
 *     hdr = *(this+off);  if (!hdr) skip;
 *     n = hdr->next;      if (!n)   skip;
 *     do { p = n; n = n->next; FactAlloc::Free(p); } while (n);
 *
 * TWO DEFECTS, BOTH PRESERVED AND NEITHER FIXED:
 *
 * 1. THE HEADERS' `next` IS LEFT DANGLING.  Nothing writes 0 to hdr->next
 *    after the chain is freed, so both list headers keep pointing at freed
 *    memory when this returns.  It is safe only because the sole callers
 *    (FindFoePathBetweenCells at 0x401c03 and 0x401c74) either abandon the
 *    search or let SearchPathNodeGraph install fresh headers before anything
 *    reads them.  Writing the obvious `hdr->next = 0` here would be a
 *    behaviour change on any future path that does read them, and CLAUDE.md
 *    is explicit that reimplementations reproduce defects.
 *
 * 2. THE HEADERS THEMSELVES ARE LEAKED.  SearchPathNodeGraph callocs a new
 *    0x44-byte header for each list on every search and this frees only the
 *    nodes hanging off them, so each search leaks two blocks.  With a foe
 *    pathfinding every tick that is a steady drip for the whole level.  It
 *    is the original's behaviour and is reproduced exactly; noting it here
 *    because it will look like a leak introduced by this project the first
 *    time anyone profiles the game, and it is not.
 *
 * The loop shape is also the original's: the head node is tested once before
 * the loop and the `next` pointer is read *before* the free, which is what
 * makes freeing while walking safe.
 *
 * TWO E8 call sites, both in FindFoePathBetweenCells (0x00401c03 and
 * 0x00401c74); xref.py reports both as CALL and nothing else.
 */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_ReleasePathSearchNodeLists(void *self)
{
    unsigned char *pf = (unsigned char *)self;
    static const unsigned lists[2] = { 0x06, 0x0a };
    static int reported = 0;
    int freed = 0;

    for (int i = 0; i < 2; ++i) {
        unsigned char *hdr = *(unsigned char **)(pf + lists[i]);
        if (hdr == 0)
            continue;

        unsigned char *n = *(unsigned char **)(hdr + 0x40);
        if (n == 0)
            continue;

        do {
            unsigned char *p = n;
            n = *(unsigned char **)(n + 0x40);   /* read before the free */
            game_free(p);
            ++freed;
        } while (n != 0);

        /* Deliberately no `hdr->next = 0` — see defect 1 above. */
    }

    /* Logged UNCONDITIONALLY, once per run, for the same reason bomb.cpp
     * logs its first tick: this function has no replay-detectable negative
     * control.  Freeing is invisible to the simulation — skipping it leaks
     * and changes nothing a recording asserts, and freeing anything extra is
     * a corruption, not a control.  So "does the suite exercise it?" cannot
     * be answered by a failing assertion, and a flag-gated line could not
     * answer it either: silence would be ambiguous between "no foe ever
     * searched" and "the flag never arrived".
     *
     * Two lines, because the first call is always empty: it runs at the top
     * of FindFoePathBetweenCells before any search has allocated, so a
     * "first call" line alone reports 0 and proves only that the function is
     * reached.  The second line is the one that shows nodes being reclaimed.
     */
    if (reported == 0) {
        reported = 1;
        log_write("foepath: first ReleasePathSearchNodeLists -- %d node(s) freed\n", freed);
    }
    if (reported == 1 && freed > 0) {
        reported = 2;
        log_write("foepath: first non-empty release -- %d node(s) freed\n", freed);
    }
}

/* ─── FoePath::FindOpenPathNodeByKey   (0x00402130) ───────────────────────
 * ─── FoePath::FindClosedPathNodeByKey (0x00402150) ───────────────────────
 *
 * Both __thiscall, RET 4, one int argument: the cell key that
 * ComputeCellLinearIndex produced.  Walk the list looking for the node whose
 * key (node+0x18) matches, and return it, or 0.
 *
 * THE TWO LISTINGS ARE BYTE-IDENTICAL BUT FOR ONE BYTE — the list offset in
 * the first instruction, `MOV EAX,[ECX+0x6]` against `MOV EAX,[ECX+0xa]`.
 * Same instruction sequence, same lengths, same branch displacements.  They
 * are taken as one cycle for that reason, which is the precedent
 * entitymath.cpp and voicepool.cpp already set for small sibling pairs; the
 * shared body below is not a tidy-up, it is what the compiler was given.
 *
 * TWO EXACTNESS POINTS:
 *
 * 1. THE HEADER IS DEREFERENCED WITHOUT A NULL CHECK.  `MOV EAX,[ECX+off]`
 *    then straight into `MOV EAX,[EAX+0x40]`; only the *node* pointer is
 *    ever tested.  Same shape as PopBestOpenPathNode, and left alone for the
 *    same reason — the lists exist before any lookup can run, and a guard
 *    would convert a fault on a corrupt object into a silent wrong answer.
 * 2. THE KEY COMPARE IS A PLAIN 32-BIT EQUALITY (`CMP [EAX+0x18],ECX` /
 *    `JZ`), so the signedness of the key never matters — which is just as
 *    well, since ComputeCellLinearIndex's IMUL can produce a negative one.
 *
 * The header node itself is never a candidate: the walk starts at
 * hdr->next.  So a key that happened to match whatever lies at hdr+0x18 is
 * not returned, and the two lists' headers are pure sentinels.
 *
 * FindOpenPathNodeByKey has ONE E8 call site and FindClosedPathNodeByKey
 * ONE, both in RelaxPathNeighbourCell; xref.py reports those and nothing
 * else for either.
 */
static void *foepath_find_by_key(void *self, unsigned listoff, int key)
{
    unsigned char *hdr = *(unsigned char **)((unsigned char *)self + listoff);
    unsigned char *n = *(unsigned char **)(hdr + 0x40);

    while (n != 0) {
        if (*(const int *)(n + 0x18) == key)
            return n;
        n = *(unsigned char **)(n + 0x40);
    }
    return 0;
}

extern "C" __declspec(dllexport) void * __attribute__((thiscall))
Sim_FindOpenPathNodeByKey(void *self, int key)
{
    if (fx_nolookup())
        return 0;
    return foepath_find_by_key(self, 0x06, key);
}

extern "C" __declspec(dllexport) void * __attribute__((thiscall))
Sim_FindClosedPathNodeByKey(void *self, int key)
{
    if (fx_nolookup())
        return 0;
    return foepath_find_by_key(self, 0x0a, key);
}

/* ─── FoePath::InsertOpenPathNodeByCost (0x00402170) ──────────────────────
 *
 * __thiscall, RET 4, one argument: the node to insert.  Inserts it into the
 * open list in ascending order of the total cost f at node[0], which is what
 * makes PopBestOpenPathNode's "take the front" the same thing as "take the
 * best".  The two were read a cycle apart and agree; this is the half that
 * establishes the invariant.
 *
 *     hdr = this->open;                    (this+0x06)
 *     if (!hdr->next) { hdr->next = n; return; }
 *     prev = hdr;  cur = hdr->next;  f = n->f;
 *     while (cur && cur->f < f) { prev = cur; cur = cur->next; }
 *     n->next = cur;  prev->next = n;
 *
 * THREE EXACTNESS POINTS:
 *
 * 1. THE COMPARE IS SIGNED (`CMP [EAX],ESI` / `JGE`).  f is g + h with h a
 *    squared Euclidean distance, so it is non-negative in practice and the
 *    distinction is unreachable — but an unsigned compare here would be a
 *    different function, and the listing says signed.
 * 2. IT IS A STABLE INSERT-BEFORE-EQUAL.  The walk stops at the first node
 *    whose f is >= the new one's (JGE leaves the loop), so a tie puts the
 *    newcomer AHEAD of the equal-cost nodes already queued.  With f ties
 *    common on a square grid this decides the search's tie-breaking, and so
 *    which of several equally short paths a foe actually takes — the very
 *    thing the SetFoeChaseTarget plate comment attributes to "the
 *    pathfinder's tie-breaking".  Reversing it to insert-after-equal would
 *    keep every path optimal and still change observable movement.
 * 3. THE FIRST `TEST EAX,EAX` INSIDE THE LOOP IS DEAD.  EAX is already known
 *    non-zero from the JNZ that entered, so the first iteration can never
 *    take it.  Left as written rather than hoisted, per the standing rule
 *    about reproducing shape.
 *
 * ONE E8 call site, at 0x00402105 in RelaxPathNeighbourCell; xref.py reports
 * that and nothing else.
 */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_InsertOpenPathNodeByCost(void *self, void *node)
{
    unsigned char *hdr = *(unsigned char **)((unsigned char *)self + 0x06);
    unsigned char *n = (unsigned char *)node;

    unsigned char *cur = *(unsigned char **)(hdr + 0x40);
    if (cur == 0) {
        *(unsigned char **)(hdr + 0x40) = n;
        return;
    }

    const int f = *(const int *)n;          /* signed, see point 1 */
    unsigned char *prev = hdr;

    while (cur != 0 && *(const int *)cur < f) {
        prev = cur;
        cur = *(unsigned char **)(cur + 0x40);
    }

    /* KAROO_SIM_FX=nosort ignores the ordering and pushes at the front, so
     * the open list becomes LIFO and the search stops being A* — it still
     * terminates under the iteration cap and still returns paths, they are
     * just no longer the cheapest.  The mirror of popsecond: that one broke
     * the read end of the invariant, this one breaks the write end. */
    if (fx_nosort()) {
        *(unsigned char **)(n + 0x40) = *(unsigned char **)(hdr + 0x40);
        *(unsigned char **)(hdr + 0x40) = n;
        return;
    }

    *(unsigned char **)(n + 0x40) = cur;
    *(unsigned char **)(prev + 0x40) = n;
}

/* ─── FoePath::PushPendingPathNode (0x00402250) ───────────────────────────
 * ─── FoePath::PopPendingPathNode  (0x00402280) ───────────────────────────
 *
 * The two halves of the cost-propagation worklist, taken as one cycle
 * because they are push and pop of a single structure: testing either alone
 * is meaningless, since the only caller of both is
 * PropagateImprovedPathCosts and a half-replaced stack has no observable
 * intermediate state.
 *
 * IT IS A STACK, NOT A QUEUE.  Push links the new cell at the head and pop
 * takes the head, so the cascade below is depth-first.  The head lives at
 * `*(this+0x12) + 4`; `this+0x12` points at a small owner block whose +4 is
 * the only field either function touches.
 *
 * Cell layout: cell[0] = the path node, cell[4] = the next cell.
 *
 * THE CELL IS ALLOCATED AS calloc(1, 9) — NINE BYTES for two dwords.  The
 * ninth byte is never read or written by anything in the cluster.  It is
 * reproduced exactly: `9` here is not a typo to round up to 12, and asking
 * the allocator for 8 would be a different call with a different bucket.
 *
 * Push re-reads `*(this+0x12)` twice (0x402265 and 0x40226e) rather than
 * keeping it; nothing between the two writes to it, so the reads are
 * equivalent, and both are kept per the standing shape rule.
 *
 * POP HAS NO EMPTINESS CHECK — it dereferences the head unconditionally.
 * PropagateImprovedPathCosts tests `owner->head != 0` before every call, so
 * the original is right; a guard here would turn a fault on a corrupt object
 * into a silent wrong answer, the same call made for the lookups and for
 * PopBestOpenPathNode.
 *
 * Pop returns the *node*, not the cell, and frees the cell through
 * FactAlloc::Free — the allocator callback already named above, and the
 * matching half of the calloc in push.
 *
 * Push has TWO E8 call sites (0x004021E8, 0x0040222F) and pop ONE
 * (0x00402202), all three in PropagateImprovedPathCosts; xref.py reports
 * those and nothing else.
 */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PushPendingPathNode(void *self, void *node)
{
    /* KAROO_SIM_FX=nopropagate drops the push, so a node whose cost improves
     * never has its descendants re-examined: the cascade in
     * PropagateImprovedPathCosts finds an empty worklist and stops after the
     * direct children.  Paths stay valid but stop being cheapest, which is
     * the one thing this stack exists to guarantee.  It also allocates
     * nothing, so it is a clean switch rather than a leak. */
    if (fx_nopropagate())
        return;

    ++g_diag.pushes;

    unsigned char *cell = (unsigned char *)AllocateZeroedHeapBlock(1, 9);

    *(void **)cell = node;

    /* Both reads of this+0x12, as the original has them. */
    *(void **)(cell + 4) =
        *(void **)(*(unsigned char **)((unsigned char *)self + 0x12) + 4);
    *(unsigned char **)(*(unsigned char **)((unsigned char *)self + 0x12) + 4) = cell;
}

extern "C" __declspec(dllexport) void * __attribute__((thiscall))
Sim_PopPendingPathNode(void *self)
{
    unsigned char *owner = *(unsigned char **)((unsigned char *)self + 0x12);
    unsigned char *cell = *(unsigned char **)(owner + 4);

    ++g_diag.pops;

    void *node = *(void **)cell;
    *(void **)(owner + 4) = *(void **)(cell + 4);

    game_free(cell);
    return node;
}

/* ─── FoePath::PropagateImprovedPathCosts (0x004021b0) ────────────────────
 *
 * __thiscall, RET 4, one argument: the node whose g has just improved.
 * Pushes that improvement out through the search graph.
 *
 * Each node carries up to EIGHT children at node+0x20 (the neighbours that
 * were relaxed through it).  For each child still reachable more cheaply via
 * this parent, the child's g, f and parent link are rewritten and the child
 * is pushed on the worklist; then the worklist is drained, repeating the
 * same eight-child sweep for every node that comes off it.
 *
 * Node fields, in the layout the rest of this file already uses:
 * f at +0x00, h at +0x04, g at +0x08, parent at +0x1c, children at +0x20.
 *
 *     gnew = parent->g + 1;
 *     if (gnew < child->g) {
 *         child->g = gnew;  child->f = child->h + gnew;  child->parent = p;
 *         push(child);
 *     }
 *
 * FIVE EXACTNESS POINTS:
 *
 * 1. THE TWO SWEEPS ARE NOT THE SAME CODE, AND THE DIFFERENCE IS REAL.  In
 *    the first sweep the parent's g is loaded ONCE before the loop into a
 *    stack slot ([ESP+0x14]) and INC'd per child; in the drain sweep it is
 *    re-read from the popped node EVERY iteration (MOV ECX,[ESI+8] at
 *    0x00402214).  Nothing in either loop writes the parent's own g, so the
 *    two are equivalent today — but they are written differently and are
 *    reproduced differently, because "equivalent today" is exactly the kind
 *    of claim that stops being true when something upstream changes.
 * 2. THE CHILD WALK STOPS AT THE FIRST EMPTY SLOT, it does not skip holes:
 *    load, TEST, JZ leaves the loop entirely.  A node whose child array had
 *    a gap would have its later children ignored.
 * 3. THE COMPARE IS SIGNED (CMP/JGE), and the step is a plain +1 — this
 *    search has unit edge costs, which is also why h being a *squared*
 *    distance makes it inadmissible and the result not truly optimal.
 *    Preserved, not corrected.
 * 4. THE OUTER LOOP IS A do/while ON THE WORKLIST HEAD, re-tested after each
 *    drain sweep and jumping back to the pop rather than to the test.
 * 5. THE CHILD LINK IS NOT CLEARED anywhere here, so a node can be pushed
 *    again later through the same parent; termination rests entirely on the
 *    strict < in the improvement test.
 *
 * ONE E8 call site, at 0x004020A5 in RelaxPathNeighbourCell.
 */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PropagateImprovedPathCosts(void *self, void *node)
{
    unsigned char *pf = (unsigned char *)self;
    unsigned char *p = (unsigned char *)node;

    /* KAROO_SIM_FX=nocostfix skips the whole cascade, so a cheaper route
     * found later never rewrites the costs recorded earlier.  Stronger than
     * nopropagate, which only dropped the worklist and left the direct
     * children still being improved. */
    if (fx_nocostfix())
        return;

    ++g_diag.cascades;
    g_diag.cur_drain = 0;

    /* Sweep 1: the parent's g hoisted once, per point 1 above. */
    const int gp = *(const int *)(p + 0x08);
    for (int i = 0; i < 8; ++i) {
        unsigned char *c = *(unsigned char **)(p + 0x20 + i * 4);
        if (c == 0)
            break;
        const int gnew = gp + 1;
        if (gnew < *(const int *)(c + 0x08)) {
            *(int *)(c + 0x08) = gnew;
            *(int *)(c + 0x00) = *(const int *)(c + 0x04) + gnew;
            *(unsigned char **)(c + 0x1c) = p;
            ++g_diag.reparent;
            Sim_PushPendingPathNode(self, c);
        }
    }

    /* Sweep 2: drain the worklist, re-reading the parent's g each time. */
    unsigned char *owner = *(unsigned char **)(pf + 0x12);
    if (*(unsigned char **)(owner + 4) == 0) {
        diag_report();
        return;
    }

    do {
        unsigned char *q = (unsigned char *)Sim_PopPendingPathNode(self);
        if (++g_diag.cur_drain > g_diag.deepest)
            g_diag.deepest = g_diag.cur_drain;

        for (int i = 0; i < 8; ++i) {
            unsigned char *c = *(unsigned char **)(q + 0x20 + i * 4);
            if (c == 0)
                break;
            const int gnew = *(const int *)(q + 0x08) + 1;   /* re-read */
            if (gnew < *(const int *)(c + 0x08)) {
                *(int *)(c + 0x08) = gnew;
                *(int *)(c + 0x00) = *(const int *)(c + 0x04) + gnew;
                *(unsigned char **)(c + 0x1c) = q;
                ++g_diag.reparent;
                Sim_PushPendingPathNode(self, c);
            }
        }

        owner = *(unsigned char **)(pf + 0x12);
    } while (*(unsigned char **)(owner + 4) != 0);

    diag_report();
}

/* ─── FoePath::RelaxPathNeighbourCell (0x00402000) ────────────────────────
 *
 * __thiscall, RET 0x14 — five stack arguments:
 *   parent   the node being expanded
 *   u, v     the neighbour cell being relaxed
 *   goalU/V  the search goal, used only for the heuristic
 *
 * The classic relax step, in three branches on where the cell already is:
 *
 *   on the OPEN list    record it as a child of parent; if gnew is better,
 *                       rewrite g, f and parent.  No cascade — the node has
 *                       not been expanded yet, so it has no children to fix.
 *   on the CLOSED list  same, and THEN call PropagateImprovedPathCosts,
 *                       because this node's descendants already carry costs
 *                       derived from its old g.  This is the one call site
 *                       of the cascade, and the reason that subsystem exists
 *                       at all (see the DIAG note above).
 *   nowhere yet         allocate a 0x44-byte node, fill it in, insert it in
 *                       f order, and record it as a child of parent.
 *
 * gnew is `parent->g + 1` — unit edge costs — and
 *     h = (v - goalV)^2 + (u - goalU)^2
 * is a SQUARED Euclidean distance.  That is the inadmissible, inconsistent
 * heuristic the whole re-open machinery exists to compensate for; it is
 * reproduced exactly and must not be "fixed" to a real distance.
 *
 * ─── A GENUINE BUFFER OVERFLOW, PRESERVED ────────────────────────────────
 *
 * All three branches record the neighbour in the parent's child array with
 * the same open-coded scan:
 *
 *     i = 0; while (parent->child[i] != 0 && ++i < 8) ;
 *     parent->child[i] = node;          <-- no bounds check on i
 *
 * The loop exits with i == 8 when every slot is occupied, and the store then
 * writes ONE PAST the eight-entry array at node+0x20..0x3c — i.e. straight
 * onto node+0x40, WHICH IS THE LIST `next` POINTER.  A ninth child silently
 * relinks the open or closed list through an arbitrary node.
 *
 * This is reproduced deliberately, per CLAUDE.md's rule that reimplementations
 * are bit-exact including defects.  It is reachable only for a cell with nine
 * or more distinct relaxed neighbours, which a 4-connected grid cannot
 * produce in one expansion — but ExpandPathNodeNeighbours relaxes through the
 * same parent repeatedly across a search, and nothing ever clears these
 * slots, so the count is cumulative rather than per-expansion.  Do not add a
 * bounds check without a recording that proves the overflow unreachable.
 *
 * ─── Order matters in two places ─────────────────────────────────────────
 *
 * 1. In the two "already present" branches the child slot is written BEFORE
 *    the cost test, and therefore EVEN WHEN THE COST DOES NOT IMPROVE.  In
 *    the new-node branch it is written LAST, after the f-ordered insert.
 *    The three are not interchangeable.
 * 2. The closed branch writes parent, f and g in that order and only then
 *    cascades, so the cascade sees the already-updated node.
 *
 * FOUR E8 call sites (0x00401F30, 0x00401F6D, 0x00401FAA, 0x00401FE7),
 * all in ExpandPathNodeNeighbours — one per neighbour; xref.py reports those
 * four as CALL and nothing else.
 */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_RelaxPathNeighbourCell(void *self, void *parent, int u, int v,
                           int goalU, int goalV)
{
    unsigned char *p = (unsigned char *)parent;
    const int gnew = *(const int *)(p + 0x08) + 1;
    const int key = Sim_ComputeCellLinearIndex(self, u, v);

    /* The open-coded scan, overflow and all — see the note above. */
    #define RELAX_RECORD_CHILD(node)                                        \
        do {                                                                \
            int _i = 0;                                                     \
            while (*(void **)(p + 0x20 + _i * 4) != 0 && ++_i < 8)          \
                ;                                                           \
            *(void **)(p + 0x20 + _i * 4) = (node);                         \
        } while (0)

    unsigned char *n = (unsigned char *)Sim_FindOpenPathNodeByKey(self, key);
    if (n != 0) {
        RELAX_RECORD_CHILD(n);
        if (gnew < *(const int *)(n + 0x08)) {
            *(int *)(n + 0x08) = gnew;
            *(int *)(n + 0x00) = *(const int *)(n + 0x04) + gnew;
            *(unsigned char **)(n + 0x1c) = p;
        }
        return;
    }

    n = (unsigned char *)Sim_FindClosedPathNodeByKey(self, key);
    if (n != 0) {
        RELAX_RECORD_CHILD(n);
        if (gnew < *(const int *)(n + 0x08)) {
            *(unsigned char **)(n + 0x1c) = p;
            *(int *)(n + 0x00) = *(const int *)(n + 0x04) + gnew;
            *(int *)(n + 0x08) = gnew;
            Sim_PropagateImprovedPathCosts(self, n);
        }
        return;
    }

    /* Not seen before: a fresh node. */
    n = (unsigned char *)AllocateZeroedHeapBlock(1, 0x44);

    const int du = u - goalU;
    const int dv = v - goalV;
    int h = dv * dv + du * du;            /* squared distance, deliberately */

    /* KAROO_SIM_FX=truedist replaces the squared heuristic with a real
     * (Manhattan) one.  That is the "obvious fix" this file warns against:
     * it makes h admissible and consistent, which changes the expansion
     * order, the paths chosen, and — since a consistent heuristic never
     * re-opens a closed node — silences the whole cost cascade too.  A
     * change of algorithm, not of a constant. */
    if (fx_truedist())
        h = (du < 0 ? -du : du) + (dv < 0 ? -dv : dv);

    *(unsigned char **)(n + 0x1c) = p;
    *(int *)(n + 0x08) = gnew;
    *(int *)(n + 0x04) = h;
    *(int *)(n + 0x18) = key;
    *(int *)(n + 0x00) = h + gnew;
    *(int *)(n + 0x10) = u;
    *(int *)(n + 0x14) = v;

    Sim_InsertOpenPathNodeByCost(self, n);

    RELAX_RECORD_CHILD(n);              /* last, unlike the branches above */

    #undef RELAX_RECORD_CHILD
}

/* ─── GetCellStepDirectionCode (0x0041f8c0) ───────────────────────────────
 *
 * `__stdcall`, RET 0x10, four byte arguments (u_from, v_from, u_to, v_to).
 * Classifies the step between two cells into a 1..4 code, or 0.
 *
 *     v_from <  v_to  -> 1
 *     u_from <  u_to  -> 4
 *     v_from >  v_to  -> 3
 *     u_to   <  u_from-> 2
 *     otherwise          0        (the two cells are the same)
 *
 * Tested in exactly that order and reproduced in it; the tests are not
 * mutually exclusive for a diagonal, so the order is what decides.
 *
 * THIS IS NOT THE MOVEMENT FACING TABLE, AND MUST NOT BE RECONCILED WITH IT.
 * worldstate.h's facing map (confirmed three ways, and independently again
 * by SetFoeChaseTarget) is 1 -> (0,-1), 2 -> (+1,0), 3 -> (0,+1),
 * 4 -> (-1,0).  This function pairs the axes the other way round: +v gives 1
 * where the facing table gives 3, and +u gives 4 where the facing table
 * gives 2.  That is not a bug in either.  The sole consumer,
 * CheckCellStepIsLegal, compares the result against a RAMP KIND minus 4 —
 * tile kinds 5..8 are the four ramp orientations, so 5..8 - 4 = 1..4 — and
 * ramp orientation is simply a different enumeration from entity facing.
 * "Fixing" this to agree with the facing table would break every ramp test.
 *
 * ALL FOUR COMPARES ARE UNSIGNED (JNC / JBE) on bytes, so the parameters are
 * u8 and there is no negative case; widening them to signed int would change
 * the answer for any coordinate above 0x7f.
 *
 * The zero case is computed rather than branched — `CMP CL,AL; SBB AL,AL;
 * AND EAX,2` — which yields 2 when u_to < u_from and 0 otherwise.  Note the
 * AND is on the full EAX whose upper bytes are stale at that point; the mask
 * clears them, so the result is a clean 0 or 2 and there is no garbage-byte
 * deviation to preserve.
 *
 * TWO E8 call sites, both inside CheckCellStepIsLegal (the ramp test in each
 * of its two halves); xref.py reports both as CALL and nothing else.
 */
extern "C" __declspec(dllexport) int __attribute__((stdcall))
Sim_GetCellStepDirectionCode(unsigned char u_from, unsigned char v_from,
                             unsigned char u_to,   unsigned char v_to)
{
    /* KAROO_SIM_FX=facingramp swaps the axis pairing to the movement facing
     * table (+v -> 3, +u -> 2).  This is precisely the "reconciliation" the
     * comment above warns is wrong: it should misclassify every ramp. */
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

/* ─── CheckCellStepIsLegal (0x0041f500) ───────────────────────────────────
 *
 * `__thiscall`, RET 0x10, four byte arguments (u_from, v_from, u_to, v_to).
 * `this` IS THE TILE BASE, not the pathfinder object — ExpandPathNodeNeighbours
 * passes `pathfinder->tilebase` (`*(void **)this`).  Getting that wrong would
 * make every offset below read from the wrong place.
 *
 * Answers "may an entity step from one cell to the adjacent one", covering
 * flat ground, ramps, kind-9 steps, bridges (0x10), elevators (0x0e) and
 * jump pads (2).
 *
 * ─── THE FOUR "IMPOSSIBLE" OFFSETS ARE NEIGHBOUR TILES ───────────────────
 *
 * The decompile renders four accesses as `EBP + 0x3338`, `EBP + -0x3000`,
 * `EBP + 0x21b` and `EBP + 0x11d`, which look like the pData trap or a
 * struct overrun.  They are neither.  With the tile stride 0x7f and the row
 * stride 100 * 0x7f = 0x319C, and the height field at +0x19c:
 *
 *     +0x3338 = +0x319C + 0x19C   ->  neighbour(u+1).height
 *     -0x3000 = -0x319C + 0x19C   ->  neighbour(u-1).height
 *     +0x021b = +0x007F + 0x19C   ->  neighbour(v+1).height
 *     +0x011d = -0x007F + 0x19C   ->  neighbour(v-1).height
 *
 * i.e. the elevator compares its own level byte (+0x1f1) against the height
 * of the neighbour on the OPPOSITE side from the direction of travel — the
 * cell behind you.  All four follow that rule consistently, which is what
 * makes the reading safe rather than a guess.  They are written below as
 * explicit neighbour lookups so the next reader does not have to redo this.
 *
 * ─── Things that must not be tidied ──────────────────────────────────────
 *
 * 1. THE CLAUSES ARE A SEQUENCE OF ASSIGNMENTS, NOT AN OR.  Later clauses
 *    OVERWRITE the verdict, including clearing it: the bridge and elevator
 *    blocks both end in an explicit `flag = 0` on their failure paths, wiping
 *    out whatever the ramp and height clauses decided.  Reordering them, or
 *    collapsing them into a chain of `||`, changes the answer.  (This is also
 *    why KAROO_SIM_FX=facingramp is unobservable — the ramp clause's verdict
 *    is frequently overwritten downstream.)
 * 2. HEIGHTS ARE ZERO-EXTENDED BYTES COMPARED AS INTS.  `to.height ==
 *    from.height - 1` is int arithmetic, so a from.height of 0 yields -1 and
 *    matches nothing.  Keeping it in int is deliberate.
 * 3. THE STEP-DIRECTION AND HEIGHT DELTA COMPARES ARE UNSIGNED on the byte
 *    coordinates but signed once widened (`JGE`/`JLE` on the delta).
 * 4. THE SECOND CheckTileIsRamp(from.kind) IS REDUNDANT — the original calls
 *    it twice with the same argument on the same path.  Reproduced as one
 *    call with a comment rather than two, since it is a pure function of its
 *    argument and cannot differ; this is the one place shape is not
 *    preserved literally, and it is noted here for that reason.
 * 5. THE ORIGINAL SCRIBBLES ON ITS OWN ARGUMENT SLOT: at 0x0041f7a8 it stores
 *    to.kind over the caller's `u_to` word, then reads it back at 0x0041f81d.
 *    Harmless (the slot is callee-cleaned and u_to is live in a register),
 *    but it is why the listing appears to compare `u_to` against 0x0e.
 *
 * ─── The jump-pad tail ───────────────────────────────────────────────────
 *
 * When the FROM cell is an unoccupied jump pad (kind 2, occupant 0) the
 * function ignores everything above and returns whether one specific cell is
 * occupied by 4.  That cell is
 *
 *     u' = u_to + 2*(u_from - u_to)  =  2*u_from - u_to
 *     v' = v_to + 2*(v_from - v_to)  =  2*v_from - v_to
 *
 * i.e. the mirror of the destination through the pad — the cell directly
 * BEHIND you as you step onto the pad.  The doubling is written exactly as
 * the original computes it (the `*2 ... *50 ... *2` chain), because the
 * intermediate is formed in a byte before being sign-extended and a tidier
 * formula would not wrap the same way.
 *
 * FOUR E8 call sites, all in ExpandPathNodeNeighbours (one per neighbour).
 */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_CheckCellStepIsLegal(void *self, unsigned char u_from, unsigned char v_from,
                         unsigned char u_to, unsigned char v_to)
{
    const unsigned char *base = (const unsigned char *)self;

    /* KAROO_SIM_FX=freestep declares every step legal.  Foes then path
     * straight through walls, height changes and the wrong way along
     * bridges — the strongest possible break of this function, and the one
     * that says whether its verdict is consulted at all. */
    if (fx_freestep())
        return 1;

    const unsigned char *to   = TILE(base, u_to,   v_to);
    const unsigned char *from = TILE(base, u_from, v_from);

    int flag = 0;

    /* Ramp entry: the destination ramp must face the way we are stepping,
     * or the reverse of it.
     *
     * THIS TEST APPEARS TWICE, AND THAT IS THE ORIGINAL, NOT A SLIP.  The
     * same CheckTileIsRamp(to.kind) and the same GetCellStepDirectionCode
     * are evaluated again below (0x0041f62e onward) to gate the ramp-height
     * rule.  Both are pure, so the second evaluation cannot differ — but the
     * two clauses set the verdict at different points in the sequence, with
     * the height clauses in between, so they are not foldable into one. */
    if (Sim_CheckTileIsRamp(to[0x19d])) {
        const int code = Sim_GetCellStepDirectionCode(u_from, v_from, u_to, v_to);
        const unsigned char k = to[0x19d];
        if ((int)(k & 0xff) - 4 == code ||
            Sim_GetTurnedDirection((unsigned char)(k - 4), 2) == code)
            flag = 1;
    }

    /* Flat step, or a kind-9 step whose two step heights bracket the move. */
    if (from[0x19d] != 0x10 && from[0x19c] == to[0x19c])
        flag = 1;
    else if (to[0x19d] == 9 &&
             (to[0x1d3] == from[0x19c] || to[0x1d4] == from[0x19c]))
        flag = 1;
    else if (from[0x19d] == 9 &&
             (from[0x1d3] == to[0x19c] || from[0x1d4] == to[0x19c]))
        flag = 1;

    if (Sim_CheckTileIsRamp(to[0x19d])) {
        const int code = Sim_GetCellStepDirectionCode(u_from, v_from, u_to, v_to);
        const unsigned char k = to[0x19d];
        if ((int)(k & 0xff) - 4 == code ||
            Sim_GetTurnedDirection((unsigned char)(k - 4), 2) == code) {
            int ok = 1;
            /* The original calls CheckTileIsRamp(from.kind) twice here; it is
             * pure, so once is exact.  See point 4 above. */
            if (Sim_CheckTileIsRamp(from[0x19d]) && code != (int)from[0x19d])
                ok = 0;
            if (ok && (int)to[0x19c] == (int)from[0x19c] - 1)
                flag = 1;
        }
    } else {
        const int d = (int)to[0x19c] - (int)from[0x19c];
        if (from[0x19d] != 0x10 && d < 3 && d > 0)
            flag = (*(const int *)(from + 0x1bc) == 0);
    }

    /* Bridge / conveyor: only passable along its own direction byte. */
    if (from[0x19d] == 0x10 && from[0x19c] == to[0x19c]) {
        const unsigned char dir = from[0x1f2];
        if (v_from < v_to && dir == 1)
            flag = 1;
        else if (u_from < u_to && dir == 4)
            flag = 1;
        else if (v_from > v_to && dir == 3)
            flag = 1;
        else if (u_from > u_to && dir == 2)
            flag = 1;
        else
            flag = 0;                    /* clears earlier clauses */
    }

    if (to[0x19d] == 0x10)
        flag = 1;

    /* Elevator: its level byte must match the height of the cell behind. */
    if (from[0x19d] == 0x0e) {
        const unsigned char lvl = from[0x1f1];
        const unsigned char *nu_pos = TILE(base, u_from + 1, v_from);
        const unsigned char *nu_neg = TILE(base, u_from - 1, v_from);
        const unsigned char *nv_pos = TILE(base, u_from, v_from + 1);
        const unsigned char *nv_neg = TILE(base, u_from, v_from - 1);

        if (u_from > u_to && lvl == nu_pos[0x19c])
            flag = 1;
        else if (u_from < u_to && lvl == nu_neg[0x19c])
            flag = 1;
        else if (v_from > v_to && lvl == nv_pos[0x19c])
            flag = 1;
        else if (v_from < v_to && lvl == nv_neg[0x19c])
            flag = 1;
        else
            flag = 0;                    /* clears earlier clauses */
    }

    if (to[0x19d] == 0x0e)
        flag = 1;

    /* Unoccupied jump pad: the answer is about the cell behind, and nothing
     * decided above survives. */
    if (from[0x19d] == 2 && from[0x1a5] == 0) {
        const signed char du = (signed char)(u_from - u_to);
        const signed char dv = (signed char)(v_from - v_to);
        const int up = (int)u_to + (int)du * 2;
        const int vp = (int)v_to + (int)dv * 2;
        const unsigned char *land = base + ((vp + up * 100) * 0x7f);
        return land[0x1a5] == 4;
    }

    return flag;
}

/* ─── FoePath::ExpandPathNodeNeighbours (0x00401ef0) ──────────────────────
 *
 * __thiscall, RET 0xc: (node, goalU, goalV).  The four-neighbour expansion,
 * and now a thin function because everything it calls is ours.
 *
 * For each neighbour, in this order — v-1, u+1, v+1, u-1 — it applies two
 * independent gates and then relaxes:
 *
 *     if (CheckPathCellPassable(u', v'))                    is the cell open?
 *         if (CheckCellStepIsLegal(tilebase, u,v, u',v'))   can we get there?
 *             RelaxPathNeighbourCell(node, u', v', goalU, goalV);
 *
 * THE ORDER IS THE SEARCH'S TIE-BREAKING and must not be sorted or
 * rearranged.  Equal-cost nodes enter the open list in this sequence, and
 * InsertOpenPathNodeByCost is a stable insert-before-equal, so this order
 * decides which of several equally short paths a foe walks.  It is the other
 * half of the tie-breaking noted on that function.
 *
 * TWO THINGS THE LISTING MAKES CLEAR THAT THE DECOMPILE DOES NOT:
 *
 * 1. THE TWO GATES TAKE DIFFERENT `this`.  CheckPathCellPassable is called on
 *    the pathfinder (ECX = this), CheckCellStepIsLegal on `*(void **)this` —
 *    the TILE BASE (`MOV ECX,[EDI]` before each of its four calls).  This is
 *    the direct confirmation of that function's `this` type, from the call
 *    site rather than from its body.
 * 2. THE FROM-COORDINATES ARE RE-READ FROM THE NODE for every neighbour, and
 *    are passed to the step test as BYTES (`MOV AL,[ESI+0x14]`) while the
 *    passability test and the relax get the full ints.  Nothing writes the
 *    node's u/v here, so the re-reads are equivalent — but the byte-vs-int
 *    split is real and is preserved by the parameter types.
 *
 * Node coordinates: u at +0x10, v at +0x14.
 *
 * ONE E8 call site, at 0x00401E79 in SearchPathNodeGraph; xref.py reports
 * that and nothing else.
 */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_ExpandPathNodeNeighbours(void *self, void *node, int goalU, int goalV)
{
    unsigned char *n = (unsigned char *)node;
    void *tilebase = *(void **)self;

    /* Each step: the neighbour coordinate is formed exactly as the original
     * forms it, from a fresh read of the node. */
    struct { int du, dv; } step[4] = { { 0, -1 }, { +1, 0 }, { 0, +1 }, { -1, 0 } };

    /* KAROO_SIM_FX=revexpand reverses the neighbour order.  Every path stays
     * exactly as short — only which equal-cost node is queued first changes —
     * so this isolates the tie-breaking claim in the comment above from the
     * expansion itself.  If it were unobservable, the claim that this order
     * decides a foe's route would be unsupported. */
    if (fx_revexpand()) {
        for (int i = 0; i < 2; ++i) {
            const int du = step[i].du, dv = step[i].dv;
            step[i].du = step[3 - i].du;  step[i].dv = step[3 - i].dv;
            step[3 - i].du = du;          step[3 - i].dv = dv;
        }
    }

    for (int i = 0; i < 4; ++i) {
        const int u = *(const int *)(n + 0x10);
        const int v = *(const int *)(n + 0x14);
        const int nu = u + step[i].du;
        const int nv = v + step[i].dv;

        if (!Sim_CheckPathCellPassable(self, nu, nv))
            continue;
        if (!Sim_CheckCellStepIsLegal(tilebase, (unsigned char)u, (unsigned char)v,
                                      (unsigned char)nu, (unsigned char)nv))
            continue;

        Sim_RelaxPathNeighbourCell(self, node, nu, nv, goalU, goalV);
    }
}

/* ─── FoePath::SearchPathNodeGraph (0x00401db0) ───────────────────────────
 *
 * __thiscall, RET 0x10: (uFoe, vFoe, uTarget, vTarget).  The search loop.
 *
 * ─── THE SEARCH RUNS BACKWARD, AND THAT EXPLAINS THE CONSUMER ────────────
 *
 * The seed node is built from the THIRD and FOURTH arguments — the target
 * cell — and the key the loop is looking for is `key(arg1, arg2)`, the foe's
 * own cell.  So the search starts at the player and walks out until it
 * reaches the foe.
 *
 * That is why SetFoeChaseTarget, having got a result, takes the found node's
 * PARENT (`node->parent` at +0x1c) as the move: the found node IS the foe's
 * cell, and its parent is the next cell along the path back toward the
 * player.  Read forwards this looks like an off-by-one; read backwards it is
 * exactly right.  It also explains why the cost cascade is invisible
 * (GAMETICK_PLAN.md): only that one parent pointer is ever consumed.
 *
 * ─── What it does ────────────────────────────────────────────────────────
 *
 *   this+0x33/0x34 = the foe cell, this+0x31/0x32 = the target cell (stored
 *   for the caller's benefit; nothing here reads them back)
 *   allocate the open list header, the closed list header and the seed node,
 *     all calloc(1, 0x44)
 *   seed: g = 0, h = f = (uT-uF)^2 + (vT-vF)^2, key = key(uT,vT),
 *         u = uT, v = vT; linked directly as open->next
 *   loop up to `cap` times, where cap is the u16 at this+0x2f that
 *     SetFoeChaseTarget writes from its `speed` argument:
 *         node = PopBestOpenPathNode();  if (!node) return 0;
 *         if (node->key == goalKey) break;
 *         ExpandPathNodeNeighbours(node, uFoe, vFoe);
 *   on break with iterations left: this+0xe = node; return 1
 *   otherwise return 0
 *
 * ─── Exactness ───────────────────────────────────────────────────────────
 *
 * 1. THE THREE HEADERS ARE LEAKED, NOT REUSED.  Each call allocates two new
 *    list headers; ReleasePathSearchNodeLists frees only the nodes hanging
 *    off them.  Documented on that function and reproduced here — this is
 *    the allocation half of the same defect.
 * 2. THE CAP IS AN UNSIGNED 16-BIT COMPARE on entry (`CMP word [ESI+0x2f],DI`
 *    with DI zero, JBE) and a SIGNED 32-bit one inside the loop, after
 *    zero-extension.  A cap of 0 therefore skips the loop entirely and falls
 *    into the tail with a junk `node` value that the `iter < cap` test then
 *    discards — reproduced by initialising the result to null and letting the
 *    same test reject it.
 * 3. h IS THE SQUARED distance again, matching RelaxPathNeighbourCell.  g is
 *    explicitly zeroed even though calloc already did it.
 * 4. THE ORIGINAL SCRIBBLES THE GOAL KEY over its own caller's third argument
 *    slot and re-reads it from there every iteration.  Equivalent to a local
 *    (nothing else writes it), and kept as a local here — noted only because
 *    it makes the listing look like it is reading an argument that has
 *    already been consumed.
 * 5. The return is a full 32-bit 0/1.
 *
 * ONE E8 call site, at 0x00401C85 in FindFoePathBetweenCells; xref.py
 * reports that and nothing else.
 */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_SearchPathNodeGraph(void *self, int uFoe, int vFoe, int uTarget, int vTarget)
{
    unsigned char *pf = (unsigned char *)self;

    pf[0x32] = (unsigned char)vTarget;
    pf[0x34] = (unsigned char)vFoe;
    pf[0x31] = (unsigned char)uTarget;
    pf[0x33] = (unsigned char)uFoe;

    const int goalKey = Sim_ComputeCellLinearIndex(self, uFoe, vFoe);

    /* Two fresh list headers every search — see exactness point 1. */
    *(void **)(pf + 0x06) = AllocateZeroedHeapBlock(1, 0x44);
    *(void **)(pf + 0x0a) = AllocateZeroedHeapBlock(1, 0x44);

    unsigned char *seed = (unsigned char *)AllocateZeroedHeapBlock(1, 0x44);

    const int du = uTarget - uFoe;
    const int dv = vTarget - vFoe;
    const int h = du * du + dv * dv;

    *(int *)(seed + 0x08) = 0;                  /* g, already zero */
    *(int *)(seed + 0x04) = h;
    *(int *)(seed + 0x00) = h;
    *(int *)(seed + 0x18) = Sim_ComputeCellLinearIndex(self, uTarget, vTarget);
    *(int *)(seed + 0x10) = uTarget;
    *(int *)(seed + 0x14) = vTarget;

    *(unsigned char **)(*(unsigned char **)(pf + 0x06) + 0x40) = seed;

    unsigned short cap = *(const unsigned short *)(pf + 0x2f);

    /* KAROO_SIM_FX=shortsearch caps the loop at a single expansion, so any
     * path longer than one step is reported as "no path".  Attacks the
     * iteration budget specifically rather than the search's correctness. */
    if (fx_shortsearch() && cap > 1)
        cap = 1;

    int iter = 0;
    unsigned char *node = 0;

    if (cap > 0) {
        for (;;) {
            node = (unsigned char *)Sim_PopBestOpenPathNode(self);
            if (node == 0)
                return 0;
            if (*(const int *)(node + 0x18) == goalKey)
                break;

            Sim_ExpandPathNodeNeighbours(self, node, uFoe, vFoe);

            if (++iter >= (int)cap)
                break;
        }
    }

    if (iter < (int)cap) {
        *(unsigned char **)(pf + 0x0e) = node;
        return 1;
    }
    return 0;
}

/* ─── FoePath::FindFoePathBetweenCells (0x00401c20) ───────────────────────
 *
 * __thiscall, RET 0x10: (uFoe, vFoe, uTarget, vTarget).  The cluster's entry
 * point, and the last of its sixteen functions.
 *
 *   if (!CheckPathCellPassable(uTarget, vTarget)) fail;
 *   if (!CheckPathCellPassable(uFoe,    vFoe))    fail;
 *   if (key(uFoe,vFoe) == key(uTarget,vTarget))   fail;   // already there
 *   ReleasePathSearchNodeLists();                         // drop the last search
 *   if (!SearchPathNodeGraph(uFoe, vFoe, uTarget, vTarget)) fail;
 *   this+0x16 = 1; return 1;
 * fail:
 *   this+0x16 = 0; return 0;
 *
 * ─── Points worth keeping ────────────────────────────────────────────────
 *
 * 1. THE TARGET IS TESTED FOR PASSABILITY BEFORE THE FOE'S OWN CELL.  Order
 *    matters only because CheckPathCellPassable is not pure — it reads live
 *    tile state — but it is preserved regardless.
 * 2. THE "SAME CELL" TEST COMPARES NODE KEYS, NOT COORDINATES.  Since
 *    ComputeCellLinearIndex uses the stride at this+0x1e rather than the tile
 *    stride, two genuinely different cells collide whenever that stride is
 *    wrong — and the function then reports "no path" rather than searching.
 *    This is the one place the stride mismatch documented on
 *    ComputeCellLinearIndex could produce visible behaviour, which is why
 *    that helper's stride must not be "corrected" to 100.
 * 3. THE PREVIOUS SEARCH'S NODES ARE FREED HERE, not by the search itself,
 *    and only once both endpoint checks have passed.  An early failure
 *    therefore LEAVES THE LAST SEARCH'S NODES ALLOCATED AND LINKED, and the
 *    next successful call frees them then.  Reproduced exactly.
 * 4. this+0x16 IS WRITTEN AS AN UNALIGNED DWORD, and is written on every
 *    path — it mirrors the return value for the caller's benefit.
 * 5. The return is a full 32-bit 0/1.
 *
 * ONE E8 call site, at 0x0043AA4E in SetFoeChaseTarget; xref.py reports
 * that and nothing else, and no absolute-address call from our own DLL
 * (the two grep hits in levelscore.cpp and plan.cpp are prose, not code).
 */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_FindFoePathBetweenCells(void *self, int uFoe, int vFoe,
                            int uTarget, int vTarget)
{
    unsigned char *pf = (unsigned char *)self;

    if (Sim_CheckPathCellPassable(self, uTarget, vTarget) != 0 &&
        Sim_CheckPathCellPassable(self, uFoe, vFoe) != 0) {

        const int keyFoe    = Sim_ComputeCellLinearIndex(self, uFoe, vFoe);
        const int keyTarget = Sim_ComputeCellLinearIndex(self, uTarget, vTarget);

        if (keyFoe != keyTarget) {
            Sim_ReleasePathSearchNodeLists(self);

            /* KAROO_SIM_FX=fwdsearch swaps the endpoints, making the search
             * run forward from the foe to the target instead of backward.
             * It still finds paths of the same length — but the node
             * SetFoeChaseTarget then reads is the TARGET's, not the foe's,
             * so its parent is no longer the foe's next step.  This is the
             * direct test of the backward-search finding documented on
             * SearchPathNodeGraph. */
            const int ok = fx_fwdsearch()
                ? Sim_SearchPathNodeGraph(self, uTarget, vTarget, uFoe, vFoe)
                : Sim_SearchPathNodeGraph(self, uFoe, vFoe, uTarget, vTarget);
            if (ok != 0) {
                *(int *)(pf + 0x16) = 1;
                return 1;
            }
        }
    }

    *(int *)(pf + 0x16) = 0;
    return 0;
}
