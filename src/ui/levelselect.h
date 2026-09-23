/* Level select -- our own addition, not a replacement.  A hidden page on the
 * Load Game screen (menu node 2): RIGHT opens it; LEFT/RIGHT change theme,
 * UP/DOWN pick a level, ENTER starts a fresh game there, ESC goes back.
 *
 * The page is not a menu-tree node.  The tree is built by the game and its
 * node ids double as actions (menutree.h), so the page is a flag layered
 * over node 2 instead: HandleKeypress hands it the frame's input and skips
 * the navigator while it is open, and RenderRestoreSlotList draws it in
 * place of the slot names.  Levels are grouped by the theme folder that
 * prefixes their JJ.GAM name ("Forest\\Start" -> "Forest").
 */
#pragma once

class Game;

/* True while the page is showing.  Closes itself if the menu has left node 2
 * (a level started, or the menu was rewound) since the last call. */
bool LevelSelect_Active(Game *g);

/* HandleKeypress, node 2, page closed: RIGHT (already polled there) opens it. */
void LevelSelect_Open(Game *g);

/* One frame of the page's input.  Returns the JJ.GAM index to start, or -1. */
int LevelSelect_Poll(Game *g);

/* How many rows the page shows; a longer theme scrolls. */
static const int LEVELSELECT_ROWS = 7;

/* What the page draws: the current theme's name, the visible window of its
 * levels, and the selected row's position within that window. */
struct LevelSelectView {
    const char *theme;
    int         count;
    const char *rows[LEVELSELECT_ROWS];
    int         selected;
};
void LevelSelect_View(Game *g, LevelSelectView *v);
