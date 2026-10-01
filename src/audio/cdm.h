/* The CD audio device.  The game played its music as audio tracks from the CD;
 * here each track is a .wav in CDTracks/ (imported from the CD image), played
 * through the platform layer's music player.  One global instance, g_cdAudio. */
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

    /* The window that receives the music's end-of-track message. */
    void  setWindowHandle(void *hwnd);
    void *windowHandle() const { return window_; }

    /* Offered every window message by the window procedure; true if it was a
     * music message and has been handled. */
    bool  handleWindowMessage(unsigned msg, unsigned long wParam, long lParam);

    int   getTrackCount();
    int   getTrackLength(char **out_ptr, int track);
    void  playTrack(int tracknumber, bool repeat);
    void  stop();

    /* The CD's mixer volume.  The game has no mixer line: set does nothing and
     * get returns 0. */
    void  setMixerVolume(unsigned level);
    unsigned int getMixerDetails();

private:
    audiodev::Music music_;
    void           *window_;       // the game window
    char            mcibuff[256];  // holds the last track length returned
};
