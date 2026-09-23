/* Level select -- our own addition, not a replacement.  A hidden page off
 * the Load Game screen (menu node 2): RIGHT opens it; LEFT/RIGHT change
 * theme, UP/DOWN pick a level, ENTER starts a fresh game there, ESC goes
 * back.
 *
 * It is built from ordinary menu-tree nodes, like every other screen:
 *
 *   0x60 + t   theme t's page; its children are its levels, all LS_START
 *   0x70       LS_START, the action node ENTER on a level descends to
 *
 * The game's builder uses nodes 0..0x50 and the save slots 200 and up
 * (every immediate it writes into the tree, scanned from the binary), so
 * this range is free.  The navigator gives UP/DOWN/ENTER/ESC as it does
 * everywhere -- ESC pops back to node 2 with its cursor restored -- and
 * our DispatchGameState draws the theme nodes with Menu_RenderLevelSelect.
 * Levels are grouped by the theme folder that prefixes their JJ.GAM name
 * ("Forest\\Start" -> "Forest").
 */
#pragma once

class Game;

static const unsigned char LS_THEME_NODE = 0x60;    /* 0x60 .. 0x6f */
static const int           LS_MAX_THEMES = 16;
static const unsigned char LS_START      = 0x70;

/* How many rows the page shows; a longer theme scrolls. */
static const int LEVELSELECT_ROWS = 7;

/* Node 0x60 + t, t below the theme count. */
bool LevelSelect_IsThemeNode(unsigned char node);

/* HandleKeypress, on the RIGHT/LEFT polls it already makes (dir +1 / -1):
 * from node 2, RIGHT descends to the page of the last-played level's theme;
 * on a theme page, either moves to the next/previous theme. */
void LevelSelect_Turn(Game *g, int dir);

/* HandleKeypress, node LS_START: pops back to the theme page and returns the
 * JJ.GAM index of the row ENTER was pressed on. */
int LevelSelect_Chosen(Game *g);

/* What the page draws: the current theme's name, the visible window of its
 * levels, and the selected row's position within that window. */
struct LevelSelectView {
    const char *theme;
    int         count;
    const char *rows[LEVELSELECT_ROWS];
    int         selected;
};
void LevelSelect_View(Game *g, LevelSelectView *v);
