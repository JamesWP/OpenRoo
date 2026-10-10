/* Game's lifecycle: the constructor, the destructor and the sound release.
 * Each member is built and torn down by its owner (cdthemes.cpp, theme.cpp,
 * soundmanager.cpp, scriptplayer.cpp, config.cpp), in declaration order;
 * teardown is its exact reverse.  World (world.h), the FrontEnd (frontend.h)
 * and the Session (session.h) are declared after the
 * sound manager and the fixed sounds, so they are torn down before them; their
 * own members (levelmap.cpp, extraobjects.cpp, player.cpp, menutree.cpp,
 * textentry.cpp, highscores.cpp, saveslots.cpp) are built and torn down by
 * World and FrontEnd.
 */

#include "inputdev.h"
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

Game::Game()
{
    world_.setSoundLibrary(&soundManager_);
}

Game::~Game() {}


/* Releases only while sound is up; soundsLoaded is cleared either way. */
void Game::releaseAllSounds()
{
    SoundManager *sm = soundManager();
    if (soundCreated() != 0) {
        Player *p = world()->player();
        audiodev::Buffer *statics1[] = {
            fixedSounds_.timeOut, fixedSounds_.lastSeconds,
            fixedSounds_.count,   fixedSounds_.switchClick,
        };
        for (audiodev::Buffer *s : statics1)
            if (s) sm->releaseStaticForOwner(s, 1);
        if (fixedSounds_.menuUpDown)
            sm->releasePooledForOwner(fixedSounds_.menuUpDown, 1);
        if (p->soundC3()) sm->releaseVoice(p->soundC3(), true);
        if (p->soundBf()) sm->releaseVoice(p->soundBf(), true);
        if (p->poolCf())  sm->releaseVoice(p->poolCf(), true);
        if (fixedSounds_.levelCompleted)
            sm->releaseStaticForOwner(fixedSounds_.levelCompleted, 1);
        SoundVoice *statics2[] = {
            p->soundC7(), p->soundA3(), p->soundB3(), p->soundB7(),
            p->soundBb(), p->soundAb(), p->soundAf(), p->soundCb(), p->soundA7(),
        };
        for (SoundVoice *s : statics2)
            if (s) sm->releaseVoice(s, true);
        if (p->pool9f())
            sm->releaseVoice(p->pool9f(), true);
        // The crystal banks and the pickup banks, one entry of each per pass.
        // PRESERVED: bank 5 is never released.
        static const int banks[] = { 0, 1, 2, 3, 4, 6, 7, 8 };
        for (int i = 0; i < 3; i++) {
            if (fixedSounds_.crystalBank[i])
                sm->releaseStaticForOwner(fixedSounds_.crystalBank[i], 1);
            for (int b : banks)
                if (p->pickupSound(b, i))
                    sm->releaseVoice(p->pickupSound(b, i), true);
        }
    }
    fixedSounds_.loaded = 0;
}

