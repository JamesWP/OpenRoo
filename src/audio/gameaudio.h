/* GameAudio: the game's sound, a sub-object of Game.  The sound manager that
 * plays every sound, the fixed sounds the front end and the play-through use
 * (the menu clicks, the countdown and the crystal banks), the theme's sound
 * table every level's sounds come from, and the CD player with its theme
 * tracks.  Game owns one.
 */
#pragma once
#include "cdm.h"
#include "cdthemes.h"
#include "soundmanager.h"
#include "themesounds.h"

/* The fixed sounds AcquireFixedSoundBuffersAndMaybeReport loads once.  They
 * are the game's, not the SoundManager's: only Game code writes them. */
namespace audiodev { class Buffer; }
class VoicePool;
struct FixedSounds {
    audiodev::Buffer *timeOut;
    audiodev::Buffer *switchClick;      /* menu select */
    VoicePool          *menuUpDown;       /* 5 voices */
    audiodev::Buffer *count;
    audiodev::Buffer *lastSeconds;      /* the countdown */
    audiodev::Buffer *levelCompleted;
    /* Three banks, 'A'..'C'; GameTick picks one by rand()%3 when the
     * crystals are complete. */
    audiodev::Buffer *crystalBank[3];
    /* Set once the load has run (or found no sound); it never runs again. */
    unsigned int        loaded;
};

class GameAudio {
public:
    GameAudio() { cdThemes_.setCdAudio(&cdAudio_); }
    GameAudio(const GameAudio &) = delete;
    GameAudio &operator=(const GameAudio &) = delete;

    SoundManager *soundManager()     { return &soundManager_; }
    /* Nonzero once sound is up. */
    int  soundCreated() const        { return (int)soundManager_.created(); }
    FixedSounds  *fixedSounds()      { return &fixedSounds_; }

    /* The theme sound entry for a slot, which the spawns and level sounds
     * acquire. */
    const SoundAssetName *themeSound(ThemeSound s) const { return themeSounds_.entry(s); }
    ThemeSoundTable      *themeSounds()             { return &themeSounds_; }

    /* The CD audio device (cdm.h); the CD-music theme table plays on it. */
    CDM            *cdAudio()                        { return &cdAudio_; }
    /* The CD-music theme table (cdthemes.h). */
    CdThemes       *cdThemes()                       { return &cdThemes_; }

    /* GameTick runs InitLevelBasedSounds while this is 0, then sets it;
     * Load, SetupLevelObjects and the 3D-sound toggle clear it. */
    int            levelSoundsReady() const          { return levelSoundsReady_; }
    void           setLevelSoundsReady(int r)        { levelSoundsReady_ = r; }

private:
    /* The CD audio device (cdm.h).  Declared before the theme table, which
     * plays on it, so it is torn down after the table. */
    CDM           cdAudio_;
    CdThemes      cdThemes_;
    int           levelSoundsReady_{};
    ThemeSoundTable themeSounds_;
    SoundManager  soundManager_;
    FixedSounds   fixedSounds_{};
};
