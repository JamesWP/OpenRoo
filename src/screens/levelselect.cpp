/* The theme nodes are rebuilt from the loaded JJ.GAM table every time the page
 * opens.  Nothing else reads or writes them. */
#include <string.h>
#include "levelselect.h"
#include "game.h"
#include "menutree.h"
#include "logger.h"
#include <algorithm>

static int  s_themeCount;
static int  s_themeFirst[LS_MAX_THEMES + 1];  // JJ.GAM index each theme starts at
static char s_themeName[LS_MAX_THEMES][64];
static int  s_top;  // first visible row

/* The folder part of a level name; the whole name if it has none. */
static void theme_of(const char *name, char *out, size_t n)
{
    const char *bs = strchr(name, '\\');
    size_t len = bs ? (size_t)(bs - name) : strlen(name);
    if (len >= n)
        len = n - 1;
    std::copy_n(name, len, out);
    out[len] = 0;
}

static int theme_size(int t) { return s_themeFirst[t + 1] - s_themeFirst[t]; }

/* JJ.GAM lists each theme's levels together, so a theme is a run of equal
 * folder names; a folder that recurred later would get a second page. */
static void build_nodes(Game *g)
{
    MenuTree *m = g->menu();
    s_themeCount = 0;
    char prev[64] = "";
    for (int i = 0; i < g->levelCount(); i++) {
        char t[64];
        theme_of(g->levelNameTableEntry((unsigned char)i), t, sizeof(t));
        if (s_themeCount == 0 || strcmp(t, prev) != 0) {
            if (s_themeCount == LS_MAX_THEMES)
                break;
            s_themeFirst[s_themeCount] = i;
            strcpy(s_themeName[s_themeCount], t);
            s_themeCount++;
            strcpy(prev, t);
        }
    }
    s_themeFirst[s_themeCount] = s_themeCount ? g->levelCount() : 0;

    for (int t = 0; t < s_themeCount; t++) {
        const unsigned char node = (unsigned char)(LS_THEME_NODE + t);
        m->setChildCount(node, (unsigned char)theme_size(t));
        for (int i = 0; i < theme_size(t); i++)
            m->setChild(node, (unsigned char)i, LS_START);
    }
}

bool LevelSelect_IsThemeNode(unsigned char node)
{
    return node >= LS_THEME_NODE && node < LS_THEME_NODE + s_themeCount;
}

void LevelSelect_Turn(Game *g, int dir)
{
    MenuTree *m = g->menu();
    if (m->node() == 2) {
        if (dir < 0)
            return;
        build_nodes(g);
        if (s_themeCount == 0)
            return;
        // Open on the theme of the level last loaded.
        int t = 0;
        for (int i = 0; i < s_themeCount; i++)
            if (g->levelIndex() >= s_themeFirst[i])
                t = i;
        // Descend as the navigator's Enter does.
        m->setSavedCursor(2, m->cursor());
        m->setCursor(0);
        m->push(2);
        m->setNode((unsigned char)(LS_THEME_NODE + t));
        s_top = 0;
        g_logger.write("levelselect: open, %d themes, %d levels\n",
                  s_themeCount, (int)g->levelCount());
        return;
    }
    if (!LevelSelect_IsThemeNode(m->node()))
        return;
    // Sideways replaces the page and leaves the stack, so Escape still goes
    // back to Load Game.
    int t = (m->node() - LS_THEME_NODE + dir + s_themeCount) % s_themeCount;
    m->setNode((unsigned char)(LS_THEME_NODE + t));
    if (m->cursor() >= theme_size(t))
        m->setCursor((unsigned char)(theme_size(t) - 1));
}

int LevelSelect_Chosen(Game *g)
{
    MenuTree *m = g->menu();
    m->pop();  // node is the theme page, cursor the row
    int level = s_themeFirst[m->node() - LS_THEME_NODE] + m->cursor();
    g_logger.write("levelselect: start level %d (%s)\n",
              level, g->levelNameTableEntry((unsigned char)level));
    return level;
}

void LevelSelect_View(Game *g, LevelSelectView *v)
{
    const int t   = g->menu()->node() - LS_THEME_NODE;
    const int n   = theme_size(t);
    const int row = g->menu()->cursor();
    // Scroll only as far as keeps the selection visible.
    if (row < s_top)
        s_top = row;
    if (row >= s_top + LEVELSELECT_ROWS)
        s_top = row - LEVELSELECT_ROWS + 1;
    if (s_top > n - LEVELSELECT_ROWS)
        s_top = n > LEVELSELECT_ROWS ? n - LEVELSELECT_ROWS : 0;

    v->theme = s_themeName[t];
    v->count = n - s_top < LEVELSELECT_ROWS ? n - s_top : LEVELSELECT_ROWS;
    for (int i = 0; i < v->count; i++) {
        const char *name = g->levelNameTableEntry((unsigned char)(s_themeFirst[t] + s_top + i));
        const char *bs = strchr(name, '\\');
        v->rows[i] = bs ? bs + 1 : name;
    }
    v->selected = row - s_top;
}
