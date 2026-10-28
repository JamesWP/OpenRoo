/* World: the whole-table operations on the level's objects.  Each walks one
 * or more of the entity tables, in the order the game always has. */

#include "world.h"
#include "prof.h"

BridgeObject *World::bridge(unsigned char switchSlot)
{
    for (BridgeObject &b : bridges_)
        if (b.switchSlot() == switchSlot)
            return &b;
    return nullptr;
}

const BridgeObject *World::bridge(unsigned char switchSlot) const
{
    return const_cast<World *>(this)->bridge(switchSlot);
}

/* Releases each object's sounds, then destroys them all, first to last. */
void World::clearObjects(PlayEvents &events)
{
    for (const LiftObject &lift : lifts_)
        events.entityDestroyed(SoundSource::Lift, lift.id());
    lifts_.clear();
    for (const PlatformObject &platform : platforms_)
        events.entityDestroyed(SoundSource::Platform, platform.id());
    platforms_.clear();
    for (const FallingTile &falling : fallings_)
        events.entityDestroyed(SoundSource::Falling, falling.id());
    fallings_.clear();
    for (const BridgeObject &bridge : bridges_)
        events.entityDestroyed(SoundSource::Bridge, bridge.id());
    bridges_.clear();

    // The 256 switch counts.
    switchCells_.clearCounts();

    // A foe frees its tile claims as it goes.
    for (Foe &foe : foes_)
        foe.despawn(events);
    foes_.clear();
    for (const Bomb &bomb : bombs_)
        events.entityDestroyed(SoundSource::Bomb, bomb.id());
    bombs_.clear();
}

/* Ticks every object in a list, in order; a tick does not add or remove. */
template <class T>
static void tickAll(EntityList<T> &list, const char *name, PlayEvents &events)
{
    if (list.empty())
        return;
    PROF_SCOPE(name);
    for (T &obj : list)
        obj.tick(events);
}

void World::tickLifts(PlayEvents &events) { tickAll(lifts_, "lifts", events); }
void World::tickPlatforms(PlayEvents &events) { tickAll(platforms_, "platforms", events); }
void World::tickFallings(PlayEvents &events) { tickAll(fallings_, "falling tiles", events); }

void World::tickBridges(PlayEvents &events)
{
    if (bridges_.empty())
        return;
    PROF_SCOPE("bridges");
    for (BridgeObject &b : bridges_) {
        b.tick(events);
        b.debugDraw(switchCells_);
    }
}

void World::tickBombs(PlayEvents &events)
{
    if (bombs_.empty())
        return;
    PROF_SCOPE("bombs");
    for (std::size_t i = 0; i < bombs_.size();) {
        Bomb &b = bombs_[i];
        b.tick(events);
        b.debugDraw((unsigned)i);
        if (b.removeRequested() != 0)
            Bomb::remove(bombs_, i, events);  // the next moves into i
        else
            ++i;
    }
}

