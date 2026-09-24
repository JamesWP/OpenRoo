/* levelplacements.cpp -- the per-level placement lists (levelplacements.h),
 * ENDGAME_PLAN.md E4, the level-load chain:
 *
 *   0x00407fc0  ReleaseLevelPlacementArrays   ours: LevelPlacements_Release
 *   0x00404dd0  BuildLevelPlacementLists      ours: LevelPlacements_Build
 *   0x00406530  BuildLevelWallStrips          ours, internal
 *
 * Written from the disassembly: the decompile types the counts as floats and
 * loses which stack buffer each wall vertex's v coordinate comes from.
 *
 * ─── Heap ─────────────────────────────────────────────────────────────────
 *
 * Every array is allocated here and freed only by the release, whose two
 * callers (the builder, WinMain's shutdown 0x42d62f) are both patched to
 * ours.  So the arrays are on OUR heap, not alloc.h.  RenderGameFrame only
 * reads them.
 *
 * ─── Defects and oddities preserved deliberately ──────────────────────────
 *
 * 1. The exit's position is not reset by the release: a level with no
 *    TILE_EXIT keeps the previous level's.  The last exit cell found wins.
 * 2. Lifts and slides store all-zero positions and rotations.
 * 3. The wall-run state is not reset between rows, nor between the column
 *    sweep of one height and the row sweep of the next.  A run still open
 *    at the end of a row carries on into the next row's first cells, and
 *    one still open when a sweep ends is either continued by the next sweep
 *    (into that sweep's list) or dropped.  The two run slots are shared by
 *    both sweeps.
 * 4. Neighbour reads at the grid edge are not bounds-checked: they read the
 *    bytes either side of the row, as the original does.
 *
 * ─── One deliberate difference ────────────────────────────────────────────
 *
 * The slide list is allocated for Game::slideCount() entries but written
 * once per TILE_SLIDE_TRACK cell.  If a level has more track cells than
 * slides, the original writes zeros past the end of that block.  Here the
 * block is sized to cover every write, so the overrun lands in our own
 * allocation instead of corrupting the heap; `count` still holds
 * slideCount(), so every reader sees the same data.
 * KAROO_PLACEMENT_DIAG=1 logs the two numbers per level.
 */
#include <windows.h>
#include <string.h>
#include <vector>
#include <new>
#include "levelplacements.h"
#include "game.h"
#include "levelmap.h"
#include "tile.h"
#include "theme.h"
#include "log.h"

/* ─── Grid access ───────────────────────────────────────────────────────── */

/* Cells come from LevelMap::tile(), which takes signed axes: the wall
 * builder's edge neighbours (u-1 at u = 0, and so on) are the same
 * out-of-row addresses the original reads (defect 4). */

static const float K_HALF = 0.5f;      /* 0x45d318 */
static const float K_NEG  = -1.0f;     /* 0x45d344 */
static const float K_ONE  = 1.0f;      /* 0x45d298 */

static const float YAW_NEG_QUARTER = -1.5707964f;   /* 0xbfc90fdb */
static const float YAW_QUARTER     =  1.5707964f;   /* 0x3fc90fdb */
static const float YAW_HALF        =  3.1415927f;   /* 0x40490fdb */

static DWORD fbits(float f) { DWORD d; memcpy(&d, &f, 4); return d; }

/* The BbVertex-shaped writes: x, y, z, 0, diffuse, 0, u, v. */
static void put(PlacementVertex *out, float x, float y, float z,
                DWORD diffuse, float u, float v)
{
    out->d[0] = fbits(x); out->d[1] = fbits(y); out->d[2] = fbits(z);
    out->d[3] = 0;        out->d[4] = diffuse;  out->d[5] = 0;
    out->d[6] = fbits(u); out->d[7] = fbits(v);
}

static void set3(float *d, float x, float y, float z) { d[0] = x; d[1] = y; d[2] = z; }

static int diag_on()
{
    static int v = -1;
    if (v < 0) {
        char buf[8];
        DWORD n = GetEnvironmentVariableA("KAROO_PLACEMENT_DIAG", buf, sizeof buf);
        v = (n > 0 && n < sizeof buf && buf[0] == '1');
    }
    return v;
}

/* ─── ReleaseLevelPlacementArrays 0x00407fc0 ──────────────────────────────── */

static void release_list(PlacementList *l)
{
    ::operator delete(l->pos); l->pos = NULL;
    ::operator delete(l->rot); l->rot = NULL;
    l->count = 0;
}

extern "C" __declspec(dllexport) void __cdecl
LevelPlacements_Release(LevelPlacements *p)
{
    ::operator delete(p->kind01Verts);
    p->kind01Verts = NULL;
    p->kind01Count = 0;
    release_list(&p->lifts);
    release_list(&p->slides);
    release_list(&p->glue);
    release_list(&p->breakables);
    release_list(&p->jumpPads);
    release_list(&p->switches);
    release_list(&p->teleporters);
    release_list(&p->ramps);
    release_list(&p->climbs);
    release_list(&p->conveyors);
    release_list(&p->destructibles);
    ::operator delete(p->wallVerts);
    p->wallVerts = NULL;
    p->wallStripCount = 0;
}

/* ─── BuildLevelWallStrips 0x00406530 ─────────────────────────────────────
 *
 * For each height L, two sweeps find the runs of solid cells at height L
 * whose neighbour on one side is not a solid cell at the same height -- the
 * top edge of a vertical face.  The row sweep (v outer, u inner) looks at
 * the v-1 and v+1 neighbours; the column sweep (u outer, v inner) at u-1 and
 * u+1.  Each run is (start xyz, end xyz); each becomes one quad from the
 * edge down by the theme's side height. */

static bool solid(unsigned char kind)
{
    switch (kind) {
    case TILE_KIND_01: case TILE_IMPASSABLE: case TILE_GLUE:
    case TILE_CONVEYOR: case TILE_DESTRUCTIBLE: case TILE_JUMP_PAD:
    case TILE_TELEPORTER: case TILE_SWITCH: case TILE_EXIT:
        return true;
    }
    return false;
}

/* Cell `t` is solid at height L, and its neighbour `n` is not solid at the
 * same height. */
static bool face(const Tile *t, const Tile *n, unsigned L)
{
    if (t->height() != L || !solid(t->objectMarker()))
        return false;
    return n->height() != t->height() || !solid(n->objectMarker());
}

struct Run { float s[6]; };
struct RunSlot { bool open; Run r; };

static void close_run(RunSlot *slot, std::vector<Run> *list)
{
    if (slot->open) {
        list->push_back(slot->r);
        slot->open = false;
    }
}

static void build_walls(LevelPlacements *p, const Game *g, float depth)
{
    std::vector<Run> lists[4];   /* row -v, row +v, column -u, column +u */
    RunSlot a = { false, {{0}} }, b = { false, {{0}} };
    const LevelMap *map = g->map();
    const unsigned V = map->extentV(), U = map->extentU();

    for (unsigned L = 0; L < 0x100; ++L) {
        for (unsigned v = 0; v < V; ++v)
            for (unsigned u = 0; u < U; ++u) {
                const Tile *t = map->tile(u, v);
                float fu = (float)u, fv = (float)v, fL = (float)L;
                if (face(t, map->tile(u, (int)v - 1), L)) {
                    if (a.open) set3(&a.r.s[3], fu + K_HALF, fL, K_HALF - fv);
                    else {
                        set3(&a.r.s[0], fu - K_HALF, fL, K_HALF - fv);
                        set3(&a.r.s[3], fu + K_HALF, fL, K_HALF - fv);
                        a.open = true;
                    }
                } else close_run(&a, &lists[0]);
                if (face(t, map->tile(u, v + 1), L)) {
                    if (b.open) set3(&b.r.s[3], fu + K_HALF, fL, -fv - K_HALF);
                    else {
                        set3(&b.r.s[0], fu - K_HALF, fL, -fv - K_HALF);
                        set3(&b.r.s[3], fu + K_HALF, fL, -fv - K_HALF);
                        b.open = true;
                    }
                } else close_run(&b, &lists[1]);
            }
        a.open = b.open = false;   /* dropped, not pushed (defect 3) */

        for (unsigned u = 0; u < U; ++u)
            for (unsigned v = 0; v < V; ++v) {
                const Tile *t = map->tile(u, v);
                float fu = (float)u, fv = (float)v, fL = (float)L;
                if (face(t, map->tile((int)u - 1, v), L)) {
                    if (a.open) set3(&a.r.s[3], fu - K_HALF, fL, -fv - K_HALF);
                    else {
                        set3(&a.r.s[0], fu - K_HALF, fL, K_HALF - fv);
                        set3(&a.r.s[3], fu - K_HALF, fL, -fv - K_HALF);
                        a.open = true;
                    }
                } else close_run(&a, &lists[2]);
                if (face(t, map->tile(u + 1, v), L)) {
                    if (b.open) set3(&b.r.s[3], fu + K_HALF, fL, -fv - K_HALF);
                    else {
                        set3(&b.r.s[0], fu + K_HALF, fL, K_HALF - fv);
                        set3(&b.r.s[3], fu + K_HALF, fL, -fv - K_HALF);
                        b.open = true;
                    }
                } else close_run(&b, &lists[3]);
            }
        /* NOT reset here: the next height's row sweep inherits a and b. */
    }

    size_t n = lists[0].size() + lists[1].size() + lists[2].size() + lists[3].size();
    p->wallStripCount = (int)n;
    p->wallVerts = (PlacementVertex *)::operator new(n * 6 * sizeof(PlacementVertex));

    /* Six vertices per run.  S/E = start/end on the edge, S_/E_ the same
     * `depth` lower; T = 1 - depth is the edge's v coordinate, 1.0 the
     * bottom's.  Row faces take u from x, column faces from z.  The order,
     * shade and coordinates are per list, read off each block. */
    const float T = K_ONE - depth;
    PlacementVertex *o = p->wallVerts;
    for (int li = 0; li < 4; ++li) {
        const DWORD shade = (li == 0 || li == 3) ? 0xff404040 : 0xffb0b0b0;
        const int ax = (li < 2) ? 0 : 2;          /* which axis is u */
        for (size_t i = 0; i < lists[li].size(); ++i) {
            const float *s = lists[li][i].s;
            const float su = s[ax] + K_HALF, eu = s[3 + ax] + K_HALF;
            const float Sy_ = s[1] - depth, Ey_ = s[4] - depth;
            switch (li) {
            case 0:
                put(o++, s[3], s[4], s[5], shade, eu, T);
                put(o++, s[0], s[1], s[2], shade, su, T);
                put(o++, s[3], Ey_,  s[5], shade, eu, K_ONE);
                put(o++, s[0], s[1], s[2], shade, su, T);
                put(o++, s[0], Sy_,  s[2], shade, su, K_ONE);
                put(o++, s[3], Ey_,  s[5], shade, eu, K_ONE);
                break;
            case 1: case 2:
                put(o++, s[0], s[1], s[2], shade, su, T);
                put(o++, s[3], s[4], s[5], shade, eu, T);
                put(o++, s[3], Ey_,  s[5], shade, eu, K_ONE);
                put(o++, s[0], Sy_,  s[2], shade, su, K_ONE);
                put(o++, s[0], s[1], s[2], shade, su, T);
                put(o++, s[3], Ey_,  s[5], shade, eu, K_ONE);
                break;
            case 3:
                put(o++, s[3], s[4], s[5], shade, eu, T);
                put(o++, s[0], s[1], s[2], shade, su, T);
                put(o++, s[3], Ey_,  s[5], shade, eu, K_ONE);
                put(o++, s[0], s[1], s[2], shade, su, T);
                put(o++, s[0], Sy_,  s[2], shade, su, K_ONE);
                put(o++, s[3], Ey_,  s[5], shade, eu, K_ONE);
                break;
            }
        }
    }
}

/* ─── BuildLevelPlacementLists 0x00404dd0 ─────────────────────────────────── */

static void alloc_list(PlacementList *l, unsigned entries)
{
    l->pos = (float (*)[3])::operator new(entries * 12);
    l->rot = (float (*)[3])::operator new(entries * 12);
}

static void put_entry(PlacementList *l, unsigned *next, float x, float y,
                      float z, float yaw)
{
    set3(l->pos[*next], x, y, z);
    set3(l->rot[*next], 0.0f, yaw, 0.0f);
    ++*next;
}

extern "C" __declspec(dllexport) void __cdecl
LevelPlacements_Build(LevelPlacements *p, const Game *g,
                      const ThemeAssetBlock *theme)
{
    LevelPlacements_Release(p);

    const LevelMap *map = g->map();
    const unsigned V = map->extentV(), U = map->extentU();

    /* Pass 1: count. */
    unsigned trackCells = 0;
    for (unsigned v = 0; v < V; ++v)
        for (unsigned u = 0; u < U; ++u) {
            unsigned char k = map->tile(u, v)->objectMarker();
            if (k == TILE_LIFT)         ++p->lifts.count;
            if (k == TILE_KIND_01)      ++p->kind01Count;
            if (k == TILE_GLUE)         ++p->glue.count;
            if (k == TILE_BREAKABLE)    ++p->breakables.count;
            if (k == TILE_JUMP_PAD)     ++p->jumpPads.count;
            if (k == TILE_TELEPORTER)   ++p->teleporters.count;
            if (k == TILE_CLIMB)        ++p->climbs.count;
            if (k == TILE_SWITCH)       ++p->switches.count;
            if (k == TILE_CONVEYOR)     ++p->conveyors.count;
            if (k == TILE_DESTRUCTIBLE) ++p->destructibles.count;
            if (k >= TILE_RAMP_1 && k <= TILE_RAMP_4) ++p->ramps.count;
            if (k == TILE_SLIDE_TRACK)  ++trackCells;   /* ours; see header */
        }
    p->slides.count = g->slideCount();

    if (diag_on())
        log_write("levelplacements: slideCount=%d, slide-track cells=%u%s\n",
                   p->slides.count, trackCells,
                   trackCells > (unsigned)p->slides.count
                       ? " -- the original overruns its slide block" : "");

    if (p->kind01Count != 0)
        p->kind01Verts = (PlacementVertex *)::operator new(p->kind01Count * 6 * sizeof(PlacementVertex));
    if (p->lifts.count != 0)       alloc_list(&p->lifts, p->lifts.count);
    if (p->glue.count != 0)        alloc_list(&p->glue, p->glue.count);
    if (p->breakables.count != 0)  alloc_list(&p->breakables, p->breakables.count);
    if (p->jumpPads.count != 0)    alloc_list(&p->jumpPads, p->jumpPads.count);
    if (p->teleporters.count != 0) alloc_list(&p->teleporters, p->teleporters.count);
    if (p->switches.count != 0)    alloc_list(&p->switches, p->switches.count);
    if (p->ramps.count != 0)       alloc_list(&p->ramps, p->ramps.count);
    if (p->climbs.count != 0)      alloc_list(&p->climbs, p->climbs.count);
    unsigned slideCap = (unsigned)p->slides.count > trackCells
                            ? (unsigned)p->slides.count : trackCells;
    if (slideCap != 0) {
        alloc_list(&p->slides, slideCap);
        memset(p->slides.rot, 0, slideCap * 12);
    }
    if (p->conveyors.count != 0)     alloc_list(&p->conveyors, p->conveyors.count);
    if (p->destructibles.count != 0) alloc_list(&p->destructibles, p->destructibles.count);

    /* The tile-top template. */
    static const DWORD quad[4][8] = {
        { 0x3f000000, 0, 0xbf000000, 0xffffffff, 0,          0x3f800000, 0,          0x3f800000 },
        { 0xbf000000, 0, 0xbf000000, 0xffffffff, 0,          0,          0,          0          },
        { 0x3f000000, 0, 0x3f000000, 0xffffffff, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000 },
        { 0xbf000000, 0, 0x3f000000, 0xffffffff, 0x3f800000, 0,          0x3f800000, 0          },
    };
    memcpy(p->tileQuad, quad, sizeof quad);

    /* Pass 2: fill, height by height, so each array is in height order. */
    unsigned n01 = 0, nLift = 0, nSlide = 0, nGlue = 0, nBreak = 0, nJump = 0,
             nTele = 0, nSwitch = 0, nRamp = 0, nClimb = 0, nConv = 0, nDest = 0;
    for (unsigned L = 0; L < 0x100; ++L)
        for (unsigned v = 0; v < V; ++v)
            for (unsigned u = 0; u < U; ++u) {
                const Tile *t = map->tile(u, v);
                if (t->height() != L)
                    continue;
                const float x = (float)u, y = (float)t->height(), z = (float)v * K_NEG;
                switch (t->objectMarker()) {
                case TILE_KIND_01: {
                    /* Two triangles over the cell, BbVertex-shaped. */
                    const float A = x + K_HALF, B = z - K_HALF;
                    const float C = x - K_HALF, D = z + K_HALF;
                    PlacementVertex *o = p->kind01Verts + n01 * 6;
                    put(o++, A, y, B, 0x00ffffff, 0.0f, 1.0f);
                    put(o++, C, y, B, 0x00ffffff, 0.0f, 0.0f);
                    put(o++, A, y, D, 0x00ffffff, 1.0f, 1.0f);
                    put(o++, A, y, D, 0x00ffffff, 1.0f, 1.0f);
                    put(o++, C, y, B, 0x00ffffff, 0.0f, 0.0f);
                    put(o++, C, y, D, 0x00ffffff, 1.0f, 0.0f);
                    ++n01;
                    break;
                }
                case TILE_GLUE:       put_entry(&p->glue, &nGlue, x, y, z, 0.0f); break;
                case TILE_EXIT:
                {   /* packed members: copy, don't take their address */
                    const float pos[3] = { x, y, z }, rot[3] = { 0.0f, 0.0f, 0.0f };
                    memcpy(p->exitPos, pos, sizeof pos);
                    memcpy(p->exitRot, rot, sizeof rot);
                }
                    break;
                case TILE_RAMP_1:     put_entry(&p->ramps, &nRamp, x, y, z, YAW_NEG_QUARTER); break;
                case TILE_RAMP_2:     put_entry(&p->ramps, &nRamp, x, y, z, 0.0f); break;
                case TILE_RAMP_3:     put_entry(&p->ramps, &nRamp, x, y, z, YAW_QUARTER); break;
                case TILE_RAMP_4:     put_entry(&p->ramps, &nRamp, x, y, z, YAW_HALF); break;
                case TILE_LIFT:       put_entry(&p->lifts, &nLift, 0.0f, 0.0f, 0.0f, 0.0f); break;
                case TILE_SLIDE_TRACK: put_entry(&p->slides, &nSlide, 0.0f, 0.0f, 0.0f, 0.0f); break;
                case TILE_BREAKABLE:  put_entry(&p->breakables, &nBreak, x, y, z, 0.0f); break;
                case TILE_JUMP_PAD:   put_entry(&p->jumpPads, &nJump, x, y, z, 0.0f); break;
                case TILE_TELEPORTER: put_entry(&p->teleporters, &nTele, x, y, z, 0.0f); break;
                case TILE_CLIMB: {
                    float yaw;
                    switch (t->climbDir()) {
                    case 1:  yaw = YAW_NEG_QUARTER; break;
                    case 2:  yaw = YAW_HALF; break;
                    case 3:  yaw = YAW_QUARTER; break;
                    default: yaw = 0.0f; break;    /* 4 and anything else */
                    }
                    put_entry(&p->climbs, &nClimb, x, y, z, yaw);
                    break;
                }
                case TILE_SWITCH:      put_entry(&p->switches, &nSwitch, x, y, z, 0.0f); break;
                case TILE_CONVEYOR:    put_entry(&p->conveyors, &nConv, x, y, z, 0.0f); break;
                case TILE_DESTRUCTIBLE: put_entry(&p->destructibles, &nDest, x, y, z, 0.0f); break;
                default: break;
                }
            }

    build_walls(p, g, theme->flSideHeight);
}

/* ─── 0x425680: static-init table entry 7 ─────────────────────────────────
 *
 * The compiler's default construction of tileQuad's four vertices: every
 * dword zero except d[3] (the diffuse), 0xffffffff.  Nothing else in the
 * object is touched -- it is zero-initialised static storage -- and nothing
 * is registered with atexit.  The builder later overwrites all four. */
extern "C" __declspec(dllexport) void __cdecl
LevelPlacements_StaticInit(void)
{
    for (int i = 0; i < 4; i++) {
        for (int k = 0; k < 8; k++)
            GG_LEVEL_PLACEMENTS->tileQuad[i].d[k] = 0;
        GG_LEVEL_PLACEMENTS->tileQuad[i].d[3] = 0xffffffff;
    }
}
