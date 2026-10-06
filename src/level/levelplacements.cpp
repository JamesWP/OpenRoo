/* Every array is allocated here and freed only by the release; the renderer
 * only reads them.
 *
 * PRESERVED:
 *   1. The exit's position is not reset by the release: a level with no
 *      exit keeps the previous level's.  The last exit cell found wins.
 *   2. Lifts and platforms store all-zero positions and rotations.
 *   3. The wall-run state is not reset between rows, nor between one
 *      height's column sweep and the next height's row sweep.  A run open
 *      at the end of a row carries into the next row's first cells; one
 *      open when a sweep ends is continued by the next sweep (into its
 *      list) or dropped.  Both sweeps share the two run slots.
 *   4. Neighbour reads at the grid edge are not bounds-checked: they read
 *      the bytes either side of the row.
 *
 * The platform list's count is the Game's platform count, but it is written once per
 * platform-track cell.  The game writes past its block when a level has more
 * track cells than platforms; here the block is sized to cover every write, so
 * the overrun stays in our allocation, and count is unchanged.
 * KAROO_PLACEMENT_DIAG=1 logs the two numbers per level. */

#include <stdint.h>
#include "sysdev.h"
#include <string.h>
#include <vector>
#include <new>
#include "levelplacements.h"
#include "renderdevice.h"
#include "game.h"
#include "levelmap.h"
#include "tile.h"
#include "theme.h"
#include "logger.h"
#include "sceneobjects.h"
#include "liftobject.h"
#include "platformobject.h"
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

static uint32_t fbits(float f) { uint32_t d; memcpy(&d, &f, 4); return d; }

/* The BbVertex-shaped writes: x, y, z, 0, diffuse, 0, u, v. */
static void put(PlacementVertex *out, float x, float y, float z,
                uint32_t diffuse, float u, float v)
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
        uint32_t n = sysdev::getEnv("KAROO_PLACEMENT_DIAG", buf, sizeof buf);
        v = (n > 0 && n < sizeof buf && buf[0] == '1');
    }
    return v;
}

void PlacementList::release()
{
    pos_.reset();
    rot_.reset();
    count_ = 0;
}

/* Placement vertices are in the layout of VertexFormat::Lit. */
const VertexBuffer *LevelPlacements::kind01Buffer(RenderDevice *dev) const
{
    if (!kind01Vb_ && kind01Count_ > 0)
        kind01Vb_ = dev->CreateVertexBuffer(VertexFormat::Lit, kind01Count_ * 6,
                                            BufferUsage::Static, kind01Verts_.get());
    return kind01Vb_;
}

const VertexBuffer *LevelPlacements::wallStripBuffer(RenderDevice *dev) const
{
    if (!wallVb_ && wallStripCount_ > 0)
        wallVb_ = dev->CreateVertexBuffer(VertexFormat::Lit, wallStripCount_ * 6,
                                          BufferUsage::Static, wallVerts_.get());
    return wallVb_;
}

void LevelPlacements::release()
{
    RenderDevice::DestroyVertexBuffer(kind01Vb_);
    RenderDevice::DestroyVertexBuffer(wallVb_);
    kind01Vb_ = wallVb_ = nullptr;
    kind01Verts_.reset();
    kind01Count_ = 0;
    lifts_.release();
    platforms_.release();
    sticky_.release();
    falling_.release();
    jumpPads_.release();
    switches_.release();
    teleporters_.release();
    ramps_.release();
    slides_.release();
    ice_.release();
    bombables_.release();
    wallVerts_.reset();
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
    case TILE_KIND_01: case TILE_IMPASSABLE: case TILE_STICKY:
    case TILE_ICE: case TILE_BOMBABLE: case TILE_JUMP_PAD:
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
    wallVerts_.reset(new PlacementVertex[n * 6]());

    // Six vertices per run.  S and E are the start and end on the edge, S_ and
    // E_ the same `depth` lower; T = 1 - depth is the edge's v coordinate, 1
    // the bottom's.  Row faces take u from x, column faces from z.  The vertex
    // order, shade and coordinates differ per list.
    const float T = K_ONE - depth;
    PlacementVertex *o = wallVerts_.get();
    for (int li = 0; li < 4; ++li) {
        const uint32_t shade = (li == 0 || li == 3) ? 0xff404040 : 0xffb0b0b0;
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
    pos_.reset(new float[entries][3]());
    rot_.reset(new float[entries][3]());
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
            if (k == TILE_STICKY)         ++sticky_.count_;
            if (k == TILE_FALLING)    ++falling_.count_;
            if (k == TILE_JUMP_PAD)     ++jumpPads_.count_;
            if (k == TILE_TELEPORTER)   ++teleporters_.count_;
            if (k == TILE_SLIDE)        ++slides_.count_;
            if (k == TILE_SWITCH)       ++switches_.count_;
            if (k == TILE_ICE)     ++ice_.count_;
            if (k == TILE_BOMBABLE) ++bombables_.count_;
            if (k >= TILE_RAMP_1 && k <= TILE_RAMP_4) ++ramps_.count_;
            if (k == TILE_PLATFORM_TRACK)  ++trackCells;  // see the top of the file
        }
    platforms_.count_ = g->platformCount();

    if (diag_on())
        g_logger.write("levelplacements: platformCount=%d, platform-track cells=%u%s\n",
                   platforms_.count_, trackCells,
                   trackCells > (unsigned)platforms_.count_
                       ? " -- the original overruns its platform block" : "");

    if (kind01Count_ != 0)
        kind01Verts_.reset(new PlacementVertex[kind01Count_ * 6]());
    if (lifts_.count_ != 0)       lifts_.alloc(lifts_.count_);
    if (sticky_.count_ != 0)        sticky_.alloc(sticky_.count_);
    if (falling_.count_ != 0)  falling_.alloc(falling_.count_);
    if (jumpPads_.count_ != 0)    jumpPads_.alloc(jumpPads_.count_);
    if (teleporters_.count_ != 0) teleporters_.alloc(teleporters_.count_);
    if (switches_.count_ != 0)    switches_.alloc(switches_.count_);
    if (ramps_.count_ != 0)       ramps_.alloc(ramps_.count_);
    if (slides_.count_ != 0)      slides_.alloc(slides_.count_);
    unsigned platformCap = (unsigned)platforms_.count_ > trackCells
                            ? (unsigned)platforms_.count_ : trackCells;
    if (platformCap != 0)
        platforms_.alloc(platformCap);
    if (ice_.count_ != 0)     ice_.alloc(ice_.count_);
    if (bombables_.count_ != 0) bombables_.alloc(bombables_.count_);

    // The tile-top template.
    static const uint32_t quad[4][8] = {
        { 0x3f000000, 0, 0xbf000000, 0xffffffff, 0,          0x3f800000, 0,          0x3f800000 },
        { 0xbf000000, 0, 0xbf000000, 0xffffffff, 0,          0,          0,          0          },
        { 0x3f000000, 0, 0x3f000000, 0xffffffff, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000 },
        { 0xbf000000, 0, 0x3f000000, 0xffffffff, 0x3f800000, 0,          0x3f800000, 0          },
    };
    memcpy(tileQuad_, quad, sizeof quad);

    // Pass 2: fill, height by height, so each array is in height order.
    unsigned n01 = 0, nLift = 0, nPlatform = 0, nSticky = 0, nBreak = 0, nJump = 0,
             nTele = 0, nSwitch = 0, nRamp = 0, nSlide = 0, nIce = 0, nDest = 0;
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
                    PlacementVertex *o = kind01Verts_.get() + n01 * 6;
                    put(o++, A, y, B, 0x00ffffff, 0.0f, 1.0f);
                    put(o++, C, y, B, 0x00ffffff, 0.0f, 0.0f);
                    put(o++, A, y, D, 0x00ffffff, 1.0f, 1.0f);
                    put(o++, A, y, D, 0x00ffffff, 1.0f, 1.0f);
                    put(o++, C, y, B, 0x00ffffff, 0.0f, 0.0f);
                    put(o++, C, y, D, 0x00ffffff, 1.0f, 0.0f);
                    ++n01;
                    break;
                }
                case TILE_STICKY:       sticky_.put(&nSticky, x, y, z, 0.0f); break;
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
                case TILE_PLATFORM_TRACK: platforms_.put(&nPlatform, 0.0f, 0.0f, 0.0f, 0.0f); break;
                case TILE_FALLING:  falling_.put(&nBreak, x, y, z, 0.0f); break;
                case TILE_JUMP_PAD:   jumpPads_.put(&nJump, x, y, z, 0.0f); break;
                case TILE_TELEPORTER: teleporters_.put(&nTele, x, y, z, 0.0f); break;
                case TILE_SLIDE: {
                    float yaw;
                    switch (t->slideDir()) {
                    case 1:  yaw = YAW_NEG_QUARTER; break;
                    case 2:  yaw = YAW_HALF; break;
                    case 3:  yaw = YAW_QUARTER; break;
                    default: yaw = 0.0f; break;  // 4 and anything else
                    }
                    slides_.put(&nSlide, x, y, z, yaw);
                    break;
                }
                case TILE_SWITCH:      switches_.put(&nSwitch, x, y, z, 0.0f); break;
                case TILE_ICE:    ice_.put(&nIce, x, y, z, 0.0f); break;
                case TILE_BOMBABLE: bombables_.put(&nDest, x, y, z, 0.0f); break;
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

/* The lift and platform passes.  Counts compare unsigned.  The block itself is
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
                             (const Vec3 *)lifts_.pos_.get(), (const Vec3 *)lifts_.rot_.get(),
                             lifts_.count_, theme->slot(THEME_OBJ_ELEVATOR),
                             d3d, now, 0.0f, 0, 0);
}

void LevelPlacements::drawPlatforms(Game *g, ThemeAssetBlock *theme,
                           RenderDevice *d3d, double now)
{
    for (unsigned int i = 0; i < (unsigned int)platforms_.count_; i++) {
        const PlatformObject *platform = g->platformSlot(i);
        platforms_.pos_[i][0] = platform->posU();
        platforms_.pos_[i][1] = platform->posY();
        platforms_.pos_[i][2] = -platform->posV();
        if (platform->kind() == 0x0a)
            platforms_.rot_[i][1] = 1.5707963705062866f;  // pi/2 as a float
    }
    float animTime = (float)fmod(now * (double)0.002f, 1.0);
    Scene_RenderSceneObjects(g, (SceneQuadVertex *)(void *)this,  // tileQuad, at +0
                             (const Vec3 *)platforms_.pos_.get(), (const Vec3 *)platforms_.rot_.get(),
                             platforms_.count_, theme->slot(THEME_OBJ_PLATFORM),
                             d3d, now, animTime, 0x14, 0);
}
