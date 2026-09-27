/* Every array is allocated here and freed only by the release; the renderer
 * only reads them.
 *
 * PRESERVED:
 *   1. The exit's position is not reset by the release: a level with no
 *      exit keeps the previous level's.  The last exit cell found wins.
 *   2. Lifts and slides store all-zero positions and rotations.
 *   3. The wall-run state is not reset between rows, nor between one
 *      height's column sweep and the next height's row sweep.  A run open
 *      at the end of a row carries into the next row's first cells; one
 *      open when a sweep ends is continued by the next sweep (into its
 *      list) or dropped.  Both sweeps share the two run slots.
 *   4. Neighbour reads at the grid edge are not bounds-checked: they read
 *      the bytes either side of the row.
 *
 * The slide list's count is the Game's slide count, but it is written once per
 * slide-track cell.  The game writes past its block when a level has more
 * track cells than slides; here the block is sized to cover every write, so
 * the overrun stays in our allocation, and count is unchanged.
 * KAROO_PLACEMENT_DIAG=1 logs the two numbers per level. */

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
#include "sceneobjects.h"
#include "liftobject.h"
#include "slideobject.h"
#include <math.h>
#include "renderdevice.h"
LevelPlacements g_levelPlacements;

/* Cells come from LevelMap::tile(), which takes signed axes, so the wall
 * builder's edge neighbours are the same out-of-row addresses (defect 4). */

static const float K_HALF = 0.5f;
static const float K_NEG  = -1.0f;
static const float K_ONE  = 1.0f;

static const float YAW_NEG_QUARTER = -1.5707964f;  // -pi/2 as a float
static const float YAW_QUARTER     =  1.5707964f;  // pi/2 as a float
static const float YAW_HALF        =  3.1415927f;  // pi as a float

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

/* The wall strips.  For each height L, two sweeps find runs of solid cells at
 * height L whose neighbour on one side is not solid at that height: the top
 * edge of a vertical face.  The row sweep (v outer, u inner) looks at the v-1
 * and v+1 neighbours; the column sweep (u outer, v inner) at u-1 and u+1.
 * Each run becomes one quad from the edge down by the theme's side height. */

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

/* Cell t is solid at height L and its neighbour n is not. */
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
    std::vector<Run> lists[4];  // row -v, row +v, column -u, column +u
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
        a.open = b.open = false;  // PRESERVED: dropped, not pushed (defect 3)

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
    // PRESERVED: not reset here; the next height's row sweep inherits a and b.
    }

    size_t n = lists[0].size() + lists[1].size() + lists[2].size() + lists[3].size();
    p->wallStripCount = (int)n;
    p->wallVerts = (PlacementVertex *)::operator new(n * 6 * sizeof(PlacementVertex));

    // Six vertices per run.  S and E are the start and end on the edge, S_ and
    // E_ the same `depth` lower; T = 1 - depth is the edge's v coordinate, 1
    // the bottom's.  Row faces take u from x, column faces from z.  The vertex
    // order, shade and coordinates differ per list.
    const float T = K_ONE - depth;
    PlacementVertex *o = p->wallVerts;
    for (int li = 0; li < 4; ++li) {
        const DWORD shade = (li == 0 || li == 3) ? 0xff404040 : 0xffb0b0b0;
        const int ax = (li < 2) ? 0 : 2;  // which axis is u
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

    // Pass 1: count.
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
            if (k == TILE_SLIDE_TRACK)  ++trackCells;  // see the top of the file
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

    // The tile-top template.
    static const DWORD quad[4][8] = {
        { 0x3f000000, 0, 0xbf000000, 0xffffffff, 0,          0x3f800000, 0,          0x3f800000 },
        { 0xbf000000, 0, 0xbf000000, 0xffffffff, 0,          0,          0,          0          },
        { 0x3f000000, 0, 0x3f000000, 0xffffffff, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000 },
        { 0xbf000000, 0, 0x3f000000, 0xffffffff, 0x3f800000, 0,          0x3f800000, 0          },
    };
    memcpy(p->tileQuad, quad, sizeof quad);

    // Pass 2: fill, height by height, so each array is in height order.
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
                    // Two triangles over the cell, BbVertex-shaped.
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
                {  // packed members: copy, don't take their address
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
                    default: yaw = 0.0f; break;  // 4 and anything else
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

/* Before WinMain: every dword of the template zero but the diffuse,
 * 0xffffffff.  The builder overwrites all four later. */
extern "C" __declspec(dllexport) void __cdecl
LevelPlacements_StaticInit(void)
{
    for (int i = 0; i < 4; i++) {
        for (int k = 0; k < 8; k++)
            g_levelPlacements.tileQuad[i].d[k] = 0;
        g_levelPlacements.tileQuad[i].d[3] = 0xffffffff;
    }
}

/* The block is packed but 4-aligned in memory; its head is the quad the scene
 * renderer animates. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"

/* The lift and slide passes.  Counts compare unsigned.  The block itself is
 * passed as the scene renderer's quad: tileQuad heads it. */
extern "C" __declspec(dllexport) void __cdecl
LevelPlacements_DrawLifts(Game *g, LevelPlacements *p, ThemeAssetBlock *theme,
                          RenderDevice *d3d, double now)
{
    for (unsigned int i = 0; i < (unsigned int)p->lifts.count; i++) {
        const LiftObject *lift = g->liftSlot(i);
        p->lifts.pos[i][0] = lift->posU();
        p->lifts.pos[i][1] = lift->height();
        p->lifts.pos[i][2] = -lift->posV();
    }
    Scene_RenderSceneObjects(g, (SceneQuadVertex *)(void *)p,  // tileQuad, at +0
                             (const Vec3 *)p->lifts.pos, (const Vec3 *)p->lifts.rot,
                             p->lifts.count, &theme->slots[THEME_OBJ_ELEVATOR],
                             d3d, now, 0.0f, 0, 0);
}

extern "C" __declspec(dllexport) void __cdecl
LevelPlacements_DrawSlides(Game *g, LevelPlacements *p, ThemeAssetBlock *theme,
                           RenderDevice *d3d, double now)
{
    for (unsigned int i = 0; i < (unsigned int)p->slides.count; i++) {
        const SlideObject *slide = g->slideSlot(i);
        p->slides.pos[i][0] = slide->posU();
        p->slides.pos[i][1] = slide->posY();
        p->slides.pos[i][2] = -slide->posV();
        if (slide->kind() == 0x0a)
            p->slides.rot[i][1] = 1.5707963705062866f;  // pi/2 as a float
    }
    float animTime = (float)fmod(now * (double)0.002f, 1.0);
    Scene_RenderSceneObjects(g, (SceneQuadVertex *)(void *)p,  // tileQuad, at +0
                             (const Vec3 *)p->slides.pos, (const Vec3 *)p->slides.rot,
                             p->slides.count, &theme->slots[THEME_OBJ_PLATFORM],
                             d3d, now, animTime, 0x14, 0);
}
#pragma GCC diagnostic pop
