/* The CD audio device.  The game played its music as audio tracks from the CD;
 * here each track is a .wav in CDTracks/ (imported from the CD image), played
 * through the platform layer's music player.  GameAudio owns the one instance
 * (gameaudio.h). */
#pragma once
#include <stddef.h>
#include "audiodev.h"

class CDM {
public:
    CDM();
    /* Stops the music (and clears the repeat flag). */
    virtual ~CDM();
    CDM(const CDM &) = delete;
    CDM &operator=(const CDM &) = delete;

    int   getTrackCount();
    const char *getTrackLength(int track);
    void  playTrack(int tracknumber, bool repeat);
    void  stop();

    /* The music's volume, 0 (silent) to 1 (full). */
    void  setVolume(float gain);

private:
    audiodev::Music music_;
};
