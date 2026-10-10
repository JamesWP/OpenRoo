#include "tilenames.h"
#include <stddef.h>
#include "tile.h"

const char *Tile_ContentsName(uint8_t c)
{
    switch (c) {
    case CONTENTS_NONE:        return NULL;
    case CONTENTS_CRYSTAL:     return "crystal";
    case CONTENTS_PARAGLIDER:  return "paraglider";
    case CONTENTS_TIME_BONUS:  return "time bonus";
    case CONTENTS_EXTRA_LIFE:  return "extra life";
    case CONTENTS_FREEZE:      return "freeze";
    case CONTENTS_GRANT_09:    return "3 bombs";
    case CONTENTS_SPEED_UP:    return "speed up";
    case CONTENTS_REVERSED:    return "reversed controls";
    case CONTENTS_SPEED_DOWN:  return "slow down";
    case CONTENTS_TRANSFORM:   return "transform";
    case CONTENTS_FREE_BOMB:   return "bomb";
    case CONTENTS_TIMED_SPAWN: return "foe spawner";
    default:                   return "item";
    }
}
