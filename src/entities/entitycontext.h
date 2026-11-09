/* What the level's entities need of the game around them, handed to them at
 * spawn so they know nothing of Game: the shared clock, tick step and map.
 * The list each kind lives in (entitylist.h) is
 * passed beside it. */
#pragma once

struct TickStep;
class  LevelMap;

struct EntityContext {
    double       *clock;
    TickStep     *tickStep;
    LevelMap     *map;
    /* The level's name (diagnostics). */
    const char     *levelName;
};
