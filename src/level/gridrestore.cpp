/* Restores each tile from the snapshot taken when the level was built, when
 * the player restarts it.  Height, object marker and parameter are copied
 * back; the contents follow these rules, in this order:
 *   - a crystal taken since the snapshot stays taken;
 *   - a bombable tile that is busy becomes marker 1, in the snapshot
 *     as well as the live tile (PRESERVED: the snapshot is written);
 *   - the tile's own respawn contents, if any, are put back;
 *   - an extra life taken since the snapshot stays taken;
 *   - any other contents come back from the snapshot.
 * Both extents are re-read every pass.
 *
 * KAROO_SIM_FX=gridkeep is a negative control: the contents are left alone, so
 * everything consumed stays consumed across a restart.
 * KAROO_GRIDRESTORE_DIAG=1 logs each call. */

#include <stdint.h>
#include "sysdev.h"
#include <string.h>
#include "logger.h"
#include "levelmap.h"
#include "gridrestore.h"

static int s_fx = 0, s_diag = 0, s_init = 0;
static unsigned s_calls = 0;

static void fx_init(void)
{
    char buf[64];
    uint32_t n;

    if (s_init)
        return;
    s_init = 1;

    n = sysdev::getEnv("KAROO_SIM_FX", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "gridkeep") == 0) {
        s_fx = 1;
        g_logger.write("gridrestore: KAROO_SIM_FX=gridkeep -- tile state bytes "
                  "not restored\n");
    }
    n = sysdev::getEnv("KAROO_GRIDRESTORE_DIAG", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0)
        s_diag = 1;
}

  void  
Sim_RestoreTileGridFromSnapshot(LevelMap *map)
{
    fx_init();
    ++s_calls;
    if (s_diag)
        g_logger.write("gridrestore: call %u extents 727=%u 728=%u\n", s_calls,
                  map->extentV(), map->extentU());

    // Outer loop over v, inner over u.
    for (unsigned r = 0; r < map->extentV(); ++r) {
        for (unsigned c = 0; c < map->extentU(); ++c) {
            Tile *t = map->tile((int)c, (int)r);
            Tile *s = map->snapshot((int)c, (int)r);

            t->setHeight(s->height());
            t->setObjectMarker(s->objectMarker());
            t->setParam(s->param());
            if (s->contents() == CONTENTS_CRYSTAL && t->contents() != CONTENTS_CRYSTAL && !s_fx)
                t->setContents(0);
            if (s->objectMarker() == TILE_BOMBABLE && t->busy() != 0) {
                s->setObjectMarker(1);
                t->setObjectMarker(1);
            }
            if (s_fx)
                continue;  // gridkeep: no contents store at all
            if (t->field202() != CONTENTS_NONE)
                t->setContents(t->field202());
            if (s->contents() == CONTENTS_EXTRA_LIFE && t->contents() != CONTENTS_EXTRA_LIFE)
                t->setContents(0);
            if (s->contents() != CONTENTS_EXTRA_LIFE && s->contents() != CONTENTS_CRYSTAL)
                t->setContents(s->contents());
        }
    }
}
