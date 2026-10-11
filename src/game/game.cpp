/* Game's lifecycle: the constructor, the destructor and the sound release.
 * Each member is built and torn down by its owner (cdthemes.cpp, theme.cpp,
 * soundmanager.cpp, scriptplayer.cpp, config.cpp), in declaration order;
 * teardown is its exact reverse.  GameAudio (gameaudio.h) is declared before
 * World (world.h), the FrontEnd (frontend.h) and the Session (session.h), so
 * they are torn down before the sound.  Their own members (levelmap.cpp,
 * extraobjects.cpp, player.cpp, menutree.cpp, textentry.cpp, highscores.cpp,
 * saveslots.cpp) are built and torn down by World and FrontEnd.
 */

#include "keycodes.h"
#include <string>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "windev.h"
#include "gamestr.h"
#include "logger.h"
#include <stdlib.h>
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
    world_.setSoundLibrary(audio_.soundManager());
}

Game::~Game() {}


/* Releases only while sound is up; soundsLoaded is cleared either way. */
void Game::releaseAllSounds()
{
    SoundManager *sm = audio()->soundManager();
    FixedSounds *fixed = audio()->fixedSounds();
    if (audio()->soundCreated() != 0) {
        Player *p = world()->player();
        audiodev::Buffer *statics1[] = {
            fixed->timeOut, fixed->lastSeconds,
            fixed->count,   fixed->switchClick,
        };
        for (audiodev::Buffer *s : statics1)
            if (s) sm->releaseStaticForOwner(s, 1);
        if (fixed->menuUpDown)
            sm->releasePooledForOwner(fixed->menuUpDown, 1);
        if (p->soundC3()) sm->releaseVoice(p->soundC3(), true);
        if (p->soundBf()) sm->releaseVoice(p->soundBf(), true);
        if (p->poolCf())  sm->releaseVoice(p->poolCf(), true);
        if (fixed->levelCompleted)
            sm->releaseStaticForOwner(fixed->levelCompleted, 1);
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
            if (fixed->crystalBank[i])
                sm->releaseStaticForOwner(fixed->crystalBank[i], 1);
            for (int b : banks)
                if (p->pickupSound(b, i))
                    sm->releaseVoice(p->pickupSound(b, i), true);
        }
    }
    fixed->loaded = 0;
}

