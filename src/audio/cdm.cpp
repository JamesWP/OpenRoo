#include <windows.h>
#include <mmsystem.h>
#include <string.h>
#include <stdio.h>
#include "cdm.h"
#include "log.h"

CDM* CDM::construct()
{
    log_write("CDM::construct(this=%p)\n", this);
    vtable        = const_cast<void*>(CDM_VTABLE);
    track_count   = 0;
    memset(tracks, 0, sizeof(tracks));
    notify_hwnd   = 0;
    log_write("CDM::construct done\n");
    return this;
}

void CDM::destruct()
{
    log_write("CDM::destruct(this=%p)\n", this);
    stopTrack();
    vtable = const_cast<void*>(CDM_VTABLE);
    log_write("CDM::destruct done\n");
}

int CDM::getTrackCount()
{
    log_write("CDM::getTrackCount → 9\n");
    return 9;  // original CD has 9 tracks (1 data + 8 audio); FUN_403420 checks for exactly 9
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
    strncpy(mci_buf, s, sizeof(mci_buf) - 1);
    mci_buf[sizeof(mci_buf) - 1] = '\0';
    if (out_ptr) *out_ptr = mci_buf;
    log_write("CDM::getTrackLength(track=%d) → \"%s\"\n", track, s);
    return 1;
}

void CDM::playTrack(int from, int to)
{
    log_write("CDM::playTrack(from=%d, to=%d, notify_hwnd=0x%lX)\n", from, to, notify_hwnd);

    // Stop and close any currently open alias before opening a new one.
    mciSendStringA("stop km", NULL, 0, NULL);
    mciSendStringA("close km", NULL, 0, NULL);

    track_to      = (BYTE)to;
    current_track = (BYTE)from;

    // CD track 2 → CDTracks\Track 1.wav, track 3 → Track 2.wav, …
    int wav = from - 1;
    if (wav < 1 || wav > 8) {
        log_write("CDM::playTrack: track %d out of WAV range\n", from);
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
    // MM_MCINOTIFY: on MCI_NOTIFY_SUCCESSFUL it re-calls CDM::PlayTrack with
    // the saved current_track, so looping is driven by the game's message pump.
    mciSendStringA("play km notify", NULL, 0, (HWND)(UINT_PTR)notify_hwnd);
    log_write("CDM::playTrack done\n");
}

void CDM::stopTrack()
{
    log_write("CDM::stopTrack\n");
    // Zero track_to so any pending MM_MCINOTIFY that slips through the
    // wParam==MCI_NOTIFY_SUCCESSFUL check does not re-trigger playback.
    track_to = 0;
    mciSendStringA("stop km", NULL, 0, NULL);
    mciSendStringA("close km", NULL, 0, NULL);
    log_write("CDM::stopTrack done\n");
}

/* ─── Exports — thin thiscall wrappers so patch.py import names resolve ─── */
extern "C" {

__declspec(dllexport) void KarooHooksLoad() {}

__declspec(dllexport) CDM* __attribute__((thiscall))
CDM_Constructor(CDM *self) { return self->construct(); }

__declspec(dllexport) void __attribute__((thiscall))
CDM_Destructor(CDM *self) { self->destruct(); }

__declspec(dllexport) int __attribute__((thiscall))
CDM_GetTrackCount(CDM *self) { return self->getTrackCount(); }

__declspec(dllexport) int __attribute__((thiscall))
CDM_GetTrackLength(CDM *self, char **out_ptr, int track) { return self->getTrackLength(out_ptr, track); }

__declspec(dllexport) void __attribute__((thiscall))
CDM_PlayTrack(CDM *self, int from, int to) { self->playTrack(from, to); }

__declspec(dllexport) void __attribute__((thiscall))
CDM_StopTrack(CDM *self) { self->stopTrack(); }

} // extern "C"
