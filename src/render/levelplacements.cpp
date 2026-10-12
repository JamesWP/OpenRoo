/* The level's fixed picture (levelplacements.h): the cells collected on level
 * entry, the floor and wall buffers, and the passes that draw them. */

#include <math.h>
#include <string.h>
#include "levelplacements.h"
#include "renderdevice.h"
#include "levelmap.h"
#include "tile.h"
#include "prof.h"
#include "animcode.h"

static const float YAW_NEG_QUARTER = -1.5707964f;
static const float YAW_QUARTER     =  1.5707964f;
static const float YAW_HALF        =  3.1415927f;

static uint32_t fbits(float f) { uint32_t d; memcpy(&d, &f, 4); return d; }

/* A Lit vertex: x, y, z, 0, diffuse, 0, u, v. */
static PlacementVertex vertex(float x, float y, float z, uint32_t diffuse, float u, float v)
{
    return { { fbits(x), fbits(y), fbits(z), 0, diffuse, 0, fbits(u), fbits(v) } };
}

/* The look each kind of item draws as, by the cell's contents. */
static bool item_look(unsigned char contents, ThemeObjectType *out)
{
    switch (contents) {
    case CONTENTS_CRYSTAL:     *out = THEME_OBJ_ITEM_CRYSTAL;    return true;
    case CONTENTS_PARAGLIDER:  *out = THEME_OBJ_ITEM_PARAGLIDER;  return true;
    case CONTENTS_TIME_BONUS:  *out = THEME_OBJ_ITEM_TIME_BONUS;       return true;
    case CONTENTS_EXTRA_LIFE:  *out = THEME_OBJ_ITEM_EXTRA_LIFE;       return true;
    case CONTENTS_FREEZE:      *out = THEME_OBJ_ITEM_FREEZE;     return true;
    case CONTENTS_GRANT_09:    *out = THEME_OBJ_ITEM_BOMBS; return true;
    case CONTENTS_SPEED_UP:    *out = THEME_OBJ_ITEM_SPEED_UP;      return true;
    case CONTENTS_PROTECTION:  *out = THEME_OBJ_ITEM_PROTECTION; return true;
    case 0xff:                 *out = THEME_OBJ_ITEM_RANDOM;   return true;  // CONTENTS_RANDOM
    default:                   return false;
    }
}

/* The theme conditions a cell meets: `active` while it is busy. */
static uint8_t cell_conditions(const Tile *t)
{
    return t->busy() != 0 ? draw::COND_ACTIVE : 0;
}

void LevelPlacements::release()
{
    RenderDevice::DestroyVertexBuffer(floorVb_);
    RenderDevice::DestroyVertexBuffer(wallVb_);
    floorVb_ = wallVb_ = nullptr;
    for (std::vector<Cell> &c : cells_)
        c.clear();
    floor_.clear();
    walls_.clear();
}

const VertexBuffer *LevelPlacements::floorBuffer(RenderDevice *dev)
{
    if (!floorVb_ && !floor_.empty())
        floorVb_ = dev->CreateVertexBuffer(VertexFormat::Lit, (uint32_t)floor_.size(),
                                           BufferUsage::Static, floor_.data());
    return floorVb_;
}

const VertexBuffer *LevelPlacements::wallBuffer(RenderDevice *dev)
{
    if (!wallVb_ && !walls_.empty())
        wallVb_ = dev->CreateVertexBuffer(VertexFormat::Lit, (uint32_t)walls_.size(),
                                          BufferUsage::Static, walls_.data());
    return wallVb_;
}

/* ─── building ─────────────────────────────────────────────────────────── */

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

/* One face's run along a row or column: its start and end on the edge. */
struct Run { Vec3 s, e; };

/* The run being extended along one side of a sweep. */
struct RunBuilder {
    bool open = false;
    Run  r{};

    void extend(Vec3 start, Vec3 end, std::vector<Run> *list, bool onFace)
    {
        if (!onFace) {
            close(list);
            return;
        }
        if (!open)
            r.s = start;
        r.e = end;
        open = true;
    }
    void close(std::vector<Run> *list)
    {
        if (open)
            list->push_back(r);
        open = false;
    }
};

void LevelPlacements::buildWalls(const LevelMap *map, float depth)
{
    std::vector<Run> lists[4];  // row -v, row +v, column -u, column +u
    const unsigned V = map->extentV(), U = map->extentU();
    const float H = 0.5f;

    for (unsigned L = 0; L < 0x100; ++L) {
        const float y = (float)L;
        for (unsigned v = 0; v < V; ++v) {
            RunBuilder a, b;
            for (unsigned u = 0; u < U; ++u) {
                const Tile *t = map->tile(u, v);
                const float x = (float)u, z = -(float)v;
                a.extend({ x - H, y, z + H }, { x + H, y, z + H }, &lists[0],
                         face(t, map->tile(u, (int)v - 1), L));
                b.extend({ x - H, y, z - H }, { x + H, y, z - H }, &lists[1],
                         face(t, map->tile(u, v + 1), L));
            }
            a.close(&lists[0]);
            b.close(&lists[1]);
        }
        for (unsigned u = 0; u < U; ++u) {
            RunBuilder a, b;
            for (unsigned v = 0; v < V; ++v) {
                const Tile *t = map->tile(u, v);
                const float x = (float)u, z = -(float)v;
                a.extend({ x - H, y, z + H }, { x - H, y, z - H }, &lists[2],
                         face(t, map->tile((int)u - 1, v), L));
                b.extend({ x + H, y, z + H }, { x + H, y, z - H }, &lists[3],
                         face(t, map->tile(u + 1, v), L));
            }
            a.close(&lists[2]);
            b.close(&lists[3]);
        }
    }

    // Two triangles per run, from the edge down `depth`.  The texture's v
    // runs from 1 - depth at the edge to 1 at the bottom, and its u along
    // the run.  The faces looking along -v and +u are shaded darker, and
    // each face winds to face outwards.
    const float T = 1.0f - depth;
    for (int li = 0; li < 4; ++li) {
        const uint32_t shade = (li == 0 || li == 3) ? 0xff404040 : 0xffb0b0b0;
        const bool alongU = li < 2;
        const bool flip = li == 0 || li == 3;
        for (const Run &r : lists[li]) {
            const float su = (alongU ? r.s.x : r.s.z) + H, eu = (alongU ? r.e.x : r.e.z) + H;
            const PlacementVertex S  = vertex(r.s.x, r.s.y, r.s.z, shade, su, T);
            const PlacementVertex E  = vertex(r.e.x, r.e.y, r.e.z, shade, eu, T);
            const PlacementVertex S_ = vertex(r.s.x, r.s.y - depth, r.s.z, shade, su, 1.0f);
            const PlacementVertex E_ = vertex(r.e.x, r.e.y - depth, r.e.z, shade, eu, 1.0f);
            if (flip)
                walls_.insert(walls_.end(), { E, S, E_, S, S_, E_ });
            else
                walls_.insert(walls_.end(), { S, E, E_, S_, S, E_ });
        }
    }
}

void LevelPlacements::build(const LevelMap *map, float sideHeight)
{
    release();
    const unsigned V = map->extentV(), U = map->extentU();
    const float H = 0.5f;

    // Height by height, so each list is in height order.
    for (unsigned L = 0; L < 0x100; ++L)
        for (unsigned v = 0; v < V; ++v)
            for (unsigned u = 0; u < U; ++u) {
                const Tile *t = map->tile(u, v);
                if (t->height() != L)
                    continue;
                const float x = (float)u, y = (float)L, z = -(float)v;
                Cell c{};
                c.in.pos = { x, y, z };
                c.u = (unsigned char)u;
                c.v = (unsigned char)v;
                Group g = GROUP_COUNT;
                switch (t->objectMarker()) {
                case TILE_KIND_01:
                    // The floor: two triangles over the cell.
                    floor_.insert(floor_.end(), {
                        vertex(x + H, y, z - H, 0x00ffffff, 0.0f, 1.0f),
                        vertex(x - H, y, z - H, 0x00ffffff, 0.0f, 0.0f),
                        vertex(x + H, y, z + H, 0x00ffffff, 1.0f, 1.0f),
                        vertex(x + H, y, z + H, 0x00ffffff, 1.0f, 1.0f),
                        vertex(x - H, y, z - H, 0x00ffffff, 0.0f, 0.0f),
                        vertex(x - H, y, z + H, 0x00ffffff, 1.0f, 0.0f),
                    });
                    break;
                case TILE_STICKY:     g = GROUP_STICKY; break;
                case TILE_EXIT:       g = GROUP_EXIT; break;
                case TILE_STAIRS_1:   g = GROUP_STAIRS; c.in.rot.y = YAW_NEG_QUARTER; break;
                case TILE_STAIRS_2:   g = GROUP_STAIRS; break;
                case TILE_STAIRS_3:   g = GROUP_STAIRS; c.in.rot.y = YAW_QUARTER; break;
                case TILE_STAIRS_4:   g = GROUP_STAIRS; c.in.rot.y = YAW_HALF; break;
                case TILE_FALLING:    g = GROUP_FALLING; break;
                case TILE_JUMP_PAD:   g = GROUP_JUMP_PAD; break;
                case TILE_TELEPORTER: g = GROUP_TELEPORTER; break;
                case TILE_SWITCH:     g = GROUP_SWITCH; break;
                case TILE_ICE:        g = GROUP_ICE; break;
                case TILE_BOMBABLE:   g = GROUP_BOMBABLE; break;
                case TILE_SLIDE:
                    g = GROUP_SLIDE;
                    switch (t->slideDir()) {
                    case 1:  c.in.rot.y = YAW_NEG_QUARTER; break;
                    case 2:  c.in.rot.y = YAW_HALF; break;
                    case 3:  c.in.rot.y = YAW_QUARTER; break;
                    default: break;
                    }
                    break;
                default:
                    break;
                }
                if (g != GROUP_COUNT)
                    cells_[g].push_back(c);
            }

    buildWalls(map, sideHeight);
}

/* ─── drawing ──────────────────────────────────────────────────────────── */

const std::vector<draw::Instance> &LevelPlacements::instances(Group g, const LevelMap *map)
{
    scratch_.clear();
    for (const Cell &c : cells_[g]) {
        draw::Instance in = c.in;
        const Tile *t = map->tile(c.u, c.v);
        in.conditions = cell_conditions(t);
        in.phase = t->itemPhase();
        scratch_.push_back(in);
    }
    return scratch_;
}

void LevelPlacements::group(draw::Frame &f, Group g, ThemeObjectType look,
                            const LevelMap *map, bool particles)
{
    if (cells_[g].empty())
        return;
    std::vector<draw::Instance> in = instances(g, map);
    if (g == GROUP_JUMP_PAD) {
        // The jump pads bounce: their walk cycle, every 500 ms.
        for (draw::Instance &i : in) {
            i.animCode = ANIM_WALK_FORWARD;
            i.animTime = (float)fmod(f.now * 0.002, 1.0);
        }
    }
    if (particles)
        draw::particles(f, look, in.data(), (unsigned)in.size());
    else
        draw::objects(f, look, in.data(), (unsigned)in.size());
}

/* The bombable blocks: whole, or while blasting their exploding look, which
 * fires its debris the first frame. */
void LevelPlacements::bombables(draw::Frame &f, LevelMap *map)
{
    for (const Cell &c : cells_[GROUP_BOMBABLE]) {
        Tile *t = map->tile(c.u, c.v);
        draw::Instance in = c.in;
        in.conditions = cell_conditions(t);
        in.phase = t->itemPhase();
        if (!t->blasting()) {
            draw::objects(f, THEME_OBJ_BOMBABLE, &in);
            continue;
        }
        draw::fireEffects(f, THEME_OBJ_BOMBABLE_EXPLODING, t->blastFxPending());
        t->setBlastFxPending(false);
        draw::objects(f, THEME_OBJ_BOMBABLE_EXPLODING, &in);
    }
}

void LevelPlacements::collectItems(const LevelMap *map)
{
    items_.clear();
    itemLooks_.clear();
    for (unsigned v = 0; v < map->extentV(); ++v)
        for (unsigned u = 0; u < map->extentU(); ++u) {
            const Tile *t = map->tile(u, v);
            ThemeObjectType look;
            if (!item_look(t->contents(), &look))
                continue;
            draw::Instance in;
            in.pos = { (float)u, (float)t->height(), -(float)v };
            in.conditions = cell_conditions(t);
            in.phase = t->itemPhase();
            items_.push_back(in);
            itemLooks_.push_back(look);
        }
}

void LevelPlacements::draw(draw::Frame &f, draw::Pass pass, LevelMap *map)
{
    switch (pass) {
    case draw::Pass::Opaque:
        {
            PROF_SCOPE("level mesh");
            draw::fieldBuffer(f, THEME_OBJ_FLOOR, floorBuffer(f.dev), (unsigned)floor_.size());
            draw::fieldBuffer(f, THEME_OBJ_WALL, wallBuffer(f.dev), (unsigned)walls_.size());
        }
        group(f, GROUP_SWITCH,     THEME_OBJ_SWITCH,        map, false);
        group(f, GROUP_ICE,        THEME_OBJ_ICE,           map, false);
        group(f, GROUP_STICKY,     THEME_OBJ_STICKY,          map, false);
        group(f, GROUP_FALLING,    THEME_OBJ_FALLING, map, false);
        group(f, GROUP_JUMP_PAD,   THEME_OBJ_JUMP_PAD,       map, false);
        group(f, GROUP_TELEPORTER, THEME_OBJ_TELEPORTER,    map, false);
        group(f, GROUP_STAIRS,     THEME_OBJ_STAIRS,         map, false);
        group(f, GROUP_SLIDE,      THEME_OBJ_SLIDE,         map, false);
        group(f, GROUP_EXIT,       THEME_OBJ_EXIT,          map, false);
        bombables(f, map);
        {
            PROF_SCOPE("items");
            collectItems(map);
            for (size_t i = 0; i < items_.size(); ++i)
                draw::objects(f, itemLooks_[i], &items_[i]);
        }
        break;
    case draw::Pass::Shadows:
        // The items' shadows, at the higher setting.
        if (f.opts.shadows > 1) {
            collectItems(map);
            for (size_t i = 0; i < items_.size(); ++i)
                draw::shadows(f, itemLooks_[i], &items_[i]);
        }
        break;
    case draw::Pass::Particles:
        if (f.opts.particles == 0)
            break;
        group(f, GROUP_SWITCH,     THEME_OBJ_SWITCH,        map, true);
        group(f, GROUP_BOMBABLE,   THEME_OBJ_BOMBABLE,      map, true);
        group(f, GROUP_STAIRS,     THEME_OBJ_STAIRS,         map, true);
        group(f, GROUP_ICE,        THEME_OBJ_ICE,           map, true);
        group(f, GROUP_STICKY,     THEME_OBJ_STICKY,          map, true);
        group(f, GROUP_FALLING,    THEME_OBJ_FALLING, map, true);
        group(f, GROUP_JUMP_PAD,   THEME_OBJ_JUMP_PAD,       map, true);
        group(f, GROUP_TELEPORTER, THEME_OBJ_TELEPORTER,    map, true);
        group(f, GROUP_EXIT,       THEME_OBJ_EXIT,          map, true);
        group(f, GROUP_SLIDE,      THEME_OBJ_SLIDE,         map, true);
        break;
    default:
        break;
    }
}
