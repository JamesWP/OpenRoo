#include <string.h>
#include <algorithm>
#include "levelthemes.h"
#include "session.h"

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

void LevelThemes_Build(Session *session)
{
    s_themeCount = 0;
    char prev[64] = "";
    for (int i = 0; i < session->levelCount(); i++) {
        char t[64];
        theme_of(session->levelNameTableEntry((unsigned char)i), t, sizeof(t));
        if (s_themeCount == 0 || strcmp(t, prev) != 0) {
            if (s_themeCount == LS_MAX_THEMES)
                break;
            s_themeFirst[s_themeCount] = i;
            strcpy(s_themeName[s_themeCount], t);
            s_themeCount++;
            strcpy(prev, t);
        }
    }
    s_themeFirst[s_themeCount] = s_themeCount ? session->levelCount() : 0;
}

int LevelThemes_Count(void) { return s_themeCount; }
int LevelThemes_First(int t) { return s_themeFirst[t]; }
int LevelThemes_Size(int t) { return theme_size(t); }
const char *LevelThemes_Name(int t) { return s_themeName[t]; }
int LevelThemes_Top(void) { return s_top; }
void LevelThemes_SetTop(int top) { s_top = top; }

bool LevelThemes_IsThemeNode(unsigned char node)
{
    return node >= LS_THEME_NODE && node < LS_THEME_NODE + s_themeCount;
}
