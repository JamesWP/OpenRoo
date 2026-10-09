#include "gamedebug.h"
#include "cheatcode.h"
#include "dbg.h"
#include "game.h"
#include "levelmap.h"
#include "player.h"
#include <stdio.h>

/* The level timer held at the value it had when the box was ticked (the level
 * index guards against carrying it into another level). */
static bool     g_pauseTimer;
static unsigned g_pausedMs;
static unsigned g_pausedLevel;

/* The slowest frame is held since the level was loaded.  A reload of the same
 * level shows as the level timer running backwards. */
static unsigned g_maxLevel = ~0u;
static unsigned g_maxTimerMs;

static void hold_timer(Game *g)
{
    if (!g_pauseTimer)
        return;
    if (g_pausedLevel != g->levelIndex() || g->timeElapsed() < g_pausedMs) {
        g_pausedLevel = g->levelIndex();
        g_pausedMs    = g->timeElapsed();
    }
    g->setTimeElapsed(g_pausedMs);
}

/* The level to load, from the same table the level select reads. */
static void level_picker(Game *g)
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

void Game_DebugPanel(Game *g)
{
    if (!dbg::active())
        return;
    if (g->levelIndex() != g_maxLevel || g->timeElapsed() < g_maxTimerMs)
        dbg::resetMax();
    g_maxLevel   = g->levelIndex();
    g_maxTimerMs = g->timeElapsed();

    dbg::Section s("Overview", true);
    if (!s)
        return;
    ImGui::Text("level %u/%u: %s", g->levelIndex() + 1, g->levelCount(), g->levelName());
    level_picker(g);

    if (ImGui::Checkbox("pause timer", &g_pauseTimer)) {
        g_pausedLevel = g->levelIndex();
        g_pausedMs    = g->timeElapsed();
    }
    hold_timer(g);
    ImGui::SameLine();
    ImGui::TextDisabled("%u s of %d", g->timeElapsed() / 1000, g->timeLimit());

    if (g->map()->extentU() == 0 || g->map()->extentV() == 0)
        return;  // a menu, or the level torn down
    const Player *pl = g->player();
    ImGui::Text("gems %d / %d   lives %u   foes killed %u", pl->gemsCollected(), g->gemsRequired(),
                (unsigned)pl->lives(), (unsigned)g->foesKilled());
    ImGui::Text("player U%u V%u H%u   exit U%u V%u H%u", (unsigned)(uint8_t)pl->cellU(),
                (unsigned)(uint8_t)pl->cellV(), (unsigned)(uint8_t)pl->heightCell(),
                (unsigned)pl->markerCellU(), (unsigned)pl->markerCellV(), (unsigned)pl->markerCellH());
    ImGui::Text("%u foes, %u bombs%s", (unsigned)g->foeCount(), (unsigned)g->bombCount(),
                pl->held() ? "   [complete]" : "");
}
