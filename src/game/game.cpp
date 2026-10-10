/* Game's lifecycle: the constructor, the destructor, the game-file loader and
 * the sound release.  Each member is built and torn down by its owner
 * (menutree.cpp, cdthemes.cpp, theme.cpp, extraobjects.cpp, soundmanager.cpp,
 * textentry.cpp, highscores.cpp, saveslots.cpp, player.cpp, scriptplayer.cpp,
 * config.cpp, levelmap.cpp), in a fixed order; teardown is its exact reverse.
 */

#include "inputdev.h"
#include <fstream>
#include <string>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "windev.h"
#include "gamestr.h"
#include "logger.h"
#include <stdlib.h>
#include "cdm.h"
#include "cdthemes.h"
#include "extraobjects.h"
#include "soundmanager.h"
#include "audiodev.h"
#include "voicepool.h"
#include "player.h"
#include "liftobject.h"
#include "platformobject.h"
#include "fallingtile.h"
#include "bridgeobject.h"
#include "foe.h"
#include "bomb.h"
#include <algorithm>
#include <iterator>
#include "sysdev.h"

Game::Game() {}

Game::~Game() {}


/* "<gamedir>\<name>.gam", opened in text mode even for the binary form.
 * FORMAT: binary is a 6,6,6,<levels> header, then every byte minus 5 into the
 * flat level-name table.  Text is one name per line, up to a line starting
 * '*', which is stored and counted, then uncounted.  PRESERVED: the EOF test
 * comes before the read, so the last, failing read decodes the byte it left
 * behind a second time. */
int Game::loadGameFile(const char *name)
{
    char path[0x80] = { 0 };
    unsigned char hdr[4];

    levelCount_ = 0;
    snprintf(path, sizeof(path), GS_GAME_FILE_PATH, gameDir_, name);
    g_logger.logMessage(2, "GAME: load game-file: %s", path);
    sysdev::TextFile in(path);
    if (!in)
        return 0;
    in.read(reinterpret_cast<char *>(hdr), 4);

    char *table = &levelNameTable_[0][0];
    if (hdr[0] == 6 && hdr[1] == 6 && hdr[2] == 6) {
        g_logger.logMessage(2, "GAME: load game-file as binary,coded file");
        levelCount_ = hdr[3];
        unsigned int n = 0;
        char c = 0;
        while (!in.eof()) {
            in.read(&c, 1);
            c = (char)(c - 5);
            table[n & 0xffff] = c;
            n++;
        }
        g_logger.logMessage(2, "GAME: game-file loaded %d levels included %d bytes loaded",
                           (unsigned)levelCount_, n & 0xffff);
        for (unsigned short i = 0; i < levelCount_; i++)
            g_logger.logMessage(2, "GAME: game-file: %s", levelNameTable_[i]);
        return 1;
    }

    g_logger.logMessage(2, "GAME: load game-file as text-file");
    unsigned int n = 0;
    in.clear();
    in.seekg(0);
    std::string line;
    while (!in.eof() && (line.empty() || line[0] != '*')) {
        if (!std::getline(in, line))
            line.clear();  // nothing read: an empty name
        strcpy(levelNameTable_[n & 0xffff], line.c_str());
        n++;
    }
    levelCount_ = (unsigned char)(n - 1);
    g_logger.logMessage(2, "GAME: game-file loaded %d levels included", (unsigned)levelCount_);
    return 1;
}

/* Releases only while sound is up; soundsLoaded is cleared either way. */
void Game::releaseAllSounds()
{
    SoundManager *sm = soundManager();
    if (soundCreated() != 0) {
        Player *p = player();
        audiodev::Buffer *statics1[] = {
            fixedSounds_.timeOut, fixedSounds_.lastSeconds,
            fixedSounds_.count,   fixedSounds_.switchClick,
        };
        for (audiodev::Buffer *s : statics1)
            if (s) sm->releaseStaticForOwner(s, 1);
        if (fixedSounds_.menuUpDown)
            sm->releasePooledForOwner(fixedSounds_.menuUpDown, 1);
        if (p->soundC3()) sm->releaseStaticForOwner(p->soundC3(), 1);
        if (p->soundBf()) sm->releaseStaticForOwner(p->soundBf(), 1);
        if (p->poolCf())  sm->releasePooledForOwner(p->poolCf(), 1);
        if (fixedSounds_.levelCompleted)
            sm->releaseStaticForOwner(fixedSounds_.levelCompleted, 1);
        audiodev::Buffer *statics2[] = {
            p->soundC7(), p->soundA3(), p->soundB3(), p->soundB7(),
            p->soundBb(), p->soundAb(), p->soundAf(), p->soundCb(), p->soundA7(),
        };
        for (audiodev::Buffer *s : statics2)
            if (s) sm->releaseStaticForOwner(s, 1);
        if (p->pool9f())
            sm->releasePooledForOwner(p->pool9f(), 1);
        // The crystal banks and the pickup banks, one entry of each per pass.
        // PRESERVED: bank 5 is never released.
        static const int banks[] = { 0, 1, 2, 3, 4, 6, 7, 8 };
        for (int i = 0; i < 3; i++) {
            if (fixedSounds_.crystalBank[i])
                sm->releaseStaticForOwner(fixedSounds_.crystalBank[i], 1);
            for (int b : banks)
                if (p->pickupSound(b, i))
                    sm->releaseStaticForOwner(p->pickupSound(b, i), 1);
        }
    }
    fixedSounds_.loaded = 0;
}

