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
#include "logger.h"
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

void PlacementList::release()
{
    ::operator delete(pos_); pos_ = NULL;
    ::operator delete(rot_); rot_ = NULL;
    count_ = 0;
}

void LevelPlacements::release()
{
    ::operator delete(kind01Verts_);
    kind01Verts_ = NULL;
    kind01Count_ = 0;
    lifts_.release();
    slides_.release();
    glue_.release();
    breakables_.release();
    jumpPads_.release();
    switches_.release();
    teleporters_.release();
    ramps_.release();
    climbs_.release();
    conveyors_.release();
    destructibles_.release();
    ::operator delete(wallVerts_);
    wallVerts_ = NULL;
    wallStripCount_ = 0;
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

void LevelPlacements::buildWalls(const Game *g, float depth)
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
    wallStripCount_ = (int)n;
    wallVerts_ = (PlacementVertex *)::operator new(n * 6 * sizeof(PlacementVertex));

    // Six vertices per run.  S and E are the start and end on the edge, S_ and
    // E_ the same `depth` lower; T = 1 - depth is the edge's v coordinate, 1
    // the bottom's.  Row faces take u from x, column faces from z.  The vertex
    // order, shade and coordinates differ per list.
    const float T = K_ONE - depth;
    PlacementVertex *o = wallVerts_;
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

void PlacementList::alloc(unsigned entries)
{
    pos_ = (float (*)[3])::operator new(entries * 12);
    rot_ = (float (*)[3])::operator new(entries * 12);
}

void PlacementList::put(unsigned *next, float x, float y, float z, float yaw)
{
    set3(pos_[*next], x, y, z);
    set3(rot_[*next], 0.0f, yaw, 0.0f);
    ++*next;
}

void LevelPlacements::build(const Game *g,
                      const ThemeAssetBlock *theme)
{
    release();

    const LevelMap *map = g->map();
    const unsigned V = map->extentV(), U = map->extentU();

    // Pass 1: count.
    unsigned trackCells = 0;
    for (unsigned v = 0; v < V; ++v)
        for (unsigned u = 0; u < U; ++u) {
            unsigned char k = map->tile(u, v)->objectMarker();
            if (k == TILE_LIFT)         ++lifts_.count_;
            if (k == TILE_KIND_01)      ++kind01Count_;
            if (k == TILE_GLUE)         ++glue_.count_;
            if (k == TILE_BREAKABLE)    ++breakables_.count_;
            if (k == TILE_JUMP_PAD)     ++jumpPads_.count_;
            if (k == TILE_TELEPORTER)   ++teleporters_.count_;
            if (k == TILE_CLIMB)        ++climbs_.count_;
            if (k == TILE_SWITCH)       ++switches_.count_;
            if (k == TILE_CONVEYOR)     ++conveyors_.count_;
            if (k == TILE_DESTRUCTIBLE) ++destructibles_.count_;
            if (k >= TILE_RAMP_1 && k <= TILE_RAMP_4) ++ramps_.count_;
            if (k == TILE_SLIDE_TRACK)  ++trackCells;  // see the top of the file
        }
    slides_.count_ = g->slideCount();

    if (diag_on())
        g_logger.write("levelplacements: slideCount=%d, slide-track cells=%u%s\n",
                   slides_.count_, trackCells,
                   trackCells > (unsigned)slides_.count_
                       ? " -- the original overruns its slide block" : "");

    if (kind01Count_ != 0)
        kind01Verts_ = (PlacementVertex *)::operator new(kind01Count_ * 6 * sizeof(PlacementVertex));
    if (lifts_.count_ != 0)       lifts_.alloc(lifts_.count_);
    if (glue_.count_ != 0)        glue_.alloc(glue_.count_);
    if (breakables_.count_ != 0)  breakables_.alloc(breakables_.count_);
    if (jumpPads_.count_ != 0)    jumpPads_.alloc(jumpPads_.count_);
    if (teleporters_.count_ != 0) teleporters_.alloc(teleporters_.count_);
    if (switches_.count_ != 0)    switches_.alloc(switches_.count_);
    if (ramps_.count_ != 0)       ramps_.alloc(ramps_.count_);
    if (climbs_.count_ != 0)      climbs_.alloc(climbs_.count_);
    unsigned slideCap = (unsigned)slides_.count_ > trackCells
                            ? (unsigned)slides_.count_ : trackCells;
    if (slideCap != 0) {
        slides_.alloc(slideCap);
        memset(slides_.rot_, 0, slideCap * 12);
    }
    if (conveyors_.count_ != 0)     conveyors_.alloc(conveyors_.count_);
    if (destructibles_.count_ != 0) destructibles_.alloc(destructibles_.count_);

    // The tile-top template.
    static const DWORD quad[4][8] = {
        { 0x3f000000, 0, 0xbf000000, 0xffffffff, 0,          0x3f800000, 0,          0x3f800000 },
        { 0xbf000000, 0, 0xbf000000, 0xffffffff, 0,          0,          0,          0          },
        { 0x3f000000, 0, 0x3f000000, 0xffffffff, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000 },
        { 0xbf000000, 0, 0x3f000000, 0xffffffff, 0x3f800000, 0,          0x3f800000, 0          },
    };
    memcpy(tileQuad_, quad, sizeof quad);

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
                    PlacementVertex *o = kind01Verts_ + n01 * 6;
                    put(o++, A, y, B, 0x00ffffff, 0.0f, 1.0f);
                    put(o++, C, y, B, 0x00ffffff, 0.0f, 0.0f);
                    put(o++, A, y, D, 0x00ffffff, 1.0f, 1.0f);
                    put(o++, A, y, D, 0x00ffffff, 1.0f, 1.0f);
                    put(o++, C, y, B, 0x00ffffff, 0.0f, 0.0f);
                    put(o++, C, y, D, 0x00ffffff, 1.0f, 0.0f);
                    ++n01;
                    break;
                }
                case TILE_GLUE:       glue_.put(&nGlue, x, y, z, 0.0f); break;
                case TILE_EXIT:
                {  // packed members: copy, don't take their address
                    const float pos[3] = { x, y, z }, rot[3] = { 0.0f, 0.0f, 0.0f };
                    memcpy(exitPos_, pos, sizeof pos);
                    memcpy(exitRot_, rot, sizeof rot);
                }
                    break;
                case TILE_RAMP_1:     ramps_.put(&nRamp, x, y, z, YAW_NEG_QUARTER); break;
                case TILE_RAMP_2:     ramps_.put(&nRamp, x, y, z, 0.0f); break;
                case TILE_RAMP_3:     ramps_.put(&nRamp, x, y, z, YAW_QUARTER); break;
                case TILE_RAMP_4:     ramps_.put(&nRamp, x, y, z, YAW_HALF); break;
                case TILE_LIFT:       lifts_.put(&nLift, 0.0f, 0.0f, 0.0f, 0.0f); break;
                case TILE_SLIDE_TRACK: slides_.put(&nSlide, 0.0f, 0.0f, 0.0f, 0.0f); break;
                case TILE_BREAKABLE:  breakables_.put(&nBreak, x, y, z, 0.0f); break;
                case TILE_JUMP_PAD:   jumpPads_.put(&nJump, x, y, z, 0.0f); break;
                case TILE_TELEPORTER: teleporters_.put(&nTele, x, y, z, 0.0f); break;
                case TILE_CLIMB: {
                    float yaw;
                    switch (t->climbDir()) {
                    case 1:  yaw = YAW_NEG_QUARTER; break;
                    case 2:  yaw = YAW_HALF; break;
                    case 3:  yaw = YAW_QUARTER; break;
                    default: yaw = 0.0f; break;  // 4 and anything else
                    }
                    climbs_.put(&nClimb, x, y, z, yaw);
                    break;
                }
                case TILE_SWITCH:      switches_.put(&nSwitch, x, y, z, 0.0f); break;
                case TILE_CONVEYOR:    conveyors_.put(&nConv, x, y, z, 0.0f); break;
                case TILE_DESTRUCTIBLE: destructibles_.put(&nDest, x, y, z, 0.0f); break;
                default: break;
                }
            }

    buildWalls(g, theme->sideHeight());
}

/* Every dword of the tile quad template zero but the diffuse, 0xffffffff.
 * The builder overwrites all four later. */
LevelPlacements::LevelPlacements()
{
    for (int i = 0; i < 4; i++) {
        for (int k = 0; k < 8; k++)
            tileQuad_[i].d[k] = 0;
        tileQuad_[i].d[3] = 0xffffffff;
    }
}

/* The lift and slide passes.  Counts compare unsigned.  The block itself is
 * passed as the scene renderer's quad: tileQuad heads it. */
void LevelPlacements::drawLifts(Game *g, ThemeAssetBlock *theme,
                          RenderDevice *d3d, double now)
{
    for (unsigned int i = 0; i < (unsigned int)lifts_.count_; i++) {
        const LiftObject *lift = g->liftSlot(i);
        lifts_.pos_[i][0] = lift->posU();
        lifts_.pos_[i][1] = lift->height();
        lifts_.pos_[i][2] = -lift->posV();
    }
    Scene_RenderSceneObjects(g, (SceneQuadVertex *)(void *)this,  // tileQuad, at +0
                             (const Vec3 *)lifts_.pos_, (const Vec3 *)lifts_.rot_,
                             lifts_.count_, theme->slot(THEME_OBJ_ELEVATOR),
                             d3d, now, 0.0f, 0, 0);
}

void LevelPlacements::drawSlides(Game *g, ThemeAssetBlock *theme,
                           RenderDevice *d3d, double now)
{
    for (unsigned int i = 0; i < (unsigned int)slides_.count_; i++) {
        const SlideObject *slide = g->slideSlot(i);
        slides_.pos_[i][0] = slide->posU();
        slides_.pos_[i][1] = slide->posY();
        slides_.pos_[i][2] = -slide->posV();
        if (slide->kind() == 0x0a)
            slides_.rot_[i][1] = 1.5707963705062866f;  // pi/2 as a float
    }
    float animTime = (float)fmod(now * (double)0.002f, 1.0);
    Scene_RenderSceneObjects(g, (SceneQuadVertex *)(void *)this,  // tileQuad, at +0
                             (const Vec3 *)slides_.pos_, (const Vec3 *)slides_.rot_,
                             slides_.count_, theme->slot(THEME_OBJ_PLATFORM),
                             d3d, now, animTime, 0x14, 0);
}
