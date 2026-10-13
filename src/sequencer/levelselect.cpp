/* The level select's logic: opening the page, turning the theme and choosing
 * a level.  The theme table is session/levelthemes.h. */
#include "levelselect.h"
#include "frontend.h"
#include "session.h"
#include "levelthemes.h"
#include "menutree.h"
#include "logger.h"
#include <stdlib.h>
#include <strings.h>

/* Builds the table from the session's levels, then the theme pages' children. */
static void build_nodes(FrontEnd *frontEnd, Session *session)
{
    MenuTree *m = frontEnd->menu();
    LevelThemes_Build(session);
    for (int t = 0; t < LevelThemes_Count(); t++) {
        const unsigned char node = (unsigned char)(LS_THEME_NODE + t);
        m->setChildCount(node, (unsigned char)LevelThemes_Size(t));
        for (int i = 0; i < LevelThemes_Size(t); i++)
            m->setChild(node, (unsigned char)i, LS_START);
    }
}

void LevelSelect_Turn(FrontEnd *frontEnd, Session *session, int dir)
{
    MenuTree *m = frontEnd->menu();
    if (m->node() == 2) {
        if (dir < 0)
            return;
        build_nodes(frontEnd, session);
        const int count = LevelThemes_Count();
        if (count == 0)
            return;
        // Open on the theme of the level last loaded.
        int t = 0;
        for (int i = 0; i < count; i++)
            if (session->levelIndex() >= LevelThemes_First(i))
                t = i;
        // Descend as the navigator's Enter does.
        m->setSavedCursor(2, m->cursor());
        m->setCursor(0);
        m->push(2);
        m->setNode((unsigned char)(LS_THEME_NODE + t));
        LevelThemes_SetTop(0);
        g_logger.write("levelselect: open, %d themes, %d levels\n",
                  count, (int)session->levelCount());
        return;
    }
    if (!LevelThemes_IsThemeNode(m->node()))
        return;
    // Sideways replaces the page and leaves the stack, so Escape still goes
    // back to Load Game.
    const int count = LevelThemes_Count();
    int t = (m->node() - LS_THEME_NODE + dir + count) % count;
    m->setNode((unsigned char)(LS_THEME_NODE + t));
    if (m->cursor() >= LevelThemes_Size(t))
        m->setCursor((unsigned char)(LevelThemes_Size(t) - 1));
}

int LevelSelect_Chosen(FrontEnd *frontEnd, Session *session)
{
    MenuTree *m = frontEnd->menu();
    m->pop();  // node is the theme page, cursor the row
    int level = LevelThemes_First(m->node() - LS_THEME_NODE) + m->cursor();
    g_logger.write("levelselect: start level %d (%s)\n",
              level, session->levelNameTableEntry((unsigned char)level));
    return level;
}

int LevelSelect_Find(Session *session, const char *spec)
{
    const int count = session->levelCount();
    char *end;
    const long n = strtol(spec, &end, 10);
    if (end != spec && *end == 0)
        return n >= 0 && n < count ? (int)n : -1;
    for (int i = 0; i < count; i++)
        if (strcasecmp(session->levelNameTableEntry((unsigned char)i), spec) == 0)
            return i;
    return -1;
}
