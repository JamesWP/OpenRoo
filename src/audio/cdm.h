/* The CD audio device.  The game played its music as audio tracks from the CD;
 * here each track is a .wav in CDTracks/ (imported from the CD image), played
 * through MCI waveaudio under the alias "km".  The window gets MM_MCINOTIFY
 * when a track ends, and a repeating track is restarted from there (main.cpp).
 * One global instance, g_cdAudio. */
#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <stddef.h>

/* One mixer line's volume control.  Nothing fills these, so the mixer paths do
 * nothing. */
struct CDVolumeControl {
    HMIXEROBJ hmixer;
    DWORD     dwVolumeControlID;
};

/* The device.  Packed, so the track number is unaligned. */
class CDM {
public:
    CDM*  construct();
    void  stopAndClose();
    void  setWindowHandle(HWND hwnd);
    int   getTrackCount();
    int   getTrackLength(char **out_ptr, int track);
    void  playTrack(int tracknumber, bool repeat);
    void  stop();
    void  setMixerVolume(DWORD level);
    /* The first mixer's volume value, or 0 with no mixer or on any error. */
    unsigned int getMixerDetails();

    HWND windowHandle() const { return windowhandle; }
    bool repeating() const    { return repeat; }
    int  track() const        { return tracknumber; }

    /* The vtable slots: the scalar deleting destructor and the two track
     * queries. */
    static CDM *scalarDeletingDtor(CDM *self, unsigned int flags);
    static int vtGetTrackCount(CDM *self);
    static int vtGetTrackLength(CDM *self, char **out_ptr, int track);

private:
 

    void            *vtable;
    DWORD            nummixers;     // always 0
    CDVolumeControl  mixers[10];    // unused
    HWND             windowhandle;  // receives MM_MCINOTIFY
    char             mcibuff[256];  // holds the last track length returned
    bool             repeat;        // restart the track when it ends
    int              tracknumber;   // the CD track playing: 2..9
};

 
extern const void *const CDM_VTABLE;
