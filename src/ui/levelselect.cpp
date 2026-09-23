/* Level select -- see levelselect.h.  Our feature; no original behind it.
 *
 * Input goes through hooks_GetAsyncKeyState like every other menu poll, so a
 * recording made on this page replays.  Edges are our own (s_lastKey): the
 * page never touches the navigator's debounce except on the way out, where
 * it hands the held key over so the navigator does not act on it too.
 */
#include <windows.h>
#include <string.h>
#include "levelselect.h"
#include "game.h"
#include "menutree.h"
#include "log.h"
#include "record.h"

#define KEY(k)  hooks_GetAsyncKeyState(k)

#define MAX_THEMES 32

static bool s_open;
static int  s_theme;                    /* index into s_themeFirst */
static int  s_row;                      /* within the theme */
static int  s_top;                      /* first visible row */
static int  s_lastKey;                  /* our own debounce */
static int  s_themeCount;
static int  s_themeFirst[MAX_THEMES + 1];   /* JJ.GAM index each theme starts at */
static char s_themeName[MAX_THEMES][64];

/* The folder part of a level name, into out; the whole name if it has none. */
static void theme_of(const char *name, char *out, size_t n)
{
    const char *bs = strchr(name, '\\');
    size_t len = bs ? (size_t)(bs - name) : strlen(name);
    if (len >= n)
        len = n - 1;
    memcpy(out, name, len);
    out[len] = 0;
}

/* JJ.GAM lists each theme's levels contiguously, so a theme is a run of
 * equal folder names.  A folder that came back later would get a second
 * page rather than be merged -- fine, and none does. */
static void build_groups(Game *g)
{
    s_themeCount = 0;
    char prev[64] = "";
    for (int i = 0; i < g->levelCount(); i++) {
        char t[64];
        theme_of(g->levelNameTableEntry((unsigned char)i), t, sizeof(t));
        if (s_themeCount == 0 || strcmp(t, prev) != 0) {
            if (s_themeCount == MAX_THEMES)
                break;
            s_themeFirst[s_themeCount] = i;
            strcpy(s_themeName[s_themeCount], t);
            s_themeCount++;
            strcpy(prev, t);
        }
    }
    s_themeFirst[s_themeCount] = g->levelCount();
}

static int theme_size(int t) { return s_themeFirst[t + 1] - s_themeFirst[t]; }

bool LevelSelect_Active(Game *g)
{
    if (s_open && g->menu()->node() != 2)
        s_open = false;
    return s_open;
}

void LevelSelect_Open(Game *g)
{
    build_groups(g);
    if (s_themeCount == 0)
        return;
    /* Open on the theme of the level the game last had loaded. */
    s_theme = 0;
    for (int t = 0; t < s_themeCount; t++)
        if (g->levelIndex() >= s_themeFirst[t])
            s_theme = t;
    s_row = 0;
    s_top = 0;
    s_lastKey = 0x27;                   /* the RIGHT that opened it */
    s_open = true;
    log_write("levelselect: open, %d themes, %d levels\n",
              s_themeCount, (int)g->levelCount());
}

int LevelSelect_Poll(Game *g)
{
    static const int keys[] = { 0x0d, 0x1b, 0x25, 0x26, 0x27, 0x28 };
    int down = 0;
    for (int k : keys)
        if (KEY(k) != 0 && down == 0)
            down = k;
    if (down == s_lastKey)
        return -1;                      /* still held, or nothing */
    s_lastKey = down;

    switch (down) {
    case 0x26:
        s_row = s_row ? s_row - 1 : theme_size(s_theme) - 1;
        break;
    case 0x28:
        s_row = s_row + 1 < theme_size(s_theme) ? s_row + 1 : 0;
        break;
    case 0x25:
    case 0x27:
        s_theme = (s_theme + (down == 0x27 ? 1 : s_themeCount - 1)) % s_themeCount;
        if (s_row >= theme_size(s_theme))
            s_row = theme_size(s_theme) - 1;
        break;
    case 0x1b:
        /* Hand the held ESC to the navigator so it does not also pop the
         * Load Game screen on the next frame. */
        g->menu()->setLastKey(0x1b);
        s_open = false;
        break;
    case 0x0d: {
        int level = s_themeFirst[s_theme] + s_row;
        log_write("levelselect: start level %d (%s)\n",
                  level, g->levelNameTableEntry((unsigned char)level));
        s_open = false;
        return level;
    }
    }
    return -1;
}

void LevelSelect_View(Game *g, LevelSelectView *v)
{
    const int n = theme_size(s_theme);
    /* Scroll only as far as it takes to keep the selection visible. */
    if (s_row < s_top)
        s_top = s_row;
    if (s_row >= s_top + LEVELSELECT_ROWS)
        s_top = s_row - LEVELSELECT_ROWS + 1;
    if (s_top > n - LEVELSELECT_ROWS)
        s_top = n > LEVELSELECT_ROWS ? n - LEVELSELECT_ROWS : 0;

    v->theme = s_themeName[s_theme];
    v->count = n - s_top < LEVELSELECT_ROWS ? n - s_top : LEVELSELECT_ROWS;
    for (int i = 0; i < v->count; i++) {
        const char *name = g->levelNameTableEntry((unsigned char)(s_themeFirst[s_theme] + s_top + i));
        const char *bs = strchr(name, '\\');
        v->rows[i] = bs ? bs + 1 : name;
    }
    v->selected = s_row - s_top;
}
