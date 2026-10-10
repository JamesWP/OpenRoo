/* World: the whole-table operations on the level's objects.  Each walks one
 * or more of the entity tables, in the order the game always has. */

#include "world.h"
#include "prof.h"
#include "bomb.h"
#include "foe.h"
#include "liftobject.h"
#include "platformobject.h"
#include "bridgeobject.h"
#include "fallingtile.h"

void World::clearObjects()
{
    LiftObject::purgeAll(entityContext(), lifts_);
    PlatformObject::purgeAll(entityContext(), platforms_);
    FallingTile::purgeAll(entityContext(), fallings_);
    BridgeObject::purgeAll(entityContext(), bridges_);

    // The 256 switch counts.
    switchCells_.clearCounts();

    while (foes_.count != 0)
        Foe::remove(entityContext(), foes_, foes_.ids[0]);
    while (bombs_.count != 0)
        Bomb::remove(entityContext(), bombs_, bombs_.ids[0]);

    fallings_.count = 0;
    foes_.count     = 0;
    lifts_.count    = 0;
    platforms_.count = 0;
    bombs_.count    = 0;
}

void World::tickLifts()
{
    if (lifts_.count == 0)
        return;
    PROF_SCOPE("lifts");
    for (int i = 0; i < (int)lifts_.count; ++i)
        lifts_.slot[i]->tick();
}

void World::tickPlatforms()
{
    if (platforms_.count == 0)
        return;
    PROF_SCOPE("platforms");
    for (int i = 0; i < (int)platforms_.count; ++i)
        platforms_.slot[i]->tick();
}

void World::tickFallings()
{
    if (fallings_.count == 0)
        return;
    PROF_SCOPE("falling tiles");
    for (int i = 0; i < (int)fallings_.count; ++i)
        fallings_.slot[i]->tick();
}

void World::tickBridges()
{
    if (bridges_.count == 0)
        return;
    PROF_SCOPE("bridges");
    for (int i = 0; i < (int)bridges_.count; ++i) {
        bridges_.slot[i]->tick();
        bridges_.slot[i]->debugDraw(switchCells_);
    }
}

void World::tickBombs()
{
    // Bombs: the count is re-read each pass; no step back after a removal.
    if (bombs_.count == 0)
        return;
    PROF_SCOPE("bombs");
    for (int i = 0; i < (int)bombs_.count; ++i) {
        bombs_.slot[bombs_.ids[i]]->tick();
        unsigned char id = bombs_.ids[i];
        bombs_.slot[id]->debugDraw(id);
        if (bombs_.slot[id]->removeRequested() != 0)
            Bomb::remove(entityContext(), bombs_, id);
    }
}
