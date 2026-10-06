#include "debugui.h"
#include "imgui.h"
#include "image.h"
#include "bridgeobject.h"
#include "cheatcode.h"
#include "levelmap.h"
#include "switchcells.h"
#include "tile.h"
#include "game.h"
#include "prof.h"
#include "renderdevice.h"
#include "windev.h"
#include "worldstate.h"
#include <algorithm>
#include <float.h>
#include <functional>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>
// Last: it defines LINE_MAX, which the game's headers use as a name.
#include "imgui_internal.h"

namespace debugui {

// ── The renderer: ImDrawData through RenderDevice ──

/* Brings one of ImGui's textures up to date: the font atlas, which it grows
 * and rewrites as glyphs are needed. */
static void update_texture(RenderDevice &dev, ImTextureData *tex)
{
    DeviceTexture *t = (DeviceTexture *)(uintptr_t)tex->TexID;
    switch (tex->Status) {
    case ImTextureStatus_WantCreate:
    case ImTextureStatus_WantUpdates: {
        Image img;
        img.width      = tex->Width;
        img.height     = tex->Height;
        img.sourceBits = 32;
        img.hasAlpha   = true;
        strcpy(img.name, "imgui");
        img.rgba.resize((size_t)tex->Width * tex->Height * 4);
        const uint8_t *src = (const uint8_t *)tex->GetPixels();
        if (tex->Format == ImTextureFormat_RGBA32) {
            memcpy(img.rgba.data(), src, img.rgba.size());
        } else {  // Alpha8: white, with the glyph coverage as alpha
            for (size_t i = 0; i < img.rgba.size() / 4; i++) {
                uint8_t *p = &img.rgba[i * 4];
                p[0] = p[1] = p[2] = 255;
                p[3] = src[i];
            }
        }
        if (tex->Status == ImTextureStatus_WantCreate) {
            t = dev.CreateTexture(img, TextureFlag::Alpha, 32);
            tex->SetTexID((ImTextureID)(uintptr_t)t);
        } else if (t) {
            dev.UpdateTexture(t, img);
        }
        tex->SetStatus(ImTextureStatus_OK);
        break;
    }
    case ImTextureStatus_WantDestroy:
        if (tex->UnusedFrames > 0) {
            RenderDevice::DestroyTexture(t);
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
        break;
    default:
        break;
    }
}

/* ImGui's colours are 0xAABBGGRR; the device's are 0xAARRGGBB. */
static uint32_t device_color(ImU32 c)
{
    return (c & 0xFF00FF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16);
}

static void render_draw_data(RenderDevice &dev, ImDrawData *dd)
{
    if (dd->Textures)
        for (ImTextureData *tex : *dd->Textures)
            if (tex->Status != ImTextureStatus_OK)
                update_texture(dev, tex);
    if (dd->DisplaySize.x <= 0.0f || dd->DisplaySize.y <= 0.0f)
        return;

    // ImGui lays out in the window's units; the overlay draws in the window's
    // native pixels.
    const float sx = (float)dev.overlayWidth() / dd->DisplaySize.x;
    const float sy = (float)dev.overlayHeight() / dd->DisplaySize.y;

    dev.SetBlend(BlendState::alpha());
    dev.SetDepth({ false, false });
    dev.SetStencil(StencilState());
    dev.SetRaster({ CullMode::None });
    SamplerState sampler;
    sampler.mag = sampler.min = Filter::Linear;
    sampler.u = sampler.v = AddressMode::Clamp;
    dev.SetSampler(0, sampler);

    std::vector<ScreenVertex> verts;
    std::vector<uint16_t> indices;
    for (const ImDrawList *list : dd->CmdLists) {
        verts.resize(list->VtxBuffer.Size);
        for (int i = 0; i < list->VtxBuffer.Size; i++) {
            const ImDrawVert &v = list->VtxBuffer[i];
            // Screen vertices put whole pixels at half-pixel coordinates.
            verts[i] = { (v.pos.x - dd->DisplayPos.x) * sx - 0.5f,
                         (v.pos.y - dd->DisplayPos.y) * sy - 0.5f,
                         0.0f, 1.0f, device_color(v.col), 0, v.uv.x, v.uv.y };
        }
        for (const ImDrawCmd &cmd : list->CmdBuffer) {
            if (cmd.UserCallback || cmd.ElemCount == 0)
                continue;
            const float x0 = std::max((cmd.ClipRect.x - dd->DisplayPos.x) * sx, 0.0f);
            const float y0 = std::max((cmd.ClipRect.y - dd->DisplayPos.y) * sy, 0.0f);
            const float x1 = std::min((cmd.ClipRect.z - dd->DisplayPos.x) * sx, (float)dev.overlayWidth());
            const float y1 = std::min((cmd.ClipRect.w - dd->DisplayPos.y) * sy, (float)dev.overlayHeight());
            if (x1 <= x0 || y1 <= y0)
                continue;
            dev.SetScissor({ true, (int)floorf(x0), (int)floorf(y0),
                             (int)ceilf(x1) - (int)floorf(x0), (int)ceilf(y1) - (int)floorf(y0) });
            dev.SetTexture(0, (const DeviceTexture *)(uintptr_t)cmd.GetTexID());

            // Only the vertices this command names go to the device, with
            // its indices counted from the first of them.
            const ImDrawIdx *idx = list->IdxBuffer.Data + cmd.IdxOffset;
            ImDrawIdx lo = idx[0], hi = idx[0];
            for (unsigned i = 1; i < cmd.ElemCount; i++) {
                lo = std::min(lo, idx[i]);
                hi = std::max(hi, idx[i]);
            }
            indices.resize(cmd.ElemCount);
            for (unsigned i = 0; i < cmd.ElemCount; i++)
                indices[i] = (uint16_t)(idx[i] - lo);
            dev.DrawIndexed(Prim::TriangleList, VertexFormat::Screen, &verts[lo],
                            (uint32_t)hi - lo + 1, indices.data(), cmd.ElemCount);
        }
    }
    dev.SetScissor(ScissorState());
    dev.SetTexture(0, nullptr);
}

// ── The panels ──

/* A read-only view of the live Game, taken once a frame while the overlay is
 * up (large, so static). */
static Observation g_obs;
static bool        g_obsValid;

static bool g_holdFreeze;

/* The level to load, from the same table the level select reads. */
static void draw_level_picker(Game *g)
{
    if (ImGui::Button("Load level..."))
        ImGui::OpenPopup("levels");
    if (ImGui::BeginPopup("levels")) {
        ImGui::BeginChild("list", ImVec2(260, 300));
        for (unsigned i = 0; i < g->levelCount(); i++) {
            ImGui::PushID((int)i);
            char label[300];
            snprintf(label, sizeof(label), "%3u  %s", i + 1, g->levelNameTableEntry((unsigned char)i));
            if (ImGui::Selectable(label, i == g->levelIndex())) {
                Cheat_LoadLevel(g, (unsigned char)i);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
}

static void draw_overview(RenderDevice &dev)
{
    Game *g = Game::instance();
    const ImGuiIO &io = ImGui::GetIO();
    ImGui::Text("%.1f fps  %.2f ms", io.Framerate, 1000.0f / io.Framerate);
    ImGui::TextDisabled("display %ux%u, %u-bit", dev.width(), dev.height(), dev.bitDepth());
    if (!g)
        return;
    ImGui::Separator();
    ImGui::Text("level %u/%u: %s", g->levelIndex() + 1, g->levelCount(), g->levelName());
    draw_level_picker(g);

    const Observation &obs = g_obs;
    if (g_obsValid) {
        ImGui::Text("gems %d / %d   lives %u   foes killed %u", obs.gems_collected,
                    obs.gems_required, obs.lives, obs.foes_killed);
        ImGui::Text("player U%u V%u H%u   exit U%u V%u H%u", obs.player_cell[0],
                    obs.player_cell[1], obs.player_cell[2], obs.exit_cell[0],
                    obs.exit_cell[1], obs.exit_cell[2]);
        ImGui::Text("%u foes, %u bombs%s", obs.n_foes, obs.n_enemies,
                    obs.level_complete ? "   [complete]" : "");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Cheats");
    if (ImGui::Button("+life"))      Cheat_AddLife(g, 1);
    ImGui::SameLine();
    if (ImGui::Button("+10 bombs"))  Cheat_AddBombs(g);
    ImGui::SameLine();
    if (ImGui::Button("+glide"))     Cheat_AddGlide(g);
    if (ImGui::Button("invulnerable")) Cheat_Invulnerable(g);
    ImGui::SameLine();
    if (ImGui::Button("kill foes"))  Cheat_KillFoes(g);
    ImGui::Checkbox("hold foes frozen", &g_holdFreeze);
    if (g_holdFreeze)
        Cheat_FreezeFoes(g);
}

/* One scope and, under it, the scopes it opened.  Indented as the code nests
 * them; the bar is the share of the whole frame. */
static void draw_prof_node(const prof::Node *nodes, unsigned count, int idx, double frameMs)
{
    const prof::Node &n = nodes[idx];
    bool leaf = true;
    for (unsigned i = 0; i < count; i++)
        if (nodes[i].parent == idx && prof::recent(nodes[i])) {
            leaf = false;
            break;
        }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (leaf)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    const bool idle = n.calls == 0;
    if (idle)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool open = ImGui::TreeNodeEx(n.name, flags);
    ImGui::TableNextColumn();
    ImGui::Text("%.2f", n.avgMs);
    ImGui::TableNextColumn();
    if (!leaf)
        ImGui::Text("%.2f", n.selfAvgMs);
    ImGui::TableNextColumn();
    ImGui::Text("%.2f", n.maxMs);
    ImGui::TableNextColumn();
    if (n.calls > 1)
        ImGui::Text("x%u", n.calls);
    ImGui::TableNextColumn();
    ImGui::ProgressBar(frameMs > 0.0 ? (float)(n.avgMs / frameMs) : 0.0f, ImVec2(-FLT_MIN, 8), "");
    if (idle)
        ImGui::PopStyleColor();
    if (open && !leaf) {
        for (unsigned i = 0; i < count; i++)
            if (nodes[i].parent == idx && prof::recent(nodes[i]))
                draw_prof_node(nodes, count, (int)i, frameMs);
        ImGui::TreePop();
    }
}

static void draw_frame_breakdown()
{
    unsigned count;
    const prof::Node *nodes = prof::nodes(&count);
    if (count == 0) {
        ImGui::TextDisabled("waiting for a frame");
        return;
    }
    if (ImGui::Button("reset"))
        prof::reset();
    ImGui::SameLine();
    ImGui::TextDisabled("ms; self excludes child scopes");
    // Opaque, so the stripes cannot wash out to white.
    ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
    if (ImGui::BeginTable("prof", 6, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("scope", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("avg", ImGuiTableColumnFlags_WidthFixed, 44);
        ImGui::TableSetupColumn("self", ImGuiTableColumnFlags_WidthFixed, 44);
        ImGui::TableSetupColumn("max", ImGuiTableColumnFlags_WidthFixed, 44);
        ImGui::TableSetupColumn("n", ImGuiTableColumnFlags_WidthFixed, 36);
        ImGui::TableSetupColumn("share", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableHeadersRow();
        for (unsigned i = 0; i < count; i++)
            if (nodes[i].parent < 0)
                draw_prof_node(nodes, count, (int)i, nodes[i].avgMs);
        ImGui::EndTable();
    }
    ImGui::PopStyleColor(2);
}


// ── The map ──

static bool g_showMap = true;
static bool g_mapInitial;  // the level as loaded, not as it is now
static bool g_showLinks;   // every switch and teleporter link, not just the hovered one

static const char *kind_name(uint8_t k)
{
    switch (k) {
    case TILE_EMPTY:        return "void";
    case TILE_KIND_01:      return "floor";
    case TILE_STICKY:       return "sticky";
    case TILE_START:        return "start";
    case TILE_EXIT:         return "exit";
    case TILE_RAMP_1: case TILE_RAMP_2: case TILE_RAMP_3: case TILE_RAMP_4:
                            return "ramp";
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

static const char *contents_name(uint8_t c)
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
    case TILE_RAMP_1: case TILE_RAMP_2: case TILE_RAMP_3: case TILE_RAMP_4: {
        // An arrow up the ramp: kind - 4 is its direction.
        const int d = kind - 4;
        const ImVec2 f(WS_DIR_DU[d] * w * 0.35f, WS_DIR_DV[d] * w * 0.35f);
        const ImVec2 s(-f.y, f.x);
        dl->AddTriangleFilled(ImVec2(c.x + f.x, c.y + f.y), ImVec2(c.x - f.x + s.x, c.y - f.y + s.y),
                              ImVec2(c.x - f.x - s.x, c.y - f.y - s.y), IM_COL32(90, 90, 90, 255));
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

/* Slides as chains: from each cell no slide feeds into, follow the cells'
 * directions while they lead onto further slides, and draw the run as one
 * band through the cell centres, from the edge it is entered by to an
 * arrowhead past the edge it leaves by.  Bends follow the tiles.  The
 * direction byte is the movement code's (WS_DIR): it becomes the move. */
/* Slides as chains: from each cell no slide feeds into, follow the cells'
 * directions while they lead onto further slides, and draw the run as one
 * band through the cell centres, from the edge it is entered by to an
 * arrowhead past the edge it leaves by.  Bends follow the tiles. */
/* A slide's direction byte is not the WS_DIR convention: foe pathing leaves a
 * direction-1 slide towards increasing V, and the Enemy Factory's slides
 * carry the foes away from their spawners.  Each points opposite WS_DIR. */
static const int SLIDE_DU[5] = { 0,  0, -1,  0, +1 };
static const int SLIDE_DV[5] = { 0, +1,  0, -1,  0 };

static void draw_slides(ImDrawList *dl, LevelMap *map, unsigned cols, unsigned rows,
                        float cell, const std::function<ImVec2(float, float)> &centre)
{
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

static void entity_tooltip(const char *what, const WsEntity &e, bool foe)
{
    ImGui::Separator();
    ImGui::Text("%s #%u at U%u V%u H%u", what, e.slot, e.gu, e.gv, e.gh);
    if (foe) {
        ImGui::Text("kind %u, behaviour %u, facing %u%s", e.kind, e.subtype, e.facing,
                    e.moving ? ", moving" : "");
        ImGui::Text("home U%u V%u H%u, drops %s", e.su, e.sv, e.sh,
                    contents_name(e.category) ? contents_name(e.category) : "nothing");
    }
    if (e.frozen)
        ImGui::TextUnformatted("frozen");
    if (e.hidden)
        ImGui::TextUnformatted("dying");
}

static void draw_map()
{
    ImGui::SetNextWindowPos(ImVec2(540, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(660, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Map", &g_showMap)) {
        ImGui::End();
        return;
    }
    const Observation &o = g_obs;
    if (!g_obsValid) {
        ImGui::TextDisabled("no level loaded");
        ImGui::End();
        return;
    }
    ImGui::Checkbox("as loaded", &g_mapInitial);
    ImGui::SameLine();
    ImGui::Checkbox("links", &g_showLinks);
    ImGui::SameLine();
    ImGui::TextDisabled("%ux%u  hover for details and links", o.cols, o.rows);

    int maxH = 1;
    for (unsigned u = 0; u < o.cols; u++)
        for (unsigned v = 0; v < o.rows; v++) {
            const WsTile &t = o.grid[v + u * WS_GRID_PITCH];
            maxH = std::max(maxH, (int)(g_mapInitial ? t.spawn_a : t.height));
        }

    bool seen[256] = {}, seenContents[256] = {}, seenBridge = false;

    // Square cells as large as the window allows, beside the legend.
    const float legendW = 150.0f;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x -= legendW;
    const float cell = std::max(4.0f, std::min(avail.x / o.cols, avail.y / o.rows));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    // The hovered cell, known before drawing so its links can be shown.
    int hoverU = -1, hoverV = -1;
    if (ImGui::IsWindowHovered()) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const int hu = (int)floorf((m.x - origin.x) / cell), hv = (int)floorf((m.y - origin.y) / cell);
        if (hu >= 0 && hv >= 0 && hu < o.cols && hv < o.rows) {
            hoverU = hu;
            hoverV = hv;
        }
    }
    auto hovered = [&](unsigned u, unsigned v) { return (int)u == hoverU && (int)v == hoverV; };
    ImDrawList *dl = ImGui::GetWindowDrawList();
    auto corner = [&](float u, float v) { return ImVec2(origin.x + u * cell, origin.y + v * cell); };
    auto centre = [&](float u, float v) { return corner(u + 0.5f, v + 0.5f); };

    for (unsigned u = 0; u < o.cols; u++)
        for (unsigned v = 0; v < o.rows; v++) {
            const WsTile &t = o.grid[v + u * WS_GRID_PITCH];
            const uint8_t h = g_mapInitial ? t.spawn_a : t.height;
            const uint8_t c = g_mapInitial ? t.spawn : t.contents;
            if (h == 0 && t.kind == TILE_EMPTY)
                continue;  // no floor here
            seen[t.kind] = true;
            if (contents_name(c))
                seenContents[c] = true;
            dl->AddRectFilled(corner(u, v), corner(u + 1, v + 1), floor_color(h, maxH));
            if (t.kind != TILE_SLIDE)  // drawn as chains below
                draw_kind_mark(dl, t.kind, corner(u, v), corner(u + 1, v + 1));
            if (contents_name(c))
                dl->AddCircleFilled(centre(u, v), cell * 0.2f, contents_color(c));
        }

    Game *game = Game::instance();
    if (game)
        draw_slides(dl, game->map(), o.cols, o.rows, cell, centre);

    // Bridges are objects, not tiles: the builder clears their cells.  The
    // whole span is outlined; the deck fills as far as it has extended.
    const unsigned nBridges = game ? game->bridgeCount() : 0;
    for (unsigned i = 0; i < nBridges; i++) {
        const BridgeExtent b = game->bridgeSlot(i)->extent();
        const bool alongU = b.axis == 1;
        const float end  = (alongU ? b.restU : b.restV) + b.step * b.span;
        const float rest = alongU ? b.restU : b.restV;
        const float reach = g_mapInitial ? rest : (alongU ? b.reachU : b.reachV);
        // The cross-axis cell is the anchor's.
        const float cross = alongU ? b.restV : b.restU;
        auto rect = [&](float a0, float a1, ImVec2 *p0, ImVec2 *p1) {
            const float lo = std::min(a0, a1), hi = std::max(a0, a1);
            *p0 = alongU ? corner(lo, cross) : corner(cross, lo);
            *p1 = alongU ? corner(hi, cross + 1) : corner(cross + 1, hi);
        };
        ImVec2 p0, p1;
        if (reach != rest) {
            rect(rest, reach, &p0, &p1);
            dl->AddRectFilled(p0, p1, IM_COL32(190, 140, 80, 255));
        }
        rect(rest, end, &p0, &p1);
        dl->AddRect(p0, p1, b.armed ? IM_COL32(255, 220, 60, 255) : IM_COL32(170, 110, 50, 255),
                    0.0f, 0, std::max(1.5f, cell * 0.1f));
        seenBridge = true;

        // A line from each of its switches to the middle of the span: all of
        // them, or those of a hovered switch or bridge.
        rect(rest, end, &p0, &p1);
        const ImVec2 mid((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
        const SwitchCells *sc = game->switchCells();
        const int hAlong = alongU ? hoverU : hoverV, hAcross = alongU ? hoverV : hoverU;
        bool show = g_showLinks || (hAcross == (int)cross && hAlong >= std::min(rest, end) &&
                                    hAlong < std::max(rest, end));
        for (unsigned k = 0; k < sc->count(b.slot); k++)
            show = show || hovered(sc->cellU(b.slot, k), sc->cellV(b.slot, k));
        for (unsigned k = 0; show && k < sc->count(b.slot); k++)
            dl->AddLine(centre(sc->cellU(b.slot, k), sc->cellV(b.slot, k)), mid,
                        IM_COL32(230, 40, 40, 200), std::max(1.0f, cell * 0.08f));
    }

    // Teleporters to their partners, once per pair.
    if (game) {
        LevelMap *map = game->map();
        for (unsigned u = 0; u < o.cols; u++)
            for (unsigned v = 0; v < o.rows; v++) {
                Tile *t = map->tile((int)u, (int)v);
                if (t->objectMarker() != TILE_TELEPORTER)
                    continue;
                const unsigned tu = t->teleportU(), tv = t->teleportV();
                if (tu >= o.cols || tv >= o.rows || (tu == u && tv == v))
                    continue;
                const bool paired = map->tile((int)tu, (int)tv)->teleportU() == u &&
                                    map->tile((int)tu, (int)tv)->teleportV() == v;
                if (!g_showLinks && !hovered(u, v) && !hovered(tu, tv))
                    continue;
                if (paired && (tu < u || (tu == u && tv < v)) && !hovered(u, v))
                    continue;  // drawn from the other end
                dl->AddLine(centre(u, v), centre(tu, tv), IM_COL32(190, 70, 230, 220),
                            std::max(1.0f, cell * 0.08f));
                if (!paired)  // one-way: mark the destination
                    dl->AddCircle(centre(tu, tv), cell * 0.25f, IM_COL32(190, 70, 230, 220), 0, 2.0f);
            }
    }

    if (!g_mapInitial) {
        for (unsigned i = 0; i < o.n_enemies; i++)
            dl->AddCircleFilled(centre(o.enemies[i].pos[0], o.enemies[i].pos[2]), cell * 0.3f,
                                IM_COL32(20, 20, 20, 255));
        for (unsigned i = 0; i < o.n_foes; i++) {
            const WsEntity &f = o.foes[i];
            dl->AddCircleFilled(centre(f.pos[0], f.pos[2]), cell * 0.35f,
                                f.hidden ? IM_COL32(120, 60, 60, 255) : IM_COL32(230, 50, 50, 255));
        }
        // The player, with a tick for its facing.
        const ImVec2 p = centre(o.player_grid[0], o.player_grid[2]);
        dl->AddCircleFilled(p, cell * 0.4f, IM_COL32(255, 255, 255, 255));
        if (o.player_facing >= WS_DIR_MIN && o.player_facing <= WS_DIR_MAX)
            dl->AddLine(p, ImVec2(p.x + WS_DIR_DU[o.player_facing] * cell * 0.6f,
                                  p.y + WS_DIR_DV[o.player_facing] * cell * 0.6f),
                        IM_COL32(0, 0, 0, 255), 2);
    }

    ImGui::InvisibleButton("grid", ImVec2(cell * o.cols, cell * o.rows));
    if (ImGui::IsItemHovered()) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const int u = (int)((m.x - origin.x) / cell), v = (int)((m.y - origin.y) / cell);
        if (u >= 0 && v >= 0 && u < o.cols && v < o.rows) {
            const WsTile &t = o.grid[v + u * WS_GRID_PITCH];
            dl->AddRect(corner(u, v), corner(u + 1, v + 1), IM_COL32(255, 255, 0, 255), 0.0f, 0, 2.0f);
            ImGui::BeginTooltip();
            ImGui::Text("U%d V%d  %s (0x%02x)", u, v, kind_name(t.kind), t.kind);
            ImGui::Text("height %u (loaded %u)", t.height, t.spawn_a);
            if (t.kind == TILE_LIFT)
                ImGui::Text("lift at %.2f", t.height_f);
            ImGui::Text("param %u (loaded %u)", t.param, t.spawn_b);
            const char *now = contents_name(t.contents), *was = contents_name(t.spawn);
            ImGui::Text("contents %s (loaded %s)", now ? now : "-", was ? was : "-");
            if (t.kind == TILE_SLIDE && game)
                ImGui::Text("slide direction %u", game->map()->tile(u, v)->slideDir());
            if (t.occupant)
                ImGui::Text("occupied (%u)", t.occupant);
            if (t.spent)
                ImGui::TextUnformatted("spent / armed");
            for (unsigned i = 0; i < nBridges; i++) {
                const BridgeExtent b = game->bridgeSlot(i)->extent();
                const bool alongU = b.axis == 1;
                const float rest = alongU ? b.restU : b.restV, end = rest + b.step * b.span;
                const int along = alongU ? u : v, across = alongU ? v : u;
                if (across != (int)(alongU ? b.restV : b.restU) ||
                    along < std::min(rest, end) || along >= std::max(rest, end))
                    continue;
                ImGui::Separator();
                ImGui::Text("bridge on switch %d, along %s, %d cells", b.slot + 1, alongU ? "U" : "V", b.span);
                ImGui::Text("%s, %s next", b.armed ? "moving" : "still", b.phase ? "retracts" : "extends");
            }
            if (u == o.player_cell[0] && v == o.player_cell[1]) {
                ImGui::Separator();
                ImGui::Text("player at H%u, facing %u%s", o.player_cell[2], o.player_facing,
                            o.player_moving ? ", moving" : "");
            }
            for (unsigned i = 0; i < o.n_foes; i++)
                if (o.foes[i].gu == u && o.foes[i].gv == v)
                    entity_tooltip("foe", o.foes[i], true);
            for (unsigned i = 0; i < o.n_enemies; i++)
                if (o.enemies[i].gu == u && o.enemies[i].gv == v)
                    entity_tooltip("bomb", o.enemies[i], false);
            ImGui::EndTooltip();
        }
    }

    // The legend: only what this level has, drawn with the map's own marks.
    ImGui::SameLine();
    ImGui::BeginGroup();
    const float ls = 16.0f;
    auto swatch = [&]() {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(ls, ls));
        ImGui::SameLine();
        return std::make_pair(p, ImVec2(p.x + ls, p.y + ls));
    };
    static const uint8_t KINDS[] = {
        TILE_EXIT, TILE_STICKY, TILE_RAMP_1, TILE_LIFT, TILE_PLATFORM_U, TILE_PLATFORM_TRACK,
        TILE_FALLING, TILE_JUMP_PAD, TILE_TELEPORTER, TILE_SLIDE, TILE_SWITCH,
        TILE_BRIDGE_U, TILE_ICE, TILE_IMPASSABLE, TILE_BOMBABLE,
    };
    for (uint8_t k : KINDS) {
        bool any = seen[k];
        if (k == TILE_RAMP_1) any = seen[5] || seen[6] || seen[7] || seen[8];
        if (k == TILE_PLATFORM_U) any = seen[TILE_PLATFORM_U] || seen[TILE_PLATFORM_V];
        if (k == TILE_BRIDGE_U) any = seenBridge;
        if (!any)
            continue;
        auto r = swatch();
        if (k == TILE_BRIDGE_U) {
            // As the map draws one: extended deck inside the span's outline.
            dl->AddRectFilled(r.first, ImVec2(r.first.x + ls * 0.6f, r.second.y), IM_COL32(190, 140, 80, 255));
            dl->AddRect(r.first, r.second, IM_COL32(170, 110, 50, 255), 0.0f, 0, 2.0f);
            ImGui::TextUnformatted("bridge");
            continue;
        }
        dl->AddRectFilled(r.first, r.second, floor_color(1, 2));
        draw_kind_mark(dl, k, r.first, r.second);
        ImGui::TextUnformatted(kind_name(k));
    }
    ImGui::Separator();
    for (int c = 1; c < 256; c++) {
        if (!seenContents[c])
            continue;
        auto r = swatch();
        dl->AddCircleFilled(ImVec2(r.first.x + ls / 2, r.first.y + ls / 2), ls * 0.2f, contents_color((uint8_t)c));
        ImGui::TextUnformatted(contents_name((uint8_t)c));
    }
    if (!g_mapInitial) {
        ImGui::Separator();
        auto r = swatch();
        dl->AddCircleFilled(ImVec2(r.first.x + ls / 2, r.first.y + ls / 2), ls * 0.4f, IM_COL32(255, 255, 255, 255));
        ImGui::TextUnformatted("player");
        r = swatch();
        dl->AddCircleFilled(ImVec2(r.first.x + ls / 2, r.first.y + ls / 2), ls * 0.35f, IM_COL32(230, 50, 50, 255));
        ImGui::TextUnformatted("foe");
        r = swatch();
        dl->AddCircleFilled(ImVec2(r.first.x + ls / 2, r.first.y + ls / 2), ls * 0.3f, IM_COL32(20, 20, 20, 255));
        ImGui::TextUnformatted("bomb");
    }
    ImGui::TextDisabled("lighter is higher");
    ImGui::EndGroup();
    ImGui::End();
}

static void draw_panels(RenderDevice &dev)
{
    // Not gated on the game mode, which is 0 while paused: observe() itself
    // refuses when no level grid is loaded.
    g_obsValid = g_obs.observe();
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520, ImGui::GetIO().DisplaySize.y - 20), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Open'Roo")) {
        if (ImGui::CollapsingHeader("Overview", ImGuiTreeNodeFlags_DefaultOpen))
            draw_overview(dev);
        if (ImGui::CollapsingHeader("Frame breakdown", ImGuiTreeNodeFlags_DefaultOpen))
            draw_frame_breakdown();
        ImGui::Checkbox("map", &g_showMap);
        ImGui::SameLine();
        ImGui::TextDisabled("F10 hides this");
    }
    ImGui::End();
    if (g_showMap)
        draw_map();
}

static bool overlay(RenderDevice &dev)
{
    prof::setEnabled(windev::debugUiShown());
    if (!windev::debugUiShown())
        return false;
    windev::debugUiNewFrame();
    ImGui::NewFrame();
    draw_panels(dev);
    ImGui::Render();
    render_draw_data(dev, ImGui::GetDrawData());
    return true;
}

// ── Life cycle ──

/* Our own toggles, kept in the ini beside ImGui's window placement as a
 * [Open'Roo][Settings] section. */
static void *settings_open(ImGuiContext *, ImGuiSettingsHandler *, const char *name)
{
    return strcmp(name, "Settings") == 0 ? (void *)1 : NULL;
}

static void settings_read_line(ImGuiContext *, ImGuiSettingsHandler *, void *, const char *line)
{
    int v;
    if (sscanf(line, "ShowMap=%d", &v) == 1)
        g_showMap = v != 0;
    else if (sscanf(line, "MapAsLoaded=%d", &v) == 1)
        g_mapInitial = v != 0;
    else if (sscanf(line, "MapLinks=%d", &v) == 1)
        g_showLinks = v != 0;
}

static void settings_write_all(ImGuiContext *, ImGuiSettingsHandler *h, ImGuiTextBuffer *out)
{
    out->appendf("[%s][Settings]\n", h->TypeName);
    out->appendf("ShowMap=%d\n", g_showMap ? 1 : 0);
    out->appendf("MapAsLoaded=%d\n", g_mapInitial ? 1 : 0);
    out->appendf("MapLinks=%d\n", g_showLinks ? 1 : 0);
    out->append("\n");
}

bool init(RenderDevice &dev)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    // In the run directory: window placement and the toggles above persist
    // from run to run.  The cheats (which change the game) are not saved.
    io.IniFilename = "debugui.ini";
    ImGuiSettingsHandler handler;
    handler.TypeName   = "Open'Roo";
    handler.TypeHash   = ImHashStr(handler.TypeName);
    handler.ReadOpenFn = settings_open;
    handler.ReadLineFn = settings_read_line;
    handler.WriteAllFn = settings_write_all;
    ImGui::AddSettingsHandler(&handler);
    io.LogFilename = NULL;
    io.BackendRendererName = "RenderDevice";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    if (!windev::debugUiInit()) {
        ImGui::DestroyContext();
        return false;
    }
    dev.SetOverlay(overlay);
    return true;
}

void shutdown(RenderDevice &dev)
{
    if (!ImGui::GetCurrentContext())
        return;
    dev.SetOverlay(NULL);
    windev::debugUiShutdown();
    for (ImTextureData *tex : ImGui::GetPlatformIO().Textures)
        if (tex->RefCount == 1) {
            RenderDevice::DestroyTexture((DeviceTexture *)(uintptr_t)tex->TexID);
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
    ImGui::DestroyContext();
}

}  // namespace debugui
