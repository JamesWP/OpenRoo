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
