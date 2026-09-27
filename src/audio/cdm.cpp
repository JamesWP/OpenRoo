#include <windows.h>
#include <mmsystem.h>
#include <string.h>
#include <stdio.h>
#include "cdm.h"
#include "log.h"
#include <stdlib.h>

CDM* CDM::construct()
{
    log_write("CDM::construct(this=%p)\n", this);
    vtable       = const_cast<void*>(CDM_VTABLE);
    nummixers    = 0;
    memset(mixers, 0, sizeof(mixers));
    windowhandle = NULL;
    repeat       = false;
    tracknumber  = 0;
    log_write("CDM::construct done\n");
    return this;
}

void CDM::stopAndClose()
{
    log_write("CDM::stopAndClose(this=%p)\n", this);
    vtable = const_cast<void*>(CDM_VTABLE);
    repeat = false;
    mciSendStringA("stop km", NULL, 0, NULL);
    mciSendStringA("close km", NULL, 0, NULL);
    log_write("CDM::stopAndClose done\n");
}

void CDM::setWindowHandle(HWND hwnd)
{
    log_write("CDM::setWindowHandle(hwnd=0x%p)\n", hwnd);
    windowhandle = hwnd;
}

int CDM::getTrackCount()
{
    log_write("CDM::getTrackCount → 9\n");
    return 9;  // the CD has 9 tracks, 1 data and 8 audio; the check wants exactly 9
}

/* The CD's track lengths, which the game checks to recognise its CD. */
static const char *track_len(int track)
{
    static const char *lens[] = {
         NULL,
         "04:38:71",  // never compared
         "04:38:71",
         "02:59:12",
         "03:28:39",
         "02:46:68",
         "03:08:54",
         "03:32:60",
         "04:00:74",
         "02:17:74",
    };
    if (track < 1 || track > 9) return "00:00:00";
    return lens[track];
}

int CDM::getTrackLength(char **out_ptr, int track)
{
    const char *s = track_len(track);
    strncpy(mcibuff, s, sizeof(mcibuff) - 1);
    mcibuff[sizeof(mcibuff) - 1] = '\0';
    if (out_ptr) *out_ptr = mcibuff;
    log_write("CDM::getTrackLength(track=%d) → \"%s\"\n", track, s);
    return 1;
}

void CDM::playTrack(int track, bool loop)
{
    log_write("CDM::playTrack(track=%d, loop=%d, windowhandle=0x%p)\n", track, (int)loop, windowhandle);

    // Close whatever is open before opening the new track.
    mciSendStringA("stop km", NULL, 0, NULL);
    mciSendStringA("close km", NULL, 0, NULL);

    tracknumber = track;
    repeat      = loop;

    // CD track 2 is CDTracks\Track 1.wav, and so on.
    int wav = track - 1;
    if (wav < 1 || wav > 8) {
        log_write("CDM::playTrack: track %d out of WAV range\n", track);
        return;
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "open \"CDTracks\\Track %d.wav\" type waveaudio alias km", wav);
    MCIERROR err = mciSendStringA(cmd, NULL, 0, NULL);
    if (err) {
        log_write("CDM::playTrack: open failed err=%lu\n", err);
        return;
    }

    // Plays without blocking; the window procedure gets MM_MCINOTIFY when it
    // ends and restarts it if repeat is set.
    mciSendStringA("play km notify", NULL, 0, windowhandle);
    log_write("CDM::playTrack done\n");
}

void CDM::stop()
{
    log_write("CDM::stop\n");
    // Clear repeat so a pending notification does not restart it.
    repeat = false;
    mciSendStringA("stop km", NULL, 0, NULL);
    mciSendStringA("close km", NULL, 0, NULL);
    log_write("CDM::stop done\n");
}

/* MCI waveaudio has no per-alias volume, so this does nothing. */
void CDM::setMixerVolume(DWORD level)
{
    log_write("CDM::setMixerVolume(level=0x%lX) — not implemented\n", level);
}

extern "C" __declspec(dllexport) void KarooHooksLoad() {}  // unused

/* Frees on bit 0; the one CDM is a global, so it never does. */
CDM *__attribute__((thiscall)) CDM::scalarDeletingDtor(CDM *self, unsigned int flags)
{
    self->stopAndClose();
    if (flags & 1)
        free(self);
    return self;
}

int __attribute__((thiscall)) CDM::vtGetTrackCount(CDM *self)
{
    return self->getTrackCount();
}

int __attribute__((thiscall)) CDM::vtGetTrackLength(CDM *self, char **out_ptr, int track)
{
    return self->getTrackLength(out_ptr, track);
}

/* No stack argument.  The first mixer's volume value, or 0 with no mixer or on
 * any error. */
unsigned int CDM::getMixerDetails()
{
    MIXERCONTROLDETAILS d;
    DWORD value;

    if (nummixers == 0)
        return 0;
    d.cbStruct = 0x18;
    d.dwControlID = mixers[0].dwVolumeControlID;
    d.cChannels = 1;
    d.hwndOwner = NULL;
    d.cbDetails = 4;
    d.paDetails = &value;
    MMRESULT r = mixerGetControlDetailsA((HMIXEROBJ)mixers[0].hmixer, &d,
                                         0x80000000);
    return r != 0 ? 0 : (unsigned int)value;
}

static void *const cdm_vtable_slots[3] = {
    (void *)&CDM::scalarDeletingDtor,
    (void *)&CDM::vtGetTrackCount,
    (void *)&CDM::vtGetTrackLength,
};
extern const void *const CDM_VTABLE = cdm_vtable_slots;
