#include "quitforward.h"
#include "game.h"
#include "windev.h"

void Core_ForwardQuit(Game *g)
{
    if (g->quitRequested()) {
        windev::quit(g->quitCode());
        g->clearQuitRequest();
    }
}
