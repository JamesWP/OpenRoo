#include "whatif.h"
#include "tile.h"
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

void restoreFallings(World &w)
{
    const Player *p = w.player();
    for (FallingTile &f : w.fallings()) {
        Tile *t = f.tile(*w.map());
        if (t->objectMarker() != TILE_FALLING) {   // fallen
            t->setObjectMarker(TILE_FALLING);
            t->setBusy(0);
            f.respawnPending_ = 0;
            f.armed_ = 0;
        } else if (f.armed_ && !(f.cellU_ == p->cellU() && f.cellV_ == p->cellV())) {
            f.armed_ = 0;
        }
    }
}

}  // namespace whatif
