/* Level select, an addition of ours: a hidden page off the Load Game screen
 * (menu node 2).  Right opens it; left and right change theme, up and down
 * pick a level, Enter starts a fresh game there, Escape goes back.
 *
 * It is built from ordinary menu-tree nodes: node 0x60 + t is theme t's page,
 * whose children are its levels, each leading to LS_START (0x70), the action
 * node Enter descends to.  The game's own tree uses nodes 0..0x50 and 200 up,
 * so this range is free.  Levels are grouped by the theme folder that prefixes
 * their name in JJ.GAM ("Forest\Start" is Forest). */
#pragma once

class Game;

static const unsigned char LS_THEME_NODE = 0x60;  // 0x60 .. 0x6f
static const int           LS_MAX_THEMES = 16;
static const unsigned char LS_START      = 0x70;

/* Rows the page shows; a longer theme scrolls. */
static const int LEVELSELECT_ROWS = 7;

/* Node 0x60 + t, for t below the theme count. */
bool LevelSelect_IsThemeNode(unsigned char node);

/* Called with the right and left polls the keypress handler already makes (dir
 * +1 or -1): from node 2, right opens the page of the last-played level's
 * theme; on a theme page, either moves to the next or previous theme. */
void LevelSelect_Turn(Game *g, int dir);

/* At LS_START: pops back to the theme page and returns the JJ.GAM index of the
 * row Enter was pressed on. */
int LevelSelect_Chosen(Game *g);

/* What the page draws: the theme's name, the visible window of its levels, and
 * the selected row's place in that window. */
struct LevelSelectView {
    const char *theme;
    int         count;
    const char *rows[LEVELSELECT_ROWS];
    int         selected;
};
void LevelSelect_View(Game *g, LevelSelectView *v);
