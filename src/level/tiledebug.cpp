#include "tiledebug.h"
#include "cheatcode.h"
#include "dbg.h"
#include "game.h"
#include "levelmap.h"
#include "tile.h"
#include "worldstate.h"  // WS_DIR_*
#include <algorithm>
#include <vector>

static const char *kind_name(uint8_t k)
{
    switch (k) {
    case TILE_EMPTY:        return "void";
    case TILE_KIND_01:      return "floor";
    case TILE_STICKY:       return "sticky";
    case TILE_START:        return "start";
    case TILE_EXIT:         return "exit";
    case TILE_STAIRS_1: case TILE_STAIRS_2: case TILE_STAIRS_3: case TILE_STAIRS_4:
                            return "stairs";
    case TILE_LIFT:         return "lift";
    case TILE_PLATFORM_U: case TILE_PLATFORM_V:
                            return "platform";
    case TILE_PLATFORM_TRACK: return "platform track";
    case TILE_FALLING:      return "falling tile";
    case TILE_JUMP_PAD:     return "jump pad";
    case TILE_TELEPORTER:   return "teleporter";
    case TILE_SLIDE:        return "slide";
    case TILE_SWITCH:       return "switch";
    case TILE_BRIDGE_U: case TILE_BRIDGE_V:
                            return "bridge";
    case TILE_ICE:          return "ice";
    case TILE_IMPASSABLE:   return "impassable";
    case TILE_BOMBABLE:     return "bombable";
    default:                return "?";
    }
}

const char *Tile_ContentsName(uint8_t c)
{
    switch (c) {
    case CONTENTS_NONE:        return NULL;
    case CONTENTS_CRYSTAL:     return "crystal";
    case CONTENTS_PARAGLIDER:  return "paraglider";
    case CONTENTS_TIME_BONUS:  return "time bonus";
    case CONTENTS_EXTRA_LIFE:  return "extra life";
    case CONTENTS_FREEZE:      return "freeze";
    case CONTENTS_GRANT_09:    return "3 bombs";
    case CONTENTS_SPEED_UP:    return "speed up";
    case CONTENTS_REVERSED:    return "reversed controls";
    case CONTENTS_SPEED_DOWN:  return "slow down";
    case CONTENTS_TRANSFORM:   return "transform";
    case CONTENTS_FREE_BOMB:   return "bomb";
    case CONTENTS_TIMED_SPAWN: return "foe spawner";
    default:                   return "item";
    }
}

/* Floor: grey by height. */
static ImU32 floor_color(uint8_t height, int maxH)
{
    const float l = 0.3f + 0.55f * (maxH > 0 ? (float)height / maxH : 0.0f);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(l, l, l, 1));
}

#define SLIDE_COLOR IM_COL32(40, 170, 160, 255)

/* Each kind's mark, drawn over the floor in the cell [a, b]: shapes and
 * borders rather than colour alone, so kinds stay apart at any height. */
static void draw_kind_mark(ImDrawList *dl, uint8_t kind, ImVec2 a, ImVec2 b)
{
    const float w = b.x - a.x, t = std::max(1.0f, w * 0.12f);
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    auto in = [&](float f) { return std::make_pair(ImVec2(a.x + w * f, a.y + w * f), ImVec2(b.x - w * f, b.y - w * f)); };
    switch (kind) {
    case TILE_EXIT:
        dl->AddRect(a, b, IM_COL32(40, 230, 40, 255), 0.0f, 0, t * 1.5f);
        break;
    case TILE_STICKY: {
        auto r = in(0.15f);
        dl->AddRectFilled(r.first, r.second, IM_COL32(220, 200, 40, 200), w * 0.3f);
        break;
    }
    case TILE_STAIRS_1: case TILE_STAIRS_2: case TILE_STAIRS_3: case TILE_STAIRS_4: {
        // Steps, walkable both ways: three treads across the slope, widest at
        // the foot and narrowest at the top.  kind - 4 is the way up.
        const int d = kind - 4;
        const ImVec2 f(WS_DIR_DU[d] * w, WS_DIR_DV[d] * w);  // one cell uphill
        const ImVec2 s(-f.y, f.x);
        for (int i = 0; i < 3; i++) {
            const float along = -0.3f + 0.3f * i, half = 0.4f - 0.1f * i;
            const ImVec2 m(c.x + f.x * along, c.y + f.y * along);
            dl->AddLine(ImVec2(m.x - s.x * half, m.y - s.y * half),
                        ImVec2(m.x + s.x * half, m.y + s.y * half), IM_COL32(80, 80, 80, 255), t * 1.3f);
        }
        break;
    }
    case TILE_LIFT: {
        auto r = in(0.15f);
        dl->AddRect(r.first, r.second, IM_COL32(60, 120, 255, 255), 0.0f, 0, t);
        dl->AddLine(ImVec2(c.x, r.first.y + t), ImVec2(c.x, r.second.y - t), IM_COL32(60, 120, 255, 255), t);
        break;
    }
    case TILE_PLATFORM_U: case TILE_PLATFORM_V: case TILE_PLATFORM_TRACK: {
        // The track along its axis; the platform's own cell is a solid block.
        const bool alongU = kind != TILE_PLATFORM_V;
        const ImU32 col = IM_COL32(60, 120, 255, 255);
        if (alongU) dl->AddLine(ImVec2(a.x, c.y), ImVec2(b.x, c.y), col, t);
        else        dl->AddLine(ImVec2(c.x, a.y), ImVec2(c.x, b.y), col, t);
        if (kind != TILE_PLATFORM_TRACK) {
            auto r = in(0.25f);
            dl->AddRectFilled(r.first, r.second, col);
        }
        break;
    }
    case TILE_FALLING: {
        auto r = in(0.1f);
        dl->AddRect(r.first, r.second, IM_COL32(200, 110, 40, 255), 0.0f, 0, t);
        dl->AddLine(r.first, r.second, IM_COL32(200, 110, 40, 255), t);
        break;
    }
    case TILE_JUMP_PAD:
        dl->AddCircle(c, w * 0.32f, IM_COL32(190, 70, 230, 255), 0, t * 1.5f);
        break;
    case TILE_TELEPORTER:
        dl->AddQuadFilled(ImVec2(c.x, a.y + w * 0.12f), ImVec2(b.x - w * 0.12f, c.y),
                          ImVec2(c.x, b.y - w * 0.12f), ImVec2(a.x + w * 0.12f, c.y), IM_COL32(190, 70, 230, 255));
        break;
    case TILE_SLIDE:
        // The legend's swatch; the map draws slides as chains (draw_slides).
        dl->AddLine(ImVec2(a.x + w * 0.1f, c.y), ImVec2(b.x - w * 0.35f, c.y), SLIDE_COLOR, w * 0.3f);
        dl->AddTriangleFilled(ImVec2(b.x - w * 0.05f, c.y), ImVec2(b.x - w * 0.4f, a.y + w * 0.15f),
                              ImVec2(b.x - w * 0.4f, b.y - w * 0.15f), SLIDE_COLOR);
        break;
    case TILE_SWITCH: {
        auto r = in(0.3f);
        dl->AddRectFilled(r.first, r.second, IM_COL32(230, 40, 40, 255));
        dl->AddRect(r.first, r.second, IM_COL32(0, 0, 0, 255), 0.0f, 0, 1.0f);
        break;
    }
    case TILE_BRIDGE_U: case TILE_BRIDGE_V: {
        // Two rails along the bridge's axis, with planks across.
        const ImU32 col = IM_COL32(150, 95, 45, 255);
        if (kind == TILE_BRIDGE_U) {
            dl->AddRectFilled(ImVec2(a.x, a.y + w * 0.2f), ImVec2(b.x, b.y - w * 0.2f), IM_COL32(190, 140, 80, 255));
            dl->AddLine(ImVec2(a.x, a.y + w * 0.2f), ImVec2(b.x, a.y + w * 0.2f), col, t);
            dl->AddLine(ImVec2(a.x, b.y - w * 0.2f), ImVec2(b.x, b.y - w * 0.2f), col, t);
        } else {
            dl->AddRectFilled(ImVec2(a.x + w * 0.2f, a.y), ImVec2(b.x - w * 0.2f, b.y), IM_COL32(190, 140, 80, 255));
            dl->AddLine(ImVec2(a.x + w * 0.2f, a.y), ImVec2(a.x + w * 0.2f, b.y), col, t);
            dl->AddLine(ImVec2(b.x - w * 0.2f, a.y), ImVec2(b.x - w * 0.2f, b.y), col, t);
        }
        break;
    }
    case TILE_ICE:
        for (int i = 0; i < 2; i++) {
            const float x = a.x + w * (0.25f + 0.3f * i);
            dl->AddLine(ImVec2(x, a.y + w * 0.25f), ImVec2(x + w * 0.2f, c.y), IM_COL32(40, 40, 40, 255), t);
            dl->AddLine(ImVec2(x + w * 0.2f, c.y), ImVec2(x, b.y - w * 0.25f), IM_COL32(40, 40, 40, 255), t);
        }
        break;
    case TILE_IMPASSABLE: {
        auto r = in(0.08f);
        dl->AddRectFilled(r.first, r.second, IM_COL32(90, 30, 30, 255));
        dl->AddLine(r.first, r.second, IM_COL32(200, 60, 60, 255), t);
        dl->AddLine(ImVec2(r.second.x, r.first.y), ImVec2(r.first.x, r.second.y), IM_COL32(200, 60, 60, 255), t);
        break;
    }
    case TILE_BOMBABLE: {
        auto r = in(0.08f);
        dl->AddRectFilled(r.first, r.second, IM_COL32(150, 95, 45, 255));
        dl->AddRect(r.first, r.second, IM_COL32(70, 40, 15, 255), 0.0f, 0, t);
        break;
    }
    default:
        break;
    }
}

static ImU32 contents_color(uint8_t c)
{
    switch (c) {
    case CONTENTS_CRYSTAL:    return IM_COL32(80, 220, 255, 255);
    case CONTENTS_EXTRA_LIFE: return IM_COL32(255, 60, 80, 255);
    case CONTENTS_FREE_BOMB:
    case CONTENTS_GRANT_09:   return IM_COL32(40, 40, 40, 255);
    default:                  return IM_COL32(255, 200, 40, 255);
    }
}

/* A slide's direction byte is not the WS_DIR convention: foe pathing leaves a
 * direction-1 slide towards increasing V, and the Enemy Factory's slides
 * carry the foes away from their spawners.  Each points opposite WS_DIR. */
static const int SLIDE_DU[5] = { 0,  0, -1,  0, +1 };
static const int SLIDE_DV[5] = { 0, +1,  0, -1,  0 };

static void draw_slides(const dbg::MapView &m, LevelMap *map)
{
    ImDrawList *dl = m.dl;
    const unsigned cols = m.cols, rows = m.rows;
    const float cell = m.cell;
    auto centre = [&](float u, float v) { return m.centre(u, v); };
    auto dirAt = [&](int u, int v) -> int {
        if (u < 0 || v < 0 || u >= (int)cols || v >= (int)rows)
            return 0;
        const Tile *t = map->tile(u, v);
        const int d = t->slideDir();
        return t->objectMarker() == TILE_SLIDE && d >= WS_DIR_MIN && d <= WS_DIR_MAX ? d : 0;
    };
    std::vector<bool> fed(cols * rows), done(cols * rows);
    for (unsigned u = 0; u < cols; u++)
        for (unsigned v = 0; v < rows; v++)
            if (const int d = dirAt(u, v)) {
                const int nu = u + WS_DIR_DU[d], nv = v + WS_DIR_DV[d];
                if (dirAt(nu, nv))
                    fed[nv * cols + nu] = true;
            }
    const float width = cell * 0.3f;
    // Starts first, then whatever is left (closed loops).
    for (int pass = 0; pass < 2; pass++)
        for (unsigned u0 = 0; u0 < cols; u0++)
            for (unsigned v0 = 0; v0 < rows; v0++) {
                if (!dirAt(u0, v0) || done[v0 * cols + u0] || (pass == 0 && fed[v0 * cols + u0]))
                    continue;
                std::vector<ImVec2> pts;
                int u = u0, v = v0, d = dirAt(u, v);
                const ImVec2 c0 = centre(u, v);
                pts.push_back(ImVec2(c0.x - WS_DIR_DU[d] * cell * 0.45f, c0.y - WS_DIR_DV[d] * cell * 0.45f));
                for (;;) {
                    done[v * cols + u] = true;
                    pts.push_back(centre(u, v));
                    d = dirAt(u, v);
                    const int nu = u + WS_DIR_DU[d], nv = v + WS_DIR_DV[d];
                    if (!dirAt(nu, nv) || done[nv * cols + nu])
                        break;
                    u = nu;
                    v = nv;
                }
                const ImVec2 last = pts.back();
                const ImVec2 f(WS_DIR_DU[d] * cell, WS_DIR_DV[d] * cell), side(-f.y, f.x);
                const ImVec2 base(last.x + f.x * 0.2f, last.y + f.y * 0.2f);
                pts.push_back(base);
                dl->AddPolyline(pts.data(), (int)pts.size(), SLIDE_COLOR, 0, width);
                dl->AddTriangleFilled(ImVec2(last.x + f.x * 0.55f, last.y + f.y * 0.55f),
                                      ImVec2(base.x + side.x * 0.3f, base.y + side.y * 0.3f),
                                      ImVec2(base.x - side.x * 0.3f, base.y - side.y * 0.3f), SLIDE_COLOR);
            }
}


static void kind_swatch(ImDrawList *dl, ImVec2 a, ImVec2 b, void *ctx)
{
    dl->AddRectFilled(a, b, floor_color(1, 2));
    draw_kind_mark(dl, (uint8_t)(uintptr_t)ctx, a, b);
}

static void contents_swatch(ImDrawList *dl, ImVec2 a, ImVec2 b, void *ctx)
{
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    dl->AddCircleFilled(c, (b.x - a.x) * 0.2f, contents_color((uint8_t)(uintptr_t)ctx));
}

/* Legend groups: what the tiles are made of, what they hold, then what moves
 * on them (the entities add theirs). */
enum { LEGEND_KINDS = 0, LEGEND_CONTENTS = 1 };

void Tile_DebugDrawMap(Game *game)
{
    if (!dbg::active())
        return;
    LevelMap *map = game->map();
    const unsigned cols = map->extentU(), rows = map->extentV();
    dbg::MapLayer layer(cols, rows);
    if (!layer)
        return;
    const dbg::MapView &m = *layer;
    ImDrawList *dl = m.dl;

    int maxH = 1;
    for (unsigned u = 0; u < cols; u++)
        for (unsigned v = 0; v < rows; v++)
            maxH = std::max(maxH, (int)(m.asLoaded ? map->snapshot((int)u, (int)v)->height()
                                                    : map->tile((int)u, (int)v)->height()));

    bool seen[256] = {}, seenContents[256] = {};
    for (unsigned u = 0; u < cols; u++)
        for (unsigned v = 0; v < rows; v++) {
            const Tile *t  = map->tile((int)u, (int)v);
            const Tile *sn = map->snapshot((int)u, (int)v);
            const uint8_t kind = t->objectMarker();
            const uint8_t h = m.asLoaded ? sn->height() : t->height();
            const uint8_t c = m.asLoaded ? sn->contents() : t->contents();
            if (h == 0 && kind == TILE_EMPTY)
                continue;  // no floor here
            seen[kind] = true;
            if (Tile_ContentsName(c))
                seenContents[c] = true;
            dl->AddRectFilled(m.corner((float)u, (float)v), m.corner(u + 1.0f, v + 1.0f), floor_color(h, maxH));
            if (kind != TILE_SLIDE)  // drawn as chains below
                draw_kind_mark(dl, kind, m.corner((float)u, (float)v), m.corner(u + 1.0f, v + 1.0f));
            if (Tile_ContentsName(c))
                dl->AddCircleFilled(m.centre((float)u, (float)v), m.cell * 0.2f, contents_color(c));
        }

    draw_slides(m, map);

    // Teleporters to their partners, once per pair.
    for (unsigned u = 0; u < cols; u++)
        for (unsigned v = 0; v < rows; v++) {
            Tile *t = map->tile((int)u, (int)v);
            if (t->objectMarker() != TILE_TELEPORTER)
                continue;
            const unsigned tu = t->teleportU(), tv = t->teleportV();
            if (tu >= cols || tv >= rows || (tu == u && tv == v))
                continue;
            const bool paired = map->tile((int)tu, (int)tv)->teleportU() == u &&
                                map->tile((int)tu, (int)tv)->teleportV() == v;
            if (!m.links && !m.hovered((int)u, (int)v) && !m.hovered((int)tu, (int)tv))
                continue;
            if (paired && (tu < u || (tu == u && tv < v)) && !m.hovered((int)u, (int)v))
                continue;  // drawn from the other end
            dl->AddLine(m.centre((float)u, (float)v), m.centre((float)tu, (float)tv),
                        IM_COL32(190, 70, 230, 220), std::max(1.0f, m.cell * 0.08f));
            if (!paired)  // one-way: mark the destination
                dl->AddCircle(m.centre((float)tu, (float)tv), m.cell * 0.25f, IM_COL32(190, 70, 230, 220), 0, 2.0f);
        }

    // The tile under the mouse, and a click to go there.
    if (m.hoverU >= 0) {
        const int u = m.hoverU, v = m.hoverV;
        const Tile *t  = map->tile(u, v);
        const Tile *sn = map->snapshot(u, v);
        const uint8_t kind = t->objectMarker();
        m.tip("U%d V%d  %s (0x%02x)", u, v, kind_name(kind), kind);
        m.tip("height %u (loaded %u)", t->height(), sn->height());
        if (kind == TILE_LIFT)
            m.tip("lift at %.2f", t->liftLiveHeight());
        m.tip("param %u (loaded %u)", t->param(), sn->param());
        const char *now = Tile_ContentsName(t->contents()), *was = Tile_ContentsName(sn->contents());
        m.tip("contents %s (loaded %s)", now ? now : "-", was ? was : "-");
        if (kind == TILE_SLIDE)
            m.tip("slide direction %u", t->slideDir());
        if (t->occupant())
            m.tip("occupied (%u)", t->occupant());
        if (t->busy())
            m.tip("spent / armed");
        int cu, cv;
        if (m.clicked(&cu, &cv) && (t->height() != 0 || kind != TILE_EMPTY))
            Cheat_TeleportPlayer(game, (unsigned char)cu, (unsigned char)cv);
    }

    // Only what this level has.
    static const uint8_t KINDS[] = {
        TILE_EXIT, TILE_STICKY, TILE_STAIRS_1, TILE_LIFT, TILE_PLATFORM_U, TILE_PLATFORM_TRACK,
        TILE_FALLING, TILE_JUMP_PAD, TILE_TELEPORTER, TILE_SLIDE, TILE_SWITCH,
        TILE_ICE, TILE_IMPASSABLE, TILE_BOMBABLE,
    };
    for (uint8_t k : KINDS) {
        bool any = seen[k];
        if (k == TILE_STAIRS_1) any = seen[5] || seen[6] || seen[7] || seen[8];
        if (k == TILE_PLATFORM_U) any = seen[TILE_PLATFORM_U] || seen[TILE_PLATFORM_V];
        if (any)
            m.legend(LEGEND_KINDS, kind_name(k), kind_swatch, (void *)(uintptr_t)k);
    }
    for (int c = 1; c < 256; c++)
        if (seenContents[c])
            m.legend(LEGEND_CONTENTS, Tile_ContentsName((uint8_t)c), contents_swatch, (void *)(uintptr_t)c);
}
