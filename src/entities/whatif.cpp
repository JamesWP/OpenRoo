#include "whatif.h"
#include "world.h"

namespace whatif {

void removeFoes(World &w)
{
    PlayEvents unheard;
    for (Foe &foe : w.foes())
        foe.despawn(*w.map(), unheard);
    w.foes().clear();
    for (unsigned i = 0; i < w.census()->timed; i++)
        w.timedSpawner(i)->maxFoes = 0;
}

}  // namespace whatif
