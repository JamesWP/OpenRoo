/* The debug UI's shared frame.  The game's own code draws the debug UI as it
 * runs: the code that updates a foe draws it on the map, the code that updates
 * the player draws the player, and a component with numbers to show adds a
 * section to the main window.  Nothing is copied or snapshotted; each piece
 * reads its own live state.
 *
 * A frame opens at the top of Render_RenderGameFrame (frameBegin) and the
 * overlay closes it (frameEnd).  In between, anything may call ImGui, but only
 * when active() is true, which is the F10 toggle: hidden, none of it runs.
 *
 * Several components share two windows.  ImGui lets a window be Begun more
 * than once in a frame, appending, so Section adds to the main window and
 * MapLayer adds to the Map window, in call order. */
#pragma once

#include "imgui.h"

namespace dbg {

/* True from frameBegin to frameEnd while the UI is shown. */
bool active();

void frameBegin();

/* Draws what is shared (the map's legend and tooltip, the profiler's
 * breakdown), then ImGui::Render().  False when no frame was open. */
bool frameEnd();

/* Puts the UI's saved toggles in the ini beside ImGui's window placement.
 * After ImGui::CreateContext. */
void registerSettings();

/* Clears the slowest-frame hold (and the profiler's), as a level load does. */
void resetMax();

/* A collapsible section of the main window.
 *     if (dbg::Section s("Player")) { ImGui::Text(...); } */
class Section {
public:
    explicit Section(const char *title, bool openByDefault = false);
    ~Section();
    Section(const Section &) = delete;
    Section &operator=(const Section &) = delete;
    explicit operator bool() const { return open_; }
private:
    bool open_;
    bool begun_;
};

/* The Map window's grid for this frame: U runs across, V down, one square per
 * tile.  Valid inside a MapLayer. */
struct MapView {
    ImDrawList *dl;
    float cell;          // a tile's side, in pixels
    ImVec2 origin;       // the top left of cell (0, 0)
    unsigned cols, rows;
    int hoverU, hoverV;  // the cell under the mouse; -1 off the grid
    bool asLoaded;       // draw the level as it was loaded, not as it is now
    bool links;          // draw every switch and teleporter link
    bool foePaths;       // draw each foe's route

    ImVec2 corner(float u, float v) const { return ImVec2(origin.x + u * cell, origin.y + v * cell); }
    ImVec2 centre(float u, float v) const { return corner(u + 0.5f, v + 0.5f); }
    bool hovered(int u, int v) const { return u == hoverU && v == hoverV; }

    /* True once, on the frame the mouse clicks a cell; the cell in u, v. */
    bool clicked(int *u, int *v) const;

    /* Lines for the tooltip shown beside the mouse.  Add them only for the
     * hovered cell; separator() starts a new block. */
    void tip(const char *fmt, ...) const IM_FMTARGS(2);
    void separator() const;

    /* An entry in the legend: draws its swatch in the square [a, b].  Entries
     * with the same name merge; groups are shown in order, divided. */
    typedef void (*SwatchFn)(ImDrawList *dl, ImVec2 a, ImVec2 b, void *ctx);
    void legend(int group, const char *name, SwatchFn draw, void *ctx = nullptr) const;
};

/* Opens the Map window to draw on it for as long as it lives.  False (and
 * the window untouched) while the map is hidden or collapsed.
 *
 * The level's base layer goes first each frame, with the grid's size; the
 * others find the grid it set and are skipped until it has.  Later layers
 * paint over earlier ones. */
class MapLayer {
public:
    MapLayer();
    MapLayer(unsigned cols, unsigned rows);
    ~MapLayer();
    MapLayer(const MapLayer &) = delete;
    MapLayer &operator=(const MapLayer &) = delete;
    explicit operator bool() const { return view_ != nullptr; }
    const MapView *operator->() const { return view_; }
    const MapView &operator*() const { return *view_; }
private:
    const MapView *view_;
    bool begun_;
};

}  // namespace dbg
