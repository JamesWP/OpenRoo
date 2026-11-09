/* World: the rules a tick applies to the level as a whole -- the timed foe
 * spawners, the player's bomb drop and switches, and the foes.  Core_GameTick
 * (core/gametick.cpp) calls them in its fixed order.
 *
 * PRESERVED:
 *   - a bomb drop, the player's or a foe's (Foe::dropBomb), refused as too
 *     soon leaves its request flag set.
 */

#include "world.h"
#include "foetype.h"
#include "logger.h"
#include "prof.h"
#include "tilequery.h"

/* The bridge a switch operates, if it is one (0xff is none) and the bridge
 * is at rest. */
static BridgeObject *idle_bridge(World &w, unsigned char sw)
{
    if (sw == 0xff)
        return nullptr;
    BridgeObject *br = w.bridge(sw);
    return (br != nullptr && br->armed() == 0) ? br : nullptr;
}

/* Arms the bridge from the switch at (u, v): the switch tile blocks while
 * the bridge is out, and every tile of the switch shows its phase. */
static void arm_bridge(World &w, BridgeObject *br, unsigned char sw, int u, int v)
{
    g_logger.logMessage(1, "GAME: switch triggered %d", (unsigned int)sw);
    w.map()->tile(u, v)->setBusy(br->phase() == 0 ? 1 : 0);
    br->arm(w.now());
}

void World::spawnTimedFoes()
{
    for (unsigned i = 0; i < census_.timed; ++i) {
        TimedSpawner &E = timedSpawners_[i];
        if (!(clock_ - (double)E.lastSpawn > (double)E.interval))
            continue;
        signed char u = (signed char)E.u, v = (signed char)E.v;
        if (map_.tile(u, v)->occupant() == 0 && foes_.size() < E.maxFoes) {
            Foe &foe = Foe::spawn(entityContext(), foes_, (unsigned char)u, (unsigned char)v,
                                  E.height, 2, (unsigned char)(E.field_0b + FOE_FACING_BASE));
            // Every 15th foe carries a life.
            foe.setDropContents(((int)foesKilled_ + 1) % 15 == 0 ? 7 : 1);
        }
        E.lastSpawn = clock_;
    }
}

void World::dropPlayerBomb()
{
    Player &pl = player_;
    if ((unsigned int)pl.bombDropRequest() == 0)
        return;
    bool behind = false;
    if ((unsigned int)pl.moveDir() != 0) {
        // Moving: only early in the step, and then in the cell it left.
        if (clock_ - (double)pl.animStart() >= 50.0)
            return;
        behind = true;
    }
    if (behind)
        Bomb::spawn(entityContext(), bombs_,
                    (unsigned char)((unsigned char)pl.cellU() - (unsigned char)pl.stepU()),
                    (unsigned char)((unsigned char)pl.cellV() - (unsigned char)pl.stepV()),
                    (unsigned char)((unsigned char)pl.heightCell() - (unsigned char)pl.field141()),
                    pl.facing());
    else
        Bomb::spawn(entityContext(), bombs_, (unsigned char)pl.cellU(), (unsigned char)pl.cellV(),
                    (unsigned char)pl.heightCell(), pl.facing());
    pl.setBombDropRequest(0);
}

void World::playerSwitch(PlayEvents &events)
{
    unsigned char sw = player_.switchSlot();
    if (BridgeObject *br = idle_bridge(*this, sw)) {
        arm_bridge(*this, br, sw, player_.cellU(), player_.cellV());
        br->playArmSound(events);
        Sim_MarkSwitchTiles(&map_, br->phase(), sw);
        player_.setSwitchSlot(0xff);
    }
}

void World::tickFoes(bool running, PlayEvents &events)
{
    if (foes_.empty())
        return;
    PROF_SCOPE("foes");
    const Player &pl = player_;
    for (std::size_t i = 0; i < foes_.size();) {
        Foe &foe = foes_[i];

        unsigned char sw = foe.switchSlot();
        if (BridgeObject *br = idle_bridge(*this, sw)) {
            arm_bridge(*this, br, sw, foe.cellU(), foe.cellV());
            Sim_MarkSwitchTiles(&map_, br->phase(), sw);
            foe.clearSwitchSlot();
        }

        // Held while play is stopped, the foes are frozen, or the player is down.
        int hold = (!running || (unsigned int)pl.freezeActive() != 0 || pl.moveState() != 0) ? 1 : 0;
        unsigned char tu, tv;
        {
            PROF_SCOPE("choose target");
            foe.chooseTarget(entityContext(), foes_, hold, (unsigned char)pl.cellU(),
                             (unsigned char)pl.cellV(), pl.markerCellU(), pl.markerCellV(), &tu, &tv);
        }
        {
            PROF_SCOPE("step");
            foe.step(entityContext(), tu, tv, events);
        }
        foe.dropBomb(entityContext(), bombs_);
        foe.checkPlayerContact(player_.moveStateRef(), pl.posU(), pl.posY(), pl.posV());
        if (foe.finishDespawn(&map_)) {
            Foe::remove(foes_, i, map_, events);  // the next moves into i
            ++foesKilled_;
        } else {
            ++i;
        }
    }
}
