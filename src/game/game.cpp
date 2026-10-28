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
            fixed->lastSeconds,
            fixed->count,   fixed->switchClick,
        };
        for (audiodev::Buffer *s : statics1)
            if (s) sm->releaseStaticForOwner(s, 1);
        if (fixed->menuUpDown)
            sm->releasePooledForOwner(fixed->menuUpDown, 1);
        soundEvents_.entityDestroyed(SoundSource::Player, p->id());
        soundEvents_.entityDestroyed(SoundSource::Level, 0);
    }
    fixed->loaded = 0;
}

