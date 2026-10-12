/* The level builder: clears the run's per-level state, tears down the previous
 * level's objects, finds the player start, seeds rand(), then walks the whole
 * map once (v outer, u inner), dispatching on each cell's kind: spawning
 * lifts, platforms, falling tiles, bridges and foes, filing switch cells and
 * teleport pairs, and counting what the score and the HUD need.
 *
 * DETERMINISM: the seed is time() through hooks_GameTime, so KAROO_SEED
 * governs the run, and it is stored in the game's single rand() seed
 * (crtrand.h), which every rand() caller shares.
 *
 * PRESERVED:
 *   - The marker-4 lookup's result is ignored and its output is written
 *     only on success, so on a level without a marker-4 cell those three
 *     bytes keep the previous level's values.
 *   - Every cell's parameter byte is cleared at the end of its visit, except
 *     on a cell that just spawned a kind 2 foe.
 *   - The teleport search clears this cell's parameter before searching,
 *     which is the only reason a teleporter cannot pair with itself.
 *   - A bombable cell moves its contents into its hidden slot, so those
 *     items count as shadow1/shadow7 rather than crystals and extra lives.
 *   - The start (kind 3) is rewritten to kind 1 in the middle of its visit,
 *     before the later tests see it.
 *   - Timed spawners split on 100: a parameter below 100 gives a cap of 5
 *     foes and an interval of param * 1000 ms; 100 or more a cap of
 *     param - 100 and 5000 ms.
 *   - The census reset leaves field_e5 alone.
 *
 * KAROO_SETUP_DIAG=1 logs the extents, the spawn census and the totals. */

#include <stdint.h>
#include "sysdev.h"
#include <string.h>

#include "logger.h"
#include "player.h"
#include "crtrand.h"
#include "game.h"
#include "bomb.h"
#include "tilequery.h"
#include "levelsetup.h"
#include "liftobject.h"
#include "platformobject.h"
#include "bridgeobject.h"
#include "fallingtile.h"
#include "foe.h"

/* The map and its tiles (levelmap.h, tile.h).  The snapshot grid holds the
 * cell as the file gave it: height, param and contents. */

/* The counters are the World's census (levelcensus.h); the free-bomb and
 * timed-spawner tables it indexes are World::freeBomb() and timedSpawner(). */
#define G_CLOCK            0x170a54  // the game clock, a double

#define G_LEVEL_NAME       0x173483

/* The two float constants the item phase uses, bit for bit: 2pi and 1/32767.
 */
#define K_TWO_PI   0x1.921fb6p+2f  // 2pi as a float
#define K_INV_32K  0x1.0002p-15f   // 1/32767 as a float


#include "gametime.h"


typedef unsigned int   __attribute__((aligned(1))) u32_ua;
typedef int            __attribute__((aligned(1))) i32_ua;
typedef unsigned short __attribute__((aligned(1))) u16_ua;
typedef float          __attribute__((aligned(1))) f32_ua;
typedef double         __attribute__((aligned(1))) f64_ua;

#define B(g, off)    (*((unsigned char *)(g) + (off)))
#define SB(g, off)   (*(signed char *)((unsigned char *)(g) + (off)))
#define W(g, off)    (*(u16_ua *)((unsigned char *)(g) + (off)))
#define DW(g, off)   (*(u32_ua *)((unsigned char *)(g) + (off)))
#define F(g, off)    (*(f32_ua *)((unsigned char *)(g) + (off)))
#define D(g, off)    (*(f64_ua *)((unsigned char *)(g) + (off)))

/* The Player is a fixed part of the World. */

static int s_diag         = 0;
static int s_init         = 0;

static unsigned s_calls = 0;

static void diag_init(void)
{

    if (s_init)
        return;
    s_init = 1;

    s_diag = sysdev::envFlag("KAROO_SETUP_DIAG");
}

/* The cell at (u, v). */
static inline Tile *CELL(LevelMap *m, unsigned u, unsigned v)
{
    return m->tile((int)u, (int)v);
}

/* The same cell from signed bytes: level data, sign-extended as the game does.
 */
static inline Tile *SCELL(LevelMap *m, int u, int v)
{
    return m->tile(u, v);
}

/* Zeroes the census in the game's store order, which is not ascending, leaving
 * field_e5 as it was. */
  void  
Sim_ResetLevelObjectCounters(Game *self)
{
    self->world()->census()->reset();
}

/* Finds the first cell of kind marker, v outer and u inner, and writes u, v
 * and its height to out[0..2]; returns 1.  Returns 0 and leaves out untouched
 * if there is none, which a caller relies on. */
  int  
Sim_FindTileByTypeMarker(LevelMap *map, unsigned int markerArg,
                         unsigned char *out)
{
    unsigned char marker = (unsigned char)markerArg;
    unsigned char v, u;

    if (map->extentV() == 0)
        return 0;

    v = 0;
    do {
        u = 0;
        if (map->extentU() != 0) {
            do {
                Tile *t = CELL(map, u, v);
                if (t->objectMarker() == marker) {
                    out[0] = u;
                    out[1] = v;
                    out[2] = t->height();
                    return 1;
                }
                u = (unsigned char)(u + 1);
            } while (u < map->extentU());
        }
        v = (unsigned char)(v + 1);
    } while (v < map->extentV());

    return 0;
}

/* KAROO_CRT_DIAG=1 logs the value in the game's seed global right after it is
 * stored: a read-back, which costs no rand() call. */
static void levelsetup_seed_diag(unsigned int seed)
{
    if (!sysdev::envFlag("KAROO_CRT_DIAG"))
        return;

    g_logger.write("levelsetup: crt_srand(%u) -> CRT seed = %u\n",
              seed, CRT_RAND_SEED);
}

  unsigned int  
Sim_SetupLevelObjects(Game *self)
{
    LevelMap *M = self->world()->map();
    unsigned char v, u;
    unsigned char w, h;
    Tile *t, *s;

    diag_init();

    // The run state.
    // The level-complete node offers Save only when there is no next-level
    // bonus.
    self->frontEnd()->menu()->setChildCount(0x28, (self->session()->nextLevelBonus() == 0) ? 2 : 1);
    self->world()->player()->setLastSecondsMark(10.0);  // 10.0

    g_logger.logMessage(2, "GAME: init level started");

    self->audio()->setLevelSoundsReady(0);
    self->scriptPlayer()->releaseStreams();
    self->world()->extraObjects()->releaseSounds();
    // The game also makes a no-op call here; it has no effect.

    // The camera focus is zeroed; the scripted eye and the pose eye start at y 1000.
    self->camera()->setFocus(0, 0.0f);
    self->camera()->setFocus(1, 0.0f);
    self->camera()->setFocus(2, 0.0f);

    self->camera()->setScriptedEye(0, 0.0f);
    self->camera()->setScriptedEye(1, 1000.0f);
    self->camera()->setScriptedEye(2, 0.0f);

    CameraPose *cam = self->camera()->pose();  // camerarig.h: note the eye/target conflict
    cam->eye()[0] = 0.0f;
    cam->eye()[1] = 1000.0f;
    cam->eye()[2] = 0.0f;

    for (int i = 0; i < 3; i++)
        cam->target()[i] = self->camera()->focus(i);
    cam->setYaw(0.0f);
    cam->setPitch(0.0f);

    self->world()->census()->reset();

    self->world()->setCrystalsInLevel(0);
    self->setGemsGoalReached(0);
    self->world()->player()->setMovingBackwards(0);
    self->world()->player()->setTeleportPhase(0);
    self->world()->player()->setField11a(0);
    self->world()->player()->setIceDir(0);

    // Tear down the previous level.
    self->world()->clearObjects();
    self->world()->setSwitchMax(0);
    self->world()->player()->setLastRoll(0);

    self->world()->player()->setMap(M);

    // The player start, and the marker-4 cell.
    if (Sim_FindTileByTypeMarker(M, 3, self->world()->player()->homeRef())) {
        // From the signed bytes.
        t = SCELL(M, (signed char)self->world()->player()->homeU(), (signed char)self->world()->player()->homeV());
        self->world()->player()->setFacing(t->param());
    }

    // PRESERVED: the result is ignored; with no marker-4 cell the three bytes
    // keep the previous level's values.
    Sim_FindTileByTypeMarker(M, 4, self->world()->player()->markerCellRef());

    self->world()->player()->setMarker((float)(int)(signed char)self->world()->player()->markerCellU(),
                    (float)(int)(signed char)self->world()->player()->markerCellH(),
                    (float)(int)(signed char)self->world()->player()->markerCellV());

    self->world()->player()->setCell(self->world()->player()->homeU(), self->world()->player()->homeV(), self->world()->player()->homeH());

    SCELL(M, (signed char)self->world()->player()->homeU(), (signed char)self->world()->player()->homeV())->setObjectMarker(1);

    self->world()->player()->setPos((float)(int)self->world()->player()->cellU(), (float)(int)self->world()->player()->heightCell(), (float)(int)self->world()->player()->cellV());

    self->world()->player()->setLastMoveDir(0);
    self->world()->player()->setKind(4);
    self->world()->player()->setStepDuration(200.0);  // 200.0

    // DETERMINISM: time() through the seed hook, stored in the shared rand()
    // seed.
    {
        const unsigned int seed =
            (unsigned int)hooks_GameTime(0);
        crt_srand(seed);
        levelsetup_seed_diag(seed);
    }

    // Pass 1: clear one word per cell.
    h = M->extentV();
    if (h != 0) {
        v = 0;
        do {
            w = M->extentU();
            if (w != 0) {
                u = 0;
                do {
                    CELL(M, u, v)->setPlatformTrack(0);
                    u = (unsigned char)(u + 1);
                } while (u < M->extentU());
            }
            v = (unsigned char)(v + 1);
        } while (v < M->extentV());
    }

    // Pass 2: the walk.
    if (M->extentV() != 0) {
        v = 0;
        do {
            if (M->extentU() == 0)
                goto next_row;
            u = 0;
            do {
                t = CELL(M, u, v);
                s = M->snapshot((int)u, (int)v);

                t->setOccupant(0);
                t->setField1f6(0);
                t->setBlasting(false);
                t->setBlastFxPending(false);
                t->setBlastHeight(0);
                t->setField1a1(0);
                t->setBusy(0);
                t->setField202(0);

                if (t->contents() == CONTENTS_CRYSTAL)
                    self->world()->setCrystalsInLevel((unsigned short)(self->world()->crystalsInLevel() + 1));
                if (t->contents() == CONTENTS_EXTRA_LIFE) self->world()->census()->extraLives++;

                if (t->objectMarker() == TILE_KIND_01) self->world()->census()->kind01++;
                if (t->objectMarker() == TILE_STICKY) self->world()->census()->stickyPads++;
                if (t->objectMarker() == TILE_SLIDE) self->world()->census()->slideTiles++;
                if (t->objectMarker() == TILE_ICE) self->world()->census()->iceTiles++;

                if (t->objectMarker() == TILE_BOMBABLE) {
                    unsigned char item;
                    self->world()->census()->bombables++;
                    t->setBusy(0);
                    item = t->contents();
                    if (item != 0) {
                        t->setField202(item);
                        t->setContents(0);
                        if (t->field202() == CONTENTS_CRYSTAL) self->world()->census()->shadow1++;
                        if (t->field202() == CONTENTS_EXTRA_LIFE) self->world()->census()->shadow7++;
                    }
                }

                // A switch cell.
                if (t->objectMarker() == TILE_SWITCH) {
                    unsigned char param = t->param();
                    if (param == 0) {
                        g_logger.logMessage(1, "GAME: warning - switch with an index lower than 1 !!!");
                    } else {
                        unsigned char idx = (unsigned char)(param - 1);
                        if (idx > self->world()->switchMax())
                            self->world()->setSwitchMax(idx);
                        t->setField1f3(idx);
                        // The count is re-read for each store.
                        self->world()->switchCells()->setCellU(idx, self->world()->switchCells()->count(idx), u);
                        self->world()->switchCells()->setCellV(idx, self->world()->switchCells()->count(idx), v);
                        self->world()->switchCells()->setCount(idx, (unsigned char)(self->world()->switchCells()->count(idx) + 1));
                    }
                    t->setParam(0);
                }

                // Bridges along u, then along v.
                if (t->objectMarker() == TILE_BRIDGE_U) {
                    unsigned char param = t->param();
                    if (param == 0) {
                        g_logger.logMessage(1, "GAME: warning - X-bridge with an index lower than 1 !!!");
                    } else {
                        BridgeObject::spawn(self->world()->entityContext(), self->world()->bridges(), u, v, t->height(),
                                            (unsigned char)(param - 1), 1);
                        t->setParam(0);
                        self->world()->census()->bridges++;
                    }
                }
                if (t->objectMarker() == TILE_BRIDGE_V) {
                    unsigned char param = t->param();
                    if (param == 0) {
                        g_logger.logMessage(1, "GAME: warning - Y-bridge with an index lower than 1 !!!");
                    } else {
                        BridgeObject::spawn(self->world()->entityContext(), self->world()->bridges(), u, v, t->height(),
                                            (unsigned char)(param - 1), 2);
                        t->setParam(0);
                        self->world()->census()->bridges++;
                    }
                }

                if (t->objectMarker() == TILE_SLIDE) {
                    t->setSlideDir(t->param());
                    t->setParam(0);
                }

                // PRESERVED: the start becomes kind 1 before the tests below
                // see it.
                if (t->objectMarker() == TILE_START)
                    t->setObjectMarker(1);

                if (t->objectMarker() == TILE_LIFT) {
                    LiftObject::spawn(self->world()->entityContext(), self->world()->lifts(), u, v, t->height(), t->param());
                    t->setParam(0);
                }

                if (t->objectMarker() == TILE_JUMP_PAD) {
                    t->setField1f1(t->param());
                    self->world()->census()->jumpPads++;
                    t->setParam(0);
                }

                // A cell holding an item gets a random phase.
                if (t->contents() != CONTENTS_NONE) {
                    double ph = (double)(int)crt_rand();
                    ph = ph * (double)K_TWO_PI;
                    ph = ph * (double)K_INV_32K;
                    t->setItemPhase((float)ph);
                }

                if (t->objectMarker() == TILE_PLATFORM_U || t->objectMarker() == TILE_PLATFORM_V) {
                    t->setContents(0);
                    t->setParam(0);
                    PlatformObject::spawn(self->world()->entityContext(), self->world()->platforms(), u, v, t->height(),
                                       t->objectMarker());
                    t->setObjectMarker(0);
                }

                if (t->objectMarker() == TILE_FALLING)
                    FallingTile::spawn(self->world()->entityContext(), self->world()->fallings(), u, v, t->height(), t->param());

                // Teleport pairing.
                if (t->objectMarker() == TILE_TELEPORTER && t->param() != 0) {
                    unsigned char id = t->param();
                    unsigned char v2;

                    // PRESERVED: cleared first, the only reason the search
                    // below cannot match this very cell.
                    t->setParam(0);
                    t->setTeleportId(id);

                    if (M->extentV() != 0) {
                        v2 = 0;
                        do {
                            if (M->extentU() != 0) {
                                unsigned char u2 = 0;
                                do {
                                    Tile *t2 = CELL(M, u2, v2);
                                    if (t2->objectMarker() == TILE_TELEPORTER &&
                                        t2->param() == id) {
                                        self->world()->census()->teleports++;
                                        t->setTeleportU(u2);
                                        t->setTeleportV(v2);
                                        t2->setTeleportU(u);
                                        t2->setTeleportV(v);
                                        t2->setParam(0);
                                    }
                                    u2 = (unsigned char)(u2 + 1);
                                } while (u2 < M->extentU());
                            }
                            v2 = (unsigned char)(v2 + 1);
                        } while (v2 < M->extentV());
                    }
                }

                // The snapshot: the cell as the file gave it.
                if (s->contents() == CONTENTS_TRANSFORM) self->world()->census()->transforms++;
                if (s->contents() == CONTENTS_FREEZE) self->world()->census()->freezeItems++;
                if (s->contents() == CONTENTS_TIME_BONUS) self->world()->census()->timeBonuses++;
                if (s->contents() == CONTENTS_PARAGLIDER) self->world()->census()->paragliders++;
                if (s->contents() == CONTENTS_SPEED_UP) self->world()->census()->speedUps++;
                if (s->contents() == CONTENTS_GRANT_09) self->world()->census()->grant09Items++;

                if (s->contents() == CONTENTS_FREE_BOMB) {
                    // The count is re-read for every store.
                    self->world()->freeBomb(self->world()->census()->freeBombs)->u = u;
                    self->world()->freeBomb(self->world()->census()->freeBombs)->v = v;
                    self->world()->freeBomb(self->world()->census()->freeBombs)->param = s->param();
                    // The clock, copied as a double.
                    self->world()->freeBomb(self->world()->census()->freeBombs)->placedAt =
                        *self->world()->clock();
                    g_logger.logMessage(3, "GAME: init level - freebomb %d created",
                                       (unsigned int)self->world()->census()->freeBombs);
                    self->world()->census()->freeBombs++;
                }

                if (s->contents() == CONTENTS_TIMED_SPAWN) {
                    unsigned idx;
                    unsigned char param;

                    // The count is re-read for every store.
                    self->world()->timedSpawner(self->world()->census()->timed)->u = u;
                    self->world()->timedSpawner(self->world()->census()->timed)->v = v;
                    self->world()->timedSpawner(self->world()->census()->timed)->height = s->height();

                    // The foe cap and the interval split on a parameter of
                    // 100.
                    param = s->param();
                    if (param < 0x64) {
                        self->world()->timedSpawner(self->world()->census()->timed)->maxFoes = 5;
                        param = s->param();
                        self->world()->timedSpawner(self->world()->census()->timed)->interval =
                            (double)(int)((unsigned)param * 1000u);
                    } else {
                        self->world()->timedSpawner(self->world()->census()->timed)->maxFoes =
                            (unsigned char)(param - 0x64);
                        // 5000.0 ms.
                        self->world()->timedSpawner(self->world()->census()->timed)->interval = 5000.0;
                    }

                    // Staggered: spawner k first fires k seconds late.
                    idx = self->world()->census()->timed;
                    {
                        double when = (double)(int)(idx * 1000u);
                        when = when + (double)*self->world()->clock();
                        self->world()->timedSpawner(idx)->lastSpawn = (double)when;
                    }
                    self->world()->census()->timed++;
                }

                // Foes.
                {
                    int spawned2 = 0;

                    if (s->contents() == CONTENTS_FOE_TYPE2) {
                        unsigned char param = t->param();
                        unsigned char hh;

                        if (param == 0x0b || param == 0x07) self->world()->census()->shadow1++;
                        if (t->param() == 0x4d)             self->world()->census()->shadow7++;

                        param = t->param();
                        hh    = t->height();
                        if (param == 0x06) {
                            unsigned char bump =
                                (unsigned char)((unsigned char)(u + v) + 0x0a);
                            t->setParam(0);
                            hh = (unsigned char)(hh + bump);
                        }
                        Foe::spawn(self->world()->entityContext(), self->world()->foes(), u, v, hh, 2, t->param());
                        t->setContents(0);
                        spawned2 = 1;
                    }

                    if (s->contents() == CONTENTS_FOE_TYPE3) {
                        Foe::spawn(self->world()->entityContext(), self->world()->foes(), u, v, t->height(), 3, t->param());
                        t->setContents(0);
                        t->setParam(0);
                    } else if (!spawned2) {
                        // PRESERVED: every cell except one that just spawned a
                        // kind 2 foe loses its parameter byte here.
                        t->setParam(0);
                    }
                }

                t->setLiftLiveHeight((float)(int)(unsigned)t->height());

                u = (unsigned char)(u + 1);
            } while (u < M->extentU());

next_row:
            v = (unsigned char)(v + 1);
        } while (v < M->extentV());
    }

    // Totals, and the rest of the reset.
    self->world()->player()->setPlatformSlot(0xff);
    self->setLevelPrepPending(1);

    // The collectable-item count (see LevelCensus), summed in this order.
    self->world()->census()->total = (unsigned short)(self->world()->census()->grant09Items + self->world()->census()->speedUps +
                                  self->world()->census()->shadow1      + self->world()->census()->transforms +
                                  self->world()->census()->paragliders  + self->world()->census()->shadow7 +
                                  self->world()->census()->freezeItems + self->world()->census()->extraLives +
                                  self->world()->census()->timeBonuses  + self->world()->crystalsInLevel());

    self->world()->player()->setMoveState(0);
    self->world()->player()->setLastContact(*self->world()->clock());

    self->world()->player()->clearEffects();

    self->world()->player()->setTickStep(self->world()->tickStep());
    self->world()->player()->setFreezeActive(0);
    self->world()->player()->setEffectBActive(0);
    self->world()->player()->setEffectAActive(0);
    self->world()->player()->setEffectCActive(0);
    self->world()->player()->setSwitchSlot(0xff);
    self->world()->player()->setFieldD3(0);
    self->world()->player()->setOnLift(0);
    self->world()->player()->setBombDropRequest(0);
    self->world()->player()->setCompletionNumerator(0);
    self->world()->setTimeLimit(M->fileTimeLimit());
    self->world()->player()->setClock(self->world()->clock());

    if (self->session()->restartCount() == 0) {
        self->session()->setFoesKilled(0);
        self->world()->player()->setItemsCollected(0);
        self->session()->setItemTotal(self->world()->census()->total);

        if (self->scriptPlayer()->loaded() == 0) {
            self->camera()->setFocus(0, (float)(int)(signed char)self->world()->player()->homeU());
            self->camera()->setFocus(1, (float)(int)(signed char)self->world()->player()->homeH());
            self->camera()->setFocus(2, (float)(int)(signed char)self->world()->player()->homeV());
        }

        if (self->audio()->cdThemes()->validateTrackLengths() == 0 &&
            self->session()->levelIndex() > 4) {
            unsigned char cu = self->world()->player()->homeU();
            unsigned char cv = self->world()->player()->homeV();
            if (Sim_FindNearestFlaggedTileInRadius(self->world()->map(), &cu, &cv, 0x14)) {
                CELL(M, cu, cv)->setContents(0);
                g_logger.logMessage(3, "GAME: CD is not in drive! Crystal at %d,%d token!",
                                   (unsigned int)cu, (unsigned int)cv);
            }
        }
    }

    self->world()->setTimeElapsed(0);
    self->session()->setPlayingTime(0);

    SCELL(M, self->world()->player()->cellU(), self->world()->player()->cellV())->setField1a1(0);

    self->world()->player()->setGlides(0);
    self->world()->player()->setBombsCarried(0);
    self->world()->player()->setEffectDActive(0);
    self->world()->player()->setFalling(0);
    self->world()->player()->setIdleStarted(0);
    self->world()->player()->setGliding(0);
    self->world()->player()->setHeld(0);
    self->world()->player()->setMoveDir(0);
    self->world()->player()->setPendingMove(0);
    self->world()->player()->setField12e(0);
    self->world()->player()->setAnim(0);
    self->world()->player()->setDying(0);
    self->world()->player()->setIdleDuration(500.0);  // 500.0
    self->camera()->setOverviewActive(0);

    g_logger.logMessage(1, "GAME: %d crystals in this level, %d needed",
                       (unsigned int)self->world()->crystalsInLevel(),
                       self->world()->gemsRequired());

    if ((int)((unsigned int)self->world()->player()->gemsCollected() + (unsigned int)self->world()->crystalsInLevel()) <
        self->world()->gemsRequired())
        g_logger.logMessage(3, "GAME: warning - not enough crystals to complete this level!!!!");

    // One argument: the level name.
    if (self->world()->extraObjects()->openFile(self->gameDir(), self->world()->levelName()) == 0) {
        self->world()->extraObjects()->setLoaded(0);
        g_logger.logMessage(1, "GAME: could not load LEO:%s.leo no extra-objects in this level",
                           self->world()->levelName());
    } else {
        self->world()->extraObjects()->setLoaded(1);
        g_logger.logMessage(1, "GAME: LEO-file %s loaded",
                           self->world()->levelName());
    }

    // The tilt in effect starts at 60 degrees.
    self->config()->setActiveCameraPitch(60.0f);

    s_calls++;
    if (s_diag)
        g_logger.write("levelsetup: DIAG call #%u map=%ux%u crystals=%u total=%u "
                  "bridges=%u teleports=%u lifts=%u platforms=%u falling tiles=%u "
                  "foes=%u freebombs=%u timed=%u switchmax=%u\n",
                  s_calls, (unsigned)M->extentU(), (unsigned)M->extentV(),
                  (unsigned)self->world()->crystalsInLevel(), (unsigned)self->world()->census()->total,
                  (unsigned)self->world()->census()->bridges, (unsigned)self->world()->census()->teleports,
                  (unsigned)self->world()->liftCount(), (unsigned)self->world()->platformCount(),
                  (unsigned)self->world()->fallingCount(), (unsigned)self->world()->foeCount(),
                  (unsigned)self->world()->census()->freeBombs, (unsigned)self->world()->census()->timed,
                  (unsigned)self->world()->switchMax());

    // Returns 0.
    return 0;
}
