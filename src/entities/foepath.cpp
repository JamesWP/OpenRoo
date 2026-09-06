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
typedef void (__cdecl *factalloc_free_fn)(void *);
static const factalloc_free_fn FactAlloc_Free = (factalloc_free_fn)0x0045087c;

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
            FactAlloc_Free(p);
            ++freed;
        } while (n != 0);

        /* Deliberately no `hdr->next = 0` — see defect 1 above. */
    }

    /* Logged UNCONDITIONALLY, once per run, for the same reason bombfuse.cpp
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

    FactAlloc_Free(cell);
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
    const int h = dv * dv + du * du;      /* squared distance, deliberately */

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
    if (v_from < v_to)
        return 1;
    if (u_from < u_to)
        return 4;
    if (v_from > v_to)
        return 3;
    return (u_to < u_from) ? 2 : 0;
}
