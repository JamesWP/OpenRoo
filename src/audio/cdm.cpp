#include <windows.h>
#include <mmsystem.h>
#include <string.h>
#include <stdio.h>
#include "cdm.h"
#include "log.h"

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
    return 9;  // original CD has 9 tracks (1 data + 8 audio); ValidateCDTrackLengths checks for exactly 9
}

static const char *track_len(int track)
{
    static const char *lens[] = {
        /* 0 */ NULL,
        /* 1 */ "04:38:71",  // not compared; overwritten by track 2
        /* 2 */ "04:38:71",  // VA 0x464418
        /* 3 */ "02:59:12",  // VA 0x46440C
        /* 4 */ "03:28:39",  // VA 0x464400
        /* 5 */ "02:46:68",  // VA 0x4643F4
        /* 6 */ "03:08:54",  // VA 0x4643E8
        /* 7 */ "03:32:60",  // VA 0x4643DC
        /* 8 */ "04:00:74",  // VA 0x4643D0
        /* 9 */ "02:17:74",  // VA 0x4643C4
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

    // Stop and close any currently open alias before opening a new one.
    mciSendStringA("stop km", NULL, 0, NULL);
    mciSendStringA("close km", NULL, 0, NULL);

    tracknumber = track;
    repeat      = loop;

    // CD track 2 → CDTracks\Track 1.wav, track 3 → Track 2.wav, …
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

    // Play non-blocking with notify. The game's WndProc (0x42CE20) handles
    // MM_MCINOTIFY: on MCI_NOTIFY_SUCCESSFUL it checks CDM::repeat and if set
    // re-calls CDM::PlayTrack with CDM::tracknumber, driving the loop.
    mciSendStringA("play km notify", NULL, 0, windowhandle);
    log_write("CDM::playTrack done\n");
}

void CDM::stop()
{
    log_write("CDM::stop\n");
    // Zero repeat so any pending MM_MCINOTIFY does not re-trigger playback.
    repeat = false;
    mciSendStringA("stop km", NULL, 0, NULL);
    mciSendStringA("close km", NULL, 0, NULL);
    log_write("CDM::stop done\n");
}

// Volume control: the game calls this via CDM_SetMixerVolume (patched call sites
// at 0x402ED0). MCI waveaudio exposes no per-alias volume API so this is a no-op.
void CDM::setMixerVolume(DWORD level)
{
    log_write("CDM::setMixerVolume(level=0x%lX) — not implemented\n", level);
}

/* ─── Exports — thin thiscall wrappers so patch.py import names resolve ─── */
extern "C" {

__declspec(dllexport) void KarooHooksLoad() {}

__declspec(dllexport) CDM* __attribute__((thiscall))
CDM_Constructor(CDM *self) { return self->construct(); }

__declspec(dllexport) void __attribute__((thiscall))
CDM_Destructor(CDM *self) { self->stopAndClose(); }

__declspec(dllexport) void __attribute__((thiscall))
CDM_SetWindowHandle(CDM *self, HWND hwnd) { self->setWindowHandle(hwnd); }

__declspec(dllexport) int __attribute__((thiscall))
CDM_GetTrackCount(CDM *self) { return self->getTrackCount(); }

__declspec(dllexport) int __attribute__((thiscall))
CDM_GetTrackLength(CDM *self, char **out_ptr, int track) { return self->getTrackLength(out_ptr, track); }

__declspec(dllexport) void __attribute__((thiscall))
CDM_PlayTrack(CDM *self, int track, bool loop) { self->playTrack(track, loop); }

__declspec(dllexport) void __attribute__((thiscall))
CDM_StopTrack(CDM *self) { self->stop(); }

__declspec(dllexport) void __attribute__((thiscall))
CDM_SetMixerVolume(CDM *self, DWORD level) { self->setMixerVolume(level); }

} // extern "C"
