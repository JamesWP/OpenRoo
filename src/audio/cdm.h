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
struct CDM {
    void            *vtable;
    DWORD            nummixers;     // always 0
    CDVolumeControl  mixers[10];    // unused
    HWND             windowhandle;  // receives MM_MCINOTIFY
    char             mcibuff[256];  // holds the last track length returned
    bool             repeat;        // restart the track when it ends
    int              tracknumber;   // the CD track playing: 2..9

    CDM*  construct();
    void  stopAndClose();
    void  setWindowHandle(HWND hwnd);
    int   getTrackCount();
    int   getTrackLength(char **out_ptr, int track);
    void  playTrack(int tracknumber, bool repeat);
    void  stop();
    void  setMixerVolume(DWORD level);
} __attribute__((packed));

static_assert(offsetof(CDM, windowhandle) == 0x58,  "CDM layout mismatch");
static_assert(offsetof(CDM, mcibuff)      == 0x5C,  "CDM layout mismatch");
static_assert(offsetof(CDM, repeat)       == 0x15C, "CDM layout mismatch");
static_assert(offsetof(CDM, tracknumber)  == 0x15D, "CDM layout mismatch");

/* The three-slot vtable: deleting destructor, track count, track length. */
extern const void *const CDM_VTABLE;

int CDM_GetTrackCount(CDM *self);
int CDM_GetTrackLength(CDM *self, char **out_ptr, int track);

/* The first mixer's volume value, or 0 with no mixer or on any error. */
unsigned int CDM_GetMixerDetails(CDM *self);
void CDM_SetMixerVolume(CDM *self, DWORD level);
void CDM_StopTrack(CDM *self);
CDM *CDM_ScalarDeletingDtor(CDM *self, unsigned int flags);

/* The window MM_MCINOTIFY is posted to. */
void CDM_SetWindowHandle(CDM *self, HWND hwnd);

/* Plays CD track 2..9, which is CDTracks\Track <track-1>.wav.  loop sets
 * repeat. */
void CDM_PlayTrack(CDM *self, int track, bool loop);

/* Construction and destruction of the global instance, driven by
 * staticinit.cpp. */
CDM *CDM_Constructor(CDM *self);
void CDM_Destructor(CDM *self);
