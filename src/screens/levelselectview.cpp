#include <string.h>
#include "levelselectview.h"
#include "frontend.h"
#include "session.h"
#include "levelthemes.h"
#include "menutree.h"

void LevelSelect_View(const FrontEnd *frontEnd, Session *session, LevelSelectView *v)
{
    const int t   = frontEnd->menu()->node() - LS_THEME_NODE;
    const int n   = LevelThemes_Size(t);
    const int row = frontEnd->menu()->cursor();
    int top = LevelThemes_Top();
    // Scroll only as far as keeps the selection visible.
    if (row < top)
        top = row;
    if (row >= top + LEVELSELECT_ROWS)
        top = row - LEVELSELECT_ROWS + 1;
    if (top > n - LEVELSELECT_ROWS)
        top = n > LEVELSELECT_ROWS ? n - LEVELSELECT_ROWS : 0;
    LevelThemes_SetTop(top);

    v->theme = LevelThemes_Name(t);
    v->count = n - top < LEVELSELECT_ROWS ? n - top : LEVELSELECT_ROWS;
    for (int i = 0; i < v->count; i++) {
        const char *name = session->levelNameTableEntry((unsigned char)(LevelThemes_First(t) + top + i));
        const char *bs = strchr(name, '\\');
        v->rows[i] = bs ? bs + 1 : name;
    }
    v->selected = row - top;
}
