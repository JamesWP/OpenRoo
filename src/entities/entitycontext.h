/* What the level's entities need of the game around them, handed to them at
 * spawn so they know nothing of Game: the shared clock, tick step, map,
 * and switches.  The list each kind lives in (entitylist.h) is
 * passed beside it. */
#pragma once

struct TickStep;
class  LevelMap;
class  SwitchCells;

struct EntityContext {
    double       *clock;
    TickStep     *tickStep;
    LevelMap     *map;
    /* The switch cells and the highest switch slot in use (a view of the
     * World's, so it follows the level being built). */
    SwitchCells         *switches;
    const unsigned char *switchMax;
    /* The level's name (diagnostics) and the level's crystal count (World's
     * crystalsInLevel), which spawning a foe of type 0x0b raises. */
    const char     *levelName;
    unsigned short *crystalsInLevel;
};
