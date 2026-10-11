/* The level select page's view: what Menu_RenderLevelSelect draws.  The
 * page's logic is flow/levelselect.h; its theme table is session/levelthemes.h. */
#pragma once

class FrontEnd;
class Session;

/* Rows the page shows; a longer theme scrolls. */
static const int LEVELSELECT_ROWS = 7;

/* What the page draws: the theme's name, the visible window of its levels, and
 * the selected row's place in that window. */
struct LevelSelectView {
    const char *theme;
    int         count;
    const char *rows[LEVELSELECT_ROWS];
    int         selected;
};

/* Fills v for the page the menu is on, and scrolls the table's top so the
 * selected row stays visible. */
void LevelSelect_View(const FrontEnd *frontEnd, Session *session, LevelSelectView *v);
