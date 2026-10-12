#include "gamedebug.h"
#include "cheats.h"
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
    if (g_pausedLevel != g->session()->levelIndex() || g->world()->timeElapsed() < g_pausedMs) {
        g_pausedLevel = g->session()->levelIndex();
        g_pausedMs    = g->world()->timeElapsed();
    }
    g->world()->setTimeElapsed(g_pausedMs);
}

/* The level to load, from the same table the level select reads. */
static void level_picker(Game *g)
{
    if (ImGui::Button("Load level..."))
        ImGui::OpenPopup("levels");
    if (ImGui::BeginPopup("levels")) {
        ImGui::BeginChild("list", ImVec2(260, 300));
        for (unsigned i = 0; i < g->session()->levelCount(); i++) {
            ImGui::PushID((int)i);
            char label[300];
            snprintf(label, sizeof(label), "%3u  %s", i + 1, g->session()->levelNameTableEntry((unsigned char)i));
            if (ImGui::Selectable(label, i == g->session()->levelIndex())) {
                Cheat_LoadLevel(g, (unsigned char)i);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
}

/* The level's state at a glance: read-only. */
static void level_stats(const World *w, unsigned foesKilled)
{
    if (w->map()->extentU() == 0 || w->map()->extentV() == 0)
        return;  // a menu, or the level torn down
    const Player *pl = w->player();
    ImGui::Text("gems %d / %d   lives %u   foes killed %u", pl->gemsCollected(), w->gemsRequired(),
                (unsigned)pl->lives(), foesKilled);
    ImGui::Text("player U%u V%u H%u   exit U%u V%u H%u", (unsigned)(uint8_t)pl->cellU(),
                (unsigned)(uint8_t)pl->cellV(), (unsigned)(uint8_t)pl->heightCell(),
                (unsigned)pl->markerCellU(), (unsigned)pl->markerCellV(), (unsigned)pl->markerCellH());
    ImGui::Text("%u foes, %u bombs%s", (unsigned)w->foes().size(), (unsigned)w->bombs().size(),
                pl->held() ? "   [complete]" : "");
}

void Game_DebugPanel(Game *g)
{
    if (!dbg::active())
        return;
    if (g->session()->levelIndex() != g_maxLevel || g->world()->timeElapsed() < g_maxTimerMs)
        dbg::resetMax();
    g_maxLevel   = g->session()->levelIndex();
    g_maxTimerMs = g->world()->timeElapsed();

    dbg::Section s("Overview", true);
    if (!s)
        return;
    ImGui::Text("level %u/%u: %s", g->session()->levelIndex() + 1, g->session()->levelCount(), g->world()->levelName());
    level_picker(g);

    if (ImGui::Checkbox("pause timer", &g_pauseTimer)) {
        g_pausedLevel = g->session()->levelIndex();
        g_pausedMs    = g->world()->timeElapsed();
    }
    hold_timer(g);
    ImGui::SameLine();
    ImGui::TextDisabled("%u s of %d", g->world()->timeElapsed() / 1000, g->world()->timeLimit());

    level_stats(g->world(), g->session()->foesKilled());
}

void Cheat_DebugPanel(Game *g)
{
    // Not saved with the window: the cheats change the game.
    static bool holdFrozen;
    dbg::Section s("Cheats", true);
    if (!s)
        return;
    if (ImGui::Button("+life"))      Cheat_AddLife(g, 1);
    ImGui::SameLine();
    if (ImGui::Button("+10 bombs"))  Cheat_AddBombs(g);
    ImGui::SameLine();
    if (ImGui::Button("+glide"))     Cheat_AddGlide(g);
    if (ImGui::Button("invulnerable")) Cheat_Invulnerable(g);
    ImGui::SameLine();
    if (ImGui::Button("kill foes"))  Cheat_KillFoes(g);
    ImGui::Checkbox("hold foes frozen", &holdFrozen);
    if (holdFrozen)
        Cheat_FreezeFoes(g);
}
