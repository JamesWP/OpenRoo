#include "dbg.h"
#include "prof.h"
#include "profpanel.h"
#include "windev.h"
#include <algorithm>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

// Last: it defines LINE_MAX, which the game's headers use as a name.
#include "imgui_internal.h"

namespace dbg {

static bool g_active;

// The toggles, kept in the ini.
static bool g_showMap = true;
static bool g_asLoaded;
static bool g_links;
static bool g_foePaths;

static float g_maxFrameMs;

bool active() { return g_active; }

void resetMax()
{
    g_maxFrameMs = 0.0f;
    prof::resetMax();
}

// ── The map ──

static MapView g_view;
static bool    g_ready;  // the base layer has run this frame

struct TipLine {
    bool        sep;
    std::string text;
};
static std::vector<TipLine> g_tip;

struct LegendEntry {
    int                 group;
    std::string         name;
    MapView::SwatchFn   draw;
    void               *ctx;
};
static std::vector<LegendEntry> g_legend;

static bool g_clicked;
static int  g_clickU, g_clickV;

static const float LEGEND_W = 150.0f;

bool MapView::clicked(int *u, int *v) const
{
    if (!g_clicked)
        return false;
    *u = g_clickU;
    *v = g_clickV;
    return true;
}

void MapView::tip(const char *fmt, ...) const
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_tip.push_back({ false, buf });
}

void MapView::separator() const { g_tip.push_back({ true, std::string() }); }

void MapView::legend(int group, const char *name, SwatchFn draw, void *ctx) const
{
    for (const LegendEntry &e : g_legend)
        if (e.name == name)
            return;
    g_legend.push_back({ group, name, draw, ctx });
}

static bool beginMap()
{
    ImGui::SetNextWindowPos(ImVec2(540, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(660, 560), ImGuiCond_FirstUseEver);
    return ImGui::Begin("Map", &g_showMap);
}

MapLayer::MapLayer() : view_(nullptr), begun_(false)
{
    if (!g_active || !g_showMap || !g_ready)
        return;
    begun_ = true;
    if (!ImGui::Begin("Map", &g_showMap))
        return;
    g_view.dl = ImGui::GetWindowDrawList();
    view_ = &g_view;
}

MapLayer::MapLayer(unsigned cols, unsigned rows) : view_(nullptr), begun_(false)
{
    if (!g_active || !g_showMap)
        return;
    begun_ = true;
    if (!beginMap())
        return;
    if (cols == 0 || rows == 0) {
        ImGui::TextDisabled("no level loaded");
        return;
    }
    ImGui::Checkbox("as loaded", &g_asLoaded);
    ImGui::SameLine();
    ImGui::Checkbox("links", &g_links);
    ImGui::SameLine();
    ImGui::Checkbox("foe paths", &g_foePaths);
    ImGui::SameLine();
    ImGui::TextDisabled("%ux%u  click teleports; hover for details and links", cols, rows);

    // Square cells as large as the window allows, beside the legend.
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x -= LEGEND_W;
    const float cell = std::max(4.0f, std::min(avail.x / cols, avail.y / rows));
    g_view.dl       = ImGui::GetWindowDrawList();
    g_view.cell     = cell;
    g_view.origin   = ImGui::GetCursorScreenPos();
    g_view.cols     = cols;
    g_view.rows     = rows;
    g_view.asLoaded = g_asLoaded;
    g_view.links    = g_links;
    g_view.foePaths = g_foePaths;
    g_view.hoverU = g_view.hoverV = -1;
    g_clicked = false;

    ImGui::InvisibleButton("grid", ImVec2(cell * cols, cell * rows));
    if (ImGui::IsItemHovered()) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const int u = (int)floorf((m.x - g_view.origin.x) / cell);
        const int v = (int)floorf((m.y - g_view.origin.y) / cell);
        if (u >= 0 && v >= 0 && (unsigned)u < cols && (unsigned)v < rows) {
            g_view.hoverU = u;
            g_view.hoverV = v;
            if (ImGui::IsItemClicked(0)) {
                g_clicked = true;
                g_clickU  = u;
                g_clickV  = v;
            }
        }
    }
    g_ready = true;
    view_   = &g_view;
}

MapLayer::~MapLayer()
{
    if (begun_)
        ImGui::End();
}

static void draw_map_extras()
{
    if (!g_ready || !g_showMap)
        return;
    if (ImGui::Begin("Map", &g_showMap)) {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const MapView &m = g_view;
        if (m.hoverU >= 0)
            dl->AddRect(m.corner((float)m.hoverU, (float)m.hoverV),
                        m.corner(m.hoverU + 1.0f, m.hoverV + 1.0f),
                        IM_COL32(255, 255, 0, 255), 0.0f, 0, 2.0f);

        // The legend: only what the layers drew, with their own marks.
        ImGui::SetCursorScreenPos(ImVec2(m.origin.x + m.cols * m.cell + 8.0f, m.origin.y));
        ImGui::BeginGroup();
        const float ls = 16.0f;
        std::stable_sort(g_legend.begin(), g_legend.end(),
                         [](const LegendEntry &a, const LegendEntry &b) { return a.group < b.group; });
        int group = g_legend.empty() ? 0 : g_legend.front().group;
        for (const LegendEntry &e : g_legend) {
            if (e.group != group) {
                ImGui::Separator();
                group = e.group;
            }
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(ls, ls));
            ImGui::SameLine();
            e.draw(dl, p, ImVec2(p.x + ls, p.y + ls), e.ctx);
            ImGui::TextUnformatted(e.name.c_str());
        }
        ImGui::TextDisabled("lighter is higher");
        ImGui::EndGroup();
    }
    ImGui::End();

    if (g_view.hoverU >= 0 && !g_tip.empty()) {
        ImGui::BeginTooltip();
        bool first = true;
        for (const TipLine &l : g_tip) {
            if (l.sep) {
                if (!first)
                    ImGui::Separator();
                continue;
            }
            ImGui::TextUnformatted(l.text.c_str());
            first = false;
        }
        ImGui::EndTooltip();
    }
}

// ── The main window ──

static bool beginMain()
{
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520, ImGui::GetIO().DisplaySize.y - 20), ImGuiCond_FirstUseEver);
    return ImGui::Begin("Open'Roo");
}

Section::Section(const char *title, bool openByDefault) : open_(false), begun_(false)
{
    if (!g_active)
        return;
    begun_ = true;
    open_  = beginMain() &&
             ImGui::CollapsingHeader(title, openByDefault ? ImGuiTreeNodeFlags_DefaultOpen : 0);
}

Section::~Section()
{
    if (begun_)
        ImGui::End();
}

// ── The frame ──

void frameBegin()
{
    g_active = false;
    g_ready  = false;
    g_tip.clear();
    g_legend.clear();
    prof::setEnabled(windev::debugUiShown());
    if (!windev::debugUiShown() || !ImGui::GetCurrentContext())
        return;
    windev::debugUiNewFrame();
    ImGui::NewFrame();
    g_active = true;

    const ImGuiIO &io = ImGui::GetIO();
    const float frameMs = io.DeltaTime * 1000.0f;
    if (frameMs > g_maxFrameMs)
        g_maxFrameMs = frameMs;
    if (beginMain()) {
        ImGui::Text("%.1f fps  %.2f ms  max %.2f ms", io.Framerate, 1000.0f / io.Framerate, g_maxFrameMs);
        ImGui::SameLine();
        if (ImGui::SmallButton("reset max"))
            resetMax();
        ImGui::TextDisabled("display %.0fx%.0f", io.DisplaySize.x, io.DisplaySize.y);
    }
    ImGui::End();
}

bool frameEnd()
{
    if (!g_active)
        return false;
    draw_map_extras();
    {
        Section s("Frame breakdown", true);
        if (s)
            prof::drawPanel();
    }
    if (beginMain()) {
        ImGui::Checkbox("map", &g_showMap);
        ImGui::SameLine();
        ImGui::TextDisabled("F10 hides this");
    }
    ImGui::End();
    ImGui::Render();
    g_active = false;
    return true;
}

// ── Settings ──

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
        g_asLoaded = v != 0;
    else if (sscanf(line, "FoePaths=%d", &v) == 1)
        g_foePaths = v != 0;
    else if (sscanf(line, "MapLinks=%d", &v) == 1)
        g_links = v != 0;
}

static void settings_write_all(ImGuiContext *, ImGuiSettingsHandler *h, ImGuiTextBuffer *out)
{
    out->appendf("[%s][Settings]\n", h->TypeName);
    out->appendf("ShowMap=%d\n", g_showMap ? 1 : 0);
    out->appendf("MapAsLoaded=%d\n", g_asLoaded ? 1 : 0);
    out->appendf("FoePaths=%d\n", g_foePaths ? 1 : 0);
    out->appendf("MapLinks=%d\n", g_links ? 1 : 0);
    out->append("\n");
}

void registerSettings()
{
    ImGuiSettingsHandler handler;
    handler.TypeName   = "Open'Roo";
    handler.TypeHash   = ImHashStr(handler.TypeName);
    handler.ReadOpenFn = settings_open;
    handler.ReadLineFn = settings_read_line;
    handler.WriteAllFn = settings_write_all;
    ImGui::AddSettingsHandler(&handler);
}

}  // namespace dbg
