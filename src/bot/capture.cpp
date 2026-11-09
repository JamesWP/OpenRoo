#include "capture.h"
#include "game.h"
#include "worldview.h"

namespace bot {

BotView capture(const Game &game)
{
    BotView view = viewOf(*game.world());
    const Session *s = game.session();
    view.level = s->levelNameTableEntry(s->levelIndex());
    return view;
}

}  // namespace bot
