/* The level loaders.  OpenLevelFile loads a level by number and is how every
 * gameplay level loads; ParseLevelFiles loads one by name (the menu's demo
 * backdrop).  Both, in order: set the current level name; build "<game
 * dir>\Levels\<name>" and save the currently loaded map name; read the map (on
 * success release the old .leo sounds and log, on failure log and quit); set mapChanged if the map name differs from the saved one; read
 * the level's script from "<game dir>\InstructionScripts\<name>".
 *
 * Before its load, OpenLevelFile peeks at the next level (on a first attempt,
 * and when there is a next level): it loads that level's map just to keep its
 * bonus flag in nextLevelBonus, then loads the real one.
 *
 * PRESERVED:
 *   - The map name is saved before the read and compared after; that order
 *     is the whole mapChanged mechanism.  The peek runs first, so the saved
 *     name is the next level's.
 *   - The peek's read is not checked.
 *   - A failed map read quits the game (windev::quit) and carries on to read the script.
 *   - Both loaders return 0 on every path.
 *   - Paths are formatted unbounded into 256-byte buffers.
 *
 * Negative controls (KAROO_SIM_FX): "levelshift" loads level N+1 whenever N is
 * asked for, failing every recording; "samelevel" saves the map name after the
 * read, so mapChanged is always 0 (the suite cannot see it).
 * KAROO_CRT_FX=path swaps the path's two parts, so no level loads.
 * KAROO_LEVELPARSE_DIAG=1 logs every load, the peek and the sound release. */

#include <windows.h>
#include "sysdev.h"
#include <stdio.h>
#include <string.h>

#include "logger.h"
#include "windev.h"
#include "game.h"
#include "levelparse.h"
#include "soundmanager.h"
#include "gamestr.h"
#include "gameglobals.h"

/* The map-changed flag and the peeked next level's bonus are Game fields. */

/* The map: its reader, the map name last loaded, and the bonus flag. */

/* OpenLevelFile's own messages, which carry the level number too. */

namespace audiodev { class Buffer; }


static int s_fx_samelevel  = 0;
static int s_fx_crtpath    = 0;
static int s_fx_levelshift = 0;
static int s_diag         = 0;
static int s_init         = 0;

static unsigned s_parses    = 0;
static unsigned s_opens     = 0;

static void fx_init(void)
{
    char buf[64];
    DWORD n;

    if (s_init)
        return;
    s_init = 1;

    n = sysdev::getEnv("KAROO_SIM_FX", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        if (strcmp(buf, "samelevel") == 0) {
            s_fx_samelevel = 1;
            g_logger.write("levelparse: KAROO_SIM_FX=samelevel -- the previous map "
                      "name is copied AFTER the read, so the +0x10 "
                      "map-changed flag is stuck at 0\n");
        } else if (strcmp(buf, "levelshift") == 0) {
            s_fx_levelshift = 1;
            g_logger.write("levelparse: KAROO_SIM_FX=levelshift -- the level-name "
                      "lookup reads entry N+1, so every load opens the NEXT "
                      "level's map and script\n");
        }
    }

    // KAROO_CRT_FX=path: swapping the two %s arguments breaks the path's
    // structure, so every recording fails at load.
    n = sysdev::getEnv("KAROO_CRT_FX", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "path") == 0) {
        s_fx_crtpath = 1;
        g_logger.write("levelparse: KAROO_CRT_FX=path -- the two %%s arguments to "
                  "our sprintf are swapped, so every level path is "
                  "nonsense\n");
    }

    n = sysdev::getEnv("KAROO_LEVELPARSE_DIAG", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0)
        s_diag = 1;
}

/* The MSVC inline string primitives. */

static void inline_strcpy(char *dst, const char *src)
{
    while ((*dst++ = *src++) != '\0')
        ;
}

/* Returns -1, 0 or 1. */
static int inline_strcmp(const unsigned char *a, const unsigned char *b)
{
    while (*a == *b) {
        if (*a == 0)
            return 0;
        a++;
        b++;
    }
    return (*a < *b) ? -1 : 1;
}

  unsigned int  
Sim_ParseLevelFiles(Game *self, const char *name)
{
    char path[256];  // PRESERVED: 256 bytes, unbounded
    char prev[256];  // the map name before the read
    int ok;

    fx_init();

    inline_strcpy(self->levelNameBuffer(), name);

    if (s_fx_crtpath)
        sprintf(path, GS_OPEN_FMT_LEVELS, name, g_gameDir);
    else
        sprintf(path, GS_OPEN_FMT_LEVELS, g_gameDir, name);

    // Saved before the read overwrites it: the mapChanged mechanism.  The
    // samelevel control moves this copy after the read.
    if (!s_fx_samelevel)
        inline_strcpy(prev, self->map()->mapName());

    ok = self->map()->readFile(path);

    if (s_fx_samelevel)
        inline_strcpy(prev, self->map()->mapName());

    if ((char)ok != 0) {
        self->extraObjects()->releaseSounds();
        g_logger.logMessage(1, "GAME: level (Bonus=%d) loaded by name: %s.jjm",
                           self->map()->bonus(), path);
    } else {
        g_logger.logMessage(4, "GAME: ** error ** could not load level by name: %s.jjm (maybe it does not exist\077)", path);
        windev::quit(1);
    // PRESERVED: carries on; does not return.
    }

    self->setMapChanged(0);
    if (inline_strcmp((const unsigned char *)prev,
                      (const unsigned char *)self->map()->mapName()) != 0)
        self->setMapChanged(1);

    sprintf(path, GS_OPEN_FMT_SCRIPTS, g_gameDir, name);
    self->scriptPlayer()->setLoaded(0);
    self->scriptPlayer()->readForLevel(path);

    g_logger.logMessage(1,
                       self->scriptPlayer()->loaded() ? "GAME: instruction-script loaded: %s.jjs"
                                                             : "GAME: could not load instruction-script: %s.jjs ,running in observation-mode only...",
                       path);

    s_parses++;
    if (s_diag)
        g_logger.write("levelparse: DIAG parse #%u name=\"%s\" map=%s changed=%u "
                  "script=%u released=%u\n",
                  s_parses, name, (char)ok ? "ok" : "FAILED",
                  self->mapChanged(),
                  self->scriptPlayer()->loaded(), ExtraObjects::releasedCount());

    // PRESERVED: 0 on success and failure alike.
    return 0;
}

  unsigned int  
Sim_SetCurrentLevelName(Game *self, unsigned int levelNo)
{

    if (s_fx_levelshift)
        levelNo = levelNo + 1;

    inline_strcpy(self->levelNameBuffer(),
                  self->levelNameTableEntry((unsigned char)(levelNo & 0xff)));

    // The upper bytes of the game's return are the copied length; unreadable.
    return 0;
}

  unsigned int  
Sim_OpenLevelFile(Game *self, unsigned int levelNo)
{
    char path[256];  // PRESERVED: 256 bytes, unbounded
    char prev[256];  // the map name before the read
    int ok;

    fx_init();

    self->setNextLevelBonus(0);

    // PRESERVED: path is overwritten before it is read.
    sprintf(path, GS_OPEN_FMT_GAM, self->gameFileName());

    // The peek: load the next level's map just to read its bonus flag.
    if (self->restartCount() == 0 &&
        (unsigned int)(self->levelIndex()) + 1 != (unsigned int)self->levelCount()) {
        Sim_SetCurrentLevelName(self, (unsigned char)(self->levelIndex() + 1));
        sprintf(path, GS_OPEN_FMT_LEVELS, g_gameDir,
                           self->levelName());
        // PRESERVED: the result is not tested.
        self->map()->readFile(path);
        self->setNextLevelBonus(self->map()->bonus());

        if (s_diag)
            g_logger.write("levelparse: bonus peek for level %u -> bonus=%u\n",
                      (unsigned)(unsigned char)(self->levelIndex() + 1),
                      self->nextLevelBonus());
    }

    Sim_SetCurrentLevelName(self, levelNo);
    sprintf(path, GS_OPEN_FMT_LEVELS, g_gameDir,
                       self->levelName());

    // Saved before the read (see the top of the file).
    if (!s_fx_samelevel)
        inline_strcpy(prev, self->map()->mapName());

    ok = self->map()->readFile(path);

    if (s_fx_samelevel)
        inline_strcpy(prev, self->map()->mapName());

    if ((char)ok != 0) {
        self->extraObjects()->releaseSounds();
        g_logger.logMessage(1, "GAME: level (Bonus=%d) loaded by Number (%d): %s.jjm",
                           self->map()->bonus(),
                           levelNo & 0xff, path);
    } else {
        g_logger.logMessage(4, "GAME: ** error ** could not load level by Number (%d) (maybe it does not exist\077): %s.jjm",
                           levelNo & 0xff, path);
        windev::quit(1);
    // PRESERVED: carries on, as in ParseLevelFiles.
    }

    self->setMapChanged(0);
    if (inline_strcmp((const unsigned char *)prev,
                      (const unsigned char *)self->map()->mapName()) != 0)
        self->setMapChanged(1);

    sprintf(path, GS_OPEN_FMT_SCRIPTS, g_gameDir,
                       self->levelName());
    self->scriptPlayer()->setLoaded(0);
    self->scriptPlayer()->readForLevel(path);

    g_logger.logMessage(1,
                       self->scriptPlayer()->loaded()
                           ? "GAME: instruction-script loaded:%s.jjs" : "GAME: could not load instruction-script:%s.jjs ,running in observation-mode only...",
                       path);

    s_opens++;
    if (s_diag)
        g_logger.write("levelparse: DIAG open #%u level=%u name=\"%s\" map=%s "
                  "changed=%u script=%u released=%u\n",
                  s_opens, levelNo & 0xff, self->levelName(),
                  (char)ok ? "ok" : "FAILED",
                  self->mapChanged(),
                  self->scriptPlayer()->loaded(), ExtraObjects::releasedCount());

    return 0;
}
